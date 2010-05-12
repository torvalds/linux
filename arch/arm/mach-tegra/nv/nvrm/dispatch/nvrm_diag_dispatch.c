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
#include "nvrm_diag.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmDiagGetTemperature_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmTmonZoneId ZoneId;
} NV_ALIGN(4) NvRmDiagGetTemperature_in;

typedef struct NvRmDiagGetTemperature_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagGetTemperature_inout;

typedef struct NvRmDiagGetTemperature_out_t
{
    NvError ret_;
    NvS32 pTemperatureC;
} NV_ALIGN(4) NvRmDiagGetTemperature_out;

typedef struct NvRmDiagGetTemperature_params_t
{
    NvRmDiagGetTemperature_in in;
    NvRmDiagGetTemperature_inout inout;
    NvRmDiagGetTemperature_out out;
} NvRmDiagGetTemperature_params;

typedef struct NvRmDiagIsLockSupported_in_t
{
    NvU32 package_;
    NvU32 function_;
} NV_ALIGN(4) NvRmDiagIsLockSupported_in;

typedef struct NvRmDiagIsLockSupported_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagIsLockSupported_inout;

typedef struct NvRmDiagIsLockSupported_out_t
{
    NvBool ret_;
} NV_ALIGN(4) NvRmDiagIsLockSupported_out;

typedef struct NvRmDiagIsLockSupported_params_t
{
    NvRmDiagIsLockSupported_in in;
    NvRmDiagIsLockSupported_inout inout;
    NvRmDiagIsLockSupported_out out;
} NvRmDiagIsLockSupported_params;

typedef struct NvRmDiagConfigurePowerRail_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagPowerRailHandle hRail;
    NvU32 VoltageMV;
} NV_ALIGN(4) NvRmDiagConfigurePowerRail_in;

typedef struct NvRmDiagConfigurePowerRail_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagConfigurePowerRail_inout;

typedef struct NvRmDiagConfigurePowerRail_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagConfigurePowerRail_out;

typedef struct NvRmDiagConfigurePowerRail_params_t
{
    NvRmDiagConfigurePowerRail_in in;
    NvRmDiagConfigurePowerRail_inout inout;
    NvRmDiagConfigurePowerRail_out out;
} NvRmDiagConfigurePowerRail_params;

typedef struct NvRmDiagModuleListPowerRails_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagModuleID id;
    NvRmDiagPowerRailHandle  * phRailList;
} NV_ALIGN(4) NvRmDiagModuleListPowerRails_in;

typedef struct NvRmDiagModuleListPowerRails_inout_t
{
    NvU32 pListSize;
} NV_ALIGN(4) NvRmDiagModuleListPowerRails_inout;

typedef struct NvRmDiagModuleListPowerRails_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagModuleListPowerRails_out;

typedef struct NvRmDiagModuleListPowerRails_params_t
{
    NvRmDiagModuleListPowerRails_in in;
    NvRmDiagModuleListPowerRails_inout inout;
    NvRmDiagModuleListPowerRails_out out;
} NvRmDiagModuleListPowerRails_params;

typedef struct NvRmDiagPowerRailGetName_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagPowerRailHandle hRail;
} NV_ALIGN(4) NvRmDiagPowerRailGetName_in;

typedef struct NvRmDiagPowerRailGetName_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagPowerRailGetName_inout;

typedef struct NvRmDiagPowerRailGetName_out_t
{
    NvU64 ret_;
} NV_ALIGN(4) NvRmDiagPowerRailGetName_out;

typedef struct NvRmDiagPowerRailGetName_params_t
{
    NvRmDiagPowerRailGetName_in in;
    NvRmDiagPowerRailGetName_inout inout;
    NvRmDiagPowerRailGetName_out out;
} NvRmDiagPowerRailGetName_params;

typedef struct NvRmDiagListPowerRails_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagPowerRailHandle  * phRailList;
} NV_ALIGN(4) NvRmDiagListPowerRails_in;

