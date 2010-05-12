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

#ifndef INCLUDED_NVRM_POWER_PRIVATE_H
#define INCLUDED_NVRM_POWER_PRIVATE_H

#include "nvrm_power.h"
#include "nvodm_query.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

// Power detect cell stabilization delay
#define NVRM_PWR_DET_DELAY_US (3)

// Minimum DFS clock domain busy time and busy hints list purge time
#define NVRM_DFS_BUSY_MIN_MS (10)
#define NVRM_DFS_BUSY_PURGE_MS (500)

// Temporary definitions for AP20 bring up
#define NVRM_POWER_AP20_BRINGUP_RETURN(hRm, cond) \
    if (((hRm)->ChipId.Id == 0x20) && ((cond))) \
        return

/**
 * Defines the DFS status flags used by OS kernel to configure SoC for
 * low power state (multiple flags can be OR'ed).
 */
typedef enum
{
    // Pause DFS during low power state
    NvRmDfsStatusFlags_Pause = 0x01,

    // Stop PLL during low power state
    NvRmDfsStatusFlags_StopPllM0 = 0x02,
    NvRmDfsStatusFlags_StopPllC0 = 0x04,
    NvRmDfsStatusFlags_StopPllP0 = 0x08,
    NvRmDfsStatusFlags_StopPllA0 = 0x10,
    NvRmDfsStatusFlags_StopPllD0 = 0x20,
    NvRmDfsStatusFlags_StopPllU0 = 0x40,
    NvRmDfsStatusFlags_StopPllX0 = 0x80,

    NvRmDfsStatusFlags_Force32 = 0x7FFFFFFF
} NvRmDfsStatusFlags;

// Defines maximum number of CPUs (must be power of 2)
#define NVRM_MAX_NUM_CPU_LOG2 (8)

/**
 * Defines RM power manager requests to OS kernel
 */
typedef enum
{
    NvRmPmRequest_None = 0,

    // The CPU number is interpreted based on the request flag it is
    // combined (ORed) with
    NvRmPmRequest_CpuNumMask = (0x1 << NVRM_MAX_NUM_CPU_LOG2) - 1,

    // Request to abort RM power manager (CPU number is ignored)
    NvRmPmRequest_ExitFlag,

    // Request to turn On/Off CPU (CPU number specifies target
    // CPU within current CPU cluster)
    NvRmPmRequest_CpuOnFlag = NvRmPmRequest_ExitFlag << 1,
    NvRmPmRequest_CpuOffFlag = NvRmPmRequest_CpuOnFlag << 1,

    // Request to switch between CPU clusters (CPU number specifies target
    // CPU cluster)
    NvRmPmRequest_CpuClusterSwitchFlag = NvRmPmRequest_CpuOffFlag << 1,

    NvRmPmRequest_Force32 = 0x7FFFFFFF
} NvRmPmRequest;

/**
 * NVRM PM function called within OS shim high priority thread
 */
NvRmPmRequest NvRmPrivPmThread(void);

/**
 * Sets combined RM clients power state in the storage shared with OS
 * adaptation layer (OAL). While the system is running RM power manger
 * calls this function to specify idle or active state based on client
 * requests. On entry to system low power state OAL calls this function
 * to store the respective LPx id.
 *
 * @param hRmDeviceHandle The RM device handle
 * @param RmState The overall power state to be set
 */
void
NvRmPrivPowerSetState(
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmPowerState RmState);

/**
 * Reads combined RM clients power state from the storage shared with OS
 * adaptation layer (OAL). While the system is running both RM and OAL may
 * call this function to read the power state. On exit from the system low
 * power state OAL uses this function to find out which LPx state is exited.
 *
 * @param hRmDeviceHandle The RM device handle
 *
 * @return RM power state
 */
