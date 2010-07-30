/*
 * arch/arm/mach-tegra/nvrm/core/common/nvrm_moduleloader.c
 *
 * AVP firmware module loader
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#define NV_ENABLE_DEBUG_PRINTS 0

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <asm/io.h>

#include "nvcommon.h"
#include "nvassert.h"
#include "nvos.h"
#include "nvutil.h"
#include "nvrm_hardware_access.h"
#include "nvrm_message.h"
#include "nvrm_rpc.h"
#include "nvrm_moduleloader.h"
#include "nvrm_moduleloader_private.h"
#include "nvrm_graphics_private.h"
#include "nvrm_structure.h"
#include "nvfw.h"

#define DEVICE_NAME "nvfw"

static const struct firmware *s_FwEntry;
static NvRmRPCHandle s_RPCHandle = NULL;
static NvError SendMsgDetachModule(NvRmLibraryHandle  hLibHandle);
static NvError SendMsgAttachModule(NvRmLibraryHandle *hLibHandle,
				void* pArgs,
				NvU32 sizeOfArgs);
NvU32 NvRmModuleGetChipId(NvRmDeviceHandle hDevice);
NvError NvRmPrivInitModuleLoaderRPC(NvRmDeviceHandle hDevice);
void NvRmPrivDeInitModuleLoaderRPC(void);

#define ADD_MASK                   0x00000001
#define SUB_MASK                   0xFFFFFFFD
// For the elf to be relocatable, we need atleast 2 program segments
// Although even elfs with more than 1 program segment may not be relocatable.
#define MIN_SEGMENTS_FOR_DYNAMIC_LOADING    2

static int nvfw_open(struct inode *inode, struct file *file);
static int nvfw_close(struct inode *inode, struct file *file);
static long nvfw_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static ssize_t nvfw_write(struct file *, const char __user *, size_t, loff_t *);

static const struct file_operations nvfw_fops =
{
	.owner		= THIS_MODULE,
	.open		= nvfw_open,
	.release	= nvfw_close,
	.write		= nvfw_write,
	.unlocked_ioctl = nvfw_ioctl,
};

static struct miscdevice nvfw_dev =
{
	.name	= DEVICE_NAME,
	.fops	= &nvfw_fops,
	.minor	= MISC_DYNAMIC_MINOR,
};

// FIXME: This function is just for debugging.
ssize_t nvfw_write(struct file *file, const char __user *buff, size_t count, loff_t *offp)
{
	NvRmDeviceHandle hRmDevice;
	NvRmLibraryHandle hRmLibHandle;
	char filename[100];
	int error;

	printk(KERN_INFO "%s: entry\n", __func__);

	error = copy_from_user(filename, buff, count);
	if (error)
		panic("%s: line=%d\n", __func__, __LINE__);
	filename[count] = 0;
	printk(KERN_INFO "%s: filename=%s\n", __func__, filename);

	error = NvRmOpen( &hRmDevice, 0 );
	if (error)
		panic("%s: line=%d\n", __func__, __LINE__);

	error = NvRmLoadLibrary(hRmDevice, filename, NULL, 0, &hRmLibHandle);
	if (error)
		panic("%s: line=%d\n", __func__, __LINE__);

	printk(KERN_INFO "%s: return\n", __func__);
	return count;
}

int nvfw_open(struct inode *inode, struct file *file)
{
	return 0;
}

int nvfw_close(struct inode *inode, struct file *file)
{
	return 0;
}

static int nvfw_ioctl_load_library(struct file *filp, void __user *arg)
{
	struct nvfw_load_handle op;
	NvRmDeviceHandle hRmDevice;
	NvRmLibraryHandle hRmLibHandle;
	char *filename = NULL;
	void *args = NULL;
	int error;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	filename = NvOsAlloc(op.length + 1);
	error = copy_from_user(filename, op.filename, op.length + 1);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;
	printk(KERN_INFO "%s: filename=%s\n", __func__, filename);

	args = NvOsAlloc(op.argssize);
	error = copy_from_user(args, op.args, op.argssize);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	error = NvRmOpen( &hRmDevice, 0 );
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	error = NvRmLoadLibrary(hRmDevice, filename, args, op.argssize, &hRmLibHandle);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	op.handle = hRmLibHandle;
	error = copy_to_user(arg, &op, sizeof(op));

error_exit:
	NvOsFree(filename);
	NvOsFree(args);
	return error;
}

static int nvfw_ioctl_load_library_ex(struct file *filp, void __user *arg)
{
	struct nvfw_load_handle op;
	NvRmDeviceHandle hRmDevice;
	NvRmLibraryHandle hRmLibHandle;
	char *filename = NULL;
	void *args = NULL;
	int error;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	filename = NvOsAlloc(op.length + 1);
	error = copy_from_user(filename, op.filename, op.length + 1);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;
	printk(KERN_INFO "%s: filename=%s\n", __func__, filename);

	args = NvOsAlloc(op.argssize);
	error = copy_from_user(args, op.args, op.argssize);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	error = NvRmOpen( &hRmDevice, 0 );
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	error = NvRmLoadLibraryEx(hRmDevice, filename, args, op.argssize, op.greedy, &hRmLibHandle);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	op.handle = hRmLibHandle;
	error = copy_to_user(arg, &op, sizeof(op));

error_exit:
	NvOsFree(filename);
	NvOsFree(args);
	return error;
}

static int nvfw_ioctl_free_library(struct file *filp, void __user *arg)
{
	struct nvfw_load_handle op;
	NvRmDeviceHandle hRmDevice;
	int error;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	error = NvRmOpen( &hRmDevice, 0 );
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	error = NvRmFreeLibrary(op.handle);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

error_exit:
	return error;
}

static int nvfw_ioctl_get_proc_address(struct file *filp, void __user *arg)
{
	struct nvfw_get_proc_address_handle op;
	NvRmDeviceHandle hRmDevice;
	char *symbolname;
	int error;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	symbolname = NvOsAlloc(op.length + 1);
	error = copy_from_user(symbolname, op.symbolname, op.length + 1);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;
	printk(KERN_INFO "%s: symbolname=%s\n", __func__, symbolname);

	error = NvRmOpen( &hRmDevice, 0 );
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	error = NvRmGetProcAddress(op.handle, symbolname, &op.address);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	if (error) goto error_exit;

	error = copy_to_user(arg, &op, sizeof(op));

error_exit:
	NvOsFree(symbolname);
	return error;
}

static long nvfw_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	int err = 0;
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case NVFW_IOC_LOAD_LIBRARY:
		err = nvfw_ioctl_load_library(filp, uarg);
		break;
	case NVFW_IOC_LOAD_LIBRARY_EX:
		err = nvfw_ioctl_load_library_ex(filp, uarg);
		break;
	case NVFW_IOC_FREE_LIBRARY:
		err = nvfw_ioctl_free_library(filp, uarg);
		break;
	case NVFW_IOC_GET_PROC_ADDRESS:
		err = nvfw_ioctl_get_proc_address(filp, uarg);
		break;
	default:
		return -ENOTTY;
	}
	return err;
}

static NvError
PrivateOsFopen(const char *filename, NvU32 flags, PrivateOsFileHandle *file)
{
	PrivateOsFileHandle hFile;

	hFile = NvOsAlloc(sizeof(PrivateOsFile));
	if (hFile == NULL) {
		return NvError_InsufficientMemory;
	}

	NvOsDebugPrintf("%s <kernel impl>: file=%s\n", __func__, filename);
	NvOsDebugPrintf("%s <kernel impl>: calling request_firmware()\n", __func__);
	if (request_firmware(&s_FwEntry, filename, nvfw_dev.this_device) != 0) {
		printk(KERN_ERR "%s: Cannot read firmware '%s'\n", __func__, filename);
		return NvError_FileReadFailed;
	}
	NvOsDebugPrintf("%s <kernel impl>: back from request_firmware()\n", __func__);
	hFile->pstart = s_FwEntry->data;
	hFile->pread = s_FwEntry->data;
	hFile->pend = s_FwEntry->data + s_FwEntry->size;

	*file = hFile;

	return NvError_Success;
}

static void
PrivateOsFclose(PrivateOsFileHandle hFile)
{
	release_firmware(s_FwEntry);
	NV_ASSERT(hFile);
	NvOsFree(hFile);
}

static NvError
PrivateOsFread(
	PrivateOsFileHandle hFile,
	void *ptr,
	size_t size,
	size_t *bytes)
{
	size_t nBytesRead = size;
	NvError err = NvError_Success;

	if (hFile->pread >= hFile->pend) {
		nBytesRead = 0;
		err = NvError_EndOfFile;
		goto epilogue;
	}

	else if (hFile->pread + size > hFile->pend) {
		nBytesRead = hFile->pend - hFile->pread;
		NvOsMemcpy(ptr, hFile->pread, nBytesRead);
		err = NvError_EndOfFile;
		goto epilogue;
	}

	else {
		NvOsMemcpy(ptr, hFile->pread, nBytesRead);
		goto epilogue;
	}

epilogue:
	hFile->pread += nBytesRead;
	*bytes = nBytesRead;
	return err;
}

static NvError
PrivateOsFseek(PrivateOsFileHandle file, NvS64 offset, NvOsSeekEnum whence)
{
	NV_ASSERT(whence == NvOsSeek_Set);
	file->pread = file->pstart + (NvU32)offset;

	return NvError_Success;
}

NvError
NvRmPrivLoadKernelLibrary(NvRmDeviceHandle hDevice,
			const char *pLibName,
			NvRmLibraryHandle *hLibHandle)
{
	NvError Error = NvSuccess;

	NvOsDebugPrintf("%s <kernel impl>: file=%s\n", __func__, pLibName);
	if ((Error = NvRmPrivLoadLibrary(hDevice, pLibName, 0, NV_FALSE,
							hLibHandle)) != NvSuccess)
	{
		return Error;
	}
	return Error;
}

NvError
NvRmLoadLibrary(NvRmDeviceHandle hDevice,
                const char *pLibName,
                void* pArgs,
                NvU32 sizeOfArgs,
                NvRmLibraryHandle *hLibHandle)
{
	NvError Error = NvSuccess;
	NV_ASSERT(sizeOfArgs <= MAX_ARGS_SIZE);

	NvOsDebugPrintf("%s <kernel impl>: file=%s\n", __func__, pLibName);
	Error = NvRmLoadLibraryEx(hDevice, pLibName, pArgs, sizeOfArgs, NV_FALSE,
				hLibHandle);
	return Error;
}

NvError
NvRmLoadLibraryEx(NvRmDeviceHandle hDevice,
                const char *pLibName,
                void* pArgs,
                NvU32 sizeOfArgs,
                NvBool IsApproachGreedy,
                NvRmLibraryHandle *hLibHandle)
{
	NvError Error = NvSuccess;
	NV_ASSERT(sizeOfArgs <= MAX_ARGS_SIZE);

	NvOsDebugPrintf("%s <kernel impl>: file=%s\n", __func__, pLibName);

	/* NvRmPrivInitModuleLoaderRPC(hDevice); */
	if ((Error = NvRmPrivInitAvp(hDevice)) != NvSuccess)
	{
		return Error;
	}

	if ((Error = NvRmPrivLoadLibrary(hDevice, pLibName, 0, IsApproachGreedy,
							hLibHandle)) != NvSuccess)
	{
		return Error;
	}

	if ((Error = NvRmPrivRPCConnect(s_RPCHandle)) == NvSuccess)
	{
		Error = SendMsgAttachModule(hLibHandle, pArgs, sizeOfArgs);
	}
	else
	{
		NvOsDebugPrintf("RPCConnect timedout during NvRmLoadLibraryEx\r\n");
	}
	if (Error)
	{
		NvRmPrivFreeLibrary(*hLibHandle);
	}
	return Error;
}

