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

#ifndef INCLUDED_nvrm_power_H
#define INCLUDED_nvrm_power_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_module.h"
#include "nvrm_init.h"

#include "nvos.h"

/**
 * Frequency data type, expressed in KHz.
 */

typedef NvU32 NvRmFreqKHz;

/**
 * Special value for an unspecified or default frequency.
 */
static const NvRmFreqKHz NvRmFreqUnspecified = 0xFFFFFFFF;

/**
 * Special value for the maximum possible frequency.
 */
static const NvRmFreqKHz NvRmFreqMaximum = 0xFFFFFFFD;

/**
 * Voltage data type, expressed in millivolts.
 */

typedef NvU32 NvRmMilliVolts;

/**
 * Special value for an unspecified or default voltage.
 */
static const NvRmMilliVolts NvRmVoltsUnspecified = 0xFFFFFFFF;

/**
 * Special value for the maximum possible voltage.
 */
static const NvRmMilliVolts NvRmVoltsMaximum = 0xFFFFFFFD;

/**
 * Special value for voltage / power disable.
 */
static const NvRmMilliVolts NvRmVoltsCycled = 0xFFFFFFFC;

/**
 * Special value for voltage / power disable.
 */
static const NvRmMilliVolts NvRmVoltsOff = 0;

/**
 * Defines possible power management events
 */

typedef enum
{

    /// Specifies no outstanding events
        NvRmPowerEvent_NoEvent = 1,

    /// Specifies wake from LP0
        NvRmPowerEvent_WakeLP0,

    /// Specifies wake from LP1
        NvRmPowerEvent_WakeLP1,
    NvRmPowerEvent_Num,
    NvRmPowerEvent_Force32 = 0x7FFFFFFF
} NvRmPowerEvent;

/**
 * Defines combined RM clients power state
 */

typedef enum
{

    /// Specifies boot state ("RM is not open, yet")
        NvRmPowerState_Boot = 1,

    /// Specifies active state ("not ready-to-suspend")
    /// This state is entered if any client enables power to any module, other
    /// than NvRmPrivModuleID_System, via NvRmPowerVoltageControl() API
        NvRmPowerState_Active,

    /// Specifies h/w autonomous state ("ready-to-core-power-on-suspend")
    /// This state is entered if all RM clients enable power only for
    /// NvRmPrivModuleID_System, via NvRmPowerVoltageControl() API
        NvRmPowerState_AutoHw,

    /// Specifies idle state ("ready-to-core-power-off-suspend")
    /// This state is entered if none of the RM clients enables power
    /// to any module.
        NvRmPowerState_Idle,

    /// Specifies LP0 state ("main power-off suspend")
        NvRmPowerState_LP0,

    /// Specifies LP1 state ("main power-on suspend")
        NvRmPowerState_LP1,

    /// Specifies Skipped LP0 state (set when LP0 entry error is
    /// detected, SoC resumes operations without entering LP0 state)
        NvRmPowerState_SkippedLP0,
    NvRmPowerState_Num,
    NvRmPowerState_Force32 = 0x7FFFFFFF
} NvRmPowerState;

/** Defines the clock configuration flags which are applicable for some modules.
 * Multiple flags can be OR'ed and passed to the NvRmPowerModuleClockConfig API.
*/

typedef enum
{

    /// Use external clock for the pads of the module.
        NvRmClockConfig_ExternalClockForPads = 0x1,

    /// Use internal clock for the pads of the module
        NvRmClockConfig_InternalClockForPads = 0x2,

    /// Use external clock for the core of the module, or
    /// module is in slave mode
        NvRmClockConfig_ExternalClockForCore = 0x4,

    /// Use Internal clock for the core of the module, or
    /// module is in master mode.
        NvRmClockConfig_InternalClockForCore = 0x8,
 
    /// Use inverted clock for the module. i.e the polarity of the clock used is
    /// inverted with respect to the source clock.
        NvRmClockConfig_InvertedClock = 0x10,
 
    /// Configure target module sub-clock
    /// - Target Display: configure Display and TVDAC
    /// - Target TVO: configure CVE and TVDAC only
    /// - Target VI: configure VI_SENSOR only
    /// - Target SPDIF: configure SPDIFIN only
        NvRmClockConfig_SubConfig = 0x20,
 
    /// Use MIPI PLL as Display clock source
        NvRmClockConfig_MipiSync = 0x40,
 
    /// Adjust Audio PLL to match requested I2S or SPDIF frequency
        NvRmClockConfig_AudioAdjust = 0x80,
 
    /// Disable TVDAC along with Display configuration
        NvRmClockConfig_DisableTvDAC = 0x100,
 
    /// Do not fail clock configuration request with specific target frequency
    /// above Hw limit - just configure clock at Hw limit. (Note that caller
    /// can request NvRmFreqMaximum to configure clock at Hw limit, regardless
    /// of this flag presence).
        NvRmClockConfig_QuietOverClock = 0x200,
    NvRmClockConfigFlags_Num,
    NvRmClockConfigFlags_Force32 = 0x7FFFFFFF
} NvRmClockConfigFlags;

/**
 * Defines SOC-wide clocks controlled by Dynamic Frequency Scaling (DFS)
 * that can be targeted by Starvation and Busy hints
 */

typedef enum
{

    /// Specifies CPU clock
        NvRmDfsClockId_Cpu = 1,

    /// Specifies AVP clock
        NvRmDfsClockId_Avp,

    /// Specifies System bus clock
        NvRmDfsClockId_System,

    /// Specifies AHB bus clock
        NvRmDfsClockId_Ahb,

    /// Specifies APB bus clock
        NvRmDfsClockId_Apb,

    /// Specifies video pipe clock
        NvRmDfsClockId_Vpipe,

    /// Specifies external memory controller clock
        NvRmDfsClockId_Emc,
    NvRmDfsClockId_Num,
    NvRmDfsClockId_Force32 = 0x7FFFFFFF
} NvRmDfsClockId;

