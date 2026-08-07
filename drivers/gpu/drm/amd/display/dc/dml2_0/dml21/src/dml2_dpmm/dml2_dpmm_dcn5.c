// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_dpmm_dcn5.h"
#include "lib_float_math.h"

static bool add_margin_and_round_to_dfs_grainularity(double clock_khz, double margin, unsigned long vco_freq_khz, unsigned long *rounded_khz, uint32_t *divider_id)
{
	enum dentist_divider_range {
		DFS_DIVIDER_RANGE_1_START = 8, /* 2.00 */
		DFS_DIVIDER_RANGE_1_STEP = 1, /* 0.25 */
		DFS_DIVIDER_RANGE_2_START = 64, /* 16.00 */
		DFS_DIVIDER_RANGE_2_STEP = 2, /* 0.50 */
		DFS_DIVIDER_RANGE_3_START = 128, /* 32.00 */
		DFS_DIVIDER_RANGE_3_STEP = 4, /* 1.00 */
		DFS_DIVIDER_RANGE_4_START = 248, /* 62.00 */
		DFS_DIVIDER_RANGE_4_STEP = 264, /* 66.00 */
		DFS_DIVIDER_RANGE_SCALE_FACTOR = 4
	};

	enum DFS_base_divider_id {
		DFS_BASE_DID_1 = 0x08,
		DFS_BASE_DID_2 = 0x40,
		DFS_BASE_DID_3 = 0x60,
		DFS_BASE_DID_4 = 0x7e,
		DFS_MAX_DID = 0x7f
	};

	unsigned int divider;

	if (clock_khz < 1 || vco_freq_khz < 1 || clock_khz > vco_freq_khz)
		return false;

	clock_khz *= 1.0 + margin;

	divider = (unsigned int)((int)DFS_DIVIDER_RANGE_SCALE_FACTOR * (vco_freq_khz / clock_khz));

	/* we want to floor here to get higher clock than required rather than lower */
	if (divider < DFS_DIVIDER_RANGE_2_START) {
		if (divider < DFS_DIVIDER_RANGE_1_START)
			*divider_id = DFS_BASE_DID_1;
		else
			*divider_id = DFS_BASE_DID_1 + ((divider - DFS_DIVIDER_RANGE_1_START) / DFS_DIVIDER_RANGE_1_STEP);
	} else if (divider < DFS_DIVIDER_RANGE_3_START) {
		*divider_id = DFS_BASE_DID_2 + ((divider - DFS_DIVIDER_RANGE_2_START) / DFS_DIVIDER_RANGE_2_STEP);
	} else if (divider < DFS_DIVIDER_RANGE_4_START) {
		*divider_id = DFS_BASE_DID_3 + ((divider - DFS_DIVIDER_RANGE_3_START) / DFS_DIVIDER_RANGE_3_STEP);
	} else {
		*divider_id = DFS_BASE_DID_4 + ((divider - DFS_DIVIDER_RANGE_4_START) / DFS_DIVIDER_RANGE_4_STEP);
		if (*divider_id > DFS_MAX_DID)
			*divider_id = DFS_MAX_DID;
	}

	*rounded_khz = vco_freq_khz * DFS_DIVIDER_RANGE_SCALE_FACTOR / divider;

	return true;
}

static bool round_to_non_dfs_granularity(unsigned long dispclk_khz, unsigned long dpprefclk_khz, unsigned long dtbrefclk_khz,
	unsigned long *rounded_dispclk_khz, unsigned long *rounded_dpprefclk_khz, unsigned long *rounded_dtbrefclk_khz)
{
	unsigned long pll_frequency_khz;

	pll_frequency_khz = (unsigned long) math_max2(600000, math_ceil2(math_max3(dispclk_khz, dpprefclk_khz, dtbrefclk_khz), 1000));

	*rounded_dispclk_khz = pll_frequency_khz / (unsigned long) math_min2(pll_frequency_khz / dispclk_khz, 32);

	*rounded_dpprefclk_khz = pll_frequency_khz / (unsigned long) math_min2(pll_frequency_khz / dpprefclk_khz, 32);

	if (dtbrefclk_khz > 0) {
		*rounded_dtbrefclk_khz = pll_frequency_khz / (unsigned long) math_min2(pll_frequency_khz / dtbrefclk_khz, 32);
	} else {
		*rounded_dtbrefclk_khz = 0;
	}

	return true;
}