NvError
NvRmGetProcAddress(NvRmLibraryHandle Handle,
		const char *pSymbol,
		void **pSymAddress)
{
	NvError Error = NvSuccess;
	NV_ASSERT(Handle);
	Error = NvRmPrivGetProcAddress(Handle, pSymbol, pSymAddress);
	return Error;
}

NvError NvRmFreeLibrary(NvRmLibraryHandle hLibHandle)
{
	NvError Error = NvSuccess;
	NV_ASSERT(hLibHandle);
	if((Error = NvRmPrivRPCConnect(s_RPCHandle)) == NvSuccess)
	{
		Error = SendMsgDetachModule(hLibHandle);
	}
	if (Error == NvSuccess)
	{
		Error = NvRmPrivFreeLibrary(hLibHandle);
	}

	return Error;
}

NvU32 NvRmModuleGetChipId(NvRmDeviceHandle hDevice)
{
	typedef struct
	{
		NvU32 Id;
	}Capabilities;

	NvError Error = NvSuccess;
	NvRmModuleCapability caps[3];
	Capabilities Cap15, Cap20,Cap16,*capabilities;

	// for AP20
	Cap20.Id = 0x20;
	caps[0].MajorVersion = 1;
	caps[0].MinorVersion = 1;
	caps[0].EcoLevel = 0;
	caps[0].Capability = (void *)&Cap20;

	//for AP15 A01
	Cap15.Id = 0x15;
	caps[1].MajorVersion = 1;
	caps[1].MinorVersion = 0;
	caps[1].EcoLevel = 0;
	caps[1].Capability = (void *)&Cap15;

	//for AP15 A02
	Cap16.Id = 0x15;
	caps[2].MajorVersion = 1;
	caps[2].MinorVersion = 1;
	caps[2].EcoLevel = 0;
	caps[2].Capability = (void *)&Cap16;

	Error = NvRmModuleGetCapabilities(hDevice, NvRmModuleID_BseA, caps, 3, (void **)&capabilities);

	return capabilities->Id;
}

//before unloading loading send message to avp with args and entry point via transport
static NvError SendMsgDetachModule(NvRmLibraryHandle hLibHandle)
{
	NvError Error = NvSuccess;
	NvU32 RecvMsgSize;
	NvRmMessage_DetachModule Msg;
	NvRmMessage_DetachModuleResponse MsgR;
	void *address = NULL;

	Msg.msg = NvRmMsg_DetachModule;

	if ((Error = NvRmGetProcAddress(hLibHandle, "main", &address)) != NvSuccess)
	{
		goto exit_gracefully;
	}
	Msg.msg = NvRmMsg_DetachModule;
	Msg.reason = NvRmModuleLoaderReason_Detach;
	Msg.entryAddress = (NvU32)address;
	RecvMsgSize = sizeof(NvRmMessage_DetachModuleResponse);
	NvRmPrivRPCSendMsgWithResponse(s_RPCHandle,
				&MsgR,
				RecvMsgSize,
				&RecvMsgSize,
				&Msg,
				sizeof(Msg));

	Error = MsgR.error;
	if (Error)
	{
		goto exit_gracefully;
	}
exit_gracefully:
	return Error;
}

