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

#define NISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE 16
#define RV770_ASI_DFLT                                1000
#define CYPRESS_HASI_DFLT                               400000
#define PCIE_PERF_REQ_PECI_GEN1         2
#define PCIE_PERF_REQ_PECI_GEN2         3
#define PCIE_PERF_REQ_PECI_GEN3         4
#define RV770_DEFAULT_VCLK_FREQ  53300 /* 10 khz */
#define RV770_DEFAULT_DCLK_FREQ  40000 /* 10 khz */

#define SMC_STROBE_RATIO    0x0F
#define SMC_STROBE_ENABLE   0x10

#define SMC_MC_EDC_RD_FLAG  0x01
#define SMC_MC_EDC_WR_FLAG  0x02
#define SMC_MC_RTT_ENABLE   0x04
#define SMC_MC_STUTTER_EN   0x08

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

struct SMC_NIslands_MCRegisterAddress
{
    uint16_t s0;
    uint16_t s1;
};

typedef struct SMC_NIslands_MCRegisterAddress SMC_NIslands_MCRegisterAddress;

struct rv7xx_power_info {
	/* flags */
	bool voltage_control; /* vddc */
	bool mvdd_control;
	bool sclk_ss;
	bool mclk_ss;
	bool dynamic_ss;
	bool thermal_protection;
	/* voltage */
	u32 mvdd_split_frequency;
	u16 max_vddc;
	u16 max_vddc_in_table;
	u16 min_vddc_in_table;
	/* stored values */
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

struct si_ps {
	u16 performance_level_count;
	bool dc_compatible;
	struct rv7xx_pl performance_levels[NISLANDS_MAX_SMC_PERFORMANCE_LEVELS_PER_SWSTATE];
};

struct evergreen_power_info {
	/* must be first! */
	struct rv7xx_power_info rv7xx;
	/* flags */
	bool vddci_control;
	bool dynamic_ac_timing;
	bool abm;
	bool mcls;
	bool pcie_performance_request;
	bool sclk_deep_sleep;
	bool smu_uvd_hs;
	bool uvd_enabled;
	/* stored values */
	u16 acpi_vddci;
	u32 mclk_edc_wr_enable_threshold;
	struct atom_voltage_table vddc_voltage_table;
	struct atom_voltage_table vddci_voltage_table;
	struct amdgpu_ps current_rps;
	struct amdgpu_ps requested_rps;
};

struct ni_power_info {
	/* must be first! */
	struct evergreen_power_info eg;
	u32 mclk_rtt_mode_threshold;
	/* flags */
	bool support_cac_long_term_average;
	bool cac_enabled;
	bool cac_configuration_required;
	bool driver_calculate_cac_leakage;
	bool enable_power_containment;
	bool enable_cac;
	bool enable_sq_ramping;
	struct si_ps current_ps;
	struct si_ps requested_ps;
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
