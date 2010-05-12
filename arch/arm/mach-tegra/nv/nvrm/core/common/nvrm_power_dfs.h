/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
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

/** 
 * @file
 * @brief <b>nVIDIA Driver Development Kit: 
 *           Power Resource manager </b>
 *
 * @b Description: NvRM DFS manager definitions. 
 * 
 */

#ifndef INCLUDED_NVRM_POWER_DFS_H
#define INCLUDED_NVRM_POWER_DFS_H

#include "nvrm_power_private.h"
#include "nvrm_clocks.h"
#include "nvrm_interrupt.h"
#include "nvodm_tmon.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

/**
 * Sampling window definitions:
 * - minimum and maximum sampling interval in ms
 * - maximum number of intervals in the sampling window
 * (always defined as power of 2 to simplify calculations)
 */
#define NVRM_DFS_MIN_SAMPLE_MS (10)
#define NVRM_DFS_MAX_SAMPLE_MS (20)

#define NVRM_DFS_MAX_SAMPLES_LOG2 (7)
#define NVRM_DFS_MAX_SAMPLES (0x1 << NVRM_DFS_MAX_SAMPLES_LOG2)

/// Specifies that CPU idle monitor readings should be explicitly offset
///  by time spent in LP2
#define NVRM_CPU_IDLE_LP2_OFFSET (1)

/// Number of bits in the fractional part of boost koefficients
#define BOOST_FRACTION_BITS (8)

/*****************************************************************************/

/// Enumerates synchronous busy hints states
typedef enum
{
    NvRmDfsBusySyncState_Idle = 0,
    NvRmDfsBusySyncState_Signal,
    NvRmDfsBusySyncState_Execute,

    NvRmDfsBusySyncState_Num,
    NvRmDfsBusySyncState_Force32 = 0x7FFFFFFF
} NvRmDfsBusySyncState;

/// Enumerates DFS modules = modules, which include activity monitors for clock
/// domains controlled by DFS
typedef enum
{
    // Specifies system statistic module - includes activity monitors
    // for CPU, AVP, AHB, and APB clock domains
    NvRmDfsModuleId_Systat = 1,

    // Specifies VDE module - includes activity monitor
    // for video-pipe clock domain
    NvRmDfsModuleId_Vde,

    // Specifies EMC module - includes activity monitor
    // for EMC 1x clock domain
    NvRmDfsModuleId_Emc,

    NvRmDfsModuleId_Num,
    NvRmDfsModuleId_Force32 = 0x7FFFFFFF
} NvRmDfsModuleId;

/**
 * Combines idle count readings from DFS activity monitors during current
 * sample interval
 */
typedef struct NvRmDfsIdleDataRec
{
    // Current Sample interval in ms
    NvU32 CurrentIntervalMs;

    // Data readings from DFS activity monitors
    NvU32 Readings[NvRmDfsClockId_Num];

    // Time spent in LP2 in ms
    NvU32 Lp2TimeMs;
} NvRmDfsIdleData;

/**
 *  DFS module access function pointers
 */
typedef struct NvRmDfsRec* NvRmDfsPtr;
typedef const struct NvRmDfsRec* NvRmConstDfsPtr;
typedef NvError (*FuncPtrModuleMonitorsInit)(NvRmDfsPtr pDfs);
typedef void (*FuncPtrModuleMonitorsDeinit)(NvRmDfsPtr pDfs);

typedef void
(*FuncPtrModuleMonitorsStart)(
    NvRmConstDfsPtr pDfs,
    const NvRmDfsFrequencies* pDfsKHz,
    const NvU32 IntevalMs);

typedef void
(*FuncPtrModuleMonitorsRead)(
    NvRmConstDfsPtr pDfs,
    const NvRmDfsFrequencies* pDfsKHz,
    NvRmDfsIdleData* pIdleData);

/**
 * Combines capabilities, access function pointers, and base virtual
 * addresses of the DFS module
 */
