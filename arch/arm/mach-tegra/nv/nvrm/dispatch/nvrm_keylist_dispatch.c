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
#include "nvrm_keylist.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmSetKeyValuePair_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRm;
    NvU32 KeyID;
    NvU32 Value;
} NV_ALIGN(4) NvRmSetKeyValuePair_in;

typedef struct NvRmSetKeyValuePair_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSetKeyValuePair_inout;

typedef struct NvRmSetKeyValuePair_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmSetKeyValuePair_out;

typedef struct NvRmSetKeyValuePair_params_t
{
    NvRmSetKeyValuePair_in in;
    NvRmSetKeyValuePair_inout inout;
    NvRmSetKeyValuePair_out out;
} NvRmSetKeyValuePair_params;

typedef struct NvRmGetKeyValue_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRm;
    NvU32 KeyID;
} NV_ALIGN(4) NvRmGetKeyValue_in;

typedef struct NvRmGetKeyValue_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGetKeyValue_inout;

typedef struct NvRmGetKeyValue_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmGetKeyValue_out;

typedef struct NvRmGetKeyValue_params_t
{
    NvRmGetKeyValue_in in;
    NvRmGetKeyValue_inout inout;
    NvRmGetKeyValue_out out;
} NvRmGetKeyValue_params;

static NvError NvRmSetKeyValuePair_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmSetKeyValuePair_in *p_in;
    NvRmSetKeyValuePair_out *p_out;

    p_in = (NvRmSetKeyValuePair_in *)InBuffer;
    p_out = (NvRmSetKeyValuePair_out *)((NvU8 *)OutBuffer + OFFSET(NvRmSetKeyValuePair_params, out) - OFFSET(NvRmSetKeyValuePair_params, inout));


    p_out->ret_ = NvRmSetKeyValuePair( p_in->hRm, p_in->KeyID, p_in->Value );

    return err_;
}

static NvError NvRmGetKeyValue_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGetKeyValue_in *p_in;
    NvRmGetKeyValue_out *p_out;

    p_in = (NvRmGetKeyValue_in *)InBuffer;
    p_out = (NvRmGetKeyValue_out *)((NvU8 *)OutBuffer + OFFSET(NvRmGetKeyValue_params, out) - OFFSET(NvRmGetKeyValue_params, inout));


    p_out->ret_ = NvRmGetKeyValue( p_in->hRm, p_in->KeyID );

    return err_;
}

NvError nvrm_keylist_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_keylist_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 1:
        err_ = NvRmSetKeyValuePair_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmGetKeyValue_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