/**
 * Defines DFS manager run states
 */

typedef enum
{

    /// DFS is in invalid, not initialized state
        NvRmDfsRunState_Invalid = 0,

    /// DFS is disabled / not supported (terminal state)
        NvRmDfsRunState_Disabled = 1,

    /// DFS is stopped - no automatic clock control. Starvation and Busy hints
    /// are recorded but have no affect.
        NvRmDfsRunState_Stopped,

    /// DFS is running in closed loop - full automatic control of SoC-wide
    /// clocks based on clock activity measuremnets. Starvation and Busy hints
    /// are functional as well.
        NvRmDfsRunState_ClosedLoop,

    /// DFS is running in closed loop with profiling (can not be set on non
    /// profiling build).
        NvRmDfsRunState_ProfiledLoop,
    NvRmDfsRunState_Num,
    NvRmDfsRunState_Force32 = 0x7FFFFFFF
} NvRmDfsRunState;

/**
 * Defines DFS profile targets
 */

typedef enum
{

    /// DFS algorithm within ISR
        NvRmDfsProfileId_Algorithm = 1,

    /// DFS Interrupt service - includes algorithm plus OS locking and
    ///  signaling calls; hence, includes blocking time (if any) as well
        NvRmDfsProfileId_Isr,

    /// DFS clock control time - includes PLL stabilazation time, OS locking
    /// and signalling calls; hence, includes blocking time (if any) as well 
        NvRmDfsProfileId_Control,
    NvRmDfsProfileId_Num,
    NvRmDfsProfileId_Force32 = 0x7FFFFFFF
} NvRmDfsProfileId;

/**
 * Defines voltage rails that are controlled in conjunction with dynamic
 * frequency scaling.
 */

typedef enum
{

    /// SoC core rail
        NvRmDfsVoltageRailId_Core = 1,

    /// Dedicated CPU rail
        NvRmDfsVoltageRailId_Cpu,
    NvRmDfsVoltageRailId_Num,
    NvRmDfsVoltageRailId_Force32 = 0x7FFFFFFF
} NvRmDfsVoltageRailId;

/**
 * Defines busy hint API synchronization modes.
 */

typedef enum
{

    /// Asynchronous mode (non-blocking API)
        NvRmDfsBusyHintSyncMode_Async = 1,

    /// Synchronous mode (blocking API)
        NvRmDfsBusyHintSyncMode_Sync,
    NvRmDfsBusyHintSyncMode_Num,
    NvRmDfsBusyHintSyncMode_Force32 = 0x7FFFFFFF
} NvRmDfsBusyHintSyncMode;

/**
 * Holds information on DFS clock domain utilization
 */

typedef struct NvRmDfsClockUsageRec
{

    /// Minimum clock domain frequency
        NvRmFreqKHz MinKHz;

    /// Maximum clock domain frequency
        NvRmFreqKHz MaxKHz;

    /// Low corner frequency - current low boundary for DFS control algorithm.
    /// Can be dynamically adjusted via APIs: NvRmDfsSetLowCorner() for all DFS
    /// domains, NvRmDfsSetCpuEnvelope() for CPU, and NvRmDfsSetEmcEnvelope()
    /// for EMC. When all DFS domains hit low corner, DFS stops waking up CPU
    ///  from low power state.
        NvRmFreqKHz LowCornerKHz;

    /// High corner frequency - current high boundary for DFS control algorithm.
    /// Can be dynamically adjusted via APIs: NvRmDfsSetCpuEnvelope() for Cpu,
    /// NvRmDfsSetEmcEnvelope() for Emc, and NvRmDfsSetAvHighCorner() for other
    //  DFS domains.
        NvRmFreqKHz HighCornerKHz;

    /// Current clock domain frequency
        NvRmFreqKHz CurrentKHz;

    /// Average frequency of domain *activity* (not average frequency). For
    /// domains that do not have activity monitors reported as unspecified.
        NvRmFreqKHz AverageKHz;
} NvRmDfsClockUsage;

/**
 * Holds information on DFS busy hint
 */

typedef struct NvRmDfsBusyHintRec
{

    /// Target clock domain ID
        NvRmDfsClockId ClockId;

    /// Requested boost duration in milliseconds
        NvU32 BoostDurationMs;

    /// Requested clock frequency level in kHz
        NvRmFreqKHz BoostKHz;

    /// Busy pulse mode indicator - if true, busy boost is completely removed
    /// after busy time has expired; if false, DFS will gradually lower domain
    /// frequency after busy boost.
        NvBool BusyAttribute;
} NvRmDfsBusyHint;

/**
 * Holds information on DFS starvation hint
 */

typedef struct NvRmDfsStarvationHintRec
{

    /// Target clock domain ID
        NvRmDfsClockId ClockId;

    /// The starvation indicator for the target domain
        NvBool Starving;
} NvRmDfsStarvationHint;

/**
 * The NVRM_POWER_CLIENT_TAG macro is used to convert ASCII 4-character codes
 * into the 32-bit tag that can be used to identify power manager clients for
 * logging purposes.
 */
#define NVRM_POWER_CLIENT_TAG(a,b,c,d) \
    ((NvU32) ((((a)&0xffUL)<<24UL) |  \
              (((b)&0xffUL)<<16UL) |  \
              (((c)&0xffUL)<< 8UL) |  \
              (((d)&0xffUL))))

/**
 * Registers RM power client.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param hEventSemaphore The client semaphore for power management event
 *  signaling. If null, no events will be signaled to the particular client.
 * @param pClientId A pointer to the storage that on entry contains client
 *  tag (optional), and on exit returns client ID, assigned by power manager.
 *
 * @retval NvSuccess if registration was successful.
 * @retval NvError_InsufficientMemory if failed to allocate memory for client
 *  registration.
 */

 NvError NvRmPowerRegister( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvOsSemaphoreHandle hEventSemaphore,
    NvU32 * pClientId );

