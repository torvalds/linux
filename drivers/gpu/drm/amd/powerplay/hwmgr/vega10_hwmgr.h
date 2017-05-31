/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef _VEGA10_HWMGR_H_
#define _VEGA10_HWMGR_H_

#include "hwmgr.h"
#include "smu9_driver_if.h"
#include "ppatomctrl.h"
#include "ppatomfwctrl.h"
#include "vega10_ppsmc.h"
#include "vega10_powertune.h"

extern const uint32_t PhwVega10_Magic;
#define VEGA10_MAX_HARDWARE_POWERLEVELS 2

#define WaterMarksExist  1
#define WaterMarksLoaded 2

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
	GNLD_AVFS,
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
	GNLD_VOLTAGE_CONTROLLER,
	GNLD_FEATURES_MAX
};

#define GNLD_DPM_MAX    (GNLD_DPM_DCEFCLK + 1)

#define SMC_DPM_FEATURES    0x30F

struct smu_features {
	bool supported;
	bool enabled;
	uint32_t smu_feature_id;
	uint32_t smu_feature_bitmap;
};

struct vega10_performance_level {
	uint32_t  soc_clock;
	uint32_t  gfx_clock;
	uint32_t  mem_clock;
};

struct vega10_bacos {
	uint32_t                       baco_flags;
	/* struct vega10_performance_level  performance_level; */
};

struct vega10_uvd_clocks {
	uint32_t  vclk;
	uint32_t  dclk;
};

struct vega10_vce_clocks {
	uint32_t  evclk;
	uint32_t  ecclk;
};

struct vega10_power_state {
	uint32_t                  magic;
	struct vega10_uvd_clocks    uvd_clks;
	struct vega10_vce_clocks    vce_clks;
	uint16_t                  performance_level_count;
	bool                      dc_compatible;
	uint32_t                  sclk_threshold;
	struct vega10_performance_level  performance_levels[VEGA10_MAX_HARDWARE_POWERLEVELS];
};

struct vega10_dpm_level {
	bool	enabled;
	uint32_t	value;
	uint32_t	param1;
};

#define VEGA10_MAX_DEEPSLEEP_DIVIDER_ID 5
#define MAX_REGULAR_DPM_NUMBER 8
#define MAX_PCIE_CONF 2
#define VEGA10_MINIMUM_ENGINE_CLOCK 2500

struct vega10_dpm_state {
	uint32_t  soft_min_level;
	uint32_t  soft_max_level;
	uint32_t  hard_min_level;
	uint32_t  hard_max_level;
};

struct vega10_single_dpm_table {
	uint32_t		count;
	struct vega10_dpm_state	dpm_state;
	struct vega10_dpm_level	dpm_levels[MAX_REGULAR_DPM_NUMBER];
};

struct vega10_pcie_table {
	uint16_t count;
	uint8_t  pcie_gen[MAX_PCIE_CONF];
	uint8_t  pcie_lane[MAX_PCIE_CONF];
	uint32_t lclk[MAX_PCIE_CONF];
};

struct vega10_dpm_table {
	struct vega10_single_dpm_table  soc_table;
	struct vega10_single_dpm_table  gfx_table;
	struct vega10_single_dpm_table  mem_table;
	struct vega10_single_dpm_table  eclk_table;
	struct vega10_single_dpm_table  vclk_table;
	struct vega10_single_dpm_table  dclk_table;
	struct vega10_single_dpm_table  dcef_table;
	struct vega10_single_dpm_table  pixel_table;
	struct vega10_single_dpm_table  display_table;
	struct vega10_single_dpm_table  phy_table;
	struct vega10_pcie_table        pcie_table;
};

#define VEGA10_MAX_LEAKAGE_COUNT  8
struct vega10_leakage_voltage {
	uint16_t  count;
	uint16_t  leakage_id[VEGA10_MAX_LEAKAGE_COUNT];
	uint16_t  actual_voltage[VEGA10_MAX_LEAKAGE_COUNT];
};

struct vega10_display_timing {
	uint32_t  min_clock_in_sr;
	uint32_t  num_existing_displays;
};

struct vega10_dpmlevel_enable_mask {
	uint32_t  uvd_dpm_enable_mask;
	uint32_t  vce_dpm_enable_mask;
	uint32_t  acp_dpm_enable_mask;
	uint32_t  samu_dpm_enable_mask;
	uint32_t  sclk_dpm_enable_mask;
	uint32_t  mclk_dpm_enable_mask;
};

struct vega10_vbios_boot_state {
	bool        bsoc_vddc_lock;
	uint16_t    vddc;
	uint16_t    vddci;
	uint16_t    mvddc;
	uint16_t    vdd_gfx;
	uint32_t    gfx_clock;
	uint32_t    mem_clock;
	uint32_t    soc_clock;
	uint32_t    dcef_clock;
};

