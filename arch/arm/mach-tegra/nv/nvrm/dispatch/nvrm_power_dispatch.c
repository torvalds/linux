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


typedef struct NvRmKernelPowerResume_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
} NV_ALIGN(4) NvRmKernelPowerResume_in;

typedef struct NvRmKernelPowerResume_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmKernelPowerResume_inout;

typedef struct NvRmKernelPowerResume_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmKernelPowerResume_out;

typedef struct NvRmKernelPowerResume_params_t
{
    NvRmKernelPowerResume_in in;
    NvRmKernelPowerResume_inout inout;
    NvRmKernelPowerResume_out out;
} NvRmKernelPowerResume_params;

typedef struct NvRmKernelPowerSuspend_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
} NV_ALIGN(4) NvRmKernelPowerSuspend_in;

typedef struct NvRmKernelPowerSuspend_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmKernelPowerSuspend_inout;

typedef struct NvRmKernelPowerSuspend_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmKernelPowerSuspend_out;

typedef struct NvRmKernelPowerSuspend_params_t
{
    NvRmKernelPowerSuspend_in in;
    NvRmKernelPowerSuspend_inout inout;
    NvRmKernelPowerSuspend_out out;
} NvRmKernelPowerSuspend_params;

typedef struct NvRmDfsSetLowVoltageThreshold_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmDfsVoltageRailId RailId;
    NvRmMilliVolts LowMv;
} NV_ALIGN(4) NvRmDfsSetLowVoltageThreshold_in;

typedef struct NvRmDfsSetLowVoltageThreshold_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsSetLowVoltageThreshold_inout;

typedef struct NvRmDfsSetLowVoltageThreshold_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsSetLowVoltageThreshold_out;

typedef struct NvRmDfsSetLowVoltageThreshold_params_t
{
    NvRmDfsSetLowVoltageThreshold_in in;
    NvRmDfsSetLowVoltageThreshold_inout inout;
    NvRmDfsSetLowVoltageThreshold_out out;
} NvRmDfsSetLowVoltageThreshold_params;

typedef struct NvRmDfsGetLowVoltageThreshold_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmDfsVoltageRailId RailId;
} NV_ALIGN(4) NvRmDfsGetLowVoltageThreshold_in;

typedef struct NvRmDfsGetLowVoltageThreshold_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsGetLowVoltageThreshold_inout;

typedef struct NvRmDfsGetLowVoltageThreshold_out_t
{
    NvRmMilliVolts pLowMv;
    NvRmMilliVolts pPresentMv;
} NV_ALIGN(4) NvRmDfsGetLowVoltageThreshold_out;

typedef struct NvRmDfsGetLowVoltageThreshold_params_t
{
    NvRmDfsGetLowVoltageThreshold_in in;
    NvRmDfsGetLowVoltageThreshold_inout inout;
    NvRmDfsGetLowVoltageThreshold_out out;
} NvRmDfsGetLowVoltageThreshold_params;

typedef struct NvRmDfsLogBusyGetEntry_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 EntryIndex;
} NV_ALIGN(4) NvRmDfsLogBusyGetEntry_in;

typedef struct NvRmDfsLogBusyGetEntry_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsLogBusyGetEntry_inout;

typedef struct NvRmDfsLogBusyGetEntry_out_t
{
    NvError ret_;
    NvU32 pSampleIndex;
    NvU32 pClientId;
    NvU32 pClientTag;
    NvRmDfsBusyHint pBusyHint;
} NV_ALIGN(4) NvRmDfsLogBusyGetEntry_out;

typedef struct NvRmDfsLogBusyGetEntry_params_t
{
    NvRmDfsLogBusyGetEntry_in in;
    NvRmDfsLogBusyGetEntry_inout inout;
    NvRmDfsLogBusyGetEntry_out out;
} NvRmDfsLogBusyGetEntry_params;

typedef struct NvRmDfsLogStarvationGetEntry_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 EntryIndex;
} NV_ALIGN(4) NvRmDfsLogStarvationGetEntry_in;

typedef struct NvRmDfsLogStarvationGetEntry_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsLogStarvationGetEntry_inout;

typedef struct NvRmDfsLogStarvationGetEntry_out_t
{
    NvError ret_;
    NvU32 pSampleIndex;
    NvU32 pClientId;
    NvU32 pClientTag;
    NvRmDfsStarvationHint pStarvationHint;
} NV_ALIGN(4) NvRmDfsLogStarvationGetEntry_out;

typedef struct NvRmDfsLogStarvationGetEntry_params_t
{
    NvRmDfsLogStarvationGetEntry_in in;
    NvRmDfsLogStarvationGetEntry_inout inout;
    NvRmDfsLogStarvationGetEntry_out out;
} NvRmDfsLogStarvationGetEntry_params;

typedef struct NvRmDfsLogActivityGetEntry_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 EntryIndex;
    NvU32 LogDomainsCount;
    NvU32  * pActiveCyclesList;
    NvRmFreqKHz  * pAveragesList;
    NvRmFreqKHz  * pFrequenciesList;
} NV_ALIGN(4) NvRmDfsLogActivityGetEntry_in;

typedef struct NvRmDfsLogActivityGetEntry_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsLogActivityGetEntry_inout;

typedef struct NvRmDfsLogActivityGetEntry_out_t
{
    NvError ret_;
    NvU32 pIntervalMs;
    NvU32 pLp2TimeMs;
} NV_ALIGN(4) NvRmDfsLogActivityGetEntry_out;

typedef struct NvRmDfsLogActivityGetEntry_params_t
{
    NvRmDfsLogActivityGetEntry_in in;
    NvRmDfsLogActivityGetEntry_inout inout;
    NvRmDfsLogActivityGetEntry_out out;
} NvRmDfsLogActivityGetEntry_params;

typedef struct NvRmDfsLogGetMeanFrequencies_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 LogMeanFreqListCount;
    NvRmFreqKHz  * pLogMeanFreqList;
} NV_ALIGN(4) NvRmDfsLogGetMeanFrequencies_in;

typedef struct NvRmDfsLogGetMeanFrequencies_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsLogGetMeanFrequencies_inout;

typedef struct NvRmDfsLogGetMeanFrequencies_out_t
{
    NvError ret_;
    NvU32 pLogLp2TimeMs;
    NvU32 pLogLp2Entries;
} NV_ALIGN(4) NvRmDfsLogGetMeanFrequencies_out;

