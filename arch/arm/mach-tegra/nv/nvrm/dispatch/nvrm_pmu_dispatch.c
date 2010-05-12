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
#include "nvrm_pmu.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmPmuIsRtcInitialized_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
} NV_ALIGN(4) NvRmPmuIsRtcInitialized_in;

typedef struct NvRmPmuIsRtcInitialized_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuIsRtcInitialized_inout;

typedef struct NvRmPmuIsRtcInitialized_out_t
{
    NvBool ret_;
} NV_ALIGN(4) NvRmPmuIsRtcInitialized_out;

typedef struct NvRmPmuIsRtcInitialized_params_t
{
    NvRmPmuIsRtcInitialized_in in;
    NvRmPmuIsRtcInitialized_inout inout;
    NvRmPmuIsRtcInitialized_out out;
} NvRmPmuIsRtcInitialized_params;

typedef struct NvRmPmuWriteRtc_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvU32 Count;
} NV_ALIGN(4) NvRmPmuWriteRtc_in;

typedef struct NvRmPmuWriteRtc_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuWriteRtc_inout;

typedef struct NvRmPmuWriteRtc_out_t
{
    NvBool ret_;
} NV_ALIGN(4) NvRmPmuWriteRtc_out;

typedef struct NvRmPmuWriteRtc_params_t
{
    NvRmPmuWriteRtc_in in;
    NvRmPmuWriteRtc_inout inout;
    NvRmPmuWriteRtc_out out;
} NvRmPmuWriteRtc_params;

typedef struct NvRmPmuReadRtc_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
} NV_ALIGN(4) NvRmPmuReadRtc_in;

typedef struct NvRmPmuReadRtc_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuReadRtc_inout;

typedef struct NvRmPmuReadRtc_out_t
{
    NvBool ret_;
    NvU32 pCount;
} NV_ALIGN(4) NvRmPmuReadRtc_out;

typedef struct NvRmPmuReadRtc_params_t
{
    NvRmPmuReadRtc_in in;
    NvRmPmuReadRtc_inout inout;
    NvRmPmuReadRtc_out out;
} NvRmPmuReadRtc_params;

typedef struct NvRmPmuGetBatteryChemistry_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvRmPmuBatteryInstance batteryInst;
} NV_ALIGN(4) NvRmPmuGetBatteryChemistry_in;

typedef struct NvRmPmuGetBatteryChemistry_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuGetBatteryChemistry_inout;

typedef struct NvRmPmuGetBatteryChemistry_out_t
{
    NvRmPmuBatteryChemistry pChemistry;
} NV_ALIGN(4) NvRmPmuGetBatteryChemistry_out;

typedef struct NvRmPmuGetBatteryChemistry_params_t
{
    NvRmPmuGetBatteryChemistry_in in;
    NvRmPmuGetBatteryChemistry_inout inout;
    NvRmPmuGetBatteryChemistry_out out;
} NvRmPmuGetBatteryChemistry_params;

typedef struct NvRmPmuGetBatteryFullLifeTime_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvRmPmuBatteryInstance batteryInst;
} NV_ALIGN(4) NvRmPmuGetBatteryFullLifeTime_in;

typedef struct NvRmPmuGetBatteryFullLifeTime_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuGetBatteryFullLifeTime_inout;

typedef struct NvRmPmuGetBatteryFullLifeTime_out_t
{
    NvU32 pLifeTime;
} NV_ALIGN(4) NvRmPmuGetBatteryFullLifeTime_out;

typedef struct NvRmPmuGetBatteryFullLifeTime_params_t
{
    NvRmPmuGetBatteryFullLifeTime_in in;
    NvRmPmuGetBatteryFullLifeTime_inout inout;
    NvRmPmuGetBatteryFullLifeTime_out out;
} NvRmPmuGetBatteryFullLifeTime_params;

typedef struct NvRmPmuGetBatteryData_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvRmPmuBatteryInstance batteryInst;
} NV_ALIGN(4) NvRmPmuGetBatteryData_in;

typedef struct NvRmPmuGetBatteryData_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuGetBatteryData_inout;

typedef struct NvRmPmuGetBatteryData_out_t
{
    NvBool ret_;
    NvRmPmuBatteryData pData;
} NV_ALIGN(4) NvRmPmuGetBatteryData_out;