/**
 * Unregisters RM power client. Power and clock for the modules enabled by this
 * client are disabled and any starvation or busy requests are cancelled during
 * the unregistration.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param ClientId The client ID obtained during registration.
 */

 void NvRmPowerUnRegister( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 ClientId );

/**
 * Gets last detected and not yet retrieved power management event.
 * Returns no outstanding event if no events has been detected since the
 * client registration or the last call to this function.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param ClientId The client ID obtained during registration.
 * @param pEvent Output storage pointer for power event identifier.
 *
 * @retval NvSuccess if event identifier was retrieved successfully.
 * @retval NvError_BadValue if specified client ID is not registered.
 */

 NvError NvRmPowerGetEvent( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 ClientId,
    NvRmPowerEvent * pEvent );

/**
 * Notifies RM about power management event. Provides an interface for
 * OS power manager to report system power events to RM.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param Event The event RM power manager is to be aware of.
 */

 void NvRmPowerEventNotify( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmPowerEvent Event );

/**
 * Gets combined RM clients power state.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param pState Output storage pointer for combined RM clients power state.
 *
 * @retval NvSuccess if power state was retrieved successfully.
 */

 NvError NvRmPowerGetState( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmPowerState * pState );

/**
 * Gets SoC primary oscillator/input frequency.
 *
 * @param hRmDeviceHandle The RM device handle.
 *
 * @retval Primary frequency in KHz.
 */

 NvRmFreqKHz NvRmPowerGetPrimaryFrequency( 
    NvRmDeviceHandle hRmDeviceHandle );

/**
 * Gets maximum frequency limit for the module clock.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param ModuleId The combined module ID and instance of the target module.
 * 
 * @retval Module clock maximum frequency in KHz.
 */

 NvRmFreqKHz NvRmPowerModuleGetMaxFrequency( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID ModuleId );

/**
 *  This API is used to set the clock configuration of the module clock.
 *  This API can also be used to query the existing configuration.
 * 
 *  Usage example:
 * 
 *  NvError Error;
 *  NvRmFreqKHz MyFreqKHz = 0;
 *  ModuleId = NVRM_MODULE_ID(NvRmModuleID_Uart, 0);
 * 
 *  // Get current frequency settings
 *  Error = NvRmPowerModuleClockConfig(RmHandle, ModuleId, ClientId,
 *                                      0, 0, NULL, 0, &MyFreqKHz, 0);
 * 
 * // Set target frequency within HW defined limits
 * MyFreqKHz = TARGET_FREQ;
 * Error = NvRmPowerModuleClockConfig(RmHandle, ModuleId, ClientId,
 *                                    NvRmFreqUnspecified, NvRmFreqUnspecified,
 *                                    &MyFreqKHz, 1, &MyFreqKHz);
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param ModuleId The combined module ID and instance of the target module.
 * @param ClientId The client ID obtained during registration.
 * @param MinFreq Requested minimum frequency for hardware module operation.
 *      If the value is NvRmFreqUnspecified, RM uses the the min freq that this
 *      module can operate. 
 *      If the value specified is more than the Hw minimum, passed value is used.
 *      If the value specified is less than the Hw minimum, it will be clipped to
 *      the HW minimum value.
 * @param MaxFreq Requested maximum frequency for hardware module operation.
 *      If the value is NvRmFreqUnspecified, RM uses the the max freq that this
 *      module can run.
 *      If the value specified is less than the Hw maximum, that value is used.
 *      If the value specified is more than the Hw limit, it will be clipped to
 *      the HW maximum.
 * @param PrefFreqList Pointer to a list of preferred frequencies, sorted in the
 *      decresing order of priority. Use NvRmFreqMaximum to request Hw maximum.
 * @param PrefFreqListCount Number of entries in the PrefFreqList array.
 * @param CurrentFreq Returns the current clock frequency of that module. NULL
 *      is a valid value for this parameter.
 * @param flags Module specific flags. Thse flags are valid only for some
 *      modules. See @NvRmClockConfigFlags
 * 
 * @retval NvSuccess if clock control request completed successfully.
 * @retval NvError_ModuleNotPresent if the module ID or instance is invalid.
 * @retval NvError_NotSupported if failed to configure requested frequency (e.g.,
 *  output frequency for possible divider settings is outside specified range).
 */

 NvError NvRmPowerModuleClockConfig( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID ModuleId,
    NvU32 ClientId,
    NvRmFreqKHz MinFreq,
    NvRmFreqKHz MaxFreq,
    const NvRmFreqKHz * PrefFreqList,
    NvU32 PrefFreqListCount,
    NvRmFreqKHz * CurrentFreq,
    NvU32 flags );

/**
 *  This API is used to enable and disable the module clock.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param ModuleId The combined module ID and instance of the target module.
 * @param ClientId The client ID obtained during registration.
 * @param Enable Enables/diables the module clock.
 * 
 * @retval NvSuccess if the module is enabled.
 * @retval NvError_ModuleNotPresent if the module ID or instance is invalid.
 */

 NvError NvRmPowerModuleClockControl( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID ModuleId,
    NvU32 ClientId,
    NvBool Enable );

