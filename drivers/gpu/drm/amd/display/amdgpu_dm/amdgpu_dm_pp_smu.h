/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#ifndef __AMDGPU_DM_PP_SMU_H__
#define __AMDGPU_DM_PP_SMU_H__

#include "dm_pp_interface.h"

struct amd_pp_display_configuration;
struct pp_smu_wm_range_sets;
struct dm_pp_wm_sets_with_clock_ranges_soc15;

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
void build_pm_display_cfg(struct amd_pp_display_configuration *pm_display_cfg,
			  const struct dm_pp_display_configuration *pp_display_cfg);
void build_wm_clock_ranges_soc15(const struct pp_smu_wm_range_sets *ranges,
		struct dm_pp_wm_sets_with_clock_ranges_soc15 *wm_with_clock_ranges);
void get_default_clock_levels(enum dm_pp_clock_type clk_type, struct dm_pp_clock_levels *clks);
enum amd_pp_clock_type dc_to_pp_clock_type(enum dm_pp_clock_type dm_pp_clk_type);
void pp_to_dc_clock_levels(const struct amd_pp_clocks *pp_clks,
			   struct dm_pp_clock_levels *dc_clks,
			   enum dm_pp_clock_type dc_clk_type);
void pp_to_dc_clock_levels_with_latency(const struct pp_clock_levels_with_latency *pp_clks,
					struct dm_pp_clock_levels_with_latency *clk_level_info,
					enum dm_pp_clock_type dc_clk_type);
void pp_to_dc_clock_levels_with_voltage(const struct pp_clock_levels_with_voltage *pp_clks,
					struct dm_pp_clock_levels_with_voltage *clk_level_info,
					enum dm_pp_clock_type dc_clk_type);
void cap_clock_levels_to_validation(struct dm_pp_clock_levels *dc_clks,
				    enum dm_pp_clock_type clk_type,
				    const struct amd_pp_simple_clock_info *validation_clks);
bool pp_smu_nv_clock_id_to_pp(enum pp_smu_nv_clock_id clock_id,
			      enum amd_pp_clock_type *clock_type);
void pp_rv_set_wm_ranges(struct pp_smu *pp, struct pp_smu_wm_range_sets *ranges);
void pp_rv_set_pme_wa_enable(struct pp_smu *pp);
void pp_rv_set_active_display_count(struct pp_smu *pp, int count);
void pp_rv_set_min_deep_sleep_dcfclk(struct pp_smu *pp, int clock);
void pp_rv_set_hard_min_dcefclk_by_freq(struct pp_smu *pp, int clock);
void pp_rv_set_hard_min_fclk_by_freq(struct pp_smu *pp, int mhz);
enum pp_smu_status pp_nv_set_wm_ranges(struct pp_smu *pp,
					struct pp_smu_wm_range_sets *ranges);
enum pp_smu_status pp_nv_set_display_count(struct pp_smu *pp, int count);
enum pp_smu_status pp_nv_set_min_deep_sleep_dcfclk(struct pp_smu *pp, int mhz);
enum pp_smu_status pp_nv_set_hard_min_dcefclk_by_freq(struct pp_smu *pp, int mhz);
enum pp_smu_status pp_nv_set_hard_min_uclk_by_freq(struct pp_smu *pp, int mhz);
enum pp_smu_status pp_nv_set_pstate_handshake_support(struct pp_smu *pp,
						      bool pstate_handshake_supported);
enum pp_smu_status pp_nv_set_voltage_by_freq(struct pp_smu *pp,
					     enum pp_smu_nv_clock_id clock_id, int mhz);
enum pp_smu_status pp_nv_get_maximum_sustainable_clocks(struct pp_smu *pp,
							struct pp_smu_nv_clock_table *max_clocks);
enum pp_smu_status pp_nv_get_uclk_dpm_states(struct pp_smu *pp,
					     unsigned int *clock_values_in_khz,
					     unsigned int *num_states);
enum pp_smu_status pp_rn_get_dpm_clock_table(struct pp_smu *pp,
					     struct dpm_clocks *clock_table);
#endif

#endif /* __AMDGPU_DM_PP_SMU_H__ */