typedef struct NvRmDfsLogGetMeanFrequencies_params_t
{
    NvRmDfsLogGetMeanFrequencies_in in;
    NvRmDfsLogGetMeanFrequencies_inout inout;
    NvRmDfsLogGetMeanFrequencies_out out;
} NvRmDfsLogGetMeanFrequencies_params;

typedef struct NvRmDfsLogStart_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
} NV_ALIGN(4) NvRmDfsLogStart_in;

typedef struct NvRmDfsLogStart_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsLogStart_inout;

typedef struct NvRmDfsLogStart_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsLogStart_out;

typedef struct NvRmDfsLogStart_params_t
{
    NvRmDfsLogStart_in in;
    NvRmDfsLogStart_inout inout;
    NvRmDfsLogStart_out out;
} NvRmDfsLogStart_params;

typedef struct NvRmDfsGetProfileData_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 DfsProfileCount;
    NvU32  * pSamplesNoList;
    NvU32  * pProfileTimeUsList;
} NV_ALIGN(4) NvRmDfsGetProfileData_in;

typedef struct NvRmDfsGetProfileData_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsGetProfileData_inout;

typedef struct NvRmDfsGetProfileData_out_t
{
    NvError ret_;
    NvU32 pDfsPeriodUs;
} NV_ALIGN(4) NvRmDfsGetProfileData_out;

typedef struct NvRmDfsGetProfileData_params_t
{
    NvRmDfsGetProfileData_in in;
    NvRmDfsGetProfileData_inout inout;
    NvRmDfsGetProfileData_out out;
} NvRmDfsGetProfileData_params;

typedef struct NvRmDfsSetAvHighCorner_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmFreqKHz DfsSystemHighKHz;
    NvRmFreqKHz DfsAvpHighKHz;
    NvRmFreqKHz DfsVpipeHighKHz;
} NV_ALIGN(4) NvRmDfsSetAvHighCorner_in;

typedef struct NvRmDfsSetAvHighCorner_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsSetAvHighCorner_inout;

typedef struct NvRmDfsSetAvHighCorner_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDfsSetAvHighCorner_out;

typedef struct NvRmDfsSetAvHighCorner_params_t
{
    NvRmDfsSetAvHighCorner_in in;
    NvRmDfsSetAvHighCorner_inout inout;
    NvRmDfsSetAvHighCorner_out out;
} NvRmDfsSetAvHighCorner_params;

typedef struct NvRmDfsSetCpuEmcHighCorner_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmFreqKHz DfsCpuHighKHz;
    NvRmFreqKHz DfsEmcHighKHz;
} NV_ALIGN(4) NvRmDfsSetCpuEmcHighCorner_in;

typedef struct NvRmDfsSetCpuEmcHighCorner_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsSetCpuEmcHighCorner_inout;

typedef struct NvRmDfsSetCpuEmcHighCorner_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDfsSetCpuEmcHighCorner_out;

typedef struct NvRmDfsSetCpuEmcHighCorner_params_t
{
    NvRmDfsSetCpuEmcHighCorner_in in;
    NvRmDfsSetCpuEmcHighCorner_inout inout;
    NvRmDfsSetCpuEmcHighCorner_out out;
} NvRmDfsSetCpuEmcHighCorner_params;

typedef struct NvRmDfsSetEmcEnvelope_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmFreqKHz DfsEmcLowCornerKHz;
    NvRmFreqKHz DfsEmcHighCornerKHz;
} NV_ALIGN(4) NvRmDfsSetEmcEnvelope_in;

typedef struct NvRmDfsSetEmcEnvelope_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsSetEmcEnvelope_inout;

typedef struct NvRmDfsSetEmcEnvelope_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDfsSetEmcEnvelope_out;

typedef struct NvRmDfsSetEmcEnvelope_params_t
{
    NvRmDfsSetEmcEnvelope_in in;
    NvRmDfsSetEmcEnvelope_inout inout;
    NvRmDfsSetEmcEnvelope_out out;
} NvRmDfsSetEmcEnvelope_params;

typedef struct NvRmDfsSetCpuEnvelope_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmFreqKHz DfsCpuLowCornerKHz;
    NvRmFreqKHz DfsCpuHighCornerKHz;
} NV_ALIGN(4) NvRmDfsSetCpuEnvelope_in;

typedef struct NvRmDfsSetCpuEnvelope_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsSetCpuEnvelope_inout;

typedef struct NvRmDfsSetCpuEnvelope_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDfsSetCpuEnvelope_out;

typedef struct NvRmDfsSetCpuEnvelope_params_t
{
    NvRmDfsSetCpuEnvelope_in in;
    NvRmDfsSetCpuEnvelope_inout inout;
    NvRmDfsSetCpuEnvelope_out out;
} NvRmDfsSetCpuEnvelope_params;

typedef struct NvRmDfsSetTarget_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 DfsFreqListCount;
    NvRmFreqKHz  * pDfsTargetFreqList;
} NV_ALIGN(4) NvRmDfsSetTarget_in;

typedef struct NvRmDfsSetTarget_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsSetTarget_inout;

typedef struct NvRmDfsSetTarget_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDfsSetTarget_out;

typedef struct NvRmDfsSetTarget_params_t
{
    NvRmDfsSetTarget_in in;
    NvRmDfsSetTarget_inout inout;
    NvRmDfsSetTarget_out out;
} NvRmDfsSetTarget_params;

typedef struct NvRmDfsSetLowCorner_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 DfsFreqListCount;
    NvRmFreqKHz  * pDfsLowFreqList;
} NV_ALIGN(4) NvRmDfsSetLowCorner_in;

typedef struct NvRmDfsSetLowCorner_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsSetLowCorner_inout;

typedef struct NvRmDfsSetLowCorner_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDfsSetLowCorner_out;

typedef struct NvRmDfsSetLowCorner_params_t
{
    NvRmDfsSetLowCorner_in in;
    NvRmDfsSetLowCorner_inout inout;
    NvRmDfsSetLowCorner_out out;
} NvRmDfsSetLowCorner_params;

typedef struct NvRmDfsSetState_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmDfsRunState NewDfsRunState;
} NV_ALIGN(4) NvRmDfsSetState_in;

typedef struct NvRmDfsSetState_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsSetState_inout;