//after successful loading send message to avp with args and entry point via transport
static NvError SendMsgAttachModule(NvRmLibraryHandle *hLibHandle,
				void* pArgs,
				NvU32 sizeOfArgs)
{
	NvError Error = NvSuccess;
	NvU32 RecvMsgSize;
	NvRmMessage_AttachModule *MsgPtr=NULL;
	NvRmMessage_AttachModuleResponse MsgR;
	void *address = NULL;

	MsgPtr = NvOsAlloc(sizeof(*MsgPtr));
	if(MsgPtr==NULL)
	{
		Error = NvError_InsufficientMemory;
		goto exit_gracefully;
	}
	MsgPtr->msg = NvRmMsg_AttachModule;

	if(pArgs)
	{
		NvOsMemcpy(MsgPtr->args, pArgs, sizeOfArgs);
	}

	MsgPtr->size = sizeOfArgs;
	if ((Error = NvRmGetProcAddress(*hLibHandle, "main", &address)) != NvSuccess)
	{
		goto exit_gracefully;
	}
	MsgPtr->entryAddress = (NvU32)address;
	MsgPtr->reason = NvRmModuleLoaderReason_Attach;
	RecvMsgSize = sizeof(NvRmMessage_AttachModuleResponse);
	NvRmPrivRPCSendMsgWithResponse(s_RPCHandle,
				&MsgR,
				RecvMsgSize,
				&RecvMsgSize,
				MsgPtr,
				sizeof(*MsgPtr));

	Error = MsgR.error;
	if (Error)
	{
		goto exit_gracefully;
	}
exit_gracefully:
	NvOsFree(MsgPtr);
	return Error;
}


NvError NvRmPrivInitModuleLoaderRPC(NvRmDeviceHandle hDevice)
{
	NvError err = NvSuccess;

	// Run only once.
	if (s_RPCHandle) return NvError_Success;

	NvOsDebugPrintf("%s <kernel impl>: NvRmPrivRPCInit(RPC_AVP_PORT)\n", __func__);
	err = NvRmPrivRPCInit(hDevice, "RPC_AVP_PORT", &s_RPCHandle);
	if (err) panic("%s: NvRmPrivRPCInit FAILED\n", __func__);

	return err;
}

void NvRmPrivDeInitModuleLoaderRPC()
{
	NvRmPrivRPCDeInit(s_RPCHandle);
}

SegmentNode* AddToSegmentList(SegmentNode *pList,
			NvRmMemHandle pRegion,
			Elf32_Phdr Phdr,
			NvU32 Idx,
			NvU32 PhysAddr,
			void* MapAddress)
{
	SegmentNode *TempRec = NULL;
	SegmentNode *CurrentRec = NULL;

	TempRec = NvOsAlloc(sizeof(SegmentNode));
	if (TempRec != NULL)
	{
		TempRec->pLoadRegion = pRegion;
		TempRec->Index = Idx;
		TempRec->VirtualAddr = Phdr.p_vaddr;
		TempRec->MemorySize = Phdr.p_memsz;
		TempRec->FileOffset = Phdr.p_offset;
		TempRec->FileSize = Phdr.p_filesz;
		TempRec->LoadAddress = PhysAddr;
		TempRec->MapAddr = MapAddress;
		TempRec->Next = NULL;

		CurrentRec = pList;
		if (CurrentRec == NULL)
		{
			pList = TempRec;
		}
		else
		{
			while (CurrentRec->Next != NULL)
			{
				CurrentRec = CurrentRec->Next;
			}
			CurrentRec->Next = TempRec;
		}
	}
	return pList;
}
void RemoveRegion(SegmentNode *pList)
{
	if (pList != NULL)
	{
		SegmentNode *pCurrentRec;
		SegmentNode *pTmpRec;
		pCurrentRec = pList;
		while (pCurrentRec != NULL)
		{
			NvRmMemUnpin(pCurrentRec->pLoadRegion);
			NvRmMemHandleFree(pCurrentRec->pLoadRegion);
			pCurrentRec->pLoadRegion = NULL;
			pTmpRec = pCurrentRec;
			pCurrentRec = pCurrentRec->Next;
			NvOsFree( pTmpRec );
		}
		pList = NULL;
	}
}

void UnMapRegion(SegmentNode *pList)
{
	if (pList != NULL)
	{
		SegmentNode *pCurrentRec;
		pCurrentRec = pList;
		while (pCurrentRec != NULL && pCurrentRec->MapAddr )
		{
			NvRmMemUnmap(pCurrentRec->pLoadRegion, pCurrentRec->MapAddr,
				pCurrentRec->MemorySize);
			pCurrentRec = pCurrentRec->Next;
		}
	}
}

NvError
ApplyRelocation(SegmentNode *pList,
                NvU32 FileOffset,
                NvU32 SegmentOffset,
                NvRmMemHandle pRegion,
                const Elf32_Rel *pRel)
{
	NvError Error = NvSuccess;
	NvU8 Type = 0;
	NvU32 SymIndex = 0;
	Elf32_Word Word = 0;
	SegmentNode *pCur;
	NvU32 TargetVirtualAddr = 0;
	NvU32 LoadAddress = 0;
	NV_ASSERT(NULL != pRel);

	NvRmMemRead(pRegion, SegmentOffset,&Word, sizeof(Word));
	NV_DEBUG_PRINTF(("NvRmMemRead: SegmentOffset 0x%04x, word %p\r\n",
					SegmentOffset, Word));
	Type = ELF32_R_TYPE(pRel->r_info);

	switch (Type)
	{
	case R_ARM_NONE:
		break;
	case R_ARM_CALL:
		break;
	case R_ARM_RABS32:
		SymIndex = ELF32_R_SYM(pRel->r_info);
		if (pList != NULL)
		{
			pCur = pList;
			while (pCur != NULL)
			{
				if (pCur->Index == (SymIndex - 1))
				{
					TargetVirtualAddr = pCur->VirtualAddr;
					LoadAddress = pCur->LoadAddress;
				}
				pCur = pCur->Next;
			}
			if (LoadAddress > TargetVirtualAddr)
			{
				Word = Word + (LoadAddress - TargetVirtualAddr);
			}
			else //handle negative displacement
			{
				Word = Word - (TargetVirtualAddr - LoadAddress);
			}
			NV_DEBUG_PRINTF(("NvRmMemWrite: SegmentOffset 0x%04x, word %p\r\n",
							SegmentOffset, Word));
			NvRmMemWrite(pRegion, SegmentOffset, &Word, sizeof(Word));
		}
		break;
	default:
		Error = NvError_NotSupported;
		NV_DEBUG_PRINTF(("This relocation type is not handled = %d\r\n", Type));
		break;
	}
	return Error;
}

