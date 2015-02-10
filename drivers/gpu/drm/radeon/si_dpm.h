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

#include "ni_dpm.h"
#include "sislands_smc.h"

enum si_cac_config_reg_type
{
	SISLANDS_CACCONFIG_MMR = 0,
	SISLANDS_CACCONFIG_CGIND,
	SISLANDS_CACCONFIG_MAX
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

#define SISLANDS_MCREGISTERTABLE_INITIAL_SLOT               0
#define SISLANDS_MCREGISTERTABLE_ACPI_SLOT                  1
#define SISLANDS_MCREGISTERTABLE_ULV_SLOT                   2
#define SISLANDS_MCREGISTERTABLE_FIRST_DRIVERSTATE_SLOT     3

struct si_leakage_voltage_entry
{
	u16 voltage;
	u16 leakage_index;
};

#define SISLANDS_LEAKAGE_INDEX0     0xff01
#define SISLANDS_MAX_LEAKAGE_COUNT  4

struct si_leakage_voltage
{
	u16 count;
	struct si_leakage_voltage_entry entries[SISLANDS_MAX_LEAKAGE_COUNT];
};

#define SISLANDS_MAX_HARDWARE_POWERLEVELS 5

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
	enum radeon_pcie_gen force_pcie_gen;
	enum radeon_pcie_gen boot_pcie_gen;
	enum radeon_pcie_gen acpi_pcie_gen;
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
};

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


#endif