typedef struct NvRmDfsSetState_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmDfsSetState_out;

typedef struct NvRmDfsSetState_params_t
{
    NvRmDfsSetState_in in;
    NvRmDfsSetState_inout inout;
    NvRmDfsSetState_out out;
} NvRmDfsSetState_params;

typedef struct NvRmDfsGetClockUtilization_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmDfsClockId ClockId;
} NV_ALIGN(4) NvRmDfsGetClockUtilization_in;

typedef struct NvRmDfsGetClockUtilization_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsGetClockUtilization_inout;

typedef struct NvRmDfsGetClockUtilization_out_t
{
    NvError ret_;
    NvRmDfsClockUsage pClockUsage;
} NV_ALIGN(4) NvRmDfsGetClockUtilization_out;

typedef struct NvRmDfsGetClockUtilization_params_t
{
    NvRmDfsGetClockUtilization_in in;
    NvRmDfsGetClockUtilization_inout inout;
    NvRmDfsGetClockUtilization_out out;
} NvRmDfsGetClockUtilization_params;

typedef struct NvRmDfsGetState_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
} NV_ALIGN(4) NvRmDfsGetState_in;

typedef struct NvRmDfsGetState_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmDfsGetState_inout;

typedef struct NvRmDfsGetState_out_t
{
    NvRmDfsRunState ret_;
} NV_ALIGN(4) NvRmDfsGetState_out;

typedef struct NvRmDfsGetState_params_t
{
    NvRmDfsGetState_in in;
    NvRmDfsGetState_inout inout;
    NvRmDfsGetState_out out;
} NvRmDfsGetState_params;

typedef struct NvRmPowerActivityHint_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID ModuleId;
    NvU32 ClientId;
    NvU32 ActivityDurationMs;
} NV_ALIGN(4) NvRmPowerActivityHint_in;

typedef struct NvRmPowerActivityHint_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerActivityHint_inout;

typedef struct NvRmPowerActivityHint_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmPowerActivityHint_out;

typedef struct NvRmPowerActivityHint_params_t
{
    NvRmPowerActivityHint_in in;
    NvRmPowerActivityHint_inout inout;
    NvRmPowerActivityHint_out out;
} NvRmPowerActivityHint_params;

typedef struct NvRmPowerStarvationHintMulti_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 ClientId;
    NvRmDfsStarvationHint  * pMultiHint;
    NvU32 NumHints;
} NV_ALIGN(4) NvRmPowerStarvationHintMulti_in;

typedef struct NvRmPowerStarvationHintMulti_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerStarvationHintMulti_inout;

typedef struct NvRmPowerStarvationHintMulti_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmPowerStarvationHintMulti_out;

typedef struct NvRmPowerStarvationHintMulti_params_t
{
    NvRmPowerStarvationHintMulti_in in;
    NvRmPowerStarvationHintMulti_inout inout;
    NvRmPowerStarvationHintMulti_out out;
} NvRmPowerStarvationHintMulti_params;

typedef struct NvRmPowerStarvationHint_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmDfsClockId ClockId;
    NvU32 ClientId;
    NvBool Starving;
} NV_ALIGN(4) NvRmPowerStarvationHint_in;

typedef struct NvRmPowerStarvationHint_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerStarvationHint_inout;

typedef struct NvRmPowerStarvationHint_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmPowerStarvationHint_out;

typedef struct NvRmPowerStarvationHint_params_t
{
    NvRmPowerStarvationHint_in in;
    NvRmPowerStarvationHint_inout inout;
    NvRmPowerStarvationHint_out out;
} NvRmPowerStarvationHint_params;

typedef struct NvRmPowerBusyHintMulti_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 ClientId;
    NvRmDfsBusyHint  * pMultiHint;
    NvU32 NumHints;
    NvRmDfsBusyHintSyncMode Mode;
} NV_ALIGN(4) NvRmPowerBusyHintMulti_in;

typedef struct NvRmPowerBusyHintMulti_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerBusyHintMulti_inout;

typedef struct NvRmPowerBusyHintMulti_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmPowerBusyHintMulti_out;

typedef struct NvRmPowerBusyHintMulti_params_t
{
    NvRmPowerBusyHintMulti_in in;
    NvRmPowerBusyHintMulti_inout inout;
    NvRmPowerBusyHintMulti_out out;
} NvRmPowerBusyHintMulti_params;

typedef struct NvRmPowerBusyHint_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmDfsClockId ClockId;
    NvU32 ClientId;
    NvU32 BoostDurationMs;
    NvRmFreqKHz BoostKHz;
} NV_ALIGN(4) NvRmPowerBusyHint_in;

typedef struct NvRmPowerBusyHint_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerBusyHint_inout;

typedef struct NvRmPowerBusyHint_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmPowerBusyHint_out;

typedef struct NvRmPowerBusyHint_params_t
{
    NvRmPowerBusyHint_in in;
    NvRmPowerBusyHint_inout inout;
    NvRmPowerBusyHint_out out;
} NvRmPowerBusyHint_params;

typedef struct NvRmListPowerAwareModules_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID  * pIdList;
    NvBool  * pActiveList;
} NV_ALIGN(4) NvRmListPowerAwareModules_in;

typedef struct NvRmListPowerAwareModules_inout_t
{
    NvU32 pListSize;
} NV_ALIGN(4) NvRmListPowerAwareModules_inout;

typedef struct NvRmListPowerAwareModules_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmListPowerAwareModules_out;

typedef struct NvRmListPowerAwareModules_params_t
{
    NvRmListPowerAwareModules_in in;
    NvRmListPowerAwareModules_inout inout;
    NvRmListPowerAwareModules_out out;
} NvRmListPowerAwareModules_params;

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

typedef struct NvRmPowerModuleGetMaxFrequency_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID ModuleId;
} NV_ALIGN(4) NvRmPowerModuleGetMaxFrequency_in;

typedef struct NvRmPowerModuleGetMaxFrequency_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerModuleGetMaxFrequency_inout;

typedef struct NvRmPowerModuleGetMaxFrequency_out_t
{
    NvRmFreqKHz ret_;
} NV_ALIGN(4) NvRmPowerModuleGetMaxFrequency_out;

typedef struct NvRmPowerModuleGetMaxFrequency_params_t
{
    NvRmPowerModuleGetMaxFrequency_in in;
    NvRmPowerModuleGetMaxFrequency_inout inout;
    NvRmPowerModuleGetMaxFrequency_out out;
} NvRmPowerModuleGetMaxFrequency_params;

