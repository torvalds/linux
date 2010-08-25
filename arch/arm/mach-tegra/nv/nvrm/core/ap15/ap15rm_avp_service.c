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

#define NV_USE_AOS 1

void NvRmPrivProcessMessage(NvRmRPCHandle hRPCHandle, char *pRecvMessage, int messageLength)
{
    NvError Error = NvSuccess;
    NvRmMemHandle hMem;

    switch (*(NvRmMsg *)pRecvMessage) {

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
        printk("AVP: %s", msg->string);
    }
    break;
    case NvRmMsg_AVP_Reset:
        NvOsDebugPrintf("AVP has been reset by WDT\n");
        break;
    default:
	    panic("AVP Service::ProcessMessage: bad message");
	    break;
    }
}

