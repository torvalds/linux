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

#ifndef INCLUDED_AP15RM_CLOCKS_H
#define INCLUDED_AP15RM_CLOCKS_H

#include "nvrm_clocks.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

extern const NvRmModuleClockInfo g_Ap15ModuleClockTable[];
extern const NvU32 g_Ap15ModuleClockTableSize;

// PLLM ratios for graphic clocks
#define NVRM_PLLM_HOST_SPEED_RATIO (4)
#define NVRM_PLLM_2D_LOW_SPEED_RATIO (3)
#define NVRM_PLLM_2D_HIGH_SPEED_RATIO (2)

/**
 * Defines frequency steps derived from PLLP0 fixed output to be used as System
 * clock source frequency. The frequency specified in kHz, and it will be rounded
 * up to the closest divider output. 
 */
#define NVRM_AP15_PLLP_POLICY_SYSTEM_CLOCK \
    PLLP_POLICY_ENTRY(54000)   /* PLLP divider  6, output frequency  54,000kHz */ \
    PLLP_POLICY_ENTRY(72000)   /* PLLP divider  4, output frequency  72,000kHz */ \
    PLLP_POLICY_ENTRY(108000)  /* PLLP divider  2, output frequency 108,000kHz */ \
    PLLP_POLICY_ENTRY(144000)  /* PLLP divider  1, output frequency 144,000kHz */ \
    PLLP_POLICY_ENTRY(216000)  /* PLLP divider  0, output frequency 216,000kHz */

/**
 * Defines frequency steps derived from PLLP0 fixed output to be used as CPU
 * clock source frequency. The frequency specified in kHz, and it will be rounded
 * up to the closest divider output. 
 */
#define NVRM_AP15_PLLP_POLICY_CPU_CLOCK \
    PLLP_POLICY_ENTRY(24000)   /* PLLP divider 16, output frequency  24,000kHz */ \
    PLLP_POLICY_ENTRY(54000)   /* PLLP divider  6, output frequency  54,000kHz */ \
    PLLP_POLICY_ENTRY(108000)  /* PLLP divider  2, output frequency 108,000kHz */ \
    PLLP_POLICY_ENTRY(216000)  /* PLLP divider  0, output frequency 216,000kHz */ \

/**
 * Combines EMC 2x frequency and the respective set of EMC timing parameters for
 * pre-defined EMC configurations (DDR clock is running at EMC 1x frequency)
 */
typedef struct NvRmAp15EmcTimingConfigRec
{
    NvRmFreqKHz Emc2xKHz;
    NvU32 Timing0Reg;
    NvU32 Timing1Reg;
    NvU32 Timing2Reg;
    NvU32 Timing3Reg;
    NvU32 Timing4Reg;
    NvU32 Timing5Reg;
    NvU32 FbioCfg6Reg;
    NvU32 FbioDqsibDly;
    NvU32 FbioQuseDly;
    NvU32 Emc2xDivisor;
    NvRmFreqKHz McKHz;
    NvU32 McDivisor;
    NvU32 McClockSource;
    NvRmFreqKHz CpuLimitKHz;
    NvRmMilliVolts CoreVoltageMv;
} NvRmAp15EmcTimingConfig;

// Defines number of EMC frequency steps for DFS 
#define NVRM_AP15_DFS_EMC_FREQ_STEPS (5)

// Dfines CPU and EMC ratio policy as 
// CpuKHz/CpuMax <= PolicyTabel[PLLM0/(2*EMC2xKHz)] / 256
#define NVRM_AP15_CPU_EMC_RATIO_POLICY \
    256, 192, 144, 122, 108, 98, 91, 86, 81, 77

/*****************************************************************************/

/**
 * Enables/disables module clock.
 * 
 * @param hDevice The RM device handle.
 * @param ModuleId Combined module ID and instance of the target module.
 * @param ClockState Target clock state.
 */
