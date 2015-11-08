/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef __CZ_DPM_H__
#define __CZ_DPM_H__

#include "smu8_fusion.h"

#define CZ_AT_DFLT					30
#define CZ_NUM_NBPSTATES				4
#define CZ_NUM_NBPMEMORY_CLOCK				2
#define CZ_MAX_HARDWARE_POWERLEVELS			8
#define CZ_MAX_DISPLAY_CLOCK_LEVEL			8
#define CZ_MAX_DISPLAYPHY_IDS				10

#define PPCZ_VOTINGRIGHTSCLIENTS_DFLT0			0x3FFFC102

#define SMC_RAM_END					0x40000

#define DPMFlags_SCLK_Enabled				0x00000001
#define DPMFlags_UVD_Enabled				0x00000002
#define DPMFlags_VCE_Enabled				0x00000004
#define DPMFlags_ACP_Enabled				0x00000008
#define DPMFlags_ForceHighestValid			0x40000000
#define DPMFlags_Debug					0x80000000

/* Do not change the following, it is also defined in SMU8.h */
#define SMU_EnabledFeatureScoreboard_AcpDpmOn		0x00000001
#define SMU_EnabledFeatureScoreboard_SclkDpmOn		0x00200000
#define SMU_EnabledFeatureScoreboard_UvdDpmOn		0x00800000
#define SMU_EnabledFeatureScoreboard_VceDpmOn		0x01000000

/* temporary solution to SetMinDeepSleepSclk
 * should indicate by display adaptor
 * 10k Hz unit*/
#define CZ_MIN_DEEP_SLEEP_SCLK				800

enum cz_pt_config_reg_type {
	CZ_CONFIGREG_MMR = 0,
	CZ_CONFIGREG_SMC_IND,
	CZ_CONFIGREG_DIDT_IND,
	CZ_CONFIGREG_CACHE,
	CZ_CONFIGREG_MAX
};

struct cz_pt_config_reg {
	uint32_t offset;
	uint32_t mask;
	uint32_t shift;
	uint32_t value;
	enum cz_pt_config_reg_type type;
};

struct cz_dpm_entry {
	uint32_t	soft_min_clk;
	uint32_t	hard_min_clk;
	uint32_t	soft_max_clk;
	uint32_t	hard_max_clk;
};

struct cz_pl {
	uint32_t sclk;
	uint8_t vddc_index;
	uint8_t ds_divider_index;
	uint8_t ss_divider_index;
	uint8_t allow_gnb_slow;
	uint8_t force_nbp_state;
	uint8_t display_wm;
	uint8_t vce_wm;
};

struct cz_ps {
	struct cz_pl levels[CZ_MAX_HARDWARE_POWERLEVELS];
	uint32_t num_levels;
	bool need_dfs_bypass;
	uint8_t dpm0_pg_nb_ps_lo;
	uint8_t dpm0_pg_nb_ps_hi;
	uint8_t dpmx_nb_ps_lo;
	uint8_t dpmx_nb_ps_hi;
	bool force_high;
};

struct cz_displayphy_entry {
	uint8_t phy_present;
	uint8_t active_lane_mapping;
	uint8_t display_conf_type;
	uint8_t num_active_lanes;
};

struct cz_displayphy_info {
	bool phy_access_initialized;
	struct cz_displayphy_entry entries[CZ_MAX_DISPLAYPHY_IDS];
};

struct cz_sys_info {
	uint32_t bootup_uma_clk;
	uint32_t bootup_sclk;
	uint32_t dentist_vco_freq;
	uint32_t nb_dpm_enable;
	uint32_t nbp_memory_clock[CZ_NUM_NBPMEMORY_CLOCK];
	uint32_t nbp_n_clock[CZ_NUM_NBPSTATES];
	uint8_t nbp_voltage_index[CZ_NUM_NBPSTATES];
	uint32_t display_clock[CZ_MAX_DISPLAY_CLOCK_LEVEL];
	uint16_t bootup_nb_voltage_index;
	uint8_t htc_tmp_lmt;
	uint8_t htc_hyst_lmt;
	uint32_t uma_channel_number;
};

