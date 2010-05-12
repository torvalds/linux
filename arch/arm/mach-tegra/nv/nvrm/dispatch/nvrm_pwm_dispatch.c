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
#include "nvrm_pwm.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmPwmConfig_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmPwmHandle hPwm;
    NvRmPwmOutputId OutputId;
    NvRmPwmMode Mode;
    NvU32 DutyCycle;
    NvU32 RequestedFreqHzOrPeriod;
} NV_ALIGN(4) NvRmPwmConfig_in;

typedef struct NvRmPwmConfig_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPwmConfig_inout;

typedef struct NvRmPwmConfig_out_t
{
    NvError ret_;
    NvU32 pCurrentFreqHzOrPeriod;
} NV_ALIGN(4) NvRmPwmConfig_out;

typedef struct NvRmPwmConfig_params_t
{
    NvRmPwmConfig_in in;
    NvRmPwmConfig_inout inout;
    NvRmPwmConfig_out out;
} NvRmPwmConfig_params;

typedef struct NvRmPwmClose_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmPwmHandle hPwm;
} NV_ALIGN(4) NvRmPwmClose_in;

typedef struct NvRmPwmClose_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPwmClose_inout;

typedef struct NvRmPwmClose_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPwmClose_out;

typedef struct NvRmPwmClose_params_t
{
    NvRmPwmClose_in in;
    NvRmPwmClose_inout inout;
    NvRmPwmClose_out out;
} NvRmPwmClose_params;

typedef struct NvRmPwmOpen_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
} NV_ALIGN(4) NvRmPwmOpen_in;

typedef struct NvRmPwmOpen_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPwmOpen_inout;

typedef struct NvRmPwmOpen_out_t
{
    NvError ret_;
    NvRmPwmHandle phPwm;
} NV_ALIGN(4) NvRmPwmOpen_out;

typedef struct NvRmPwmOpen_params_t
{
    NvRmPwmOpen_in in;
    NvRmPwmOpen_inout inout;
    NvRmPwmOpen_out out;
} NvRmPwmOpen_params;

static NvError NvRmPwmConfig_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPwmConfig_in *p_in;
    NvRmPwmConfig_out *p_out;

    p_in = (NvRmPwmConfig_in *)InBuffer;
    p_out = (NvRmPwmConfig_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPwmConfig_params, out) - OFFSET(NvRmPwmConfig_params, inout));


    p_out->ret_ = NvRmPwmConfig( p_in->hPwm, p_in->OutputId, p_in->Mode, p_in->DutyCycle, p_in->RequestedFreqHzOrPeriod, &p_out->pCurrentFreqHzOrPeriod );

    return err_;
}

static NvError NvRmPwmClose_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPwmClose_in *p_in;

    p_in = (NvRmPwmClose_in *)InBuffer;


    NvRmPwmClose( p_in->hPwm );

    return err_;
}

static NvError NvRmPwmOpen_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPwmOpen_in *p_in;
    NvRmPwmOpen_out *p_out;

    p_in = (NvRmPwmOpen_in *)InBuffer;
    p_out = (NvRmPwmOpen_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPwmOpen_params, out) - OFFSET(NvRmPwmOpen_params, inout));


    p_out->ret_ = NvRmPwmOpen( p_in->hDevice, &p_out->phPwm );

    return err_;
}

NvError nvrm_pwm_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_pwm_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 2:
        err_ = NvRmPwmConfig_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmPwmClose_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmPwmOpen_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