typedef struct NvRmPowerGetPrimaryFrequency_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
} NV_ALIGN(4) NvRmPowerGetPrimaryFrequency_in;

typedef struct NvRmPowerGetPrimaryFrequency_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerGetPrimaryFrequency_inout;

typedef struct NvRmPowerGetPrimaryFrequency_out_t
{
    NvRmFreqKHz ret_;
} NV_ALIGN(4) NvRmPowerGetPrimaryFrequency_out;

typedef struct NvRmPowerGetPrimaryFrequency_params_t
{
    NvRmPowerGetPrimaryFrequency_in in;
    NvRmPowerGetPrimaryFrequency_inout inout;
    NvRmPowerGetPrimaryFrequency_out out;
} NvRmPowerGetPrimaryFrequency_params;

typedef struct NvRmPowerGetState_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
} NV_ALIGN(4) NvRmPowerGetState_in;

typedef struct NvRmPowerGetState_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerGetState_inout;

typedef struct NvRmPowerGetState_out_t
{
    NvError ret_;
    NvRmPowerState pState;
} NV_ALIGN(4) NvRmPowerGetState_out;

typedef struct NvRmPowerGetState_params_t
{
    NvRmPowerGetState_in in;
    NvRmPowerGetState_inout inout;
    NvRmPowerGetState_out out;
} NvRmPowerGetState_params;

typedef struct NvRmPowerEventNotify_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmPowerEvent Event;
} NV_ALIGN(4) NvRmPowerEventNotify_in;

typedef struct NvRmPowerEventNotify_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerEventNotify_inout;

typedef struct NvRmPowerEventNotify_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerEventNotify_out;

typedef struct NvRmPowerEventNotify_params_t
{
    NvRmPowerEventNotify_in in;
    NvRmPowerEventNotify_inout inout;
    NvRmPowerEventNotify_out out;
} NvRmPowerEventNotify_params;

typedef struct NvRmPowerGetEvent_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 ClientId;
} NV_ALIGN(4) NvRmPowerGetEvent_in;

typedef struct NvRmPowerGetEvent_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerGetEvent_inout;

typedef struct NvRmPowerGetEvent_out_t
{
    NvError ret_;
    NvRmPowerEvent pEvent;
} NV_ALIGN(4) NvRmPowerGetEvent_out;

typedef struct NvRmPowerGetEvent_params_t
{
    NvRmPowerGetEvent_in in;
    NvRmPowerGetEvent_inout inout;
    NvRmPowerGetEvent_out out;
} NvRmPowerGetEvent_params;

typedef struct NvRmPowerUnRegister_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 ClientId;
} NV_ALIGN(4) NvRmPowerUnRegister_in;

typedef struct NvRmPowerUnRegister_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerUnRegister_inout;

typedef struct NvRmPowerUnRegister_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmPowerUnRegister_out;

typedef struct NvRmPowerUnRegister_params_t
{
    NvRmPowerUnRegister_in in;
    NvRmPowerUnRegister_inout inout;
    NvRmPowerUnRegister_out out;
} NvRmPowerUnRegister_params;

typedef struct NvRmPowerRegister_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvOsSemaphoreHandle hEventSemaphore;
} NV_ALIGN(4) NvRmPowerRegister_in;

typedef struct NvRmPowerRegister_inout_t
{
    NvU32 pClientId;
} NV_ALIGN(4) NvRmPowerRegister_inout;

typedef struct NvRmPowerRegister_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmPowerRegister_out;

typedef struct NvRmPowerRegister_params_t
{
    NvRmPowerRegister_in in;
    NvRmPowerRegister_inout inout;
    NvRmPowerRegister_out out;
} NvRmPowerRegister_params;

static NvError NvRmKernelPowerResume_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmKernelPowerResume_in *p_in;
    NvRmKernelPowerResume_out *p_out;

    p_in = (NvRmKernelPowerResume_in *)InBuffer;
    p_out = (NvRmKernelPowerResume_out *)((NvU8 *)OutBuffer + OFFSET(NvRmKernelPowerResume_params, out) - OFFSET(NvRmKernelPowerResume_params, inout));


    p_out->ret_ = NvRmKernelPowerResume( p_in->hRmDeviceHandle );

    return err_;
}

static NvError NvRmKernelPowerSuspend_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmKernelPowerSuspend_in *p_in;
    NvRmKernelPowerSuspend_out *p_out;

    p_in = (NvRmKernelPowerSuspend_in *)InBuffer;
    p_out = (NvRmKernelPowerSuspend_out *)((NvU8 *)OutBuffer + OFFSET(NvRmKernelPowerSuspend_params, out) - OFFSET(NvRmKernelPowerSuspend_params, inout));


    p_out->ret_ = NvRmKernelPowerSuspend( p_in->hRmDeviceHandle );

    return err_;
}

static NvError NvRmDfsSetLowVoltageThreshold_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsSetLowVoltageThreshold_in *p_in;

    p_in = (NvRmDfsSetLowVoltageThreshold_in *)InBuffer;


    NvRmDfsSetLowVoltageThreshold( p_in->hRmDeviceHandle, p_in->RailId, p_in->LowMv );

    return err_;
}

static NvError NvRmDfsGetLowVoltageThreshold_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsGetLowVoltageThreshold_in *p_in;
    NvRmDfsGetLowVoltageThreshold_out *p_out;

    p_in = (NvRmDfsGetLowVoltageThreshold_in *)InBuffer;
    p_out = (NvRmDfsGetLowVoltageThreshold_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsGetLowVoltageThreshold_params, out) - OFFSET(NvRmDfsGetLowVoltageThreshold_params, inout));


    NvRmDfsGetLowVoltageThreshold( p_in->hRmDeviceHandle, p_in->RailId, &p_out->pLowMv, &p_out->pPresentMv );

    return err_;
}

static NvError NvRmDfsLogBusyGetEntry_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsLogBusyGetEntry_in *p_in;
    NvRmDfsLogBusyGetEntry_out *p_out;

    p_in = (NvRmDfsLogBusyGetEntry_in *)InBuffer;
    p_out = (NvRmDfsLogBusyGetEntry_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsLogBusyGetEntry_params, out) - OFFSET(NvRmDfsLogBusyGetEntry_params, inout));


    p_out->ret_ = NvRmDfsLogBusyGetEntry( p_in->hRmDeviceHandle, p_in->EntryIndex, &p_out->pSampleIndex, &p_out->pClientId, &p_out->pClientTag, &p_out->pBusyHint );

    return err_;
}

