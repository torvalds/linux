/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef PP_SISLANDS_SMC_H
#define PP_SISLANDS_SMC_H

#include "ppsmc.h"

#pragma pack(push, 1)

#define SISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE 16

struct PP_SIslands_Dpm2PerfLevel
{
    uint8_t MaxPS;
    uint8_t TgtAct;
    uint8_t MaxPS_StepInc;
    uint8_t MaxPS_StepDec;
    uint8_t PSSamplingTime;
    uint8_t NearTDPDec;
    uint8_t AboveSafeInc;
    uint8_t BelowSafeInc;
    uint8_t PSDeltaLimit;
    uint8_t PSDeltaWin;
    uint16_t PwrEfficiencyRatio;
    uint8_t Reserved[4];
};

typedef struct PP_SIslands_Dpm2PerfLevel PP_SIslands_Dpm2PerfLevel;

struct PP_SIslands_DPM2Status
{
    uint32_t    dpm2Flags;
    uint8_t     CurrPSkip;
    uint8_t     CurrPSkipPowerShift;
    uint8_t     CurrPSkipTDP;
    uint8_t     CurrPSkipOCP;
    uint8_t     MaxSPLLIndex;
    uint8_t     MinSPLLIndex;
    uint8_t     CurrSPLLIndex;
    uint8_t     InfSweepMode;
    uint8_t     InfSweepDir;
    uint8_t     TDPexceeded;
    uint8_t     reserved;
    uint8_t     SwitchDownThreshold;
    uint32_t    SwitchDownCounter;
    uint32_t    SysScalingFactor;
};

typedef struct PP_SIslands_DPM2Status PP_SIslands_DPM2Status;

struct PP_SIslands_DPM2Parameters
{
    uint32_t    TDPLimit;
    uint32_t    NearTDPLimit;
    uint32_t    SafePowerLimit;
    uint32_t    PowerBoostLimit;
    uint32_t    MinLimitDelta;
};
typedef struct PP_SIslands_DPM2Parameters PP_SIslands_DPM2Parameters;

struct PP_SIslands_PAPMStatus
{
    uint32_t    EstimatedDGPU_T;
    uint32_t    EstimatedDGPU_P;
    uint32_t    EstimatedAPU_T;
    uint32_t    EstimatedAPU_P;
    uint8_t     dGPU_T_Limit_Exceeded;
    uint8_t     reserved[3];
};
typedef struct PP_SIslands_PAPMStatus PP_SIslands_PAPMStatus;

struct PP_SIslands_PAPMParameters
{
    uint32_t    NearTDPLimitTherm;
    uint32_t    NearTDPLimitPAPM;
    uint32_t    PlatformPowerLimit;
    uint32_t    dGPU_T_Limit;
    uint32_t    dGPU_T_Warning;
    uint32_t    dGPU_T_Hysteresis;
};
typedef struct PP_SIslands_PAPMParameters PP_SIslands_PAPMParameters;

struct SISLANDS_SMC_SCLK_VALUE
{
    uint32_t    vCG_SPLL_FUNC_CNTL;
    uint32_t    vCG_SPLL_FUNC_CNTL_2;
    uint32_t    vCG_SPLL_FUNC_CNTL_3;
    uint32_t    vCG_SPLL_FUNC_CNTL_4;
    uint32_t    vCG_SPLL_SPREAD_SPECTRUM;
    uint32_t    vCG_SPLL_SPREAD_SPECTRUM_2;
    uint32_t    sclk_value;
};

typedef struct SISLANDS_SMC_SCLK_VALUE SISLANDS_SMC_SCLK_VALUE;

struct SISLANDS_SMC_MCLK_VALUE
{
    uint32_t    vMPLL_FUNC_CNTL;
    uint32_t    vMPLL_FUNC_CNTL_1;
    uint32_t    vMPLL_FUNC_CNTL_2;
    uint32_t    vMPLL_AD_FUNC_CNTL;
    uint32_t    vMPLL_DQ_FUNC_CNTL;
    uint32_t    vMCLK_PWRMGT_CNTL;
    uint32_t    vDLL_CNTL;
    uint32_t    vMPLL_SS;
    uint32_t    vMPLL_SS2;
    uint32_t    mclk_value;
};

typedef struct SISLANDS_SMC_MCLK_VALUE SISLANDS_SMC_MCLK_VALUE;

struct SISLANDS_SMC_VOLTAGE_VALUE
{
    uint16_t    value;
    uint8_t     index;
    uint8_t     phase_settings;
};

typedef struct SISLANDS_SMC_VOLTAGE_VALUE SISLANDS_SMC_VOLTAGE_VALUE;

