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
#include "nvrm_power.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))

typedef struct NvRmPowerVoltageControl_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID ModuleId;
    NvU32 ClientId;
    NvRmMilliVolts MinVolts;
    NvRmMilliVolts MaxVolts;
    NvRmMilliVolts  * PrefVoltageList;
    NvU32 PrefVoltageListCount;
} NV_ALIGN(4) NvRmPowerVoltageControl_in;

typedef struct NvRmPowerVoltageControl_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerVoltageControl_inout;

typedef struct NvRmPowerVoltageControl_out_t
{
    NvError ret_;
    NvRmMilliVolts CurrentVolts;
} NV_ALIGN(4) NvRmPowerVoltageControl_out;

typedef struct NvRmPowerVoltageControl_params_t
{
    NvRmPowerVoltageControl_in in;
    NvRmPowerVoltageControl_inout inout;
    NvRmPowerVoltageControl_out out;
} NvRmPowerVoltageControl_params;

typedef struct NvRmPowerModuleClockControl_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID ModuleId;
    NvU32 ClientId;
    NvBool Enable;
} NV_ALIGN(4) NvRmPowerModuleClockControl_in;

typedef struct NvRmPowerModuleClockControl_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerModuleClockControl_inout;

typedef struct NvRmPowerModuleClockControl_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmPowerModuleClockControl_out;

typedef struct NvRmPowerModuleClockControl_params_t
{
    NvRmPowerModuleClockControl_in in;
    NvRmPowerModuleClockControl_inout inout;
    NvRmPowerModuleClockControl_out out;
} NvRmPowerModuleClockControl_params;

typedef struct NvRmPowerModuleClockConfig_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID ModuleId;
    NvU32 ClientId;
    NvRmFreqKHz MinFreq;
    NvRmFreqKHz MaxFreq;
    NvRmFreqKHz  * PrefFreqList;
    NvU32 PrefFreqListCount;
    NvU32 flags;
} NV_ALIGN(4) NvRmPowerModuleClockConfig_in;

typedef struct NvRmPowerModuleClockConfig_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerModuleClockConfig_inout;

typedef struct NvRmPowerModuleClockConfig_out_t
{
    NvError ret_;
    NvRmFreqKHz CurrentFreq;
} NV_ALIGN(4) NvRmPowerModuleClockConfig_out;

typedef struct NvRmPowerModuleClockConfig_params_t
{
    NvRmPowerModuleClockConfig_in in;
    NvRmPowerModuleClockConfig_inout inout;
    NvRmPowerModuleClockConfig_out out;
} NvRmPowerModuleClockConfig_params;

static NvError NvRmPowerVoltageControl_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerVoltageControl_in *p_in;
    NvRmPowerVoltageControl_out *p_out;
    NvRmMilliVolts *PrefVoltageList = NULL;

    p_in = (NvRmPowerVoltageControl_in *)InBuffer;
    p_out = (NvRmPowerVoltageControl_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerVoltageControl_params, out) - OFFSET(NvRmPowerVoltageControl_params, inout));

    if( p_in->PrefVoltageListCount && p_in->PrefVoltageList )
    {
        PrefVoltageList = (NvRmMilliVolts  *)NvOsAlloc( p_in->PrefVoltageListCount * sizeof( NvRmMilliVolts  ) );
        if( !PrefVoltageList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->PrefVoltageList )
        {
            err_ = NvOsCopyIn( PrefVoltageList, p_in->PrefVoltageList, p_in->PrefVoltageListCount * sizeof( NvRmMilliVolts  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmPowerVoltageControl( p_in->hRmDeviceHandle, p_in->ModuleId, p_in->ClientId, p_in->MinVolts, p_in->MaxVolts, PrefVoltageList, p_in->PrefVoltageListCount, &p_out->CurrentVolts );

clean:
    NvOsFree( PrefVoltageList );
    return err_;
}

static NvError NvRmPowerModuleClockControl_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerModuleClockControl_in *p_in;
    NvRmPowerModuleClockControl_out *p_out;

    p_in = (NvRmPowerModuleClockControl_in *)InBuffer;
    p_out = (NvRmPowerModuleClockControl_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerModuleClockControl_params, out) - OFFSET(NvRmPowerModuleClockControl_params, inout));


    p_out->ret_ = NvRmPowerModuleClockControl( p_in->hRmDeviceHandle, p_in->ModuleId, p_in->ClientId, p_in->Enable );

    return err_;
}

static NvError NvRmPowerModuleClockConfig_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerModuleClockConfig_in *p_in;
    NvRmPowerModuleClockConfig_out *p_out;
    NvRmFreqKHz *PrefFreqList = NULL;

    p_in = (NvRmPowerModuleClockConfig_in *)InBuffer;
    p_out = (NvRmPowerModuleClockConfig_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerModuleClockConfig_params, out) - OFFSET(NvRmPowerModuleClockConfig_params, inout));

    if( p_in->PrefFreqListCount && p_in->PrefFreqList )
    {
        PrefFreqList = (NvRmFreqKHz  *)NvOsAlloc( p_in->PrefFreqListCount * sizeof( NvRmFreqKHz  ) );
        if( !PrefFreqList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->PrefFreqList )
        {
            err_ = NvOsCopyIn( PrefFreqList, p_in->PrefFreqList, p_in->PrefFreqListCount * sizeof( NvRmFreqKHz  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmPowerModuleClockConfig( p_in->hRmDeviceHandle, p_in->ModuleId, p_in->ClientId, p_in->MinFreq, p_in->MaxFreq, PrefFreqList, p_in->PrefFreqListCount, &p_out->CurrentFreq, p_in->flags );

clean:
    NvOsFree( PrefFreqList );
    return err_;
}

NvError nvrm_power_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 9:
        err_ = NvRmPowerVoltageControl_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 8:
        err_ = NvRmPowerModuleClockControl_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 7:
        err_ = NvRmPowerModuleClockConfig_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvSuccess;
        break;
    }

    return err_;
}
