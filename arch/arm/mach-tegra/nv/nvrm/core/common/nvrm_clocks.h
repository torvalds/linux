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

#ifndef INCLUDED_NVRM_CLOCKS_H
#define INCLUDED_NVRM_CLOCKS_H

#include "nvrm_clocks_limits_private.h"
#include "nvrm_module.h"
#include "nvrm_diag.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

#define NVRM_RESET_DELAY            (10)
#define NVRM_CLOCK_CHANGE_DELAY     (2)
#define NVRM_VARIABLE_DIVIDER       ((NvU32)-1)

// Fixed HDMI frequencies
#define NVRM_HDMI_480p_FIXED_FREQ_KHZ (27000)
#define NVRM_HDMI_720p_FIXED_FREQ_KHZ (74250)
#define NVRM_HDMI_1080p_FIXED_FREQ_KHZ (148500)

#define NvRmIsFixedHdmiKHz(KHz) \
    (((KHz) == NVRM_HDMI_480p_FIXED_FREQ_KHZ) || \
     ((KHz) == NVRM_HDMI_720p_FIXED_FREQ_KHZ) || \
     ((KHz) == NVRM_HDMI_1080p_FIXED_FREQ_KHZ))

// BR-fixed PLLP output frequency in kHz (override disabled) 
#define NV_BOOT_PLLP_FIXED_FREQ_KHZ (432000)

// RM-fixed PLLP output frequency in kHz (override enabled)
#define NVRM_PLLP_FIXED_FREQ_KHZ (216000)

// PLLP1-PLLP4 configurations set by RM during initialization and resume
// from LP0 state. PLLP1 and PLLP3 settings are never changed. PLLP2 and
// PLLP4 settings are overwritten according to SoC-specific DVFS policy.
// PLLPx output frequency = NVRM_PLLP_FIXED_FREQ_KHZ / (1 + setting/2)
#define NVRM_FIXED_PLLP1_SETTING (13)
#define NVRM_FIXED_PLLP2_SETTING (7)
#define NVRM_FIXED_PLLP3_SETTING (4)
#define NVRM_FIXED_PLLP4_SETTING (2)

/// Guaranteed MIPI PLL Stabilization Delay
#define NVRM_PLL_MIPI_STABLE_DELAY_US (1000)

/**
 * MIPI PLL feedback divider N threshold for loop filter control setting:
 * LFCON = 1 if N is above threshold, and LFCON = 0, otherwise
 */
#define NVRM_PLL_MIPI_LFCON_SELECT_N_DIVIDER (600)

/**
 * MIPI PLL feedback divider N thresholds for charge pump control setting
 * selection.
 */
#define NVRM_PLL_MIPI_CPCON_SELECT_STEPS_N_DIVIDER \
    0,      /* CPCON = 1 if feedback divider  N = 0 (invalid setting)*/ \
    50,     /* CPCON = 2 if feedback divider  N <= 50 */ \
    175,    /* CPCON = 3 if feedback divider  N = ( 50 - 175] */ \
    300,    /* CPCON = 4 if feedback divider  N = (175 - 300] */ \
    375,    /* CPCON = 5 if feedback divider  N = (300 - 375] */ \
    450,    /* CPCON = 6 if feedback divider  N = (375 - 450] */ \
    525,    /* CPCON = 7 if feedback divider  N = (450 - 525] */ \
    600,    /* CPCON = 8 if feedback divider  N = (525 - 600] */ \
    700,    /* CPCON = 9 if feedback divider  N = (600 - 700] */ \
    800,    /* CPCON = 10 if feedback divider N = (700 - 800] */ \
    900,    /* CPCON = 11 if feedback divider N = (800 - 900] */ \
    1000    /* CPCON = 12 if feedback divider N = (900 - 1000] */
            /* CPCON = 13 if feedback divider N > 1000 (invalid setting) */

/// Guaranteed Low power PLL Stabilization Delay
#define NVRM_PLL_LP_STABLE_DELAY_US (300)

/**
 * Low power PLL feedback divider N threshold for charge pump control. For N
 * values below threshold charge pump control is always set to 1. For N values
 * above threshold charge pump control setting depends on comparison frequency
 * as specified in the table below.
 */
#define NVRM_PLL_LP_MIN_N_FOR_CPCON_SELECTION (200)

/**
 * Low power PLL comparison frequency Fcomp = Din/M thresholds for charge pump
 * control setting selection.
 */
#define NVRM_PLL_LP_CPCON_SELECT_STEPS_KHZ \
    6000,   /* CPCON = 1 if Fin/M >= 6000 kHz (outside valid range)*/ \
    4000,   /* CPCON = 2 if Fin/M = [4000 - 6000) kHz */ \
    3000,   /* CPCON = 3 if Fin/M = [3000 - 4000) kHz */ \
    2000,   /* CPCON = 4 if Fin/M = [2000 - 3000) kHz */ \
    1750,   /* CPCON = 5 if Fin/M = [1750 - 2000) kHz */ \
    1500,   /* CPCON = 6 if Fin/M = [1500 - 1750) kHz */ \
    1250,   /* CPCON = 7 if Fin/M = [1250 - 1500) kHz */ \
    1000    /* CPCON = 8 if Fin/M = [1000 - 1250) kHz */
            /* CPCON = 9 if Fin/M < 1000 kHz (outside valid range) */

/// Combines PLL and PLL output divider settings for fixed pre-defined frequency
typedef struct NvRmPllFixedConfigRec
{
    // Output pre-defined frequency
    NvRmFreqKHz OutputKHz;

    // Interanl PLL dividers settings
    NvU32 M;
    NvU32 N;
    NvU32 P;

    // Exteranl output divider settings
    // (ignored if there is no output divider)
    NvU32 D;
} NvRmPllFixedConfig;

/**
 * Defines list of supported PLLA configurations (2 entries for 12.2896
 * frequency that can be either truncated or rounded to KHz). The reference
 * frequency for PLLA is fixed at 28.8MHz, therefore there is no dependency on
 * oscillator frequency. Output frequency is divided by PLLA_OUT0 fractional
 * divider.
 */
