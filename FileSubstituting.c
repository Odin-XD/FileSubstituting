#include <fltKernel.h>

#define TARGET_FILENAME       L"FileSubstituting.sys"
#define SUBSTITUTE_FILENAME   L"\\??\\C:\\Windows\\System32\\drivers\\ntfs.sys"

PFLT_FILTER g_Filter = NULL;

static UNICODE_STRING g_TargetName = RTL_CONSTANT_STRING(TARGET_FILENAME);
static UNICODE_STRING g_SubstituteName = RTL_CONSTANT_STRING(SUBSTITUTE_FILENAME);

static BOOLEAN IsTargetFile(_In_ PFILE_OBJECT FileObject)
{
    if (!FileObject || !FileObject->FileName.Buffer)
        return FALSE;

    UNICODE_STRING fileName = FileObject->FileName;
    PWCH lastBackslash = NULL;
    
    for (USHORT i = 0; i < fileName.Length / sizeof(WCHAR); i++) {
        if (fileName.Buffer[i] == L'\\') {
            lastBackslash = &fileName.Buffer[i + 1];
        }
    }
    
    if (lastBackslash) {
        UNICODE_STRING finalComponent;
        finalComponent.Buffer = lastBackslash;
        finalComponent.Length = (USHORT)((fileName.Buffer + fileName.Length / sizeof(WCHAR)) - lastBackslash) * sizeof(WCHAR);
        finalComponent.MaximumLength = finalComponent.Length;
        
        return RtlEqualUnicodeString(&finalComponent, &g_TargetName, TRUE);
    }
    
    return RtlEqualUnicodeString(&fileName, &g_TargetName, TRUE);
}

static FLT_PREOP_CALLBACK_STATUS PreCreate(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, _Out_ PVOID* CompletionContext)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(CompletionContext);

    if (IoGetTopLevelIrp() == (PIRP)FSRTL_FSP_TOP_LEVEL_IRP)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    if (Data->Iopb->MajorFunction != IRP_MJ_CREATE)
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    PFLT_FILE_NAME_INFORMATION nameInfo = NULL;
    NTSTATUS status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo);

    if (!NT_SUCCESS(status))
        return FLT_PREOP_SUCCESS_NO_CALLBACK;

    status = FltParseFileNameInformation(nameInfo);
    if (!NT_SUCCESS(status)) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    BOOLEAN isMatch = RtlEqualUnicodeString(&nameInfo->FinalComponent, &g_TargetName, TRUE);
    if (!isMatch) {
        FltReleaseFileNameInformation(nameInfo);
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    FltReleaseFileNameInformation(nameInfo);

    USHORT substLen = g_SubstituteName.Length;
    PWCH newBuf = (PWCH)ExAllocatePoolWithTag(NonPagedPoolNx, substLen, 'pRFF');

    if (!newBuf) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    RtlCopyMemory(newBuf, g_SubstituteName.Buffer, substLen);

    PFILE_OBJECT fileObject = Data->Iopb->TargetFileObject;

    if (fileObject->FileName.Buffer)
        ExFreePool(fileObject->FileName.Buffer);

    fileObject->FileName.Buffer = newBuf;
    fileObject->FileName.Length = substLen;
    fileObject->FileName.MaximumLength = substLen;

    Data->IoStatus.Status = STATUS_REPARSE;
    Data->IoStatus.Information = IO_REPARSE;

    FltSetCallbackDataDirty(Data);

    return FLT_PREOP_COMPLETE;
}

static NTSTATUS FilterUnload(_In_ FLT_FILTER_UNLOAD_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(Flags);
    FltUnregisterFilter(g_Filter);
    return STATUS_SUCCESS;
}

static NTSTATUS InstanceSetup(_In_ PCFLT_RELATED_OBJECTS FltObjects, _In_ FLT_INSTANCE_SETUP_FLAGS Flags, _In_ DEVICE_TYPE VolumeDeviceType, _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeDeviceType);
    return (VolumeFilesystemType == FLT_FSTYPE_NTFS) ? STATUS_SUCCESS : STATUS_FLT_DO_NOT_ATTACH;
}

static NTSTATUS InstanceQueryTeardown(_In_ PCFLT_RELATED_OBJECTS FltObjects, _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_SUCCESS;
}

static const FLT_OPERATION_REGISTRATION g_Callbacks[] = {
    { IRP_MJ_CREATE, 0, PreCreate, NULL },
    { IRP_MJ_OPERATION_END }
};

static const FLT_REGISTRATION g_FilterRegistration = {
    sizeof(FLT_REGISTRATION),
    FLT_REGISTRATION_VERSION,
    0,
    NULL,
    g_Callbacks,
    FilterUnload,
    InstanceSetup,
    InstanceQueryTeardown,
    NULL, NULL, NULL, NULL, NULL
};

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status = FltRegisterFilter(DriverObject, &g_FilterRegistration, &g_Filter);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = FltStartFiltering(g_Filter);
    if (!NT_SUCCESS(status)) {
        FltUnregisterFilter(g_Filter);
        return status;
    }

    return STATUS_SUCCESS;
}