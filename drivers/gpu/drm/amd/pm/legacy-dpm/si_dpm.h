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
#ifndef __SI_DPM_H__
#define __SI_DPM_H__

#include "amdgpu_atombios.h"
#include "sislands_smc.h"

#define MC_CG_CONFIG                                    0x96f
#define MC_ARB_CG                                       0x9fa
#define		CG_ARB_REQ(x)				((x) << 0)
#define		CG_ARB_REQ_MASK				(0xff << 0)

#define	MC_ARB_DRAM_TIMING_1				0x9fc
#define	MC_ARB_DRAM_TIMING_2				0x9fd
#define	MC_ARB_DRAM_TIMING_3				0x9fe
#define	MC_ARB_DRAM_TIMING2_1				0x9ff
#define	MC_ARB_DRAM_TIMING2_2				0xa00
#define	MC_ARB_DRAM_TIMING2_3				0xa01

#define MAX_NO_OF_MVDD_VALUES 2
#define MAX_NO_VREG_STEPS 32
#define NISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE 16
#define SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE 32
#define SMC_NISLANDS_MC_REGISTER_ARRAY_SET_COUNT 20
#define RV770_ASI_DFLT                                1000
#define CYPRESS_HASI_DFLT                               400000
#define PCIE_PERF_REQ_PECI_GEN1         2
#define PCIE_PERF_REQ_PECI_GEN2         3
#define PCIE_PERF_REQ_PECI_GEN3         4
#define RV770_DEFAULT_VCLK_FREQ  53300 /* 10 khz */
#define RV770_DEFAULT_DCLK_FREQ  40000 /* 10 khz */

#define SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE 16

#define RV770_SMC_TABLE_ADDRESS 0xB000
#define RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE    3

#define SMC_STROBE_RATIO    0x0F
#define SMC_STROBE_ENABLE   0x10

#define SMC_MC_EDC_RD_FLAG  0x01
#define SMC_MC_EDC_WR_FLAG  0x02
#define SMC_MC_RTT_ENABLE   0x04
#define SMC_MC_STUTTER_EN   0x08

#define RV770_SMC_VOLTAGEMASK_VDDC 0
#define RV770_SMC_VOLTAGEMASK_MVDD 1
#define RV770_SMC_VOLTAGEMASK_VDDCI 2
#define RV770_SMC_VOLTAGEMASK_MAX  4

#define NISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE 16
#define NISLANDS_SMC_STROBE_RATIO    0x0F
#define NISLANDS_SMC_STROBE_ENABLE   0x10

#define NISLANDS_SMC_MC_EDC_RD_FLAG  0x01
#define NISLANDS_SMC_MC_EDC_WR_FLAG  0x02
#define NISLANDS_SMC_MC_RTT_ENABLE   0x04
#define NISLANDS_SMC_MC_STUTTER_EN   0x08

#define MAX_NO_VREG_STEPS 32

#define NISLANDS_SMC_VOLTAGEMASK_VDDC  0
#define NISLANDS_SMC_VOLTAGEMASK_MVDD  1
#define NISLANDS_SMC_VOLTAGEMASK_VDDCI 2
#define NISLANDS_SMC_VOLTAGEMASK_MAX   4

#define SISLANDS_MCREGISTERTABLE_INITIAL_SLOT               0
#define SISLANDS_MCREGISTERTABLE_ACPI_SLOT                  1
#define SISLANDS_MCREGISTERTABLE_ULV_SLOT                   2
#define SISLANDS_MCREGISTERTABLE_FIRST_DRIVERSTATE_SLOT     3

#define SISLANDS_LEAKAGE_INDEX0     0xff01
#define SISLANDS_MAX_LEAKAGE_COUNT  4

#define SISLANDS_MAX_HARDWARE_POWERLEVELS 5
#define SISLANDS_INITIAL_STATE_ARB_INDEX    0
#define SISLANDS_ACPI_STATE_ARB_INDEX       1
#define SISLANDS_ULV_STATE_ARB_INDEX        2
#define SISLANDS_DRIVER_STATE_ARB_INDEX     3