#define NVRM_PLLA_CONFIGURATIONS \
    { 11289, 25, 49, 0, 8}, \
    { 11290, 25, 49, 0, 8}, \
    { 12000, 24, 50, 0, 8}, \
    { 12288, 25, 64, 0, 10}, \
    { 56448, 25, 49, 0, 0}, \
    { 73728, 25, 64, 0, 0}

// Default audio sync frequency
#define NVRM_AUDIO_SYNC_KHZ (11289)

/**
 * Defines PLLU configurations for different oscillator frequencies. Output
 * frequency is 12MHz for USB with no ULPI support, or 60MHz if null ULPI is
 * supported, or 480MHz for HS PLL. PLLU_OUT0 does not have output divider.
 * 
 */
#define NVRM_PLLU_AT_12MHZ { 12000, 12, 384, 5, 0}
#define NVRM_PLLU_AT_13MHZ { 12000, 13, 384, 5, 0}
#define NVRM_PLLU_AT_19MHZ { 12000,  4,  80, 5, 0}
#define NVRM_PLLU_AT_26MHZ { 12000, 26, 384, 5, 0}

#define NVRM_PLLU_ULPI_AT_12MHZ { 60000, 12, 240, 2, 0}
#define NVRM_PLLU_ULPI_AT_13MHZ { 60000, 13, 240, 2, 0}
#define NVRM_PLLU_ULPI_AT_19MHZ { 60000,  4,  50, 2, 0}
#define NVRM_PLLU_ULPI_AT_26MHZ { 60000, 26, 240, 2, 0}

#define NVRM_PLLU_HS_AT_12MHZ { 480000, 12, 960, 1, 0}
#define NVRM_PLLU_HS_AT_13MHZ { 480000, 13, 960, 1, 0}
#define NVRM_PLLU_HS_AT_19MHZ { 480000,  4, 200, 1, 0}
#define NVRM_PLLU_HS_AT_26MHZ { 480000, 26, 960, 1, 0}

/**
 * Defines PLLP configurations for different oscillator frequencies. Output
 * frequency is always the same. PLLP_OUT0 does not have output divider
 * 
 */
#define NVRM_PLLP_AT_12MHZ { NVRM_PLLP_FIXED_FREQ_KHZ, 12, 432, 1, 0}
#define NVRM_PLLP_AT_13MHZ { NVRM_PLLP_FIXED_FREQ_KHZ, 13, 432, 1, 0}
#define NVRM_PLLP_AT_19MHZ { NVRM_PLLP_FIXED_FREQ_KHZ,  4,  90, 1, 0}
#define NVRM_PLLP_AT_26MHZ { NVRM_PLLP_FIXED_FREQ_KHZ, 26, 432, 1, 0}

/**
 * Defines PLLD/PLLC 720p/1080i HDMI configurations for different oscillator
 * frequencies. For both PLLC and PLLD output frequency is fixed as 4 * 74250
 * = 594000. However, PLLC_OUT0 will be running at this frequency exactly, while
 * PLLD_OUT0 will be runnig at half frequency 297000 (h/w divide by 2 always). 
 * This difference in source frequency is will be taken care by Display and
 * HDMI clock dividers.
 */
#define NVRM_PLLHD_AT_12MHZ { 594000, 12, 594, 0, 0}
#define NVRM_PLLHD_AT_13MHZ { 594000, 13, 594, 0, 0}
#define NVRM_PLLHD_AT_19MHZ { 594000, 16, 495, 0, 0}
#define NVRM_PLLHD_AT_26MHZ { 594000, 26, 594, 0, 0}

// Display divider is part of the display module and it is not described
// in central module clock information table. Hence, need this define.
#define NVRM_DISPLAY_DIVIDER_MAX (128)

/*****************************************************************************/
/*****************************************************************************/

/*
 * Defines module clock state
 */ 
typedef enum 
{
    // Module clock disable
    ModuleClockState_Disable = 0, 

    // Module clock enable
    ModuleClockState_Enable = 1, 

    ModuleClockState_Force32 = 0x7FFFFFFF
} ModuleClockState;


typedef enum
{
    NvRmClockSource_Invalid = 0,
#define NVRM_CLOCK_SOURCE(A, B, C, D, E, F, G, H, x) NvRmClockSource_##x,
#include "nvrm_clockids.h"
#undef NVRM_CLOCK_SOURCE
    NvRmClockSource_Num,
    NvRmClockSource_Force32 = 0x7FFFFFFF
} NvRmClockSource;


typedef enum
{
    // Clock source with fixed frequency (e.g., oscillator, not configurable
    // PLL, external clock, etc.)
    NvRmClockSourceType_Fixed = 1,

    // Clock source from configurable PLL
    NvRmClockSourceType_Pll,

    // Secondary clock source derived from oscillator, PLL or other secondary
    // source via clock divider
    NvRmClockSourceType_Divider,

    // Core clock source derived from several input sources via 2-stage selector
    // and rational super-clock divider
    NvRmClockSourceType_Core,

    // Selector clock source derived from several input sources via 1-stage selector
    // and optional clock frequency doubler
    NvRmClockSourceType_Selector,

    NvRmClockSourceType_Num,
    NvRmClockSourceType_Force32 = 0x7FFFFFFF
} NvRmClockSourceType;

typedef enum
{
    // No divider
    NvRmClockDivider_None = 1,

    // Integer divider by N
    NvRmClockDivider_Integer,

    // Integer divider by (N + 1)
    NvRmClockDivider_Integer_1,

    // Fractional divider by (N/2 + 1)
    NvRmClockDivider_Fractional_2,

    // Skipping N clocks out of every 16, i.e fout = fin * (16-N)/16
    // (= to Keeper16 with 1-complemented settings N = 15 - M)
    NvRmClockDivider_Skipper16,

    // Keep M+1 clocks out of every 16, fout = fin * (M+1)/16
    // (= to Skipper16 with 1-complemented setting M = 15 - N)
    NvRmClockDivider_Keeper16,

    // Integer divider by (N + 2) = cascade Fractional : Fixed 1/2
    NvRmClockDivider_Integer_2,

    NvRmClockDivider_Num,
    NvRmClockDivider_Force32 = 0x7FFFFFFF
} NvRmClockDivider;