static bool validate_min_clocks(const struct dml2_display_solution *solution, struct dml2_display_cfg_programming *programming, const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned int i;

	if (!utm_soc_bb || !programming)
		return false;

	if (programming->min_clocks.dcn4x.dispclk_khz > utm_soc_bb->max_dispclk_khz)
		return false;

	if (programming->min_clocks.dcn4x.dpprefclk_khz > utm_soc_bb->max_dppclk_khz)
		return false;

	if (programming->min_clocks.dcn4x.dtbrefclk_khz > utm_soc_bb->max_dtbclk_khz)
		return false;

	for (i = 0; i < solution->dispcfg.num_planes; i++)
		if (programming->plane_programming[i].min_clocks.dcn4x.dppclk_khz > utm_soc_bb->max_dppclk_khz)
			return false;

	for (i = 0; i < solution->dispcfg.num_streams; i++)
		if (programming->stream_programming[i].min_clocks.dcn4x.dscclk_khz > utm_soc_bb->max_dscclk_khz)
			return false;
		else if (programming->stream_programming[i].min_clocks.dcn4x.dtbclk_khz > utm_soc_bb->max_dtbclk_khz)
			return false;
		else if (programming->stream_programming[i].min_clocks.dcn4x.phyclk_khz > utm_soc_bb->max_phyclk_khz)
			return false;

	return true;
}

static bool are_timings_trivially_synchronizable(const struct dml2_display_solution *solution, int mask)
{
	unsigned int i;
	bool identical = true;
	bool contains_drr = false;
	unsigned int remap_array[DML2_MAX_PLANES];
	unsigned int remap_array_size = 0;

	// Create a remap array to enable simple iteration through only masked stream indicies
	for (i = 0; i < solution->dispcfg.num_streams; i++) {
		if (mask & (0x1 << i)) {
			remap_array[remap_array_size++] = i;
		}
	}

	// 0 or 1 display is always trivially synchronizable
	if (remap_array_size <= 1)
		return true;

	// Check that all displays timings are the same
	for (i = 1; i < remap_array_size; i++) {
		if (memcmp(&solution->dispcfg.stream_descriptors[remap_array[i - 1]].timing, &solution->dispcfg.stream_descriptors[remap_array[i]].timing, sizeof(struct dml2_timing_cfg))) {
			identical = false;
			break;
		}
	}

	// Check if any displays are drr
	for (i = 0; i < remap_array_size; i++) {
		if (solution->dispcfg.stream_descriptors[remap_array[i]].timing.drr_config.enabled) {
			contains_drr = true;
			break;
		}
	}

	// Trivial sync is possible if all displays are identical and none are DRR
	return !contains_drr && identical;
}

static int find_smallest_idle_time_in_vblank_us(const struct dml2_display_solution *solution, int mask)
{
	unsigned int i;
	int min_idle_us = 0;
	unsigned int remap_array[DML2_MAX_PLANES];
	unsigned int remap_array_size = 0;
	const struct dml2_core_mode_support_result *mode_support_result = &solution->validation_result.mode_support;

	// Create a remap array to enable simple iteration through only masked stream indicies
	for (i = 0; i < solution->dispcfg.num_streams; i++) {
		if (mask & (0x1 << i)) {
			remap_array[remap_array_size++] = i;
		}
	}

	if (remap_array_size == 0)
		return 0;

	min_idle_us = mode_support_result->cfg_support_info.stream_support_info[remap_array[0]].vblank_reserved_time_us;

	for (i = 1; i < remap_array_size; i++) {
		if (min_idle_us > mode_support_result->cfg_support_info.stream_support_info[remap_array[i]].vblank_reserved_time_us)
			min_idle_us = mode_support_result->cfg_support_info.stream_support_info[remap_array[i]].vblank_reserved_time_us;
	}

	return min_idle_us;
}