static NvError NvRmDfsLogStarvationGetEntry_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsLogStarvationGetEntry_in *p_in;
    NvRmDfsLogStarvationGetEntry_out *p_out;

    p_in = (NvRmDfsLogStarvationGetEntry_in *)InBuffer;
    p_out = (NvRmDfsLogStarvationGetEntry_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsLogStarvationGetEntry_params, out) - OFFSET(NvRmDfsLogStarvationGetEntry_params, inout));


    p_out->ret_ = NvRmDfsLogStarvationGetEntry( p_in->hRmDeviceHandle, p_in->EntryIndex, &p_out->pSampleIndex, &p_out->pClientId, &p_out->pClientTag, &p_out->pStarvationHint );

    return err_;
}

static NvError NvRmDfsLogActivityGetEntry_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsLogActivityGetEntry_in *p_in;
    NvRmDfsLogActivityGetEntry_out *p_out;
    NvU32  *pActiveCyclesList = NULL;
    NvRmFreqKHz *pAveragesList = NULL;
    NvRmFreqKHz *pFrequenciesList = NULL;

    p_in = (NvRmDfsLogActivityGetEntry_in *)InBuffer;
    p_out = (NvRmDfsLogActivityGetEntry_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsLogActivityGetEntry_params, out) - OFFSET(NvRmDfsLogActivityGetEntry_params, inout));

    if( p_in->LogDomainsCount && p_in->pActiveCyclesList )
    {
        pActiveCyclesList = (NvU32  *)NvOsAlloc( p_in->LogDomainsCount * sizeof( NvU32  ) );
        if( !pActiveCyclesList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }
    if( p_in->LogDomainsCount && p_in->pAveragesList )
    {
        pAveragesList = (NvRmFreqKHz  *)NvOsAlloc( p_in->LogDomainsCount * sizeof( NvRmFreqKHz  ) );
        if( !pAveragesList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }
    if( p_in->LogDomainsCount && p_in->pFrequenciesList )
    {
        pFrequenciesList = (NvRmFreqKHz  *)NvOsAlloc( p_in->LogDomainsCount * sizeof( NvRmFreqKHz  ) );
        if( !pFrequenciesList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmDfsLogActivityGetEntry( p_in->hRmDeviceHandle, p_in->EntryIndex, p_in->LogDomainsCount, &p_out->pIntervalMs, &p_out->pLp2TimeMs, pActiveCyclesList, pAveragesList, pFrequenciesList );

    if(p_in->pActiveCyclesList && pActiveCyclesList)
    {
        err_ = NvOsCopyOut( p_in->pActiveCyclesList, pActiveCyclesList, p_in->LogDomainsCount * sizeof( NvU32  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
    if(p_in->pAveragesList && pAveragesList)
    {
        err_ = NvOsCopyOut( p_in->pAveragesList, pAveragesList, p_in->LogDomainsCount * sizeof( NvRmFreqKHz  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
    if(p_in->pFrequenciesList && pFrequenciesList)
    {
        err_ = NvOsCopyOut( p_in->pFrequenciesList, pFrequenciesList, p_in->LogDomainsCount * sizeof( NvRmFreqKHz  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pActiveCyclesList );
    NvOsFree( pAveragesList );
    NvOsFree( pFrequenciesList );
    return err_;
}

static NvError NvRmDfsLogGetMeanFrequencies_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsLogGetMeanFrequencies_in *p_in;
    NvRmDfsLogGetMeanFrequencies_out *p_out;
    NvRmFreqKHz *pLogMeanFreqList = NULL;

    p_in = (NvRmDfsLogGetMeanFrequencies_in *)InBuffer;
    p_out = (NvRmDfsLogGetMeanFrequencies_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsLogGetMeanFrequencies_params, out) - OFFSET(NvRmDfsLogGetMeanFrequencies_params, inout));

    if( p_in->LogMeanFreqListCount && p_in->pLogMeanFreqList )
    {
        pLogMeanFreqList = (NvRmFreqKHz  *)NvOsAlloc( p_in->LogMeanFreqListCount * sizeof( NvRmFreqKHz  ) );
        if( !pLogMeanFreqList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmDfsLogGetMeanFrequencies( p_in->hRmDeviceHandle, p_in->LogMeanFreqListCount, pLogMeanFreqList, &p_out->pLogLp2TimeMs, &p_out->pLogLp2Entries );

    if(p_in->pLogMeanFreqList && pLogMeanFreqList)
    {
        err_ = NvOsCopyOut( p_in->pLogMeanFreqList, pLogMeanFreqList, p_in->LogMeanFreqListCount * sizeof( NvRmFreqKHz  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pLogMeanFreqList );
    return err_;
}

static NvError NvRmDfsLogStart_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsLogStart_in *p_in;

    p_in = (NvRmDfsLogStart_in *)InBuffer;


    NvRmDfsLogStart( p_in->hRmDeviceHandle );

    return err_;
}

static NvError NvRmDfsGetProfileData_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsGetProfileData_in *p_in;
    NvRmDfsGetProfileData_out *p_out;
    NvU32  *pSamplesNoList = NULL;
    NvU32  *pProfileTimeUsList = NULL;

    p_in = (NvRmDfsGetProfileData_in *)InBuffer;
    p_out = (NvRmDfsGetProfileData_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsGetProfileData_params, out) - OFFSET(NvRmDfsGetProfileData_params, inout));

    if( p_in->DfsProfileCount && p_in->pSamplesNoList )
    {
        pSamplesNoList = (NvU32  *)NvOsAlloc( p_in->DfsProfileCount * sizeof( NvU32  ) );
        if( !pSamplesNoList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }
    if( p_in->DfsProfileCount && p_in->pProfileTimeUsList )
    {
        pProfileTimeUsList = (NvU32  *)NvOsAlloc( p_in->DfsProfileCount * sizeof( NvU32  ) );
        if( !pProfileTimeUsList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmDfsGetProfileData( p_in->hRmDeviceHandle, p_in->DfsProfileCount, pSamplesNoList, pProfileTimeUsList, &p_out->pDfsPeriodUs );

    if(p_in->pSamplesNoList && pSamplesNoList)
    {
        err_ = NvOsCopyOut( p_in->pSamplesNoList, pSamplesNoList, p_in->DfsProfileCount * sizeof( NvU32  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
    if(p_in->pProfileTimeUsList && pProfileTimeUsList)
    {
        err_ = NvOsCopyOut( p_in->pProfileTimeUsList, pProfileTimeUsList, p_in->DfsProfileCount * sizeof( NvU32  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pSamplesNoList );
    NvOsFree( pProfileTimeUsList );
    return err_;
}

static NvError NvRmDfsSetAvHighCorner_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsSetAvHighCorner_in *p_in;
    NvRmDfsSetAvHighCorner_out *p_out;

    p_in = (NvRmDfsSetAvHighCorner_in *)InBuffer;
    p_out = (NvRmDfsSetAvHighCorner_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsSetAvHighCorner_params, out) - OFFSET(NvRmDfsSetAvHighCorner_params, inout));


    p_out->ret_ = NvRmDfsSetAvHighCorner( p_in->hRmDeviceHandle, p_in->DfsSystemHighKHz, p_in->DfsAvpHighKHz, p_in->DfsVpipeHighKHz );

    return err_;
}

static NvError NvRmDfsSetCpuEmcHighCorner_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsSetCpuEmcHighCorner_in *p_in;
    NvRmDfsSetCpuEmcHighCorner_out *p_out;

    p_in = (NvRmDfsSetCpuEmcHighCorner_in *)InBuffer;
    p_out = (NvRmDfsSetCpuEmcHighCorner_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsSetCpuEmcHighCorner_params, out) - OFFSET(NvRmDfsSetCpuEmcHighCorner_params, inout));


    p_out->ret_ = NvRmDfsSetCpuEmcHighCorner( p_in->hRmDeviceHandle, p_in->DfsCpuHighKHz, p_in->DfsEmcHighKHz );

    return err_;
}

static NvError NvRmDfsSetEmcEnvelope_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsSetEmcEnvelope_in *p_in;
    NvRmDfsSetEmcEnvelope_out *p_out;

    p_in = (NvRmDfsSetEmcEnvelope_in *)InBuffer;
    p_out = (NvRmDfsSetEmcEnvelope_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsSetEmcEnvelope_params, out) - OFFSET(NvRmDfsSetEmcEnvelope_params, inout));


    p_out->ret_ = NvRmDfsSetEmcEnvelope( p_in->hRmDeviceHandle, p_in->DfsEmcLowCornerKHz, p_in->DfsEmcHighCornerKHz );

    return err_;
}

static NvError NvRmDfsSetCpuEnvelope_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsSetCpuEnvelope_in *p_in;
    NvRmDfsSetCpuEnvelope_out *p_out;

    p_in = (NvRmDfsSetCpuEnvelope_in *)InBuffer;
    p_out = (NvRmDfsSetCpuEnvelope_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsSetCpuEnvelope_params, out) - OFFSET(NvRmDfsSetCpuEnvelope_params, inout));


    p_out->ret_ = NvRmDfsSetCpuEnvelope( p_in->hRmDeviceHandle, p_in->DfsCpuLowCornerKHz, p_in->DfsCpuHighCornerKHz );

    return err_;
}

static NvError NvRmDfsSetTarget_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsSetTarget_in *p_in;
    NvRmDfsSetTarget_out *p_out;
    NvRmFreqKHz *pDfsTargetFreqList = NULL;

    p_in = (NvRmDfsSetTarget_in *)InBuffer;
    p_out = (NvRmDfsSetTarget_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsSetTarget_params, out) - OFFSET(NvRmDfsSetTarget_params, inout));

    if( p_in->DfsFreqListCount && p_in->pDfsTargetFreqList )
    {
        pDfsTargetFreqList = (NvRmFreqKHz  *)NvOsAlloc( p_in->DfsFreqListCount * sizeof( NvRmFreqKHz  ) );
        if( !pDfsTargetFreqList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pDfsTargetFreqList )
        {
            err_ = NvOsCopyIn( pDfsTargetFreqList, p_in->pDfsTargetFreqList, p_in->DfsFreqListCount * sizeof( NvRmFreqKHz  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmDfsSetTarget( p_in->hRmDeviceHandle, p_in->DfsFreqListCount, pDfsTargetFreqList );

clean:
    NvOsFree( pDfsTargetFreqList );
    return err_;
}

static NvError NvRmDfsSetLowCorner_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsSetLowCorner_in *p_in;
    NvRmDfsSetLowCorner_out *p_out;
    NvRmFreqKHz *pDfsLowFreqList = NULL;

    p_in = (NvRmDfsSetLowCorner_in *)InBuffer;
    p_out = (NvRmDfsSetLowCorner_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsSetLowCorner_params, out) - OFFSET(NvRmDfsSetLowCorner_params, inout));

    if( p_in->DfsFreqListCount && p_in->pDfsLowFreqList )
    {
        pDfsLowFreqList = (NvRmFreqKHz  *)NvOsAlloc( p_in->DfsFreqListCount * sizeof( NvRmFreqKHz  ) );
        if( !pDfsLowFreqList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pDfsLowFreqList )
        {
            err_ = NvOsCopyIn( pDfsLowFreqList, p_in->pDfsLowFreqList, p_in->DfsFreqListCount * sizeof( NvRmFreqKHz  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmDfsSetLowCorner( p_in->hRmDeviceHandle, p_in->DfsFreqListCount, pDfsLowFreqList );

clean:
    NvOsFree( pDfsLowFreqList );
    return err_;
}

static NvError NvRmDfsSetState_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsSetState_in *p_in;
    NvRmDfsSetState_out *p_out;

    p_in = (NvRmDfsSetState_in *)InBuffer;
    p_out = (NvRmDfsSetState_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsSetState_params, out) - OFFSET(NvRmDfsSetState_params, inout));


    p_out->ret_ = NvRmDfsSetState( p_in->hRmDeviceHandle, p_in->NewDfsRunState );

    return err_;
}

static NvError NvRmDfsGetClockUtilization_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsGetClockUtilization_in *p_in;
    NvRmDfsGetClockUtilization_out *p_out;

    p_in = (NvRmDfsGetClockUtilization_in *)InBuffer;
    p_out = (NvRmDfsGetClockUtilization_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsGetClockUtilization_params, out) - OFFSET(NvRmDfsGetClockUtilization_params, inout));


    p_out->ret_ = NvRmDfsGetClockUtilization( p_in->hRmDeviceHandle, p_in->ClockId, &p_out->pClockUsage );

    return err_;
}

static NvError NvRmDfsGetState_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmDfsGetState_in *p_in;
    NvRmDfsGetState_out *p_out;

    p_in = (NvRmDfsGetState_in *)InBuffer;
    p_out = (NvRmDfsGetState_out *)((NvU8 *)OutBuffer + OFFSET(NvRmDfsGetState_params, out) - OFFSET(NvRmDfsGetState_params, inout));


    p_out->ret_ = NvRmDfsGetState( p_in->hRmDeviceHandle );

    return err_;
}

static NvError NvRmPowerActivityHint_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerActivityHint_in *p_in;
    NvRmPowerActivityHint_out *p_out;

    p_in = (NvRmPowerActivityHint_in *)InBuffer;
    p_out = (NvRmPowerActivityHint_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerActivityHint_params, out) - OFFSET(NvRmPowerActivityHint_params, inout));


    p_out->ret_ = NvRmPowerActivityHint( p_in->hRmDeviceHandle, p_in->ModuleId, p_in->ClientId, p_in->ActivityDurationMs );

    return err_;
}

static NvError NvRmPowerStarvationHintMulti_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerStarvationHintMulti_in *p_in;
    NvRmPowerStarvationHintMulti_out *p_out;
    NvRmDfsStarvationHint *pMultiHint = NULL;

    p_in = (NvRmPowerStarvationHintMulti_in *)InBuffer;
    p_out = (NvRmPowerStarvationHintMulti_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerStarvationHintMulti_params, out) - OFFSET(NvRmPowerStarvationHintMulti_params, inout));

    if( p_in->NumHints && p_in->pMultiHint )
    {
        pMultiHint = (NvRmDfsStarvationHint  *)NvOsAlloc( p_in->NumHints * sizeof( NvRmDfsStarvationHint  ) );
        if( !pMultiHint )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pMultiHint )
        {
            err_ = NvOsCopyIn( pMultiHint, p_in->pMultiHint, p_in->NumHints * sizeof( NvRmDfsStarvationHint  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmPowerStarvationHintMulti( p_in->hRmDeviceHandle, p_in->ClientId, pMultiHint, p_in->NumHints );

clean:
    NvOsFree( pMultiHint );
    return err_;
}

static NvError NvRmPowerStarvationHint_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerStarvationHint_in *p_in;
    NvRmPowerStarvationHint_out *p_out;

    p_in = (NvRmPowerStarvationHint_in *)InBuffer;
    p_out = (NvRmPowerStarvationHint_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerStarvationHint_params, out) - OFFSET(NvRmPowerStarvationHint_params, inout));


    p_out->ret_ = NvRmPowerStarvationHint( p_in->hRmDeviceHandle, p_in->ClockId, p_in->ClientId, p_in->Starving );

    return err_;
}

static NvError NvRmPowerBusyHintMulti_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerBusyHintMulti_in *p_in;
    NvRmPowerBusyHintMulti_out *p_out;
    NvRmDfsBusyHint *pMultiHint = NULL;

    p_in = (NvRmPowerBusyHintMulti_in *)InBuffer;
    p_out = (NvRmPowerBusyHintMulti_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerBusyHintMulti_params, out) - OFFSET(NvRmPowerBusyHintMulti_params, inout));

    if( p_in->NumHints && p_in->pMultiHint )
    {
        pMultiHint = (NvRmDfsBusyHint  *)NvOsAlloc( p_in->NumHints * sizeof( NvRmDfsBusyHint  ) );
        if( !pMultiHint )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pMultiHint )
        {
            err_ = NvOsCopyIn( pMultiHint, p_in->pMultiHint, p_in->NumHints * sizeof( NvRmDfsBusyHint  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmPowerBusyHintMulti( p_in->hRmDeviceHandle, p_in->ClientId, pMultiHint, p_in->NumHints, p_in->Mode );

clean:
    NvOsFree( pMultiHint );
    return err_;
}

static NvError NvRmPowerBusyHint_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerBusyHint_in *p_in;
    NvRmPowerBusyHint_out *p_out;

    p_in = (NvRmPowerBusyHint_in *)InBuffer;
    p_out = (NvRmPowerBusyHint_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerBusyHint_params, out) - OFFSET(NvRmPowerBusyHint_params, inout));


    p_out->ret_ = NvRmPowerBusyHint( p_in->hRmDeviceHandle, p_in->ClockId, p_in->ClientId, p_in->BoostDurationMs, p_in->BoostKHz );

    return err_;
}

static NvError NvRmListPowerAwareModules_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmListPowerAwareModules_in *p_in;
    NvRmListPowerAwareModules_inout *p_inout;
    NvRmListPowerAwareModules_inout inout;
    NvRmModuleID *pIdList = NULL;
    NvBool  *pActiveList = NULL;

    p_in = (NvRmListPowerAwareModules_in *)InBuffer;
    p_inout = (NvRmListPowerAwareModules_inout *)((NvU8 *)InBuffer + OFFSET(NvRmListPowerAwareModules_params, inout));

    (void)inout;
    inout.pListSize = p_inout->pListSize;
    if( p_inout->pListSize && p_in->pIdList )
    {
        pIdList = (NvRmModuleID  *)NvOsAlloc( p_inout->pListSize * sizeof( NvRmModuleID  ) );
        if( !pIdList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }
    if( p_inout->pListSize && p_in->pActiveList )
    {
        pActiveList = (NvBool  *)NvOsAlloc( p_inout->pListSize * sizeof( NvBool  ) );
        if( !pActiveList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    NvRmListPowerAwareModules( p_in->hRmDeviceHandle, &inout.pListSize, pIdList, pActiveList );


    p_inout = (NvRmListPowerAwareModules_inout *)OutBuffer;
    p_inout->pListSize = inout.pListSize;
    if(p_in->pIdList && pIdList)
    {
        err_ = NvOsCopyOut( p_in->pIdList, pIdList, p_inout->pListSize * sizeof( NvRmModuleID  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
    if(p_in->pActiveList && pActiveList)
    {
        err_ = NvOsCopyOut( p_in->pActiveList, pActiveList, p_inout->pListSize * sizeof( NvBool  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pIdList );
    NvOsFree( pActiveList );
    return err_;
}

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

static NvError NvRmPowerModuleGetMaxFrequency_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerModuleGetMaxFrequency_in *p_in;
    NvRmPowerModuleGetMaxFrequency_out *p_out;

    p_in = (NvRmPowerModuleGetMaxFrequency_in *)InBuffer;
    p_out = (NvRmPowerModuleGetMaxFrequency_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerModuleGetMaxFrequency_params, out) - OFFSET(NvRmPowerModuleGetMaxFrequency_params, inout));


    p_out->ret_ = NvRmPowerModuleGetMaxFrequency( p_in->hRmDeviceHandle, p_in->ModuleId );

    return err_;
}

static NvError NvRmPowerGetPrimaryFrequency_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerGetPrimaryFrequency_in *p_in;
    NvRmPowerGetPrimaryFrequency_out *p_out;

    p_in = (NvRmPowerGetPrimaryFrequency_in *)InBuffer;
    p_out = (NvRmPowerGetPrimaryFrequency_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerGetPrimaryFrequency_params, out) - OFFSET(NvRmPowerGetPrimaryFrequency_params, inout));


    p_out->ret_ = NvRmPowerGetPrimaryFrequency( p_in->hRmDeviceHandle );

    return err_;
}

static NvError NvRmPowerGetState_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerGetState_in *p_in;
    NvRmPowerGetState_out *p_out;

    p_in = (NvRmPowerGetState_in *)InBuffer;
    p_out = (NvRmPowerGetState_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerGetState_params, out) - OFFSET(NvRmPowerGetState_params, inout));


    p_out->ret_ = NvRmPowerGetState( p_in->hRmDeviceHandle, &p_out->pState );

    return err_;
}

static NvError NvRmPowerEventNotify_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerEventNotify_in *p_in;

    p_in = (NvRmPowerEventNotify_in *)InBuffer;


    NvRmPowerEventNotify( p_in->hRmDeviceHandle, p_in->Event );

    return err_;
}

static NvError NvRmPowerGetEvent_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerGetEvent_in *p_in;
    NvRmPowerGetEvent_out *p_out;

    p_in = (NvRmPowerGetEvent_in *)InBuffer;
    p_out = (NvRmPowerGetEvent_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerGetEvent_params, out) - OFFSET(NvRmPowerGetEvent_params, inout));


    p_out->ret_ = NvRmPowerGetEvent( p_in->hRmDeviceHandle, p_in->ClientId, &p_out->pEvent );

    return err_;
}

static NvError NvRmPowerUnRegister_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerUnRegister_in *p_in;

    p_in = (NvRmPowerUnRegister_in *)InBuffer;


    NvRmPowerUnRegister( p_in->hRmDeviceHandle, p_in->ClientId );

    return err_;
}

static NvError NvRmPowerRegister_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmPowerRegister_in *p_in;
    NvRmPowerRegister_inout *p_inout;
    NvRmPowerRegister_out *p_out;
    NvRmPowerRegister_inout inout;
    NvOsSemaphoreHandle hEventSemaphore = NULL;

    p_in = (NvRmPowerRegister_in *)InBuffer;
    p_inout = (NvRmPowerRegister_inout *)((NvU8 *)InBuffer + OFFSET(NvRmPowerRegister_params, inout));
    p_out = (NvRmPowerRegister_out *)((NvU8 *)OutBuffer + OFFSET(NvRmPowerRegister_params, out) - OFFSET(NvRmPowerRegister_params, inout));

    (void)inout;
    if( p_in->hEventSemaphore )
    {
        err_ = NvOsSemaphoreUnmarshal( p_in->hEventSemaphore, &hEventSemaphore );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
            goto clean;
        }
    }
    inout.pClientId = p_inout->pClientId;

    p_out->ret_ = NvRmPowerRegister( p_in->hRmDeviceHandle, hEventSemaphore, &inout.pClientId );


    p_inout = (NvRmPowerRegister_inout *)OutBuffer;
    p_inout->pClientId = inout.pClientId;
clean:
    NvOsSemaphoreDestroy( hEventSemaphore );
    return err_;
}