/**
 * Request the voltage range for a hardware module. As power planes are shared
 * between different modules, in the majority of cases the RM will choose the
 * appropriate voltage, and module owners only need to enable or disable power
 * for a module. Enable request is always completed (i.e., voltage is applied
 * to the module) before this function returns. Disable request just means that
 * the client is ready for module power down. Actually the power may be removed
 * within the call or any time later, depending on other client needs and power
 * plane dependencies with other modules.
 * 
 * Assert encountered in debug mode if the module ID or instance is invalid.
 *
 * Usage example:
 * 
 * NvError Error;
 * ModuleId = NVRM_MODULE_ID(NvRmModuleID_Uart, 0);
 * 
 * // Enable module power
 * Error = NvRmPowerVoltageControl(RmHandle, ModuleId, ClientId,
 *          NvRmVoltsUnspecified, NvRmVoltsUnspecified,
 *          NULL, 0, NULL);
 * 
 * // Disable module power
 * Error = NvRmPowerVoltageControl(RmHandle, ModuleId, ClientId,
 *          NvRmVoltsOff, NvRmVoltsOff,
 *          NULL, 0, NULL);
 * 
 * @param hRmDeviceHandle The RM device handle
 * @param ModuleId The combined module ID and instance of the target module
 * @param ClientId The client ID obtained during registration
 * @param MinVolts Requested minimum voltage for hardware module operation
 * @param MaxVolts Requested maximum voltage for hardware module operation
 *  Set to NvRmVoltsUnspecified when enabling power for a module, or to
 *  NvRmVoltsOff when disabling.
 * @param PrefVoltageList Pointer to a list of preferred voltages, ordered from
 *  lowest to highest, and terminated with a voltage of NvRmVoltsUnspecified.
 *  This parameter is optional - ignored if null.
 * @param PrefVoltageListCount Number of entries in the PrefVoltageList array.
 * @param CurrentVolts Output storage pointer for resulting module voltage.
 *  NvRmVoltsUnspecified is returned if module power is On and was not cycled,
 *   since the last voltage request with the same ClientId and ModuleId;
 *  NvRmVoltsCycled is returned if module power is On but was powered down,
 *   since the last voltage request with the same ClientId and ModuleId;
 *  NvRmVoltsOff  is returned if module power is Off.
 *  This parameter is optional - ignored  if null.
 * 
 * @retval NvSuccess if voltage control request completed successfully.
 * @retval NvError_BadValue if specified client ID is not registered.
 * @retval NvError_InsufficientMemory if failed to allocate memory for
 *  voltage request.
 */

 NvError NvRmPowerVoltageControl( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID ModuleId,
    NvU32 ClientId,
    NvRmMilliVolts MinVolts,
    NvRmMilliVolts MaxVolts,
    const NvRmMilliVolts * PrefVoltageList,
    NvU32 PrefVoltageListCount,
    NvRmMilliVolts * CurrentVolts );

/**
 * Lists modules registered by power clients for voltage control.
 * 
 * @param pListSize Pointer to the list size. On entry specifies list size
 *  allocated by the caller, on exit - actual number of Ids returned. If
 *  entry size is 0, maximum list size is returned. 
 * @param pIdList Pointer to the list of combined module Id/Instance values
 *  to be filled in by this function. Ignored if input list size is 0.
 * @param pActiveList Pointer to the list of modules Active attributes
 *  to be filled in by this function. Ignored if input list size is 0.
 */                               

 void NvRmListPowerAwareModules( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 * pListSize,
    NvRmModuleID * pIdList,
    NvBool * pActiveList );

/**
 * Requests immediate frequency boost for SOC-wide clocks. In general, the RM
 * DFS manages SOC-wide clocks by measuring the average use of clock cycles,
 * and adjusting clock rates to minimize wasted clocks. It is preferable and
 * expected that modules consume clock cycles at a more-or-less constant rate.
 * Under some circumstances this will not be the case. For example, many cycles
 * may be consumed to prime a new media processing activity. If power client
 * anticipates such circumstances, it may sparingly use this API to alert the RM
 * that a temporary spike in clock usage is about to occur.
 *
 * Usage example:
 * 
 * // Busy hint for CPU clock
 * NvError Error;
 * Error = NvRmPowerBusyHint(RmHandle, NvRmDfsClockId_Cpu, ClientId,
 *          BoostDurationMs, BoostFreqKHz);
 *
 * Clients should not call this API in an attempt to micro-manage a particular
 * clock frequency as that is the responsibility of the RM.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param ClockId The DFS ID of the clock targeted by this hint.
 * @param ClientId The client ID obtained during registration.
 * @param BoostDurationMs The estimate of the boost duration in milliseconds.
 *  Use NV_WAIT_INFINITE to specify busy until canceled. Use 0 to request
 *  instantaneous spike in frequency and let DFS to scale down. 
 * @param BoostKHz The requirements for the boosted clock frequency in kHz.
 *  Use NvRmFreqMaximum to request maximum domain frequency. Use 0 to cancel
 *  all busy hints reported by the specified client for the specified domain.
 *
 * @retval NvSuccess if busy request completed successfully.
 * @retval NvError_BadValue if specified client ID is not registered.
 * @retval NvError_InsufficientMemory if failed to allocate memory for
 *  busy hint.
 */

 NvError NvRmPowerBusyHint( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmDfsClockId ClockId,
    NvU32 ClientId,
    NvU32 BoostDurationMs,
    NvRmFreqKHz BoostKHz );

/**
 * Requests immediate frequency boost for multiple SOC-wide clock domains. 
 * @sa NvRmPowerBusyHint() for detailed explanation of busy hint effects. 
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param ClientId The client ID obtained during registration.
 * @param pMultiHint Pointer to a list of busy hint records for
 *  targeted clocks.
 * @param NumHints Number of entries in pMultiHint array.
 * @param Mode Synchronization mode. In asynchronous mode this API returns to
 *  the caller after request is signaled to power manager (non-blocking call).
 *  In synchronous mode the API returns after busy hints are processed by power
 *  manager (blocking call).
 * 
 * @note It is recommended to use synchronous mode only when low frequency
 *  may result in functional failure. Otherwise, use asynchronous mode or
 *  NvRmPowerBusyHint API, which is always executed as non-blocking request.
 *  Synchronous mode must not be used by PMU transport.
 * 
 *
 * @retval NvSuccess if busy hint request completed successfully.
 * @retval NvError_BadValue if specified client ID is not registered.
 * @retval NvError_InsufficientMemory if failed to allocate memory for
 *  busy hints.
 */

 NvError NvRmPowerBusyHintMulti( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 ClientId,
    const NvRmDfsBusyHint * pMultiHint,
    NvU32 NumHints,
    NvRmDfsBusyHintSyncMode Mode );

