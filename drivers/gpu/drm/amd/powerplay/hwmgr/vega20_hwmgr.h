/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef _VEGA20_HWMGR_H_
#define _VEGA20_HWMGR_H_

#include "hwmgr.h"
#include "smu11_driver_if.h"
#include "ppatomfwctrl.h"

#define VEGA20_MAX_HARDWARE_POWERLEVELS 2

#define WaterMarksExist  1
#define WaterMarksLoaded 2

#define VG20_PSUEDO_NUM_GFXCLK_DPM_LEVELS   8
#define VG20_PSUEDO_NUM_SOCCLK_DPM_LEVELS   8
#define VG20_PSUEDO_NUM_DCEFCLK_DPM_LEVELS  8
#define VG20_PSUEDO_NUM_UCLK_DPM_LEVELS     4

//OverDriver8 macro defs
#define AVFS_CURVE 0
#define OD8_HOTCURVE_TEMPERATURE 85

#define VG20_CLOCK_MAX_DEFAULT 0xFFFF

typedef uint32_t PP_Clock;

enum {
	GNLD_DPM_PREFETCHER = 0,
	GNLD_DPM_GFXCLK,
	GNLD_DPM_UCLK,
	GNLD_DPM_SOCCLK,
	GNLD_DPM_UVD,
	GNLD_DPM_VCE,
	GNLD_ULV,
	GNLD_DPM_MP0CLK,
	GNLD_DPM_LINK,
	GNLD_DPM_DCEFCLK,
	GNLD_DS_GFXCLK,
	GNLD_DS_SOCCLK,
	GNLD_DS_LCLK,
	GNLD_PPT,
	GNLD_TDC,
	GNLD_THERMAL,
	GNLD_GFX_PER_CU_CG,
	GNLD_RM,
	GNLD_DS_DCEFCLK,
	GNLD_ACDC,
	GNLD_VR0HOT,
	GNLD_VR1HOT,
	GNLD_FW_CTF,
	GNLD_LED_DISPLAY,
	GNLD_FAN_CONTROL,
	GNLD_DIDT,
	GNLD_GFXOFF,
	GNLD_CG,
	GNLD_DPM_FCLK,
	GNLD_DS_FCLK,
	GNLD_DS_MP1CLK,
	GNLD_DS_MP0CLK,
	GNLD_XGMI,
	GNLD_ECC,

	GNLD_FEATURES_MAX
};


#define GNLD_DPM_MAX    (GNLD_DPM_DCEFCLK + 1)

#define SMC_DPM_FEATURES    0x30F

struct smu_features {
	bool supported;
	bool enabled;
	bool allowed;
	uint32_t smu_feature_id;
	uint64_t smu_feature_bitmap;
};

struct vega20_performance_level {
	uint32_t  soc_clock;
	uint32_t  gfx_clock;
	uint32_t  mem_clock;
};

struct vega20_bacos {
	uint32_t                       baco_flags;
	/* struct vega20_performance_level  performance_level; */
};

struct vega20_uvd_clocks {
	uint32_t  vclk;
	uint32_t  dclk;
};

struct vega20_vce_clocks {
	uint32_t  evclk;
	uint32_t  ecclk;
};

struct vega20_power_state {
	uint32_t                  magic;
	struct vega20_uvd_clocks    uvd_clks;
	struct vega20_vce_clocks    vce_clks;
	uint16_t                  performance_level_count;
	bool                      dc_compatible;
	uint32_t                  sclk_threshold;
	struct vega20_performance_level  performance_levels[VEGA20_MAX_HARDWARE_POWERLEVELS];
};

struct vega20_dpm_level {
	bool		enabled;
	uint32_t	value;
	uint32_t	param1;
};

#define VEGA20_MAX_DEEPSLEEP_DIVIDER_ID 5
#define MAX_REGULAR_DPM_NUMBER 16
#define MAX_PCIE_CONF 2
#define VEGA20_MINIMUM_ENGINE_CLOCK 2500