typedef enum
{
    // AP10 PLLs (PLLC and PLLA)
    NvRmPllType_AP10 = 1,

    // MIPI PLLs (PLLD and PLLU on AP15)
    NvRmPllType_MIPI,

    // Low Power PLLs (PLLA, PLLC, PLLM, PLLP, PLLX, PLLS) 
    NvRmPllType_LP,

    // AP20 USB HS PLL (PLLU on AP20) 
    NvRmPllType_UHS,

    NvRmPllType_Num,
    NvRmPllType_Force32 = 0x7FFFFFFF
} NvRmPllType;

/**
 * Defines PLL configuration flags which are applicable for some PLLs.
 * Multiple flags can be OR'ed and passed to the NvRmPrivAp15PllSet() API.
 */
typedef enum
{
    /// Use Slow Mode output for MIPI PLL
    NvRmPllConfigFlags_SlowMode = 0x1,

    /// Use Fast Mode output for MIPI PLL
    NvRmPllConfigFlags_FastMode = 0x2,

    /// Enable differential outputs for MIPI PLL
    NvRmPllConfigFlags_DiffClkEnable = 0x4,

    /// Disable differential outputs for MIPI PLL
    NvRmPllConfigFlags_DiffClkDisable = 0x8,

    /// Override fixed configuration for PLLP
    NvRmPllConfigFlags_Override = 0x10,

    /// Enable duty cycle correction for LP PLL
    NvRmPllConfigFlags_DccEnable = 0x20,

    /// Disable duty cycle correction for LP PLL
    NvRmPllConfigFlags_DccDisable = 0x40,

    NvRmPllConfigFlags_Num,
    NvRmPllConfigFlags_Force32 = 0x7FFFFFFF
} NvRmPllConfigFlags;

/*****************************************************************************/

// Holds source selection and divider configuration for module clock as well
// as module reset information.
typedef struct NvRmModuleClockInfoRec
{
    NvRmModuleID Module;
    NvU32 Instance;
    NvU32 SubClockId;

    NvRmClockSource Sources[NvRmClockSource_Num];
    NvRmClockDivider Divider;

    NvU32 ClkSourceOffset;

    NvU32 SourceFieldMask;
    NvU32 SourceFieldShift;

    NvU32 DivisorFieldMask;
    NvU32 DivisorFieldShift;

    NvU32 ClkEnableOffset;
    NvU32 ClkEnableField;
    NvU32 ClkResetOffset;
    NvU32 ClkResetField;

    NvRmDiagModuleID DiagModuleID;
}NvRmModuleClockInfo;

typedef struct NvRmModuleClockStateRec
{
    NvU32 Divider;
    NvU32 SourceClock;
    NvRmFreqKHz actual_freq;
    NvU32 refCount;
    NvU32 Vstep;
    NvBool Vscale;
    NvBool FirstReference;
#if NVRM_DIAG_LOCK_SUPPORTED
    NvBool DiagLock;    // once locked, can not be changed
#endif
} NvRmModuleClockState;

/*****************************************************************************/

// Holds configuration information about the fixed clock source that can be
// only enabled/disabled (e.g, oscillator, external clock, fixed frequency PLL).
typedef struct NvRmFixedClockInfoRec
{
    // Source ID
    NvRmClockSource SourceId;

    // Fixed source input (must be fixed source as well). For primary sources
    // this field is set to NvRmClockSource_Invalid
    NvRmClockSource InputId;

    // Enable register offset and field
    NvU32 ClkEnableOffset;
    NvU32 ClkEnableField;
} NvRmFixedClockInfo;


// Holds configuration information about configurable PLL 
typedef struct NvRmPllClockInfoRec
{
    // PLL output ID
    NvRmClockSource SourceId;

    // PLL reference clock ID
    NvRmClockSource InputId;

    // PLL type
    NvRmPllType PllType;

    // Ofsets of PLL registers
    NvU32 PllBaseOffset;
    NvU32 PllMiscOffset;

    // PLL VCO range
    NvRmFreqKHz PllVcoMin;
    NvRmFreqKHz PllVcoMax;
} NvRmPllClockInfo;


// Holds configuration information about secondary clock source derived
// from one input source via clock divider
typedef struct NvRmDividerClockInfoRec
{
    // Divider output clock ID
    NvRmClockSource SourceId;

    // Divider input clock ID
    NvRmClockSource InputId;

    // Type of the divider
    NvRmClockDivider Divider;

    // Divider control register offset
    NvU32 ClkControlOffset;

    // Clock rate parameter field;
    // ignored for divider with fixed setting
    NvU32 ClkRateFieldMask;
    NvU32 ClkRateFieldShift;

    // Divider control field
    NvU32 ClkControlField;
    NvU32 ClkEnableSettings;
    NvU32 ClkDisableSettings;

    // Fixed divider rate parameter setting;
    // NVRM_VARIABLE_DIVIDER if divider is variable
    NvU32 FixedRateSetting;
} NvRmDividerClockInfo;


typedef enum
{
    // The enumeartion values must not be changed for Mode(ModeField) formula
    // below to work properly 
    NvRmCoreClockMode_Suspend = 0,
    NvRmCoreClockMode_Idle = 1,
    NvRmCoreClockMode_Run = 2,
    NvRmCoreClockMode_Irq = 3,
    NvRmCoreClockMode_Fiq = 4,

    NvRmCoreClockMode_Num,
    NvRmCoreClockMode_Force32 = 0x7FFFFFFF
} NvRmCoreClockMode;