/**
 * Request frequency increase for SOC-wide clock to avoid real-time starvation
 * conditions. Allows modules to contribute to the detection and avoidance of
 * clock starvation for DFS controlled clocks.
 * 
 * This API should be called to indicate starvation threat and also to cancel
 * request when a starvation condition has eased.
 * 
 * @note Although the RM DFS does its best to manage clocks without starving
 * the system for clock cycles, bursty clock usage can occasionally cause
 * short-term clock starvation. One solution is to leave a large enough clock
 * rate guard band such that any possible burst in clock usage will be absorbed.
 * This approach tends to waste clock cycles, and worsen power management.
 * 
 * By allowing power clients to participate in the avoidance of system clock
 * starvation situations, detection responsibility can be moved closer to the
 * hardware buffers and processors where starvation occurs, while leaving the
 * overall dynamic clocking policy to the RM. A typical client would be a module
 * that manages media processing and is able to determine when it is falling
 * behind by watching buffer levels or some other module-specific indicator. In
 * response to the starvation request the RM increases gradually the respective
 * clock frequency until the request vis cancelled by the client.
 * 
 * Usage example:
 * 
 * NvError Error;
 * 
 * // Request CPU clock frequency increase to avoid starvation
 * Error = NvRmPowerStarvationHint(
 *          RmHandle, NvRmDfsClockId_Cpu, ClientId, NV_TRUE);
 * 
 * // Cancel starvation request for CPU clock frequency
 * Error = NvRmPowerStarvationHint(
 *          RmHandle, NvRmDfsClockId_Cpu, ClientId, NV_FALSE);
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param ClockId The DFS ID of the clock targeted by this hint.
 * @param ClientId The client ID obtained during registration.
 * @param Starving The starvation indicator for the target module. If true,
 *  the client is requesting target frequency increase to avoid starvation
 *  If false, the indication is that the imminent starvation is no longer a
 *  concern for this particular client.
 * 
 * @retval NvSuccess if starvation request completed successfully.
 * @retval NvError_BadValue if specified client ID is not registered.
 * @retval NvError_InsufficientMemory if failed to allocate memory for
 *  starvation hint.
 */

 NvError NvRmPowerStarvationHint( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmDfsClockId ClockId,
    NvU32 ClientId,
    NvBool Starving );

/**
 * Request frequency increase for multiple SOC-wide clock domains to avoid
 * real-time starvation conditions.
 * @sa NvRmPowerStarvationHint() for detailed explanation of starvation hint
 * effects.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param ClientId The client ID obtained during registration.
 * @param pMultiHint Pointer to a list of starvation hint records for
 *  targeted clocks.
 * @param NumHints Number of entries in pMultiHint array.
 *
 * @retval NvSuccess if starvation hint request completed successfully.
 * @retval NvError_BadValue if specified client ID is not registered.
 * @retval NvError_InsufficientMemory if failed to allocate memory for
 *  starvation hints.
 */

 NvError NvRmPowerStarvationHintMulti( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 ClientId,
    const NvRmDfsStarvationHint * pMultiHint,
    NvU32 NumHints );

/**
 * Notifies the RM about DDK module activity.
 * 
 * @note This function lets DDK modules notify the RM about interesting system
 * activities. Not all modules will need to make this indication, typically only
 * modules involved in user input or output activities. However, with current
 * SOC power management architecture such activities will be detected by the OS
 * adaptation layer, not RM. This API is not removed, just in case, we will find
 * out that RM still need to participate in user activity detection. In general,
 * modules should call this interface sparingly, no more than once every few
 * seconds.
 * 
 * In current power management architecture user activity is handled by OS
 * (nor RM) power manager, and activity API is not used at all.
 * 
 * Assert encountered in debug mode if the module ID or instance is invalid.
 *
 * TODO: Remove this API?
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param ModuleId The combined module ID and instance of the target module.
 * @param ClientId The client ID obtained during registration.
 * @param ActivityDurationMs The duration of the module activity.
 * 
 * For cases when activity is a series of discontinuous events (keypresses, for
 * example), this parameter should simply be set to 1.
 * 
 * For lengthy, continuous activities, this parameter is set to the estimated
 * length of the activity in milliseconds. This can reduce the number of calls
 * made to this API.
 * 
 * A value of 0 in this parameter indicates that the module is not active and
 * can be used to signal the end of a previously estimated continuous activity.
 * 
 * @retval NvSuccess if clock control request completed successfully.
 */

 NvError NvRmPowerActivityHint( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID ModuleId,
    NvU32 ClientId,
    NvU32 ActivityDurationMs );

/**
 * Gets DFS run sate.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * 
 * @return Current DFS run state.
 */

 NvRmDfsRunState NvRmDfsGetState( 
    NvRmDeviceHandle hRmDeviceHandle );

/**
 * Gets information on DFS controlled clock utilization. If DFS is stopped
 * or disabled the average frequency is always equal to current frequency.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param ClockId The DFS ID of the clock targeted by this request.
 * @param pClockInfo Output storage pointer for clock utilization information.
 * 
 * @return NvSuccess if clock usage information is returned successfully.
 */

 NvError NvRmDfsGetClockUtilization( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmDfsClockId ClockId,
    NvRmDfsClockUsage * pClockUsage );

