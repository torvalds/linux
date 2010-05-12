/*
 * Copyright (c) 2010 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
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

// Maximum message queue depth
enum {MAX_QUEUE_DEPTH = 5};
// Maximum message length
enum {MAX_MESSAGE_LENGTH = 256};
// Maximum argument size
enum {MAX_ARGS_SIZE = 220};
// Max String length
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
    NvRmHeap            Heaps[NvRmHeap_Num];
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
    NvU32         entryAddress;
    NvU32         size;
    char          args[MAX_ARGS_SIZE];
    NvU32         reason;
}NvRmMessage_AttachModule;

typedef struct{
    NvRmMsg       msg;
    NvError       error;
}NvRmMessage_AttachModuleResponse;

typedef struct{
    NvRmMsg       msg;
    NvU32         reason;
    NvU32         entryAddress;
}NvRmMessage_DetachModule;

typedef struct{
    NvRmMsg       msg;
    NvError       error;
}NvRmMessage_DetachModuleResponse;

#ifdef __cplusplus
}
#endif  /* __cplusplus */


#endif
