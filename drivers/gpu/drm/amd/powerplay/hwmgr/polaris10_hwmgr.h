/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef POLARIS10_HWMGR_H
#define POLARIS10_HWMGR_H

#include "hwmgr.h"
#include "smu74.h"
#include "smu74_discrete.h"
#include "ppatomctrl.h"
#include "polaris10_ppsmc.h"
#include "polaris10_powertune.h"

#define POLARIS10_MAX_HARDWARE_POWERLEVELS	2

#define POLARIS10_VOLTAGE_CONTROL_NONE                   0x0
#define POLARIS10_VOLTAGE_CONTROL_BY_GPIO                0x1
#define POLARIS10_VOLTAGE_CONTROL_BY_SVID2               0x2
#define POLARIS10_VOLTAGE_CONTROL_MERGED                 0x3

#define DPMTABLE_OD_UPDATE_SCLK     0x00000001
#define DPMTABLE_OD_UPDATE_MCLK     0x00000002
#define DPMTABLE_UPDATE_SCLK        0x00000004
#define DPMTABLE_UPDATE_MCLK        0x00000008

struct polaris10_performance_level {
	uint32_t  memory_clock;
	uint32_t  engine_clock;
	uint16_t  pcie_gen;
	uint16_t  pcie_lane;
};

struct polaris10_uvd_clocks {
	uint32_t  vclk;
	uint32_t  dclk;
};

struct polaris10_vce_clocks {
	uint32_t  evclk;
	uint32_t  ecclk;
};

struct polaris10_power_state {
	uint32_t                  magic;
	struct polaris10_uvd_clocks    uvd_clks;
	struct polaris10_vce_clocks    vce_clks;
	uint32_t                  sam_clk;
	uint16_t                  performance_level_count;
	bool                      dc_compatible;
	uint32_t                  sclk_threshold;
	struct polaris10_performance_level  performance_levels[POLARIS10_MAX_HARDWARE_POWERLEVELS];
};

struct polaris10_dpm_level {
	bool	enabled;
	uint32_t	value;
	uint32_t	param1;
};

#define POLARIS10_MAX_DEEPSLEEP_DIVIDER_ID 5
#define MAX_REGULAR_DPM_NUMBER 8
#define POLARIS10_MINIMUM_ENGINE_CLOCK 2500

struct polaris10_single_dpm_table {
	uint32_t		count;
	struct polaris10_dpm_level	dpm_levels[MAX_REGULAR_DPM_NUMBER];
};

struct polaris10_dpm_table {
	struct polaris10_single_dpm_table  sclk_table;
	struct polaris10_single_dpm_table  mclk_table;
	struct polaris10_single_dpm_table  pcie_speed_table;
	struct polaris10_single_dpm_table  vddc_table;
	struct polaris10_single_dpm_table  vddci_table;
	struct polaris10_single_dpm_table  mvdd_table;
};

struct polaris10_clock_registers {
	uint32_t  vCG_SPLL_FUNC_CNTL;
	uint32_t  vCG_SPLL_FUNC_CNTL_2;
	uint32_t  vCG_SPLL_FUNC_CNTL_3;
	uint32_t  vCG_SPLL_FUNC_CNTL_4;
	uint32_t  vCG_SPLL_SPREAD_SPECTRUM;
	uint32_t  vCG_SPLL_SPREAD_SPECTRUM_2;
	uint32_t  vDLL_CNTL;
	uint32_t  vMCLK_PWRMGT_CNTL;
	uint32_t  vMPLL_AD_FUNC_CNTL;
	uint32_t  vMPLL_DQ_FUNC_CNTL;
	uint32_t  vMPLL_FUNC_CNTL;
	uint32_t  vMPLL_FUNC_CNTL_1;
	uint32_t  vMPLL_FUNC_CNTL_2;
	uint32_t  vMPLL_SS1;
	uint32_t  vMPLL_SS2;
};

#define DISABLE_MC_LOADMICROCODE   1
#define DISABLE_MC_CFGPROGRAMMING  2

struct polaris10_voltage_smio_registers {
	uint32_t vS0_VID_LOWER_SMIO_CNTL;
};

#define POLARIS10_MAX_LEAKAGE_COUNT  8

struct polaris10_leakage_voltage {
	uint16_t  count;
	uint16_t  leakage_id[POLARIS10_MAX_LEAKAGE_COUNT];
	uint16_t  actual_voltage[POLARIS10_MAX_LEAKAGE_COUNT];
};

struct polaris10_vbios_boot_state {
	uint16_t    mvdd_bootup_value;
	uint16_t    vddc_bootup_value;
	uint16_t    vddci_bootup_value;
	uint32_t    sclk_bootup_value;
	uint32_t    mclk_bootup_value;
	uint16_t    pcie_gen_bootup_value;
	uint16_t    pcie_lane_bootup_value;
};