struct vega20_max_sustainable_clocks {
	PP_Clock display_clock;
	PP_Clock phy_clock;
	PP_Clock pixel_clock;
	PP_Clock uclock;
	PP_Clock dcef_clock;
	PP_Clock soc_clock;
};

struct vega20_dpm_state {
	uint32_t  soft_min_level;
	uint32_t  soft_max_level;
	uint32_t  hard_min_level;
	uint32_t  hard_max_level;
};

struct vega20_single_dpm_table {
	uint32_t		count;
	struct vega20_dpm_state	dpm_state;
	struct vega20_dpm_level	dpm_levels[MAX_REGULAR_DPM_NUMBER];
};

struct vega20_odn_dpm_control {
	uint32_t	count;
	uint32_t	entries[MAX_REGULAR_DPM_NUMBER];
};

struct vega20_pcie_table {
	uint16_t count;
	uint8_t  pcie_gen[MAX_PCIE_CONF];
	uint8_t  pcie_lane[MAX_PCIE_CONF];
	uint32_t lclk[MAX_PCIE_CONF];
};

struct vega20_dpm_table {
	struct vega20_single_dpm_table  soc_table;
	struct vega20_single_dpm_table  gfx_table;
	struct vega20_single_dpm_table  mem_table;
	struct vega20_single_dpm_table  eclk_table;
	struct vega20_single_dpm_table  vclk_table;
	struct vega20_single_dpm_table  dclk_table;
	struct vega20_single_dpm_table  dcef_table;
	struct vega20_single_dpm_table  pixel_table;
	struct vega20_single_dpm_table  display_table;
	struct vega20_single_dpm_table  phy_table;
	struct vega20_single_dpm_table  fclk_table;
	struct vega20_pcie_table        pcie_table;
};

#define VEGA20_MAX_LEAKAGE_COUNT  8
struct vega20_leakage_voltage {
	uint16_t  count;
	uint16_t  leakage_id[VEGA20_MAX_LEAKAGE_COUNT];
	uint16_t  actual_voltage[VEGA20_MAX_LEAKAGE_COUNT];
};

struct vega20_display_timing {
	uint32_t  min_clock_in_sr;
	uint32_t  num_existing_displays;
};

struct vega20_dpmlevel_enable_mask {
	uint32_t  uvd_dpm_enable_mask;
	uint32_t  vce_dpm_enable_mask;
	uint32_t  samu_dpm_enable_mask;
	uint32_t  sclk_dpm_enable_mask;
	uint32_t  mclk_dpm_enable_mask;
};

struct vega20_vbios_boot_state {
	uint8_t     uc_cooling_id;
	uint16_t    vddc;
	uint16_t    vddci;
	uint16_t    mvddc;
	uint16_t    vdd_gfx;
	uint32_t    gfx_clock;
	uint32_t    mem_clock;
	uint32_t    soc_clock;
	uint32_t    dcef_clock;
	uint32_t    eclock;
	uint32_t    dclock;
	uint32_t    vclock;
	uint32_t    fclock;
};

#define DPMTABLE_OD_UPDATE_SCLK     0x00000001
#define DPMTABLE_OD_UPDATE_MCLK     0x00000002
#define DPMTABLE_UPDATE_SCLK        0x00000004
#define DPMTABLE_UPDATE_MCLK        0x00000008
#define DPMTABLE_OD_UPDATE_VDDC     0x00000010
#define DPMTABLE_OD_UPDATE_SCLK_MASK     0x00000020
#define DPMTABLE_OD_UPDATE_MCLK_MASK     0x00000040

// To determine if sclk and mclk are in overdrive state
#define SCLK_MASK_OVERDRIVE_ENABLED      0x00000008
#define MCLK_MASK_OVERDRIVE_ENABLED      0x00000010
#define SOCCLK_OVERDRIVE_ENABLED         0x00000020

