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
#ifndef __TRINITY_DPM_H__
#define __TRINITY_DPM_H__

#include "sumo_dpm.h"

#define TRINITY_SIZEOF_DPM_STATE_TABLE (SMU_SCLK_DPM_STATE_1_CNTL_0 - SMU_SCLK_DPM_STATE_0_CNTL_0)

struct trinity_pl {
	u32 sclk;
	u8 vddc_index;
	u8 ds_divider_index;
	u8 ss_divider_index;
	u8 allow_gnb_slow;
	u8 force_nbp_state;
	u8 display_wm;
	u8 vce_wm;
};

#define TRINITY_POWERSTATE_FLAGS_NBPS_FORCEHIGH  (1 << 0)
#define TRINITY_POWERSTATE_FLAGS_NBPS_LOCKTOHIGH (1 << 1)
#define TRINITY_POWERSTATE_FLAGS_NBPS_LOCKTOLOW  (1 << 2)

#define TRINITY_POWERSTATE_FLAGS_BAPM_DISABLE    (1 << 0)

struct trinity_ps {
	u32 num_levels;
	struct trinity_pl levels[SUMO_MAX_HARDWARE_POWERLEVELS];

	u32 nbps_flags;
	u32 bapm_flags;

	u8 Dpm0PgNbPsLo;
	u8 Dpm0PgNbPsHi;
	u8 DpmXNbPsLo;
	u8 DpmXNbPsHi;

	u32 vclk_low_divider;
	u32 vclk_high_divider;
	u32 dclk_low_divider;
	u32 dclk_high_divider;
};

#define TRINITY_NUM_NBPSTATES   4

struct trinity_uvd_clock_table_entry
{
	u32 vclk;
	u32 dclk;
	u8 vclk_did;
	u8 dclk_did;
	u8 rsv[2];
};

struct trinity_sys_info {
	u32 bootup_uma_clk;
	u32 bootup_sclk;
	u32 min_sclk;
	u32 dentist_vco_freq;
	u32 nb_dpm_enable;
	u32 nbp_mclk[TRINITY_NUM_NBPSTATES];
	u32 nbp_nclk[TRINITY_NUM_NBPSTATES];
	u16 nbp_voltage_index[TRINITY_NUM_NBPSTATES];
	u16 bootup_nb_voltage_index;
	u8 htc_tmp_lmt;
	u8 htc_hyst_lmt;
	struct sumo_sclk_voltage_mapping_table sclk_voltage_mapping_table;
	struct sumo_vid_mapping_table vid_mapping_table;
	u32 uma_channel_number;
	struct trinity_uvd_clock_table_entry uvd_clock_table_entries[4];
};

struct trinity_power_info {
	u32 at[SUMO_MAX_HARDWARE_POWERLEVELS];
	u32 dpm_interval;
	u32 thermal_auto_throttling;
	struct trinity_sys_info sys_info;
	struct trinity_pl boot_pl;
	struct trinity_ps current_ps;
	u32 min_sclk_did;
	bool enable_nbps_policy;
	bool voltage_drop_in_dce;
	bool override_dynamic_mgpg;
	bool enable_gfx_clock_gating;
	bool enable_gfx_power_gating;
	bool enable_mg_clock_gating;
	bool enable_gfx_dynamic_mgpg;
	bool enable_auto_thermal_throttling;
	bool enable_dpm;
	bool enable_sclk_ds;
	bool uvd_dpm;
};

#define TRINITY_AT_DFLT            30

/* trinity_smc.c */
int trinity_dpm_config(struct radeon_device *rdev, bool enable);
int trinity_uvd_dpm_config(struct radeon_device *rdev);
int trinity_dpm_force_state(struct radeon_device *rdev, u32 n);
int trinity_dpm_no_forced_level(struct radeon_device *rdev);
int trinity_dce_enable_voltage_adjustment(struct radeon_device *rdev,
					  bool enable);
int trinity_gfx_dynamic_mgpg_config(struct radeon_device *rdev);
void trinity_acquire_mutex(struct radeon_device *rdev);
void trinity_release_mutex(struct radeon_device *rdev);

#endif