struct SISLANDS_SMC_HW_PERFORMANCE_LEVEL
{
    uint8_t                     ACIndex;
    uint8_t                     displayWatermark;
    uint8_t                     gen2PCIE;
    uint8_t                     UVDWatermark;
    uint8_t                     VCEWatermark;
    uint8_t                     strobeMode;
    uint8_t                     mcFlags;
    uint8_t                     padding;
    uint32_t                    aT;
    uint32_t                    bSP;
    SISLANDS_SMC_SCLK_VALUE     sclk;
    SISLANDS_SMC_MCLK_VALUE     mclk;
    SISLANDS_SMC_VOLTAGE_VALUE  vddc;
    SISLANDS_SMC_VOLTAGE_VALUE  mvdd;
    SISLANDS_SMC_VOLTAGE_VALUE  vddci;
    SISLANDS_SMC_VOLTAGE_VALUE  std_vddc;
    uint8_t                     hysteresisUp;
    uint8_t                     hysteresisDown;
    uint8_t                     stateFlags;
    uint8_t                     arbRefreshState;
    uint32_t                    SQPowerThrottle;
    uint32_t                    SQPowerThrottle_2;
    uint32_t                    MaxPoweredUpCU;
    SISLANDS_SMC_VOLTAGE_VALUE  high_temp_vddc;
    SISLANDS_SMC_VOLTAGE_VALUE  low_temp_vddc;
    uint32_t                    reserved[2];
    PP_SIslands_Dpm2PerfLevel   dpm2;
};

#define SISLANDS_SMC_STROBE_RATIO    0x0F
#define SISLANDS_SMC_STROBE_ENABLE   0x10

#define SISLANDS_SMC_MC_EDC_RD_FLAG  0x01
#define SISLANDS_SMC_MC_EDC_WR_FLAG  0x02
#define SISLANDS_SMC_MC_RTT_ENABLE   0x04
#define SISLANDS_SMC_MC_STUTTER_EN   0x08
#define SISLANDS_SMC_MC_PG_EN        0x10

typedef struct SISLANDS_SMC_HW_PERFORMANCE_LEVEL SISLANDS_SMC_HW_PERFORMANCE_LEVEL;

struct SISLANDS_SMC_SWSTATE
{
    uint8_t                             flags;
    uint8_t                             levelCount;
    uint8_t                             padding2;
    uint8_t                             padding3;
    SISLANDS_SMC_HW_PERFORMANCE_LEVEL   levels[1];
};

typedef struct SISLANDS_SMC_SWSTATE SISLANDS_SMC_SWSTATE;

#define SISLANDS_SMC_VOLTAGEMASK_VDDC  0
#define SISLANDS_SMC_VOLTAGEMASK_MVDD  1
#define SISLANDS_SMC_VOLTAGEMASK_VDDCI 2
#define SISLANDS_SMC_VOLTAGEMASK_MAX   4

struct SISLANDS_SMC_VOLTAGEMASKTABLE
{
    uint32_t lowMask[SISLANDS_SMC_VOLTAGEMASK_MAX];
};

typedef struct SISLANDS_SMC_VOLTAGEMASKTABLE SISLANDS_SMC_VOLTAGEMASKTABLE;

#define SISLANDS_MAX_NO_VREG_STEPS 32

struct SISLANDS_SMC_STATETABLE
{
    uint8_t                             thermalProtectType;
    uint8_t                             systemFlags;
    uint8_t                             maxVDDCIndexInPPTable;
    uint8_t                             extraFlags;
    uint32_t                            lowSMIO[SISLANDS_MAX_NO_VREG_STEPS];
    SISLANDS_SMC_VOLTAGEMASKTABLE       voltageMaskTable;
    SISLANDS_SMC_VOLTAGEMASKTABLE       phaseMaskTable;
    PP_SIslands_DPM2Parameters          dpm2Params;
    SISLANDS_SMC_SWSTATE                initialState;
    SISLANDS_SMC_SWSTATE                ACPIState;
    SISLANDS_SMC_SWSTATE                ULVState;
    SISLANDS_SMC_SWSTATE                driverState;
    SISLANDS_SMC_HW_PERFORMANCE_LEVEL   dpmLevels[SISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE - 1];
};

typedef struct SISLANDS_SMC_STATETABLE SISLANDS_SMC_STATETABLE;