#define DPMTABLE_OD_UPDATE_SCLK     0x00000001
#define DPMTABLE_OD_UPDATE_MCLK     0x00000002
#define DPMTABLE_UPDATE_SCLK        0x00000004
#define DPMTABLE_UPDATE_MCLK        0x00000008
#define DPMTABLE_OD_UPDATE_VDDC     0x00000010

struct vega10_smc_state_table {
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
	AvfsTable_t     avfs_table;
	AvfsFuseOverride_t avfs_fuse_override_table;
};

struct vega10_mclk_latency_entries {
	uint32_t  frequency;
	uint32_t  latency;
};

struct vega10_mclk_latency_table {
	uint32_t  count;
	struct vega10_mclk_latency_entries  entries[MAX_REGULAR_DPM_NUMBER];
};

struct vega10_registry_data {
	uint8_t   ac_dc_switch_gpio_support;
	uint8_t   avfs_support;
	uint8_t   cac_support;
	uint8_t   clock_stretcher_support;
	uint8_t   db_ramping_support;
	uint8_t   didt_support;
	uint8_t   dynamic_state_patching_support;
	uint8_t   enable_pkg_pwr_tracking_feature;
	uint8_t   enable_tdc_limit_feature;
	uint32_t  fast_watermark_threshold;
	uint8_t   force_dpm_high;
	uint8_t   fuzzy_fan_control_support;
	uint8_t   long_idle_baco_support;
	uint8_t   mclk_dpm_key_disabled;
	uint8_t   od_state_in_dc_support;
	uint8_t   pcieLaneOverride;
	uint8_t   pcieSpeedOverride;
	uint32_t  pcieClockOverride;
	uint8_t   pcie_dpm_key_disabled;
	uint8_t   dcefclk_dpm_key_disabled;
	uint8_t   power_containment_support;
	uint8_t   ppt_support;
	uint8_t   prefetcher_dpm_key_disabled;
	uint8_t   quick_transition_support;
	uint8_t   regulator_hot_gpio_support;
	uint8_t   sclk_deep_sleep_support;
	uint8_t   sclk_dpm_key_disabled;
	uint8_t   sclk_from_vbios;
	uint8_t   sclk_throttle_low_notification;
	uint8_t   show_baco_dbg_info;
	uint8_t   skip_baco_hardware;
	uint8_t   socclk_dpm_key_disabled;
	uint8_t   spll_shutdown_support;
	uint8_t   sq_ramping_support;
	uint32_t  stable_pstate_sclk_dpm_percentage;
	uint8_t   tcp_ramping_support;
	uint8_t   tdc_support;
	uint8_t   td_ramping_support;
	uint8_t   thermal_out_gpio_support;
	uint8_t   thermal_support;
	uint8_t   fw_ctf_enabled;
	uint8_t   fan_control_support;
	uint8_t   ulps_support;
	uint8_t   ulv_support;
	uint32_t  vddc_vddci_delta;
	uint8_t   odn_feature_enable;
	uint8_t   disable_water_mark;
	uint8_t   zrpm_stop_temp;
	uint8_t   zrpm_start_temp;
	uint8_t   led_dpm_enabled;
	uint8_t   vr0hot_enabled;
	uint8_t   vr1hot_enabled;
};

struct vega10_odn_clock_voltage_dependency_table {
	uint32_t count;
	struct phm_ppt_v1_clock_voltage_dependency_record
		entries[MAX_REGULAR_DPM_NUMBER];
};

struct vega10_odn_dpm_table {
	struct phm_odn_clock_levels		odn_core_clock_dpm_levels;
	struct phm_odn_clock_levels		odn_memory_clock_dpm_levels;
	struct vega10_odn_clock_voltage_dependency_table		vdd_dependency_on_sclk;
	struct vega10_odn_clock_voltage_dependency_table		vdd_dependency_on_mclk;
};

struct vega10_odn_fan_table {
	uint32_t	target_fan_speed;
	uint32_t	target_temperature;
	uint32_t	min_performance_clock;
	uint32_t	min_fan_limit;
};

struct vega10_hwmgr {
	struct vega10_dpm_table			dpm_table;
	struct vega10_dpm_table			golden_dpm_table;
	struct vega10_registry_data      registry_data;
	struct vega10_vbios_boot_state   vbios_boot_state;
	struct vega10_mclk_latency_table mclk_latency_table;

	struct vega10_leakage_voltage    vddc_leakage;

	uint32_t                           vddc_control;
	struct pp_atomfwctrl_voltage_table vddc_voltage_table;
	uint32_t                           mvdd_control;
	struct pp_atomfwctrl_voltage_table mvdd_voltage_table;
	uint32_t                           vddci_control;
	struct pp_atomfwctrl_voltage_table vddci_voltage_table;

	uint32_t                           active_auto_throttle_sources;
	uint32_t                           water_marks_bitmap;
	struct vega10_bacos                bacos;

	struct vega10_odn_dpm_table       odn_dpm_table;
	struct vega10_odn_fan_table       odn_fan_table;

