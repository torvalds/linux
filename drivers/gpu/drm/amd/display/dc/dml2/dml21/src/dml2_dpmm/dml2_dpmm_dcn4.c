// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_dpmm_dcn4.h"
#include "dml2_internal_shared_types.h"
#include "dml_top_types.h"
#include "lib_float_math.h"

static double dram_bw_kbps_to_uclk_khz(unsigned long long bandwidth_kbps, const struct dml2_dram_params *dram_config)
{
	double uclk_khz = 0;
	unsigned long uclk_mbytes_per_tick = 0;

	uclk_mbytes_per_tick = dram_config->channel_count * dram_config->channel_width_bytes * dram_config->transactions_per_clock;

	uclk_khz = (double)bandwidth_kbps / uclk_mbytes_per_tick;

	return uclk_khz;
}

static void get_minimum_clocks_for_latency(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out,
	double *uclk,
	double *fclk,
	double *dcfclk)
{
	int min_clock_index_for_latency;

	if (in_out->display_cfg->stage3.success)
		min_clock_index_for_latency = in_out->display_cfg->stage3.min_clk_index_for_latency;
	else
		min_clock_index_for_latency = in_out->display_cfg->stage1.min_clk_index_for_latency;

	*dcfclk = in_out->min_clk_table->dram_bw_table.entries[min_clock_index_for_latency].min_dcfclk_khz;
	*fclk = in_out->min_clk_table->dram_bw_table.entries[min_clock_index_for_latency].min_fclk_khz;
	*uclk = dram_bw_kbps_to_uclk_khz(in_out->min_clk_table->dram_bw_table.entries[min_clock_index_for_latency].pre_derate_dram_bw_kbps,
		&in_out->soc_bb->clk_table.dram_config);
}

static unsigned long dml_round_up(double a)
{
	if (a - (unsigned long)a > 0) {
		return ((unsigned long)a) + 1;
	}
	return (unsigned long)a;
}

static void calculate_system_active_minimums(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	double min_uclk_avg, min_uclk_urgent, min_uclk_bw;
	double min_fclk_avg, min_fclk_urgent, min_fclk_bw;
	double min_dcfclk_avg, min_dcfclk_urgent, min_dcfclk_bw;
	double min_uclk_latency, min_fclk_latency, min_dcfclk_latency;
	const struct dml2_core_mode_support_result *mode_support_result = &in_out->display_cfg->mode_support_result;

	min_uclk_avg = dram_bw_kbps_to_uclk_khz(mode_support_result->global.active.average_bw_dram_kbps, &in_out->soc_bb->clk_table.dram_config);
	min_uclk_avg = (double)min_uclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_average.dram_derate_percent_pixel / 100);

	min_uclk_urgent = dram_bw_kbps_to_uclk_khz(mode_support_result->global.active.urgent_bw_dram_kbps, &in_out->soc_bb->clk_table.dram_config);
	if (in_out->display_cfg->display_config.hostvm_enable)
		min_uclk_urgent = (double)min_uclk_urgent / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_urgent.dram_derate_percent_pixel_and_vm / 100);
	else
		min_uclk_urgent = (double)min_uclk_urgent / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_urgent.dram_derate_percent_pixel / 100);

	min_uclk_bw = min_uclk_urgent > min_uclk_avg ? min_uclk_urgent : min_uclk_avg;

	min_fclk_avg = (double)mode_support_result->global.active.average_bw_sdp_kbps / in_out->soc_bb->fabric_datapath_to_dcn_data_return_bytes;
	min_fclk_avg = (double)min_fclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_average.fclk_derate_percent / 100);

	min_fclk_urgent = (double)mode_support_result->global.active.urgent_bw_sdp_kbps / in_out->soc_bb->fabric_datapath_to_dcn_data_return_bytes;
	min_fclk_urgent = (double)min_fclk_urgent / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_urgent.fclk_derate_percent / 100);

	min_fclk_bw = min_fclk_urgent > min_fclk_avg ? min_fclk_urgent : min_fclk_avg;

	min_dcfclk_avg = (double)mode_support_result->global.active.average_bw_sdp_kbps / in_out->soc_bb->return_bus_width_bytes;
	min_dcfclk_avg = (double)min_dcfclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_average.dcfclk_derate_percent / 100);

	min_dcfclk_urgent = (double)mode_support_result->global.active.urgent_bw_sdp_kbps / in_out->soc_bb->return_bus_width_bytes;
	min_dcfclk_urgent = (double)min_dcfclk_urgent / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_urgent.dcfclk_derate_percent / 100);

	min_dcfclk_bw = min_dcfclk_urgent > min_dcfclk_avg ? min_dcfclk_urgent : min_dcfclk_avg;

	get_minimum_clocks_for_latency(in_out, &min_uclk_latency, &min_fclk_latency, &min_dcfclk_latency);

	in_out->programming->min_clocks.dcn4x.active.uclk_khz = dml_round_up(min_uclk_bw > min_uclk_latency ? min_uclk_bw : min_uclk_latency);
	in_out->programming->min_clocks.dcn4x.active.fclk_khz = dml_round_up(min_fclk_bw > min_fclk_latency ? min_fclk_bw : min_fclk_latency);
	in_out->programming->min_clocks.dcn4x.active.dcfclk_khz = dml_round_up(min_dcfclk_bw > min_dcfclk_latency ? min_dcfclk_bw : min_dcfclk_latency);
}