// Holds configuration information about core clock source derived from several
// input sources via 2-stage selector and rational super-clock divider  
typedef struct NvRmCoreClockInfoRec
{
    // Core clock ID
    NvRmClockSource SourceId;

    // Super clock input sources, same in each mode
    NvRmClockSource Sources[NvRmClockSource_Num];

    // Offset of the core clock input source selector register
    NvU32 SelectorOffset;

    // Clock mode field: 
    // 0 => NvRmCoreClockMode_Suspend (0)
    // 1 => NvRmCoreClockMode_Idle (1)
    // 2-3 => NvRmCoreClockMode_Run (2)
    // 4-7 => NvRmCoreClockMode_Irq (3)
    // 8-15 => NvRmCoreClockMode_Fiq (4)
    // Mode = (ModeField == 0) ? NvRmCoreClockMode_Suspend : (1 + LOG2(ModeField))
    NvU32 ModeFieldMask;
    NvU32 ModeFieldShift;

    // Sorce selection fileds for each mode
    NvU32 SourceFieldMasks[NvRmCoreClockMode_Num];
    NvU32 SourceFieldShifts[NvRmCoreClockMode_Num];

    // Offset of the divider register
    NvU32 DividerOffset;

    // Divider enable field (divider is by-passed if disabled)
    // Fout = Fin * (Dividend + 1) / (Divisor + 1)
    NvU32 DividerEnableFiledMask;
    NvU32 DividerEnableFiledShift;

    // Dividend field
    NvU32 DividendFieldMask;
    NvU32 DividendFieldShift;
    NvU32 DividendFieldSize;

    // Divisor field
    NvU32 DivisorFieldMask;
    NvU32 DivisorFieldShift;
    NvU32 DivisorFieldSize;
} NvRmCoreClockInfo;

// Holds configuration information about secondary clock source derived from
// several input sources via 1-stage selector and clock frequency doubler 
typedef struct NvRmSelectorClockInfoRec
{
    // Selector output clock ID
    NvRmClockSource SourceId;

    // Selector input sources
    NvRmClockSource Sources[NvRmClockSource_Num];

    // Offset of the input source selector register
    NvU32 SelectorOffset;

    // Source selection field
    NvU32 SourceFieldMask;
    NvU32 SourceFieldShift;

    // Doubler control (optional - set field to 0, if no doubler)
    NvU32 DoublerEnableOffset;
    NvU32 DoublerEnableField;
} NvRmSelectorClockInfo;

// Holds information on system bus clock dividers
typedef struct NvRmSystemBusComplexInfoRec
{
    // Offset of the Bus Rates control register
    NvU32 BusRateOffset;

    // Combined bus clocks disable fields (1 = disable)
    NvU32 BusClockDisableFields;

    // V-pipe vclk divider field: vclk rate = system core rate * (n+1) /16
    // All fields are 0, if VDE (V-pipe) clock is decoupled from the System bus
    NvU32 VclkDividendFieldMask;
    NvU32 VclkDividendFieldShift;
    NvU32 VclkDividendFieldSize;

    // AHB hclk divider field: hclk rate = system core rate / (n+1)
    NvU32 HclkDivisorFieldMask;
    NvU32 HclkDivisorFieldShift;
    NvU32 HclkDivisorFieldSize;

    // APB pclk divider field: pclk rate = hclk rate / (n+1)
    NvU32 PclkDivisorFieldMask;
    NvU32 PclkDivisorFieldShift;
    NvU32 PclkDivisorFieldSize;
} NvRmSystemBusComplexInfo;

/*****************************************************************************/

typedef union
{
    NvRmFixedClockInfo* pFixed;
    NvRmPllClockInfo* pPll;
    NvRmDividerClockInfo* pDivider;
    NvRmCoreClockInfo* pCore;
    NvRmSelectorClockInfo* pSelector;
} NvRmClockSourceInfoPtr;

// Abstarcts clock source information for different source types.
typedef struct NvRmClockSourceInfoRec
{
    // Clock source ID
    NvRmClockSource SourceId;

    // Clock source type
    NvRmClockSourceType SourceType;

    // Pointer to clock source information
    NvRmClockSourceInfoPtr pInfo;
} NvRmClockSourceInfo;

/*****************************************************************************/

// Holds PLL references
typedef struct NvRmPllReferenceRec
{
    // PLL ID
    NvRmClockSource SourceId;

    // Stop PLL during low power state flag (reported by DFS to kernel)
    NvRmDfsStatusFlags StopFlag;

    // Reference counter
    NvU32 ReferenceCnt;

    // Module clocks reference array
    NvBool* AttachedModules; 

    // External clock attachment reference count (debugging only)
    NvU32 ExternalClockRefCnt;
} NvRmPllReference;

/**
 * Holds DFS clock source configuration record
 */
typedef struct NvRmDfsSourceRec
{
    // DFS Clock Source Id
    NvRmClockSource SourceId;

    // DFS Clock Source frequency
    // CPU and System/AVP clock domains: this field holds input frequency
    // of core super-divider (from base PLL output or secondary PLL divider) 
    // V-pipe domain (if it is decoupled from System bus): this field holds
    // output frequency of VDE module divider = VDE domain frequency
    // EMC domain: this field holds EMC2x frequency specified in selected
    // entry in EMC configuration table
    NvRmFreqKHz SourceKHz;

    // DFS Clock Source divider setting
    // CPU and System/AVP clock domains: this field holds settings for
    // secondary PLL divider between base PLL output and super-divider
    // V-pipe domain (if it is decoupled from System bus): this field holds
    // settings for VDE module clock divider
    // EMC domain: this field holds index into EMC configuration table
    NvU32 DividerSetting;

    // Minimum Voltage required to run DFS domain from this source 
    NvRmMilliVolts MinMv;
} NvRmDfsSource;

/**
 * Combines frequencies for DFS controlled clock domains
 */
typedef struct NvRmDfsFrequenciesRec
{
    NvRmFreqKHz Domains[NvRmDfsClockId_Num];
} NvRmDfsFrequencies;

/*****************************************************************************/

/*
 * Defines execution platforms
 */
typedef enum
{
    // SoC Chip
    ExecPlatform_Soc = 0x1,

    // FPGA
    ExecPlatform_Fpga,

    // QuickTurn
    ExecPlatform_Qt,

    // Simulation
    ExecPlatform_Sim,

    ExecPlatform_Force32 = 0x7FFFFFFF
} ExecPlatform;

