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
#ifndef __KV_DPM_H__
#define __KV_DPM_H__

#define SMU__NUM_SCLK_DPM_STATE  8
#define SMU__NUM_MCLK_DPM_LEVELS 4
#define SMU__NUM_LCLK_DPM_LEVELS 8
#define SMU__NUM_PCIE_DPM_LEVELS 0 /* ??? */
#include "smu7_fusion.h"
#include "trinity_dpm.h"
#include "ppsmc.h"

#define KV_NUM_NBPSTATES   4

enum kv_pt_config_reg_type {
	KV_CONFIGREG_MMR = 0,
	KV_CONFIGREG_SMC_IND,
	KV_CONFIGREG_DIDT_IND,
	KV_CONFIGREG_CACHE,
	KV_CONFIGREG_MAX
};

struct kv_pt_config_reg {
	u32 offset;
	u32 mask;
	u32 shift;
	u32 value;
	enum kv_pt_config_reg_type type;
};

struct kv_lcac_config_values {
	u32 block_id;
	u32 signal_id;
	u32 t;
};

struct kv_lcac_config_reg {
	u32 cntl;
	u32 block_mask;
	u32 block_shift;
	u32 signal_mask;
	u32 signal_shift;
	u32 t_mask;
	u32 t_shift;
	u32 enable_mask;
	u32 enable_shift;
};

struct kv_pl {
	u32 sclk;
	u8 vddc_index;
	u8 ds_divider_index;
	u8 ss_divider_index;
	u8 allow_gnb_slow;
	u8 force_nbp_state;
	u8 display_wm;
	u8 vce_wm;
};

struct kv_ps {
	struct kv_pl levels[SUMO_MAX_HARDWARE_POWERLEVELS];
	u32 num_levels;
	bool need_dfs_bypass;
	u8 dpm0_pg_nb_ps_lo;
	u8 dpm0_pg_nb_ps_hi;
	u8 dpmx_nb_ps_lo;
	u8 dpmx_nb_ps_hi;
};

struct kv_sys_info {
	u32 bootup_uma_clk;
	u32 bootup_sclk;
	u32 dentist_vco_freq;
	u32 nb_dpm_enable;
	u32 nbp_memory_clock[KV_NUM_NBPSTATES];
	u32 nbp_n_clock[KV_NUM_NBPSTATES];
	u16 bootup_nb_voltage_index;
	u8 htc_tmp_lmt;
	u8 htc_hyst_lmt;
	struct sumo_sclk_voltage_mapping_table sclk_voltage_mapping_table;
	struct sumo_vid_mapping_table vid_mapping_table;
	u32 uma_channel_number;
};

struct kv_power_info {
	u32 at[SUMO_MAX_HARDWARE_POWERLEVELS];
	u32 voltage_drop_t;
	struct kv_sys_info sys_info;
	struct kv_pl boot_pl;
	bool enable_nb_ps_policy;
	bool disable_nb_ps3_in_battery;
	bool video_start;
	bool battery_state;
	u32 lowest_valid;
	u32 highest_valid;
	u16 high_voltage_t;
	bool cac_enabled;
	bool bapm_enable;
	/* smc offsets */
	u32 sram_end;
	u32 dpm_table_start;
	u32 soft_regs_start;
	/* dpm SMU tables */
	u8 graphics_dpm_level_count;
	u8 uvd_level_count;
	u8 vce_level_count;
	u8 acp_level_count;
	u8 samu_level_count;
	u16 fps_high_t;
	SMU7_Fusion_GraphicsLevel graphics_level[SMU__NUM_SCLK_DPM_STATE];
	SMU7_Fusion_ACPILevel acpi_level;
	SMU7_Fusion_UvdLevel uvd_level[SMU7_MAX_LEVELS_UVD];
	SMU7_Fusion_ExtClkLevel vce_level[SMU7_MAX_LEVELS_VCE];
	SMU7_Fusion_ExtClkLevel acp_level[SMU7_MAX_LEVELS_ACP];
	SMU7_Fusion_ExtClkLevel samu_level[SMU7_MAX_LEVELS_SAMU];
	u8 uvd_boot_level;
	u8 vce_boot_level;
	u8 acp_boot_level;
	u8 samu_boot_level;
	u8 uvd_interval;
	u8 vce_interval;
	u8 acp_interval;
	u8 samu_interval;
	u8 graphics_boot_level;
	u8 graphics_interval;
	u8 graphics_therm_throttle_enable;
	u8 graphics_voltage_change_enable;
	u8 graphics_clk_slow_enable;
	u8 graphics_clk_slow_divider;
	u8 fps_low_t;
	u32 low_sclk_interrupt_t;
	bool uvd_power_gated;
	bool vce_power_gated;
	bool acp_power_gated;
	bool samu_power_gated;
	bool nb_dpm_enabled;
	/* flags */
	bool enable_didt;
	bool enable_dpm;
	bool enable_auto_thermal_throttling;
	bool enable_nb_dpm;
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
	bool caps_vce_pg;
	bool caps_samu_pg;
	bool caps_acp_pg;
	bool caps_stable_p_state;
	bool caps_enable_dfs_bypass;
	bool caps_sclk_ds;
	struct radeon_ps current_rps;
	struct kv_ps current_ps;
	struct radeon_ps requested_rps;
	struct kv_ps requested_ps;
};


/* kv_smc.c */
int kv_notify_message_to_smu(struct radeon_device *rdev, u32 id);
int kv_dpm_get_enable_mask(struct radeon_device *rdev, u32 *enable_mask);
int kv_send_msg_to_smc_with_parameter(struct radeon_device *rdev,
				      PPSMC_Msg msg, u32 parameter);
int kv_read_smc_sram_dword(struct radeon_device *rdev, u32 smc_address,
			   u32 *value, u32 limit);
int kv_smc_dpm_enable(struct radeon_device *rdev, bool enable);
int kv_copy_bytes_to_smc(struct radeon_device *rdev,
			 u32 smc_start_address,
			 const u8 *src, u32 byte_count, u32 limit);

#endif