NvError
GetSpecialSectionName(Elf32_Word SectionType,
		Elf32_Word SectionFlags,
		const char** SpecialSectionName)
{
	const char *unknownSection = "Unknown\r\n";
	*SpecialSectionName = unknownSection;
	/// Mask off the high 16 bits for now
	switch (SectionFlags & 0xffff)
	{
	case SHF_ALLOC|SHF_WRITE:
		if (SectionType == SHT_NOBITS)
			*SpecialSectionName = ".bss\r\n";
		else if (SectionType == SHT_PROGBITS)
			*SpecialSectionName = ".data\r\n";
		else if (SectionType == SHT_FINI_ARRAY)
			*SpecialSectionName = ".fini_array\r\n";
		else if (SectionType == SHT_INIT_ARRAY)
			*SpecialSectionName = ".init_array\r\n";
		break;
	case SHF_ALLOC|SHF_EXECINSTR:
		if (SectionType == SHT_PROGBITS)
			*SpecialSectionName = ".init or fini \r\n";

		break;
	case SHF_ALLOC:
		if (SectionType == SHT_STRTAB)
			*SpecialSectionName = ".dynstr\r\n";
		else if (SectionType == SHT_DYNSYM)
			*SpecialSectionName = ".dynsym\r\n";
		else if (SectionType == SHT_HASH)
			*SpecialSectionName = ".hash\r\n";
		else if (SectionType == SHT_PROGBITS)
			*SpecialSectionName = ".rodata\r\n";
		else
			*SpecialSectionName = unknownSection;
		break;
	default:
		if (SectionType == SHT_PROGBITS)
			*SpecialSectionName = ".comment\r\n";
		else
			*SpecialSectionName = unknownSection;
		break;
	}
	return NvSuccess;
}

NvError
ParseDynamicSegment(SegmentNode *pList,
		const char* pSegmentData,
		size_t SegmentSize,
		NvU32 DynamicSegmentOffset)
{
	NvError Error = NvSuccess;
	Elf32_Dyn* pDynSeg = NULL;
	NvU32  Counter = 0;
	NvU32 RelocationTableAddressOffset = 0;
	NvU32 RelocationTableSize = 0;
	NvU32 RelocationEntrySize = 0;
	const Elf32_Rel* RelocationTablePtr = NULL;
	NvU32 SymbolTableAddressOffset = 0;
	NvU32 SymbolTableEntrySize = 0;
	NvU32 SymbolTableSize = 0;
	NvU32 SegmentOffset = 0;
	NvU32 FileOffset = 0;
	SegmentNode *node;
#if NV_ENABLE_DEBUG_PRINTS
	// Strings for interpreting ELF header e_type field.
	static const char * s_DynSecTypeText[] =
		{
			"DT_NULL",
			"DT_NEEDED",
			"DT_PLTRELSZ",
			"DT_PLTGOT",
			"DT_HASH",
//        "DT_STRTAB",
			"String Table Address",
//        "DT_SYMTAB",
			"Symbol Table Address",
//        "DT_RELA",
			"Relocation Table Address",
//        "DT_RELASZ",
			"Relocation Table Size",
//        "DT_RELAENT",
			"Relocation Entry Size",
//        "DT_STRSZ",
			"String Table Size",
//        "DT_SYMENT",
			"Symbol Table Entry Size",
			"DT_INIT",
			"DT_FINI",
			"DT_SONAME",
			"DT_RPATH",
			"DT_SYMBOLIC",
//        "DT_REL",
			"Relocation Table Address",
//        "DT_RELSZ",
			"Relocation Table Size",
//        "DT_RELENT",
			"Relocation Entry Size",
			"DT_PLTREL",
			"DT_DEBUG",
			"DT_TEXTREL",
			"DT_JMPREL",
			"DT_BIND_NOW",
			"DT_INIT_ARRAY",
			"DT_FINI_ARRAY",
			"DT_INIT_ARRAYSZ",
			"DT_FINI_ARRAYSZ",
			"DT_RUNPATH",
			"DT_FLAGS",
			"DT_ENCODING",
			"DT_PREINIT_ARRAY",
			"DT_PREINIT_ARRAYSZ",
			"DT_NUM",
			"DT_OS-specific",
			"DT_PROC-specific",
			""
		};
#else
#define s_DynSecTypeText ((char**)0)
#endif

	pDynSeg = (Elf32_Dyn*)pSegmentData;
	do
	{
		if (pDynSeg->d_tag < DT_NUM)
		{
			NV_DEBUG_PRINTF(("Entry %d with Tag %s %d\r\n",
							Counter++, s_DynSecTypeText[pDynSeg->d_tag], pDynSeg->d_val));
		}
		else
		{
			NV_DEBUG_PRINTF(("Entry %d Special Compatibility Range %x %d\r\n",
							Counter++, pDynSeg->d_tag, pDynSeg->d_val));
		}
		if (pDynSeg->d_tag == DT_NULL)
			break;
		if ((pDynSeg->d_tag == DT_REL) || (pDynSeg->d_tag == DT_RELA))
			RelocationTableAddressOffset = pDynSeg->d_un.d_val;
		if ((pDynSeg->d_tag == DT_RELENT) || (pDynSeg->d_tag == DT_RELAENT))
			RelocationEntrySize = pDynSeg->d_un.d_val;
		if ((pDynSeg->d_tag == DT_RELSZ) || (pDynSeg->d_tag == DT_RELASZ))
			RelocationTableSize = pDynSeg->d_un.d_val;
		if (pDynSeg->d_tag == DT_SYMTAB)
			SymbolTableAddressOffset = pDynSeg->d_un.d_val;
		if (pDynSeg->d_tag == DT_SYMENT)
			SymbolTableEntrySize = pDynSeg->d_un.d_val;
		if (pDynSeg->d_tag == DT_ARM_RESERVED1)
			SymbolTableSize = pDynSeg->d_un.d_val;
		pDynSeg++;

	}while ((Counter*sizeof(Elf32_Dyn)) < SegmentSize);

	if (RelocationTableAddressOffset && RelocationTableSize && RelocationEntrySize)
	{
		RelocationTablePtr = (const Elf32_Rel*)&pSegmentData[RelocationTableAddressOffset];

		for (Counter = 0; Counter < (RelocationTableSize/RelocationEntrySize); Counter++)
		{
			//calculate the actual offset of the reloc entry
			NV_DEBUG_PRINTF(("Reloc %d offset is %x RType %d SymIdx %d \r\n",
							Counter, RelocationTablePtr->r_offset,
							ELF32_R_TYPE(RelocationTablePtr->r_info),
							ELF32_R_SYM(RelocationTablePtr->r_info)));

			node = pList;
			while (node != NULL)
			{
				if ( (RelocationTablePtr->r_offset > node->VirtualAddr) &&
					(RelocationTablePtr->r_offset <=
						(node->VirtualAddr + node->MemorySize)))
				{
					FileOffset = node->FileOffset +
						(RelocationTablePtr->r_offset - node->VirtualAddr);

					NV_DEBUG_PRINTF(("File offset to be relocated %d \r\n", FileOffset));

					SegmentOffset = (RelocationTablePtr->r_offset - node->VirtualAddr);

					NV_DEBUG_PRINTF(("Segment offset to be relocated %d \r\n", SegmentOffset));

					Error = ApplyRelocation(pList, FileOffset, SegmentOffset,
								node->pLoadRegion, RelocationTablePtr);

				}
				node = node->Next;
			}
			RelocationTablePtr++;
		}

	}
	if (SymbolTableAddressOffset && SymbolTableSize && SymbolTableEntrySize)
	{
#if 0
		const Elf32_Sym* SymbolTablePtr = NULL;
		SymbolTablePtr = (const Elf32_Sym*)&pSegmentData[SymbolTableAddressOffset];
		for (Counter = 0; Counter <SymbolTableSize/SymbolTableEntrySize; Counter++)
		{

			NvOsDebugPrintf("Symbol name %x, value %x, size %x, info %x, other %x, shndx %x\r\n",
					SymbolTablePtr->st_name, SymbolTablePtr->st_value,
					SymbolTablePtr->st_size, SymbolTablePtr->st_info,
					SymbolTablePtr->st_other, SymbolTablePtr->st_shndx);
			NV_DEBUG_PRINTF(("Symbol name %x, value %x, size %x, info %x, other %x, shndx %x\r\n",
							SymbolTablePtr->st_name, SymbolTablePtr->st_value,
							SymbolTablePtr->st_size, SymbolTablePtr->st_info,
							SymbolTablePtr->st_other, SymbolTablePtr->st_shndx));

			SymbolTablePtr++;
		}
#endif
	}
	return Error;
}