typedef struct NvRmDiagListPowerRails_inout_t
{
    NvU32 pListSize;
} NV_ALIGN(4) NvRmDiagListPowerRails_inout;

typedef struct NvRmDiagListPowerRails_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagListPowerRails_out;

typedef struct NvRmDiagListPowerRails_params_t
{
    NvRmDiagListPowerRails_in in;
    NvRmDiagListPowerRails_inout inout;
    NvRmDiagListPowerRails_out out;
} NvRmDiagListPowerRails_params;

typedef struct NvRmDiagModuleReset_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagModuleID id;
    NvBool KeepAsserted;
} NV_ALIGN(4) NvRmDiagModuleReset_in;

typedef struct NvRmDiagModuleReset_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagModuleReset_inout;

typedef struct NvRmDiagModuleReset_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagModuleReset_out;

typedef struct NvRmDiagModuleReset_params_t
{
    NvRmDiagModuleReset_in in;
    NvRmDiagModuleReset_inout inout;
    NvRmDiagModuleReset_out out;
} NvRmDiagModuleReset_params;

typedef struct NvRmDiagClockScalerConfigure_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagClockSourceHandle hScaler;
    NvRmDiagClockSourceHandle hInput;
    NvU32 M;
    NvU32 N;
} NV_ALIGN(4) NvRmDiagClockScalerConfigure_in;

typedef struct NvRmDiagClockScalerConfigure_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagClockScalerConfigure_inout;

typedef struct NvRmDiagClockScalerConfigure_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagClockScalerConfigure_out;

typedef struct NvRmDiagClockScalerConfigure_params_t
{
    NvRmDiagClockScalerConfigure_in in;
    NvRmDiagClockScalerConfigure_inout inout;
    NvRmDiagClockScalerConfigure_out out;
} NvRmDiagClockScalerConfigure_params;

typedef struct NvRmDiagPllConfigure_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagClockSourceHandle hPll;
    NvU32 M;
    NvU32 N;
    NvU32 P;
} NV_ALIGN(4) NvRmDiagPllConfigure_in;

typedef struct NvRmDiagPllConfigure_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagPllConfigure_inout;

typedef struct NvRmDiagPllConfigure_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagPllConfigure_out;

typedef struct NvRmDiagPllConfigure_params_t
{
    NvRmDiagPllConfigure_in in;
    NvRmDiagPllConfigure_inout inout;
    NvRmDiagPllConfigure_out out;
} NvRmDiagPllConfigure_params;

typedef struct NvRmDiagOscillatorGetFreq_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagClockSourceHandle hOscillator;
} NV_ALIGN(4) NvRmDiagOscillatorGetFreq_in;

typedef struct NvRmDiagOscillatorGetFreq_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagOscillatorGetFreq_inout;

typedef struct NvRmDiagOscillatorGetFreq_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmDiagOscillatorGetFreq_out;

typedef struct NvRmDiagOscillatorGetFreq_params_t
{
    NvRmDiagOscillatorGetFreq_in in;
    NvRmDiagOscillatorGetFreq_inout inout;
    NvRmDiagOscillatorGetFreq_out out;
} NvRmDiagOscillatorGetFreq_params;

typedef struct NvRmDiagClockSourceListSources_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagClockSourceHandle hSource;
    NvRmDiagClockSourceHandle  * phSourceList;
} NV_ALIGN(4) NvRmDiagClockSourceListSources_in;

typedef struct NvRmDiagClockSourceListSources_inout_t
{
    NvU32 pListSize;
} NV_ALIGN(4) NvRmDiagClockSourceListSources_inout;

typedef struct NvRmDiagClockSourceListSources_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagClockSourceListSources_out;

typedef struct NvRmDiagClockSourceListSources_params_t
{
    NvRmDiagClockSourceListSources_in in;
    NvRmDiagClockSourceListSources_inout inout;
    NvRmDiagClockSourceListSources_out out;
} NvRmDiagClockSourceListSources_params;