static int get_displays_without_vactive_margin_mask(const struct dml2_display_solution *solution, const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned int i;
	int displays_without_vactive_margin_mask = 0x0;
	const struct dml2_core_mode_support_result *mode_support_result = &solution->validation_result.mode_support;

	for (i = 0; i < solution->dispcfg.num_planes; i++) {
		if (mode_support_result->cfg_support_info.plane_support_info[i].active_latency_hiding_us
			< (int)utm_soc_bb->power_management_parameters.fclk_change_blackout_us)
			displays_without_vactive_margin_mask |= (0x1 << i);
	}

	return displays_without_vactive_margin_mask;
}

static unsigned long calculate_dispclk_khz(const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb, const struct dml2_core_ip_params *ip_params)
{
	double dispclk_khz;

	// need some massaging for the dispclk ramping cases:
	dispclk_khz = solution->validation_result.mode_support.global.dispclk_khz * (1 + utm_soc_bb->dcn_downspread_percent / 100.0) * (1.0 + ip_params->dispclk_ramp_margin_percent / 100.0);
	// ramping margin should not make dispclk exceed the maximum dispclk speed:
	dispclk_khz = math_min2(dispclk_khz, utm_soc_bb->max_dispclk_khz);
	// but still the required dispclk can be more than the maximum dispclk speed:
	dispclk_khz = math_max2(dispclk_khz, solution->validation_result.mode_support.global.dispclk_khz * (1 + utm_soc_bb->dcn_downspread_percent / 100.0));

	return (unsigned long) dispclk_khz;
}

static unsigned long calculate_dpprefclk_khz(const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned long dpprefclk_khz = 0;
	unsigned int i;

	// DPP Ref is always set to max of all DPP clocks
	for (i = 0; i < solution->dispcfg.num_planes; i++)
		if (dpprefclk_khz < solution->validation_result.mode_support.per_plane[i].dppclk_khz)
			dpprefclk_khz = solution->validation_result.mode_support.per_plane[i].dppclk_khz;
	dpprefclk_khz = (unsigned long) (dpprefclk_khz * (1 + utm_soc_bb->dcn_downspread_percent / 100.0));

	return dpprefclk_khz;
}

static unsigned long calculate_dtbrefclk_khz(const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned long dtbrefclk_khz = 0;
	unsigned int i;

	// DTB Ref is always set to max of all DTB clocks
	for (i = 0; i < solution->dispcfg.num_streams; i++)
		if (dtbrefclk_khz < solution->validation_result.mode_support.per_stream[i].dtbclk_khz)
			dtbrefclk_khz = solution->validation_result.mode_support.per_stream[i].dtbclk_khz;
	dtbrefclk_khz = (unsigned long)(dtbrefclk_khz * (1 + utm_soc_bb->dcn_downspread_percent / 100.0));

	return dtbrefclk_khz;
}

static unsigned long calculate_dppclk_khz_plane_index(unsigned int plane_index,
		const struct dml2_display_solution *solution,
		const struct dml2_utm_soc_bb *utm_soc_bb,
		unsigned long dpprefclk_khz)
{
	return (unsigned long)(dpprefclk_khz / 255.0
				* math_ceil2(solution->validation_result.mode_support.per_plane[plane_index].dppclk_khz * (1.0 + utm_soc_bb->dcn_downspread_percent / 100.0) * 255.0 / dpprefclk_khz, 1.0));
}

static void round_min_clocks_to_granularity(struct dml2_display_cfg_programming *programming, const struct dml2_utm_soc_bb *utm_soc_bb)
{
	if (utm_soc_bb->no_dfs) {
		round_to_non_dfs_granularity(programming->min_clocks.dcn4x.dispclk_khz, programming->min_clocks.dcn4x.dpprefclk_khz, programming->min_clocks.dcn4x.dtbrefclk_khz,
			&programming->min_clocks.dcn4x.dispclk_khz, &programming->min_clocks.dcn4x.dpprefclk_khz, &programming->min_clocks.dcn4x.dtbrefclk_khz);
	} else {
		add_margin_and_round_to_dfs_grainularity(programming->min_clocks.dcn4x.dispclk_khz, 0.0,
			(unsigned long)(utm_soc_bb->dispclk_dppclk_vco_speed_mhz * 1000), &programming->min_clocks.dcn4x.dispclk_khz, &programming->min_clocks.dcn4x.divider_ids.dispclk_did);

		add_margin_and_round_to_dfs_grainularity(programming->min_clocks.dcn4x.dpprefclk_khz, 0.0,
			(unsigned long)(utm_soc_bb->dispclk_dppclk_vco_speed_mhz * 1000), &programming->min_clocks.dcn4x.dpprefclk_khz, &programming->min_clocks.dcn4x.divider_ids.dpprefclk_did);

		add_margin_and_round_to_dfs_grainularity(programming->min_clocks.dcn4x.dtbrefclk_khz, 0.0,
			(unsigned long)(utm_soc_bb->dispclk_dppclk_vco_speed_mhz * 1000), &programming->min_clocks.dcn4x.dtbrefclk_khz, &programming->min_clocks.dcn4x.divider_ids.dtbrefclk_did);
	}
}

