
#define NV_IDL_IS_DISPATCH

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

#include "nvcommon.h"
#include "nvos.h"
#include "nvassert.h"
#include "nvreftrack.h"
#include "nvidlcmd.h"
#include "nvrm_xpc.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmXpcModuleRelease_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmModuleID modId;
} NV_ALIGN(4) NvRmXpcModuleRelease_in;

typedef struct NvRmXpcModuleRelease_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmXpcModuleRelease_inout;

typedef struct NvRmXpcModuleRelease_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmXpcModuleRelease_out;

typedef struct NvRmXpcModuleRelease_params_t
{
    NvRmXpcModuleRelease_in in;
    NvRmXpcModuleRelease_inout inout;
    NvRmXpcModuleRelease_out out;
} NvRmXpcModuleRelease_params;

typedef struct NvRmXpcModuleAcquire_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmModuleID modId;
} NV_ALIGN(4) NvRmXpcModuleAcquire_in;

typedef struct NvRmXpcModuleAcquire_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmXpcModuleAcquire_inout;

typedef struct NvRmXpcModuleAcquire_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmXpcModuleAcquire_out;

typedef struct NvRmXpcModuleAcquire_params_t
{
    NvRmXpcModuleAcquire_in in;
    NvRmXpcModuleAcquire_inout inout;
    NvRmXpcModuleAcquire_out out;
} NvRmXpcModuleAcquire_params;

typedef struct NvRmXpcInitArbSemaSystem_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
} NV_ALIGN(4) NvRmXpcInitArbSemaSystem_in;

typedef struct NvRmXpcInitArbSemaSystem_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmXpcInitArbSemaSystem_inout;

typedef struct NvRmXpcInitArbSemaSystem_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmXpcInitArbSemaSystem_out;

typedef struct NvRmXpcInitArbSemaSystem_params_t
{
    NvRmXpcInitArbSemaSystem_in in;
    NvRmXpcInitArbSemaSystem_inout inout;
    NvRmXpcInitArbSemaSystem_out out;
} NvRmXpcInitArbSemaSystem_params;

typedef struct NvRmPrivXpcGetMessage_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmPrivXpcMessageHandle hXpcMessage;
} NV_ALIGN(4) NvRmPrivXpcGetMessage_in;

typedef struct NvRmPrivXpcGetMessage_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPrivXpcGetMessage_inout;

typedef struct NvRmPrivXpcGetMessage_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmPrivXpcGetMessage_out;

typedef struct NvRmPrivXpcGetMessage_params_t
{
    NvRmPrivXpcGetMessage_in in;
    NvRmPrivXpcGetMessage_inout inout;
    NvRmPrivXpcGetMessage_out out;
} NvRmPrivXpcGetMessage_params;

typedef struct NvRmPrivXpcSendMessage_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmPrivXpcMessageHandle hXpcMessage;
    NvU32 data;
} NV_ALIGN(4) NvRmPrivXpcSendMessage_in;

typedef struct NvRmPrivXpcSendMessage_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPrivXpcSendMessage_inout;

typedef struct NvRmPrivXpcSendMessage_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmPrivXpcSendMessage_out;

typedef struct NvRmPrivXpcSendMessage_params_t
{
    NvRmPrivXpcSendMessage_in in;
    NvRmPrivXpcSendMessage_inout inout;
    NvRmPrivXpcSendMessage_out out;
} NvRmPrivXpcSendMessage_params;

typedef struct NvRmPrivXpcDestroy_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmPrivXpcMessageHandle hXpcMessage;
} NV_ALIGN(4) NvRmPrivXpcDestroy_in;

typedef struct NvRmPrivXpcDestroy_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPrivXpcDestroy_inout;

typedef struct NvRmPrivXpcDestroy_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPrivXpcDestroy_out;

typedef struct NvRmPrivXpcDestroy_params_t
{
    NvRmPrivXpcDestroy_in in;
    NvRmPrivXpcDestroy_inout inout;
    NvRmPrivXpcDestroy_out out;
} NvRmPrivXpcDestroy_params;

