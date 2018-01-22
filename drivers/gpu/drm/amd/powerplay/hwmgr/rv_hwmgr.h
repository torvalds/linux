/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#ifndef RAVEN_HWMGR_H
#define RAVEN_HWMGR_H

#include "hwmgr.h"
#include "rv_inc.h"
#include "smu10_driver_if.h"
#include "rv_ppsmc.h"


#define RAVEN_MAX_HARDWARE_POWERLEVELS               8
#define PHMRAVEN_DYNCLK_NUMBER_OF_TREND_COEFFICIENTS   15

#define DPMFlags_SCLK_Enabled                     0x00000001
#define DPMFlags_UVD_Enabled                      0x00000002
#define DPMFlags_VCE_Enabled                      0x00000004
#define DPMFlags_ACP_Enabled                      0x00000008
#define DPMFlags_ForceHighestValid                0x40000000

/* Do not change the following, it is also defined in SMU8.h */
#define SMU_EnabledFeatureScoreboard_AcpDpmOn     0x00000001
#define SMU_EnabledFeatureScoreboard_SclkDpmOn    0x00200000
#define SMU_EnabledFeatureScoreboard_UvdDpmOn     0x01000000
#define SMU_EnabledFeatureScoreboard_VceDpmOn     0x02000000

#define SMU_PHYID_SHIFT      8

#define RAVEN_PCIE_POWERGATING_TARGET_GFX            0
#define RAVEN_PCIE_POWERGATING_TARGET_DDI            1
#define RAVEN_PCIE_POWERGATING_TARGET_PLLCASCADE     2
#define RAVEN_PCIE_POWERGATING_TARGET_PHY            3

enum VQ_TYPE {
	CLOCK_TYPE_DCLK = 0L,
	CLOCK_TYPE_ECLK,
	CLOCK_TYPE_SCLK,
	CLOCK_TYPE_CCLK,
	VQ_GFX_CU
};

#define SUSTAINABLE_SCLK_MASK  0x00ffffff
#define SUSTAINABLE_SCLK_SHIFT 0
#define SUSTAINABLE_CU_MASK    0xff000000
#define SUSTAINABLE_CU_SHIFT   24

struct rv_dpm_entry {
	uint32_t soft_min_clk;
	uint32_t hard_min_clk;
	uint32_t soft_max_clk;
	uint32_t hard_max_clk;
};

struct rv_power_level {
	uint32_t engine_clock;
	uint8_t vddc_index;
	uint8_t ds_divider_index;
	uint8_t ss_divider_index;
	uint8_t allow_gnb_slow;
	uint8_t force_nbp_state;
	uint8_t display_wm;
	uint8_t vce_wm;
	uint8_t num_simd_to_powerdown;
	uint8_t hysteresis_up;
	uint8_t rsv[3];
};

/*used for the nbpsFlags field in rv_power state*/
#define RAVEN_POWERSTATE_FLAGS_NBPS_FORCEHIGH (1<<0)
#define RAVEN_POWERSTATE_FLAGS_NBPS_LOCKTOHIGH (1<<1)
#define RAVEN_POWERSTATE_FLAGS_NBPS_LOCKTOLOW (1<<2)

#define RAVEN_POWERSTATE_FLAGS_BAPM_DISABLE    (1<<0)

struct rv_uvd_clocks {
	uint32_t vclk;
	uint32_t dclk;
	uint32_t vclk_low_divider;
	uint32_t vclk_high_divider;
	uint32_t dclk_low_divider;
	uint32_t dclk_high_divider;
};

struct pp_disable_nbpslo_flags {
	union {
		struct {
			uint32_t entry : 1;
			uint32_t display : 1;
			uint32_t driver: 1;
			uint32_t vce : 1;
			uint32_t uvd : 1;
			uint32_t acp : 1;
			uint32_t reserved: 26;
		} bits;
		uint32_t u32All;
	};
};


enum rv_pstate_previous_action {
	DO_NOTHING = 1,
	FORCE_HIGH,
	CANCEL_FORCE_HIGH
};

struct rv_power_state {
	unsigned int magic;
	uint32_t level;
	struct rv_uvd_clocks uvd_clocks;
	uint32_t evclk;
	uint32_t ecclk;
	uint32_t samclk;
	uint32_t acpclk;
	bool need_dfs_bypass;

	uint32_t nbps_flags;
	uint32_t bapm_flags;
	uint8_t dpm0_pg_nbps_low;
	uint8_t dpm0_pg_nbps_high;
	uint8_t dpm_x_nbps_low;
	uint8_t dpm_x_nbps_high;

	enum rv_pstate_previous_action action;

	struct rv_power_level levels[RAVEN_MAX_HARDWARE_POWERLEVELS];
	struct pp_disable_nbpslo_flags nbpslo_flags;
};

#define RAVEN_NUM_NBPSTATES        4
#define RAVEN_NUM_NBPMEMORYCLOCK   2


struct rv_display_phy_info_entry {
	uint8_t                   phy_present;
	uint8_t                   active_lane_mapping;
	uint8_t                   display_config_type;
	uint8_t                   active_num_of_lanes;
};