typedef struct NvRmDiagClockSourceGetScaler_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagClockSourceHandle hSource;
} NV_ALIGN(4) NvRmDiagClockSourceGetScaler_in;

typedef struct NvRmDiagClockSourceGetScaler_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagClockSourceGetScaler_inout;

typedef struct NvRmDiagClockSourceGetScaler_out_t
{
    NvRmDiagClockScalerType ret_;
} NV_ALIGN(4) NvRmDiagClockSourceGetScaler_out;

typedef struct NvRmDiagClockSourceGetScaler_params_t
{
    NvRmDiagClockSourceGetScaler_in in;
    NvRmDiagClockSourceGetScaler_inout inout;
    NvRmDiagClockSourceGetScaler_out out;
} NvRmDiagClockSourceGetScaler_params;

typedef struct NvRmDiagClockSourceGetType_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagClockSourceHandle hSource;
} NV_ALIGN(4) NvRmDiagClockSourceGetType_in;

typedef struct NvRmDiagClockSourceGetType_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagClockSourceGetType_inout;

typedef struct NvRmDiagClockSourceGetType_out_t
{
    NvRmDiagClockSourceType ret_;
} NV_ALIGN(4) NvRmDiagClockSourceGetType_out;

typedef struct NvRmDiagClockSourceGetType_params_t
{
    NvRmDiagClockSourceGetType_in in;
    NvRmDiagClockSourceGetType_inout inout;
    NvRmDiagClockSourceGetType_out out;
} NvRmDiagClockSourceGetType_params;

typedef struct NvRmDiagClockSourceGetName_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagClockSourceHandle hSource;
} NV_ALIGN(4) NvRmDiagClockSourceGetName_in;

typedef struct NvRmDiagClockSourceGetName_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagClockSourceGetName_inout;

typedef struct NvRmDiagClockSourceGetName_out_t
{
    NvU64 ret_;
} NV_ALIGN(4) NvRmDiagClockSourceGetName_out;

typedef struct NvRmDiagClockSourceGetName_params_t
{
    NvRmDiagClockSourceGetName_in in;
    NvRmDiagClockSourceGetName_inout inout;
    NvRmDiagClockSourceGetName_out out;
} NvRmDiagClockSourceGetName_params;

typedef struct NvRmDiagModuleClockConfigure_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagModuleID id;
    NvRmDiagClockSourceHandle hSource;
    NvU32 divider;
    NvBool Source1st;
} NV_ALIGN(4) NvRmDiagModuleClockConfigure_in;

typedef struct NvRmDiagModuleClockConfigure_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagModuleClockConfigure_inout;

typedef struct NvRmDiagModuleClockConfigure_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagModuleClockConfigure_out;

typedef struct NvRmDiagModuleClockConfigure_params_t
{
    NvRmDiagModuleClockConfigure_in in;
    NvRmDiagModuleClockConfigure_inout inout;
    NvRmDiagModuleClockConfigure_out out;
} NvRmDiagModuleClockConfigure_params;

typedef struct NvRmDiagModuleClockEnable_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagModuleID id;
    NvBool enable;
} NV_ALIGN(4) NvRmDiagModuleClockEnable_in;

typedef struct NvRmDiagModuleClockEnable_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagModuleClockEnable_inout;

typedef struct NvRmDiagModuleClockEnable_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagModuleClockEnable_out;

typedef struct NvRmDiagModuleClockEnable_params_t
{
    NvRmDiagModuleClockEnable_in in;
    NvRmDiagModuleClockEnable_inout inout;
    NvRmDiagModuleClockEnable_out out;
} NvRmDiagModuleClockEnable_params;

typedef struct NvRmDiagModuleListClockSources_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagModuleID id;
    NvRmDiagClockSourceHandle  * phSourceList;
} NV_ALIGN(4) NvRmDiagModuleListClockSources_in;