#define SISLANDS_DPM2_MAX_PULSE_SKIP        256

#define SISLANDS_DPM2_NEAR_TDP_DEC          10
#define SISLANDS_DPM2_ABOVE_SAFE_INC        5
#define SISLANDS_DPM2_BELOW_SAFE_INC        20

#define SISLANDS_DPM2_TDP_SAFE_LIMIT_PERCENT            80

#define SISLANDS_DPM2_MAXPS_PERCENT_H                   99
#define SISLANDS_DPM2_MAXPS_PERCENT_M                   99

#define SISLANDS_DPM2_SQ_RAMP_MAX_POWER                 0x3FFF
#define SISLANDS_DPM2_SQ_RAMP_MIN_POWER                 0x12
#define SISLANDS_DPM2_SQ_RAMP_MAX_POWER_DELTA           0x15
#define SISLANDS_DPM2_SQ_RAMP_STI_SIZE                  0x1E
#define SISLANDS_DPM2_SQ_RAMP_LTI_RATIO                 0xF

#define SISLANDS_DPM2_PWREFFICIENCYRATIO_MARGIN         10

#define SISLANDS_VRC_DFLT                               0xC000B3
#define SISLANDS_ULVVOLTAGECHANGEDELAY_DFLT             1687
#define SISLANDS_CGULVPARAMETER_DFLT                    0x00040035
#define SISLANDS_CGULVCONTROL_DFLT                      0x1f007550

#define SI_ASI_DFLT                                10000
#define SI_BSP_DFLT                                0x41EB
#define SI_BSU_DFLT                                0x2
#define SI_AH_DFLT                                 5
#define SI_RLP_DFLT                                25
#define SI_RMP_DFLT                                65
#define SI_LHP_DFLT                                40
#define SI_LMP_DFLT                                15
#define SI_TD_DFLT                                 0
#define SI_UTC_DFLT_00                             0x24
#define SI_UTC_DFLT_01                             0x22
#define SI_UTC_DFLT_02                             0x22
#define SI_UTC_DFLT_03                             0x22
#define SI_UTC_DFLT_04                             0x22
#define SI_UTC_DFLT_05                             0x22
#define SI_UTC_DFLT_06                             0x22
#define SI_UTC_DFLT_07                             0x22
#define SI_UTC_DFLT_08                             0x22
#define SI_UTC_DFLT_09                             0x22
#define SI_UTC_DFLT_10                             0x22
#define SI_UTC_DFLT_11                             0x22
#define SI_UTC_DFLT_12                             0x22
#define SI_UTC_DFLT_13                             0x22
#define SI_UTC_DFLT_14                             0x22
#define SI_DTC_DFLT_00                             0x24
#define SI_DTC_DFLT_01                             0x22
#define SI_DTC_DFLT_02                             0x22
#define SI_DTC_DFLT_03                             0x22
#define SI_DTC_DFLT_04                             0x22
#define SI_DTC_DFLT_05                             0x22
#define SI_DTC_DFLT_06                             0x22
#define SI_DTC_DFLT_07                             0x22
#define SI_DTC_DFLT_08                             0x22
#define SI_DTC_DFLT_09                             0x22
#define SI_DTC_DFLT_10                             0x22
#define SI_DTC_DFLT_11                             0x22
#define SI_DTC_DFLT_12                             0x22
#define SI_DTC_DFLT_13                             0x22
#define SI_DTC_DFLT_14                             0x22
#define SI_VRC_DFLT                                0x0000C003
#define SI_VOLTAGERESPONSETIME_DFLT                1000
#define SI_BACKBIASRESPONSETIME_DFLT               1000
#define SI_VRU_DFLT                                0x3
#define SI_SPLLSTEPTIME_DFLT                       0x1000
#define SI_SPLLSTEPUNIT_DFLT                       0x3
#define SI_TPU_DFLT                                0
#define SI_TPC_DFLT                                0x200
#define SI_SSTU_DFLT                               0
#define SI_SST_DFLT                                0x00C8
#define SI_GICST_DFLT                              0x200
#define SI_FCT_DFLT                                0x0400
#define SI_FCTU_DFLT                               0
#define SI_CTXCGTT3DRPHC_DFLT                      0x20
#define SI_CTXCGTT3DRSDC_DFLT                      0x40
#define SI_VDDC3DOORPHC_DFLT                       0x100
#define SI_VDDC3DOORSDC_DFLT                       0x7
#define SI_VDDC3DOORSU_DFLT                        0
#define SI_MPLLLOCKTIME_DFLT                       100
#define SI_MPLLRESETTIME_DFLT                      150
#define SI_VCOSTEPPCT_DFLT                          20
#define SI_ENDINGVCOSTEPPCT_DFLT                    5
#define SI_REFERENCEDIVIDER_DFLT                    4

