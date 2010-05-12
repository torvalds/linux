/*
 * Copyright (c) 2009 NVIDIA Corporation.
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

#define NV_IDL_IS_DISPATCH

#include "nvcommon.h"
#include "nvos.h"
#include "nvassert.h"
#include "nvreftrack.h"
#include "nvidlcmd.h"
#include "nvrm_init.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmClose_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
} NV_ALIGN(4) NvRmClose_in;

typedef struct NvRmClose_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmClose_inout;

typedef struct NvRmClose_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmClose_out;

typedef struct NvRmClose_params_t
{
    NvRmClose_in in;
    NvRmClose_inout inout;
    NvRmClose_out out;
} NvRmClose_params;

typedef struct NvRmOpenNew_in_t
{
    NvU32 package_;
    NvU32 function_;
} NV_ALIGN(4) NvRmOpenNew_in;

typedef struct NvRmOpenNew_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmOpenNew_inout;

typedef struct NvRmOpenNew_out_t
{
    NvError ret_;
    NvRmDeviceHandle pHandle;
} NV_ALIGN(4) NvRmOpenNew_out;

typedef struct NvRmOpenNew_params_t
{
    NvRmOpenNew_in in;
    NvRmOpenNew_inout inout;
    NvRmOpenNew_out out;
} NvRmOpenNew_params;

typedef struct NvRmInit_in_t
{
    NvU32 package_;
    NvU32 function_;
} NV_ALIGN(4) NvRmInit_in;

typedef struct NvRmInit_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmInit_inout;

typedef struct NvRmInit_out_t
{
    NvRmDeviceHandle pHandle;
} NV_ALIGN(4) NvRmInit_out;

typedef struct NvRmInit_params_t
{
    NvRmInit_in in;
    NvRmInit_inout inout;
    NvRmInit_out out;
} NvRmInit_params;

typedef struct NvRmOpen_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvU32 DeviceId;
} NV_ALIGN(4) NvRmOpen_in;

typedef struct NvRmOpen_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmOpen_inout;

typedef struct NvRmOpen_out_t
{
    NvError ret_;
    NvRmDeviceHandle pHandle;
} NV_ALIGN(4) NvRmOpen_out;

typedef struct NvRmOpen_params_t
{
    NvRmOpen_in in;
    NvRmOpen_inout inout;
    NvRmOpen_out out;
} NvRmOpen_params;

static NvError NvRmClose_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmClose_in *p_in;

    p_in = (NvRmClose_in *)InBuffer;


    NvRmClose( p_in->hDevice );

    return err_;
}

static NvError NvRmOpenNew_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmOpenNew_in *p_in;
    NvRmOpenNew_out *p_out;

    p_in = (NvRmOpenNew_in *)InBuffer;
    p_out = (NvRmOpenNew_out *)((NvU8 *)OutBuffer + OFFSET(NvRmOpenNew_params, out) - OFFSET(NvRmOpenNew_params, inout));


    p_out->ret_ = NvRmOpenNew( &p_out->pHandle );

    return err_;
}

static NvError NvRmInit_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmInit_in *p_in;
    NvRmInit_out *p_out;

    p_in = (NvRmInit_in *)InBuffer;
    p_out = (NvRmInit_out *)((NvU8 *)OutBuffer + OFFSET(NvRmInit_params, out) - OFFSET(NvRmInit_params, inout));


    NvRmInit( &p_out->pHandle );

    return err_;
}

static NvError NvRmOpen_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmOpen_in *p_in;
    NvRmOpen_out *p_out;

    p_in = (NvRmOpen_in *)InBuffer;
    p_out = (NvRmOpen_out *)((NvU8 *)OutBuffer + OFFSET(NvRmOpen_params, out) - OFFSET(NvRmOpen_params, inout));


    p_out->ret_ = NvRmOpen( &p_out->pHandle, p_in->DeviceId );

    return err_;
}

NvError nvrm_init_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_init_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 3:
        err_ = NvRmClose_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmOpenNew_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmInit_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmOpen_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