struct vega20_smc_state_table {
	uint32_t        soc_boot_level;
	uint32_t        gfx_boot_level;
	uint32_t        dcef_boot_level;
	uint32_t        mem_boot_level;
	uint32_t        uvd_boot_level;
	uint32_t        vce_boot_level;
	uint32_t        gfx_max_level;
	uint32_t        mem_max_level;
	uint8_t         vr_hot_gpio;
	uint8_t         ac_dc_gpio;
	uint8_t         therm_out_gpio;
	uint8_t         therm_out_polarity;
	uint8_t         therm_out_mode;
	PPTable_t       pp_table;
	Watermarks_t    water_marks_table;
	AvfsDebugTable_t avfs_debug_table;
	AvfsFuseOverride_t avfs_fuse_override_table;
	SmuMetrics_t    smu_metrics;
	DriverSmuConfig_t driver_smu_config;
	DpmActivityMonitorCoeffInt_t dpm_activity_monitor_coeffint;
	OverDriveTable_t overdrive_table;
};

struct vega20_mclk_latency_entries {
	uint32_t  frequency;
	uint32_t  latency;
};

struct vega20_mclk_latency_table {
	uint32_t  count;
	struct vega20_mclk_latency_entries  entries[MAX_REGULAR_DPM_NUMBER];
};

struct vega20_registry_data {
	uint64_t  disallowed_features;
	uint8_t   ac_dc_switch_gpio_support;
	uint8_t   acg_loop_support;
	uint8_t   clock_stretcher_support;
	uint8_t   db_ramping_support;
	uint8_t   didt_mode;
	uint8_t   didt_support;
	uint8_t   edc_didt_support;
	uint8_t   force_dpm_high;
	uint8_t   fuzzy_fan_control_support;
	uint8_t   mclk_dpm_key_disabled;
	uint8_t   od_state_in_dc_support;
	uint8_t   pcie_lane_override;
	uint8_t   pcie_speed_override;
	uint32_t  pcie_clock_override;
	uint8_t   pcie_dpm_key_disabled;
	uint8_t   dcefclk_dpm_key_disabled;
	uint8_t   prefetcher_dpm_key_disabled;
	uint8_t   quick_transition_support;
	uint8_t   regulator_hot_gpio_support;
	uint8_t   master_deep_sleep_support;
	uint8_t   gfx_clk_deep_sleep_support;
	uint8_t   sclk_deep_sleep_support;
	uint8_t   lclk_deep_sleep_support;
	uint8_t   dce_fclk_deep_sleep_support;
	uint8_t   sclk_dpm_key_disabled;
	uint8_t   sclk_throttle_low_notification;
	uint8_t   skip_baco_hardware;
	uint8_t   socclk_dpm_key_disabled;
	uint8_t   sq_ramping_support;
	uint8_t   tcp_ramping_support;
	uint8_t   td_ramping_support;
	uint8_t   dbr_ramping_support;
	uint8_t   gc_didt_support;
	uint8_t   psm_didt_support;
	uint8_t   thermal_support;
	uint8_t   fw_ctf_enabled;
	uint8_t   led_dpm_enabled;
	uint8_t   fan_control_support;
	uint8_t   ulv_support;
	uint8_t   od8_feature_enable;
	uint8_t   disable_water_mark;
	uint8_t   disable_workload_policy;
	uint32_t  force_workload_policy_mask;
	uint8_t   disable_3d_fs_detection;
	uint8_t   disable_pp_tuning;
	uint8_t   disable_xlpp_tuning;
	uint32_t  perf_ui_tuning_profile_turbo;
	uint32_t  perf_ui_tuning_profile_powerSave;
	uint32_t  perf_ui_tuning_profile_xl;
	uint16_t  zrpm_stop_temp;
	uint16_t  zrpm_start_temp;
	uint32_t  stable_pstate_sclk_dpm_percentage;
	uint8_t   fps_support;
	uint8_t   vr0hot;
	uint8_t   vr1hot;
	uint8_t   disable_auto_wattman;
	uint32_t  auto_wattman_debug;
	uint32_t  auto_wattman_sample_period;
	uint32_t  fclk_gfxclk_ratio;
	uint8_t   auto_wattman_threshold;
	uint8_t   log_avfs_param;
	uint8_t   enable_enginess;
	uint8_t   custom_fan_support;
	uint8_t   disable_pcc_limit_control;
	uint8_t   gfxoff_controlled_by_driver;
};

