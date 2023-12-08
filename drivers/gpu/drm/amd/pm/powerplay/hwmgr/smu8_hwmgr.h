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

#ifndef _SMU8_HWMGR_H_
#define _SMU8_HWMGR_H_

#include "cgs_common.h"
#include "ppatomctrl.h"

#define SMU8_NUM_NBPSTATES               4
#define SMU8_NUM_NBPMEMORYCLOCK          2
#define MAX_DISPLAY_CLOCK_LEVEL        8
#define SMU8_MAX_HARDWARE_POWERLEVELS    8
#define SMU8_VOTINGRIGHTSCLIENTS_DFLT0   0x3FFFC102
#define SMU8_MIN_DEEP_SLEEP_SCLK         800

/* Carrizo device IDs */
#define DEVICE_ID_CZ_9870             0x9870
#define DEVICE_ID_CZ_9874             0x9874
#define DEVICE_ID_CZ_9875             0x9875
#define DEVICE_ID_CZ_9876             0x9876
#define DEVICE_ID_CZ_9877             0x9877

struct smu8_dpm_entry {
	uint32_t soft_min_clk;
	uint32_t hard_min_clk;
	uint32_t soft_max_clk;
	uint32_t hard_max_clk;
};

struct smu8_sys_info {
	uint32_t bootup_uma_clock;
	uint32_t bootup_engine_clock;
	uint32_t dentist_vco_freq;
	uint32_t nb_dpm_enable;
	uint32_t nbp_memory_clock[SMU8_NUM_NBPMEMORYCLOCK];
	uint32_t nbp_n_clock[SMU8_NUM_NBPSTATES];
	uint16_t nbp_voltage_index[SMU8_NUM_NBPSTATES];
	uint32_t display_clock[MAX_DISPLAY_CLOCK_LEVEL];
	uint16_t bootup_nb_voltage_index;
	uint8_t htc_tmp_lmt;
	uint8_t htc_hyst_lmt;
	uint32_t system_config;
	uint32_t uma_channel_number;
};

#define MAX_DISPLAYPHY_IDS			0x8
#define DISPLAYPHY_LANEMASK			0xF
#define UNKNOWN_TRANSMITTER_PHY_ID		(-1)

#define DISPLAYPHY_PHYID_SHIFT			24
#define DISPLAYPHY_LANESELECT_SHIFT		16

#define DISPLAYPHY_RX_SELECT			0x1
#define DISPLAYPHY_TX_SELECT			0x2
#define DISPLAYPHY_CORE_SELECT			0x4

#define DDI_POWERGATING_ARG(phyID, lanemask, rx, tx, core) \
		(((uint32_t)(phyID))<<DISPLAYPHY_PHYID_SHIFT | \
		((uint32_t)(lanemask))<<DISPLAYPHY_LANESELECT_SHIFT | \
		((rx) ? DISPLAYPHY_RX_SELECT : 0) | \
		((tx) ? DISPLAYPHY_TX_SELECT : 0) | \
		((core) ? DISPLAYPHY_CORE_SELECT : 0))

struct smu8_display_phy_info_entry {
	uint8_t phy_present;
	uint8_t active_lane_mapping;
	uint8_t display_config_type;
	uint8_t active_number_of_lanes;
};

#define SMU8_MAX_DISPLAYPHY_IDS			10

struct smu8_display_phy_info {
	bool display_phy_access_initialized;
	struct smu8_display_phy_info_entry entries[SMU8_MAX_DISPLAYPHY_IDS];
};

struct smu8_power_level {
	uint32_t engineClock;
	uint8_t vddcIndex;
	uint8_t dsDividerIndex;
	uint8_t ssDividerIndex;
	uint8_t allowGnbSlow;
	uint8_t forceNBPstate;
	uint8_t display_wm;
	uint8_t vce_wm;
	uint8_t numSIMDToPowerDown;
	uint8_t hysteresis_up;
	uint8_t rsv[3];
};

struct smu8_uvd_clocks {
	uint32_t vclk;
	uint32_t dclk;
	uint32_t vclk_low_divider;
	uint32_t vclk_high_divider;
	uint32_t dclk_low_divider;
	uint32_t dclk_high_divider;
};

enum smu8_pstate_previous_action {
	DO_NOTHING = 1,
	FORCE_HIGH,
	CANCEL_FORCE_HIGH
};

