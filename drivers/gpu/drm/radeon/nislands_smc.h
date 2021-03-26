/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
#ifndef __NISLANDS_SMC_H__
#define __NISLANDS_SMC_H__

#pragma pack(push, 1)

#define NISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE 16

struct PP_NIslands_Dpm2PerfLevel
{
    uint8_t     MaxPS;
    uint8_t     TgtAct;
    uint8_t     MaxPS_StepInc;
    uint8_t     MaxPS_StepDec;
    uint8_t     PSST;
    uint8_t     NearTDPDec;
    uint8_t     AboveSafeInc;
    uint8_t     BelowSafeInc;
    uint8_t     PSDeltaLimit;
    uint8_t     PSDeltaWin;
    uint8_t     Reserved[6];
};

typedef struct PP_NIslands_Dpm2PerfLevel PP_NIslands_Dpm2PerfLevel;

struct PP_NIslands_DPM2Parameters
{
    uint32_t    TDPLimit;
    uint32_t    NearTDPLimit;
    uint32_t    SafePowerLimit;
    uint32_t    PowerBoostLimit;
};
typedef struct PP_NIslands_DPM2Parameters PP_NIslands_DPM2Parameters;

struct NISLANDS_SMC_SCLK_VALUE
{
    uint32_t        vCG_SPLL_FUNC_CNTL;
    uint32_t        vCG_SPLL_FUNC_CNTL_2;
    uint32_t        vCG_SPLL_FUNC_CNTL_3;
    uint32_t        vCG_SPLL_FUNC_CNTL_4;
    uint32_t        vCG_SPLL_SPREAD_SPECTRUM;
    uint32_t        vCG_SPLL_SPREAD_SPECTRUM_2;
    uint32_t        sclk_value;
};

typedef struct NISLANDS_SMC_SCLK_VALUE NISLANDS_SMC_SCLK_VALUE;

struct NISLANDS_SMC_MCLK_VALUE
{
    uint32_t        vMPLL_FUNC_CNTL;
    uint32_t        vMPLL_FUNC_CNTL_1;
    uint32_t        vMPLL_FUNC_CNTL_2;
    uint32_t        vMPLL_AD_FUNC_CNTL;
    uint32_t        vMPLL_AD_FUNC_CNTL_2;
    uint32_t        vMPLL_DQ_FUNC_CNTL;
    uint32_t        vMPLL_DQ_FUNC_CNTL_2;
    uint32_t        vMCLK_PWRMGT_CNTL;
    uint32_t        vDLL_CNTL;
    uint32_t        vMPLL_SS;
    uint32_t        vMPLL_SS2;
    uint32_t        mclk_value;
};

typedef struct NISLANDS_SMC_MCLK_VALUE NISLANDS_SMC_MCLK_VALUE;

struct NISLANDS_SMC_VOLTAGE_VALUE
{
    uint16_t             value;
    uint8_t              index;
    uint8_t              padding;
};

typedef struct NISLANDS_SMC_VOLTAGE_VALUE NISLANDS_SMC_VOLTAGE_VALUE;

struct NISLANDS_SMC_HW_PERFORMANCE_LEVEL
{
    uint8_t                     arbValue;
    uint8_t                     ACIndex;
    uint8_t                     displayWatermark;
    uint8_t                     gen2PCIE;
    uint8_t                     reserved1;
    uint8_t                     reserved2;
    uint8_t                     strobeMode;
    uint8_t                     mcFlags;
    uint32_t                    aT;
    uint32_t                    bSP;
    NISLANDS_SMC_SCLK_VALUE     sclk;
    NISLANDS_SMC_MCLK_VALUE     mclk;
    NISLANDS_SMC_VOLTAGE_VALUE  vddc;
    NISLANDS_SMC_VOLTAGE_VALUE  mvdd;
    NISLANDS_SMC_VOLTAGE_VALUE  vddci;
    NISLANDS_SMC_VOLTAGE_VALUE  std_vddc;
    uint32_t                    powergate_en;
    uint8_t                     hUp;
    uint8_t                     hDown;
    uint8_t                     stateFlags;
    uint8_t                     arbRefreshState;
    uint32_t                    SQPowerThrottle;
    uint32_t                    SQPowerThrottle_2;
    uint32_t                    reserved[2];
    PP_NIslands_Dpm2PerfLevel   dpm2;
};

#define NISLANDS_SMC_STROBE_RATIO    0x0F
#define NISLANDS_SMC_STROBE_ENABLE   0x10

#define NISLANDS_SMC_MC_EDC_RD_FLAG  0x01
#define NISLANDS_SMC_MC_EDC_WR_FLAG  0x02
#define NISLANDS_SMC_MC_RTT_ENABLE   0x04
#define NISLANDS_SMC_MC_STUTTER_EN   0x08