void
Ap15EnableModuleClock(
    NvRmDeviceHandle hDevice,
    NvRmModuleID ModuleId, 
    ModuleClockState ClockState);

// Separate API to control TVDAC clock independently of TVO
// (when TVDAC is used for CRT)  
void
Ap15EnableTvDacClock(
    NvRmDeviceHandle hDevice,
    ModuleClockState ClockState);

/**
 * Resets module (assert/delay/deassert reset signal) if the hold paramter is
 * NV_FLASE. If the hols paramter is NV_TRUE, just assert the reset and return.
 * 
 * @param hDevice The RM device handle.
 * @param Module Combined module ID and instance of the target module.
 * @param hold      To hold or relese the reset.
 */
void AP15ModuleReset(NvRmDeviceHandle hDevice, NvRmModuleID ModuleId, NvBool hold);

/*****************************************************************************/

/**
 * Initializes PLL references table.
 * 
 * @param pPllReferencesTable A pointer to a pointer which this function sets
 *  to the PLL reference table base. 
 * @param pPllReferencesTableSize A pointer to a variable which this function
 *  sets to the PLL reference table size.
 */
void
NvRmPrivAp15PllReferenceTableInit(
    NvRmPllReference** pPllReferencesTable,
    NvU32* pPllReferencesTableSize);

/**
 * Initializes EMC clocks configuration structures and tables.
 * 
 * @param hRmDevice The RM device handle. 
 */
void
NvRmPrivAp15EmcConfigInit(NvRmDeviceHandle hRmDevice);

/**
 * Resets 2D module.
 * 
 * @param hRmDevice The RM device handle. 
 */
void
NvRmPrivAp15Reset2D(NvRmDeviceHandle hRmDevice);

/**
 * Initializes clock source table.
 * 
 * @return Pointer to the clock sources descriptor table.
 */
NvRmClockSourceInfo* NvRmPrivAp15ClockSourceTableInit(void);

/**
 * Sets "as is" specified PLL configuration: switches PLL in bypass mode,
 * changes PLL settings, waits for PLL stabilization, and switches to PLL
 * output.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the PLL description structure.
 * @param M PLL input divider setting.
 * @param N PLL feedback divider setting.
 * @param P PLL output divider setting.
 *  PLL is left disabled (not bypassed) if either M or N setting is zero:
 *  M = 0 or N = 0; otherwise, M, N, P validation is caller responsibility.
 * @param StableDelayUs PLL stabilization delay in microseconds. If specified
 *  value is above guaranteed stabilization time, the latter one is used.
 * @param cpcon PLL charge pump control setting; ignored if TypicalControls
 *  is true.
 * @param lfcon PLL loop filter control setting; ignored if TypicalControls
 *  is true.
 * @param TypicalControls If true, both charge pump and loop filter parameters
 *  are ignored and typical controls that corresponds to specified M, N, P
 *  values will be set. If false, the cpcon and lfcon parameters are set; in
 *  this case parameter validation is caller responsibility.
 * @param flags PLL specific flags. Thse flags are valid only for some PLLs,
 *  see @NvRmPllConfigFlags.
 */
void
NvRmPrivAp15PllSet(
    NvRmDeviceHandle hRmDevice,
    const NvRmPllClockInfo* pCinfo,
    NvU32 M,
    NvU32 N,
    NvU32 P,
    NvU32 StableDelayUs,
    NvU32 cpcon,
    NvU32 lfcon,
    NvBool TypicalControls,
    NvU32 flags);

/**
 * Configures output frequency for specified PLL.
 * 
 * @param hRmDevice The RM device handle.
 * @param PllId Targeted PLL ID.
 * @param MaxOutKHz Upper limit for PLL output frequency.
 * @param pPllOutKHz A pointer to the requested PLL frequency on entry,
 *  and to the actually configured frequency on exit.
 */
void
NvRmPrivAp15PllConfigureSimple(
    NvRmDeviceHandle hRmDevice,
    NvRmClockSource PllId,
    NvRmFreqKHz MaxOutKHz,
    NvRmFreqKHz* pPllOutKHz);