typedef struct NvRmDfsModuleRec
{
    // Clock domains monitored by this module 
    NvBool DomainMap[NvRmDfsClockId_Num];

    // Pointer to the function that initializes module activity monitors
    // (null if module is not present)
    FuncPtrModuleMonitorsInit Init;

    // Pointer to the function that de-initializes module activity monitors
    // (null if module is not present)
    FuncPtrModuleMonitorsDeinit Deinit;

    // Pointer to the function that starts module activity monitors
    // (null if module is not present)
    FuncPtrModuleMonitorsStart Start;

    // Pointer to the function that reads module activity monitors
    // (null if module is not present)
    FuncPtrModuleMonitorsRead Read;

    // Monitor readouts scale and offset (usage and interpretation may differ
    // for different monitors)
    NvU32 Scale;
    NvU32 Offset;

    // Base virtual address for module registers
    void* pBaseReg;
} NvRmDfsModule;

/*****************************************************************************/

/**
 * Combines DFS starvation control parameters
 */
typedef struct NvRmDfsStarveParamRec
{
    // Fixed increase in frequency boost for a sample interval the clock
    // consumer is starving: new boost = old boost +  BoostStepKHz
    NvRmFreqKHz BoostStepKHz;

    // Proportional increase in frequency boost for a sample interval the
    // clock consumer is starving (scaled in 0-255 range):
    // new boost = old boost + old boost * BoostIncKoef / 256
    NvU8 BoostIncKoef;

    // Proportional decrease in frequency boost for a sample interval the
    // clock consumer is not starving (scaled in 0-255 range):
    // new boost = old boost - old boost * BoostDecKoef / 256
    NvU8 BoostDecKoef;
} NvRmDfsStarveParam;


/**
 * Combines scaling algorithm parameters for DFS controlled clock domain
 */
typedef struct NvRmDfsParamRec
{
    // Maximum domain clock frequency
    NvRmFreqKHz MaxKHz;
    // Minimum domain clock frequency
    NvRmFreqKHz MinKHz;

    // Minimum average activity change in upward direction recognized by DFS
    NvRmFreqKHz UpperBandKHz;
    // Minimum average activity change in downward direction recognized by DFS
    NvRmFreqKHz LowerBandKHz;

    // Control parameters for real time starvation reported by the DFS client
    NvRmDfsStarveParam RtStarveParam;

    // Control parameters for non real time starvation detected by DFS itself
    NvRmDfsStarveParam NrtStarveParam;

    // Relative adjustment up of average activity applied by DFS:
    // adjusted frequency = measured average activity * (1 + 2^(-RelAdjustBits))
    NvU8 RelAdjustBits;

    // Minimum number of sample intervals in a row with non-realtime starvation
    // that triggers frequency boost (0 = boost trigger on the 1st NRT interval)
    NvU8 MinNrtSamples;

    // Minimum number of idle cycles in the sample interval required to avoid
    // non-realtime starvation
    NvU32 MinNrtIdleCycles;
} NvRmDfsParam;

/**
 * Combines sampling statistic and starvation controls for DFS clock domain
 */
typedef struct NvRmDfsSamplerRec
{
    // Domain clock id
    NvRmDfsClockId ClockId;

    // Activity monitor present indicator (domain is still controlled by DFS
    // even if no activity monitor present)
    NvBool MonitorPresent;

    // Circular buffer of active cycles per sample interval within the
    // sampling window
    NvU32 Cycles[NVRM_DFS_MAX_SAMPLES];

    // Pointer to the last ("recent") sample in the sampling window
    NvU32* pLastSample;

    // Total number of active cycles in the sampling window
    NvU64 TotalActiveCycles;

    // Measured average clock activity frequency over the sampling window
    NvRmFreqKHz AverageKHz;

    // Average clock frequency adjusted up by DFS
    NvRmFreqKHz BumpedAverageKHz;

    // Non-real time starving sample counter
    NvU32 NrtSampleCounter;

    // Non-real time starvation boost 
    NvRmFreqKHz NrtStarveBoostKHz;

    // Real time starvation boost 
    NvRmFreqKHz RtStarveBoostKHz;

    // Busy pulse mode indicator - if true, busy boost is completely removed
    // after busy time has expired; if false, DFS averaging mechanism is used
    // to gradually lower frequency after busy boost
    NvBool BusyPulseMode;

    // Cumulative number of cycles since log start
    NvU64 CumulativeLogCycles;
} NvRmDfsSampler;

/**
 * Holds information for DFS moving sampling window
 */