typedef struct NvRmDiagModuleListClockSources_inout_t
{
    NvU32 pListSize;
} NV_ALIGN(4) NvRmDiagModuleListClockSources_inout;

typedef struct NvRmDiagModuleListClockSources_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagModuleListClockSources_out;

typedef struct NvRmDiagModuleListClockSources_params_t
{
    NvRmDiagModuleListClockSources_in in;
    NvRmDiagModuleListClockSources_inout inout;
    NvRmDiagModuleListClockSources_out out;
} NvRmDiagModuleListClockSources_params;

typedef struct NvRmDiagListClockSources_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagClockSourceHandle  * phSourceList;
} NV_ALIGN(4) NvRmDiagListClockSources_in;

typedef struct NvRmDiagListClockSources_inout_t
{
    NvU32 pListSize;
} NV_ALIGN(4) NvRmDiagListClockSources_inout;

typedef struct NvRmDiagListClockSources_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagListClockSources_out;

typedef struct NvRmDiagListClockSources_params_t
{
    NvRmDiagListClockSources_in in;
    NvRmDiagListClockSources_inout inout;
    NvRmDiagListClockSources_out out;
} NvRmDiagListClockSources_params;

typedef struct NvRmDiagListModules_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDiagModuleID  * pIdList;
} NV_ALIGN(4) NvRmDiagListModules_in;

typedef struct NvRmDiagListModules_inout_t
{
    NvU32 pListSize;
} NV_ALIGN(4) NvRmDiagListModules_inout;

typedef struct NvRmDiagListModules_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagListModules_out;

typedef struct NvRmDiagListModules_params_t
{
    NvRmDiagListModules_in in;
    NvRmDiagListModules_inout inout;
    NvRmDiagListModules_out out;
} NvRmDiagListModules_params;

typedef struct NvRmDiagEnable_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
} NV_ALIGN(4) NvRmDiagEnable_in;

typedef struct NvRmDiagEnable_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDiagEnable_inout;

typedef struct NvRmDiagEnable_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDiagEnable_out;

typedef struct NvRmDiagEnable_params_t
{
    NvRmDiagEnable_in in;
    NvRmDiagEnable_inout inout;
    NvRmDiagEnable_out out;
} NvRmDiagEnable_params;

static NvError NvRmDiagGetTemperature_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagGetTemperature_in *p_in;
    NvRmDiagGetTemperature_out *p_out;

    p_in = (NvRmDiagGetTemperature_in *)InBuffer;
    p_out = (NvRmDiagGetTemperature_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagGetTemperature_params, out) - OFFSET(NvRmDiagGetTemperature_params, inout));


    p_out->ret_ = NvRmDiagGetTemperature( p_in->hRmDeviceHandle, p_in->ZoneId, &p_out->pTemperatureC );

    return err_;
}

static NvError NvRmDiagIsLockSupported_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagIsLockSupported_out *p_out;
    p_out = (NvRmDiagIsLockSupported_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagIsLockSupported_params, out) - OFFSET(NvRmDiagIsLockSupported_params, inout));


    p_out->ret_ = NvRmDiagIsLockSupported(  );

    return err_;
}

static NvError NvRmDiagConfigurePowerRail_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagConfigurePowerRail_in *p_in;
    NvRmDiagConfigurePowerRail_out *p_out;

    p_in = (NvRmDiagConfigurePowerRail_in *)InBuffer;
    p_out = (NvRmDiagConfigurePowerRail_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagConfigurePowerRail_params, out) - OFFSET(NvRmDiagConfigurePowerRail_params, inout));


    p_out->ret_ = NvRmDiagConfigurePowerRail( p_in->hRail, p_in->VoltageMV );

    return err_;
}