struct vega20_odn_clock_voltage_dependency_table {
	uint32_t count;
	struct phm_ppt_v1_clock_voltage_dependency_record
		entries[MAX_REGULAR_DPM_NUMBER];
};

struct vega20_odn_dpm_table {
	struct vega20_odn_dpm_control		control_gfxclk_state;
	struct vega20_odn_dpm_control		control_memclk_state;
	struct phm_odn_clock_levels		odn_core_clock_dpm_levels;
	struct phm_odn_clock_levels		odn_memory_clock_dpm_levels;
	struct vega20_odn_clock_voltage_dependency_table		vdd_dependency_on_sclk;
	struct vega20_odn_clock_voltage_dependency_table		vdd_dependency_on_mclk;
	struct vega20_odn_clock_voltage_dependency_table		vdd_dependency_on_socclk;
	uint32_t				odn_mclk_min_limit;
};

struct vega20_odn_fan_table {
	uint32_t	target_fan_speed;
	uint32_t	target_temperature;
	uint32_t	min_performance_clock;
	uint32_t	min_fan_limit;
	bool		force_fan_pwm;
};

struct vega20_odn_temp_table {
	uint16_t	target_operating_temp;
	uint16_t	default_target_operating_temp;
	uint16_t	operating_temp_min_limit;
	uint16_t	operating_temp_max_limit;
	uint16_t	operating_temp_step;
};

struct vega20_odn_data {
	uint32_t	apply_overdrive_next_settings_mask;
	uint32_t	overdrive_next_state;
	uint32_t	overdrive_next_capabilities;
	uint32_t	odn_sclk_dpm_enable_mask;
	uint32_t	odn_mclk_dpm_enable_mask;
	struct vega20_odn_dpm_table	odn_dpm_table;
	struct vega20_odn_fan_table	odn_fan_table;
	struct vega20_odn_temp_table	odn_temp_table;
};

enum OD8_FEATURE_ID
{
	OD8_GFXCLK_LIMITS               = 1 << 0,
	OD8_GFXCLK_CURVE                = 1 << 1,
	OD8_UCLK_MAX                    = 1 << 2,
	OD8_POWER_LIMIT                 = 1 << 3,
	OD8_ACOUSTIC_LIMIT_SCLK         = 1 << 4,   //FanMaximumRpm
	OD8_FAN_SPEED_MIN               = 1 << 5,   //FanMinimumPwm
	OD8_TEMPERATURE_FAN             = 1 << 6,   //FanTargetTemperature
	OD8_TEMPERATURE_SYSTEM          = 1 << 7,   //MaxOpTemp
	OD8_MEMORY_TIMING_TUNE          = 1 << 8,
	OD8_FAN_ZERO_RPM_CONTROL        = 1 << 9
};

enum OD8_SETTING_ID
{
	OD8_SETTING_GFXCLK_FMIN = 0,
	OD8_SETTING_GFXCLK_FMAX,
	OD8_SETTING_GFXCLK_FREQ1,
	OD8_SETTING_GFXCLK_VOLTAGE1,
	OD8_SETTING_GFXCLK_FREQ2,
	OD8_SETTING_GFXCLK_VOLTAGE2,
	OD8_SETTING_GFXCLK_FREQ3,
	OD8_SETTING_GFXCLK_VOLTAGE3,
	OD8_SETTING_UCLK_FMAX,
	OD8_SETTING_POWER_PERCENTAGE,
	OD8_SETTING_FAN_ACOUSTIC_LIMIT,
	OD8_SETTING_FAN_MIN_SPEED,
	OD8_SETTING_FAN_TARGET_TEMP,
	OD8_SETTING_OPERATING_TEMP_MAX,
	OD8_SETTING_AC_TIMING,
	OD8_SETTING_FAN_ZERO_RPM_CONTROL,
	OD8_SETTING_COUNT
};