struct cz_power_info {
	uint32_t active_target[CZ_MAX_HARDWARE_POWERLEVELS];
	struct cz_sys_info sys_info;
	struct cz_pl boot_pl;
	bool disable_nb_ps3_in_battery;
	bool battery_state;
	uint32_t lowest_valid;
	uint32_t highest_valid;
	uint16_t high_voltage_threshold;
	/* smc offsets */
	uint32_t sram_end;
	uint32_t dpm_table_start;
	uint32_t soft_regs_start;
	/* dpm SMU tables */
	uint8_t uvd_level_count;
	uint8_t vce_level_count;
	uint8_t acp_level_count;
	uint32_t fps_high_threshold;
	uint32_t fps_low_threshold;
	/* dpm table */
	uint32_t dpm_flags;
	struct cz_dpm_entry sclk_dpm;
	struct cz_dpm_entry uvd_dpm;
	struct cz_dpm_entry vce_dpm;
	struct cz_dpm_entry acp_dpm;

	uint8_t uvd_boot_level;
	uint8_t uvd_interval;
	uint8_t vce_boot_level;
	uint8_t vce_interval;
	uint8_t acp_boot_level;
	uint8_t acp_interval;

	uint8_t graphics_boot_level;
	uint8_t graphics_interval;
	uint8_t graphics_therm_throttle_enable;
	uint8_t graphics_voltage_change_enable;
	uint8_t graphics_clk_slow_enable;
	uint8_t graphics_clk_slow_divider;

	uint32_t low_sclk_interrupt_threshold;
	bool uvd_power_gated;
	bool vce_power_gated;
	bool acp_power_gated;

	uint32_t active_process_mask;

	uint32_t mgcg_cgtt_local0;
	uint32_t mgcg_cgtt_local1;
	uint32_t clock_slow_down_step;
	uint32_t skip_clock_slow_down;
	bool enable_nb_ps_policy;
	uint32_t voting_clients;
	uint32_t voltage_drop_threshold;
	uint32_t gfx_pg_threshold;
	uint32_t max_sclk_level;
	/* flags */
	bool didt_enabled;
	bool video_start;
	bool cac_enabled;
	bool bapm_enabled;
	bool nb_dpm_enabled_by_driver;
	bool nb_dpm_enabled;
	bool auto_thermal_throttling_enabled;
	bool dpm_enabled;
	bool need_pptable_upload;
	/* caps */
	bool caps_cac;
	bool caps_power_containment;
	bool caps_sq_ramping;
	bool caps_db_ramping;
	bool caps_td_ramping;
	bool caps_tcp_ramping;
	bool caps_sclk_throttle_low_notification;
	bool caps_fps;
	bool caps_uvd_dpm;
	bool caps_uvd_pg;
	bool caps_vce_dpm;
	bool caps_vce_pg;
	bool caps_acp_dpm;
	bool caps_acp_pg;
	bool caps_stable_power_state;
	bool caps_enable_dfs_bypass;
	bool caps_sclk_ds;
	bool caps_voltage_island;
	/* power state */
	struct amdgpu_ps current_rps;
	struct cz_ps current_ps;
	struct amdgpu_ps requested_rps;
	struct cz_ps requested_ps;

	bool uvd_power_down;
	bool vce_power_down;
	bool acp_power_down;

	bool uvd_dynamic_pg;
};

/* cz_smc.c */
uint32_t cz_get_argument(struct amdgpu_device *adev);
int cz_send_msg_to_smc(struct amdgpu_device *adev, uint16_t msg);
int cz_send_msg_to_smc_with_parameter(struct amdgpu_device *adev,
			uint16_t msg, uint32_t parameter);
int cz_read_smc_sram_dword(struct amdgpu_device *adev,
			uint32_t smc_address, uint32_t *value, uint32_t limit);
int cz_smu_upload_pptable(struct amdgpu_device *adev);
int cz_smu_download_pptable(struct amdgpu_device *adev, void **table);
#endif