static void calculate_svp_prefetch_minimums(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	double min_uclk_avg, min_uclk_urgent, min_uclk_bw;
	double min_fclk_avg, min_fclk_urgent, min_fclk_bw;
	double min_dcfclk_avg, min_dcfclk_urgent, min_dcfclk_bw;
	double min_fclk_latency, min_dcfclk_latency;
	double min_uclk_latency;
	const struct dml2_core_mode_support_result *mode_support_result = &in_out->display_cfg->mode_support_result;

	/* assumes DF throttling is enabled */
	min_uclk_avg = dram_bw_kbps_to_uclk_khz(mode_support_result->global.svp_prefetch.average_bw_dram_kbps, &in_out->soc_bb->clk_table.dram_config);
	min_uclk_avg = (double)min_uclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.dcn_mall_prefetch_average.dram_derate_percent_pixel / 100);

	min_uclk_urgent = dram_bw_kbps_to_uclk_khz(mode_support_result->global.svp_prefetch.urgent_bw_dram_kbps, &in_out->soc_bb->clk_table.dram_config);
	min_uclk_urgent = (double)min_uclk_urgent / ((double)in_out->soc_bb->qos_parameters.derate_table.dcn_mall_prefetch_urgent.dram_derate_percent_pixel / 100);

	min_uclk_bw = min_uclk_urgent > min_uclk_avg ? min_uclk_urgent : min_uclk_avg;

	min_fclk_avg = (double)mode_support_result->global.svp_prefetch.average_bw_sdp_kbps / in_out->soc_bb->fabric_datapath_to_dcn_data_return_bytes;
	min_fclk_avg = (double)min_fclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.dcn_mall_prefetch_average.fclk_derate_percent / 100);

	min_fclk_urgent = (double)mode_support_result->global.svp_prefetch.urgent_bw_sdp_kbps / in_out->soc_bb->fabric_datapath_to_dcn_data_return_bytes;
	min_fclk_urgent = (double)min_fclk_urgent / ((double)in_out->soc_bb->qos_parameters.derate_table.dcn_mall_prefetch_urgent.fclk_derate_percent / 100);

	min_fclk_bw = min_fclk_urgent > min_fclk_avg ? min_fclk_urgent : min_fclk_avg;

	min_dcfclk_avg = (double)mode_support_result->global.svp_prefetch.average_bw_sdp_kbps / in_out->soc_bb->return_bus_width_bytes;
	min_dcfclk_avg = (double)min_dcfclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.dcn_mall_prefetch_average.dcfclk_derate_percent / 100);

	min_dcfclk_urgent = (double)mode_support_result->global.svp_prefetch.urgent_bw_sdp_kbps / in_out->soc_bb->return_bus_width_bytes;
	min_dcfclk_urgent = (double)min_dcfclk_urgent / ((double)in_out->soc_bb->qos_parameters.derate_table.dcn_mall_prefetch_urgent.dcfclk_derate_percent / 100);

	min_dcfclk_bw = min_dcfclk_urgent > min_dcfclk_avg ? min_dcfclk_urgent : min_dcfclk_avg;

	get_minimum_clocks_for_latency(in_out, &min_uclk_latency, &min_fclk_latency, &min_dcfclk_latency);

	in_out->programming->min_clocks.dcn4x.svp_prefetch.uclk_khz = dml_round_up(min_uclk_bw > min_uclk_latency ? min_uclk_bw : min_uclk_latency);
	in_out->programming->min_clocks.dcn4x.svp_prefetch.fclk_khz = dml_round_up(min_fclk_bw > min_fclk_latency ? min_fclk_bw : min_fclk_latency);
	in_out->programming->min_clocks.dcn4x.svp_prefetch.dcfclk_khz = dml_round_up(min_dcfclk_bw > min_dcfclk_latency ? min_dcfclk_bw : min_dcfclk_latency);

	/* assumes DF throttling is disabled */
	min_uclk_avg = dram_bw_kbps_to_uclk_khz(mode_support_result->global.svp_prefetch.average_bw_dram_kbps, &in_out->soc_bb->clk_table.dram_config);
	min_uclk_avg = (double)min_uclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_average.dram_derate_percent_pixel / 100);

	min_uclk_urgent = dram_bw_kbps_to_uclk_khz(mode_support_result->global.svp_prefetch.urgent_bw_dram_kbps, &in_out->soc_bb->clk_table.dram_config);
	min_uclk_urgent = (double)min_uclk_urgent / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_urgent.dram_derate_percent_pixel / 100);

	min_uclk_bw = min_uclk_urgent > min_uclk_avg ? min_uclk_urgent : min_uclk_avg;

	min_fclk_avg = (double)mode_support_result->global.svp_prefetch.average_bw_sdp_kbps / in_out->soc_bb->fabric_datapath_to_dcn_data_return_bytes;
	min_fclk_avg = (double)min_fclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_average.fclk_derate_percent / 100);

	min_fclk_urgent = (double)mode_support_result->global.svp_prefetch.urgent_bw_sdp_kbps / in_out->soc_bb->fabric_datapath_to_dcn_data_return_bytes;
	min_fclk_urgent = (double)min_fclk_urgent / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_urgent.fclk_derate_percent / 100);

	min_fclk_bw = min_fclk_urgent > min_fclk_avg ? min_fclk_urgent : min_fclk_avg;

	min_dcfclk_avg = (double)mode_support_result->global.svp_prefetch.average_bw_sdp_kbps / in_out->soc_bb->return_bus_width_bytes;
	min_dcfclk_avg = (double)min_dcfclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_average.dcfclk_derate_percent / 100);

	min_dcfclk_urgent = (double)mode_support_result->global.svp_prefetch.urgent_bw_sdp_kbps / in_out->soc_bb->return_bus_width_bytes;
	min_dcfclk_urgent = (double)min_dcfclk_urgent / ((double)in_out->soc_bb->qos_parameters.derate_table.system_active_urgent.dcfclk_derate_percent / 100);

	min_dcfclk_bw = min_dcfclk_urgent > min_dcfclk_avg ? min_dcfclk_urgent : min_dcfclk_avg;

	get_minimum_clocks_for_latency(in_out, &min_uclk_latency, &min_fclk_latency, &min_dcfclk_latency);

	in_out->programming->min_clocks.dcn4x.svp_prefetch_no_throttle.uclk_khz = dml_round_up(min_uclk_bw > min_uclk_latency ? min_uclk_bw : min_uclk_latency);
	in_out->programming->min_clocks.dcn4x.svp_prefetch_no_throttle.fclk_khz = dml_round_up(min_fclk_bw > min_fclk_latency ? min_fclk_bw : min_fclk_latency);
	in_out->programming->min_clocks.dcn4x.svp_prefetch_no_throttle.dcfclk_khz = dml_round_up(min_dcfclk_bw > min_dcfclk_latency ? min_dcfclk_bw : min_dcfclk_latency);
}