typedef struct NvRmPmuGetBatteryData_params_t
{
    NvRmPmuGetBatteryData_in in;
    NvRmPmuGetBatteryData_inout inout;
    NvRmPmuGetBatteryData_out out;
} NvRmPmuGetBatteryData_params;

typedef struct NvRmPmuGetBatteryStatus_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvRmPmuBatteryInstance batteryInst;
} NV_ALIGN(4) NvRmPmuGetBatteryStatus_in;

typedef struct NvRmPmuGetBatteryStatus_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuGetBatteryStatus_inout;

typedef struct NvRmPmuGetBatteryStatus_out_t
{
    NvBool ret_;
    NvU8 pStatus;
} NV_ALIGN(4) NvRmPmuGetBatteryStatus_out;

typedef struct NvRmPmuGetBatteryStatus_params_t
{
    NvRmPmuGetBatteryStatus_in in;
    NvRmPmuGetBatteryStatus_inout inout;
    NvRmPmuGetBatteryStatus_out out;
} NvRmPmuGetBatteryStatus_params;

typedef struct NvRmPmuGetAcLineStatus_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
} NV_ALIGN(4) NvRmPmuGetAcLineStatus_in;

typedef struct NvRmPmuGetAcLineStatus_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuGetAcLineStatus_inout;

typedef struct NvRmPmuGetAcLineStatus_out_t
{
    NvBool ret_;
    NvRmPmuAcLineStatus pStatus;
} NV_ALIGN(4) NvRmPmuGetAcLineStatus_out;

typedef struct NvRmPmuGetAcLineStatus_params_t
{
    NvRmPmuGetAcLineStatus_in in;
    NvRmPmuGetAcLineStatus_inout inout;
    NvRmPmuGetAcLineStatus_out out;
} NvRmPmuGetAcLineStatus_params;

typedef struct NvRmPmuSetChargingCurrentLimit_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvRmPmuChargingPath ChargingPath;
    NvU32 ChargingCurrentLimitMa;
    NvU32 ChargerType;
} NV_ALIGN(4) NvRmPmuSetChargingCurrentLimit_in;

typedef struct NvRmPmuSetChargingCurrentLimit_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuSetChargingCurrentLimit_inout;

typedef struct NvRmPmuSetChargingCurrentLimit_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuSetChargingCurrentLimit_out;

typedef struct NvRmPmuSetChargingCurrentLimit_params_t
{
    NvRmPmuSetChargingCurrentLimit_in in;
    NvRmPmuSetChargingCurrentLimit_inout inout;
    NvRmPmuSetChargingCurrentLimit_out out;
} NvRmPmuSetChargingCurrentLimit_params;

typedef struct NvRmPmuSetSocRailPowerState_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 vddId;
    NvBool Enable;
} NV_ALIGN(4) NvRmPmuSetSocRailPowerState_in;

typedef struct NvRmPmuSetSocRailPowerState_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuSetSocRailPowerState_inout;

typedef struct NvRmPmuSetSocRailPowerState_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuSetSocRailPowerState_out;

typedef struct NvRmPmuSetSocRailPowerState_params_t
{
    NvRmPmuSetSocRailPowerState_in in;
    NvRmPmuSetSocRailPowerState_inout inout;
    NvRmPmuSetSocRailPowerState_out out;
} NvRmPmuSetSocRailPowerState_params;

typedef struct NvRmPmuSetVoltage_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 vddId;
    NvU32 MilliVolts;
} NV_ALIGN(4) NvRmPmuSetVoltage_in;

typedef struct NvRmPmuSetVoltage_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuSetVoltage_inout;

typedef struct NvRmPmuSetVoltage_out_t
{
    NvU32 pSettleMicroSeconds;
} NV_ALIGN(4) NvRmPmuSetVoltage_out;

typedef struct NvRmPmuSetVoltage_params_t
{
    NvRmPmuSetVoltage_in in;
    NvRmPmuSetVoltage_inout inout;
    NvRmPmuSetVoltage_out out;
} NvRmPmuSetVoltage_params;

typedef struct NvRmPmuGetVoltage_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 vddId;
} NV_ALIGN(4) NvRmPmuGetVoltage_in;

typedef struct NvRmPmuGetVoltage_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuGetVoltage_inout;

