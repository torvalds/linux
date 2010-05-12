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

#ifndef INCLUDED_nvrm_xpc_H
#define INCLUDED_nvrm_xpc_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_module.h"
#include "nvrm_init.h"

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_init.h"
#include "nvrm_interrupt.h"

/** 
 * @brief 16 Byte allignment for the shared memory message transfer.
 */

typedef enum
{
    XPC_MESSAGE_ALIGNMENT_SIZE = 0x10,
    Xpc_Alignment_Num,
    Xpc_Alignment_Force32 = 0x7FFFFFFF
} Xpc_Alignment;

/**
 * NvRmPrivXpcMessageHandle is an opaque handle to NvRmPrivXpcMessage.
 * 
 * @ingroup nvrm_xpc
 */

typedef struct NvRmPrivXpcMessageRec *NvRmPrivXpcMessageHandle;

/**
 * Create the xpc message handles for sending/receiving the message to/from 
 * target processor.
 * This function allocates the memory (from multiprocessor shared memory 
 * region) and os resources for the message transfer and synchrnoisation.
 *
 * @see NvRmPrivXpcSendMessage()
 * @see NvRmPrivXpcGetMessage()
 *
 * @param hDevice Handle to the Rm device which is required by Ddk to acquire 
 * the resources from RM.
 * @param phXpcMessage Pointer to the handle to Xpc message where created 
 * Xpc message handle is stored.
 *
 * @retval NvSuccess Indicates the message queue is successfully created.
 * @retval NvError_BadValue The parameter passed are incorrect.
 * @retval NvError_InsufficientMemory Indicates that function fails to allocate the 
 * memory for message queue.
 * @retval NvError_MemoryMapFailed Indicates that the memory mapping for xpc 
 * controller register failed.
 * @retval NvError_NotSupported Indicates that the requested operation is not 
 * supported for the given target processor/Instance.
 * 
 */
 
 NvError NvRmPrivXpcCreate( 
    NvRmDeviceHandle hDevice,
    NvRmPrivXpcMessageHandle * phXpcMessage );

/**
 * Destroy the created Xpc message handle. This frees all the resources
 * allocated for the xpc message handle.
 *
 * @note After calling this function client will not able to  send/receive any 
 * message.
 *
 * @see NvRmPrivXpcMessageCreate()
 *
 * @param hXpcMessage Xpc message queue handle which need to be destroy. 
 * This cas created when function NvRmPrivXpcMessageCreate() was called.
 *
 */

 void NvRmPrivXpcDestroy( 
    NvRmPrivXpcMessageHandle hXpcMessage );

 NvError NvRmPrivXpcSendMessage( 
    NvRmPrivXpcMessageHandle hXpcMessage,
    NvU32 data );

 NvU32 NvRmPrivXpcGetMessage( 
    NvRmPrivXpcMessageHandle hXpcMessage );

/**
 * Initializes the Arbitration semaphore system for cross processor synchronization.
 *
 * @param hDevice The RM handle.
 *
 * @retval "NvError_IrqRegistrationFailed" if interupt is already registred.
 * @retval "NvSuccess" if successfull.
 */

 NvError NvRmXpcInitArbSemaSystem( 
    NvRmDeviceHandle hDevice );

/**
 * Tries to obtain a hw arbitration semaphore. This API is used to
 * synchronize access to hw blocks across processors.
 *
 * @param modId The module that we need to cross-processor safe access to.
 */

 void NvRmXpcModuleAcquire( 
    NvRmModuleID modId );

/**
 * Releases the arbitration semaphore corresponding to the given module id.
 *
 * @param modId The module that we are releasing.
 */

 void NvRmXpcModuleRelease( 
    NvRmModuleID modId );

#if defined(__cplusplus)
}
#endif

#endif
