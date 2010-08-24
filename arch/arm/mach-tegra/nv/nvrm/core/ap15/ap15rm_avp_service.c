/*
 * arch/arm/mach-tegra/nvrm/core/ap15/ap15rm_avp_service.c
 *
 * AVP service to handle AVP messages.
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

/** @file
 * @brief <b>NVIDIA Driver Development Kit:
 *              Testcases for the xpc </b>
 *
 * @b Description: This file implements the AVP service to handle AVP messages.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

#include "nvcommon.h"
#include "nvassert.h"
#include "nvrm_drf.h"
#include "nvrm_init.h"
#include "nvrm_message.h"
#include "nvrm_rpc.h"
#include "nvrm_moduleloader_private.h"
#include "nvrm_graphics_private.h"
#include "ap15/arres_sema.h"
#include "ap15/arflow_ctlr.h"
#include "ap15/arapbpm.h"
#include "nvrm_avp_swi_registry.h"
#include "ap15/arevp.h"
#include "nvrm_hardware_access.h"
#include "mach/io.h"
#include "mach/iomap.h"

static NvU32 s_AvpInitialized = NV_FALSE;
static NvRmLibraryHandle s_hAvpLibrary = NULL;

#define _TEGRA_AVP_RESET_VECTOR_ADDR	\
	(IO_ADDRESS(TEGRA_EXCEPTION_VECTORS_BASE) + EVP_COP_RESET_VECTOR_0)

#define NV_USE_AOS 1

#define AVP_EXECUTABLE_NAME "nvrm_avp.axf"

void NvRmPrivProcessMessage(NvRmRPCHandle hRPCHandle, char *pRecvMessage, int messageLength)
{
    NvError Error = NvSuccess;
    NvRmMemHandle hMem;

    switch ((NvRmMsg)*pRecvMessage) {

    case NvRmMsg_MemHandleCreate:
    {
        NvRmMessage_HandleCreat *msgHandleCreate = NULL;
        NvRmMessage_HandleCreatResponse msgRHandleCreate;

        msgHandleCreate = (NvRmMessage_HandleCreat*)pRecvMessage;

        msgRHandleCreate.msg = NvRmMsg_MemHandleCreate;
        Error = NvRmMemHandleCreate(hRPCHandle->hRmDevice,&hMem, msgHandleCreate->size);
        if (!Error) {
            msgRHandleCreate.hMem = hMem;
        }
        msgRHandleCreate.msg = NvRmMsg_MemHandleCreate_Response;
        msgRHandleCreate.error = Error;

        NvRmPrivRPCSendMsg(hRPCHandle, &msgRHandleCreate,
                           sizeof(msgRHandleCreate));
    }
    break;
    case NvRmMsg_MemHandleOpen:
        break;
    case NvRmMsg_MemHandleFree:
    {
        NvRmMessage_HandleFree *msgHandleFree = NULL;
        msgHandleFree = (NvRmMessage_HandleFree*)pRecvMessage;
        NvRmMemHandleFree(msgHandleFree->hMem);
    }
    break;
    case NvRmMsg_MemAlloc:
    {
        NvRmMessage_MemAlloc *msgMemAlloc = NULL;
        NvRmMessage_Response msgResponse;
        msgMemAlloc = (NvRmMessage_MemAlloc*)pRecvMessage;

        Error = NvRmMemAlloc(msgMemAlloc->hMem,
                    (msgMemAlloc->NumHeaps == 0) ? NULL : msgMemAlloc->Heaps,
                    msgMemAlloc->NumHeaps,
                    msgMemAlloc->Alignment,
                    msgMemAlloc->Coherency);
        msgResponse.msg = NvRmMsg_MemAlloc_Response;
        msgResponse.error = Error;

        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(msgResponse));
    }
    break;
    case NvRmMsg_MemPin:
    {
        NvRmMessage_Pin *msgHandleFree = NULL;
        NvRmMessage_PinResponse msgResponse;
        msgHandleFree = (NvRmMessage_Pin*)pRecvMessage;

        msgResponse.address = NvRmMemPin(msgHandleFree->hMem);
        msgResponse.msg = NvRmMsg_MemPin_Response;

        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(msgResponse));
    }
    break;
    case NvRmMsg_MemUnpin:
    {
        NvRmMessage_HandleFree *msgHandleFree = NULL;
        NvRmMessage_Response msgResponse;
        msgHandleFree = (NvRmMessage_HandleFree*)pRecvMessage;

        NvRmMemUnpin(msgHandleFree->hMem);

        msgResponse.msg = NvRmMsg_MemUnpin_Response;
        msgResponse.error = NvSuccess;

        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(msgResponse));
    }
    break;
    case NvRmMsg_MemGetAddress:
    {
        NvRmMessage_GetAddress *msgGetAddress = NULL;
        NvRmMessage_GetAddressResponse msgGetAddrResponse;

        msgGetAddress = (NvRmMessage_GetAddress*)pRecvMessage;

        msgGetAddrResponse.msg     = NvRmMsg_MemGetAddress_Response;
        msgGetAddrResponse.address = NvRmMemGetAddress(msgGetAddress->hMem,msgGetAddress->Offset);

        NvRmPrivRPCSendMsg(hRPCHandle, &msgGetAddrResponse, sizeof(msgGetAddrResponse));
    }
    break;
    case NvRmMsg_HandleFromId :
    {
        NvRmMessage_HandleFromId *msgHandleFromId = NULL;
        NvRmMessage_Response msgResponse;
        NvRmMemHandle hMem;

        msgHandleFromId = (NvRmMessage_HandleFromId*)pRecvMessage;

        msgResponse.msg     = NvRmMsg_HandleFromId_Response;
        msgResponse.error = NvRmMemHandleFromId(msgHandleFromId->id, &hMem);

        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(NvRmMessage_Response));
    }
    break;
    case NvRmMsg_PowerModuleClockControl:
    {
        NvRmMessage_Module *msgPMCC;
        NvRmMessage_Response msgPMCCR;
        msgPMCC = (NvRmMessage_Module*)pRecvMessage;

        msgPMCCR.msg = NvRmMsg_PowerModuleClockControl_Response;
        msgPMCCR.error = NvRmPowerModuleClockControl(hRPCHandle->hRmDevice,
                                                     msgPMCC->ModuleId,
                                                     msgPMCC->ClientId,
                                                     msgPMCC->Enable);

        NvRmPrivRPCSendMsg(hRPCHandle, &msgPMCCR, sizeof(msgPMCCR));
    }
    break;
    case NvRmMsg_ModuleReset:
    {
        NvRmMessage_Module *msgPMCC;
        NvRmMessage_Response msgPMCCR;
        msgPMCC = (NvRmMessage_Module*)pRecvMessage;

        msgPMCCR.msg = NvRmMsg_ModuleReset_Response;

        NvRmModuleReset(hRPCHandle->hRmDevice, msgPMCC->ModuleId);
        /// Send response since clients to this call needs to wait
        /// for some time before they can start using the HW module
        NvRmPrivRPCSendMsg(hRPCHandle, &msgPMCCR, sizeof(msgPMCCR));
    }
    break;

    case NvRmMsg_PowerRegister:
    {
        NvRmMessage_PowerRegister *msgPower;
        NvRmMessage_PowerRegister_Response msgResponse;

        msgPower = (NvRmMessage_PowerRegister*)pRecvMessage;

        msgResponse.msg   = NvRmMsg_PowerResponse;
        msgResponse.error = NvSuccess;
        msgResponse.clientId = msgPower->clientId;

        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(msgResponse));

    }
    break;

    case NvRmMsg_PowerUnRegister:
        break;
    case NvRmMsg_PowerStarvationHint:
    case NvRmMsg_PowerBusyHint:
    {
        NvRmMessage_Response msgResponse;
        msgResponse.msg   = NvRmMsg_PowerResponse;
        msgResponse.error = NvSuccess;
        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(msgResponse));
    }
    break;
    case NvRmMsg_PowerBusyMultiHint:
        break;
    case NvRmMsg_PowerDfsGetState:
    {
        NvRmMessage_PowerDfsGetState_Response msgResponse;
        msgResponse.msg = NvRmMsg_PowerDfsGetState_Response;
        msgResponse.state = NvRmDfsRunState_Stopped;
        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(msgResponse));
    }
    break;
    case NvRmMsg_PowerModuleGetMaxFreq:
    {
        NvRmMessage_PowerModuleGetMaxFreq_Response msgResponse;
        msgResponse.msg = NvRmMsg_PowerModuleGetMaxFreq;
        msgResponse.freqKHz = 0;
        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(msgResponse));
    }
    break;
    case NvRmMsg_PowerDfsGetClockUtilization:
    {
        NvRmMessage_PowerDfsGetClockUtilization_Response msgResponse;
        NvRmDfsClockUsage ClockUsage = { 0, 0, 0, 0, 0, 0 };

        msgResponse.msg = NvRmMsg_PowerDfsGetClockUtilization_Response;
        msgResponse.error = NvSuccess;
        NvOsMemcpy(&msgResponse.clockUsage, &ClockUsage, sizeof(ClockUsage));
        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(msgResponse));
    }
    break;
    case NvRmMsg_InitiateLP0:
    {
        //Just for testing purposes.
    }
    break;
    case NvRmMsg_RemotePrintf:
    {
        NvRmMessage_RemotePrintf *msg;

        msg = (NvRmMessage_RemotePrintf*)pRecvMessage;
        NvOsDebugPrintf("AVP: %s", msg->string);
    }
    break;
    case NvRmMsg_AVP_Reset:
        NvOsDebugPrintf("AVP has been reset by WDT\n");
        break;
    default:
        NV_ASSERT( !"AVP Service::ProcessMessage: bad message" );
        break;
    }
}

NvError
NvRmPrivInitAvp(NvRmDeviceHandle hDevice)
{
    NvError err = NvSuccess;
    void* avpExecutionJumpAddress;
    NvU32 RegVal, resetVector;

    // Do this only once.
    if (s_AvpInitialized) return NvSuccess;

    NvOsDebugPrintf("%s <kernel impl>: called\n", __func__);

    err = NvRmPrivLoadKernelLibrary(hDevice, AVP_EXECUTABLE_NAME, &s_hAvpLibrary);
    if (err != NvSuccess) {
        NV_DEBUG_PRINTF(("AVP executable file not found\n"));
        NV_ASSERT(!"AVP executable file not found");
    }

    err = NvRmGetProcAddress(s_hAvpLibrary, "main", &avpExecutionJumpAddress);
    NV_ASSERT(err == NvSuccess);
    NvOsDebugPrintf("%s <kernel impl>: avpExecutionJumpAddress=%x\n", __func__, avpExecutionJumpAddress);

    resetVector = (NvU32)avpExecutionJumpAddress & 0xFFFFFFFE;

    NV_WRITE32(_TEGRA_AVP_RESET_VECTOR_ADDR, resetVector);
    RegVal = NV_READ32(_TEGRA_AVP_RESET_VECTOR_ADDR);
    NV_ASSERT( RegVal == resetVector );

    NvRmModuleReset(hDevice, NvRmModuleID_Avp);

    /// Resume AVP
    RegVal = NV_DRF_DEF(FLOW_CTLR, HALT_COP_EVENTS, MODE, FLOW_MODE_NONE);
    NV_WRITE32(IO_ADDRESS(TEGRA_FLOW_CTRL_BASE) +  FLOW_CTLR_HALT_COP_EVENTS_0, RegVal);

    s_AvpInitialized = NV_TRUE;

    err = NvRmPrivInitService(hDevice);
    if (err) return err;

    err = NvRmPrivInitModuleLoaderRPC(hDevice);
    if (err) return err;

    return err;
}

static void __iomem *iram_base = IO_ADDRESS(TEGRA_IRAM_BASE);
static void __iomem *iram_backup;
static dma_addr_t iram_backup_addr;
static u32 iram_size = TEGRA_IRAM_SIZE;
static u32 iram_backup_size = TEGRA_IRAM_SIZE + 4;
static u32 avp_resume_addr;

NvError
NvRmPrivSuspendAvp(NvRmRPCHandle hRPCHandle)
{
    NvError err = NvSuccess;
    NvRmMessage_InitiateLP0 lp0_msg;
    void *avp_suspend_done = iram_backup + iram_size;
    unsigned long timeout;

    pr_info("%s()+\n", __func__);

    if (!s_AvpInitialized)
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

NvError
NvRmPrivResumeAvp(NvRmRPCHandle hRPCHandle)
{
    NvError ret = NvSuccess;
    u32 tmp;

    pr_info("%s()+\n", __func__);
    if (!s_AvpInitialized || !avp_resume_addr)
        goto done;

    writel(avp_resume_addr, _TEGRA_AVP_RESET_VECTOR_ADDR);
    tmp = readl(_TEGRA_AVP_RESET_VECTOR_ADDR);
    NV_ASSERT(tmp == resetVector);

    NvRmModuleReset(hRPCHandle->hRmDevice, NvRmModuleID_Avp);

    /// Resume AVP
    tmp = NV_DRF_DEF(FLOW_CTLR, HALT_COP_EVENTS, MODE, FLOW_MODE_NONE);
    writel(tmp, IO_ADDRESS(TEGRA_FLOW_CTRL_BASE) + FLOW_CTLR_HALT_COP_EVENTS_0);

    /* clear the avp resume addr so that if suspend fails, we don't try to
     * resume */
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
	if (!iram_backup) {
		pr_err("%s: Unable to allocate iram backup mem\n", __func__);
		return -ENOMEM;
	}

	return 0;
}