/**
 * Configures specified PLL output to the CM of fixed HDMI frequencies.
 *
 * @param hRmDevice The RM device handle.
 * @param PllId Targeted PLL ID.
 * @param pPllOutKHz A pointer to the actually configured frequency on exit.
 */
void
NvRmPrivAp15PllConfigureHdmi(
    NvRmDeviceHandle hRmDevice,
    NvRmClockSource PllId,
    NvRmFreqKHz* pPllOutKHz);

/**
 * Gets PLL output frequency.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the PLL description structure.
 * 
 * @return PLL output frequency in kHz (reference frequency if PLL
 *  is by-passed; zero if PLL is disabled and not by-passed).
 */
NvRmFreqKHz
NvRmPrivAp15PllFreqGet(
    NvRmDeviceHandle hRmDevice,
    const NvRmPllClockInfo* pCinfo);

/**
 * Gets frequencies of DFS controlled clocks
 * 
 * @param hRmDevice The RM device handle.
 * @param pDfsKHz Output storage pointer for DFS clock frequencies structure
 *  (all frequencies returned in kHz).
 */
void
NvRmPrivAp15DfsClockFreqGet(
    NvRmDeviceHandle hRmDevice,
    NvRmDfsFrequencies* pDfsKHz);

/**
 * Configures DFS controlled clocks
 * 
 * @param hRmDevice The RM device handle.
 * @param pMaxKHz Pointer to the DFS clock frequencies upper limits
 * @param pDfsKHz Pointer to the target DFS frequencies structure on entry;
 *  updated with actual DFS clock frequencies on exit.
 * 
 * @return NV_TRUE if clock configuration is completed; NV_FALSE if this
 *  function has to be called again to complete configuration.
 */
NvBool
NvRmPrivAp15DfsClockConfigure(
    NvRmDeviceHandle hRmDevice,
    const NvRmDfsFrequencies* pMaxKHz,
    NvRmDfsFrequencies* pDfsKHz);

/**
 * Gets maximum DFS domains frequencies that can be used at specified
 *  core voltage.
 * 
 * @param hRmDevice The RM device handle.
 * @param TargetMv Targeted core voltage in mV.
 * @param pDfsKHz Pointer to a structure filled in by this function with
 *  output clock frequencies.
 */
void
NvRmPrivAp15DfsVscaleFreqGet(
    NvRmDeviceHandle hRmDevice,
    NvRmMilliVolts TargetMv,
    NvRmDfsFrequencies* pDfsKHz);

/**
 * Determines if module clock configuration requires AP15-specific handling,
 * and configures the clock if yes.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the module clock descriptor. 
 * @param ClockSourceCount Number of module clock sources.
 * @param MinFreq Requested minimum module clock frequency.
 * @param MaxFreq Requested maximum module clock frequency.
 * @param PrefFreqList Pointer to a list of preferred frequencies sorted
 *  in the decreasing order of priority.
 * @param PrefCount Number of entries in the PrefFreqList array.
 * @param pCstate Pointer to module state structure filled in if special
 *  handling is completed.
 * @param flags Module specific flags
 *
 * @return True indicates that module clock is configured, and regular
 *  configuration should be aborted; False indicates that regular clock
 *  configuration should proceed.
 */
NvBool
NvRmPrivAp15IsModuleClockException(
    NvRmDeviceHandle hRmDevice,
    NvRmModuleClockInfo *pCinfo,
    NvU32 ClockSourceCount,
    NvRmFreqKHz MinFreq,
    NvRmFreqKHz MaxFreq,
    const NvRmFreqKHz* PrefFreqList,
    NvU32 PrefCount,
    NvRmModuleClockState* pCstate,
    NvU32 flags);

/**
 * Configures EMC low-latency fifo for CPU clock source switch.
 * 
 * @param hRmDevice The RM device handle.
 */