NvError nvrm_power_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_power_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 34:
        err_ = NvRmKernelPowerResume_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 33:
        err_ = NvRmKernelPowerSuspend_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 32:
        err_ = NvRmDfsSetLowVoltageThreshold_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 31:
        err_ = NvRmDfsGetLowVoltageThreshold_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 30:
        err_ = NvRmDfsLogBusyGetEntry_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 29:
        err_ = NvRmDfsLogStarvationGetEntry_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 28:
        err_ = NvRmDfsLogActivityGetEntry_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 27:
        err_ = NvRmDfsLogGetMeanFrequencies_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 26:
        err_ = NvRmDfsLogStart_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 25:
        err_ = NvRmDfsGetProfileData_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 24:
        err_ = NvRmDfsSetAvHighCorner_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 23:
        err_ = NvRmDfsSetCpuEmcHighCorner_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 22:
        err_ = NvRmDfsSetEmcEnvelope_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 21:
        err_ = NvRmDfsSetCpuEnvelope_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 20:
        err_ = NvRmDfsSetTarget_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 19:
        err_ = NvRmDfsSetLowCorner_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 18:
        err_ = NvRmDfsSetState_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 17:
        err_ = NvRmDfsGetClockUtilization_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 16:
        err_ = NvRmDfsGetState_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 15:
        err_ = NvRmPowerActivityHint_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 14:
        err_ = NvRmPowerStarvationHintMulti_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 13:
        err_ = NvRmPowerStarvationHint_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 12:
        err_ = NvRmPowerBusyHintMulti_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 11:
        err_ = NvRmPowerBusyHint_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 10:
        err_ = NvRmListPowerAwareModules_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 9:
        err_ = NvRmPowerVoltageControl_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 8:
        err_ = NvRmPowerModuleClockControl_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 7:
        err_ = NvRmPowerModuleClockConfig_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 6:
        err_ = NvRmPowerModuleGetMaxFrequency_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 5:
        err_ = NvRmPowerGetPrimaryFrequency_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmPowerGetState_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmPowerEventNotify_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmPowerGetEvent_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmPowerUnRegister_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmPowerRegister_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