static void calculate_idle_minimums(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	double min_uclk_avg;
	double min_fclk_avg;
	double min_dcfclk_avg;
	double min_uclk_latency, min_fclk_latency, min_dcfclk_latency;
	const struct dml2_core_mode_support_result *mode_support_result = &in_out->display_cfg->mode_support_result;

	min_uclk_avg = dram_bw_kbps_to_uclk_khz(mode_support_result->global.active.average_bw_dram_kbps, &in_out->soc_bb->clk_table.dram_config);
	min_uclk_avg = (double)min_uclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.system_idle_average.dram_derate_percent_pixel / 100);

	min_fclk_avg = (double)mode_support_result->global.active.average_bw_sdp_kbps / in_out->soc_bb->fabric_datapath_to_dcn_data_return_bytes;
	min_fclk_avg = (double)min_fclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.system_idle_average.fclk_derate_percent / 100);

	min_dcfclk_avg = (double)mode_support_result->global.active.average_bw_sdp_kbps / in_out->soc_bb->return_bus_width_bytes;
	min_dcfclk_avg = (double)min_dcfclk_avg / ((double)in_out->soc_bb->qos_parameters.derate_table.system_idle_average.dcfclk_derate_percent / 100);

	get_minimum_clocks_for_latency(in_out, &min_uclk_latency, &min_fclk_latency, &min_dcfclk_latency);

	in_out->programming->min_clocks.dcn4x.idle.uclk_khz = dml_round_up(min_uclk_avg > min_uclk_latency ? min_uclk_avg : min_uclk_latency);
	in_out->programming->min_clocks.dcn4x.idle.fclk_khz = dml_round_up(min_fclk_avg > min_fclk_latency ? min_fclk_avg : min_fclk_latency);
	in_out->programming->min_clocks.dcn4x.idle.dcfclk_khz = dml_round_up(min_dcfclk_avg > min_dcfclk_latency ? min_dcfclk_avg : min_dcfclk_latency);
}

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

	divider = (unsigned int)(DFS_DIVIDER_RANGE_SCALE_FACTOR * (vco_freq_khz / clock_khz));

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