/**
 * Sets DFS run state. Allows to stop or re-start DFS as well as switch
 * between open and closed loop operations.
 * 
 * On transition to the DFS stopped state, the DFS clocks are just kept at
 * current frequencies. On transition to DFS run states, DFS sampling data
 * is re-initialized only if originally DFS was stopped. Transition between
 * running states has no additional effects, besides operation mode changes.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param NewDfsRunState The DFS run state to be set.
 * 
 * @retval NvSuccess if DFS state was set successfully.
 * @retval NvError_NotSupported if DFS was disabled initially, in attempt
 * to disable initially enabled DFS, or in attempt to run profiled loop
 * on non profiling build.
 */

 NvError NvRmDfsSetState( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmDfsRunState NewDfsRunState );

/**
 * Sets DFS low corner frequencies - low boundaries for DFS clocks when DFS.
 * is running. If all DFS domains hit low corner, DFS will no longer wake
 * CPU from low power state.  
 *
 * @note When CPU envelope is set via NvRmDfsSetCpuEnvelope() API the CPU
 *  low corner boundary can not be changed by this function.
 * @note When EMC envelope is set via NvRmDfsSetEmcEnvelope() API the EMC
 *  low corner boundary can not be changed by this function.
 * 
 * Usage example:
 * 
 * NvError Error;
 * NvRmFreqKHz LowCorner[NvRmDfsClockId_Num];
 * 
 * // Fill in low corner array
 * LowCorner[NvRmDfsClockId_Cpu] = NvRmFreqUnspecified;
 * LowCorner[NvRmDfsClockId_Avp] = ... ;
 * LowCorner[NvRmDfsClockId_System] = ...;
 * LowCorner[NvRmDfsClockId_Ahb] = ...;
 * LowCorner[NvRmDfsClockId_Apb] = ...;
 * LowCorner[NvRmDfsClockId_Vpipe] = ...;
 * LowCorner[NvRmDfsClockId_Emc] = ...;
 * 
 * // Set new low corner for domains other than CPU, and preserve CPU boundary
 * Error = NvRmDfsSetLowCorner(RmHandle, NvRmDfsClockId_Num, LowCorner);
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param DfsFreqListCount Number of entries in the pDfsLowFreqList array.
 *  Must be always equal to NvRmDfsClockId_Num.
 * @param pDfsLowFreqList Pointer to a list of low corner frequencies, ordered
 *  according to NvRmDfsClockId enumeration. If the list entry is set to
 *  NvRmFreqUnspecified, the respective low corner boundary is not modified.
 * 
 * @retval NvSuccess if low corner frequencies were updated successfully.
 * @retval NvError_NotSupported if DFS is disabled.
 */

 NvError NvRmDfsSetLowCorner( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 DfsFreqListCount,
    const NvRmFreqKHz * pDfsLowFreqList );

/**
 * Sets DFS target frequencies. If DFS is stopped clocks for the DFS domains
 * will be targeted with the specified frequencies. In any other DFS state
 * this function has no effect.
 *
 * Usage example:
 * 
 * NvError Error;
 * NvRmFreqKHz Target[NvRmDfsClockId_Num];
 * 
 * // Fill in target frequencies array
 * Target[NvRmDfsClockId_Cpu] = ... ;
 * Target[NvRmDfsClockId_Avp] = ... ;
 * Target[NvRmDfsClockId_System] = ...;
 * Target[NvRmDfsClockId_Ahb] = ...;
 * Target[NvRmDfsClockId_Apb] = ...;
 * Target[NvRmDfsClockId_Vpipe] = ...;
 * Target[NvRmDfsClockId_Emc] = ...;
 * 
 * // Set new target
 * Error = NvRmDfsSetTarget(RmHandle, NvRmDfsClockId_Num, Target);
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param DfsFreqListCount Number of entries in the pDfsTargetFreqList array.
 *  Must be always equal to NvRmDfsClockId_Num.
 * @param pDfsTargetFreqList Pointer to a list of target frequencies, ordered
 *  according to NvRmDfsClockId enumeration. If the list entry is set to
 *  NvRmFreqUnspecified, the current domain frequency is used as a target.
 * 
 * @retval NvSuccess if target frequencies were updated successfully.
 * @retval NvError_NotSupported if DFS is not stopped (disabled, or running).
 */

 NvError NvRmDfsSetTarget( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 DfsFreqListCount,
    const NvRmFreqKHz * pDfsTargetFreqList );

/**
 * Sets DFS high and low boundaries for CPU domain clock frequency.
 * 
 * Usage example:
 *
 * NvError Error;
 * 
 * // Set CPU envelope boundaries to LowKHz : HighKHz
 * Error = NvRmDfsSetCpuEnvelope(RmHandle, LowKHz, HighKHz);
 * 
 * // Change CPU envelope high boundary to HighKHz
 * Error = NvRmDfsSetCpuEnvelope(RmHandle, NvRmFreqUnspecified, HighKHz);
 *
 * // Release CPU envelope back to HW limits
 * Error = NvRmDfsSetCpuEnvelope(RmHandle, 0, NvRmFreqMaximum);
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param DfsCpuEnvelopeLowKHz Requested low boundary in kHz.
 * @param DfsCpuEnvelopeHighKHz Requested high limit in kHz.
 * 
 * Envelope parameters are clipped to the HW defined CPU domain range.
 * If envelope parameter is set to NvRmFreqUnspecified, the respective
 * CPU boundary is not modified, unless it violates the new setting for
 * the other boundary; in the latter case both boundaries are set to the
 * new specified value.
 * 
 * @retval NvSuccess if DFS envelope for for CPU domain was updated
 *  successfully.
 * @retval NvError_BadValue if reversed boundaries are specified.
 * @retval NvError_NotSupported if DFS is disabled.
 */

 NvError NvRmDfsSetCpuEnvelope( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmFreqKHz DfsCpuLowCornerKHz,
    NvRmFreqKHz DfsCpuHighCornerKHz );