typedef struct NvRmPmuGetVoltage_out_t
{
    NvU32 pMilliVolts;
} NV_ALIGN(4) NvRmPmuGetVoltage_out;

typedef struct NvRmPmuGetVoltage_params_t
{
    NvRmPmuGetVoltage_in in;
    NvRmPmuGetVoltage_inout inout;
    NvRmPmuGetVoltage_out out;
} NvRmPmuGetVoltage_params;

typedef struct NvRmPmuGetCapabilities_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvU32 vddId;
} NV_ALIGN(4) NvRmPmuGetCapabilities_in;

typedef struct NvRmPmuGetCapabilities_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPmuGetCapabilities_inout;

typedef struct NvRmPmuGetCapabilities_out_t
{
    NvRmPmuVddRailCapabilities pCapabilities;
} NV_ALIGN(4) NvRmPmuGetCapabilities_out;

typedef struct NvRmPmuGetCapabilities_params_t
{
    NvRmPmuGetCapabilities_in in;
    NvRmPmuGetCapabilities_inout inout;
    NvRmPmuGetCapabilities_out out;
} NvRmPmuGetCapabilities_params;

static NvError NvRmPmuIsRtcInitialized_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuIsRtcInitialized_in *p_in;
    NvRmPmuIsRtcInitialized_out *p_out;

    p_in = (NvRmPmuIsRtcInitialized_in *)InBuffer;
    p_out = (NvRmPmuIsRtcInitialized_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuIsRtcInitialized_params, out) - OFFSET(NvRmPmuIsRtcInitialized_params, inout));


    p_out->ret_ = NvRmPmuIsRtcInitialized( p_in->hRmDevice );

    return err_;
}

static NvError NvRmPmuWriteRtc_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuWriteRtc_in *p_in;
    NvRmPmuWriteRtc_out *p_out;

    p_in = (NvRmPmuWriteRtc_in *)InBuffer;
    p_out = (NvRmPmuWriteRtc_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuWriteRtc_params, out) - OFFSET(NvRmPmuWriteRtc_params, inout));


    p_out->ret_ = NvRmPmuWriteRtc( p_in->hRmDevice, p_in->Count );

    return err_;
}

static NvError NvRmPmuReadRtc_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuReadRtc_in *p_in;
    NvRmPmuReadRtc_out *p_out;

    p_in = (NvRmPmuReadRtc_in *)InBuffer;
    p_out = (NvRmPmuReadRtc_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuReadRtc_params, out) - OFFSET(NvRmPmuReadRtc_params, inout));


    p_out->ret_ = NvRmPmuReadRtc( p_in->hRmDevice, &p_out->pCount );

    return err_;
}

static NvError NvRmPmuGetBatteryChemistry_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuGetBatteryChemistry_in *p_in;
    NvRmPmuGetBatteryChemistry_out *p_out;

    p_in = (NvRmPmuGetBatteryChemistry_in *)InBuffer;
    p_out = (NvRmPmuGetBatteryChemistry_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuGetBatteryChemistry_params, out) - OFFSET(NvRmPmuGetBatteryChemistry_params, inout));


    NvRmPmuGetBatteryChemistry( p_in->hRmDevice, p_in->batteryInst, &p_out->pChemistry );

    return err_;
}

static NvError NvRmPmuGetBatteryFullLifeTime_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuGetBatteryFullLifeTime_in *p_in;
    NvRmPmuGetBatteryFullLifeTime_out *p_out;

    p_in = (NvRmPmuGetBatteryFullLifeTime_in *)InBuffer;
    p_out = (NvRmPmuGetBatteryFullLifeTime_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuGetBatteryFullLifeTime_params, out) - OFFSET(NvRmPmuGetBatteryFullLifeTime_params, inout));


    NvRmPmuGetBatteryFullLifeTime( p_in->hRmDevice, p_in->batteryInst, &p_out->pLifeTime );

    return err_;
}

static NvError NvRmPmuGetBatteryData_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuGetBatteryData_in *p_in;
    NvRmPmuGetBatteryData_out *p_out;

    p_in = (NvRmPmuGetBatteryData_in *)InBuffer;
    p_out = (NvRmPmuGetBatteryData_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuGetBatteryData_params, out) - OFFSET(NvRmPmuGetBatteryData_params, inout));


    p_out->ret_ = NvRmPmuGetBatteryData( p_in->hRmDevice, p_in->batteryInst, &p_out->pData );

    return err_;
}