void
NvRmPrivAp15SetEmcForCpuSrcSwitch(NvRmDeviceHandle hRmDevice);

/**
 * Configures EMC low-latency fifo for CPU clock divider switch.
 *
 * @param hRmDevice The RM device handle.
 * @param CpuFreq Resulting CPU frequency after divider switch
 * @param Before Specifies if this function is called before (True)
 *  or after (False) divider changes.
 */
void
NvRmPrivAp15SetEmcForCpuDivSwitch(
    NvRmDeviceHandle hRmDevice,
    NvRmFreqKHz CpuFreq,
    NvBool Before);

/**
 * Configures maximum core and memory clocks.
 * 
 * @param hRmDevice The RM device handle.
 */
void
NvRmPrivAp15FastClockConfig(NvRmDeviceHandle hRmDevice);

/**
 * Gets module frequency synchronized with EMC speed.
 * 
 * @param hRmDevice The RM device handle.
 * @param Module The target module ID.
 * 
 * @return Module frequency in kHz.
 */
NvRmFreqKHz NvRmPrivAp15GetEmcSyncFreq(
    NvRmDeviceHandle hRmDevice,
    NvRmModuleID Module);

/**
 * Disables PLLs
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the last configured module clock descriptor. 
 * @param pCstate Pointer to the last configured module state structure.
 */
void
NvRmPrivAp15DisablePLLs(
    NvRmDeviceHandle hRmDevice,
    const NvRmModuleClockInfo* pCinfo,
    const NvRmModuleClockState* pCstate);

/**
 * Turns PLLD (MIPI PLL) power On/Off
 * 
 * @param hRmDevice The RM device handle.
 * @param ConfigEntry NV_TRUE if this function is called before display
 *  clock configuration; NV_FALSE otherwise.
 * @param Pointer to the current state of MIPI PLL power rail, updated
 *  by this function.
 */
void
NvRmPrivAp15PllDPowerControl(
    NvRmDeviceHandle hRmDevice,
    NvBool ConfigEntry,
    NvBool* pMipiPllVddOn);

/**
 * Clips EMC frequency high limit to one of the fixed DFS EMC configurations,
 * and if necessary adjust CPU high limit respectively.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCpuHighKHz A pointer to the variable, which contains CPU frequency
 *  high limit in KHz (on entry - requested limit, on exit - clipped limit)
 * @param pEmcHighKHz A pointer to the variable, which contains EMC frequency
 *  high limit in KHz (on entry - requested limit, on exit - clipped limit)
 */
void
NvRmPrivAp15ClipCpuEmcHighLimits(
    NvRmDeviceHandle hRmDevice,
    NvRmFreqKHz* pCpuHighKHz,
    NvRmFreqKHz* pEmcHighKHz);


/**
 * Configures some special bits in the clock source register for given module.
 * 
 * @param hRmDevice The RM device handle.
 * @param Module Target module ID.
 * @param ClkSourceOffset Clock source register offset.
 * @param flags Module specific clock configuration flags.
 */
void
NvRmPrivAp15ClockConfigEx(
    NvRmDeviceHandle hRmDevice,
    NvRmModuleID Module,
    NvU32 ClkSourceOffset,
    NvU32 flags);

/**
 * Enables PLL in simulation.
 * 
 * @param hRmDevice The RM device handle.
 */
void NvRmPrivAp15SimPllInit(NvRmDeviceHandle hRmDevice);

/**
 * Configures oscillator (main) clock doubler.
 * 
 * @param hRmDevice The RM device handle.
 * @param OscKHz Oscillator (main) clock frequency in kHz.
 * 
 * @return NvSuccess if the specified oscillator frequency is supported, and
 * NvError_NotSupported, otherwise.
 */
NvError
NvRmPrivAp15OscDoublerConfigure(
    NvRmDeviceHandle hRmDevice,
    NvRmFreqKHz OscKHz);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  // INCLUDED_AP15RM_CLOCKS_H 