static bool dcn5_populate_min_clocks_in_programming(struct dml2_display_cfg_programming *programming,
		const struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_core_ip_params *ip_params,
		const struct dml2_display_solution *solution)
{
	unsigned int i;

	programming->min_clocks.dcn4x.dispclk_khz =
			calculate_dispclk_khz(solution, utm_soc_bb, ip_params);
	programming->min_clocks.dcn4x.dpprefclk_khz =
			calculate_dpprefclk_khz(solution, utm_soc_bb);
	programming->min_clocks.dcn4x.dtbrefclk_khz =
			calculate_dtbrefclk_khz(solution, utm_soc_bb);
	programming->min_clocks.dcn4x.deepsleep_dcfclk_khz =
			(unsigned long) math_min2((double)solution->validation_result.mode_support.global.dcfclk_deepsleep_khz, (double)solution->sop_constraint.dcn5.clocks.dcfclk_khz);
	programming->min_clocks.dcn4x.socclk_khz = solution->sop_constraint.dcn5.clocks.socclk_khz;
	programming->min_clocks.dcn4x.active.dcfclk_khz = solution->sop_constraint.dcn5.clocks.dcfclk_khz;
	programming->min_clocks.dcn4x.active.fclk_khz = solution->sop_constraint.dcn5.clocks.fclk_khz;
	programming->min_clocks.dcn4x.active.uclk_khz = solution->sop_constraint.dcn5.clocks.uclk_khz;
	round_min_clocks_to_granularity(programming, utm_soc_bb);
	for (i = 0; i < solution->dispcfg.num_planes; i++)
		programming->plane_programming[i].min_clocks.dcn4x.dppclk_khz =
				calculate_dppclk_khz_plane_index(i, solution, utm_soc_bb,
						programming->min_clocks.dcn4x.dpprefclk_khz);
	for (i = 0; i < solution->dispcfg.num_streams; i++) {
		programming->stream_programming[i].min_clocks.dcn4x.dscclk_khz =
				solution->validation_result.mode_support.per_stream[i].dscclk_khz;
		programming->stream_programming[i].min_clocks.dcn4x.dtbclk_khz =
				solution->validation_result.mode_support.per_stream[i].dtbclk_khz;
		programming->stream_programming[i].min_clocks.dcn4x.phyclk_khz =
				solution->validation_result.mode_support.per_stream[i].phyclk_khz;
	}

	return validate_min_clocks(solution, programming, utm_soc_bb);
}

void dcn5_populate_pstate_support_in_programming(struct dml2_display_cfg_programming *programming,
		const struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_display_solution *solution)
{
	unsigned int plane_index;
	int displays_without_vactive_margin_mask = 0x0;
	bool uclk_pstate_supported = true;
	int min_idle_us = 0;

	for (plane_index = 0; plane_index < solution->dispcfg.num_planes; plane_index++) {
		if (solution->uclk_pstate_params.pstate_switch_modes[plane_index] == dml2_pstate_method_na) {
			/* UCLK P-State is supported if the pstate method is populated */
			uclk_pstate_supported = false;
			break;
		}
	}
	programming->uclk_pstate_supported = uclk_pstate_supported;
	programming->fclk_pstate_supported = false;
	displays_without_vactive_margin_mask =
		get_displays_without_vactive_margin_mask(solution, utm_soc_bb);
	if (displays_without_vactive_margin_mask == 0) {
		programming->fclk_pstate_supported = true;
	} else {
		if (are_timings_trivially_synchronizable(solution, displays_without_vactive_margin_mask)) {
			min_idle_us = find_smallest_idle_time_in_vblank_us(solution, displays_without_vactive_margin_mask);

			if (min_idle_us >= utm_soc_bb->power_management_parameters.fclk_change_blackout_us)
				programming->fclk_pstate_supported = true;
		}
	}
}