/* Ultra Low Voltage parameter structure */
struct polaris10_ulv_parm {
	bool                           ulv_supported;
	uint32_t                       cg_ulv_parameter;
	uint32_t                       ulv_volt_change_delay;
	struct polaris10_performance_level  ulv_power_level;
};

struct polaris10_display_timing {
	uint32_t  min_clock_in_sr;
	uint32_t  num_existing_displays;
};

struct polaris10_dpmlevel_enable_mask {
	uint32_t  uvd_dpm_enable_mask;
	uint32_t  vce_dpm_enable_mask;
	uint32_t  acp_dpm_enable_mask;
	uint32_t  samu_dpm_enable_mask;
	uint32_t  sclk_dpm_enable_mask;
	uint32_t  mclk_dpm_enable_mask;
	uint32_t  pcie_dpm_enable_mask;
};

struct polaris10_pcie_perf_range {
	uint16_t  max;
	uint16_t  min;
};
struct polaris10_range_table {
	uint32_t trans_lower_frequency; /* in 10khz */
	uint32_t trans_upper_frequency;
};

struct polaris10_hwmgr {
	struct polaris10_dpm_table			dpm_table;
	struct polaris10_dpm_table			golden_dpm_table;
	SMU74_Discrete_DpmTable				smc_state_table;
	struct SMU74_Discrete_Ulv            ulv_setting;

	struct polaris10_range_table                range_table[NUM_SCLK_RANGE];
	uint32_t						voting_rights_clients0;
	uint32_t						voting_rights_clients1;
	uint32_t						voting_rights_clients2;
	uint32_t						voting_rights_clients3;
	uint32_t						voting_rights_clients4;
	uint32_t						voting_rights_clients5;
	uint32_t						voting_rights_clients6;
	uint32_t						voting_rights_clients7;
	uint32_t						static_screen_threshold_unit;
	uint32_t						static_screen_threshold;
	uint32_t						voltage_control;
	uint32_t						vddc_vddci_delta;

	uint32_t						active_auto_throttle_sources;

	struct polaris10_clock_registers            clock_registers;
	struct polaris10_voltage_smio_registers      voltage_smio_registers;

	bool                           is_memory_gddr5;
	uint16_t                       acpi_vddc;
	bool                           pspp_notify_required;
	uint16_t                       force_pcie_gen;
	uint16_t                       acpi_pcie_gen;
	uint32_t                       pcie_gen_cap;
	uint32_t                       pcie_lane_cap;
	uint32_t                       pcie_spc_cap;
	struct polaris10_leakage_voltage          vddc_leakage;
	struct polaris10_leakage_voltage          Vddci_leakage;

	uint32_t                             mvdd_control;
	uint32_t                             vddc_mask_low;
	uint32_t                             mvdd_mask_low;
	uint16_t                            max_vddc_in_pptable;
	uint16_t                            min_vddc_in_pptable;
	uint16_t                            max_vddci_in_pptable;
	uint16_t                            min_vddci_in_pptable;
	uint32_t                             mclk_strobe_mode_threshold;
	uint32_t                             mclk_stutter_mode_threshold;
	uint32_t                             mclk_edc_enable_threshold;
	uint32_t                             mclk_edcwr_enable_threshold;
	bool                                is_uvd_enabled;
	struct polaris10_vbios_boot_state        vbios_boot_state;

	bool                           pcie_performance_request;
	bool                           battery_state;
	bool                           is_tlu_enabled;

	/* ---- SMC SRAM Address of firmware header tables ---- */
	uint32_t                             sram_end;
	uint32_t                             dpm_table_start;
	uint32_t                             soft_regs_start;
	uint32_t                             mc_reg_table_start;
	uint32_t                             fan_table_start;
	uint32_t                             arb_table_start;

	/* ---- Stuff originally coming from Evergreen ---- */
	uint32_t                             vddci_control;
	struct pp_atomctrl_voltage_table     vddc_voltage_table;
	struct pp_atomctrl_voltage_table     vddci_voltage_table;
	struct pp_atomctrl_voltage_table     mvdd_voltage_table;

	uint32_t                             mgcg_cgtt_local2;
	uint32_t                             mgcg_cgtt_local3;
	uint32_t                             gpio_debug;
	uint32_t                             mc_micro_code_feature;
	uint32_t                             highest_mclk;
	uint16_t                             acpi_vddci;
	uint8_t                              mvdd_high_index;
	uint8_t                              mvdd_low_index;
	bool                                 dll_default_on;
	bool                                 performance_request_registered;

	/* ---- Low Power Features ---- */
	struct polaris10_ulv_parm                 ulv;

	/* ---- CAC Stuff ---- */
	uint32_t                       cac_table_start;
	bool                           cac_configuration_required;
	bool                           driver_calculate_cac_leakage;
	bool                           cac_enabled;