/*****************************************************************************/
/*****************************************************************************/

/**
 * Determines execution platform.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * 
 * @return Execution platform ID.
 */
ExecPlatform NvRmPrivGetExecPlatform(NvRmDeviceHandle hRmDeviceHandle);

/**
 * Initializes clock sources frequencies.
 * 
 * @param hRmDevice The RM device handle. 
 * @param pClockSourceFreq A pointer to the source frequencies table to be
 *  filled in by this function.
 */
void
NvRmPrivClockSourceFreqInit(
    NvRmDeviceHandle hRmDevice,
    NvU32* pClockSourceFreq);

/**
 * Initializes bus clocks.
 * 
 * @param hRmDevice The RM device handle. 
 * @param SystemFreq The system bus frequency
 */
void
NvRmPrivBusClockInit(NvRmDeviceHandle hRmDevice, NvRmFreqKHz SystemFreq);

/**
 * Initializes PLL power rails and synchronizes PMU ref count
 * 
 * @param hRmDevice The RM device handle.
 */
void
NvRmPrivPllRailsInit(NvRmDeviceHandle hRmDevice);

/**
 * Set nominal core and DDR I/O voltages and boosts core and memory
 * clocks to maximum.
 * 
 * @param hRmDevice The RM device handle.
 */
void
NvRmPrivBoostClocks(NvRmDeviceHandle hRmDevice);

/**
 * Enables/disables module clock (private utility directly accessing h/w,
 * no ref counting).
 * 
 * @param hDevice The RM device handle.
 * @param ModuleId Combined module ID and instance of the target module.
 * @param ClockState Target clock state.
 */
void
NvRmPrivEnableModuleClock(
    NvRmDeviceHandle hRmDevice,
    NvRmModuleID ModuleId,
    ModuleClockState ClockState);

/**
 * Gets currently selected clock source for the specified core clock.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the core clock description structure.
 * 
 * @return Core clock source ID.
 */
NvRmClockSource
NvRmPrivCoreClockSourceGet(
    NvRmDeviceHandle hRmDevice,
    const NvRmCoreClockInfo* pCinfo);

/**
 * Gets core clock frequency.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the core clock description structure.
 * 
 * @return Core clock frequency in kHz.
 */
NvRmFreqKHz
NvRmPrivCoreClockFreqGet(
    NvRmDeviceHandle hRmDevice,
    const NvRmCoreClockInfo* pCinfo);

/**
 * Finds the slection index of the specified core clock source.
 * 
 * @param pCinfo Pointer to the core clock description structure.
 * @param SourceId Id of the clock source to find index of 
 * @param pSourceIndex Output storage pointer for the clock source index;
 *  returns NvRmClockSource_Num if specified source Id can not be found
 *  in the core clock descriptor.
 */
void
NvRmPrivCoreClockSourceIndexFind(
    const NvRmCoreClockInfo* pCinfo,
    NvRmClockSource SourceId,
    NvU32* pSourceIndex);

/**
 * Finds the best source for the target core clock frequency.
 * The best source is a valid source with frequency above and closest
 * to the target; if such source does not exist, the best source is a
 * valid source below and closest to the target. If no valid source
 * exists (i.e., all available find source are above maximum domain
 * frequency)
 * 
 * @param pCinfo Pointer to the core clock description structure.
 * @param MaxFreq Upper limit for source frequency in kHz
 * @param Target frequency in kHz
 * @param pSourceFreq Output storage pointer for the best source frequency;
 *  returns 0 if no valid source below upper limit was found
 * @param pSourceIndex Output storage pointer for the best source index in
 *  core clock descriptor; returns NvRmClockSource_Num if no valid source
 *  was found
 */
void
NvRmPrivCoreClockBestSourceFind(
    const NvRmCoreClockInfo* pCinfo,
    NvRmFreqKHz MaxFreq,
    NvRmFreqKHz TargetFreq,
    NvRmFreqKHz* pSourceFreq,
    NvU32* pSourceIndex);

/**
 * Sets "as is" specified core clock configuration.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the core clock description structure.
 * @param SourceId The ID of the clock source to drive core clock.
 * @param m Superdivider dividend value.
 * @param n Superdivider divisor value.
 * 
 * There is no error return status for this API call.
 * If specified source can not be selected(not present 
 * in core clock descriptor), asserts are encountered.
 */
void
NvRmPrivCoreClockSet(
    NvRmDeviceHandle hRmDevice,
    const NvRmCoreClockInfo* pCinfo,
    NvRmClockSource SourceId,
    NvU32 m,
    NvU32 n);

/**
 * Configures core clock frequency.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the core clock description structure.
 * @param MaxFreq Upper limit for clock source frequency in kHz.
 * @param pFreq Pointer to the target frequency in kHz on entry; updated
 *  with actual clock frequencies on exit.
 * @param pSourceId Pointer to the target clock source ID on entry; if set
 *  to NvRmClockSource_Num, no source target is specified, and the best source
 *  for the target frequency is selected automatically. On exit, points to the
 *  actually selected source ID.
 * 
 * @retval NvSuccess if core clock was configured successfully.
 * @retval NvError_NotSupported if the specified target source is invalid or
 *  no target source specified and no valid source was found. 
 */
NvError
NvRmPrivCoreClockConfigure(
    NvRmDeviceHandle hRmDevice,
    const NvRmCoreClockInfo* pCinfo,
    NvRmFreqKHz MaxFreq,
    NvRmFreqKHz* pFreq,
    NvRmClockSource* pSourceId);

/**
 * Gets bus clocks frequencies.
 * 
 * @param hRmDevice The RM device handle.
 * @param SystemFreq System core clock frequency in kHz.
 * @param pVclkFreq Output storage pointer for V-bus clock frequency in kHz.
 *  If VDE clock is decoupled from the System bus, 0kHz will be returned.
 * @param pHclkFreq Output storage pointer for AHB clock frequency in kHz.
 * @param pPclkFreq Output storage pointer for APB clock frequency in kHz.
 */