void dcn5_populate_stutter_support_in_programming(struct dml2_display_cfg_programming *programming,
		const struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_display_solution *solution)
{
	int min_idle_us;

	min_idle_us = find_smallest_idle_time_in_vblank_us(solution, 0xFF);

	if (utm_soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us > 0 &&
			min_idle_us >= utm_soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us)
		programming->stutter.supported_in_blank = true;
	else
		programming->stutter.supported_in_blank = false;

	if (utm_soc_bb->power_management_parameters.z8_min_idle_time > 0 &&
			programming->informative.power_management.z8.stutter_period >= utm_soc_bb->power_management_parameters.z8_min_idle_time)
		programming->z8_stutter.meets_eco = true;
	else
		programming->z8_stutter.meets_eco = false;

	if (utm_soc_bb->power_management_parameters.z8_stutter_exit_latency_us > 0 &&
			min_idle_us >= utm_soc_bb->power_management_parameters.z8_stutter_exit_latency_us)
		programming->z8_stutter.supported_in_blank = true;
	else
		programming->z8_stutter.supported_in_blank = false;
}

static void dcn5_populate_qos_bound_in_programming(struct dml2_display_cfg_programming *programming,
		const struct dml2_display_solution *solution)
{
	programming->qos_bound.latency_ub = solution->sop_constraint.dcn5.latency;
	/*
	 * A true UTM design enables dynamic bandwidth percentage allocation. At DPM1 when the system is idle, we can
	 * allocate more bandwidth percentage share to DCN. The clocks can be designed to lower values to save power.
	 * When the system is at its peak load. The system can still increase to the max DPM level and reduce bandwidth
	 * percentage share to DCN as long as it still meets DCN's QoS requirements. So other clients such as GFX may be
	 * given even more bandwidth at its peak load. The UTM design increases DML complicity because higher DPM level
	 * no longer equates to more or equal bandwidth to DCN. It now depends on the bandwidth percentage share (aka.
	 * bandwidth derate). DCN needs to express its true QoS bandwidth requirements without knowing bandwidth
	 * availability at any DPM levels.
	 *
	 * We assume that there is a top down decision that this generation of hardware has a fixed bandwidth derate
	 * across all DPM levels and it doesn't require a true UTM design. As such the QoS bandwidth requirement can be
	 * based on the fixed bandwidth availability at lowest supported DPM. This simplifies mode programming
	 * calculation. We are making corresponding QoS Bound change below to be compatible with the simplified mode
	 * programming calculation.
	 */
	// programming->qos_bound.bandwidth_lb = solution->validation_result.mode_support.bandwidth_upper_bound;
	programming->qos_bound.bandwidth_lb.dcn5.urgent_bandwidth_kbps = solution->sop_constraint.dcn5.min_available_urgent_bandwidth_KBps;
}

bool dpmm_dcn5_map_mode_to_soc_dpm(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	dcn5_populate_qos_bound_in_programming(in_out->programming, in_out->solution);
	dcn5_populate_pstate_support_in_programming(in_out->programming, in_out->utm_soc_bb, in_out->solution);
	dcn5_populate_stutter_support_in_programming(in_out->programming, in_out->utm_soc_bb, in_out->solution);
	return dcn5_populate_min_clocks_in_programming(
			in_out->programming, in_out->utm_soc_bb, in_out->ip, in_out->solution);
}

