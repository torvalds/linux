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
#ifndef __SUMO_DPM_H__
#define __SUMO_DPM_H__

#include "atom.h"

#define SUMO_MAX_HARDWARE_POWERLEVELS 5
#define SUMO_PM_NUMBER_OF_TC 15

struct sumo_pl {
	u32 sclk;
	u32 vddc_index;
	u32 ds_divider_index;
	u32 ss_divider_index;
	u32 allow_gnb_slow;
	u32 sclk_dpm_tdp_limit;
};

/* used for the flags field */
#define SUMO_POWERSTATE_FLAGS_FORCE_NBPS1_STATE (1 << 0)
#define SUMO_POWERSTATE_FLAGS_BOOST_STATE       (1 << 1)

struct sumo_ps {
	struct sumo_pl levels[SUMO_MAX_HARDWARE_POWERLEVELS];
	u32 num_levels;
	/* flags */
	u32 flags;
};

#define NUMBER_OF_M3ARB_PARAM_SETS 10
#define SUMO_MAX_NUMBER_VOLTAGES    4

struct sumo_disp_clock_voltage_mapping_table {
	u32 num_max_voltage_levels;
	u32 display_clock_frequency[SUMO_MAX_NUMBER_VOLTAGES];
};

struct sumo_vid_mapping_entry {
	u16 vid_2bit;
	u16 vid_7bit;
};

struct sumo_vid_mapping_table {
	u32 num_entries;
	struct sumo_vid_mapping_entry entries[SUMO_MAX_NUMBER_VOLTAGES];
};

struct sumo_sclk_voltage_mapping_entry {
	u32 sclk_frequency;
	u16 vid_2bit;
	u16 rsv;
};

struct sumo_sclk_voltage_mapping_table {
	u32 num_max_dpm_entries;
	struct sumo_sclk_voltage_mapping_entry entries[SUMO_MAX_HARDWARE_POWERLEVELS];
};

struct sumo_sys_info {
	u32 bootup_sclk;
	u32 min_sclk;
	u32 bootup_uma_clk;
	u16 bootup_nb_voltage_index;
	u8 htc_tmp_lmt;
	u8 htc_hyst_lmt;
	struct sumo_sclk_voltage_mapping_table sclk_voltage_mapping_table;
	struct sumo_disp_clock_voltage_mapping_table disp_clk_voltage_mapping_table;
	struct sumo_vid_mapping_table vid_mapping_table;
	u32 csr_m3_arb_cntl_default[NUMBER_OF_M3ARB_PARAM_SETS];
	u32 csr_m3_arb_cntl_uvd[NUMBER_OF_M3ARB_PARAM_SETS];
	u32 csr_m3_arb_cntl_fs3d[NUMBER_OF_M3ARB_PARAM_SETS];
	u32 sclk_dpm_boost_margin;
	u32 sclk_dpm_throttle_margin;
	u32 sclk_dpm_tdp_limit_pg;
	u32 gnb_tdp_limit;
	u32 sclk_dpm_tdp_limit_boost;
	u32 boost_sclk;
	u32 boost_vid_2bit;
	bool enable_boost;
};

struct sumo_power_info {
	u32 asi;
	u32 pasi;
	u32 bsp;
	u32 bsu;
	u32 pbsp;
	u32 pbsu;
	u32 dsp;
	u32 psp;
	u32 thermal_auto_throttling;
	u32 uvd_m3_arbiter;
	u32 fw_version;
	struct sumo_sys_info sys_info;
	struct sumo_pl acpi_pl;
	struct sumo_pl boot_pl;
	struct sumo_pl boost_pl;
	struct sumo_ps current_ps;
	bool disable_gfx_power_gating_in_uvd;
	bool driver_nbps_policy_disable;
	bool enable_alt_vddnb;
	bool enable_dynamic_m3_arbiter;
	bool enable_gfx_clock_gating;
	bool enable_gfx_power_gating;
	bool enable_mg_clock_gating;
	bool enable_sclk_ds;
	bool enable_auto_thermal_throttling;
	bool enable_dynamic_patch_ps;
	bool enable_dpm;
	bool enable_boost;
};

