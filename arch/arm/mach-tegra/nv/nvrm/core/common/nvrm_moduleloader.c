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
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <asm/cacheflush.h>
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
#include "ap15/arflow_ctlr.h"
#include "ap15/arevp.h"
#include "mach/io.h"
#include "mach/iomap.h"
#include "headavp.h"

#define DEVICE_NAME "nvfw"

#define _TEGRA_AVP_RESET_VECTOR_ADDR	\
	(IO_ADDRESS(TEGRA_EXCEPTION_VECTORS_BASE) + EVP_COP_RESET_VECTOR_0)

static const struct firmware *s_FwEntry;
static NvRmRPCHandle s_RPCHandle = NULL;
static NvRmMemHandle s_KernelImage = NULL;
static NvError SendMsgDetachModule(NvRmLibraryHandle  hLibHandle);
static NvError SendMsgAttachModule(
    NvRmLibraryHandle hLibHandle,
    void* pArgs,
    NvU32 loadAddress,
    NvU32 fileSize,
    NvBool greedy,
    NvU32 sizeOfArgs);
NvU32 NvRmModuleGetChipId(NvRmDeviceHandle hDevice);
NvError NvRmPrivInitModuleLoaderRPC(NvRmDeviceHandle hDevice);
void NvRmPrivDeInitModuleLoaderRPC(void);
static NvError NvRmPrivInitAvp(NvRmDeviceHandle hDevice);

#define AVP_KERNEL_SIZE_MAX	SZ_1M

#define ADD_MASK                   0x00000001
#define SUB_MASK                   0xFFFFFFFD

static int nvfw_open(struct inode *inode, struct file *file);
static int nvfw_close(struct inode *inode, struct file *file);
static long nvfw_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static ssize_t nvfw_write(struct file *, const char __user *, size_t, loff_t *);

static NvError NvRmPrivInitAvp(NvRmDeviceHandle hRm);

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

	error = copy_from_user(filename, buff, count);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);
	filename[count] = 0;
	error = NvRmOpen( &hRmDevice, 0 );
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	error = NvRmLoadLibrary(hRmDevice, filename, NULL, 0, &hRmLibHandle);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

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

	filename = NvOsAlloc(op.length + 1);
	error = copy_from_user(filename, op.filename, op.length + 1);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	args = NvOsAlloc(op.argssize);
	error = copy_from_user(args, op.args, op.argssize);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	error = NvRmOpen( &hRmDevice, 0 );
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	error = NvRmLoadLibrary(hRmDevice, filename, args, op.argssize, &hRmLibHandle);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	op.handle = hRmLibHandle;
	error = copy_to_user(arg, &op, sizeof(op));

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

	filename = NvOsAlloc(op.length + 1);
	error = copy_from_user(filename, op.filename, op.length + 1);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	args = NvOsAlloc(op.argssize);
	error = copy_from_user(args, op.args, op.argssize);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	error = NvRmOpen( &hRmDevice, 0 );
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	error = NvRmLoadLibraryEx(hRmDevice, filename, args, op.argssize, op.greedy, &hRmLibHandle);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	op.handle = hRmLibHandle;
	error = copy_to_user(arg, &op, sizeof(op));

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

	error = NvRmOpen( &hRmDevice, 0 );
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	error = NvRmFreeLibrary(op.handle);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

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

	symbolname = NvOsAlloc(op.length + 1);
	error = copy_from_user(symbolname, op.symbolname, op.length + 1);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	error = NvRmOpen( &hRmDevice, 0 );
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	error = NvRmGetProcAddress(op.handle, symbolname, &op.address);
	if (error) panic("%s: line=%d\n", __func__, __LINE__);

	error = copy_to_user(arg, &op, sizeof(op));

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