bool dpmm_dcn5_map_watermarks(struct dml2_dpmm_map_watermarks_params_in_out *in_out)
{
	const struct dml2_display_cfg *display_cfg = &in_out->solution->dispcfg;
	const struct dml2_core_internal_display_mode_lib *mode_lib = &in_out->core->clean_me_up.mode_lib;
	struct dml2_dchub_global_register_set *dchubbub_regs = &in_out->programming->global_regs;
	struct dml2_mcif_global_register_set *mcif_regs = &in_out->programming->mcif_global_regs;

	double refclk_freq_in_mhz = (display_cfg->overrides.hw.dlg_ref_clk_mhz > 0) ? (double)display_cfg->overrides.hw.dlg_ref_clk_mhz : in_out->core->utm_soc_bb->dchub_refclk_mhz;

	/* set A */
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].fclk_pstate = (int unsigned)(mode_lib->mp.Watermark.FCLKChangeWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].sr_enter = (int unsigned)(mode_lib->mp.Watermark.StutterEnterPlusExitWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].sr_exit = (int unsigned)(mode_lib->mp.Watermark.StutterExitWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].sr_enter_z8 = (int unsigned)(mode_lib->mp.Watermark.Z8StutterEnterPlusExitWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].sr_exit_z8 = (int unsigned)(mode_lib->mp.Watermark.Z8StutterExitWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].temp_read_or_ppt = (int unsigned)(mode_lib->mp.Watermark.temp_read_or_ppt_watermark_us * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].uclk_pstate = (int unsigned)(mode_lib->mp.Watermark.DRAMClockChangeWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].urgent = (int unsigned)(mode_lib->mp.Watermark.UrgentWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].usr = (int unsigned)(mode_lib->mp.Watermark.USRRetrainingWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].refcyc_per_trip_to_mem = (unsigned int)(mode_lib->mp.Watermark.UrgentWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].refcyc_per_meta_trip_to_mem = (unsigned int)(mode_lib->mp.Watermark.UrgentWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].frac_urg_bw_flip = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidthImmediateFlip * 1000);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].frac_urg_bw_nom = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidth * 1000);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].frac_urg_bw_mall = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidthMALL * 1000);

	/* set B */
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].fclk_pstate = (int unsigned)(mode_lib->mp.Watermark.FCLKChangeWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].sr_enter = (int unsigned)(mode_lib->mp.Watermark.StutterEnterPlusExitWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].sr_exit = (int unsigned)(mode_lib->mp.Watermark.StutterExitWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].sr_enter_z8 = (int unsigned)(mode_lib->mp.Watermark.Z8StutterEnterPlusExitWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].sr_exit_z8 = (int unsigned)(mode_lib->mp.Watermark.Z8StutterExitWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].temp_read_or_ppt = (int unsigned)(mode_lib->mp.Watermark.temp_read_or_ppt_watermark_us * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].uclk_pstate = (int unsigned)(mode_lib->mp.Watermark.DRAMClockChangeWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].urgent = (int unsigned)(mode_lib->mp.Watermark.UrgentWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].usr = (int unsigned)(mode_lib->mp.Watermark.USRRetrainingWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].refcyc_per_trip_to_mem = (unsigned int)(mode_lib->mp.Watermark.UrgentWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].refcyc_per_meta_trip_to_mem = (unsigned int)(mode_lib->mp.Watermark.UrgentWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].frac_urg_bw_flip = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidthImmediateFlip * 1000);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].frac_urg_bw_nom = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidth * 1000);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B].frac_urg_bw_mall = (unsigned int)(mode_lib->mp.FractionOfUrgentBandwidthMALL * 1000);

	dchubbub_regs->num_watermark_sets = 2;

	/* MCIF */
	mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].urgent = (int unsigned)(mode_lib->mp.Watermark.WritebackUrgentWatermark * 1000.0);
	mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].uclk_pstate = (int unsigned)(mode_lib->mp.Watermark.WritebackDRAMClockChangeWatermark * 1000.0);
	mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].fclk_pstate = (int unsigned)(mode_lib->mp.Watermark.WritebackFCLKChangeWatermark * 1000.0);
	mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].temp_read_or_ppt = (int unsigned)(mode_lib->mp.Watermark.writeback_temp_read_or_ppt_watermark_us * 1000.0);

	/* replicate sets A through D */
	memcpy(&mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_B], &mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A], sizeof(mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A]));
	memcpy(&mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_C], &mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A], sizeof(mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A]));
	memcpy(&mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_D], &mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A], sizeof(mcif_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A]));

	mcif_regs->num_watermark_sets = 4;

	return true;
}