static bool round_up_and_copy_to_next_dpm(unsigned long min_value, unsigned long *rounded_value, const struct dml2_clk_table *clock_table)
{
	bool result = false;
	int index = 0;

	if (clock_table->num_clk_values > 2) {
		while (index < clock_table->num_clk_values && clock_table->clk_values_khz[index] < min_value)
			index++;

		if (index < clock_table->num_clk_values) {
			*rounded_value = clock_table->clk_values_khz[index];
			result = true;
		}
	} else if (clock_table->clk_values_khz[clock_table->num_clk_values - 1] >= min_value) {
		*rounded_value = min_value;
		result = true;
	}
	return result;
}

static bool round_up_to_next_dpm(unsigned long *clock_value, const struct dml2_clk_table *clock_table)
{
	return round_up_and_copy_to_next_dpm(*clock_value, clock_value, clock_table);
}

static bool map_soc_min_clocks_to_dpm_fine_grained(struct dml2_display_cfg_programming *display_cfg, const struct dml2_soc_state_table *state_table)
{
	bool result;

	result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.active.dcfclk_khz, &state_table->dcfclk);
	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.active.fclk_khz, &state_table->fclk);
	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.active.uclk_khz, &state_table->uclk);

	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.svp_prefetch.dcfclk_khz, &state_table->dcfclk);
	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.svp_prefetch.fclk_khz, &state_table->fclk);
	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.svp_prefetch.uclk_khz, &state_table->uclk);

	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.idle.dcfclk_khz, &state_table->dcfclk);
	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.idle.fclk_khz, &state_table->fclk);
	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.idle.uclk_khz, &state_table->uclk);

	/* these clocks are optional, so they can fail to map, in which case map all to 0 */
	if (result) {
		if (!round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.svp_prefetch_no_throttle.dcfclk_khz, &state_table->dcfclk) ||
				!round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.svp_prefetch_no_throttle.fclk_khz, &state_table->fclk) ||
				!round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.svp_prefetch_no_throttle.uclk_khz, &state_table->uclk)) {
			display_cfg->min_clocks.dcn4x.svp_prefetch_no_throttle.dcfclk_khz = 0;
			display_cfg->min_clocks.dcn4x.svp_prefetch_no_throttle.fclk_khz = 0;
			display_cfg->min_clocks.dcn4x.svp_prefetch_no_throttle.uclk_khz = 0;
		}
	}

	return result;
}

static bool map_soc_min_clocks_to_dpm_coarse_grained(struct dml2_display_cfg_programming *display_cfg, const struct dml2_soc_state_table *state_table)
{
	bool result;
	int index;

	result = false;
	for (index = 0; index < state_table->uclk.num_clk_values; index++) {
		if (display_cfg->min_clocks.dcn4x.active.dcfclk_khz <= state_table->dcfclk.clk_values_khz[index] &&
			display_cfg->min_clocks.dcn4x.active.fclk_khz <= state_table->fclk.clk_values_khz[index] &&
			display_cfg->min_clocks.dcn4x.active.uclk_khz <= state_table->uclk.clk_values_khz[index]) {
			display_cfg->min_clocks.dcn4x.active.dcfclk_khz = state_table->dcfclk.clk_values_khz[index];
			display_cfg->min_clocks.dcn4x.active.fclk_khz = state_table->fclk.clk_values_khz[index];
			display_cfg->min_clocks.dcn4x.active.uclk_khz = state_table->uclk.clk_values_khz[index];
			result = true;
			break;
		}
	}

	if (result) {
		result = false;
		for (index = 0; index < state_table->uclk.num_clk_values; index++) {
			if (display_cfg->min_clocks.dcn4x.idle.dcfclk_khz <= state_table->dcfclk.clk_values_khz[index] &&
				display_cfg->min_clocks.dcn4x.idle.fclk_khz <= state_table->fclk.clk_values_khz[index] &&
				display_cfg->min_clocks.dcn4x.idle.uclk_khz <= state_table->uclk.clk_values_khz[index]) {
				display_cfg->min_clocks.dcn4x.idle.dcfclk_khz = state_table->dcfclk.clk_values_khz[index];
				display_cfg->min_clocks.dcn4x.idle.fclk_khz = state_table->fclk.clk_values_khz[index];
				display_cfg->min_clocks.dcn4x.idle.uclk_khz = state_table->uclk.clk_values_khz[index];
				result = true;
				break;
			}
		}
	}

	// SVP is not supported on any coarse grained SoCs
	display_cfg->min_clocks.dcn4x.svp_prefetch.dcfclk_khz = 0;
	display_cfg->min_clocks.dcn4x.svp_prefetch.fclk_khz = 0;
	display_cfg->min_clocks.dcn4x.svp_prefetch.uclk_khz = 0;

	return result;
}