typedef struct NvRmDfsSampleWindowRec
{
    // Minimum sampling interval
    NvU32 MinIntervalMs;

    // Maximum sampling interval
    NvU32 MaxIntervalMs;

    // Next sample interval
    NvU32 NextIntervalMs;

    // Circular buffer of sample intervals in the sampling window
    NvU32 IntervalsMs[NVRM_DFS_MAX_SAMPLES];

    // Pointer to the last ("recent") sample unterval in the sampling window
    NvU32* pLastInterval;

    // Cumulative width of the sampling window
    NvU32 SampleWindowMs;

    // Last busy hints check time stamp
    NvU32 BusyCheckLastUs;

    // Delay before busy hints next check
    NvU32 BusyCheckDelayUs;

    // Free running sample counter
    NvU32 SampleCnt;

    // Cumulative DFS time since log start
    NvU32 CumulativeLogMs;

    // Cumulative LP2 statistic since log start
    NvU32 CumulativeLp2TimeMs;
    NvU32 CumulativeLp2Entries;
} NvRmDfsSampleWindow;

/*****************************************************************************/

/**
 * Holds voltage corner for DFS domains and non-DFS modules. Each voltage
 * corner field specifies minimum core voltage required to run the respective
 * device(s) at current clock frequency.
 */
typedef struct NvRmDvsCornerRec
{
    // CPU voltage requirements
    NvRmMilliVolts CpuMv;

    // AVP/System voltage requirements
    NvRmMilliVolts SystemMv;

    // EMC / DDR voltage requirements
    NvRmMilliVolts EmcMv;

    // Cumulative voltage requirements for non-DFS modules
    NvRmMilliVolts ModulesMv;
} NvRmDvsCorner;

/**
 * Combines voltage threshold and core rail status and control information
 */
typedef struct NvRmDvsRec
{
    // Current DVS voltage thresholds
    NvRmDvsCorner DvsCorner;

    // RTC (AO) rail address (PMU voltage id)
    NvU32 RtcRailAddress;

    // Core rail address (PMU voltage id)
    NvU32 CoreRailAddress;

    // Current core rail voltage
    NvRmMilliVolts CurrentCoreMv;

    // Nominal core rail voltage
    NvRmMilliVolts NominalCoreMv;

    // Minimum core rail voltage
    NvRmMilliVolts MinCoreMv;

    // Low corner voltage for core rail loaded by DVS control API
    NvRmMilliVolts LowCornerCoreMv;

    // Dedicated Cpu rail address (PMU voltage id)
    NvU32 CpuRailAddress;

    // Current dedicated CPU rail voltage
    NvRmMilliVolts CurrentCpuMv;

    // Nominal dedicated CPU rail voltage
    NvRmMilliVolts NominalCpuMv;

    // Minimum dedicated CPU rail voltage
    NvRmMilliVolts MinCpuMv;

    // Low corner voltage for CPU rail loaded by DVS control API
    NvRmMilliVolts LowCornerCpuMv;

    // OTP (default) dedicated CPU rail voltage
    NvRmMilliVolts CpuOTPMv;

    // Specifies whether or not CPU voltage will switch back to OTP
    // (default) value after CPU request On-Off-On transition
    NvBool  VCpuOTPOnWakeup;

    // RAM timing SVOP controls low voltage threshold
    NvRmMilliVolts LowSvopThresholdMv;

    // RAM timing SVOP controls low voltage setting
    NvU32 LowSvopSettings;

    // RAM timing SVOP controls high voltage setting
    NvU32 HighSvopSettings;

    // Request core voltage update
    volatile NvBool UpdateFlag;

    // Stop voltage scaling flag
    volatile NvBool StopFlag;

    // CPU LP2 state indicator (used on platforms with dedicated CPU rail that
    // returns to default setting by PMU underneath DVFS on every LP2 exit)
    volatile NvBool Lp2SyncOTPFlag;
} NvRmDvs;

/**
 * RM thermal zone policy
 */
typedef struct NvRmTzonePolicyRec
{
    // Request policy update
    volatile NvBool UpdateFlag;

    // Last policy update request time stamp
    NvU32 TimeUs;

    // Update period (NV_WAIT_INFINITE is allowed in interrupt mode) 
    NvU32 UpdateIntervalUs;

    // Out of limit interrupt boundaries
    NvS32 LowLimit;
    NvS32 HighLimit;

    // Policy range
    NvU32 PolicyRange;
} NvRmTzonePolicy;

/**
 * Combines status and control information for dynamic thermal throttling
 */