	/* ---- General data ---- */
	uint8_t                           need_update_dpm_table;

	bool                           cac_enabled;
	bool                           battery_state;
	bool                           is_tlu_enabled;

	uint32_t                       low_sclk_interrupt_threshold;

	uint32_t                       total_active_cus;

	struct vega10_display_timing display_timing;

	/* ---- Vega10 Dyn Register Settings ---- */

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
	struct vega10_dpmlevel_enable_mask     dpm_level_enable_mask;

	/* ---- Power Gating States ---- */
	bool                           uvd_power_gated;
	bool                           vce_power_gated;
	bool                           samu_power_gated;
	bool                           need_long_memory_training;

	/* Internal settings to apply the application power optimization parameters */
	bool                           apply_optimized_settings;
	uint32_t                       disable_dpm_mask;

	/* ---- Overdrive next setting ---- */
	uint32_t                       apply_overdrive_next_settings_mask;

	/* ---- Workload Mask ---- */
	uint32_t                       workload_mask;

	/* ---- SMU9 ---- */
	struct smu_features            smu_features[GNLD_FEATURES_MAX];
	struct vega10_smc_state_table  smc_state_table;

	uint32_t                       config_telemetry;
};

#define VEGA10_DPM2_NEAR_TDP_DEC                      10
#define VEGA10_DPM2_ABOVE_SAFE_INC                    5
#define VEGA10_DPM2_BELOW_SAFE_INC                    20

#define VEGA10_DPM2_LTA_WINDOW_SIZE                   7

#define VEGA10_DPM2_LTS_TRUNCATE                      0

#define VEGA10_DPM2_TDP_SAFE_LIMIT_PERCENT            80

#define VEGA10_DPM2_MAXPS_PERCENT_M                   90
#define VEGA10_DPM2_MAXPS_PERCENT_H                   90

#define VEGA10_DPM2_PWREFFICIENCYRATIO_MARGIN         50

#define VEGA10_DPM2_SQ_RAMP_MAX_POWER                 0x3FFF
#define VEGA10_DPM2_SQ_RAMP_MIN_POWER                 0x12
#define VEGA10_DPM2_SQ_RAMP_MAX_POWER_DELTA           0x15
#define VEGA10_DPM2_SQ_RAMP_SHORT_TERM_INTERVAL_SIZE  0x1E
#define VEGA10_DPM2_SQ_RAMP_LONG_TERM_INTERVAL_RATIO  0xF

#define VEGA10_VOLTAGE_CONTROL_NONE                   0x0
#define VEGA10_VOLTAGE_CONTROL_BY_GPIO                0x1
#define VEGA10_VOLTAGE_CONTROL_BY_SVID2               0x2
#define VEGA10_VOLTAGE_CONTROL_MERGED                 0x3
/* To convert to Q8.8 format for firmware */
#define VEGA10_Q88_FORMAT_CONVERSION_UNIT             256

#define VEGA10_UNUSED_GPIO_PIN       0x7F

#define VEGA10_THERM_OUT_MODE_DISABLE       0x0
#define VEGA10_THERM_OUT_MODE_THERM_ONLY    0x1
#define VEGA10_THERM_OUT_MODE_THERM_VRHOT   0x2

#define PPVEGA10_VEGA10DISPLAYVOLTAGEMODE_DFLT   0xffffffff
#define PPREGKEY_VEGA10QUADRATICEQUATION_DFLT    0xffffffff

#define PPVEGA10_VEGA10GFXCLKAVERAGEALPHA_DFLT       25 /* 10% * 255 = 25 */
#define PPVEGA10_VEGA10SOCCLKAVERAGEALPHA_DFLT       25 /* 10% * 255 = 25 */
#define PPVEGA10_VEGA10UCLKCLKAVERAGEALPHA_DFLT      25 /* 10% * 255 = 25 */
#define PPVEGA10_VEGA10GFXACTIVITYAVERAGEALPHA_DFLT  25 /* 10% * 255 = 25 */

extern int tonga_initializa_dynamic_state_adjustment_rule_settings(struct pp_hwmgr *hwmgr);
extern int tonga_hwmgr_backend_fini(struct pp_hwmgr *hwmgr);
extern int tonga_get_mc_microcode_version (struct pp_hwmgr *hwmgr);
extern int tonga_notify_smc_display_config_after_ps_adjustment(struct pp_hwmgr *hwmgr);
extern int tonga_notify_smc_display_change(struct pp_hwmgr *hwmgr, bool has_display);
int vega10_update_vce_dpm(struct pp_hwmgr *hwmgr, const void *input);
int vega10_update_uvd_dpm(struct pp_hwmgr *hwmgr, bool bgate);
int vega10_update_samu_dpm(struct pp_hwmgr *hwmgr, bool bgate);
int vega10_update_acp_dpm(struct pp_hwmgr *hwmgr, bool bgate);
int vega10_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable);

#endif /* _VEGA10_HWMGR_H_ */