#define SI_SMC_SOFT_REGISTER_mclk_chg_timeout         0x0
#define SI_SMC_SOFT_REGISTER_delay_vreg               0xC
#define SI_SMC_SOFT_REGISTER_delay_acpi               0x28
#define SI_SMC_SOFT_REGISTER_seq_index                0x5C
#define SI_SMC_SOFT_REGISTER_mvdd_chg_time            0x60
#define SI_SMC_SOFT_REGISTER_mclk_switch_lim          0x70
#define SI_SMC_SOFT_REGISTER_watermark_threshold      0x78
#define SI_SMC_SOFT_REGISTER_phase_shedding_delay     0x88
#define SI_SMC_SOFT_REGISTER_ulv_volt_change_delay    0x8C
#define SI_SMC_SOFT_REGISTER_mc_block_delay           0x98
#define SI_SMC_SOFT_REGISTER_ticks_per_us             0xA8
#define SI_SMC_SOFT_REGISTER_crtc_index               0xC4
#define SI_SMC_SOFT_REGISTER_mclk_change_block_cp_min 0xC8
#define SI_SMC_SOFT_REGISTER_mclk_change_block_cp_max 0xCC
#define SI_SMC_SOFT_REGISTER_non_ulv_pcie_link_width  0xF4
#define SI_SMC_SOFT_REGISTER_tdr_is_about_to_happen   0xFC
#define SI_SMC_SOFT_REGISTER_vr_hot_gpio              0x100

#define SMC_SISLANDS_LKGE_LUT_NUM_OF_TEMP_ENTRIES 16
#define SMC_SISLANDS_LKGE_LUT_NUM_OF_VOLT_ENTRIES 32

#define SMC_SISLANDS_SCALE_I  7
#define SMC_SISLANDS_SCALE_R 12

struct PP_SIslands_CacConfig
{
    uint16_t   cac_lkge_lut[SMC_SISLANDS_LKGE_LUT_NUM_OF_TEMP_ENTRIES][SMC_SISLANDS_LKGE_LUT_NUM_OF_VOLT_ENTRIES];
    uint32_t   lkge_lut_V0;
    uint32_t   lkge_lut_Vstep;
    uint32_t   WinTime;
    uint32_t   R_LL;
    uint32_t   calculation_repeats;
    uint32_t   l2numWin_TDP;
    uint32_t   dc_cac;
    uint8_t    lts_truncate_n;
    uint8_t    SHIFT_N;
    uint8_t    log2_PG_LKG_SCALE;
    uint8_t    cac_temp;
    uint32_t   lkge_lut_T0;
    uint32_t   lkge_lut_Tstep;
};

typedef struct PP_SIslands_CacConfig PP_SIslands_CacConfig;

#define SMC_SISLANDS_MC_REGISTER_ARRAY_SIZE 16
#define SMC_SISLANDS_MC_REGISTER_ARRAY_SET_COUNT 20

struct SMC_SIslands_MCRegisterAddress
{
    uint16_t s0;
    uint16_t s1;
};

typedef struct SMC_SIslands_MCRegisterAddress SMC_SIslands_MCRegisterAddress;

struct SMC_SIslands_MCRegisterSet
{
    uint32_t value[SMC_SISLANDS_MC_REGISTER_ARRAY_SIZE];
};

typedef struct SMC_SIslands_MCRegisterSet SMC_SIslands_MCRegisterSet;

struct SMC_SIslands_MCRegisters
{
    uint8_t                             last;
    uint8_t                             reserved[3];
    SMC_SIslands_MCRegisterAddress      address[SMC_SISLANDS_MC_REGISTER_ARRAY_SIZE];
    SMC_SIslands_MCRegisterSet          data[SMC_SISLANDS_MC_REGISTER_ARRAY_SET_COUNT];
};

typedef struct SMC_SIslands_MCRegisters SMC_SIslands_MCRegisters;

struct SMC_SIslands_MCArbDramTimingRegisterSet
{
    uint32_t mc_arb_dram_timing;
    uint32_t mc_arb_dram_timing2;
    uint8_t  mc_arb_rfsh_rate;
    uint8_t  mc_arb_burst_time;
    uint8_t  padding[2];
};

typedef struct SMC_SIslands_MCArbDramTimingRegisterSet SMC_SIslands_MCArbDramTimingRegisterSet;

struct SMC_SIslands_MCArbDramTimingRegisters
{
    uint8_t                                     arb_current;
    uint8_t                                     reserved[3];
    SMC_SIslands_MCArbDramTimingRegisterSet     data[16];
};

typedef struct SMC_SIslands_MCArbDramTimingRegisters SMC_SIslands_MCArbDramTimingRegisters;

struct SMC_SISLANDS_SPLL_DIV_TABLE
{
    uint32_t    freq[256];
    uint32_t    ss[256];
};