struct vega20_od8_single_setting {
	uint32_t	feature_id;
	int32_t		min_value;
	int32_t		max_value;
	int32_t		current_value;
	int32_t		default_value;
};

struct vega20_od8_settings {
	uint32_t	overdrive8_capabilities;
	struct vega20_od8_single_setting	od8_settings_array[OD8_SETTING_COUNT];
};

struct vega20_hwmgr {
	struct vega20_dpm_table          dpm_table;
	struct vega20_dpm_table          golden_dpm_table;
	struct vega20_registry_data      registry_data;
	struct vega20_vbios_boot_state   vbios_boot_state;
	struct vega20_mclk_latency_table mclk_latency_table;

	struct vega20_max_sustainable_clocks max_sustainable_clocks;

	struct vega20_leakage_voltage    vddc_leakage;

	uint32_t                           vddc_control;
	struct pp_atomfwctrl_voltage_table vddc_voltage_table;
	uint32_t                           mvdd_control;
	struct pp_atomfwctrl_voltage_table mvdd_voltage_table;
	uint32_t                           vddci_control;
	struct pp_atomfwctrl_voltage_table vddci_voltage_table;

	uint32_t                           active_auto_throttle_sources;
	struct vega20_bacos                bacos;

	/* ---- General data ---- */
	uint8_t                           need_update_dpm_table;

	bool                           cac_enabled;
	bool                           battery_state;
	bool                           is_tlu_enabled;
	bool                           avfs_exist;

	uint32_t                       low_sclk_interrupt_threshold;

	uint32_t                       total_active_cus;

	uint32_t                       water_marks_bitmap;

	struct vega20_display_timing display_timing;

	/* ---- Vega20 Dyn Register Settings ---- */

	uint32_t                       debug_settings;
	uint32_t                       lowest_uclk_reserved_for_ulv;
	uint32_t                       gfxclk_average_alpha;
	uint32_t                       socclk_average_alpha;
	uint32_t                       uclk_average_alpha;
	uint32_t                       gfx_activity_average_alpha;
	uint32_t                       display_voltage_mode;
	uint32_t                       dcef_clk_quad_eqn_a;
	uint32_t                       dcef_clk_quad_eqn_b;
	uint32_t                       dcef_clk_quad_eqn_c;
	uint32_t                       disp_clk_quad_eqn_a;
	uint32_t                       disp_clk_quad_eqn_b;
	uint32_t                       disp_clk_quad_eqn_c;
	uint32_t                       pixel_clk_quad_eqn_a;
	uint32_t                       pixel_clk_quad_eqn_b;
	uint32_t                       pixel_clk_quad_eqn_c;
	uint32_t                       phy_clk_quad_eqn_a;
	uint32_t                       phy_clk_quad_eqn_b;
	uint32_t                       phy_clk_quad_eqn_c;

	/* ---- Thermal Temperature Setting ---- */
	struct vega20_dpmlevel_enable_mask     dpm_level_enable_mask;

	/* ---- Power Gating States ---- */
	bool                           uvd_power_gated;
	bool                           vce_power_gated;
	bool                           samu_power_gated;
	bool                           need_long_memory_training;

	/* Internal settings to apply the application power optimization parameters */
	bool                           apply_optimized_settings;
	uint32_t                       disable_dpm_mask;

	/* ---- Overdrive next setting ---- */
	struct vega20_odn_data         odn_data;
	bool                           gfxclk_overdrive;
	bool                           memclk_overdrive;

	/* ---- Overdrive8 Setting ---- */
	struct vega20_od8_settings     od8_settings;

	/* ---- Workload Mask ---- */
	uint32_t                       workload_mask;

	/* ---- SMU9 ---- */
	uint32_t                       smu_version;
	struct smu_features            smu_features[GNLD_FEATURES_MAX];
	struct vega20_smc_state_table  smc_state_table;