#define SUMO_UTC_DFLT_00                     0x48
#define SUMO_UTC_DFLT_01                     0x44
#define SUMO_UTC_DFLT_02                     0x44
#define SUMO_UTC_DFLT_03                     0x44
#define SUMO_UTC_DFLT_04                     0x44
#define SUMO_UTC_DFLT_05                     0x44
#define SUMO_UTC_DFLT_06                     0x44
#define SUMO_UTC_DFLT_07                     0x44
#define SUMO_UTC_DFLT_08                     0x44
#define SUMO_UTC_DFLT_09                     0x44
#define SUMO_UTC_DFLT_10                     0x44
#define SUMO_UTC_DFLT_11                     0x44
#define SUMO_UTC_DFLT_12                     0x44
#define SUMO_UTC_DFLT_13                     0x44
#define SUMO_UTC_DFLT_14                     0x44

#define SUMO_DTC_DFLT_00                     0x48
#define SUMO_DTC_DFLT_01                     0x44
#define SUMO_DTC_DFLT_02                     0x44
#define SUMO_DTC_DFLT_03                     0x44
#define SUMO_DTC_DFLT_04                     0x44
#define SUMO_DTC_DFLT_05                     0x44
#define SUMO_DTC_DFLT_06                     0x44
#define SUMO_DTC_DFLT_07                     0x44
#define SUMO_DTC_DFLT_08                     0x44
#define SUMO_DTC_DFLT_09                     0x44
#define SUMO_DTC_DFLT_10                     0x44
#define SUMO_DTC_DFLT_11                     0x44
#define SUMO_DTC_DFLT_12                     0x44
#define SUMO_DTC_DFLT_13                     0x44
#define SUMO_DTC_DFLT_14                     0x44

#define SUMO_AH_DFLT               5

#define SUMO_R_DFLT0               70
#define SUMO_R_DFLT1               70
#define SUMO_R_DFLT2               70
#define SUMO_R_DFLT3               70
#define SUMO_R_DFLT4               100

#define SUMO_L_DFLT0               0
#define SUMO_L_DFLT1               20
#define SUMO_L_DFLT2               20
#define SUMO_L_DFLT3               20
#define SUMO_L_DFLT4               20
#define SUMO_VRC_DFLT              0x30033
#define SUMO_MGCGTTLOCAL0_DFLT     0
#define SUMO_MGCGTTLOCAL1_DFLT     0
#define SUMO_GICST_DFLT            19
#define SUMO_SST_DFLT              8
#define SUMO_VOLTAGEDROPT_DFLT     1
#define SUMO_GFXPOWERGATINGT_DFLT  100

/* sumo_dpm.c */
u32 sumo_get_xclk(struct radeon_device *rdev);
void sumo_gfx_clockgating_initialize(struct radeon_device *rdev);
void sumo_program_vc(struct radeon_device *rdev, u32 vrc);
void sumo_clear_vc(struct radeon_device *rdev);
void sumo_program_sstp(struct radeon_device *rdev);
void sumo_take_smu_control(struct radeon_device *rdev, bool enable);
void sumo_construct_sclk_voltage_mapping_table(struct radeon_device *rdev,
					       struct sumo_sclk_voltage_mapping_table *sclk_voltage_mapping_table,
					       ATOM_AVAILABLE_SCLK_LIST *table);
void sumo_construct_vid_mapping_table(struct radeon_device *rdev,
				      struct sumo_vid_mapping_table *vid_mapping_table,
				      ATOM_AVAILABLE_SCLK_LIST *table);
u32 sumo_convert_vid2_to_vid7(struct radeon_device *rdev,
			      struct sumo_vid_mapping_table *vid_mapping_table,
			      u32 vid_2bit);
u32 sumo_get_sleep_divider_from_id(u32 id);
u32 sumo_get_sleep_divider_id_from_clock(struct radeon_device *rdev,
					 u32 sclk,
					 u32 min_sclk_in_sr);

/* sumo_smc.c */
void sumo_initialize_m3_arb(struct radeon_device *rdev);
void sumo_smu_pg_init(struct radeon_device *rdev);
void sumo_set_tdp_limit(struct radeon_device *rdev, u32 index, u32 tdp_limit);
void sumo_smu_notify_alt_vddnb_change(struct radeon_device *rdev,
				      bool powersaving, bool force_nbps1);
void sumo_boost_state_enable(struct radeon_device *rdev, bool enable);
void sumo_enable_boost_timer(struct radeon_device *rdev);
u32 sumo_get_running_fw_version(struct radeon_device *rdev);

#endif