static bool map_min_clocks_to_dpm(const struct dml2_core_mode_support_result *mode_support_result, struct dml2_display_cfg_programming *display_cfg, const struct dml2_soc_state_table *state_table)
{
	bool result = false;
	bool dcfclk_fine_grained = false, fclk_fine_grained = false, clock_state_count_identical = false;
	unsigned int i;

	if (!state_table || !display_cfg)
		return false;

	if (state_table->dcfclk.num_clk_values == 2) {
		dcfclk_fine_grained = true;
	}

	if (state_table->fclk.num_clk_values == 2) {
		fclk_fine_grained = true;
	}

	if (state_table->fclk.num_clk_values == state_table->dcfclk.num_clk_values &&
		state_table->fclk.num_clk_values == state_table->uclk.num_clk_values) {
		clock_state_count_identical = true;
	}

	if (dcfclk_fine_grained || fclk_fine_grained || !clock_state_count_identical)
		result = map_soc_min_clocks_to_dpm_fine_grained(display_cfg, state_table);
	else
		result = map_soc_min_clocks_to_dpm_coarse_grained(display_cfg, state_table);

	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.dispclk_khz, &state_table->dispclk);

	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.deepsleep_dcfclk_khz, &state_table->dcfclk);

	for (i = 0; i < DML2_MAX_DCN_PIPES; i++) {
		if (result)
			result = round_up_to_next_dpm(&display_cfg->plane_programming[i].min_clocks.dcn4x.dppclk_khz, &state_table->dppclk);
	}

	for (i = 0; i < display_cfg->display_config.num_streams; i++) {
		if (result)
			result = round_up_and_copy_to_next_dpm(mode_support_result->per_stream[i].dscclk_khz, &display_cfg->stream_programming[i].min_clocks.dcn4x.dscclk_khz, &state_table->dscclk);
		if (result)
			result = round_up_and_copy_to_next_dpm(mode_support_result->per_stream[i].dtbclk_khz, &display_cfg->stream_programming[i].min_clocks.dcn4x.dtbclk_khz, &state_table->dtbclk);
		if (result)
			result = round_up_and_copy_to_next_dpm(mode_support_result->per_stream[i].phyclk_khz, &display_cfg->stream_programming[i].min_clocks.dcn4x.phyclk_khz, &state_table->phyclk);
	}

	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.dpprefclk_khz, &state_table->dppclk);

	if (result)
		result = round_up_to_next_dpm(&display_cfg->min_clocks.dcn4x.dtbrefclk_khz, &state_table->dtbclk);

	return result;
}

static bool are_timings_trivially_synchronizable(struct dml2_display_cfg *display_config, int mask)
{
	unsigned char i;
	bool identical = true;
	bool contains_drr = false;
	unsigned char remap_array[DML2_MAX_PLANES];
	unsigned char remap_array_size = 0;

	// Create a remap array to enable simple iteration through only masked stream indicies
	for (i = 0; i < display_config->num_streams; i++) {
		if (mask & (0x1 << i)) {
			remap_array[remap_array_size++] = i;
		}
	}

	// 0 or 1 display is always trivially synchronizable
	if (remap_array_size <= 1)
		return true;

	// Check that all displays timings are the same
	for (i = 1; i < remap_array_size; i++) {
		if (memcmp(&display_config->stream_descriptors[remap_array[i - 1]].timing, &display_config->stream_descriptors[remap_array[i]].timing, sizeof(struct dml2_timing_cfg))) {
			identical = false;
			break;
		}
	}

	// Check if any displays are drr
	for (i = 0; i < remap_array_size; i++) {
		if (display_config->stream_descriptors[remap_array[i]].timing.drr_config.enabled) {
			contains_drr = true;
			break;
		}
	}

	// Trivial sync is possible if all displays are identical and none are DRR
	return !contains_drr && identical;
}