/**
 * Sets DFS high and low boundaries for EMC domain clock frequency.
 * 
 * Usage example:
 *
 * NvError Error;
 * 
 * // Set EMC envelope boundaries to LowKHz : HighKHz
 * Error = NvRmDfsSetEmcEnvelope(RmHandle, LowKHz, HighKHz);
 * 
 * // Change EMC envelope high boundary to HighKHz
 * Error = NvRmDfsSetEmcEnvelope(RmHandle, NvRmFreqUnspecified, HighKHz);
 *
 * // Release EMC envelope back to HW limits
 * Error = NvRmDfsSetEmcEnvelope(RmHandle, 0, NvRmFreqMaximum);
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param DfsEmcEnvelopeLowKHz Requested low boundary in kHz.
 * @param DfsEmcEnvelopeHighKHz Requested high limit in kHz.
 * 
 * Envelope parameters are clipped to the ODM defined EMC configurations
 * within HW defined EMC domain range. If envelope parameter is set to
 * NvRmFreqUnspecified, the respective EMC boundary is not modified, unless
 * it violates the new setting for the other boundary; in the latter case
 * both boundaries are set to the new specified value.
 * 
 * @retval NvSuccess if DFS envelope for for EMC domain was updated
 *  successfully.
 * @retval NvError_BadValue if reversed boundaries are specified.
 * @retval NvError_NotSupported if DFS is disabled.
 */

 NvError NvRmDfsSetEmcEnvelope( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmFreqKHz DfsEmcLowCornerKHz,
    NvRmFreqKHz DfsEmcHighCornerKHz );

/**
 * Sets DFS high boundaries for CPU and EMC.
 * 
 * @note When either CPU or EMC envelope is set via NvRmDfsSetXxxEnvelope()
 * API, neither CPU nor EMC boundary is changed by this function.
 * 
 * Usage example:
 *
 * NvError Error;
 * 
 * // Set CPU subsystem clock limit to CpuHighKHz and Emc clock limit
 * // to EmcHighKHz
 * Error = NvRmDfsSetCpuEmcHighCorner(RmHandle, CpuHighKHz, EmcHighKHz);
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param DfsCpuHighKHz Requested high boundary in kHz for CPU.
 * @param DfsEmcHighKHz Requested high limit in kHz for EMC.
 * 
 * Requested parameters are clipped to the respective HW defined domain
 * ranges, as well as to ODM defined EMC configurations. If any parameter
 * is set to NvRmFreqUnspecified, the respective boundary is not modified.
 * 
 * @retval NvSuccess if high corner for AV subsystem was updated successfully.
 * @retval NvError_NotSupported if DFS is disabled.
 */

 NvError NvRmDfsSetCpuEmcHighCorner( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmFreqKHz DfsCpuHighKHz,
    NvRmFreqKHz DfsEmcHighKHz );

/**
 * Sets DFS high boundaries for AV subsystem clocks.
 * 
 * Usage example:
 *
 * NvError Error;
 * 
 * // Set AVP clock limit to AvpHighKHz, Vde clock limit to VpipeHighKHz,
 * // and preserve System bus clock limit provided it is above requested
 * // AVP and Vpipe levels.
 * Error = NvRmDfsSetAvHighCorner(
 *          RmHandle, NvRmFreqUnspecified, AvpHighKHz, VpipeHighKHz);
 * 
 *@note System bus clock limit must be always above AvpHighKHz, and above
 * VpipeHighKHz. Therefore it may be adjusted up, as a result of this call,
 * even though, it is marked unspecified.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param DfsSysHighKHz Requested high boundary in kHz for System bus.
 * @param DfsAvpHighKHz Requested high boundary in kHz for AVP.
 * @param DfsVdeHighCornerKHz Requested high limit in kHz for Vde pipe.
 * 
 * Requested parameter is clipped to the respective HW defined domain
 * range. If  parameter is set to NvRmFreqUnspecified, the respective
 * boundary is not modified.
 * 
 * @retval NvSuccess if high corner for AV subsystem was updated successfully.
 * @retval NvError_NotSupported if DFS is disabled.
 */

 NvError NvRmDfsSetAvHighCorner( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmFreqKHz DfsSystemHighKHz,
    NvRmFreqKHz DfsAvpHighKHz,
    NvRmFreqKHz DfsVpipeHighKHz );

/**
 * Gets DFS profiling information.
 * 
 * DFS profiling starts/re-starts every time NvRmDfsRunState_ProfiledLoop
 * state is set via NvRmDfsSetState(). DFS profiling stops when any other
 * sate is set.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param DfsProfileCount Number of DFS profiles. Must be always equal to
 *  NvRmDfsProfileId_Num.
 * @param pSamplesNoList Output storage pointer to an array of sample counts
 *  for each profile target ordered according to NvRmDfsProfileId enumeration.
 * @param pProfileTimeUsList Output storage pointer to an array of cummulative
 *  execution time in microseconds for each profile target ordered according
 *  to NvRmDfsProfileId enumeration.
 * @param pDfsPeriodUs Output storage pointer for average DFS sample
 *  period in microseconds.
 * 
 * @retval NvSuccess if profile information is returned successfully.
 * @retval NvError_NotSupported if DFS is not ruuning in profiled loop.
 */

 NvError NvRmDfsGetProfileData( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 DfsProfileCount,
    NvU32 * pSamplesNoList,
    NvU32 * pProfileTimeUsList,
    NvU32 * pDfsPeriodUs );

/**
 * Starts/Re-starts NV DFS logging.
 * 
 * @param hRmDeviceHandle The RM device handle.
 */

 void NvRmDfsLogStart( 
    NvRmDeviceHandle hRmDeviceHandle );