void
NvRmPrivBusClockFreqGet(
    NvRmDeviceHandle hRmDevice,
    NvRmFreqKHz SystemFreq,
    NvRmFreqKHz* pVclkFreq,
    NvRmFreqKHz* pHclkFreq,
    NvRmFreqKHz* pPclkFreq);

/**
 * Configures bus clocks frequencies.
 * 
 * @param hRmDevice The RM device handle.
 * @param SystemFreq System core clock frequency in kHz.
 * @param pVclkFreq Pointer to the target V-bus clock frequency in kHz
 *  on entry, updated with actually set frequency on exit. If VDE clock
 *  is decoupled from the System bus, 0kHz will be returned.
 * @param pHclkFreq Pointer to the target AHB clock frequency in kHz
 *  on entry, updated with actually set frequency on exit.
 * @param pPclkFreq Pointer to the target APB clock frequency in kHz
 *  on entry, updated with actually set frequency on exit.
 * @param PclkMaxFreq APB clock maximum frequency; APB is the only clock
 *  in the system complex that may have different (lower) maximum limit. 
 */
void
NvRmPrivBusClockFreqSet(
    NvRmDeviceHandle hRmDevice,
    NvRmFreqKHz SystemFreq,
    NvRmFreqKHz* pVclkFreq,
    NvRmFreqKHz* pHclkFreq,
    NvRmFreqKHz* pPclkFreq,
    NvRmFreqKHz PclkMaxFreq);

/**
 * Reconfigures PLLX0 to specified frequency (and switches CPU to back-up
 * PLLP0 if PLLX0 is currently used as CPU source).
 * 
 * @param hRmDevice The RM device handle.
 * @param TargetFreq New PLLX0 output frequency.
 */
void
NvRmPrivReConfigurePllX(
    NvRmDeviceHandle hRmDevice,
    NvRmFreqKHz TargetFreq);

/**
 * Reconfigures PLLC0 to specified frequency (switches to PLLP0 all modules
 * that use PLLC0 as a source, and then restores source configuration back).
 * Should be called only when core voltage is set at nominal.
 * 
 * @param hRmDevice The RM device handle.
 * @param TargetFreq New PLLC0 output frequency.
 */
void
NvRmPrivReConfigurePllC(
    NvRmDeviceHandle hRmDevice,
    NvRmFreqKHz TargetFreq);

/**
 * Gets maximum PLLC0 frequency set as a default target, when there are no
 * fixed frequency requirements.
 * 
 * @param hRmDevice The RM device handle.
 * 
 * @return Maximum target for PLLC0 frequency.
 */
NvRmFreqKHz NvRmPrivGetMaxFreqPllC(NvRmDeviceHandle hRmDevice);

/**
 * Configures PLLC0 at maximum frequency, when there are no fixed frequency
 * requirements. Should be called only when core voltage is set at nominal.
 * 
 * @param hRmDevice The RM device handle.
 * 
 * @return Maximum target for PLLC0 frequency.
 */
void NvRmPrivBoostPllC(NvRmDeviceHandle hRmDevice);

/**
 * Updates PLL frequency entry in the clock source table.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the PLL description structure.
 */
void
NvRmPrivPllFreqUpdate(
    NvRmDeviceHandle hRmDevice,
    const NvRmPllClockInfo* pCinfo);

/**
 * Updates divider frequency entry in the clock source table.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the divider clock description structure.
 */
void
NvRmPrivDividerFreqUpdate(
    NvRmDeviceHandle hRmDevice,
    const NvRmDividerClockInfo* pCinfo);

/**
 * Sets "as is" specified divider parmeter.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the divider clock description structure.
 * @param setting Divider setting
 */
void
NvRmPrivDividerSet(
    NvRmDeviceHandle hRmDevice,
    const NvRmDividerClockInfo* pCinfo,
    NvU32 setting);

/**
 * Gets divider output frequency.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the divider clock description structure.
 * 
 * @return Divider output frequency in kHz; zero if divider itself or
 *  divider's input clock is disabled.
 */
NvRmFreqKHz
NvRmPrivDividerFreqGet(
    NvRmDeviceHandle hRmDevice,
    const NvRmDividerClockInfo* pCinfo);

/**
 * Finds minimum divider output frequency, which is above the specified
 *  target frequency.
 * 
 * @param DividerType Divider type (only fractional dividers for now).
 * @param pCinfo SourceKHz Divider source (input) frequency in kHz.
 * @param MaxKHz Output divider frequency upper limit. Target frequency must
 *  be below this limit. If no frequency above the target but within the limit
 *  can be found, then maximum frequency within the limit is returned.
 * @param pTargetKHz A pointer to the divider output frequency. On entry
 *  specifies target; on exit - found frequency.  
 * 
 * @return Divider setting to get found frequency from the given source.
 */
NvU32
NvRmPrivFindFreqMinAbove(
    NvRmClockDivider DividerType,
    NvRmFreqKHz SourceKHz,
    NvRmFreqKHz MaxKHz,
    NvRmFreqKHz* pTargetKHz);

/**
 * Finds maximum divider output frequency, which is below the specified
 *  target frequency.
 * 
 * @param DividerType Divider type (only fractional dividers for now).
 * @param pCinfo SourceKHz Divider source (input) frequency in kHz.
 * @param MaxKHz Output divider frequency upper limit. Target frequency must
 *  be below this limit.
 * @param pTargetKHz A pointer to the divider output frequency. On entry
 *  specifies target; on exit - found frequency.  
 * 
 * @return Divider setting to get found frequency from the given source.
 */
NvU32
NvRmPrivFindFreqMaxBelow(
    NvRmClockDivider DividerType,
    NvRmFreqKHz SourceKHz,
    NvRmFreqKHz MaxKHz,
    NvRmFreqKHz* pTargetKHz);

/**
 * Sets "as is" specified slector clock configuration.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the selector clock description structure.
 * @param SourceId The ID of the input clock source to select.
 * @param Double If true, enable output doubler. If false, disable
 *  output doubler.
 * 
 * There is no error return status for this API call.
 * If specified source can not be selected(not present 
 * in core clock descriptor), asserts are encountered.
 */