static int find_smallest_idle_time_in_vblank_us(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out, int mask)
{
	unsigned char i;
	int min_idle_us = 0;
	unsigned char remap_array[DML2_MAX_PLANES];
	unsigned char remap_array_size = 0;
	const struct dml2_core_mode_support_result *mode_support_result = &in_out->display_cfg->mode_support_result;

	// Create a remap array to enable simple iteration through only masked stream indicies
	for (i = 0; i < in_out->programming->display_config.num_streams; i++) {
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

static bool determine_power_management_features_with_vblank_only(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	int min_idle_us;

	if (are_timings_trivially_synchronizable(&in_out->programming->display_config, 0xF)) {
		min_idle_us = find_smallest_idle_time_in_vblank_us(in_out, 0xF);

		if (min_idle_us >= in_out->soc_bb->power_management_parameters.dram_clk_change_blackout_us)
			in_out->programming->uclk_pstate_supported = true;

		if (min_idle_us >= in_out->soc_bb->power_management_parameters.fclk_change_blackout_us)
			in_out->programming->fclk_pstate_supported = true;
	}

	return true;
}

static int get_displays_without_vactive_margin_mask(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out, int latency_hiding_requirement_us)
{
	unsigned int i;
	int displays_without_vactive_margin_mask = 0x0;
	const struct dml2_core_mode_support_result *mode_support_result = &in_out->display_cfg->mode_support_result;

	for (i = 0; i < in_out->programming->display_config.num_planes; i++) {
		if (mode_support_result->cfg_support_info.plane_support_info[i].active_latency_hiding_us
			< latency_hiding_requirement_us)
			displays_without_vactive_margin_mask |= (0x1 << i);
	}

	return displays_without_vactive_margin_mask;
}

static int get_displays_with_fams_mask(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out, int latency_hiding_requirement_us)
{
	unsigned int i;
	int displays_with_fams_mask = 0x0;

	for (i = 0; i < in_out->programming->display_config.num_planes; i++) {
		if (in_out->programming->display_config.plane_descriptors->overrides.legacy_svp_config != dml2_svp_mode_override_auto)
			displays_with_fams_mask |= (0x1 << i);
	}

	return displays_with_fams_mask;
}

static bool determine_power_management_features_with_vactive_and_vblank(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	int displays_without_vactive_margin_mask = 0x0;
	int min_idle_us = 0;

	if (in_out->programming->uclk_pstate_supported == false) {
		displays_without_vactive_margin_mask =
			get_displays_without_vactive_margin_mask(in_out, (int)(in_out->soc_bb->power_management_parameters.dram_clk_change_blackout_us));

		if (are_timings_trivially_synchronizable(&in_out->programming->display_config, displays_without_vactive_margin_mask)) {
			min_idle_us = find_smallest_idle_time_in_vblank_us(in_out, displays_without_vactive_margin_mask);

			if (min_idle_us >= in_out->soc_bb->power_management_parameters.dram_clk_change_blackout_us)
				in_out->programming->uclk_pstate_supported = true;
		}
	}

	if (in_out->programming->fclk_pstate_supported == false) {
		displays_without_vactive_margin_mask =
			get_displays_without_vactive_margin_mask(in_out, (int)(in_out->soc_bb->power_management_parameters.fclk_change_blackout_us));

		if (are_timings_trivially_synchronizable(&in_out->programming->display_config, displays_without_vactive_margin_mask)) {
			min_idle_us = find_smallest_idle_time_in_vblank_us(in_out, displays_without_vactive_margin_mask);

			if (min_idle_us >= in_out->soc_bb->power_management_parameters.fclk_change_blackout_us)
				in_out->programming->fclk_pstate_supported = true;
		}
	}

	return true;
}

static bool determine_power_management_features_with_fams(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	int displays_without_vactive_margin_mask = 0x0;
	int displays_without_fams_mask = 0x0;

	displays_without_vactive_margin_mask =
		get_displays_without_vactive_margin_mask(in_out, (int)(in_out->soc_bb->power_management_parameters.dram_clk_change_blackout_us));

	displays_without_fams_mask =
		get_displays_with_fams_mask(in_out, (int)(in_out->soc_bb->power_management_parameters.dram_clk_change_blackout_us));

	if ((displays_without_vactive_margin_mask & ~displays_without_fams_mask) == 0)
		in_out->programming->uclk_pstate_supported = true;

	return true;
}

static void clamp_uclk_to_max(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	in_out->programming->min_clocks.dcn4x.active.uclk_khz = in_out->soc_bb->clk_table.uclk.clk_values_khz[in_out->soc_bb->clk_table.uclk.num_clk_values - 1];
	in_out->programming->min_clocks.dcn4x.svp_prefetch.uclk_khz = in_out->soc_bb->clk_table.uclk.clk_values_khz[in_out->soc_bb->clk_table.uclk.num_clk_values - 1];
	in_out->programming->min_clocks.dcn4x.idle.uclk_khz = in_out->soc_bb->clk_table.uclk.clk_values_khz[in_out->soc_bb->clk_table.uclk.num_clk_values - 1];
}

static void clamp_fclk_to_max(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	in_out->programming->min_clocks.dcn4x.active.fclk_khz = in_out->soc_bb->clk_table.fclk.clk_values_khz[in_out->soc_bb->clk_table.fclk.num_clk_values - 1];
	in_out->programming->min_clocks.dcn4x.idle.fclk_khz = in_out->soc_bb->clk_table.fclk.clk_values_khz[in_out->soc_bb->clk_table.fclk.num_clk_values - 1];
}

static bool map_mode_to_soc_dpm(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	int i;
	bool result;
	double dispclk_khz;
	const struct dml2_core_mode_support_result *mode_support_result = &in_out->display_cfg->mode_support_result;

	calculate_system_active_minimums(in_out);
	calculate_svp_prefetch_minimums(in_out);
	calculate_idle_minimums(in_out);

	// In NV4, there's no support for FCLK or DCFCLK DPM change before SVP prefetch starts, therefore
	// active minimums must be boosted to prefetch minimums
	if (in_out->programming->min_clocks.dcn4x.svp_prefetch.uclk_khz > in_out->programming->min_clocks.dcn4x.active.uclk_khz)
		in_out->programming->min_clocks.dcn4x.active.uclk_khz = in_out->programming->min_clocks.dcn4x.svp_prefetch.uclk_khz;

	if (in_out->programming->min_clocks.dcn4x.svp_prefetch.fclk_khz > in_out->programming->min_clocks.dcn4x.active.fclk_khz)
		in_out->programming->min_clocks.dcn4x.active.fclk_khz = in_out->programming->min_clocks.dcn4x.svp_prefetch.fclk_khz;

	if (in_out->programming->min_clocks.dcn4x.svp_prefetch.dcfclk_khz > in_out->programming->min_clocks.dcn4x.active.dcfclk_khz)
		in_out->programming->min_clocks.dcn4x.active.dcfclk_khz = in_out->programming->min_clocks.dcn4x.svp_prefetch.dcfclk_khz;

	// need some massaging for the dispclk ramping cases:
	dispclk_khz = mode_support_result->global.dispclk_khz * (1 + in_out->soc_bb->dcn_downspread_percent / 100.0) * (1.0 + in_out->ip->dispclk_ramp_margin_percent / 100.0);
	// ramping margin should not make dispclk exceed the maximum dispclk speed:
	dispclk_khz = math_min2(dispclk_khz, in_out->min_clk_table->max_clocks_khz.dispclk);
	// but still the required dispclk can be more than the maximum dispclk speed:
	dispclk_khz = math_max2(dispclk_khz, mode_support_result->global.dispclk_khz * (1 + in_out->soc_bb->dcn_downspread_percent / 100.0));

	// DPP Ref is always set to max of all DPP clocks
	for (i = 0; i < DML2_MAX_DCN_PIPES; i++) {
		if (in_out->programming->min_clocks.dcn4x.dpprefclk_khz < mode_support_result->per_plane[i].dppclk_khz)
			in_out->programming->min_clocks.dcn4x.dpprefclk_khz = mode_support_result->per_plane[i].dppclk_khz;
	}
	in_out->programming->min_clocks.dcn4x.dpprefclk_khz = (unsigned long) (in_out->programming->min_clocks.dcn4x.dpprefclk_khz * (1 + in_out->soc_bb->dcn_downspread_percent / 100.0));

	// DTB Ref is always set to max of all DTB clocks
	for (i = 0; i < DML2_MAX_DCN_PIPES; i++) {
		if (in_out->programming->min_clocks.dcn4x.dtbrefclk_khz < mode_support_result->per_stream[i].dtbclk_khz)
			in_out->programming->min_clocks.dcn4x.dtbrefclk_khz = mode_support_result->per_stream[i].dtbclk_khz;
	}
	in_out->programming->min_clocks.dcn4x.dtbrefclk_khz = (unsigned long)(in_out->programming->min_clocks.dcn4x.dtbrefclk_khz * (1 + in_out->soc_bb->dcn_downspread_percent / 100.0));

	if (in_out->soc_bb->no_dfs) {
		round_to_non_dfs_granularity((unsigned long)dispclk_khz, in_out->programming->min_clocks.dcn4x.dpprefclk_khz, in_out->programming->min_clocks.dcn4x.dtbrefclk_khz,
			&in_out->programming->min_clocks.dcn4x.dispclk_khz, &in_out->programming->min_clocks.dcn4x.dpprefclk_khz, &in_out->programming->min_clocks.dcn4x.dtbrefclk_khz);
	} else {
		add_margin_and_round_to_dfs_grainularity(dispclk_khz, 0.0,
			(unsigned long)(in_out->soc_bb->dispclk_dppclk_vco_speed_mhz * 1000), &in_out->programming->min_clocks.dcn4x.dispclk_khz, &in_out->programming->min_clocks.dcn4x.divider_ids.dispclk_did);

		add_margin_and_round_to_dfs_grainularity(in_out->programming->min_clocks.dcn4x.dpprefclk_khz, 0.0,
			(unsigned long)(in_out->soc_bb->dispclk_dppclk_vco_speed_mhz * 1000), &in_out->programming->min_clocks.dcn4x.dpprefclk_khz, &in_out->programming->min_clocks.dcn4x.divider_ids.dpprefclk_did);

		add_margin_and_round_to_dfs_grainularity(in_out->programming->min_clocks.dcn4x.dtbrefclk_khz, 0.0,
			(unsigned long)(in_out->soc_bb->dispclk_dppclk_vco_speed_mhz * 1000), &in_out->programming->min_clocks.dcn4x.dtbrefclk_khz, &in_out->programming->min_clocks.dcn4x.divider_ids.dtbrefclk_did);
	}


	for (i = 0; i < DML2_MAX_DCN_PIPES; i++) {
		in_out->programming->plane_programming[i].min_clocks.dcn4x.dppclk_khz = (unsigned long)(in_out->programming->min_clocks.dcn4x.dpprefclk_khz / 255.0
			* math_ceil2(in_out->display_cfg->mode_support_result.per_plane[i].dppclk_khz * (1.0 + in_out->soc_bb->dcn_downspread_percent / 100.0) * 255.0 / in_out->programming->min_clocks.dcn4x.dpprefclk_khz, 1.0));
	}

	in_out->programming->min_clocks.dcn4x.deepsleep_dcfclk_khz = mode_support_result->global.dcfclk_deepsleep_khz;
	in_out->programming->min_clocks.dcn4x.socclk_khz = mode_support_result->global.socclk_khz;

	result = map_min_clocks_to_dpm(mode_support_result, in_out->programming, &in_out->soc_bb->clk_table);

	// By default, all power management features are not enabled
	in_out->programming->fclk_pstate_supported = false;
	in_out->programming->uclk_pstate_supported = false;

	return result;
}

bool dpmm_dcn3_map_mode_to_soc_dpm(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	bool result;

	result = map_mode_to_soc_dpm(in_out);

	// Check if any can be enabled by nominal vblank idle time
	determine_power_management_features_with_vblank_only(in_out);

	// Check if any can be enabled in vactive/vblank
	determine_power_management_features_with_vactive_and_vblank(in_out);

	// Check if any can be enabled via fams
	determine_power_management_features_with_fams(in_out);

	if (in_out->programming->uclk_pstate_supported == false)
		clamp_uclk_to_max(in_out);

	if (in_out->programming->fclk_pstate_supported == false)
		clamp_fclk_to_max(in_out);

	return result;
}

bool dpmm_dcn4_map_mode_to_soc_dpm(struct dml2_dpmm_map_mode_to_soc_dpm_params_in_out *in_out)
{
	bool result;
	int displays_without_vactive_margin_mask = 0x0;
	int min_idle_us = 0;

	result = map_mode_to_soc_dpm(in_out);

	if (in_out->display_cfg->stage3.success)
		in_out->programming->uclk_pstate_supported = true;

	displays_without_vactive_margin_mask =
		get_displays_without_vactive_margin_mask(in_out, (int)(in_out->soc_bb->power_management_parameters.fclk_change_blackout_us));

	if (displays_without_vactive_margin_mask == 0) {
		in_out->programming->fclk_pstate_supported = true;
	} else {
		if (are_timings_trivially_synchronizable(&in_out->programming->display_config, displays_without_vactive_margin_mask)) {
			min_idle_us = find_smallest_idle_time_in_vblank_us(in_out, displays_without_vactive_margin_mask);

			if (min_idle_us >= in_out->soc_bb->power_management_parameters.fclk_change_blackout_us)
				in_out->programming->fclk_pstate_supported = true;
		}
	}

	if (in_out->programming->uclk_pstate_supported == false)
		clamp_uclk_to_max(in_out);

	if (in_out->programming->fclk_pstate_supported == false)
		clamp_fclk_to_max(in_out);

	min_idle_us = find_smallest_idle_time_in_vblank_us(in_out, 0xFF);
	if (in_out->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us > 0 &&
		min_idle_us >= in_out->soc_bb->power_management_parameters.stutter_enter_plus_exit_latency_us)
		in_out->programming->stutter.supported_in_blank = true;
	else
		in_out->programming->stutter.supported_in_blank = false;

	// TODO: Fix me Sam
	if (in_out->soc_bb->power_management_parameters.z8_min_idle_time > 0 &&
		in_out->programming->informative.power_management.z8.stutter_period >= in_out->soc_bb->power_management_parameters.z8_min_idle_time)
		in_out->programming->z8_stutter.meets_eco = true;
	else
		in_out->programming->z8_stutter.meets_eco = false;

	if (in_out->soc_bb->power_management_parameters.z8_stutter_exit_latency_us > 0 &&
		min_idle_us >= in_out->soc_bb->power_management_parameters.z8_stutter_exit_latency_us)
		in_out->programming->z8_stutter.supported_in_blank = true;
	else
		in_out->programming->z8_stutter.supported_in_blank = false;

	return result;
}

bool dpmm_dcn4_map_watermarks(struct dml2_dpmm_map_watermarks_params_in_out *in_out)
{
	const struct dml2_display_cfg *display_cfg = &in_out->display_cfg->display_config;
	const struct dml2_core_internal_display_mode_lib *mode_lib = &in_out->core->clean_me_up.mode_lib;
	struct dml2_dchub_global_register_set *dchubbub_regs = &in_out->programming->global_regs;

	double refclk_freq_in_mhz = (display_cfg->overrides.hw.dlg_ref_clk_mhz > 0) ? (double)display_cfg->overrides.hw.dlg_ref_clk_mhz : mode_lib->soc.dchub_refclk_mhz;

	/* set A */
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].fclk_pstate = (int unsigned)(mode_lib->mp.Watermark.FCLKChangeWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].sr_enter = (int unsigned)(mode_lib->mp.Watermark.StutterEnterPlusExitWatermark * refclk_freq_in_mhz);
	dchubbub_regs->wm_regs[DML2_DCHUB_WATERMARK_SET_A].sr_exit = (int unsigned)(mode_lib->mp.Watermark.StutterExitWatermark * refclk_freq_in_mhz);
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

	return true;
}