	/* ---- Gfxoff ---- */
	bool                           gfxoff_allowed;
	uint32_t                       counter_gfxoff;

	unsigned long                  metrics_time;
	SmuMetrics_t                   metrics_table;

	bool                           pcie_parameters_override;
	uint32_t                       pcie_gen_level1;
	uint32_t                       pcie_width_level1;

	bool                           is_custom_profile_set;
};

#define VEGA20_DPM2_NEAR_TDP_DEC                      10
#define VEGA20_DPM2_ABOVE_SAFE_INC                    5
#define VEGA20_DPM2_BELOW_SAFE_INC                    20

#define VEGA20_DPM2_LTA_WINDOW_SIZE                   7

#define VEGA20_DPM2_LTS_TRUNCATE                      0

#define VEGA20_DPM2_TDP_SAFE_LIMIT_PERCENT            80

#define VEGA20_DPM2_MAXPS_PERCENT_M                   90
#define VEGA20_DPM2_MAXPS_PERCENT_H                   90

#define VEGA20_DPM2_PWREFFICIENCYRATIO_MARGIN         50

#define VEGA20_DPM2_SQ_RAMP_MAX_POWER                 0x3FFF
#define VEGA20_DPM2_SQ_RAMP_MIN_POWER                 0x12
#define VEGA20_DPM2_SQ_RAMP_MAX_POWER_DELTA           0x15
#define VEGA20_DPM2_SQ_RAMP_SHORT_TERM_INTERVAL_SIZE  0x1E
#define VEGA20_DPM2_SQ_RAMP_LONG_TERM_INTERVAL_RATIO  0xF

#define VEGA20_VOLTAGE_CONTROL_NONE                   0x0
#define VEGA20_VOLTAGE_CONTROL_BY_GPIO                0x1
#define VEGA20_VOLTAGE_CONTROL_BY_SVID2               0x2
#define VEGA20_VOLTAGE_CONTROL_MERGED                 0x3
/* To convert to Q8.8 format for firmware */
#define VEGA20_Q88_FORMAT_CONVERSION_UNIT             256

#define VEGA20_UNUSED_GPIO_PIN       0x7F

#define VEGA20_THERM_OUT_MODE_DISABLE       0x0
#define VEGA20_THERM_OUT_MODE_THERM_ONLY    0x1
#define VEGA20_THERM_OUT_MODE_THERM_VRHOT   0x2

#define PPVEGA20_VEGA20DISPLAYVOLTAGEMODE_DFLT   0xffffffff
#define PPREGKEY_VEGA20QUADRATICEQUATION_DFLT    0xffffffff

#define PPVEGA20_VEGA20GFXCLKAVERAGEALPHA_DFLT       25 /* 10% * 255 = 25 */
#define PPVEGA20_VEGA20SOCCLKAVERAGEALPHA_DFLT       25 /* 10% * 255 = 25 */
#define PPVEGA20_VEGA20UCLKCLKAVERAGEALPHA_DFLT      25 /* 10% * 255 = 25 */
#define PPVEGA20_VEGA20GFXACTIVITYAVERAGEALPHA_DFLT  25 /* 10% * 255 = 25 */
#define PPVEGA20_VEGA20LOWESTUCLKRESERVEDFORULV_DFLT   0xffffffff
#define PPVEGA20_VEGA20DISPLAYVOLTAGEMODE_DFLT         0xffffffff
#define PPREGKEY_VEGA20QUADRATICEQUATION_DFLT          0xffffffff

#define VEGA20_UMD_PSTATE_GFXCLK_LEVEL         0x3
#define VEGA20_UMD_PSTATE_SOCCLK_LEVEL         0x3
#define VEGA20_UMD_PSTATE_MCLK_LEVEL           0x2
#define VEGA20_UMD_PSTATE_UVDCLK_LEVEL         0x3
#define VEGA20_UMD_PSTATE_VCEMCLK_LEVEL        0x3

#endif /* _VEGA20_HWMGR_H_ */