/**
 * Stops DFS logging and gets cumulative mean values of DFS domains frequencies
 *  over logging time.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param LogMeanFreqListCount Number of entries in the pLogMeanFreqList array.
 *  Must be always equal to NvRmDfsClockId_Num.
 * @param pLogMeanFreqList Pointer to a list filled with mean values of DFS
 *  frequencies, ordered according to NvRmDfsClockId enumeration.
 * @param pLogLp2TimeMs Pointer to a variable filled with cumulative time spent
 *  in LP2 in milliseconds.
 * @param pLogLp2Entries Pointer to a variable filled with cumulative number of
 *  LP2 mode entries.
 * 
 * @retval NvSuccess if mean values are returned successfully.
 * @retval NvError_NotSupported if DFS is disabled.
 */

 NvError NvRmDfsLogGetMeanFrequencies( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 LogMeanFreqListCount,
    NvRmFreqKHz * pLogMeanFreqList,
    NvU32 * pLogLp2TimeMs,
    NvU32 * pLogLp2Entries );

/**
 * Gets specified entry of the detailed DFS activity log.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param EntryIndex Log entrty index.
 * @param LogDomainsCount The size of activity arrays.
 *  Must be always equal to NvRmDfsClockId_Num.
 * @param pIntervalMs Pointer to a variable filled with sample interval time
 *  in milliseconds.
 * @param pLp2TimeMs Pointer to a variable filled with time spent in LP2
 *  in milliseconds.
 * @param pActiveCyclesList Pointer to a list filled with domain active cycles
 *  within sample interval.
 * @param pAveragesList Pointer to a list filled with average domain activity
 *  over DFS moving window.
 * @param pFrequenciesList Pointer to a list filled with instantaneous domains
 *  frequencies.
 *  All lists are ordered according to NvRmDfsClockId enumeration.
 * 
 * @retval NvSuccess if log entry is retrieved successfully.
 * @retval NvError_InvalidAddress if requetsed entry is empty.
 * @retval NvError_NotSupported if DFS is disabled, or detailed logging
 *  is not supported.
 */

 NvError NvRmDfsLogActivityGetEntry( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 EntryIndex,
    NvU32 LogDomainsCount,
    NvU32 * pIntervalMs,
    NvU32 * pLp2TimeMs,
    NvU32 * pActiveCyclesList,
    NvRmFreqKHz * pAveragesList,
    NvRmFreqKHz * pFrequenciesList );

/**
 * Gets specified entry of the detailed DFS starvation hints log.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param EntryIndex Log entrty index.
 * @param pSampleIndex Pointer to a variable filled with sample interval
 *  index in the activity log when this hint is associated with.
 * @param pStarvationHint Pointer to a variable filled with starvation
 *  hint record.
 * 
 * @retval NvSuccess if next entry is retrieved successfully.
 * @retval NvError_InvalidAddress if requetsed entry is empty.
 * @retval NvError_NotSupported if DFS is disabled, or detailed logging
 *  is not supported.
 */

 NvError NvRmDfsLogStarvationGetEntry( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 EntryIndex,
    NvU32 * pSampleIndex,
    NvU32 * pClientId,
    NvU32 * pClientTag,
    NvRmDfsStarvationHint * pStarvationHint );

/**
 * Gets specified entry of the detailed DFS busy hints log.
 *
 * @param hRmDeviceHandle The RM device handle.
 * @param EntryIndex Log entrty index.
 * @param pSampleIndex Pointer to a variable filled with sample interval
 *  index in the activity log when this hint is associated with.
 * @param pBusyHint Pointer to a variable filled with busy
 *  hint record.
 * 
 * @retval NvSuccess if next entry is retrieved successfully.
 * @retval NvError_InvalidAddress if requetsed entry is empty.
 * @retval NvError_NotSupported if DFS is disabled, or detailed logging
 *  is not supported.
 */

 NvError NvRmDfsLogBusyGetEntry( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 EntryIndex,
    NvU32 * pSampleIndex,
    NvU32 * pClientId,
    NvU32 * pClientTag,
    NvRmDfsBusyHint * pBusyHint );

/**
 * Gets low threshold and present voltage on the given rail.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param RailId The targeted voltage rail ID.
 * @param pLowMv Output storage pointer for low voltage threshold (in 
 *  millivolt). NvRmVoltsUnspecified is returned if targeted rail does
 *  not exist on SoC.
 * @param pPresentMv Output storage pointer for present rail voltage (in 
 *  millivolt). NvRmVoltsUnspecified is returned if targeted rail does
 *  not exist on SoC.
 */

 void NvRmDfsGetLowVoltageThreshold( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmDfsVoltageRailId RailId,
    NvRmMilliVolts * pLowMv,
    NvRmMilliVolts * pPresentMv );

/**
 * Sets low threshold for the given rail. The actual rail voltage is scaled
 *  to match SoC clock frequencies, but not below the specified threshold.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param RailId The targeted voltage rail ID.
 * @param LowMv Low voltage threshold (in millivolts) for the targeted rail.
 *  Ignored if targeted rail does not exist on SoC.
 */

 void NvRmDfsSetLowVoltageThreshold( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmDfsVoltageRailId RailId,
    NvRmMilliVolts LowMv );

/**
 * Notifies RM Kernel about entering Suspend state.
 * 
 * @param hRmDeviceHandle The RM device handle.
 *
 * @retval NvSuccess if notifying RM entering Suspend state successfully.
 */

 NvError NvRmKernelPowerSuspend( 
    NvRmDeviceHandle hRmDeviceHandle );

/**
 * Notifies RM kernel about entering Resume state.
 * 
 * @param hRmDeviceHandle The RM device handle.
 *
 * @retval NvSuccess if notifying RM entering Resume state successfully.
 */

 NvError NvRmKernelPowerResume( 
    NvRmDeviceHandle hRmDeviceHandle );

/** @} */

#if defined(__cplusplus)
}
#endif

#endif