typedef struct NvRmPrivXpcCreate_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
} NV_ALIGN(4) NvRmPrivXpcCreate_in;

typedef struct NvRmPrivXpcCreate_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPrivXpcCreate_inout;

typedef struct NvRmPrivXpcCreate_out_t
{
    NvError ret_;
    NvRmPrivXpcMessageHandle phXpcMessage;
} NV_ALIGN(4) NvRmPrivXpcCreate_out;

typedef struct NvRmPrivXpcCreate_params_t
{
    NvRmPrivXpcCreate_in in;
    NvRmPrivXpcCreate_inout inout;
    NvRmPrivXpcCreate_out out;
} NvRmPrivXpcCreate_params;

static NvError NvRmXpcModuleRelease_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmXpcModuleRelease_in *p_in;

    p_in = (NvRmXpcModuleRelease_in *)InBuffer;


    NvRmXpcModuleRelease( p_in->modId );

    return err_;
}

static NvError NvRmXpcModuleAcquire_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmXpcModuleAcquire_in *p_in;

    p_in = (NvRmXpcModuleAcquire_in *)InBuffer;


    NvRmXpcModuleAcquire( p_in->modId );

    return err_;
}

static NvError NvRmXpcInitArbSemaSystem_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmXpcInitArbSemaSystem_in *p_in;
    NvRmXpcInitArbSemaSystem_out *p_out;

    p_in = (NvRmXpcInitArbSemaSystem_in *)InBuffer;
    p_out = (NvRmXpcInitArbSemaSystem_out *)((NvU8 *)OutBuffer + OFFSET(NvRmXpcInitArbSemaSystem_params, out) - OFFSET(NvRmXpcInitArbSemaSystem_params, inout));


    p_out->ret_ = NvRmXpcInitArbSemaSystem( p_in->hDevice );

    return err_;
}

static NvError NvRmPrivXpcGetMessage_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPrivXpcGetMessage_in *p_in;
    NvRmPrivXpcGetMessage_out *p_out;

    p_in = (NvRmPrivXpcGetMessage_in *)InBuffer;
    p_out = (NvRmPrivXpcGetMessage_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPrivXpcGetMessage_params, out) - OFFSET(NvRmPrivXpcGetMessage_params, inout));


    p_out->ret_ = NvRmPrivXpcGetMessage( p_in->hXpcMessage );

    return err_;
}

static NvError NvRmPrivXpcSendMessage_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPrivXpcSendMessage_in *p_in;
    NvRmPrivXpcSendMessage_out *p_out;

    p_in = (NvRmPrivXpcSendMessage_in *)InBuffer;
    p_out = (NvRmPrivXpcSendMessage_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPrivXpcSendMessage_params, out) - OFFSET(NvRmPrivXpcSendMessage_params, inout));


    p_out->ret_ = NvRmPrivXpcSendMessage( p_in->hXpcMessage, p_in->data );

    return err_;
}

static NvError NvRmPrivXpcDestroy_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPrivXpcDestroy_in *p_in;

    p_in = (NvRmPrivXpcDestroy_in *)InBuffer;


    NvRmPrivXpcDestroy( p_in->hXpcMessage );

    return err_;
}

static NvError NvRmPrivXpcCreate_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPrivXpcCreate_in *p_in;
    NvRmPrivXpcCreate_out *p_out;

    p_in = (NvRmPrivXpcCreate_in *)InBuffer;
    p_out = (NvRmPrivXpcCreate_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPrivXpcCreate_params, out) - OFFSET(NvRmPrivXpcCreate_params, inout));


    p_out->ret_ = NvRmPrivXpcCreate( p_in->hDevice, &p_out->phXpcMessage );

    return err_;
}

NvError nvrm_xpc_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_xpc_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 6:
        err_ = NvRmXpcModuleRelease_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 5:
        err_ = NvRmXpcModuleAcquire_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmXpcInitArbSemaSystem_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmPrivXpcGetMessage_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmPrivXpcSendMessage_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmPrivXpcDestroy_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmPrivXpcCreate_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
