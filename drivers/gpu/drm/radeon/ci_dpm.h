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
#ifndef __CI_DPM_H__
#define __CI_DPM_H__

#include "ppsmc.h"

#define SMU__NUM_SCLK_DPM_STATE  8
#define SMU__NUM_MCLK_DPM_LEVELS 6
#define SMU__NUM_LCLK_DPM_LEVELS 8
#define SMU__NUM_PCIE_DPM_LEVELS 8
#include "smu7_discrete.h"

#define CISLANDS_MAX_HARDWARE_POWERLEVELS 2

#define CISLANDS_UNUSED_GPIO_PIN 0x7F

struct ci_pl {
	u32 mclk;
	u32 sclk;
	enum radeon_pcie_gen pcie_gen;
	u16 pcie_lane;
};

struct ci_ps {
	u16 performance_level_count;
	bool dc_compatible;
	u32 sclk_t;
	struct ci_pl performance_levels[CISLANDS_MAX_HARDWARE_POWERLEVELS];
};

struct ci_dpm_level {
	bool enabled;
	u32 value;
	u32 param1;
};

#define CISLAND_MAX_DEEPSLEEP_DIVIDER_ID 5
#define MAX_REGULAR_DPM_NUMBER 8
#define CISLAND_MINIMUM_ENGINE_CLOCK 800

struct ci_single_dpm_table {
	u32 count;
	struct ci_dpm_level dpm_levels[MAX_REGULAR_DPM_NUMBER];
};

struct ci_dpm_table {
	struct ci_single_dpm_table sclk_table;
	struct ci_single_dpm_table mclk_table;
	struct ci_single_dpm_table pcie_speed_table;
	struct ci_single_dpm_table vddc_table;
	struct ci_single_dpm_table vddci_table;
	struct ci_single_dpm_table mvdd_table;
};

