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
#include "nvrm_interrupt.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmGetIrqCountForLogicalInterrupt_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvRmModuleID ModuleID;
} NV_ALIGN(4) NvRmGetIrqCountForLogicalInterrupt_in;

typedef struct NvRmGetIrqCountForLogicalInterrupt_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGetIrqCountForLogicalInterrupt_inout;

typedef struct NvRmGetIrqCountForLogicalInterrupt_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmGetIrqCountForLogicalInterrupt_out;

typedef struct NvRmGetIrqCountForLogicalInterrupt_params_t
{
    NvRmGetIrqCountForLogicalInterrupt_in in;
    NvRmGetIrqCountForLogicalInterrupt_inout inout;
    NvRmGetIrqCountForLogicalInterrupt_out out;
} NvRmGetIrqCountForLogicalInterrupt_params;

typedef struct NvRmGetIrqForLogicalInterrupt_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvRmModuleID ModuleID;
    NvU32 Index;
} NV_ALIGN(4) NvRmGetIrqForLogicalInterrupt_in;

typedef struct NvRmGetIrqForLogicalInterrupt_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGetIrqForLogicalInterrupt_inout;

typedef struct NvRmGetIrqForLogicalInterrupt_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmGetIrqForLogicalInterrupt_out;

typedef struct NvRmGetIrqForLogicalInterrupt_params_t
{
    NvRmGetIrqForLogicalInterrupt_in in;
    NvRmGetIrqForLogicalInterrupt_inout inout;
    NvRmGetIrqForLogicalInterrupt_out out;
} NvRmGetIrqForLogicalInterrupt_params;

static NvError NvRmGetIrqCountForLogicalInterrupt_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGetIrqCountForLogicalInterrupt_in *p_in;
    NvRmGetIrqCountForLogicalInterrupt_out *p_out;

    p_in = (NvRmGetIrqCountForLogicalInterrupt_in *)InBuffer;
    p_out = (NvRmGetIrqCountForLogicalInterrupt_out *)((NvU8 *)OutBuffer + OFFSET(NvRmGetIrqCountForLogicalInterrupt_params, out) - OFFSET(NvRmGetIrqCountForLogicalInterrupt_params, inout));


    p_out->ret_ = NvRmGetIrqCountForLogicalInterrupt( p_in->hRmDevice, p_in->ModuleID );

    return err_;
}

static NvError NvRmGetIrqForLogicalInterrupt_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGetIrqForLogicalInterrupt_in *p_in;
    NvRmGetIrqForLogicalInterrupt_out *p_out;

    p_in = (NvRmGetIrqForLogicalInterrupt_in *)InBuffer;
    p_out = (NvRmGetIrqForLogicalInterrupt_out *)((NvU8 *)OutBuffer + OFFSET(NvRmGetIrqForLogicalInterrupt_params, out) - OFFSET(NvRmGetIrqForLogicalInterrupt_params, inout));


    p_out->ret_ = NvRmGetIrqForLogicalInterrupt( p_in->hRmDevice, p_in->ModuleID, p_in->Index );

    return err_;
}

NvError nvrm_interrupt_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_interrupt_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 1:
        err_ = NvRmGetIrqCountForLogicalInterrupt_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmGetIrqForLogicalInterrupt_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