static NvError NvRmDiagModuleListPowerRails_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagModuleListPowerRails_in *p_in;
    NvRmDiagModuleListPowerRails_inout *p_inout;
    NvRmDiagModuleListPowerRails_out *p_out;
    NvRmDiagModuleListPowerRails_inout inout;
    NvRmDiagPowerRailHandle *phRailList = NULL;

    p_in = (NvRmDiagModuleListPowerRails_in *)InBuffer;
    p_inout = (NvRmDiagModuleListPowerRails_inout *)((NvU8 *)InBuffer + OFFSET(NvRmDiagModuleListPowerRails_params, inout));
    p_out = (NvRmDiagModuleListPowerRails_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagModuleListPowerRails_params, out) - OFFSET(NvRmDiagModuleListPowerRails_params, inout));

    (void)inout;
    inout.pListSize = p_inout->pListSize;
    if( p_inout->pListSize && p_in->phRailList )
    {
        phRailList = (NvRmDiagPowerRailHandle  *)NvOsAlloc( p_inout->pListSize * sizeof( NvRmDiagPowerRailHandle  ) );
        if( !phRailList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmDiagModuleListPowerRails( p_in->id, &inout.pListSize, phRailList );


    p_inout = (NvRmDiagModuleListPowerRails_inout *)OutBuffer;
    p_inout->pListSize = inout.pListSize;
    if(p_in->phRailList && phRailList)
    {
        err_ = NvOsCopyOut( p_in->phRailList, phRailList, p_inout->pListSize * sizeof( NvRmDiagPowerRailHandle  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( phRailList );
    return err_;
}

static NvError NvRmDiagPowerRailGetName_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagPowerRailGetName_in *p_in;
    NvRmDiagPowerRailGetName_out *p_out;

    p_in = (NvRmDiagPowerRailGetName_in *)InBuffer;
    p_out = (NvRmDiagPowerRailGetName_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagPowerRailGetName_params, out) - OFFSET(NvRmDiagPowerRailGetName_params, inout));


    p_out->ret_ = NvRmDiagPowerRailGetName( p_in->hRail );

    return err_;
}

static NvError NvRmDiagListPowerRails_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagListPowerRails_in *p_in;
    NvRmDiagListPowerRails_inout *p_inout;
    NvRmDiagListPowerRails_out *p_out;
    NvRmDiagListPowerRails_inout inout;
    NvRmDiagPowerRailHandle *phRailList = NULL;

    p_in = (NvRmDiagListPowerRails_in *)InBuffer;
    p_inout = (NvRmDiagListPowerRails_inout *)((NvU8 *)InBuffer + OFFSET(NvRmDiagListPowerRails_params, inout));
    p_out = (NvRmDiagListPowerRails_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagListPowerRails_params, out) - OFFSET(NvRmDiagListPowerRails_params, inout));

    (void)inout;
    inout.pListSize = p_inout->pListSize;
    if( p_inout->pListSize && p_in->phRailList )
    {
        phRailList = (NvRmDiagPowerRailHandle  *)NvOsAlloc( p_inout->pListSize * sizeof( NvRmDiagPowerRailHandle  ) );
        if( !phRailList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmDiagListPowerRails( &inout.pListSize, phRailList );


    p_inout = (NvRmDiagListPowerRails_inout *)OutBuffer;
    p_inout->pListSize = inout.pListSize;
    if(p_in->phRailList && phRailList)
    {
        err_ = NvOsCopyOut( p_in->phRailList, phRailList, p_inout->pListSize * sizeof( NvRmDiagPowerRailHandle  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( phRailList );
    return err_;
}

static NvError NvRmDiagModuleReset_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagModuleReset_in *p_in;
    NvRmDiagModuleReset_out *p_out;

    p_in = (NvRmDiagModuleReset_in *)InBuffer;
    p_out = (NvRmDiagModuleReset_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagModuleReset_params, out) - OFFSET(NvRmDiagModuleReset_params, inout));


    p_out->ret_ = NvRmDiagModuleReset( p_in->id, p_in->KeepAsserted );

    return err_;
}

static NvError NvRmDiagClockScalerConfigure_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagClockScalerConfigure_in *p_in;
    NvRmDiagClockScalerConfigure_out *p_out;

    p_in = (NvRmDiagClockScalerConfigure_in *)InBuffer;
    p_out = (NvRmDiagClockScalerConfigure_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagClockScalerConfigure_params, out) - OFFSET(NvRmDiagClockScalerConfigure_params, inout));


    p_out->ret_ = NvRmDiagClockScalerConfigure( p_in->hScaler, p_in->hInput, p_in->M, p_in->N );

    return err_;
}

static NvError NvRmDiagPllConfigure_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagPllConfigure_in *p_in;
    NvRmDiagPllConfigure_out *p_out;

    p_in = (NvRmDiagPllConfigure_in *)InBuffer;
    p_out = (NvRmDiagPllConfigure_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagPllConfigure_params, out) - OFFSET(NvRmDiagPllConfigure_params, inout));


    p_out->ret_ = NvRmDiagPllConfigure( p_in->hPll, p_in->M, p_in->N, p_in->P );

    return err_;
}