typedef struct NvRmDttRec
{
    // SoC core temperature monitor (TMON) handle
    NvOdmTmonDeviceHandle hOdmTcore;

    // Core TMON out-of-limit-interrupt handle
    NvOdmTmonIntrHandle hOdmTcoreIntr;

    // Core TMON capabilities
    NvOdmTmonCapabilities TcoreCaps;

    // Out of limit interrupt cpabilities for low limit
    NvOdmTmonParameterCaps TcoreLowLimitCaps;

    // Out-of-limit interrupt cpabilities for high limit
    NvOdmTmonParameterCaps TcoreHighLimitCaps;

    // Core zone policy
    NvRmTzonePolicy TcorePolicy;

    // Core temperature
    NvS32 CoreTemperatureC;

    // Specifies if out-of-limit interrupt is used for temperature update
    volatile NvBool UseIntr;
} NvRmDtt;

/*****************************************************************************/

/**
 * Combines DFS status and control information
 */
typedef struct NvRmDfsRec
{
    // RM Device handle
    NvRmDeviceHandle hRm;

    // DFS state variable
    NvRmDfsRunState DfsRunState;

    // DFS state saved on system suspend entry
    NvRmDfsRunState DfsLPxSavedState;

    // ID assigned to DFS by RM Power Manager
    NvU32 PowerClientId;

    // DFS low power corner hit status - true, when all domains (with
    // possible exception of CPU) are running at minimum frequency
    NvBool LowCornerHit;

    // Request to report low corner hit status to OS adaptation layer; DFS
    // interrupt will not wake CPU if it is power gated and low corner is hit
    NvBool LowCornerReport;

    // PM thread request for CPU state control
    NvRmPmRequest PmRequest;

    // DFS IRQ number
    NvU16 IrqNumber;

    // DFS mutex for safe data access by DFS ISR,
    // clock control thread, and API threads
    NvOsIntrMutexHandle hIntrMutex;

    // DFS mutex for synchronous busy hints
    NvOsMutexHandle hSyncBusyMutex;

    // DFS semaphore for synchronous busy hints
    NvOsSemaphoreHandle hSyncBusySemaphore;

    // Synchronous busy hints state
    volatile NvRmDfsBusySyncState BusySyncState;

    // Clock control execution thread init indicator
    volatile NvBool InitializedThread;

    // Clock control execution thread abort indicator
    volatile NvBool AbortThread;

    // DFS semaphore for sampling interrupt and wake event signaling
    NvOsSemaphoreHandle hSemaphore;

    // DFS Modules
    NvRmDfsModule Modules[NvRmDfsModuleId_Num];

    // DFS algorithm parameters 
    NvRmDfsParam DfsParameters[NvRmDfsClockId_Num];

    // DFS Samplers
    NvRmDfsSampler Samplers[NvRmDfsClockId_Num];

    // DFS sampling window
    NvRmDfsSampleWindow SamplingWindow;

    // Maximum DFS domains frequencies (shortcut to the respective parameters)
    NvRmDfsFrequencies MaxKHz;

    // Target DFS doamins frequencies: output of the DFS algorithm,
    // input to clock control
    NvRmDfsFrequencies TargetKHz;

    // Current DFS domains frequencies: output from clock control, input
    // to DFS algorithm
    NvRmDfsFrequencies CurrentKHz;

    // DFS domains frequencies set on entry to suspend state
    NvRmDfsFrequencies SuspendKHz;

    // Busy boost frequencies requested by Busy load API
    NvRmDfsFrequencies BusyKHz;

    // Low corner frequencies loaded by DFS control API
    NvRmDfsFrequencies LowCornerKHz;

    // High corner frequencies loaded by DFS control API
    NvRmDfsFrequencies HighCornerKHz;

    // A shadow of CPU corners (updated by APIs that directly set CPU corners,
    // preserved when CPU corners are indirectly throttled by EMC envelope)
    NvRmModuleClockLimits CpuCornersShadow;

    // CPU envelope API indicator (if set supercedes low/high corner APIs)
    NvBool CpuEnvelopeSet;

    // EMC envelope API indicator (if set supercedes low/high corner APIs)
    NvBool EmcEnvelopeSet;

    // Voltage Scaler
    NvRmDvs VoltageScaler;

    // Thermal throttler
    NvRmDtt ThermalThrottler;

    // nvos interrupt handle for DVS 
    NvOsInterruptHandle DfsInterruptHandle;
} NvRmDfs;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif // INCLUDED_NVRM_POWER_DFS_H