void
NvRmPrivSelectorClockSet(
    NvRmDeviceHandle hRmDevice,
    const NvRmSelectorClockInfo* pCinfo,
    NvRmClockSource SourceId,
    NvBool Double);

/**
 * Parses clock sources configuration table of the given type.
 * 
 * @param pDst The pointer to the list of the clock source records the results
 *  of parsing are to be stored in. The records in this list are arranged in
 *  the order of source IDs.
 * @param DestinationTableSize Maximum number of sources that can be recorded.
 * @param The clock source configuration table to be parsed.
 * @param SourceTableSize Number of records to be parsed.
 * @param SourceType The type of source records to be parsed.
 */
void
NvRmPrivParseClockSources(
    NvRmClockSourceInfo* pDst,
    NvU32 DestinationTableSize,
    NvRmClockSourceInfoPtr Src,
    NvU32 SourceTableSize,
    NvRmClockSourceType SourceType);

/**
 * Gets pointer to the given clock source descriptor.
 * 
 * @param id The targeted clock source ID.
 * 
 * @return A pointer to the specified clock source descriptor.
 *  NULL is returned, if the target clock source is not valid.
 */
NvRmClockSourceInfo* NvRmPrivGetClockSourceHandle(NvRmClockSource id);

/**
 * Gets given clock source frequency,
 * 
 * @param id The targeted clock source ID.
 * 
 * @return Clock source frequency in KHz.
 */
NvRmFreqKHz NvRmPrivGetClockSourceFreq(NvRmClockSource id);

/**
 * Verifies if the specified clock source is currently selected
 * by the specified module.
 * 
 * @param hRmDevice The RM device handle.
 * @param SourceId The clock source ID to be verified. 
 * @param ModuleId The combined module id and instance of the module in question.
 * 
 * @return True if specified clock source is selected by the module;
 *  False returned, otherwise.
 */
NvBool
NvRmPrivIsSourceSelectedByModule(
    NvRmDeviceHandle hRmDevice,
    NvRmClockSource SourceId,
    NvRmModuleID ModuleId);

/**
 * Verifies if specified frequency range is reachable from the given
 *  clock source. 
 * 
 * @param SourceFreq Clock source frequency in KHz.
 * @param MinFreq Frequency range low boundary in KHz. 
 * @param MaxFreq Frequency range high boundary in KHz.
 * @param MaxDivisor Maximum possible source clock divisor.
 * 
 * @return True, if whole divisor can be found so that divided source
 *  frequency is within the range boundaries; False, otherwise.
 */
NvBool
NvRmIsFreqRangeReachable(
    NvRmFreqKHz SourceFreq,
    NvRmFreqKHz MinFreq,
    NvRmFreqKHz MaxFreq,
    NvU32 MaxDivisor);

/**
 * Reports if clock/voltage diagnostic is in progress for the specified module.
 * 
 * @param ModuleId The combined module id and instance of the module in question.
 *  If set to NvRmModuleID_Invalid reports if diagnostic is in progress for any
 *  module.
 * 
 * @return True, if clock/voltage diagnostic is in progress; False, otherwise.
 */
NvBool NvRmPrivIsDiagMode(NvRmModuleID ModuleId);

/**
 * Gets clock frequency limits for the specified SoC module.
 * 
 * @param ModuleId The targeted module ID.
 * 
 * @return The pointer to the clock limts structure for the given module ID.
 */
const NvRmModuleClockLimits* NvRmPrivGetSocClockLimits(NvRmModuleID Module);

/** 
 * Locks/Unclocks acces to shared PLL
 */
void NvRmPrivLockSharedPll(void);
void NvRmPrivUnlockSharedPll(void);

/**
 * Locks/Unclocks acces to module clock state
 */
void NvRmPrivLockModuleClockState(void);
void NvRmPrivUnlockModuleClockState(void);

/** 
 * Enable/Disable the clock source for the module. 
 *
 * @param hRmDevice The RM device handle.
 * @param ModuleId Module ID and instace information.
 * @param enbale Should the clock source be enabled or disabled.
 */
void
NvRmPrivConfigureClockSource(
        NvRmDeviceHandle hRmDevice, 
        NvRmModuleID ModuleId, 
        NvBool enable);

/** 
 * Gets pointers to clock descriptor and clock state for the given module.
 *
 * @param hDevice The RM device handle.
 * @param ModuleId Module ID and instance information.
 * @param CinfoOut A pointer to a variable that this function sets to the
 *  clock descriptor pointer.
 * @param StateOut A pointer to a variable that this function sets to the
 *  clock state pointer.
 * 
 * @retval NvSuccess if busy request completed successfully.
 * @retval NvError_NotSupported if no clock descriptor for the given module.
 * @retval NvError_ModuleNotPresent if the given module is not listed in
 *  relocation table.
 */
NvError
NvRmPrivGetClockState(
    NvRmDeviceHandle hDevice,
    NvRmModuleID ModuleId,
    NvRmModuleClockInfo** CinfoOut,
    NvRmModuleClockState** StateOut);

/**
 * Updates memory controller clock source reference counts.
 * 
 * @param hDevice The RM device handle.
 * @param pCinfo Pointer to the memory controller clock descriptor.
 * @param pCstate Pointer to the memory controller clock state.
 */
void
NvRmPrivMemoryClockReAttach(
    NvRmDeviceHandle hDevice,
    const NvRmModuleClockInfo* pCinfo,
    const NvRmModuleClockState* pCstate);

/**
 * Updates generic module clock source reference counts.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the targeted module clock descriptor.
 * @param pCstate Pointer to the targeted module clock state.
 */
void
NvRmPrivModuleClockReAttach(
    NvRmDeviceHandle hRmDevice,
    const NvRmModuleClockInfo* cinfo,
    const NvRmModuleClockState* state);