static NvError NvRmPmuGetBatteryStatus_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuGetBatteryStatus_in *p_in;
    NvRmPmuGetBatteryStatus_out *p_out;

    p_in = (NvRmPmuGetBatteryStatus_in *)InBuffer;
    p_out = (NvRmPmuGetBatteryStatus_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuGetBatteryStatus_params, out) - OFFSET(NvRmPmuGetBatteryStatus_params, inout));


    p_out->ret_ = NvRmPmuGetBatteryStatus( p_in->hRmDevice, p_in->batteryInst, &p_out->pStatus );

    return err_;
}

static NvError NvRmPmuGetAcLineStatus_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuGetAcLineStatus_in *p_in;
    NvRmPmuGetAcLineStatus_out *p_out;

    p_in = (NvRmPmuGetAcLineStatus_in *)InBuffer;
    p_out = (NvRmPmuGetAcLineStatus_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuGetAcLineStatus_params, out) - OFFSET(NvRmPmuGetAcLineStatus_params, inout));


    p_out->ret_ = NvRmPmuGetAcLineStatus( p_in->hRmDevice, &p_out->pStatus );

    return err_;
}

static NvError NvRmPmuSetChargingCurrentLimit_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuSetChargingCurrentLimit_in *p_in;

    p_in = (NvRmPmuSetChargingCurrentLimit_in *)InBuffer;


    NvRmPmuSetChargingCurrentLimit( p_in->hRmDevice, p_in->ChargingPath, p_in->ChargingCurrentLimitMa, p_in->ChargerType );

    return err_;
}

static NvError NvRmPmuSetSocRailPowerState_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuSetSocRailPowerState_in *p_in;

    p_in = (NvRmPmuSetSocRailPowerState_in *)InBuffer;


    NvRmPmuSetSocRailPowerState( p_in->hDevice, p_in->vddId, p_in->Enable );

    return err_;
}

static NvError NvRmPmuSetVoltage_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuSetVoltage_in *p_in;
    NvRmPmuSetVoltage_out *p_out;

    p_in = (NvRmPmuSetVoltage_in *)InBuffer;
    p_out = (NvRmPmuSetVoltage_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuSetVoltage_params, out) - OFFSET(NvRmPmuSetVoltage_params, inout));


    NvRmPmuSetVoltage( p_in->hDevice, p_in->vddId, p_in->MilliVolts, &p_out->pSettleMicroSeconds );

    return err_;
}

static NvError NvRmPmuGetVoltage_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuGetVoltage_in *p_in;
    NvRmPmuGetVoltage_out *p_out;

    p_in = (NvRmPmuGetVoltage_in *)InBuffer;
    p_out = (NvRmPmuGetVoltage_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuGetVoltage_params, out) - OFFSET(NvRmPmuGetVoltage_params, inout));


    NvRmPmuGetVoltage( p_in->hDevice, p_in->vddId, &p_out->pMilliVolts );

    return err_;
}

static NvError NvRmPmuGetCapabilities_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPmuGetCapabilities_in *p_in;
    NvRmPmuGetCapabilities_out *p_out;

    p_in = (NvRmPmuGetCapabilities_in *)InBuffer;
    p_out = (NvRmPmuGetCapabilities_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPmuGetCapabilities_params, out) - OFFSET(NvRmPmuGetCapabilities_params, inout));


    NvRmPmuGetCapabilities( p_in->hDevice, p_in->vddId, &p_out->pCapabilities );

    return err_;
}

NvError nvrm_pmu_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_pmu_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 12:
        err_ = NvRmPmuIsRtcInitialized_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 11:
        err_ = NvRmPmuWriteRtc_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 10:
        err_ = NvRmPmuReadRtc_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 9:
        err_ = NvRmPmuGetBatteryChemistry_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 8:
        err_ = NvRmPmuGetBatteryFullLifeTime_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 7:
        err_ = NvRmPmuGetBatteryData_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 6:
        err_ = NvRmPmuGetBatteryStatus_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 5:
        err_ = NvRmPmuGetAcLineStatus_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmPmuSetChargingCurrentLimit_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmPmuSetSocRailPowerState_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmPmuSetVoltage_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmPmuGetVoltage_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmPmuGetCapabilities_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