typedef struct NISLANDS_SMC_HW_PERFORMANCE_LEVEL NISLANDS_SMC_HW_PERFORMANCE_LEVEL;

struct NISLANDS_SMC_SWSTATE
{
	uint8_t                             flags;
	uint8_t                             levelCount;
	uint8_t                             padding2;
	uint8_t                             padding3;
	NISLANDS_SMC_HW_PERFORMANCE_LEVEL   levels[];
};

typedef struct NISLANDS_SMC_SWSTATE NISLANDS_SMC_SWSTATE;

#define NISLANDS_SMC_VOLTAGEMASK_VDDC  0
#define NISLANDS_SMC_VOLTAGEMASK_MVDD  1
#define NISLANDS_SMC_VOLTAGEMASK_VDDCI 2
#define NISLANDS_SMC_VOLTAGEMASK_MAX   4

struct NISLANDS_SMC_VOLTAGEMASKTABLE
{
    uint8_t  highMask[NISLANDS_SMC_VOLTAGEMASK_MAX];
    uint32_t lowMask[NISLANDS_SMC_VOLTAGEMASK_MAX];
};

typedef struct NISLANDS_SMC_VOLTAGEMASKTABLE NISLANDS_SMC_VOLTAGEMASKTABLE;

#define NISLANDS_MAX_NO_VREG_STEPS 32

struct NISLANDS_SMC_STATETABLE
{
    uint8_t                             thermalProtectType;
    uint8_t                             systemFlags;
    uint8_t                             maxVDDCIndexInPPTable;
    uint8_t                             extraFlags;
    uint8_t                             highSMIO[NISLANDS_MAX_NO_VREG_STEPS];
    uint32_t                            lowSMIO[NISLANDS_MAX_NO_VREG_STEPS];
    NISLANDS_SMC_VOLTAGEMASKTABLE       voltageMaskTable;
    PP_NIslands_DPM2Parameters          dpm2Params;
    NISLANDS_SMC_SWSTATE                initialState;
    NISLANDS_SMC_SWSTATE                ACPIState;
    NISLANDS_SMC_SWSTATE                ULVState;
    NISLANDS_SMC_SWSTATE                driverState;
    NISLANDS_SMC_HW_PERFORMANCE_LEVEL   dpmLevels[NISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE - 1];
};

typedef struct NISLANDS_SMC_STATETABLE NISLANDS_SMC_STATETABLE;

#define NI_SMC_SOFT_REGISTERS_START        0x108

#define NI_SMC_SOFT_REGISTER_mclk_chg_timeout        0x0
#define NI_SMC_SOFT_REGISTER_delay_bbias             0xC
#define NI_SMC_SOFT_REGISTER_delay_vreg              0x10
#define NI_SMC_SOFT_REGISTER_delay_acpi              0x2C
#define NI_SMC_SOFT_REGISTER_seq_index               0x64
#define NI_SMC_SOFT_REGISTER_mvdd_chg_time           0x68
#define NI_SMC_SOFT_REGISTER_mclk_switch_lim         0x78
#define NI_SMC_SOFT_REGISTER_watermark_threshold     0x80
#define NI_SMC_SOFT_REGISTER_mc_block_delay          0x84
#define NI_SMC_SOFT_REGISTER_uvd_enabled             0x98

#define SMC_NISLANDS_MC_TPP_CAC_NUM_OF_ENTRIES 16
#define SMC_NISLANDS_LKGE_LUT_NUM_OF_TEMP_ENTRIES 16
#define SMC_NISLANDS_LKGE_LUT_NUM_OF_VOLT_ENTRIES 16
#define SMC_NISLANDS_BIF_LUT_NUM_OF_ENTRIES 4

struct SMC_NISLANDS_MC_TPP_CAC_TABLE
{
    uint32_t    tpp[SMC_NISLANDS_MC_TPP_CAC_NUM_OF_ENTRIES];
    uint32_t    cacValue[SMC_NISLANDS_MC_TPP_CAC_NUM_OF_ENTRIES];
};

typedef struct SMC_NISLANDS_MC_TPP_CAC_TABLE SMC_NISLANDS_MC_TPP_CAC_TABLE;


struct PP_NIslands_CACTABLES
{
    uint32_t                cac_bif_lut[SMC_NISLANDS_BIF_LUT_NUM_OF_ENTRIES];
    uint32_t                cac_lkge_lut[SMC_NISLANDS_LKGE_LUT_NUM_OF_TEMP_ENTRIES][SMC_NISLANDS_LKGE_LUT_NUM_OF_VOLT_ENTRIES];

    uint32_t                pwr_const;

    uint32_t                dc_cacValue;
    uint32_t                bif_cacValue;
    uint32_t                lkge_pwr;

    uint8_t                 cac_width;
    uint8_t                 window_size_p2;