NvRmPowerState
NvRmPrivPowerGetState(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Updates DFS pause flag in the storage shared by RM and NV boot loader
 *
 * @param hRmDeviceHandle The RM device handle
 * @param Pause If NV_TRUE, set DFS pause flag,
 *  if NV_FALSE, clear DFS pause flag
 *
 */
void
NvRmPrivUpdateDfsPauseFlag(
    NvRmDeviceHandle hRmDeviceHandle,
    NvBool Pause);

/**
 * Reads DFS status flags from the storage shared by RM and NV boot loader.
 *
 * @param hRmDeviceHandle The RM device handle
 *
 * @return DFS status flags as defined @NvRmDfsStatusFlags
 */
NvU32
NvRmPrivGetDfsFlags(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Sets download transport in the storage shared by RM and NV boot loader
 *
 * @param hRmDeviceHandle The RM device handle
 * @param Transport current download transport (NvOdmDownloadTransport_None
 *  if no transport or it is not active)
 */
void
NvRmPrivSetDownloadTransport(
    NvRmDeviceHandle hRmDeviceHandle,
    NvOdmDownloadTransport Transport);

/**
 * Reads download transport from the storage shared by RM and NV boot loader.
 *
 * @param hRmDeviceHandle The RM device handle
 *
 * @return current download transport (NvOdmDownloadTransport_None
 *  if no transport or it is not active)
 */
NvOdmDownloadTransport
NvRmPrivGetDownloadTransport(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Save LP2 time in the storage shared by RM and NV boot loader.
 *
 * @param hRmDeviceHandle The RM device handle
 * @param TimeUS Time in microseconds CPU was in LP2 state (power gated)
 */
void
NvRmPrivSetLp2TimeUS(
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 TimeUS);

/**
 * Reads LP2 time from the storage shared by RM and NV boot loader.
 *
 * @param hRmDeviceHandle The RM device handle
 *
 * @return Time in microseconds CPU was in LP2 state (power gated)
 */
NvU32
NvRmPrivGetLp2TimeUS(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Initializes RM access to the storage shared by RM and NV boot loader
 *
 * @param hRmDeviceHandle The RM device handle
 *
 * @return NvSuccess if initialization completed successfully
 * or one of common error codes on failure
 */
NvError NvRmPrivOalIntfInit(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Deinitializes RM access to the storage shared by RM and NV boot loader
 *
 * @param hRmDeviceHandle The RM device handle
 */
void NvRmPrivOalIntfDeinit(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Initializes RM DFS manager
 *
 * @param hRmDeviceHandle The RM device handle
 *
 * @return NvSuccess if initialization completed successfully
 * or one of common error codes on failure
 */
NvError NvRmPrivDfsInit(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Deinitializes RM DFS manager
 *
 * @param hRmDeviceHandle The RM device handle
 */
void NvRmPrivDfsDeinit(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Initializes RM DTT manager
 *
 * @param hRmDeviceHandle The RM device handle
 */
void NvRmPrivDttInit(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Deinitializes RM DTT manager
 */
void NvRmPrivDttDeinit(void);

/**
 * Initializes RM power manager
 *
 * @param hRmDeviceHandle The RM device handle
 *
 * @return NvSuccess if initialization completed successfully
 * or one of common error codes on failure
 */
NvError
NvRmPrivPowerInit(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Deinitializes RM power manager
 *
 * @param hRmDeviceHandle The RM device handle
 */
void
NvRmPrivPowerDeinit(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Initializes IO power rails control
 *
 * @param hRmDeviceHandle The RM device handle
 */
void NvRmPrivIoPowerControlInit(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Starts IO power rails level detection
 *
 * @param hRmDeviceHandle The RM device handle
 * @param PwrDetMask The bit mask of power detection cells to be activated
 */
void NvRmPrivIoPowerDetectStart(
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 PwrDetMask);

/**
 * Resets enabled power detect cells (chip-specific).
 *
 * @param hRmDeviceHandle The RM device handle
 */
void NvRmPrivAp15IoPowerDetectReset(NvRmDeviceHandle hRmDeviceHandle);
void NvRmPrivAp20IoPowerDetectReset(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Latches the results of IO power rails level detection
 *
 * @param hRmDeviceHandle The RM device handle
 */
void NvRmPrivIoPowerDetectLatch(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Enables/Disables IO pads on specified power rails
 *
 * @param hRmDeviceHandle The RM device handle
 * @param NoIoPwrMask Bit mask of affected power rails
 * @param Enable Set NV_TRUE to enable IO pads, or NV_FALSE to disable.
 */
void NvRmPrivIoPowerControl(
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 NoIoPwrMask,
    NvBool Enable);

/**
 * Configures SoC power rail controls for the upcoming PMU voltage transition.
 *
 * @note Should be called just before PMU rail On/Off, or Off/On transition.
 *  Should not be called if rail voltage level is changing within On range.
 *
 * @param hDevice The Rm device handle.
 * @param PmuRailAddress PMU address (id) for targeted power rail.
 * @param Enable Set NV_TRUE if target voltage is about to be turned On, or
 *  NV_FALSE if target voltage is about to be turned Off.
 * @param pIoPwrDetectMask A pointer to a variable filled with the bit mask
 *  of activated IO power detection cells to be latched by the caller after
 *  Off/On transition (set to 0 for On/Off transition).
 * @param pNoIoPwrMask A pointer to a variable filled with the bit mask of IO
 *  power pads to be enabled by the caller after Off/On transition (set to 0
 *  for On/Off transition).
 */
void
NvRmPrivSetSocRailPowerState(
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 PmuRailAddress,
    NvBool Enable,
    NvU32* pIoPwrDetectMask,
    NvU32* pNoIoPwrMask);

/**
 * Initializes core SoC power rail.
 *
 * @param hDevice The Rm device handle.
 */
void NvRmPrivCoreVoltageInit(NvRmDeviceHandle hRmDevice);

/**
 * Request nominal core (and rtc) voltage.
 *
 * @param hRmDeviceHandle The RM device handle
 */
void
NvRmPrivSetNominalCoreVoltage(NvRmDeviceHandle hRmDevice);

/**
 * Initializes power group control table (chip-specific)
 * 
 * @param pPowerGroupIdsTable 
 * @param pPowerGroupIdsTable A pointer to a pointer which this function sets
 *  to the chip specific map between power group number and power gate ID. 
 * @param pPowerGroupIdsTableSize A pointer to a variable which this function
 *  sets to the power group IDs table size.
 * 
 */
void
NvRmPrivAp15PowerGroupTableInit(
    const NvU32** pPowerGroupIdsTable,
    NvU32* pPowerGroupIdsTableSize);

void
NvRmPrivAp20PowerGroupTableInit(
    const NvU32** pPowerGroupIdsTable,
    NvU32* pPowerGroupIdsTableSize);

/**
 * Initializes power group control.
 * 
 * @param hRmDeviceHandle The RM device handle
 */
void NvRmPrivPowerGroupControlInit(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Enables/disables power for the specified power group
 *
 * @param hRmDeviceHandle The RM device handle
 * @param PowerGroup targeted power group
 * @param Enable If NV_TRUE, enable power to the specified power group,
 *  if NV_FALSE, disable power (power gate) the specified power group
 */
void
NvRmPrivPowerGroupControl(
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 PowerGroup,
    NvBool Enable);

/**
 * Retrieves given power group voltage
 *
 * @param hRmDeviceHandle The RM device handle
 * @param PowerGroup targeted power group
 *
 * @return NvRmVoltsUnspecified if power group is On,
 *  and NvRmVoltsOff if it is power gated
 */
NvRmMilliVolts
NvRmPrivPowerGroupGetVoltage(
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 PowerGroup);

/**
 * Controls power state and clamping for PCIEXCLK/PLLE (chip-specific).
 *
 * @param hRmDevice The RM device handle.
 * @param Enable If NV_TRUE, power up PCIEXCLK and remove clamps,
 *  if NV_FALSE, power down PCIEXCLK  and set clamps.
 */
void
NvRmPrivAp20PowerPcieXclkControl(
    NvRmDeviceHandle hRmDevice,
    NvBool Enable);

/**
 * Verifies if the specified DFS clock domain is starving.
 *
 * @param ClockId The DFS ID of the clock domain to be checked.
 *
 * @retval NV_TRUE if domain is starving
 * @retval NV_FALSE if domain is not starving
 */
NvBool NvRmPrivDfsIsStarving(NvRmDfsClockId ClockId);

/**
 * Gets current busy boost frequency and pulse mode requested for the
 *  specified DFS clock domain.
 *
 * @param ClockId The DFS ID of the targeted clock domain.
 * @param pBusyKHz A pointer to a variable filled with boost frequency in kHz.
 * @param pBusyKHz A pointer to a variable filled with pulse mode indicator.
 * @param pBusyExpireMs A pointer to a variable filled with busy boost
 *  expiration interval in ms.
 */
void NvRmPrivDfsGetBusyHint(
    NvRmDfsClockId ClockId,
    NvRmFreqKHz* pBusyKHz,
    NvBool* pBusyPulseMode,
    NvU32* pBusyExpireMs);

/**
 * Gets maximum frequency for the specified DFS clock domain.
 *
 * @param ClockId The DFS ID of the targeted clock domain.
 *
 * @return Maximum domain frequency in kHz
 */
NvRmFreqKHz NvRmPrivDfsGetMaxKHz(NvRmDfsClockId ClockId);

/**
 * Gets minimum frequency for the specified DFS clock domain.
 *
 * @param ClockId The DFS ID of the targeted clock domain.
 *
 * @return Minimum domain frequency in kHz
 */
NvRmFreqKHz NvRmPrivDfsGetMinKHz(NvRmDfsClockId ClockId);

/**
 * Gets current frequency for the specified DFS clock domain.
 *
 * @param ClockId The DFS ID of the targeted clock domain.
 *
 * @return Current domain frequency in kHz
 */
NvRmFreqKHz NvRmPrivDfsGetCurrentKHz(NvRmDfsClockId ClockId);

/**
 * Signals DFS clock control thread
 * 
 * @param Mode Synchronization mode. In synchronous mode this function returns
 *  to the caller after DFS clock control procedure is executed (blocking call).
 *  In asynchronous mode returns immediately after control thread is signaled.
 */
void NvRmPrivDfsSignal(NvRmDfsBusyHintSyncMode Mode);

/**
 * Synchronize DFS samplers with current clock frequencies
 */
void NvRmPrivDfsResync(void);

/**
 * Gets DFS ready for low power state entry.
 *
 * @param state Target low power state.
 *
 */
void NvRmPrivDfsSuspend(NvOdmSocPowerState state);

/**
 * Restore clock sources after exit from low power state.
 *
 * @param hRmDevice The RM device handle.
 */
void
NvRmPrivClocksResume(NvRmDeviceHandle hRmDevice);


/**
 * Initializes DVS settings
 */
void NvRmPrivDvsInit(void);

/**
 * Scales core voltage according to DFS controlled clock frequencies.
 *
 * @param BeforeFreqChange Indicates whether this function is called
 *  before (NV_TRUE) or after (NV_FALSE) frequency change.
 * @param CpuMv Core voltage in mV required to run CPU at clock source
 *  frequency selected by DFS.
 * @param SystemMv Core voltage in mV required to run AVP/System at clock
 *  source frequency selected by DFS.
 * @param EmcMv Core voltage in mV required to run EMC/DDR at clock source
 *  frequency selected by DFS.
 */
void NvRmPrivVoltageScale(
    NvBool BeforeFreqChange,
    NvRmMilliVolts CpuMv,
    NvRmMilliVolts SystemMv,
    NvRmMilliVolts EmcMv);

/**
 * Requests core voltage update.
 *
 * @param TargetMv Requested core voltage level in mV.
 */
void NvRmPrivDvsRequest(NvRmMilliVolts TargetMv);

/**
 * Gets low threshold and present voltage on the given rail.
 *
 * @param RailId The targeted voltage rail ID.
 * @param pLowMv Output storage pointer for low voltage threshold (in
 *  millivolt).
 * @param pPresentMv Output storage pointer for present rail voltage (in
 *  millivolt). This parameter is optional, set to NULL if only low
 *  threshold is to be retrieved.
 *
 *  NvRmVoltsUnspecified is returned if targeted rail does not exist on SoC.
 */
void
NvRmPrivGetLowVoltageThreshold(
    NvRmDfsVoltageRailId RailId,
    NvRmMilliVolts* pLowMv,
    NvRmMilliVolts* pPresentMv);

/**
 * Outputs debug messages for starvation hints sent by the specified client.
 *
 * @param ClientId The client ID assigned by the RM power manager.
 * @param ClientTag The client tag reported to the RM power manager.
 * @param pMultiHint Pointer to a list of starvation hints sent by the client.
 * @param NumHints Number of entries in the pMultiHint list.
 *
 */
void NvRmPrivStarvationHintPrintf(
    NvU32 ClientId,
    NvU32 ClientTag,
    const NvRmDfsStarvationHint* pMultiHint,
    NvU32 NumHints);

/**
 * Outputs debug messages for busy hints sent by the specified client.
 *
 * @param ClientId The client ID assigned by the RM power manager.
 * @param ClientTag The client tag reported to the RM power manager.
 * @param pMultiHint Pointer to a list of busy hints sent by the client.
 * @param NumHints Number of entries in the pMultiHint list.
 *
 */
void NvRmPrivBusyHintPrintf(
    NvU32 ClientId,
    NvU32 ClientTag,
    const NvRmDfsBusyHint* pMultiHint,
    NvU32 NumHints);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif // INCLUDED_NVRM_POWER_PRIVATE_H