#define SMC_SISLANDS_SPLL_DIV_TABLE_FBDIV_MASK  0x01ffffff
#define SMC_SISLANDS_SPLL_DIV_TABLE_FBDIV_SHIFT 0
#define SMC_SISLANDS_SPLL_DIV_TABLE_PDIV_MASK   0xfe000000
#define SMC_SISLANDS_SPLL_DIV_TABLE_PDIV_SHIFT  25
#define SMC_SISLANDS_SPLL_DIV_TABLE_CLKV_MASK   0x000fffff
#define SMC_SISLANDS_SPLL_DIV_TABLE_CLKV_SHIFT  0
#define SMC_SISLANDS_SPLL_DIV_TABLE_CLKS_MASK   0xfff00000
#define SMC_SISLANDS_SPLL_DIV_TABLE_CLKS_SHIFT  20

typedef struct SMC_SISLANDS_SPLL_DIV_TABLE SMC_SISLANDS_SPLL_DIV_TABLE;

#define SMC_SISLANDS_DTE_MAX_FILTER_STAGES 5

#define SMC_SISLANDS_DTE_MAX_TEMPERATURE_DEPENDENT_ARRAY_SIZE 16

struct Smc_SIslands_DTE_Configuration
{
    uint32_t tau[SMC_SISLANDS_DTE_MAX_FILTER_STAGES];
    uint32_t R[SMC_SISLANDS_DTE_MAX_FILTER_STAGES];
    uint32_t K;
    uint32_t T0;
    uint32_t MaxT;
    uint8_t  WindowSize;
    uint8_t  Tdep_count;
    uint8_t  temp_select;
    uint8_t  DTE_mode;
    uint8_t  T_limits[SMC_SISLANDS_DTE_MAX_TEMPERATURE_DEPENDENT_ARRAY_SIZE];
    uint32_t Tdep_tau[SMC_SISLANDS_DTE_MAX_TEMPERATURE_DEPENDENT_ARRAY_SIZE];
    uint32_t Tdep_R[SMC_SISLANDS_DTE_MAX_TEMPERATURE_DEPENDENT_ARRAY_SIZE];
    uint32_t Tthreshold;
};

typedef struct Smc_SIslands_DTE_Configuration Smc_SIslands_DTE_Configuration;

#define SMC_SISLANDS_DTE_STATUS_FLAG_DTE_ON 1

#define SISLANDS_SMC_FIRMWARE_HEADER_LOCATION 0x10000

#define SISLANDS_SMC_FIRMWARE_HEADER_version                   0x0
#define SISLANDS_SMC_FIRMWARE_HEADER_flags                     0x4
#define SISLANDS_SMC_FIRMWARE_HEADER_softRegisters             0xC
#define SISLANDS_SMC_FIRMWARE_HEADER_stateTable                0x10
#define SISLANDS_SMC_FIRMWARE_HEADER_fanTable                  0x14
#define SISLANDS_SMC_FIRMWARE_HEADER_CacConfigTable            0x18
#define SISLANDS_SMC_FIRMWARE_HEADER_mcRegisterTable           0x24
#define SISLANDS_SMC_FIRMWARE_HEADER_mcArbDramAutoRefreshTable 0x30
#define SISLANDS_SMC_FIRMWARE_HEADER_spllTable                 0x38
#define SISLANDS_SMC_FIRMWARE_HEADER_DteConfiguration          0x40
#define SISLANDS_SMC_FIRMWARE_HEADER_PAPMParameters            0x48

#pragma pack(pop)

int si_copy_bytes_to_smc(struct radeon_device *rdev,
			 u32 smc_start_address,
			 const u8 *src, u32 byte_count, u32 limit);
void si_start_smc(struct radeon_device *rdev);
void si_reset_smc(struct radeon_device *rdev);
int si_program_jump_on_start(struct radeon_device *rdev);
void si_stop_smc_clock(struct radeon_device *rdev);
void si_start_smc_clock(struct radeon_device *rdev);
bool si_is_smc_running(struct radeon_device *rdev);
PPSMC_Result si_send_msg_to_smc(struct radeon_device *rdev, PPSMC_Msg msg);
PPSMC_Result si_wait_for_smc_inactive(struct radeon_device *rdev);
int si_load_smc_ucode(struct radeon_device *rdev, u32 limit);
int si_read_smc_sram_dword(struct radeon_device *rdev, u32 smc_address,
			   u32 *value, u32 limit);
int si_write_smc_sram_dword(struct radeon_device *rdev, u32 smc_address,
			    u32 value, u32 limit);

#endif