struct pp_disable_nb_ps_flags {
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

struct smu8_power_state {
	unsigned int magic;
	uint32_t level;
	struct smu8_uvd_clocks uvd_clocks;
	uint32_t evclk;
	uint32_t ecclk;
	uint32_t samclk;
	uint32_t acpclk;
	bool need_dfs_bypass;
	uint32_t nbps_flags;
	uint32_t bapm_flags;
	uint8_t dpm_0_pg_nb_ps_low;
	uint8_t dpm_0_pg_nb_ps_high;
	uint8_t dpm_x_nb_ps_low;
	uint8_t dpm_x_nb_ps_high;
	enum smu8_pstate_previous_action action;
	struct smu8_power_level levels[SMU8_MAX_HARDWARE_POWERLEVELS];
	struct pp_disable_nb_ps_flags disable_nb_ps_flag;
};

#define DPMFlags_SCLK_Enabled			0x00000001
#define DPMFlags_UVD_Enabled			0x00000002
#define DPMFlags_VCE_Enabled			0x00000004
#define DPMFlags_ACP_Enabled			0x00000008
#define DPMFlags_ForceHighestValid		0x40000000
#define DPMFlags_Debug				0x80000000

#define SMU_EnabledFeatureScoreboard_AcpDpmOn   0x00000001 /* bit 0 */
#define SMU_EnabledFeatureScoreboard_UvdDpmOn   0x00800000 /* bit 23 */
#define SMU_EnabledFeatureScoreboard_VceDpmOn   0x01000000 /* bit 24 */

struct cc6_settings {
	bool cc6_setting_changed;
	bool nb_pstate_switch_disable;/* controls NB PState switch */
	bool cpu_cc6_disable; /* controls CPU CState switch ( on or off) */
	bool cpu_pstate_disable;
	uint32_t cpu_pstate_separation_time;
};

struct smu8_hwmgr {
	uint32_t dpm_interval;

	uint32_t voltage_drop_threshold;

	uint32_t voting_rights_clients;

	uint32_t disable_driver_thermal_policy;

	uint32_t static_screen_threshold;

	uint32_t gfx_power_gating_threshold;

	uint32_t activity_hysteresis;
	uint32_t bootup_sclk_divider;
	uint32_t gfx_ramp_step;
	uint32_t gfx_ramp_delay; /* in micro-seconds */

	uint32_t thermal_auto_throttling_treshold;

	struct smu8_sys_info sys_info;

	struct smu8_power_level boot_power_level;
	struct smu8_power_state *smu8_current_ps;
	struct smu8_power_state *smu8_requested_ps;

	uint32_t mgcg_cgtt_local0;
	uint32_t mgcg_cgtt_local1;

	uint32_t tdr_clock; /* in 10khz unit */

	uint32_t ddi_power_gating_disabled;
	uint32_t disable_gfx_power_gating_in_uvd;
	uint32_t disable_nb_ps3_in_battery;

	uint32_t lock_nb_ps_in_uvd_play_back;

	struct smu8_display_phy_info display_phy_info;
	uint32_t vce_slow_sclk_threshold; /* default 200mhz */
	uint32_t dce_slow_sclk_threshold; /* default 300mhz */
	uint32_t min_sclk_did;  /* minimum sclk divider */

	bool disp_clk_bypass;
	bool disp_clk_bypass_pending;
	uint32_t bapm_enabled;
	uint32_t clock_slow_down_freq;
	uint32_t skip_clock_slow_down;
	uint32_t enable_nb_ps_policy;
	uint32_t voltage_drop_in_dce_power_gating;
	uint32_t uvd_dpm_interval;
	uint32_t override_dynamic_mgpg;
	uint32_t lclk_deep_enabled;

	uint32_t uvd_performance;

	bool video_start;
	bool battery_state;
	uint32_t lowest_valid;
	uint32_t highest_valid;
	uint32_t high_voltage_threshold;
	uint32_t is_nb_dpm_enabled;
	struct cc6_settings cc6_settings;
	uint32_t is_voltage_island_enabled;

	bool pgacpinit;

	uint8_t disp_config;

	/* PowerTune */
	uint32_t power_containment_features;
	bool cac_enabled;
	bool disable_uvd_power_tune_feature;
	bool enable_ba_pm_feature;
	bool enable_tdc_limit_feature;

	uint32_t sram_end;
	uint32_t dpm_table_start;
	uint32_t soft_regs_start;

	uint8_t uvd_level_count;
	uint8_t vce_level_count;

	uint8_t acp_level_count;
	uint8_t samu_level_count;
	uint32_t fps_high_threshold;
	uint32_t fps_low_threshold;

	uint32_t dpm_flags;
	struct smu8_dpm_entry sclk_dpm;
	struct smu8_dpm_entry uvd_dpm;
	struct smu8_dpm_entry vce_dpm;
	struct smu8_dpm_entry acp_dpm;

	uint8_t uvd_boot_level;
	uint8_t vce_boot_level;
	uint8_t acp_boot_level;
	uint8_t samu_boot_level;
	uint8_t uvd_interval;
	uint8_t vce_interval;
	uint8_t acp_interval;
	uint8_t samu_interval;

	uint8_t graphics_interval;
	uint8_t graphics_therm_throttle_enable;
	uint8_t graphics_voltage_change_enable;

	uint8_t graphics_clk_slow_enable;
	uint8_t graphics_clk_slow_divider;

	uint32_t display_cac;
	uint32_t low_sclk_interrupt_threshold;

	uint32_t dram_log_addr_h;
	uint32_t dram_log_addr_l;
	uint32_t dram_log_phy_addr_h;
	uint32_t dram_log_phy_addr_l;
	uint32_t dram_log_buff_size;

	bool uvd_power_gated;
	bool vce_power_gated;
	bool samu_power_gated;
	bool acp_power_gated;
	bool acp_power_up_no_dsp;
	uint32_t active_process_mask;

	uint32_t max_sclk_level;
	uint32_t num_of_clk_entries;
};

#endif /* _SMU8_HWMGR_H_ */