struct ci_mc_reg_entry {
	u32 mclk_max;
	u32 mc_data[SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};

struct ci_mc_reg_table {
	u8 last;
	u8 num_entries;
	u16 valid_flag;
	struct ci_mc_reg_entry mc_reg_table_entry[MAX_AC_TIMING_ENTRIES];
	SMU7_Discrete_MCRegisterAddress mc_reg_address[SMU7_DISCRETE_MC_REGISTER_ARRAY_SIZE];
};

struct ci_ulv_parm
{
	bool supported;
	u32 cg_ulv_parameter;
	u32 volt_change_delay;
	struct ci_pl pl;
};

#define CISLANDS_MAX_LEAKAGE_COUNT  8

struct ci_leakage_voltage {
	u16 count;
	u16 leakage_id[CISLANDS_MAX_LEAKAGE_COUNT];
	u16 actual_voltage[CISLANDS_MAX_LEAKAGE_COUNT];
};

struct ci_dpm_level_enable_mask {
	u32 uvd_dpm_enable_mask;
	u32 vce_dpm_enable_mask;
	u32 acp_dpm_enable_mask;
	u32 samu_dpm_enable_mask;
	u32 sclk_dpm_enable_mask;
	u32 mclk_dpm_enable_mask;
	u32 pcie_dpm_enable_mask;
};

struct ci_vbios_boot_state
{
	u16 mvdd_bootup_value;
	u16 vddc_bootup_value;
	u16 vddci_bootup_value;
	u32 sclk_bootup_value;
	u32 mclk_bootup_value;
	u16 pcie_gen_bootup_value;
	u16 pcie_lane_bootup_value;
};

struct ci_clock_registers {
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

struct ci_thermal_temperature_setting {
	s32 temperature_low;
	s32 temperature_high;
	s32 temperature_shutdown;
};

struct ci_pcie_perf_range {
	u16 max;
	u16 min;
};

enum ci_pt_config_reg_type {
	CISLANDS_CONFIGREG_MMR = 0,
	CISLANDS_CONFIGREG_SMC_IND,
	CISLANDS_CONFIGREG_DIDT_IND,
	CISLANDS_CONFIGREG_CACHE,
	CISLANDS_CONFIGREG_MAX
};

#define POWERCONTAINMENT_FEATURE_BAPM            0x00000001
#define POWERCONTAINMENT_FEATURE_TDCLimit        0x00000002
#define POWERCONTAINMENT_FEATURE_PkgPwrLimit     0x00000004

struct ci_pt_config_reg {
	u32 offset;
	u32 mask;
	u32 shift;
	u32 value;
	enum ci_pt_config_reg_type type;
};

struct ci_pt_defaults {
	u8 svi_load_line_en;
	u8 svi_load_line_vddc;
	u8 tdc_vddc_throttle_release_limit_perc;
	u8 tdc_mawt;
	u8 tdc_waterfall_ctl;
	u8 dte_ambient_temp_base;
	u32 display_cac;
	u32 bapm_temp_gradient;
	u16 bapmti_r[SMU7_DTE_ITERATIONS * SMU7_DTE_SOURCES * SMU7_DTE_SINKS];
	u16 bapmti_rc[SMU7_DTE_ITERATIONS * SMU7_DTE_SOURCES * SMU7_DTE_SINKS];
};

#define DPMTABLE_OD_UPDATE_SCLK     0x00000001
#define DPMTABLE_OD_UPDATE_MCLK     0x00000002
#define DPMTABLE_UPDATE_SCLK        0x00000004
#define DPMTABLE_UPDATE_MCLK        0x00000008

struct ci_power_info {
	struct ci_dpm_table dpm_table;
	u32 voltage_control;
	u32 mvdd_control;
	u32 vddci_control;
	u32 active_auto_throttle_sources;
	struct ci_clock_registers clock_registers;
	u16 acpi_vddc;
	u16 acpi_vddci;
	enum radeon_pcie_gen force_pcie_gen;
	enum radeon_pcie_gen acpi_pcie_gen;
	struct ci_leakage_voltage vddc_leakage;
	struct ci_leakage_voltage vddci_leakage;
	u16 max_vddc_in_pp_table;
	u16 min_vddc_in_pp_table;
	u16 max_vddci_in_pp_table;
	u16 min_vddci_in_pp_table;
	u32 mclk_strobe_mode_threshold;
	u32 mclk_stutter_mode_threshold;
	u32 mclk_edc_enable_threshold;
	u32 mclk_edc_wr_enable_threshold;
	struct ci_vbios_boot_state vbios_boot_state;
	/* smc offsets */
	u32 sram_end;
	u32 dpm_table_start;
	u32 soft_regs_start;
	u32 mc_reg_table_start;
	u32 fan_table_start;
	u32 arb_table_start;
	/* smc tables */
	SMU7_Discrete_DpmTable smc_state_table;
	SMU7_Discrete_MCRegisters smc_mc_reg_table;
	SMU7_Discrete_PmFuses smc_powertune_table;
	/* other stuff */
	struct ci_mc_reg_table mc_reg_table;
	struct atom_voltage_table vddc_voltage_table;
	struct atom_voltage_table vddci_voltage_table;
	struct atom_voltage_table mvdd_voltage_table;
	struct ci_ulv_parm ulv;
	u32 power_containment_features;
	const struct ci_pt_defaults *powertune_defaults;
	u32 dte_tj_offset;
	bool vddc_phase_shed_control;
	struct ci_thermal_temperature_setting thermal_temp_setting;
	struct ci_dpm_level_enable_mask dpm_level_enable_mask;
	u32 need_update_smu7_dpm_table;
	u32 sclk_dpm_key_disabled;
	u32 mclk_dpm_key_disabled;
	u32 pcie_dpm_key_disabled;
	u32 thermal_sclk_dpm_enabled;
	struct ci_pcie_perf_range pcie_gen_performance;
	struct ci_pcie_perf_range pcie_lane_performance;
	struct ci_pcie_perf_range pcie_gen_powersaving;
	struct ci_pcie_perf_range pcie_lane_powersaving;
	u32 activity_target[SMU7_MAX_LEVELS_GRAPHICS];
	u32 mclk_activity_target;
	u32 low_sclk_interrupt_t;
	u32 last_mclk_dpm_enable_mask;
	u32 sys_pcie_mask;
	/* caps */
	bool caps_power_containment;
	bool caps_cac;
	bool caps_sq_ramping;
	bool caps_db_ramping;
	bool caps_td_ramping;
	bool caps_tcp_ramping;
	bool caps_fps;
	bool caps_sclk_ds;
	bool caps_sclk_ss_support;
	bool caps_mclk_ss_support;
	bool caps_uvd_dpm;
	bool caps_vce_dpm;
	bool caps_samu_dpm;
	bool caps_acp_dpm;
	bool caps_automatic_dc_transition;
	bool caps_sclk_throttle_low_notification;
	bool caps_dynamic_ac_timing;
	bool caps_od_fuzzy_fan_control_support;
	/* flags */
	bool thermal_protection;
	bool pcie_performance_request;
	bool dynamic_ss;
	bool dll_default_on;
	bool cac_enabled;
	bool uvd_enabled;
	bool battery_state;
	bool pspp_notify_required;
	bool mem_gddr5;
	bool enable_bapm_feature;
	bool enable_tdc_limit_feature;
	bool enable_pkg_pwr_tracking_feature;
	bool use_pcie_performance_levels;
	bool use_pcie_powersaving_levels;
	bool uvd_power_gated;
	/* driver states */
	struct radeon_ps current_rps;
	struct ci_ps current_ps;
	struct radeon_ps requested_rps;
	struct ci_ps requested_ps;
	/* fan control */
	bool fan_ctrl_is_in_default_mode;
	bool fan_is_controlled_by_smc;
	u32 t_min;
	u32 fan_ctrl_default_mode;
};

#define CISLANDS_VOLTAGE_CONTROL_NONE                   0x0
#define CISLANDS_VOLTAGE_CONTROL_BY_GPIO                0x1
#define CISLANDS_VOLTAGE_CONTROL_BY_SVID2               0x2

#define CISLANDS_Q88_FORMAT_CONVERSION_UNIT             256

#define CISLANDS_VRC_DFLT0                              0x3FFFC000
#define CISLANDS_VRC_DFLT1                              0x000400
#define CISLANDS_VRC_DFLT2                              0xC00080
#define CISLANDS_VRC_DFLT3                              0xC00200
#define CISLANDS_VRC_DFLT4                              0xC01680
#define CISLANDS_VRC_DFLT5                              0xC00033
#define CISLANDS_VRC_DFLT6                              0xC00033
#define CISLANDS_VRC_DFLT7                              0x3FFFC000

#define CISLANDS_CGULVPARAMETER_DFLT                    0x00040035
#define CISLAND_TARGETACTIVITY_DFLT                     30
#define CISLAND_MCLK_TARGETACTIVITY_DFLT                10

#define PCIE_PERF_REQ_REMOVE_REGISTRY   0
#define PCIE_PERF_REQ_FORCE_LOWPOWER    1
#define PCIE_PERF_REQ_PECI_GEN1         2
#define PCIE_PERF_REQ_PECI_GEN2         3
#define PCIE_PERF_REQ_PECI_GEN3         4

int ci_copy_bytes_to_smc(struct radeon_device *rdev,
			 u32 smc_start_address,
			 const u8 *src, u32 byte_count, u32 limit);
void ci_start_smc(struct radeon_device *rdev);
void ci_reset_smc(struct radeon_device *rdev);
int ci_program_jump_on_start(struct radeon_device *rdev);
void ci_stop_smc_clock(struct radeon_device *rdev);
void ci_start_smc_clock(struct radeon_device *rdev);
bool ci_is_smc_running(struct radeon_device *rdev);
PPSMC_Result ci_send_msg_to_smc(struct radeon_device *rdev, PPSMC_Msg msg);
PPSMC_Result ci_wait_for_smc_inactive(struct radeon_device *rdev);
int ci_load_smc_ucode(struct radeon_device *rdev, u32 limit);
int ci_read_smc_sram_dword(struct radeon_device *rdev,
			   u32 smc_address, u32 *value, u32 limit);
int ci_write_smc_sram_dword(struct radeon_device *rdev,
			    u32 smc_address, u32 value, u32 limit);

#endif