NvError
LoadLoadableProgramSegment(PrivateOsFileHandle elfSourceHandle,
			NvRmDeviceHandle hDevice,
			NvRmLibraryHandle hLibHandle,
			Elf32_Phdr Phdr,
			Elf32_Ehdr Ehdr,
			const NvRmHeap * Heaps,
			NvU32 NumHeaps,
			NvU32 loop,
			const char *Filename,
			SegmentNode **segmentList)
{
	NvError Error = NvSuccess;
	NvRmMemHandle pLoadRegion = NULL;
	void* pLoadAddress = NULL;
	NvU32 offset = 0;
	NvU32 addr;  // address of pLoadRegion
	size_t bytesRead = 0;

	Error = NvRmMemHandleCreate(hDevice,
                                &pLoadRegion,
                                Phdr.p_memsz);

	if (Error != NvSuccess)
		goto CleanUpExit;

	Error = NvRmMemAlloc(pLoadRegion,
			Heaps,
			NumHeaps,
			NV_MAX(16, Phdr.p_align),
			NvOsMemAttribute_Uncached);

	if (Error != NvSuccess)
	{
		NV_DEBUG_PRINTF(("Memory Allocation %d Failed\r\n",Error));
		NvRmMemHandleFree(pLoadRegion);
		pLoadRegion = NULL;
		goto CleanUpExit;
	}
	addr = NvRmMemPin(pLoadRegion);

	Error = NvRmMemMap(pLoadRegion, 0, Phdr.p_memsz,
			NVOS_MEM_READ_WRITE, &pLoadAddress);
	if (Error != NvSuccess)
	{
		pLoadAddress = NULL;
	}

	// This will initialize ZI to zero
	if( pLoadAddress )
	{
		NvOsMemset(pLoadAddress, 0, Phdr.p_memsz);
	}
	else
	{
		NvU8 *tmp = NvOsAlloc( Phdr.p_memsz );
		if( !tmp )
		{
			goto CleanUpExit;
		}
		NvOsMemset( tmp, 0, Phdr.p_memsz );
		NvRmMemWrite( pLoadRegion, 0, tmp, Phdr.p_memsz );
		NvOsFree( tmp );
	}

	if(Phdr.p_filesz)
	{
		if( pLoadAddress )
		{
			if ((Error = PrivateOsFread(elfSourceHandle, pLoadAddress,
								Phdr.p_filesz, &bytesRead)) != NvSuccess)
			{
				NV_DEBUG_PRINTF(("File Read failed %d\r\n", bytesRead));
				goto CleanUpExit;
			}
		}
		else
		{
			NvU8 *tmp = NvOsAlloc( Phdr.p_filesz );
			if( !tmp )
			{
				goto CleanUpExit;
			}

			Error = PrivateOsFread( elfSourceHandle, tmp, Phdr.p_filesz,
					&bytesRead );
			if( Error != NvSuccess )
			{
				NvOsFree( tmp );
				goto CleanUpExit;
			}

			NvRmMemWrite( pLoadRegion, 0, tmp, Phdr.p_filesz );

			NvOsFree( tmp );
		}
	}
	if ((Ehdr.e_entry >= Phdr.p_vaddr)
		&& (Ehdr.e_entry < (Phdr.p_vaddr + Phdr.p_memsz)))
	{
		// Odd address indicates entry point is Thumb code.
		// The address needs to be masked with LSB before being invoked.
		if (addr > Phdr.p_vaddr)
		{
			offset = (addr - Phdr.p_vaddr) | ADD_MASK;
			hLibHandle->EntryAddress = (void*)(Ehdr.e_entry + offset);
		}
		else
		{
			offset = ((Phdr.p_vaddr - addr) | (ADD_MASK)) & (SUB_MASK);
			hLibHandle->EntryAddress = (void*)(Ehdr.e_entry - offset);
		}
		NV_DEBUG_PRINTF(("Load Address for %s segment %d:%x\r\n",
						Filename, loop, addr));
		NvOsDebugPrintf("Load Address for %s segment %d:%x\r\n",
				Filename, loop, addr);
	}

	*segmentList = AddToSegmentList((*segmentList), pLoadRegion, Phdr, loop,
					addr, pLoadAddress);

CleanUpExit:
	if (Error != NvSuccess)
	{
		if(pLoadRegion != NULL)
		{
			if( pLoadAddress )
			{
				NvRmMemUnmap(pLoadRegion, pLoadAddress, Phdr.p_memsz);
			}

			NvRmMemUnpin(pLoadRegion);
			NvRmMemHandleFree(pLoadRegion);
		}
	}
	return Error;
}