/**
 * Updates external clock source references.
 * 
 * @param hDevice The RM device handle.
 * @param SourceId The external clock source ID.
 * @param Enable NV_TRUE if external clock is enabled;
 *  NV_FALSE if external clock is disabled.
 */
void
NvRmPrivExternalClockAttach( 
    NvRmDeviceHandle hDevice,
    NvRmClockSource SourceId,
    NvBool Enable);

/**
 * Updates PLL attachment reference count and PLL stop flag in the storage
 *  shared by RM and NV boot loader.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param pPllRef Pointer to the PLL references record.
 * @param Increment If NV_TRUE, increment PLL reference count,
 *  if NV_FALSE, decrement PLL reference count.
 */
void
NvRmPrivPllRefUpdate(
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmPllReference* pPllRef,
    NvBool Increment);

/**
 * Verifies if the targeted module is prohibited to use the specified clock
 *  source per clock manager policy.
 * 
 * @param hRmDevice The RM device handle.
 * @param Module Target module ID.
 * @param SourceId Clock source ID.
 * 
 * @return NV_TRUE if the targeted module is prohibited to use the specified
 *  clock source; NV_FALSE if the targeted module can use the specified clock
 *  source.
 */
NvBool
NvRmPrivIsSourceProtected(
    NvRmDeviceHandle hRmDevice,
    NvRmModuleID Module,
    NvRmClockSource SourceId);

/**
 * Gets maximum avilable clock source frequency for the specified module
 *  per clock manager policy.
 * 
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the targeted module clock descriptor.
 * 
 * @return Source frequency in kHz.
 */
NvRmFreqKHz
NvRmPrivModuleGetMaxSrcKHz(
    NvRmDeviceHandle hRmDevice,
    const NvRmModuleClockInfo* pCinfo);

/**
 * Similar to the Rm pulbic Module reset API, but have the option of either
 * pulsing or keeping the reset line active.
 *
 * @param hold  if NV_TRUE keep the asserting the reset. If the value is
 * NV_FALSE pulse a reset to the hardware module.
 *
 */
void 
NvRmPrivModuleReset(NvRmDeviceHandle hDevice, NvRmModuleID ModuleId, NvBool hold);

/**
 * Updates voltage scaling references, when the specified module clock
 * is enabled, or disabled.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param pCinfo Pointer to the targeted module clock descriptor.
 * @param pCstate Pointer to the targeted module clock state.
 * @param Enable NV_TRUE if module clock is about to be enabled;
 *  NV_FALSE if module clock has just been disabled.
 * @param Preview NV_TRUE if scaling references should be preserved when
 *  voltage increase is required, NV_FALSE if scaling references should
 *  be updated in any case.
 * 
 * @return Core voltage level in mV required for the new module configuration.
 *  NvRmVoltsUnspecified is returned if module clock can be enabled without
 *  changing voltage requirements. NvRmVoltsOff is returned when module clock
 *  is disabled.
 */
NvRmMilliVolts
NvRmPrivModuleVscaleAttach(
    NvRmDeviceHandle hRmDevice,
    const NvRmModuleClockInfo* pCinfo,
    NvRmModuleClockState* pCstate,
    NvBool Enable,
    NvBool Preview);

/**
 * Updates voltage scaling references, when the clock frequency for the
 * specified module is re-configured.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param pCinfo Pointer to the targeted module clock descriptor.
 * @param pCstate Pointer to the targeted module clock state.
 * @param TargetModuleKHz Traget module frequency in kHz.
 * @param TargetSrcKHz Clock source frequency for the traget module in kHz.
 * @param Preview NV_TRUE if scaling references should be preserved when
 *  voltage increase is required, NV_FALSE if scaling references should
 *  be updated in any case.
 *
 * @return Core voltage level in mV required for new module configuration.
 *  NvRmVoltsUnspecified is returned if all specified frequencies can be
 *  configured without changing voltage requirements. NvRmVoltsOff is returned
 *  if new configuration may lower voltage requirements.
 */
NvRmMilliVolts
NvRmPrivModuleVscaleReAttach(
    NvRmDeviceHandle hRmDevice,
    const NvRmModuleClockInfo* pCinfo,
    NvRmModuleClockState* pCstate,
    NvRmFreqKHz TargetModuleKHz,
    NvRmFreqKHz TargetSrcKHz,
    NvBool Preview);

/**
 * Updates target level, and reference count for pending voltage scaling
 * transactions.
 *
 * @param hRmDevice The RM device handle.
 * @param Set PendingMv pending transaction target; NvRmVoltsOff is used
 *  to indicate completed transaction.
 *
 */
void NvRmPrivModuleVscaleSetPending(
    NvRmDeviceHandle hRmDevice,
    NvRmMilliVolts PendingMv);

/**
 * Sets voltage scaling attribute for the specified module clock.
 * 
 * @param hRmDeviceHandle The RM device handle.
 * @param pCinfo Pointer to the targeted module clock descriptor.
 * @param pCstate Pointer to the targeted module clock state, which is updated
 *  by this function. 
 * 
 * @note The scaling attribute in the clock state structure is set NV_FALSE for
 *  all core clocks (CPU, AVP, system buses, memory). For modules designated
 *  clocks it is set NV_FALSE if any frequency within module clock limits can
 *  be selected at any core voltage level within SoC operational range.
 *  Otherwise, the attribute is set NV_TRUE.
 */
void
NvRmPrivModuleSetScalingAttribute(
    NvRmDeviceHandle hRmDevice,
    const NvRmModuleClockInfo* pCinfo,
    NvRmModuleClockState* pCstate);

/**
 * Sets "as is" module clock configuration as specified by the given
 * clock state structure.
 *
 * @param hRmDevice The RM device handle.
 * @param pCinfo Pointer to the targeted module clock descriptor.
 * @param pCstate Pointer to the targeted module clock state to be set
 *  by this function. 
 */
void
NvRmPrivModuleClockSet(
    NvRmDeviceHandle hRmDevice,
    const NvRmModuleClockInfo* pCinfo,
    const NvRmModuleClockState* pCstate);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  // INCLUDED_NVRM_CLOCKS_H