static NvError PrivateOsFopen(
    const char *filename,
    NvU32 flags,
    PrivateOsFileHandle *file)
{
    PrivateOsFileHandle hFile;

    hFile = NvOsAlloc(sizeof(PrivateOsFile));
    if (hFile == NULL)
        return NvError_InsufficientMemory;

    NvOsDebugPrintf("%s <kernel impl>: file=%s\n", __func__, filename);
    NvOsDebugPrintf("%s <kernel impl>: calling request_firmware()\n", __func__);
    if (request_firmware(&s_FwEntry, filename, nvfw_dev.this_device) != 0)
    {
        pr_err("%s: Cannot read firmware '%s'\n", __func__, filename);
        return NvError_FileReadFailed;
    }
    NvOsDebugPrintf("%s <kernel impl>: back from request_firmware()\n", __func__);
    hFile->pstart = s_FwEntry->data;
    hFile->pread = s_FwEntry->data;
    hFile->pend = s_FwEntry->data + s_FwEntry->size;

    *file = hFile;

    return NvError_Success;
}

static void PrivateOsFclose(PrivateOsFileHandle hFile)
{
    release_firmware(s_FwEntry);
    NV_ASSERT(hFile);
    NvOsFree(hFile);
}

NvError NvRmLoadLibrary(
    NvRmDeviceHandle hDevice,
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

NvError NvRmLoadLibraryEx(
    NvRmDeviceHandle hDevice,
    const char *pLibName,
    void* pArgs,
    NvU32 sizeOfArgs,
    NvBool IsApproachGreedy,
    NvRmLibraryHandle *hLibHandle)
{
    NvRmLibraryHandle library = NULL;
    NvError e = NvSuccess;
    PrivateOsFileHandle hFile = NULL;
    NvRmMemHandle hMem = NULL;
    NvRmHeap loadHeap = NvRmHeap_ExternalCarveOut;
    void *loadAddr = NULL;
    NvU32 len = 0;
    NvU32 physAddr;

    NV_ASSERT(sizeOfArgs <= MAX_ARGS_SIZE);

    NvOsDebugPrintf("%s <kernel impl>: file=%s\n", __func__, pLibName);

    NV_CHECK_ERROR_CLEANUP(NvRmPrivInitAvp(hDevice));

    e = NvRmPrivRPCConnect(s_RPCHandle);
    if (e != NvSuccess)
    {
        NvOsDebugPrintf("RPCConnect timed out during NvRmLoadLibrary\n");
        goto fail;
    }

    library = NvOsAlloc(sizeof(*library));
    if (!library)
    {
        e = NvError_InsufficientMemory;
        goto fail;
    }

    NV_CHECK_ERROR_CLEANUP(PrivateOsFopen(pLibName, NVOS_OPEN_READ, &hFile));
    len = (NvU32)hFile->pend - (NvU32)hFile->pstart;

    NV_CHECK_ERROR_CLEANUP(NvRmMemHandleCreate(hDevice, &hMem, len));

    NV_CHECK_ERROR_CLEANUP(NvRmMemAlloc(hMem, &loadHeap, 1, L1_CACHE_BYTES,
                                        NvOsMemAttribute_WriteCombined));

    NV_CHECK_ERROR_CLEANUP(NvRmMemMap(hMem, 0, len, NVOS_MEM_READ_WRITE, &loadAddr));

    physAddr = NvRmMemPin(hMem);

    NvOsMemcpy(loadAddr, hFile->pstart, len);

    NvOsFlushWriteCombineBuffer();

    NV_CHECK_ERROR_CLEANUP(SendMsgAttachModule(library, pArgs, physAddr, len,
                                               IsApproachGreedy, sizeOfArgs));

fail:
    if (loadAddr)
    {
        NvRmMemUnpin(hMem);
        NvRmMemUnmap(hMem, loadAddr, len);
    }

    NvRmMemHandleFree(hMem);
    if (hFile)
        PrivateOsFclose(hFile);

    if (e != NvSuccess)
    {
        NvOsFree(library);
        library = NULL;
    }

    *hLibHandle = library;
    return e;
}

NvError NvRmGetProcAddress(
    NvRmLibraryHandle Handle,
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
    NvError e = NvSuccess;
    NV_ASSERT(hLibHandle);

    e = NvRmPrivRPCConnect(s_RPCHandle);
    if (e != NvSuccess)
        return e;

    e = SendMsgDetachModule(hLibHandle);
    if (e != NvSuccess)
        return e;

    NvOsFree(hLibHandle);
    return NvSuccess;
}

//before unloading loading send message to avp with args and entry point via transport
static NvError SendMsgDetachModule(NvRmLibraryHandle hLibHandle)
{
    NvU32 RecvMsgSize;
    NvRmMessage_DetachModule Msg;
    NvRmMessage_DetachModuleResponse MsgR;

    Msg.msg = NvRmMsg_DetachModule;

    Msg.msg = NvRmMsg_DetachModule;
    Msg.reason = NvRmModuleLoaderReason_Detach;
    Msg.libraryId = hLibHandle->libraryId;
    RecvMsgSize = sizeof(NvRmMessage_DetachModuleResponse);
    NvRmPrivRPCSendMsgWithResponse(s_RPCHandle, &MsgR, RecvMsgSize,
                                   &RecvMsgSize, &Msg, sizeof(Msg));

    return MsgR.error;
}

//after successful loading send message to avp with args and entry point via transport
static NvError SendMsgAttachModule(
    NvRmLibraryHandle hLibHandle,
    void* pArgs,
    NvU32 loadAddress,
    NvU32 fileSize,
    NvBool greedy,
    NvU32 sizeOfArgs)
{
    NvU32 RecvMsgSize;
    NvRmMessage_AttachModule Msg;
    NvRmMessage_AttachModuleResponse MsgR;

    NvOsMemset(&Msg, 0, sizeof(Msg));
    Msg.msg = NvRmMsg_AttachModule;

    if(pArgs)
        NvOsMemcpy(Msg.args, pArgs, sizeOfArgs);

    Msg.size = sizeOfArgs;
    Msg.address = loadAddress;
    Msg.filesize = fileSize;
    if (greedy)
        Msg.reason = NvRmModuleLoaderReason_AttachGreedy;
    else
        Msg.reason = NvRmModuleLoaderReason_Attach;

    RecvMsgSize = sizeof(NvRmMessage_AttachModuleResponse);

    NvRmPrivRPCSendMsgWithResponse(s_RPCHandle, &MsgR, RecvMsgSize,
                                   &RecvMsgSize, &Msg, sizeof(Msg));

    hLibHandle->libraryId = MsgR.libraryId;
    return MsgR.error;
}


NvError NvRmPrivInitModuleLoaderRPC(NvRmDeviceHandle hDevice)
{
    NvError err = NvSuccess;

    if (s_RPCHandle)
        return NvError_Success;

    NvOsDebugPrintf("%s <kernel impl>: NvRmPrivRPCInit(RPC_AVP_PORT)\n", __func__);
    err = NvRmPrivRPCInit(hDevice, "RPC_AVP_PORT", &s_RPCHandle);
    if (err) panic("%s: NvRmPrivRPCInit FAILED\n", __func__);

    return err;
}

void NvRmPrivDeInitModuleLoaderRPC()
{
    NvRmPrivRPCDeInit(s_RPCHandle);
}

NvError NvRmPrivGetProcAddress(
    NvRmLibraryHandle Handle,
    const char *pSymbol,
    void **pSymAddress)
{
    NvRmLibHandle *hHandle = Handle;

    if (hHandle->libraryId == 0)
        return NvError_SymbolNotFound;

    *pSymAddress = (void *)hHandle->libraryId;
    return NvSuccess;
}

static void NvRmPrivResetAvp(NvRmDeviceHandle hRm, unsigned long reset_va)
{
    u32 *stub_va = &_tegra_avp_launcher_stub_data[AVP_LAUNCHER_START_VA];
    unsigned long stub_addr = virt_to_phys(_tegra_avp_launcher_stub);
    unsigned int tmp;

    *stub_va = reset_va;
    __cpuc_flush_dcache_area(stub_va, sizeof(*stub_va));
    outer_clean_range(__pa(stub_va), __pa(stub_va+1));

    tmp = readl(_TEGRA_AVP_RESET_VECTOR_ADDR);
    writel(stub_addr, _TEGRA_AVP_RESET_VECTOR_ADDR);
    barrier();
    NvRmModuleReset(hRm, NvRmModuleID_Avp);
    writel(0, IO_ADDRESS(TEGRA_FLOW_CTRL_BASE) + FLOW_CTRL_HALT_COP);
    barrier();
    writel(tmp, _TEGRA_AVP_RESET_VECTOR_ADDR);
}

static NvError NvRmPrivInitAvp(NvRmDeviceHandle hRm)
{
    u32 *stub_phys = &_tegra_avp_launcher_stub_data[AVP_LAUNCHER_MMU_PHYSICAL];
    NvRmHeap heaps[] = { NvRmHeap_External, NvRmHeap_ExternalCarveOut };
    PrivateOsFileHandle kernel;
    void *map = NULL;
    NvError e;
    NvU32 len;
    NvU32 phys;

    if (s_KernelImage)
        return NvSuccess;

    NV_CHECK_ERROR_CLEANUP(NvRmMemHandleCreate(hRm, &s_KernelImage, SZ_1M));
    NV_CHECK_ERROR_CLEANUP(NvRmMemAlloc(s_KernelImage, heaps,
                                        NV_ARRAY_SIZE(heaps), SZ_1M,
                                        NvOsMemAttribute_WriteCombined));
    NV_CHECK_ERROR_CLEANUP(NvRmMemMap(s_KernelImage, 0, SZ_1M,
                                      NVOS_MEM_READ_WRITE, &map));
    
    phys = NvRmMemPin(s_KernelImage);

    NV_CHECK_ERROR_CLEANUP(PrivateOsFopen("nvrm_avp.bin",
                                          NVOS_OPEN_READ, &kernel));

    NvOsMemset(map, 0, SZ_1M);
    len = (NvU32)kernel->pend - (NvU32)kernel->pstart;
    NvOsMemcpy(map, kernel->pstart, len);

    PrivateOsFclose(kernel);

    *stub_phys = phys;
    __cpuc_flush_dcache_area(stub_phys, sizeof(*stub_phys));
    outer_clean_range(__pa(stub_phys), __pa(stub_phys+1));

    NvRmPrivResetAvp(hRm, 0x00100000ul);

    NV_CHECK_ERROR_CLEANUP(NvRmPrivInitService(hRm));
    e = NvRmPrivInitModuleLoaderRPC(hRm);
    if (e != NvSuccess)
    {
        NvRmPrivServiceDeInit();
        goto fail;
    }

    NvRmMemUnmap(s_KernelImage, map, SZ_1M);

    return NvSuccess;

fail:
    writel(2 << 29, IO_ADDRESS(TEGRA_FLOW_CTRL_BASE) + FLOW_CTRL_HALT_COP);
    if (map)
    {
        NvRmMemUnpin(s_KernelImage);
        NvRmMemUnmap(s_KernelImage, map, SZ_1M);
    }
    NvRmMemHandleFree(s_KernelImage);
    s_KernelImage = NULL;
    return e;
}

static void __iomem *iram_base = IO_ADDRESS(TEGRA_IRAM_BASE);
static void __iomem *iram_backup;
static dma_addr_t iram_backup_addr;
static u32 iram_size = TEGRA_IRAM_SIZE;
static u32 iram_backup_size = TEGRA_IRAM_SIZE + 4;
static u32 avp_resume_addr;

static NvError NvRmPrivSuspendAvp(NvRmRPCHandle hRPCHandle)
{
    NvError err = NvSuccess;
    NvRmMessage_InitiateLP0 lp0_msg;
    void *avp_suspend_done = iram_backup + iram_size;
    unsigned long timeout;

    pr_info("%s()+\n", __func__);

    if (!s_KernelImage)
        goto done;
    else if (!iram_backup_addr) {
        /* XXX: should we return error? */
        pr_warning("%s: iram backup ram missing, not suspending avp\n",
                   __func__);
        goto done;
    }

    NV_ASSERT(hRPCHandle->svcTransportHandle != NULL);

    lp0_msg.msg = NvRmMsg_InitiateLP0;
    lp0_msg.sourceAddr = (u32)TEGRA_IRAM_BASE;
    lp0_msg.bufferAddr = (u32)iram_backup_addr;
    lp0_msg.bufferSize = (u32)iram_size;

    writel(0, avp_suspend_done);

    NvOsMutexLock(hRPCHandle->RecvLock);
    err = NvRmTransportSendMsg(hRPCHandle->svcTransportHandle, &lp0_msg,
                               sizeof(lp0_msg), 1000);
    NvOsMutexUnlock(hRPCHandle->RecvLock);

    if (err != NvSuccess) {
        pr_err("%s: cannot send AVP LP0 message\n", __func__);
        goto done;
    }

    timeout = jiffies + msecs_to_jiffies(1000);
    while (!readl(avp_suspend_done) && time_before(jiffies, timeout)) {
        udelay(10);
        cpu_relax();
    }

    if (!readl(avp_suspend_done)) {
        pr_err("%s: AVP failed to suspend\n", __func__);
        err = NvError_Timeout;
        goto done;
    }

    avp_resume_addr = readl(iram_base);
    if (!avp_resume_addr) {
        pr_err("%s: AVP failed to set it's resume address\n", __func__);
        err = NvError_InvalidState;
        goto done;
    }

    pr_info("avp_suspend: resume_addr=%x\n", avp_resume_addr);
    avp_resume_addr &= 0xFFFFFFFE;

    pr_info("%s()-\n", __func__);

done:
    return err;
}

static NvError NvRmPrivResumeAvp(NvRmRPCHandle hRPCHandle)
{
    NvError ret = NvSuccess;

    pr_info("%s()+\n", __func__);
    if (!s_KernelImage || !avp_resume_addr)
        goto done;

    NvRmPrivResetAvp(hRPCHandle->hRmDevice, avp_resume_addr);
    avp_resume_addr = 0;

    pr_info("%s()-\n", __func__);

done:
    return ret;
}

int __init _avp_suspend_resume_init(void)
{
    /* allocate an iram sized chunk of ram to give to the AVP */
    iram_backup = dma_alloc_coherent(NULL, iram_backup_size,
                                     &iram_backup_addr, GFP_KERNEL);
    if (!iram_backup)
    {
        pr_err("%s: Unable to allocate iram backup mem\n", __func__);
        return -ENOMEM;
    }

    return 0;
}

static int avp_suspend(struct platform_device *pdev, pm_message_t state)
{
	NvError err;

	err = NvRmPrivSuspendAvp(s_RPCHandle);
	if (err != NvSuccess)
		return -EIO;
	return 0;
}

static int avp_resume(struct platform_device *pdev)
{
	NvError err;

	err = NvRmPrivResumeAvp(s_RPCHandle);
	if (err != NvSuccess)
		return -EIO;
	return 0;
}

static struct platform_driver avp_nvfw_driver = {
	.suspend = avp_suspend,
	.resume  = avp_resume,
	.driver  = {
		.name  = "nvfw-avp-device",
		.owner = THIS_MODULE,
	},
};

int __init _avp_suspend_resume_init(void);

static int __init nvfw_init(void)
{
    int ret = 0;
    struct platform_device *pdev;

    ret = misc_register(&nvfw_dev);
    s_KernelImage = NULL;
    if (ret) panic("%s: misc_register FAILED\n", __func__);

    ret = _avp_suspend_resume_init();
    if (ret)
        goto err;
    pdev = platform_create_bundle(&avp_nvfw_driver, NULL, NULL, 0, NULL, 0);
    if (!pdev) {
        pr_err("%s: Can't reg platform driver\n", __func__);
        ret = -EINVAL;
        goto err;
    }

    return 0;

err:
    return ret;
}

static void __exit nvfw_deinit(void)
{
    misc_deregister(&nvfw_dev);
}

module_init(nvfw_init);
module_exit(nvfw_deinit);