#define SI_PM_NUMBER_OF_TC 15
#define SI_PM_NUMBER_OF_SCLKS 20
#define SI_PM_NUMBER_OF_MCLKS 4
#define SI_PM_NUMBER_OF_VOLTAGE_LEVELS 4
#define SI_PM_NUMBER_OF_ACTIVITY_LEVELS 3

/* XXX are these ok? */
#define SI_TEMP_RANGE_MIN (90 * 1000)
#define SI_TEMP_RANGE_MAX (120 * 1000)

#define FDO_PWM_MODE_STATIC  1
#define FDO_PWM_MODE_STATIC_RPM 5

enum ni_dc_cac_level
{
	NISLANDS_DCCAC_LEVEL_0 = 0,
	NISLANDS_DCCAC_LEVEL_1,
	NISLANDS_DCCAC_LEVEL_2,
	NISLANDS_DCCAC_LEVEL_3,
	NISLANDS_DCCAC_LEVEL_4,
	NISLANDS_DCCAC_LEVEL_5,
	NISLANDS_DCCAC_LEVEL_6,
	NISLANDS_DCCAC_LEVEL_7,
	NISLANDS_DCCAC_MAX_LEVELS
};

enum si_cac_config_reg_type
{
	SISLANDS_CACCONFIG_MMR = 0,
	SISLANDS_CACCONFIG_CGIND,
	SISLANDS_CACCONFIG_MAX
};

enum si_power_level {
	SI_POWER_LEVEL_LOW = 0,
	SI_POWER_LEVEL_MEDIUM = 1,
	SI_POWER_LEVEL_HIGH = 2,
	SI_POWER_LEVEL_CTXSW = 3,
};

enum si_td {
	SI_TD_AUTO,
	SI_TD_UP,
	SI_TD_DOWN,
};

enum si_display_watermark {
	SI_DISPLAY_WATERMARK_LOW = 0,
	SI_DISPLAY_WATERMARK_HIGH = 1,
};

enum si_display_gap
{
    SI_PM_DISPLAY_GAP_VBLANK_OR_WM = 0,
    SI_PM_DISPLAY_GAP_VBLANK       = 1,
    SI_PM_DISPLAY_GAP_WATERMARK    = 2,
    SI_PM_DISPLAY_GAP_IGNORE       = 3,
};

extern const struct amdgpu_ip_block_version si_smu_ip_block;

struct ni_leakage_coeffients
{
	u32 at;
	u32 bt;
	u32 av;
	u32 bv;
	s32 t_slope;
	s32 t_intercept;
	u32 t_ref;
};

struct SMC_Evergreen_MCRegisterAddress
{
    uint16_t s0;
    uint16_t s1;
};

typedef struct SMC_Evergreen_MCRegisterAddress SMC_Evergreen_MCRegisterAddress;

struct evergreen_mc_reg_entry {
	u32 mclk_max;
	u32 mc_data[SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE];
};

struct evergreen_mc_reg_table {
	u8 last;
	u8 num_entries;
	u16 valid_flag;
	struct evergreen_mc_reg_entry mc_reg_table_entry[MAX_AC_TIMING_ENTRIES];
	SMC_Evergreen_MCRegisterAddress mc_reg_address[SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE];
};