static NvError NvRmDiagOscillatorGetFreq_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagOscillatorGetFreq_in *p_in;
    NvRmDiagOscillatorGetFreq_out *p_out;

    p_in = (NvRmDiagOscillatorGetFreq_in *)InBuffer;
    p_out = (NvRmDiagOscillatorGetFreq_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagOscillatorGetFreq_params, out) - OFFSET(NvRmDiagOscillatorGetFreq_params, inout));


    p_out->ret_ = NvRmDiagOscillatorGetFreq( p_in->hOscillator );

    return err_;
}

static NvError NvRmDiagClockSourceListSources_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagClockSourceListSources_in *p_in;
    NvRmDiagClockSourceListSources_inout *p_inout;
    NvRmDiagClockSourceListSources_out *p_out;
    NvRmDiagClockSourceListSources_inout inout;
    NvRmDiagClockSourceHandle *phSourceList = NULL;

    p_in = (NvRmDiagClockSourceListSources_in *)InBuffer;
    p_inout = (NvRmDiagClockSourceListSources_inout *)((NvU8 *)InBuffer + OFFSET(NvRmDiagClockSourceListSources_params, inout));
    p_out = (NvRmDiagClockSourceListSources_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagClockSourceListSources_params, out) - OFFSET(NvRmDiagClockSourceListSources_params, inout));

    (void)inout;
    inout.pListSize = p_inout->pListSize;
    if( p_inout->pListSize && p_in->phSourceList )
    {
        phSourceList = (NvRmDiagClockSourceHandle  *)NvOsAlloc( p_inout->pListSize * sizeof( NvRmDiagClockSourceHandle  ) );
        if( !phSourceList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmDiagClockSourceListSources( p_in->hSource, &inout.pListSize, phSourceList );


    p_inout = (NvRmDiagClockSourceListSources_inout *)OutBuffer;
    p_inout->pListSize = inout.pListSize;
    if(p_in->phSourceList && phSourceList)
    {
        err_ = NvOsCopyOut( p_in->phSourceList, phSourceList, p_inout->pListSize * sizeof( NvRmDiagClockSourceHandle  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( phSourceList );
    return err_;
}

static NvError NvRmDiagClockSourceGetScaler_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagClockSourceGetScaler_in *p_in;
    NvRmDiagClockSourceGetScaler_out *p_out;

    p_in = (NvRmDiagClockSourceGetScaler_in *)InBuffer;
    p_out = (NvRmDiagClockSourceGetScaler_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagClockSourceGetScaler_params, out) - OFFSET(NvRmDiagClockSourceGetScaler_params, inout));


    p_out->ret_ = NvRmDiagClockSourceGetScaler( p_in->hSource );

    return err_;
}

static NvError NvRmDiagClockSourceGetType_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagClockSourceGetType_in *p_in;
    NvRmDiagClockSourceGetType_out *p_out;

    p_in = (NvRmDiagClockSourceGetType_in *)InBuffer;
    p_out = (NvRmDiagClockSourceGetType_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagClockSourceGetType_params, out) - OFFSET(NvRmDiagClockSourceGetType_params, inout));


    p_out->ret_ = NvRmDiagClockSourceGetType( p_in->hSource );

    return err_;
}