#define RAVEN_MAX_DISPLAYPHY_IDS       10

struct rv_display_phy_info {
	bool                         display_phy_access_initialized;
	struct rv_display_phy_info_entry  entries[RAVEN_MAX_DISPLAYPHY_IDS];
};

#define MAX_DISPLAY_CLOCK_LEVEL 8

struct rv_system_info{
	uint8_t                      htc_tmp_lmt;
	uint8_t                      htc_hyst_lmt;
};

#define MAX_REGULAR_DPM_NUMBER 8

struct rv_mclk_latency_entries {
	uint32_t  frequency;
	uint32_t  latency;
};

struct rv_mclk_latency_table {
	uint32_t  count;
	struct rv_mclk_latency_entries  entries[MAX_REGULAR_DPM_NUMBER];
};

struct rv_clock_voltage_dependency_record {
	uint32_t clk;
	uint32_t vol;
};


struct rv_voltage_dependency_table {
	uint32_t count;
	struct rv_clock_voltage_dependency_record entries[1];
};

struct rv_clock_voltage_information {
	struct rv_voltage_dependency_table    *vdd_dep_on_dcefclk;
	struct rv_voltage_dependency_table    *vdd_dep_on_socclk;
	struct rv_voltage_dependency_table    *vdd_dep_on_fclk;
	struct rv_voltage_dependency_table    *vdd_dep_on_mclk;
	struct rv_voltage_dependency_table    *vdd_dep_on_dispclk;
	struct rv_voltage_dependency_table    *vdd_dep_on_dppclk;
	struct rv_voltage_dependency_table    *vdd_dep_on_phyclk;
};

struct rv_hwmgr {
	uint32_t disable_driver_thermal_policy;
	uint32_t thermal_auto_throttling_treshold;
	struct rv_system_info sys_info;
	struct rv_mclk_latency_table mclk_latency_table;

	uint32_t ddi_power_gating_disabled;

	struct rv_display_phy_info_entry            display_phy_info;
	uint32_t dce_slow_sclk_threshold;

	bool disp_clk_bypass;
	bool disp_clk_bypass_pending;
	uint32_t bapm_enabled;

	bool video_start;
	bool battery_state;

	uint32_t is_nb_dpm_enabled;
	uint32_t is_voltage_island_enabled;
	uint32_t disable_smu_acp_s3_handshake;
	uint32_t disable_notify_smu_vpu_recovery;
	bool                           in_vpu_recovery;
	bool pg_acp_init;
	uint8_t disp_config;

	/* PowerTune */
	uint32_t power_containment_features;
	bool cac_enabled;
	bool disable_uvd_power_tune_feature;
	bool enable_bapm_feature;
	bool enable_tdc_limit_feature;


	/* SMC SRAM Address of firmware header tables */
	uint32_t sram_end;
	uint32_t dpm_table_start;
	uint32_t soft_regs_start;

	/* start of SMU7_Fusion_DpmTable */

	uint8_t uvd_level_count;
	uint8_t vce_level_count;
	uint8_t acp_level_count;
	uint8_t samu_level_count;

	uint32_t fps_high_threshold;
	uint32_t fps_low_threshold;

	uint32_t dpm_flags;
	struct rv_dpm_entry sclk_dpm;
	struct rv_dpm_entry uvd_dpm;
	struct rv_dpm_entry vce_dpm;
	struct rv_dpm_entry acp_dpm;
	bool acp_power_up_no_dsp;

	uint32_t max_sclk_level;
	uint32_t num_of_clk_entries;

	/* CPU Power State */
	uint32_t                          separation_time;
	bool                              cc6_disable;
	bool                              pstate_disable;
	bool                              cc6_setting_changed;

	uint32_t                             ulTotalActiveCUs;

	bool                           isp_tileA_power_gated;
	bool                           isp_tileB_power_gated;
	uint32_t                       isp_actual_hard_min_freq;
	uint32_t                       soc_actual_hard_min_freq;
	uint32_t                       dcf_actual_hard_min_freq;

	uint32_t                        f_actual_hard_min_freq;
	uint32_t                        fabric_actual_soft_min_freq;
	uint32_t                        vclk_soft_min;
	uint32_t                        dclk_soft_min;
	uint32_t                        gfx_actual_soft_min_freq;
	uint32_t                        gfx_min_freq_limit;
	uint32_t                        gfx_max_freq_limit;

	bool                           vcn_power_gated;
	bool                           vcn_dpg_mode;

	bool                           gfx_off_controled_by_driver;
	Watermarks_t                      water_marks_table;
	struct rv_clock_voltage_information   clock_vol_info;
	DpmClocks_t                       clock_table;

	uint32_t active_process_mask;
	bool need_min_deep_sleep_dcefclk;
	uint32_t                             deep_sleep_dcefclk;
	uint32_t                             num_active_display;
};

struct pp_hwmgr;

int rv_init_function_pointers(struct pp_hwmgr *hwmgr);

#endif
