/*
 * arch/arm/mach-tegra/nvrm/core/common/nvrm_message.h
 *
 *
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

#ifndef INCLUDED_NVRM_MESSAGE_H
#define INCLUDED_NVRM_MESSAGE_H

#include "nvrm_memmgr.h"
#include "nvrm_module.h"
#include "nvrm_transport.h"
#include "nvrm_power.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

/* Maximum message queue depth */
enum {MAX_QUEUE_DEPTH = 5};
/* Maximum message length */
enum {MAX_MESSAGE_LENGTH = 256};
/* Maximum argument size */
enum {MAX_ARGS_SIZE = 220};
/* Max String length */
enum {MAX_STRING_LENGTH = 200};

typedef struct NvRmRPCRec
{
    NvRmTransportHandle svcTransportHandle;
    NvOsSemaphoreHandle TransportRecvSemId;
    NvOsMutexHandle RecvLock;
    NvRmDeviceHandle hRmDevice;
    NvBool isConnected;
} NvRmRPC;

typedef struct NvRmRPCRec *NvRmRPCHandle;

void NvRmPrivProcessMessage(NvRmRPCHandle hRPCHandle, char *pRecvMessage, int messageLength);

typedef enum
{
    NvRmMsg_MemHandleCreate = 0x0,
    NvRmMsg_MemHandleCreate_Response,
    NvRmMsg_MemHandleOpen,
    NvRmMsg_MemHandleFree,
    NvRmMsg_MemAlloc,
    NvRmMsg_MemAlloc_Response,
    NvRmMsg_MemPin,
    NvRmMsg_MemPin_Response,
    NvRmMsg_MemUnpin,
    NvRmMsg_MemUnpin_Response,
    NvRmMsg_MemGetAddress,
    NvRmMsg_MemGetAddress_Response,
    NvRmMsg_HandleFromId,
    NvRmMsg_HandleFromId_Response,
    NvRmMsg_PowerModuleClockControl,
    NvRmMsg_PowerModuleClockControl_Response,
    NvRmMsg_ModuleReset,
    NvRmMsg_ModuleReset_Response,
    NvRmMsg_PowerRegister,
    NvRmMsg_PowerUnRegister,
    NvRmMsg_PowerStarvationHint,
    NvRmMsg_PowerBusyHint,
    NvRmMsg_PowerBusyMultiHint,
    NvRmMsg_PowerDfsGetState,
    NvRmMsg_PowerDfsGetState_Response,
    NvRmMsg_PowerResponse,
    NvRmMsg_PowerModuleGetMaxFreq,
    NvRmMsg_InitiateLP0,
    NvRmMsg_InitiateLP0_Response,
    NvRmMsg_RemotePrintf,
    NvRmMsg_AttachModule,
    NvRmMsg_AttachModule_Response,
    NvRmMsg_DetachModule,
    NvRmMsg_DetachModule_Response,
    NvRmMsg_AVP_Reset,
    NvRmMsg_PowerDfsGetClockUtilization,
    NvRmMsg_PowerDfsGetClockUtilization_Response,
    NvRmMsg_Force32 = 0x7FFFFFFF
}NvRmMsg;

typedef struct{
    NvRmMsg         msg;
    NvU32           size;
}NvRmMessage_HandleCreat;

typedef struct{
    NvRmMsg         msg;
    NvRmMemHandle   hMem;
    NvError         error;
}NvRmMessage_HandleCreatResponse;

typedef struct{
    NvRmMsg        msg;
    NvRmMemHandle  hMem;
}NvRmMessage_HandleFree;

typedef struct{
    NvRmMsg     msg;
    NvError     error;
}NvRmMessage_Response;

typedef struct{
    NvRmMsg             msg;
    NvRmMemHandle       hMem;
    NvRmHeap            Heaps[4];
    NvU32               NumHeaps;
    NvU32               Alignment;
    NvOsMemAttribute    Coherency;
}NvRmMessage_MemAlloc;