NvError
NvRmPrivLoadLibrary(NvRmDeviceHandle hDevice,
		const char *Filename,
		NvU32 Address,
		NvBool IsApproachGreedy,
		NvRmLibraryHandle *hLibHandle)
{
	NvError Error = NvSuccess;
	PrivateOsFileHandle elfSourceHandle = 0;
	size_t bytesRead = 0;
	Elf32_Ehdr elf;
	Elf32_Phdr progHeader;
	NvU32 loop = 0;
	char *dynamicSegementBuffer = NULL;
	int dynamicSegmentOffset = 0;
	int lastFileOffset = 0;
	SegmentNode *segmentList = NULL;
	NvRmHeap HeapProperty[2];
	NvU32 HeapSize = 0;

	NV_ASSERT(NULL != Filename);
	*hLibHandle = NULL;

	NvOsDebugPrintf("%s <kernel impl>: file=%s\n", __func__, Filename);
	if ((Error = PrivateOsFopen(Filename, NVOS_OPEN_READ,
						&elfSourceHandle)) != NvSuccess)
	{
		NV_DEBUG_PRINTF(("Elf source file Not found Error = %d\r\n", Error));
		NvOsDebugPrintf("Failed to load library %s, NvError=%d."
				" Make sure it is present on the device\r\n", Filename, Error);
		return Error;
	}
	if ((Error = PrivateOsFread(elfSourceHandle, &elf,
						sizeof(elf), &bytesRead)) != NvSuccess)
	{
		NV_DEBUG_PRINTF(("File Read size mismatch %d\r\n", bytesRead));
		goto CleanUpExit;
	}
	// Parse the elf headers and display information
	parseElfHeader(&elf);
	/// Parse the Program Segment Headers and display information
	if ((Error = parseProgramSegmentHeaders(elfSourceHandle, elf.e_phoff, elf.e_phnum)) != NvSuccess)
	{
		NV_DEBUG_PRINTF(("parseProgramSegmentHeaders failed %d\r\n", Error));
		goto CleanUpExit;
	}
	/// Parse the section Headers and display information
	if ((Error = parseSectionHeaders(elfSourceHandle, &elf)) != NvSuccess)
	{
		NV_DEBUG_PRINTF(("parseSectionHeaders failed %d\r\n", Error));
		goto CleanUpExit;
	}
	// allocate memory for handle....
	*hLibHandle = NvOsAlloc(sizeof(NvRmLibHandle));
	if (!*hLibHandle)
	{
		Error = NvError_InsufficientMemory;
		goto CleanUpExit;
	}

	if (elf.e_phnum && elf.e_phnum < MIN_SEGMENTS_FOR_DYNAMIC_LOADING)
	{
		if ((Error = loadSegmentsInFixedMemory(elfSourceHandle,
								&elf, 0, &(*hLibHandle)->pLibBaseAddress)) != NvSuccess)
		{
			NV_DEBUG_PRINTF(("LoadSegmentsInFixedMemory Failed %d\r\n", Error));
			goto CleanUpExit;
		}
		(*hLibHandle)->EntryAddress = (*hLibHandle)->pLibBaseAddress;
		return Error;
	}
	else if (elf.e_phnum)
	{
		if ((Error = PrivateOsFseek(elfSourceHandle,
							elf.e_phoff, NvOsSeek_Set)) != NvSuccess)
		{
			NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
			goto CleanUpExit;
		}
		lastFileOffset = elf.e_phoff;
		// load the IRAM mandatory  and DRAM mandatory sections first...
		for (loop = 0; loop < elf.e_phnum; loop++)
		{
			if ((Error = PrivateOsFread(elfSourceHandle, &progHeader,
								sizeof(Elf32_Phdr), &bytesRead)) != NvSuccess)
			{
				NV_DEBUG_PRINTF(("File Read failed %d\r\n", bytesRead));
				goto CleanUpExit;
			}
			lastFileOffset += bytesRead;
			if (progHeader.p_type == PT_LOAD)
			{
				NV_DEBUG_PRINTF(("Found load segment %d\r\n",loop));
				if ((Error = PrivateOsFseek(elfSourceHandle,
									progHeader.p_offset, NvOsSeek_Set)) != NvSuccess)
				{
					NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
					goto CleanUpExit;
				}
				if (progHeader.p_vaddr >= DRAM_MAND_ADDRESS && progHeader.p_vaddr < IRAM_PREF_EXT_ADDRESS)
				{


					if (progHeader.p_vaddr >= DRAM_MAND_ADDRESS && progHeader.p_vaddr < IRAM_MAND_ADDRESS)
					{
						HeapProperty[0] = NvRmHeap_ExternalCarveOut;
					}
					else if (progHeader.p_vaddr >= IRAM_MAND_ADDRESS)
					{
						HeapProperty[0] = NvRmHeap_IRam;
					}
					Error = LoadLoadableProgramSegment(elfSourceHandle, hDevice, (*hLibHandle),
                                                                        progHeader, elf, HeapProperty, 1, loop,
                                                                        Filename, &segmentList);
					if (Error != NvSuccess)
					{
						NV_DEBUG_PRINTF(("Unable to load segment %d \r\n", loop));
						goto CleanUpExit;
					}
				}
				if ((Error = PrivateOsFseek(elfSourceHandle,
									lastFileOffset, NvOsSeek_Set)) != NvSuccess)
				{
					NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
					goto CleanUpExit;
				}
			}
		}

		// now load the preferred and dynamic sections
		if ((Error = PrivateOsFseek(elfSourceHandle,
							elf.e_phoff, NvOsSeek_Set)) != NvSuccess)
		{
			NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
			goto CleanUpExit;
		}
		lastFileOffset = elf.e_phoff;
		for (loop = 0; loop < elf.e_phnum; loop++)
		{
			if ((Error = PrivateOsFread(elfSourceHandle, &progHeader,
								sizeof(Elf32_Phdr), &bytesRead)) != NvSuccess)
			{
				NV_DEBUG_PRINTF(("File Read failed %d\r\n", bytesRead));
				goto CleanUpExit;
			}
			lastFileOffset += bytesRead;
			if (progHeader.p_type == PT_LOAD)
			{
				NV_DEBUG_PRINTF(("Found load segment %d\r\n",loop));
				if ((Error = PrivateOsFseek(elfSourceHandle,
									progHeader.p_offset, NvOsSeek_Set)) != NvSuccess)
				{
					NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
					goto CleanUpExit;
				}
				if (progHeader.p_vaddr < DRAM_MAND_ADDRESS)
				{
					if (IsApproachGreedy == NV_FALSE)
					{
						HeapSize = 1;
						//conservative allocation - IRAM_PREF sections in DRAM.
						HeapProperty[0] = NvRmHeap_ExternalCarveOut;
					}
					else
					{
						// greedy allocation - IRAM_PREF sections in IRAM, otherwise fallback to DRAM
						HeapSize = 2;
						HeapProperty[0] = NvRmHeap_IRam;
						HeapProperty[1] = NvRmHeap_ExternalCarveOut;
					}
					Error = LoadLoadableProgramSegment(elfSourceHandle, hDevice,
                                                                        (*hLibHandle), progHeader, elf,
                                                                        HeapProperty, HeapSize, loop,
                                                                        Filename, &segmentList);
					if (Error != NvSuccess)
					{
						NV_DEBUG_PRINTF(("Unable to load segment %d \r\n", loop));
						goto CleanUpExit;
					}
				}
				else if (progHeader.p_vaddr >= IRAM_PREF_EXT_ADDRESS)
				{
					NvU32 Chipid = NvRmModuleGetChipId(hDevice);
					if(Chipid == 0x15 || Chipid == 0x16)
					{
						HeapSize = 1;
						//conservative allocation - IRAM_PREF_EXT sections in DRAM for AP15.
						HeapProperty[0] = NvRmHeap_ExternalCarveOut;
					}
					else if(Chipid >= 0x20)
					{
						if (IsApproachGreedy == NV_FALSE)
						{
							HeapSize = 1;
							//conservative allocation - IRAM_PREF sections in DRAM.
							HeapProperty[0] = NvRmHeap_ExternalCarveOut;
						}
						else
						{
							// greedy allocation - IRAM_PREF sections in IRAM, otherwise fallback to DRAM
							HeapSize = 2;
							HeapProperty[0] = NvRmHeap_IRam;
							HeapProperty[1] = NvRmHeap_ExternalCarveOut;
						}
					}
					Error = LoadLoadableProgramSegment(elfSourceHandle, hDevice,
                                                                        (*hLibHandle), progHeader, elf,
                                                                        HeapProperty, HeapSize, loop,
                                                                        Filename, &segmentList);
					if (Error != NvSuccess)
					{
						NV_DEBUG_PRINTF(("Unable to load segment %d \r\n", loop));
						goto CleanUpExit;
					}
				}
				if ((Error = PrivateOsFseek(elfSourceHandle,
									lastFileOffset, NvOsSeek_Set)) != NvSuccess)
				{
					NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
					goto CleanUpExit;
				}
			}
			if (progHeader.p_type != PT_DYNAMIC)
				continue;
			dynamicSegmentOffset = progHeader.p_offset;
			if ((Error = PrivateOsFseek(elfSourceHandle,
								dynamicSegmentOffset, NvOsSeek_Set)) != NvSuccess)
			{
				NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
				goto CleanUpExit;
			}
			dynamicSegementBuffer = NvOsAlloc(progHeader.p_filesz);
			if (dynamicSegementBuffer == NULL)
			{
				NV_DEBUG_PRINTF(("Memory Allocation %d Failed\r\n", progHeader.p_filesz));
				goto CleanUpExit;
			}
			if ((Error = PrivateOsFread(elfSourceHandle, dynamicSegementBuffer,
								progHeader.p_filesz, &bytesRead)) != NvSuccess)
			{
				NV_DEBUG_PRINTF(("File Read failed %d\r\n", bytesRead));
				goto CleanUpExit;
			}
			if ((Error = ParseDynamicSegment(
						segmentList,
						dynamicSegementBuffer,
						progHeader.p_filesz,
						dynamicSegmentOffset)) != NvSuccess)
			{
				NV_DEBUG_PRINTF(("Parsing and relocation of segment failed \r\n"));
				goto CleanUpExit;
			}
			(*hLibHandle)->pList = segmentList;
			if ((Error = PrivateOsFseek(elfSourceHandle,
								lastFileOffset, NvOsSeek_Set)) != NvSuccess)
			{
				NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
				goto CleanUpExit;
			}
		}
	}

CleanUpExit:
	{
		if (Error == NvSuccess)
		{
			UnMapRegion(segmentList);
			NvOsFree(dynamicSegementBuffer);
			PrivateOsFclose(elfSourceHandle);
		}
		else
		{
			RemoveRegion(segmentList);
			NvOsFree(dynamicSegementBuffer);
			PrivateOsFclose(elfSourceHandle);
			NvOsFree(*hLibHandle);
		}
	}
	return Error;
}