	/* ---- DPM2 Parameters ---- */
	uint32_t                       power_containment_features;
	bool                           enable_dte_feature;
	bool                           enable_tdc_limit_feature;
	bool                           enable_pkg_pwr_tracking_feature;
	bool                           disable_uvd_power_tune_feature;
	const struct polaris10_pt_defaults       *power_tune_defaults;
	struct SMU74_Discrete_PmFuses  power_tune_table;
	uint32_t                       dte_tj_offset;
	uint32_t                       fast_watermark_threshold;

	/* ---- Phase Shedding ---- */
	bool                           vddc_phase_shed_control;

	/* ---- DI/DT ---- */
	struct polaris10_display_timing        display_timing;
	uint32_t                      bif_sclk_table[SMU74_MAX_LEVELS_LINK];

	/* ---- Thermal Temperature Setting ---- */
	struct polaris10_dpmlevel_enable_mask     dpm_level_enable_mask;
	uint32_t                                  need_update_smu7_dpm_table;
	uint32_t                                  sclk_dpm_key_disabled;
	uint32_t                                  mclk_dpm_key_disabled;
	uint32_t                                  pcie_dpm_key_disabled;
	uint32_t                                  min_engine_clocks;
	struct polaris10_pcie_perf_range          pcie_gen_performance;
	struct polaris10_pcie_perf_range          pcie_lane_performance;
	struct polaris10_pcie_perf_range          pcie_gen_power_saving;
	struct polaris10_pcie_perf_range          pcie_lane_power_saving;
	bool                                      use_pcie_performance_levels;
	bool                                      use_pcie_power_saving_levels;
	uint32_t                                  activity_target[SMU74_MAX_LEVELS_GRAPHICS];
	uint32_t                                  mclk_activity_target;
	uint32_t                                  mclk_dpm0_activity_target;
	uint32_t                                  low_sclk_interrupt_threshold;
	uint32_t                                  last_mclk_dpm_enable_mask;
	bool                                      uvd_enabled;

	/* ---- Power Gating States ---- */
	bool                           uvd_power_gated;
	bool                           vce_power_gated;
	bool                           samu_power_gated;
	bool                           need_long_memory_training;

	/* Application power optimization parameters */
	bool                               update_up_hyst;
	bool                               update_down_hyst;
	uint32_t                           down_hyst;
	uint32_t                           up_hyst;
	uint32_t disable_dpm_mask;
	bool apply_optimized_settings;
	uint32_t                              avfs_vdroop_override_setting;
	bool                                  apply_avfs_cks_off_voltage;
	uint32_t                              frame_time_x2;
};

/* To convert to Q8.8 format for firmware */
#define POLARIS10_Q88_FORMAT_CONVERSION_UNIT             256

enum Polaris10_I2CLineID {
	Polaris10_I2CLineID_DDC1 = 0x90,
	Polaris10_I2CLineID_DDC2 = 0x91,
	Polaris10_I2CLineID_DDC3 = 0x92,
	Polaris10_I2CLineID_DDC4 = 0x93,
	Polaris10_I2CLineID_DDC5 = 0x94,
	Polaris10_I2CLineID_DDC6 = 0x95,
	Polaris10_I2CLineID_SCLSDA = 0x96,
	Polaris10_I2CLineID_DDCVGA = 0x97
};

#define POLARIS10_I2C_DDC1DATA          0
#define POLARIS10_I2C_DDC1CLK           1
#define POLARIS10_I2C_DDC2DATA          2
#define POLARIS10_I2C_DDC2CLK           3
#define POLARIS10_I2C_DDC3DATA          4
#define POLARIS10_I2C_DDC3CLK           5
#define POLARIS10_I2C_SDA               40
#define POLARIS10_I2C_SCL               41
#define POLARIS10_I2C_DDC4DATA          65
#define POLARIS10_I2C_DDC4CLK           66
#define POLARIS10_I2C_DDC5DATA          0x48
#define POLARIS10_I2C_DDC5CLK           0x49
#define POLARIS10_I2C_DDC6DATA          0x4a
#define POLARIS10_I2C_DDC6CLK           0x4b
#define POLARIS10_I2C_DDCVGADATA        0x4c
#define POLARIS10_I2C_DDCVGACLK         0x4d

#define POLARIS10_UNUSED_GPIO_PIN       0x7F

int polaris10_hwmgr_init(struct pp_hwmgr *hwmgr);

int polaris10_update_uvd_dpm(struct pp_hwmgr *hwmgr, bool bgate);
int polaris10_update_samu_dpm(struct pp_hwmgr *hwmgr, bool bgate);
int polaris10_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable);
int polaris10_update_vce_dpm(struct pp_hwmgr *hwmgr, bool bgate);
#endif