    uint8_t                 num_drop_lsb;
    uint8_t                 padding_0;

    uint32_t                last_power;

    uint8_t                 AllowOvrflw;
    uint8_t                 MCWrWeight;
    uint8_t                 MCRdWeight;
    uint8_t                 padding_1[9];

    uint8_t                 enableWinAvg;
    uint8_t                 numWin_TDP;
    uint8_t                 l2numWin_TDP;
    uint8_t                 WinIndex;

    uint32_t                dynPwr_TDP[4];
    uint32_t                lkgePwr_TDP[4];
    uint32_t                power_TDP[4];
    uint32_t                avg_dynPwr_TDP;
    uint32_t                avg_lkgePwr_TDP;
    uint32_t                avg_power_TDP;
    uint32_t                lts_power_TDP;
    uint8_t                 lts_truncate_n;
    uint8_t                 padding_2[7];
};

typedef struct PP_NIslands_CACTABLES PP_NIslands_CACTABLES;

#define SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE 32
#define SMC_NISLANDS_MC_REGISTER_ARRAY_SET_COUNT 20

struct SMC_NIslands_MCRegisterAddress
{
    uint16_t s0;
    uint16_t s1;
};

typedef struct SMC_NIslands_MCRegisterAddress SMC_NIslands_MCRegisterAddress;


struct SMC_NIslands_MCRegisterSet
{
    uint32_t value[SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE];
};

typedef struct SMC_NIslands_MCRegisterSet SMC_NIslands_MCRegisterSet;

struct SMC_NIslands_MCRegisters
{
    uint8_t                             last;
    uint8_t                             reserved[3];
    SMC_NIslands_MCRegisterAddress      address[SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE];
    SMC_NIslands_MCRegisterSet          data[SMC_NISLANDS_MC_REGISTER_ARRAY_SET_COUNT];
};

typedef struct SMC_NIslands_MCRegisters SMC_NIslands_MCRegisters;

struct SMC_NIslands_MCArbDramTimingRegisterSet
{
    uint32_t mc_arb_dram_timing;
    uint32_t mc_arb_dram_timing2;
    uint8_t  mc_arb_rfsh_rate;
    uint8_t  padding[3];
};

typedef struct SMC_NIslands_MCArbDramTimingRegisterSet SMC_NIslands_MCArbDramTimingRegisterSet;

struct SMC_NIslands_MCArbDramTimingRegisters
{
    uint8_t                                     arb_current;
    uint8_t                                     reserved[3];
    SMC_NIslands_MCArbDramTimingRegisterSet     data[20];
};

typedef struct SMC_NIslands_MCArbDramTimingRegisters SMC_NIslands_MCArbDramTimingRegisters;

struct SMC_NISLANDS_SPLL_DIV_TABLE
{
    uint32_t    freq[256];
    uint32_t    ss[256];
};

#define SMC_NISLANDS_SPLL_DIV_TABLE_FBDIV_MASK  0x01ffffff
#define SMC_NISLANDS_SPLL_DIV_TABLE_FBDIV_SHIFT 0
#define SMC_NISLANDS_SPLL_DIV_TABLE_PDIV_MASK   0xfe000000
#define SMC_NISLANDS_SPLL_DIV_TABLE_PDIV_SHIFT  25
#define SMC_NISLANDS_SPLL_DIV_TABLE_CLKV_MASK   0x000fffff
#define SMC_NISLANDS_SPLL_DIV_TABLE_CLKV_SHIFT  0
#define SMC_NISLANDS_SPLL_DIV_TABLE_CLKS_MASK   0xfff00000
#define SMC_NISLANDS_SPLL_DIV_TABLE_CLKS_SHIFT  20

typedef struct SMC_NISLANDS_SPLL_DIV_TABLE SMC_NISLANDS_SPLL_DIV_TABLE;

#define NISLANDS_SMC_FIRMWARE_HEADER_LOCATION 0x100

#define NISLANDS_SMC_FIRMWARE_HEADER_version                   0x0
#define NISLANDS_SMC_FIRMWARE_HEADER_flags                     0x4
#define NISLANDS_SMC_FIRMWARE_HEADER_softRegisters             0x8
#define NISLANDS_SMC_FIRMWARE_HEADER_stateTable                0xC
#define NISLANDS_SMC_FIRMWARE_HEADER_fanTable                  0x10
#define NISLANDS_SMC_FIRMWARE_HEADER_cacTable                  0x14
#define NISLANDS_SMC_FIRMWARE_HEADER_mcRegisterTable           0x20
#define NISLANDS_SMC_FIRMWARE_HEADER_mcArbDramAutoRefreshTable 0x2C
#define NISLANDS_SMC_FIRMWARE_HEADER_spllTable                 0x30

#pragma pack(pop)

#endif