NvError NvRmPrivFreeLibrary(NvRmLibHandle *hLibHandle)
{
	NvError Error = NvSuccess;
	RemoveRegion(hLibHandle->pList);
	NvOsFree(hLibHandle);
	return Error;
}

void parseElfHeader(Elf32_Ehdr *elf)
{
	if (elf->e_ident[0] == ELF_MAG0)
	{
		NV_DEBUG_PRINTF(("File is elf Object File with Identification %c%c%c\r\n",
						elf->e_ident[1], elf->e_ident[2], elf->e_ident[3]));
		NV_DEBUG_PRINTF(("Type of ELF is %x\r\n", elf->e_type));
		//An object file conforming to this specification must have
		//the value EM_ARM (40, 0x28).
		NV_DEBUG_PRINTF(("Machine type of the file is %x\r\n", elf->e_machine));
		//Address of entry point for this file. bit 1:0
		//indicates if entry point is ARM or thum mode
		NV_DEBUG_PRINTF(("Entry point for this axf is %x\r\n", elf->e_entry));
		NV_DEBUG_PRINTF(("Version of the ELF is %d\r\n", elf->e_version));
		NV_DEBUG_PRINTF(("Program Table Header Offset %d\r\n", elf->e_phoff));
		NV_DEBUG_PRINTF(("Section Table Header Offset %d\r\n", elf->e_shoff));
		NV_DEBUG_PRINTF(("Elf Header size %d\r\n", elf->e_ehsize));
		NV_DEBUG_PRINTF(("Section Header's Size %d\r\n", elf->e_shentsize));
		NV_DEBUG_PRINTF(("Number of Section Headers %d\r\n", elf->e_shnum));
		NV_DEBUG_PRINTF(("String Table Section Header Index %d\r\n", elf->e_shstrndx));
		NV_DEBUG_PRINTF(("\r\n"));
	}
}

NvError parseProgramSegmentHeaders(PrivateOsFileHandle elfSourceHandle,
				NvU32 segmentHeaderOffset,
				NvU32 segmentCount)
{
	Elf32_Phdr progHeader;
	size_t bytesRead = 0;
	NvU32 loop = 0;
	NvError Error = NvSuccess;
	if (segmentCount)
	{
		NV_DEBUG_PRINTF(("Program Headers Found %d\r\n", segmentCount));
		if ((Error = PrivateOsFseek(elfSourceHandle,
							segmentHeaderOffset, NvOsSeek_Set)) != NvSuccess)
		{
			NV_DEBUG_PRINTF(("File Seek failed %d\r\n", Error));
			return Error;
		}

		for (loop = 0; loop < segmentCount; loop++)
		{
			if ((Error = PrivateOsFread(elfSourceHandle, &progHeader,
								sizeof(progHeader), &bytesRead)) != NvSuccess)
			{
				NV_DEBUG_PRINTF(("File Read failed %d\r\n", bytesRead));
				return Error;
			}

			NV_DEBUG_PRINTF(("Program %d Header type %d\r\n",
							loop, progHeader.p_type));
			NV_DEBUG_PRINTF(("Program %d Header offset %d\r\n",
							loop, progHeader.p_offset));
			NV_DEBUG_PRINTF(("Program %d Header Virtual Address %x\r\n",
							loop, progHeader.p_vaddr));
			NV_DEBUG_PRINTF(("Program %d Header Physical Address %x\r\n",
							loop, progHeader.p_paddr));
			NV_DEBUG_PRINTF(("Program %d Header Filesize %d\r\n",
							loop, progHeader.p_filesz));
			NV_DEBUG_PRINTF(("Program %d Header Memory Size %d\r\n",
							loop, progHeader.p_memsz));
			NV_DEBUG_PRINTF(("Program %d Header Flags %x\r\n",
							loop, progHeader.p_flags));
			NV_DEBUG_PRINTF(("Program %d Header alignment %d\r\n",
							loop, progHeader.p_align));
			NV_DEBUG_PRINTF(("\r\n"));
		}
	}
	return NvSuccess;
}