struct SMC_Evergreen_MCRegisterSet
{
    uint32_t value[SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE];
};

typedef struct SMC_Evergreen_MCRegisterSet SMC_Evergreen_MCRegisterSet;

struct SMC_Evergreen_MCRegisters
{
    uint8_t                             last;
    uint8_t                             reserved[3];
    SMC_Evergreen_MCRegisterAddress     address[SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE];
    SMC_Evergreen_MCRegisterSet         data[5];
};

typedef struct SMC_Evergreen_MCRegisters SMC_Evergreen_MCRegisters;

struct SMC_NIslands_MCRegisterSet
{
    uint32_t value[SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE];
};

typedef struct SMC_NIslands_MCRegisterSet SMC_NIslands_MCRegisterSet;

struct ni_mc_reg_entry {
	u32 mclk_max;
	u32 mc_data[SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE];
};

struct SMC_NIslands_MCRegisterAddress
{
    uint16_t s0;
    uint16_t s1;
};

typedef struct SMC_NIslands_MCRegisterAddress SMC_NIslands_MCRegisterAddress;

struct SMC_NIslands_MCRegisters
{
    uint8_t                             last;
    uint8_t                             reserved[3];
    SMC_NIslands_MCRegisterAddress      address[SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE];
    SMC_NIslands_MCRegisterSet          data[SMC_NISLANDS_MC_REGISTER_ARRAY_SET_COUNT];
};

typedef struct SMC_NIslands_MCRegisters SMC_NIslands_MCRegisters;

struct evergreen_ulv_param {
	bool supported;
	struct rv7xx_pl *pl;
};

struct evergreen_arb_registers {
	u32 mc_arb_dram_timing;
	u32 mc_arb_dram_timing2;
	u32 mc_arb_rfsh_rate;
	u32 mc_arb_burst_time;
};

struct at {
	u32 rlp;
	u32 rmp;
	u32 lhp;
	u32 lmp;
};

struct ni_clock_registers {
	u32 cg_spll_func_cntl;
	u32 cg_spll_func_cntl_2;
	u32 cg_spll_func_cntl_3;
	u32 cg_spll_func_cntl_4;
	u32 cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2;
	u32 mclk_pwrmgt_cntl;
	u32 dll_cntl;
	u32 mpll_ad_func_cntl;
	u32 mpll_ad_func_cntl_2;
	u32 mpll_dq_func_cntl;
	u32 mpll_dq_func_cntl_2;
	u32 mpll_ss1;
	u32 mpll_ss2;
};

struct RV770_SMC_SCLK_VALUE
{
    uint32_t        vCG_SPLL_FUNC_CNTL;
    uint32_t        vCG_SPLL_FUNC_CNTL_2;
    uint32_t        vCG_SPLL_FUNC_CNTL_3;
    uint32_t        vCG_SPLL_SPREAD_SPECTRUM;
    uint32_t        vCG_SPLL_SPREAD_SPECTRUM_2;
    uint32_t        sclk_value;
};

typedef struct RV770_SMC_SCLK_VALUE RV770_SMC_SCLK_VALUE;