typedef struct{
    NvRmMsg       msg;
    NvRmMemHandle hMem;
    NvU32         Offset;
}NvRmMessage_GetAddress;

typedef struct{
    NvRmMsg       msg;
    NvU32         address;
}NvRmMessage_GetAddressResponse;

typedef struct{
    NvRmMsg       msg;
    NvU32         id;
}NvRmMessage_HandleFromId;

typedef struct{
    NvRmMsg       msg;
    NvRmMemHandle hMem;
}NvRmMessage_Pin;

typedef struct{
    NvRmMsg       msg;
    NvU32         address;
}NvRmMessage_PinResponse;

typedef struct{
    NvRmMsg         msg;
    NvRmModuleID    ModuleId;
    NvU32           ClientId;
    NvBool          Enable;
}NvRmMessage_Module;

typedef struct{
    NvRmMsg msg;
    NvU32 clientId;
    NvOsSemaphoreHandle eventSema;
}NvRmMessage_PowerRegister;

typedef struct{
    NvRmMsg msg;
    NvU32 clientId;
}NvRmMessage_PowerUnRegister;

typedef struct{
    NvRmMsg msg;
    NvRmDfsClockId clockId;
    NvU32 clientId;
    NvBool starving;
}NvRmMessage_PowerStarvationHint;

typedef struct{
    NvRmMsg msg;
    NvRmDfsClockId clockId;
    NvU32 clientId;
    NvU32 boostDurationMS;
    NvRmFreqKHz boostKHz;
}NvRmMessage_PowerBusyHint;

typedef struct{
    NvRmMsg msg;
    NvU32 numHints;
    NvU8 busyHints[MAX_STRING_LENGTH];
}NvRmMessage_PowerBusyMultiHint;

typedef struct{
    NvRmMsg msg;
}NvRmMessage_PowerDfsGetState;

typedef struct{
    NvRmMsg msg;
    NvError error;
    NvU32 clientId;
}NvRmMessage_PowerRegister_Response;

typedef struct{
    NvRmMsg msg;
    NvRmDfsRunState state;
}NvRmMessage_PowerDfsGetState_Response;

typedef struct{
    NvRmMsg msg;
    NvRmModuleID moduleID;
}NvRmMessage_PowerModuleGetMaxFreq;

typedef struct{
    NvRmMsg msg;
    NvRmFreqKHz freqKHz;
}NvRmMessage_PowerModuleGetMaxFreq_Response;

typedef struct{
    NvRmMsg msg;
    NvError error;
    NvRmDfsClockId clockId;
}NvRmMessage_PowerDfsGetClockUtilization;

typedef struct{
    NvRmMsg msg;
    NvError error;
    NvRmDfsClockUsage clockUsage;
}NvRmMessage_PowerDfsGetClockUtilization_Response;

typedef struct{
    NvRmMsg msg;
    NvU32 sourceAddr;
    NvU32 bufferAddr;
    NvU32 bufferSize;
} NvRmMessage_InitiateLP0;

typedef struct{
    NvRmMsg msg;
    const char string[MAX_STRING_LENGTH];
} NvRmMessage_RemotePrintf;


typedef struct{
    NvRmMsg       msg;
    NvU32         address;
    NvU32         size;
    NvU32         filesize;
    char          args[MAX_ARGS_SIZE];
    NvU32         reason;
}NvRmMessage_AttachModule;

typedef struct {
    NvRmMsg       msg;
    NvError       error;
    NvU32         libraryId;
}NvRmMessage_AttachModuleResponse;

typedef struct {
    NvRmMsg       msg;
    NvU32         reason;
    NvU32         libraryId;
}NvRmMessage_DetachModule;

typedef struct{
    NvRmMsg       msg;
    NvError       error;
}NvRmMessage_DetachModuleResponse;

#ifdef __cplusplus
}
#endif  /* __cplusplus */


#endif
