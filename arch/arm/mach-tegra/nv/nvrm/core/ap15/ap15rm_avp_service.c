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

#include <mach/nvmap.h>

#include "../../../../../../../drivers/video/tegra/nvmap/nvmap.h"
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

extern struct nvmap_client *s_AvpClient;

#define NV_USE_AOS 1

static void HandleCreateMessage(const NvRmMessage_HandleCreat *req,
                                NvRmMessage_HandleCreatResponse *resp)
{
    struct nvmap_handle_ref *ref;

    resp->msg = NvRmMsg_MemHandleCreate_Response;
    ref = nvmap_create_handle(s_AvpClient, req->size);
    if (IS_ERR(ref)) {
        pr_err("[AVP] error creating handle %ld\n", PTR_ERR(ref));
        resp->error = NvError_InsufficientMemory;
    } else {
        resp->error = NvSuccess;
        resp->hMem = (NvRmMemHandle)nvmap_ref_to_id(ref);
    }
}

static void HandleAllocMessage(const NvRmMessage_MemAlloc *req, NvRmMessage_Response *resp)
{
    struct nvmap_handle *handle;
    unsigned int heap_mask = 0;
    unsigned int i;
    size_t align;
    int err;

    resp->msg = NvRmMsg_MemAlloc_Response;

    if (!req->NumHeaps)
        heap_mask = NVMAP_HEAP_CARVEOUT_GENERIC | NVMAP_HEAP_SYSMEM;

    for (i = 0; i < req->NumHeaps; i++) {
        if (req->Heaps[i] == NvRmHeap_GART)
            heap_mask |= NVMAP_HEAP_IOVMM;
        else if (req->Heaps[i] == NvRmHeap_IRam)
            heap_mask |= NVMAP_HEAP_CARVEOUT_IRAM;
        else if (req->Heaps[i] == NvRmHeap_External)
            heap_mask |= NVMAP_HEAP_SYSMEM;
        else if (req->Heaps[i] == NvRmHeap_ExternalCarveOut)
            heap_mask |= NVMAP_HEAP_CARVEOUT_GENERIC;
    }

    handle = nvmap_get_handle_id(s_AvpClient, (unsigned long)req->hMem);
    if (IS_ERR(handle)) {
        resp->error = NvError_AccessDenied;
        return;
    }

    align = max_t(size_t, L1_CACHE_BYTES, req->Alignment);
    err = nvmap_alloc_handle_id(s_AvpClient, (unsigned long)req->hMem,
                                heap_mask, align, 0);
    nvmap_handle_put(handle);

    if (err) {
        pr_err("[AVP] allocate handle error %d\n", err);
        resp->error = NvError_InsufficientMemory;
    } else {
        resp->error = NvSuccess;
    }
}
void NvRmPrivProcessMessage(NvRmRPCHandle hRPCHandle, char *pRecvMessage, int messageLength)
{
    switch (*(NvRmMsg *)pRecvMessage) {

    case NvRmMsg_MemHandleCreate:
    {
        NvRmMessage_HandleCreat *msgHandleCreate = NULL;
        NvRmMessage_HandleCreatResponse msgRHandleCreate;

        msgHandleCreate = (NvRmMessage_HandleCreat*)pRecvMessage;
        HandleCreateMessage(msgHandleCreate, &msgRHandleCreate);
        NvRmPrivRPCSendMsg(hRPCHandle, &msgRHandleCreate,
                           sizeof(msgRHandleCreate));
        barrier();
    }
    break;
    case NvRmMsg_MemHandleOpen:
        break;
    case NvRmMsg_MemHandleFree:
    {
        NvRmMessage_HandleFree *msgHandleFree = NULL;
        msgHandleFree = (NvRmMessage_HandleFree*)pRecvMessage;
        nvmap_free_handle_id(s_AvpClient, (unsigned long)msgHandleFree->hMem);
        barrier();
    }
    break;
    case NvRmMsg_MemAlloc:
    {
        NvRmMessage_MemAlloc *msgMemAlloc = NULL;
        NvRmMessage_Response msgResponse;
        msgMemAlloc = (NvRmMessage_MemAlloc*)pRecvMessage;

        HandleAllocMessage(msgMemAlloc, &msgResponse);
        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(msgResponse));
        barrier();
    }
    break;
    case NvRmMsg_MemPin:
    {
        struct nvmap_handle_ref *ref;
        NvRmMessage_Pin *msg;
        NvRmMessage_PinResponse response;
        unsigned long id;
        int err;

        msg = (NvRmMessage_Pin *)pRecvMessage;
        id = (unsigned long)msg->hMem;
        response.msg = NvRmMsg_MemPin_Response;

        ref = nvmap_duplicate_handle_id(s_AvpClient, id);
        if (IS_ERR(ref)) {
            pr_err("[AVP] unable to duplicate handle for pin\n");
            err = PTR_ERR(ref);
        } else {
            err = nvmap_pin_ids(s_AvpClient, 1, &id);
        }
        if (!err) {
            response.address = nvmap_handle_address(s_AvpClient, id);
        } else {
            pr_err("[AVP] pin error %d\n", err);
            response.address = 0xffffffff;
        }

        NvRmPrivRPCSendMsg(hRPCHandle, &response, sizeof(response));
        barrier();
    }
    break;
    case NvRmMsg_MemUnpin:
    {
        NvRmMessage_HandleFree *msg = NULL;
        NvRmMessage_Response msgResponse;
        unsigned long id;

        msg = (NvRmMessage_HandleFree*)pRecvMessage;
        id = (unsigned long)msg->hMem;
        nvmap_unpin_ids(s_AvpClient, 1, &id);
	nvmap_free_handle_id(s_AvpClient, id);

        msgResponse.msg = NvRmMsg_MemUnpin_Response;
        msgResponse.error = NvSuccess;

        NvRmPrivRPCSendMsg(hRPCHandle, &msgResponse, sizeof(msgResponse));
        barrier();
    }
    break;
    case NvRmMsg_MemGetAddress:
    {
        NvRmMessage_GetAddress *msg = NULL;
        NvRmMessage_GetAddressResponse response;
        unsigned long address;

        msg = (NvRmMessage_GetAddress*)pRecvMessage;
        address = nvmap_handle_address(s_AvpClient, (unsigned long)msg->hMem);
        response.address = address + msg->Offset;
        response.msg = NvRmMsg_MemGetAddress_Response;
        NvRmPrivRPCSendMsg(hRPCHandle, &response, sizeof(response));
        barrier();
    }
    break;
    case NvRmMsg_HandleFromId:
    {
        NvRmMessage_HandleFromId *msg = NULL;
        struct nvmap_handle_ref *ref;
        NvRmMessage_Response response;

        msg = (NvRmMessage_HandleFromId*)pRecvMessage;
        ref = nvmap_duplicate_handle_id(s_AvpClient, msg->id);

        response.msg = NvRmMsg_HandleFromId_Response;
        if (IS_ERR(ref)) {
            response.error = NvError_InsufficientMemory;
            pr_err("[AVP] duplicate handle error %ld\n", PTR_ERR(ref));
        } else {
            response.error = NvSuccess;
        }
        NvRmPrivRPCSendMsg(hRPCHandle, &response, sizeof(response));
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