struct RV770_SMC_MCLK_VALUE
{
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

typedef struct RV770_SMC_MCLK_VALUE RV770_SMC_MCLK_VALUE;


struct RV730_SMC_MCLK_VALUE
{
    uint32_t        vMCLK_PWRMGT_CNTL;
    uint32_t        vDLL_CNTL;
    uint32_t        vMPLL_FUNC_CNTL;
    uint32_t        vMPLL_FUNC_CNTL2;
    uint32_t        vMPLL_FUNC_CNTL3;
    uint32_t        vMPLL_SS;
    uint32_t        vMPLL_SS2;
    uint32_t        mclk_value;
};

typedef struct RV730_SMC_MCLK_VALUE RV730_SMC_MCLK_VALUE;

struct RV770_SMC_VOLTAGE_VALUE
{
    uint16_t             value;
    uint8_t              index;
    uint8_t              padding;
};

typedef struct RV770_SMC_VOLTAGE_VALUE RV770_SMC_VOLTAGE_VALUE;

union RV7XX_SMC_MCLK_VALUE
{
    RV770_SMC_MCLK_VALUE    mclk770;
    RV730_SMC_MCLK_VALUE    mclk730;
};

typedef union RV7XX_SMC_MCLK_VALUE RV7XX_SMC_MCLK_VALUE, *LPRV7XX_SMC_MCLK_VALUE;

struct RV770_SMC_HW_PERFORMANCE_LEVEL
{
    uint8_t                 arbValue;
    union{
        uint8_t             seqValue;
        uint8_t             ACIndex;
    };
    uint8_t                 displayWatermark;
    uint8_t                 gen2PCIE;
    uint8_t                 gen2XSP;
    uint8_t                 backbias;
    uint8_t                 strobeMode;
    uint8_t                 mcFlags;
    uint32_t                aT;
    uint32_t                bSP;
    RV770_SMC_SCLK_VALUE    sclk;
    RV7XX_SMC_MCLK_VALUE    mclk;
    RV770_SMC_VOLTAGE_VALUE vddc;
    RV770_SMC_VOLTAGE_VALUE mvdd;
    RV770_SMC_VOLTAGE_VALUE vddci;
    uint8_t                 reserved1;
    uint8_t                 reserved2;
    uint8_t                 stateFlags;
    uint8_t                 padding;
};

typedef struct RV770_SMC_HW_PERFORMANCE_LEVEL RV770_SMC_HW_PERFORMANCE_LEVEL;

struct RV770_SMC_SWSTATE
{
    uint8_t           flags;
    uint8_t           padding1;
    uint8_t           padding2;
    uint8_t           padding3;
    RV770_SMC_HW_PERFORMANCE_LEVEL levels[RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE];
};

typedef struct RV770_SMC_SWSTATE RV770_SMC_SWSTATE;

struct RV770_SMC_VOLTAGEMASKTABLE
{
    uint8_t  highMask[RV770_SMC_VOLTAGEMASK_MAX];
    uint32_t lowMask[RV770_SMC_VOLTAGEMASK_MAX];
};

typedef struct RV770_SMC_VOLTAGEMASKTABLE RV770_SMC_VOLTAGEMASKTABLE;

struct RV770_SMC_STATETABLE
{
    uint8_t             thermalProtectType;
    uint8_t             systemFlags;
    uint8_t             maxVDDCIndexInPPTable;
    uint8_t             extraFlags;
    uint8_t             highSMIO[MAX_NO_VREG_STEPS];
    uint32_t            lowSMIO[MAX_NO_VREG_STEPS];
    RV770_SMC_VOLTAGEMASKTABLE voltageMaskTable;
    RV770_SMC_SWSTATE   initialState;
    RV770_SMC_SWSTATE   ACPIState;
    RV770_SMC_SWSTATE   driverState;
    RV770_SMC_SWSTATE   ULVState;
};

typedef struct RV770_SMC_STATETABLE RV770_SMC_STATETABLE;

struct vddc_table_entry {
	u16 vddc;
	u8 vddc_index;
	u8 high_smio;
	u32 low_smio;
};

struct rv770_clock_registers {
	u32 cg_spll_func_cntl;
	u32 cg_spll_func_cntl_2;
	u32 cg_spll_func_cntl_3;
	u32 cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2;
	u32 mpll_ad_func_cntl;
	u32 mpll_ad_func_cntl_2;
	u32 mpll_dq_func_cntl;
	u32 mpll_dq_func_cntl_2;
	u32 mclk_pwrmgt_cntl;
	u32 dll_cntl;
	u32 mpll_ss1;
	u32 mpll_ss2;
};

struct rv730_clock_registers {
	u32 cg_spll_func_cntl;
	u32 cg_spll_func_cntl_2;
	u32 cg_spll_func_cntl_3;
	u32 cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2;
	u32 mclk_pwrmgt_cntl;
	u32 dll_cntl;
	u32 mpll_func_cntl;
	u32 mpll_func_cntl2;
	u32 mpll_func_cntl3;
	u32 mpll_ss;
	u32 mpll_ss2;
};

union r7xx_clock_registers {
	struct rv770_clock_registers rv770;
	struct rv730_clock_registers rv730;
};

struct rv7xx_power_info {
	/* flags */
	bool mem_gddr5;
	bool pcie_gen2;
	bool dynamic_pcie_gen2;
	bool acpi_pcie_gen2;
	bool boot_in_gen2;
	bool voltage_control; /* vddc */
	bool mvdd_control;
	bool sclk_ss;
	bool mclk_ss;
	bool dynamic_ss;
	bool gfx_clock_gating;
	bool mg_clock_gating;
	bool mgcgtssm;
	bool power_gating;
	bool thermal_protection;
	bool display_gap;
	bool dcodt;
	bool ulps;
	/* registers */
	union r7xx_clock_registers clk_regs;
	u32 s0_vid_lower_smio_cntl;
	/* voltage */
	u32 vddc_mask_low;
	u32 mvdd_mask_low;
	u32 mvdd_split_frequency;
	u32 mvdd_low_smio[MAX_NO_OF_MVDD_VALUES];
	u16 max_vddc;
	u16 max_vddc_in_table;
	u16 min_vddc_in_table;
	struct vddc_table_entry vddc_table[MAX_NO_VREG_STEPS];
	u8 valid_vddc_entries;
	/* dc odt */
	u32 mclk_odt_threshold;
	u8 odt_value_0[2];
	u8 odt_value_1[2];
	/* stored values */
	u32 boot_sclk;
	u16 acpi_vddc;
	u32 ref_div;
	u32 active_auto_throttle_sources;
	u32 mclk_stutter_mode_threshold;
	u32 mclk_strobe_mode_threshold;
	u32 mclk_edc_enable_threshold;
	u32 bsp;
	u32 bsu;
	u32 pbsp;
	u32 pbsu;
	u32 dsp;
	u32 psp;
	u32 asi;
	u32 pasi;
	u32 vrc;
	u32 restricted_levels;
	u32 rlp;
	u32 rmp;
	u32 lhp;
	u32 lmp;
	/* smc offsets */
	u16 state_table_start;
	u16 soft_regs_start;
	u16 sram_end;
	/* scratch structs */
	RV770_SMC_STATETABLE smc_statetable;
};

enum si_pcie_gen {
	SI_PCIE_GEN1 = 0,
	SI_PCIE_GEN2 = 1,
	SI_PCIE_GEN3 = 2,
	SI_PCIE_GEN_INVALID = 0xffff
};

struct rv7xx_pl {
	u32 sclk;
	u32 mclk;
	u16 vddc;
	u16 vddci; /* eg+ only */
	u32 flags;
	enum si_pcie_gen pcie_gen; /* si+ only */
};

struct rv7xx_ps {
	struct rv7xx_pl high;
	struct rv7xx_pl medium;
	struct rv7xx_pl low;
	bool dc_compatible;
};

struct si_ps {
	u16 performance_level_count;
	bool dc_compatible;
	struct rv7xx_pl performance_levels[NISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE];
};

struct ni_mc_reg_table {
	u8 last;
	u8 num_entries;
	u16 valid_flag;
	struct ni_mc_reg_entry mc_reg_table_entry[MAX_AC_TIMING_ENTRIES];
	SMC_NIslands_MCRegisterAddress mc_reg_address[SMC_NISLANDS_MC_REGISTER_ARRAY_SIZE];
};

struct ni_cac_data
{
	struct ni_leakage_coeffients leakage_coefficients;
	u32 i_leakage;
	s32 leakage_minimum_temperature;
	u32 pwr_const;
	u32 dc_cac_value;
	u32 bif_cac_value;
	u32 lkge_pwr;
	u8 mc_wr_weight;
	u8 mc_rd_weight;
	u8 allow_ovrflw;
	u8 num_win_tdp;
	u8 l2num_win_tdp;
	u8 lts_truncate_n;
};

struct evergreen_power_info {
	/* must be first! */
	struct rv7xx_power_info rv7xx;
	/* flags */
	bool vddci_control;
	bool dynamic_ac_timing;
	bool abm;
	bool mcls;
	bool light_sleep;
	bool memory_transition;
	bool pcie_performance_request;
	bool pcie_performance_request_registered;
	bool sclk_deep_sleep;
	bool dll_default_on;
	bool ls_clock_gating;
	bool smu_uvd_hs;
	bool uvd_enabled;
	/* stored values */
	u16 acpi_vddci;
	u8 mvdd_high_index;
	u8 mvdd_low_index;
	u32 mclk_edc_wr_enable_threshold;
	struct evergreen_mc_reg_table mc_reg_table;
	struct atom_voltage_table vddc_voltage_table;
	struct atom_voltage_table vddci_voltage_table;
	struct evergreen_arb_registers bootup_arb_registers;
	struct evergreen_ulv_param ulv;
	struct at ats[2];
	/* smc offsets */
	u16 mc_reg_table_start;
	struct amdgpu_ps current_rps;
	struct rv7xx_ps current_ps;
	struct amdgpu_ps requested_rps;
	struct rv7xx_ps requested_ps;
};

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

struct ni_power_info {
	/* must be first! */
	struct evergreen_power_info eg;
	struct ni_clock_registers clock_registers;
	struct ni_mc_reg_table mc_reg_table;
	u32 mclk_rtt_mode_threshold;
	/* flags */
	bool use_power_boost_limit;
	bool support_cac_long_term_average;
	bool cac_enabled;
	bool cac_configuration_required;
	bool driver_calculate_cac_leakage;
	bool pc_enabled;
	bool enable_power_containment;
	bool enable_cac;
	bool enable_sq_ramping;
	/* smc offsets */
	u16 arb_table_start;
	u16 fan_table_start;
	u16 cac_table_start;
	u16 spll_table_start;
	/* CAC stuff */
	struct ni_cac_data cac_data;
	u32 dc_cac_table[NISLANDS_DCCAC_MAX_LEVELS];
	const struct ni_cac_weights *cac_weights;
	u8 lta_window_size;
	u8 lts_truncate;
	struct si_ps current_ps;
	struct si_ps requested_ps;
	/* scratch structs */
	SMC_NIslands_MCRegisters smc_mc_reg_table;
	NISLANDS_SMC_STATETABLE smc_statetable;
};

struct si_cac_config_reg
{
	u32 offset;
	u32 mask;
	u32 shift;
	u32 value;
	enum si_cac_config_reg_type type;
};

struct si_powertune_data
{
	u32 cac_window;
	u32 l2_lta_window_size_default;
	u8 lts_truncate_default;
	u8 shift_n_default;
	u8 operating_temp;
	struct ni_leakage_coeffients leakage_coefficients;
	u32 fixed_kt;
	u32 lkge_lut_v0_percent;
	u8 dc_cac[NISLANDS_DCCAC_MAX_LEVELS];
	bool enable_powertune_by_default;
};

struct si_dyn_powertune_data
{
	u32 cac_leakage;
	s32 leakage_minimum_temperature;
	u32 wintime;
	u32 l2_lta_window_size;
	u8 lts_truncate;
	u8 shift_n;
	u8 dc_pwr_value;
	bool disable_uvd_powertune;
};

struct si_dte_data
{
	u32 tau[SMC_SISLANDS_DTE_MAX_FILTER_STAGES];
	u32 r[SMC_SISLANDS_DTE_MAX_FILTER_STAGES];
	u32 k;
	u32 t0;
	u32 max_t;
	u8 window_size;
	u8 temp_select;
	u8 dte_mode;
	u8 tdep_count;
	u8 t_limits[SMC_SISLANDS_DTE_MAX_TEMPERATURE_DEPENDENT_ARRAY_SIZE];
	u32 tdep_tau[SMC_SISLANDS_DTE_MAX_TEMPERATURE_DEPENDENT_ARRAY_SIZE];
	u32 tdep_r[SMC_SISLANDS_DTE_MAX_TEMPERATURE_DEPENDENT_ARRAY_SIZE];
	u32 t_threshold;
	bool enable_dte_by_default;
};

struct si_clock_registers {
	u32 cg_spll_func_cntl;
	u32 cg_spll_func_cntl_2;
	u32 cg_spll_func_cntl_3;
	u32 cg_spll_func_cntl_4;
	u32 cg_spll_spread_spectrum;
	u32 cg_spll_spread_spectrum_2;
	u32 dll_cntl;
	u32 mclk_pwrmgt_cntl;
	u32 mpll_ad_func_cntl;
	u32 mpll_dq_func_cntl;
	u32 mpll_func_cntl;
	u32 mpll_func_cntl_1;
	u32 mpll_func_cntl_2;
	u32 mpll_ss1;
	u32 mpll_ss2;
};

struct si_mc_reg_entry {
	u32 mclk_max;
	u32 mc_data[SMC_SISLANDS_MC_REGISTER_ARRAY_SIZE];
};

struct si_mc_reg_table {
	u8 last;
	u8 num_entries;
	u16 valid_flag;
	struct si_mc_reg_entry mc_reg_table_entry[MAX_AC_TIMING_ENTRIES];
	SMC_NIslands_MCRegisterAddress mc_reg_address[SMC_SISLANDS_MC_REGISTER_ARRAY_SIZE];
};

struct si_leakage_voltage_entry
{
	u16 voltage;
	u16 leakage_index;
};

struct si_leakage_voltage
{
	u16 count;
	struct si_leakage_voltage_entry entries[SISLANDS_MAX_LEAKAGE_COUNT];
};


struct si_ulv_param {
	bool supported;
	u32 cg_ulv_control;
	u32 cg_ulv_parameter;
	u32 volt_change_delay;
	struct rv7xx_pl pl;
	bool one_pcie_lane_in_ulv;
};

struct si_power_info {
	/* must be first! */
	struct ni_power_info ni;
	struct si_clock_registers clock_registers;
	struct si_mc_reg_table mc_reg_table;
	struct atom_voltage_table mvdd_voltage_table;
	struct atom_voltage_table vddc_phase_shed_table;
	struct si_leakage_voltage leakage_voltage;
	u16 mvdd_bootup_value;
	struct si_ulv_param ulv;
	u32 max_cu;
	/* pcie gen */
	enum si_pcie_gen force_pcie_gen;
	enum si_pcie_gen boot_pcie_gen;
	enum si_pcie_gen acpi_pcie_gen;
	u32 sys_pcie_mask;
	/* flags */
	bool enable_dte;
	bool enable_ppm;
	bool vddc_phase_shed_control;
	bool pspp_notify_required;
	bool sclk_deep_sleep_above_low;
	bool voltage_control_svi2;
	bool vddci_control_svi2;
	/* smc offsets */
	u32 sram_end;
	u32 state_table_start;
	u32 soft_regs_start;
	u32 mc_reg_table_start;
	u32 arb_table_start;
	u32 cac_table_start;
	u32 dte_table_start;
	u32 spll_table_start;
	u32 papm_cfg_table_start;
	u32 fan_table_start;
	/* CAC stuff */
	const struct si_cac_config_reg *cac_weights;
	const struct si_cac_config_reg *lcac_config;
	const struct si_cac_config_reg *cac_override;
	const struct si_powertune_data *powertune_data;
	struct si_dyn_powertune_data dyn_powertune_data;
	/* DTE stuff */
	struct si_dte_data dte_data;
	/* scratch structs */
	SMC_SIslands_MCRegisters smc_mc_reg_table;
	SISLANDS_SMC_STATETABLE smc_statetable;
	PP_SIslands_PAPMParameters papm_parm;
	/* SVI2 */
	u8 svd_gpio_id;
	u8 svc_gpio_id;
	/* fan control */
	bool fan_ctrl_is_in_default_mode;
	u32 t_min;
	u32 fan_ctrl_default_mode;
	bool fan_is_controlled_by_smc;
};

#endif