NvError
parseSectionHeaders(PrivateOsFileHandle elfSourceHandle, Elf32_Ehdr *elf)
{
	NvError Error = NvSuccess;
	NvU32 stringTableOffset = 0;
	Elf32_Shdr sectionHeader;
	size_t bytesRead = 0;
	NvU32 loop = 0;
	char* stringTable = NULL;
	const char *specialNamePtr = NULL;

	// Try to get to the string table so that we can get section names
	stringTableOffset = elf->e_shoff + (elf->e_shentsize * elf->e_shstrndx);

	NV_DEBUG_PRINTF(("String Table File Offset is %d\r\n", stringTableOffset));

	if ((Error = PrivateOsFseek(elfSourceHandle,
						stringTableOffset, NvOsSeek_Set)) != NvSuccess)
	{
		NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
		return Error;
	}
	if ((Error = PrivateOsFread(elfSourceHandle, &sectionHeader,
						sizeof(sectionHeader), &bytesRead)) != NvSuccess)
	{
		NV_DEBUG_PRINTF(("File Read failed %d\r\n", bytesRead));
		return Error;
	}
	if (sectionHeader.sh_type == SHT_STRTAB)
	{
		NV_DEBUG_PRINTF(("Found Section is string Table\r\n"));
		if (sectionHeader.sh_size)
		{
			stringTable = NvOsAlloc(sectionHeader.sh_size);
			if (stringTable == NULL)
			{
				NV_DEBUG_PRINTF(("String table mem allocation failed for %d\r\n",
								sectionHeader.sh_size));
				return  NvError_InsufficientMemory;
			}
			if ((Error = PrivateOsFseek(elfSourceHandle,
								sectionHeader.sh_offset, NvOsSeek_Set)) != NvSuccess)
			{
				NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
				goto CleanUpExit_parseSectionHeaders;
			}
			if ((Error = PrivateOsFread(elfSourceHandle, stringTable,
								sectionHeader.sh_size, &bytesRead)) != NvSuccess)
			{
				NV_DEBUG_PRINTF(("File Read failed %d\r\n", bytesRead));
				goto CleanUpExit_parseSectionHeaders;
			}
		}
	}
	if ((Error = PrivateOsFseek(elfSourceHandle,
						elf->e_shoff, NvOsSeek_Set)) != NvSuccess)
	{
		NV_DEBUG_PRINTF(("File Seek failed %d\r\n", bytesRead));
		goto CleanUpExit_parseSectionHeaders;
	}
	for (loop = 0; loop < elf->e_shnum; loop++)
	{
		if ((Error = PrivateOsFread(elfSourceHandle, &sectionHeader,
							sizeof(sectionHeader), &bytesRead)) != NvSuccess)
		{
			NV_DEBUG_PRINTF(("File Read failed %d\r\n", bytesRead));
			goto CleanUpExit_parseSectionHeaders;
		}

		NV_DEBUG_PRINTF(("Section %d is named %s\r\n",
						loop, &stringTable[sectionHeader.sh_name]));
		NV_DEBUG_PRINTF(("Section %d Type %d\r\n",
						loop, sectionHeader.sh_type));
		NV_DEBUG_PRINTF(("Section %d Flags %x\r\n",
						loop, sectionHeader.sh_flags));

		GetSpecialSectionName(sectionHeader.sh_type,
				sectionHeader.sh_flags, &specialNamePtr);

		NV_DEBUG_PRINTF(("Section %d Special Name is %s",
						loop, specialNamePtr));
		NV_DEBUG_PRINTF(("Section %d Address %x\r\n",
						loop, sectionHeader.sh_addr));
		NV_DEBUG_PRINTF(("Section %d File Offset %d\r\n",
						loop, sectionHeader.sh_offset));
		NV_DEBUG_PRINTF(("Section %d Size %d \r\n",
						loop, sectionHeader.sh_size));
		NV_DEBUG_PRINTF(("Section %d Link %d \r\n",
						loop, sectionHeader.sh_link));
		NV_DEBUG_PRINTF(("Section %d Info %d\r\n",
						loop, sectionHeader.sh_info));
		NV_DEBUG_PRINTF(("Section %d alignment %d\r\n",
						loop, sectionHeader.sh_addralign));
		NV_DEBUG_PRINTF(("Section %d Fixed Entry Size %d\r\n",
						loop, sectionHeader.sh_entsize));
		NV_DEBUG_PRINTF(("\r\n"));

	}
CleanUpExit_parseSectionHeaders:
	if (stringTable)
		NvOsFree(stringTable);
	return Error;
}


NvError
loadSegmentsInFixedMemory(PrivateOsFileHandle elfSourceHandle,
			Elf32_Ehdr *elf, NvU32 segmentIndex, void **loadaddress)
{
	NvError Error = NvSuccess;
	size_t bytesRead = 0;
	Elf32_Phdr progHeader;

	if ((Error = PrivateOsFseek(elfSourceHandle,
						elf->e_phoff + (segmentIndex * sizeof(progHeader)), NvOsSeek_Set)) != NvSuccess)
	{
		NV_DEBUG_PRINTF(("loadSegmentsInFixedMemory File Seek failed %d\r\n", bytesRead));
		return Error;
	}

	if ((Error = PrivateOsFread(elfSourceHandle, &progHeader,
						sizeof(Elf32_Phdr), &bytesRead)) != NvSuccess)
	{
		NV_DEBUG_PRINTF((" loadSegmentsInFixedMemory File Read failed %d\r\n", bytesRead));
		return Error;
	}
	NV_ASSERT(progHeader.p_type == PT_LOAD);
	if ((Error = PrivateOsFseek(elfSourceHandle,
						progHeader.p_offset, NvOsSeek_Set)) != NvSuccess)
	{
		NV_DEBUG_PRINTF(("loadSegmentsInFixedMemory File Seek failed %d\r\n", Error));
		return Error;
	}

	/* if((Error = NvRmPhysicalMemMap(progHeader.p_vaddr, */
	/* 					progHeader.p_memsz, NVOS_MEM_READ_WRITE, */
	/* 					NvOsMemAttribute_Uncached, loadaddress)) != NvSuccess) */
	/* { */
	/* 	NV_DEBUG_PRINTF(("loadSegmentsInFixedMemory Failed trying to Mem Map %x\r\n", progHeader.p_vaddr)); */
	/* 	return Error; */
	/* } */
	// This will initialize ZI to zero
	*loadaddress = ioremap_nocache(progHeader.p_vaddr, progHeader.p_memsz);

	NvOsMemset(*loadaddress, 0, progHeader.p_memsz);
	if ((Error = PrivateOsFread(elfSourceHandle, *loadaddress,
						progHeader.p_filesz, &bytesRead)) != NvSuccess)
	{
		NV_DEBUG_PRINTF(("loadSegmentsInFixedMemory File Read failed %d\r\n", bytesRead));
		return Error;
	}
	// Load address need to be reset to the physical address as this is passed to the AVP as the entry point.
	*loadaddress = (void *)progHeader.p_vaddr;

	return Error;
}

NvError NvRmPrivGetProcAddress(NvRmLibraryHandle Handle,
			const char *pSymbol,
			void **pSymAddress)
{
	NvError Error = NvSuccess;
	// In phase 1, this API will just return the load address as entry address
	NvRmLibHandle *hHandle = Handle;

	//NOTE: The EntryAddress is pointing to a THUMB function
	//(LSB is set). The Entry Function must be in THUMB mode.
	if (hHandle->EntryAddress != NULL)
	{
		*pSymAddress = hHandle->EntryAddress;
	}
	else
	{
		Error = NvError_SymbolNotFound;
	}
	return Error;
}

static int __init nvfw_init(void)
{
	int ret = 0;

	NvOsDebugPrintf("%s: called\n", __func__);
	ret = misc_register(&nvfw_dev);
	if (ret) panic("%s: misc_register FAILED\n", __func__);

	return ret;
}

static void __exit nvfw_deinit(void)
{
	misc_deregister(&nvfw_dev);
}

module_init(nvfw_init);
module_exit(nvfw_deinit);