static NvError NvRmDiagClockSourceGetName_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagClockSourceGetName_in *p_in;
    NvRmDiagClockSourceGetName_out *p_out;

    p_in = (NvRmDiagClockSourceGetName_in *)InBuffer;
    p_out = (NvRmDiagClockSourceGetName_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagClockSourceGetName_params, out) - OFFSET(NvRmDiagClockSourceGetName_params, inout));


    p_out->ret_ = NvRmDiagClockSourceGetName( p_in->hSource );

    return err_;
}

static NvError NvRmDiagModuleClockConfigure_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagModuleClockConfigure_in *p_in;
    NvRmDiagModuleClockConfigure_out *p_out;

    p_in = (NvRmDiagModuleClockConfigure_in *)InBuffer;
    p_out = (NvRmDiagModuleClockConfigure_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagModuleClockConfigure_params, out) - OFFSET(NvRmDiagModuleClockConfigure_params, inout));


    p_out->ret_ = NvRmDiagModuleClockConfigure( p_in->id, p_in->hSource, p_in->divider, p_in->Source1st );

    return err_;
}

static NvError NvRmDiagModuleClockEnable_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagModuleClockEnable_in *p_in;
    NvRmDiagModuleClockEnable_out *p_out;

    p_in = (NvRmDiagModuleClockEnable_in *)InBuffer;
    p_out = (NvRmDiagModuleClockEnable_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagModuleClockEnable_params, out) - OFFSET(NvRmDiagModuleClockEnable_params, inout));


    p_out->ret_ = NvRmDiagModuleClockEnable( p_in->id, p_in->enable );

    return err_;
}

