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
#include "nvrm_pinmux.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmGetStraps_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvRmStrapGroup StrapGroup;
} NV_ALIGN(4) NvRmGetStraps_in;

typedef struct NvRmGetStraps_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGetStraps_inout;

typedef struct NvRmGetStraps_out_t
{
    NvError ret_;
    NvU32 pStrapValue;
} NV_ALIGN(4) NvRmGetStraps_out;

typedef struct NvRmGetStraps_params_t
{
    NvRmGetStraps_in in;
    NvRmGetStraps_inout inout;
    NvRmGetStraps_out out;
} NvRmGetStraps_params;

typedef struct NvRmGetModuleInterfaceCapabilities_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRm;
    NvRmModuleID ModuleId;
    NvU32 CapStructSize;
    void* pCaps;
} NV_ALIGN(4) NvRmGetModuleInterfaceCapabilities_in;

typedef struct NvRmGetModuleInterfaceCapabilities_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGetModuleInterfaceCapabilities_inout;

typedef struct NvRmGetModuleInterfaceCapabilities_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmGetModuleInterfaceCapabilities_out;

typedef struct NvRmGetModuleInterfaceCapabilities_params_t
{
    NvRmGetModuleInterfaceCapabilities_in in;
    NvRmGetModuleInterfaceCapabilities_inout inout;
    NvRmGetModuleInterfaceCapabilities_out out;
} NvRmGetModuleInterfaceCapabilities_params;

typedef struct NvRmExternalClockConfig_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 IoModule;
    NvU32 Instance;
    NvU32 Config;
    NvBool EnableTristate;
} NV_ALIGN(4) NvRmExternalClockConfig_in;

typedef struct NvRmExternalClockConfig_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmExternalClockConfig_inout;

typedef struct NvRmExternalClockConfig_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmExternalClockConfig_out;

typedef struct NvRmExternalClockConfig_params_t
{
    NvRmExternalClockConfig_in in;
    NvRmExternalClockConfig_inout inout;
    NvRmExternalClockConfig_out out;
} NvRmExternalClockConfig_params;

typedef struct NvRmSetOdmModuleTristate_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 OdmModule;
    NvU32 OdmInstance;
    NvBool EnableTristate;
} NV_ALIGN(4) NvRmSetOdmModuleTristate_in;

typedef struct NvRmSetOdmModuleTristate_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSetOdmModuleTristate_inout;

typedef struct NvRmSetOdmModuleTristate_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmSetOdmModuleTristate_out;

typedef struct NvRmSetOdmModuleTristate_params_t
{
    NvRmSetOdmModuleTristate_in in;
    NvRmSetOdmModuleTristate_inout inout;
    NvRmSetOdmModuleTristate_out out;
} NvRmSetOdmModuleTristate_params;

typedef struct NvRmSetModuleTristate_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvRmModuleID RmModule;
    NvBool EnableTristate;
} NV_ALIGN(4) NvRmSetModuleTristate_in;

typedef struct NvRmSetModuleTristate_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmSetModuleTristate_inout;

typedef struct NvRmSetModuleTristate_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmSetModuleTristate_out;

typedef struct NvRmSetModuleTristate_params_t
{
    NvRmSetModuleTristate_in in;
    NvRmSetModuleTristate_inout inout;
    NvRmSetModuleTristate_out out;
} NvRmSetModuleTristate_params;

static NvError NvRmGetStraps_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGetStraps_in *p_in;
    NvRmGetStraps_out *p_out;

    p_in = (NvRmGetStraps_in *)InBuffer;
    p_out = (NvRmGetStraps_out *)((NvU8 *)OutBuffer + OFFSET(NvRmGetStraps_params, out) - OFFSET(NvRmGetStraps_params, inout));


    p_out->ret_ = NvRmGetStraps( p_in->hDevice, p_in->StrapGroup, &p_out->pStrapValue );

    return err_;
}

static NvError NvRmGetModuleInterfaceCapabilities_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGetModuleInterfaceCapabilities_in *p_in;
    NvRmGetModuleInterfaceCapabilities_out *p_out;
    void*  pCaps = NULL;

    p_in = (NvRmGetModuleInterfaceCapabilities_in *)InBuffer;
    p_out = (NvRmGetModuleInterfaceCapabilities_out *)((NvU8 *)OutBuffer + OFFSET(NvRmGetModuleInterfaceCapabilities_params, out) - OFFSET(NvRmGetModuleInterfaceCapabilities_params, inout));

    if( p_in->CapStructSize && p_in->pCaps )
    {
        pCaps = (void*  )NvOsAlloc( p_in->CapStructSize );
        if( !pCaps )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmGetModuleInterfaceCapabilities( p_in->hRm, p_in->ModuleId, p_in->CapStructSize, pCaps );

    if(p_in->pCaps && pCaps)
    {
        err_ = NvOsCopyOut( p_in->pCaps, pCaps, p_in->CapStructSize );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pCaps );
    return err_;
}

static NvError NvRmExternalClockConfig_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmExternalClockConfig_in *p_in;
    NvRmExternalClockConfig_out *p_out;

    p_in = (NvRmExternalClockConfig_in *)InBuffer;
    p_out = (NvRmExternalClockConfig_out *)((NvU8 *)OutBuffer + OFFSET(NvRmExternalClockConfig_params, out) - OFFSET(NvRmExternalClockConfig_params, inout));


    p_out->ret_ = NvRmExternalClockConfig( p_in->hDevice, p_in->IoModule, p_in->Instance, p_in->Config, p_in->EnableTristate );

    return err_;
}

static NvError NvRmSetOdmModuleTristate_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmSetOdmModuleTristate_in *p_in;
    NvRmSetOdmModuleTristate_out *p_out;

    p_in = (NvRmSetOdmModuleTristate_in *)InBuffer;
    p_out = (NvRmSetOdmModuleTristate_out *)((NvU8 *)OutBuffer + OFFSET(NvRmSetOdmModuleTristate_params, out) - OFFSET(NvRmSetOdmModuleTristate_params, inout));


    p_out->ret_ = NvRmSetOdmModuleTristate( p_in->hDevice, p_in->OdmModule, p_in->OdmInstance, p_in->EnableTristate );

    return err_;
}

static NvError NvRmSetModuleTristate_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmSetModuleTristate_in *p_in;
    NvRmSetModuleTristate_out *p_out;

    p_in = (NvRmSetModuleTristate_in *)InBuffer;
    p_out = (NvRmSetModuleTristate_out *)((NvU8 *)OutBuffer + OFFSET(NvRmSetModuleTristate_params, out) - OFFSET(NvRmSetModuleTristate_params, inout));


    p_out->ret_ = NvRmSetModuleTristate( p_in->hDevice, p_in->RmModule, p_in->EnableTristate );

    return err_;
}

NvError nvrm_pinmux_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_pinmux_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 4:
        err_ = NvRmGetStraps_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmGetModuleInterfaceCapabilities_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmExternalClockConfig_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmSetOdmModuleTristate_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmSetModuleTristate_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