static NvError NvRmDiagModuleListClockSources_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagModuleListClockSources_in *p_in;
    NvRmDiagModuleListClockSources_inout *p_inout;
    NvRmDiagModuleListClockSources_out *p_out;
    NvRmDiagModuleListClockSources_inout inout;
    NvRmDiagClockSourceHandle *phSourceList = NULL;

    p_in = (NvRmDiagModuleListClockSources_in *)InBuffer;
    p_inout = (NvRmDiagModuleListClockSources_inout *)((NvU8 *)InBuffer + OFFSET(NvRmDiagModuleListClockSources_params, inout));
    p_out = (NvRmDiagModuleListClockSources_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagModuleListClockSources_params, out) - OFFSET(NvRmDiagModuleListClockSources_params, inout));

    (void)inout;
    inout.pListSize = p_inout->pListSize;
    if( p_inout->pListSize && p_in->phSourceList )
    {
        phSourceList = (NvRmDiagClockSourceHandle  *)NvOsAlloc( p_inout->pListSize * sizeof( NvRmDiagClockSourceHandle  ) );
        if( !phSourceList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmDiagModuleListClockSources( p_in->id, &inout.pListSize, phSourceList );


    p_inout = (NvRmDiagModuleListClockSources_inout *)OutBuffer;
    p_inout->pListSize = inout.pListSize;
    if(p_in->phSourceList && phSourceList)
    {
        err_ = NvOsCopyOut( p_in->phSourceList, phSourceList, p_inout->pListSize * sizeof( NvRmDiagClockSourceHandle  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( phSourceList );
    return err_;
}

static NvError NvRmDiagListClockSources_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagListClockSources_in *p_in;
    NvRmDiagListClockSources_inout *p_inout;
    NvRmDiagListClockSources_out *p_out;
    NvRmDiagListClockSources_inout inout;
    NvRmDiagClockSourceHandle *phSourceList = NULL;

    p_in = (NvRmDiagListClockSources_in *)InBuffer;
    p_inout = (NvRmDiagListClockSources_inout *)((NvU8 *)InBuffer + OFFSET(NvRmDiagListClockSources_params, inout));
    p_out = (NvRmDiagListClockSources_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagListClockSources_params, out) - OFFSET(NvRmDiagListClockSources_params, inout));

    (void)inout;
    inout.pListSize = p_inout->pListSize;
    if( p_inout->pListSize && p_in->phSourceList )
    {
        phSourceList = (NvRmDiagClockSourceHandle  *)NvOsAlloc( p_inout->pListSize * sizeof( NvRmDiagClockSourceHandle  ) );
        if( !phSourceList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmDiagListClockSources( &inout.pListSize, phSourceList );


    p_inout = (NvRmDiagListClockSources_inout *)OutBuffer;
    p_inout->pListSize = inout.pListSize;
    if(p_in->phSourceList && phSourceList)
    {
        err_ = NvOsCopyOut( p_in->phSourceList, phSourceList, p_inout->pListSize * sizeof( NvRmDiagClockSourceHandle  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( phSourceList );
    return err_;
}

static NvError NvRmDiagListModules_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagListModules_in *p_in;
    NvRmDiagListModules_inout *p_inout;
    NvRmDiagListModules_out *p_out;
    NvRmDiagListModules_inout inout;
    NvRmDiagModuleID *pIdList = NULL;

    p_in = (NvRmDiagListModules_in *)InBuffer;
    p_inout = (NvRmDiagListModules_inout *)((NvU8 *)InBuffer + OFFSET(NvRmDiagListModules_params, inout));
    p_out = (NvRmDiagListModules_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagListModules_params, out) - OFFSET(NvRmDiagListModules_params, inout));

    (void)inout;
    inout.pListSize = p_inout->pListSize;
    if( p_inout->pListSize && p_in->pIdList )
    {
        pIdList = (NvRmDiagModuleID  *)NvOsAlloc( p_inout->pListSize * sizeof( NvRmDiagModuleID  ) );
        if( !pIdList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmDiagListModules( &inout.pListSize, pIdList );


    p_inout = (NvRmDiagListModules_inout *)OutBuffer;
    p_inout->pListSize = inout.pListSize;
    if(p_in->pIdList && pIdList)
    {
        err_ = NvOsCopyOut( p_in->pIdList, pIdList, p_inout->pListSize * sizeof( NvRmDiagModuleID  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pIdList );
    return err_;
}

static NvError NvRmDiagEnable_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDiagEnable_in *p_in;
    NvRmDiagEnable_out *p_out;

    p_in = (NvRmDiagEnable_in *)InBuffer;
    p_out = (NvRmDiagEnable_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDiagEnable_params, out) - OFFSET(NvRmDiagEnable_params, inout));


    p_out->ret_ = NvRmDiagEnable( p_in->hDevice );

    return err_;
}

NvError nvrm_diag_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_diag_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 19:
        err_ = NvRmDiagGetTemperature_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 18:
        err_ = NvRmDiagIsLockSupported_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 17:
        err_ = NvRmDiagConfigurePowerRail_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 16:
        err_ = NvRmDiagModuleListPowerRails_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 15:
        err_ = NvRmDiagPowerRailGetName_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 14:
        err_ = NvRmDiagListPowerRails_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 13:
        err_ = NvRmDiagModuleReset_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 12:
        err_ = NvRmDiagClockScalerConfigure_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 11:
        err_ = NvRmDiagPllConfigure_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 10:
        err_ = NvRmDiagOscillatorGetFreq_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 9:
        err_ = NvRmDiagClockSourceListSources_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 8:
        err_ = NvRmDiagClockSourceGetScaler_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 7:
        err_ = NvRmDiagClockSourceGetType_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 6:
        err_ = NvRmDiagClockSourceGetName_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 5:
        err_ = NvRmDiagModuleClockConfigure_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmDiagModuleClockEnable_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmDiagModuleListClockSources_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmDiagListClockSources_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmDiagListModules_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmDiagEnable_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
