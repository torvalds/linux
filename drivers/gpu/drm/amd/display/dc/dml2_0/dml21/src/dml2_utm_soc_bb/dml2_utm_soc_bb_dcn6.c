// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dml2_utm_soc_bb_dcn6.h"
#include "bounding_boxes/dcn6_soc_bb.h"
#include "lib_float_math.h"
#include "dml2_debug.h"

static unsigned int dcn6_sop_table_get_highest_index(const struct dml2_sop_table *table)
{
	return table->model->sop_count - 1;
}

static void dcn6_sop_table_get_sop_constraint_at_index(const struct dml2_sop_table *table,
		unsigned int index, struct dml2_sop_constraint *constraint)
{
	const struct utm_qos_model *model = table->model;
	const struct utm_qos_model_dchub_v2 *dchub = model->dchub_v2;

	DML_ASSERT_MSG(index < model->sop_count, "unsupported sop index\n");
	constraint->dcn5.clocks.fclk_khz = model->sops[index].fclk_khz;
	constraint->dcn5.clocks.uclk_khz = model->sops[index].uclk_khz;
	constraint->dcn5.clocks.dcfclk_khz = table->sop_optimal_dcfclks_khz[index];

	constraint->dcn5.latency.dcn5.urgent_ramp = dchub->latencies[index].urgent_ramp_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.t_trip = dchub->latencies[index].t_trip_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.meta_trip_to_mem = dchub->latencies[index].meta_trip_to_mem_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.max_req_latency_urg = dchub->latencies[index].max_req_latency_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.avg_req_latency_urg = dchub->latencies[index].avg_req_latency_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.max_req_latency_non_urg = dchub->latencies[index].max_req_latency_non_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.avg_req_latency_non_urg = dchub->latencies[index].avg_req_latency_non_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.df_response_time_us = dchub->latencies[index].df_response_time_ps / 1000000.0;
	constraint->dcn5.min_available_urgent_bandwidth_KBps = table->sop_min_available_urgent_bandwidths_KBps[index];
	constraint->dcn5.min_sop_index = index;
}

static bool dcn6_sop_table_is_bandwidth_supported_at_index(
		const struct dml2_sop_table *table, const struct dml2_memory_path_bandwidth *bw, unsigned int index)
{
	const struct utm_qos_model *model = table->model;
	struct utm_qos_model_dchub_memory_path_bandwidth_v2 qos_bandwidth;
	bool result = true;
	unsigned int highest_sop_index = table->model->sop_count - 1;

	qos_bandwidth.nominal_bandwidth_KBps = (uint32_t) bw->dcn5.non_urgent_bandwidth_kbps;
	qos_bandwidth.urgent_bandwidth_KBps = (uint32_t) bw->dcn5.urgent_bandwidth_kbps;

	/* check if the bandwidth is supported by current sop at idle */
	if (!dchub_v2_is_qos_bandwidth_supported_by_sop(table->model, &qos_bandwidth, (uint8_t) index,
			model->dchub_v2->max_nominal_utm_budget_percent, model->dchub_v2->max_urgent_utm_budget_percent))
		result = false;

	/* check if the bandwidth is supported by the highest sop at active */
	if (!dchub_v2_is_qos_bandwidth_supported_by_sop(table->model, &qos_bandwidth, (uint8_t) highest_sop_index,
			model->dchub_v2->min_nominal_utm_budget_percent, model->dchub_v2->min_urgent_utm_budget_percent))
		result = false;

	return result;
}

static void dcn6_sop_table_get_max_sop(const struct dml2_sop_table *table, struct dml2_soc_operating_point *sop)
{
	const struct utm_qos_model *model = table->model;

	DML_ASSERT_MSG(model->sop_count > 0, "utm_qos_model must contain at least 1 sop\n");
	if (model->sop_count > 0) {
		sop->fclk_khz = model->sops[model->sop_count-1].fclk_khz;
		sop->uclk_khz = model->sops[model->sop_count-1].uclk_khz;
		sop->dcfclk_khz = table->sop_optimal_dcfclks_khz[model->sop_count - 1];
	}

}

static void dcn6_sop_table_get_min_sop(const struct dml2_sop_table *table, struct dml2_soc_operating_point *sop)
{
	const struct utm_qos_model *model = table->model;

	sop->fclk_khz = model->sops[0].fclk_khz;
	sop->uclk_khz = model->sops[0].uclk_khz;
	sop->dcfclk_khz = table->sop_optimal_dcfclks_khz[0];
}

#define SDP_DERATE_PERCENT_NOMINAL 76.0
#define SDP_DERATE_PERCENT_URGENT 100.0

static uint32_t dcn6_sop_table_calculate_optimal_dcfclk_khz(
		const struct dml2_sop_table *table,
		const struct dml2_utm_soc_bb *utm_soc_bb,
		struct utm_qos_model_dchub_memory_path_bandwidth_v2 *total_available_bandwidth)
{
	const struct utm_qos_model *model = table->model;
	double nominal_dcfclk_khz;
	double urgent_dcfclk_khz;
	uint32_t sop_optimal_dcfclk_khz;

	/* calculate the optimal dcfclk based on the available bandwidth */
	nominal_dcfclk_khz = total_available_bandwidth->nominal_bandwidth_KBps
			* (model->dchub_v2->max_nominal_utm_budget_percent / 100.0)
			/ (SDP_DERATE_PERCENT_NOMINAL / 100.0)
			/ utm_soc_bb->return_bus_width_bytes;
	urgent_dcfclk_khz = total_available_bandwidth->urgent_bandwidth_KBps
			* (model->dchub_v2->max_urgent_utm_budget_percent / 100.0)
			/ (SDP_DERATE_PERCENT_URGENT / 100.0)
			/ utm_soc_bb->return_bus_width_bytes;
	sop_optimal_dcfclk_khz = (uint32_t)math_ceil(math_max2(urgent_dcfclk_khz, nominal_dcfclk_khz));

	/* ensure the calculated dcfclk is within dcfclk limits */
	if (sop_optimal_dcfclk_khz > utm_soc_bb->max_dcfclk_khz)
		sop_optimal_dcfclk_khz = utm_soc_bb->max_dcfclk_khz;
	else if (sop_optimal_dcfclk_khz < utm_soc_bb->min_dcfclk_khz)
		sop_optimal_dcfclk_khz = utm_soc_bb->min_dcfclk_khz;

	return sop_optimal_dcfclk_khz;
}

static void dml2_utm_soc_bb_dcn6_build_sop_table(struct dml2_sop_table *table,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	unsigned int i;
	struct utm_qos_model_dchub_memory_path_bandwidth_v2 total_available_bandwidth;

	table->get_highest_sop_index = dcn6_sop_table_get_highest_index;
	table->get_sop_constraint_at_index = dcn6_sop_table_get_sop_constraint_at_index;
	table->is_bw_supported_at_index = dcn6_sop_table_is_bandwidth_supported_at_index;
	table->get_max_sop = dcn6_sop_table_get_max_sop;
	table->get_min_sop = dcn6_sop_table_get_min_sop;
	table->model = &utm_soc_bb->qos_model;

	for (i = 0; i < table->model->sop_count; i++) {
		dchub_v2_get_sop_total_available_bandwidth_KBps(table->model, &total_available_bandwidth, (uint8_t) i);
		table->sop_optimal_dcfclks_khz[i] =
				dcn6_sop_table_calculate_optimal_dcfclk_khz(table, utm_soc_bb, &total_available_bandwidth);
		table->sop_min_available_urgent_bandwidths_KBps[i] = (uint32_t) math_floor(
				total_available_bandwidth.urgent_bandwidth_KBps
				* (utm_soc_bb->qos_model.dchub_v2->min_urgent_utm_budget_percent / 100.0));
	}

	DML_ASSERT_MSG(table->model->sop_count > 0, "qos_model must contain at least 1 sop\n");
}

/*
 * v3 SOP table functions — flat pre-computed bandwidth/latency model
 */

static unsigned int dcn6_v3_sop_table_get_highest_index(const struct dml2_sop_table *table)
{
	return table->model->dchub_v3->sop_count - 1;
}

static void dcn6_v3_sop_table_get_sop_constraint_at_index(const struct dml2_sop_table *table,
		unsigned int index, struct dml2_sop_constraint *constraint)
{
	const struct utm_qos_model_dchub_v3 *dchub = table->model->dchub_v3;
	const struct utm_qos_model_dchub_v3_sop_entry *entry =
			&dchub->sops[UTM_QOS_MODEL_V3_LOAD_LEVEL_ACTIVE_ALTERNATE_PSTATE][index];

	DML_ASSERT_MSG(index < dchub->sop_count, "unsupported sop index\n");
	constraint->dcn5.clocks.fclk_khz = 0;
	constraint->dcn5.clocks.uclk_khz = 0;
	constraint->dcn5.clocks.dcfclk_khz = table->sop_optimal_dcfclks_khz[index];

	constraint->dcn5.latency.dcn5.urgent_ramp = entry->urgent_ramp_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.t_trip = entry->t_trip_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.meta_trip_to_mem = entry->meta_trip_to_mem_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.max_req_latency_urg = entry->max_req_latency_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.avg_req_latency_urg = entry->avg_req_latency_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.max_req_latency_non_urg = entry->max_req_latency_non_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.avg_req_latency_non_urg = entry->avg_req_latency_non_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.df_response_time_us = entry->df_response_time_ps / 1000000.0;
	constraint->dcn5.min_available_urgent_bandwidth_KBps = table->sop_min_available_urgent_bandwidths_KBps[index];
	constraint->dcn5.min_sop_index = index;
}

static bool dcn6_v3_sop_table_is_bandwidth_supported_at_index(
		const struct dml2_sop_table *table, const struct dml2_memory_path_bandwidth *bw, unsigned int index)
{
	const struct utm_qos_model_dchub_v3 *dchub = table->model->dchub_v3;
	unsigned int highest_sop_index = dchub->sop_count - 1;
	const struct utm_qos_model_dchub_v3_sop_entry *idle_entry =
			&dchub->sops[UTM_QOS_MODEL_V3_LOAD_LEVEL_IDLE][index];
	const struct utm_qos_model_dchub_v3_sop_entry *active_entry =
			&dchub->sops[UTM_QOS_MODEL_V3_LOAD_LEVEL_ACTIVE_ALTERNATE_PSTATE][highest_sop_index];

	if (bw->dcn5.non_urgent_bandwidth_kbps > idle_entry->nominal_bandwidth_KBps
			|| bw->dcn5.urgent_bandwidth_kbps > idle_entry->urgent_bandwidth_KBps)
		return false;

	if (bw->dcn5.non_urgent_bandwidth_kbps > active_entry->nominal_bandwidth_KBps
			|| bw->dcn5.urgent_bandwidth_kbps > active_entry->urgent_bandwidth_KBps)
		return false;

	return true;
}

static void dcn6_v3_sop_table_get_max_sop(const struct dml2_sop_table *table, struct dml2_soc_operating_point *sop)
{
	const struct utm_qos_model_dchub_v3 *dchub = table->model->dchub_v3;
	const struct dml2_utm_soc_bb *utm_soc_bb =
			(const struct dml2_utm_soc_bb *)((const char *)table - offsetof(struct dml2_utm_soc_bb, sop_table));

	DML_ASSERT_MSG(dchub->sop_count > 0, "utm_qos_model must contain at least 1 sop\n");
	sop->fclk_khz = utm_soc_bb->max_fclk_khz;
	sop->uclk_khz = utm_soc_bb->max_uclk_khz;
	sop->dcfclk_khz = table->sop_optimal_dcfclks_khz[dchub->sop_count - 1];
}

static void dcn6_v3_sop_table_get_min_sop(const struct dml2_sop_table *table, struct dml2_soc_operating_point *sop)
{
	const struct dml2_utm_soc_bb *utm_soc_bb =
			(const struct dml2_utm_soc_bb *)((const char *)table - offsetof(struct dml2_utm_soc_bb, sop_table));

	sop->fclk_khz = utm_soc_bb->max_fclk_khz;
	sop->uclk_khz = utm_soc_bb->max_uclk_khz;
	sop->dcfclk_khz = table->sop_optimal_dcfclks_khz[0];
}

static uint32_t dcn6_v3_sop_table_calculate_optimal_dcfclk_khz(
		const struct dml2_utm_soc_bb *utm_soc_bb,
		const struct utm_qos_model_dchub_v3_sop_entry *idle_entry)
{
	double nominal_dcfclk_khz;
	double urgent_dcfclk_khz;
	uint32_t optimal;

	nominal_dcfclk_khz = (double)idle_entry->nominal_bandwidth_KBps
			/ (utm_soc_bb->nominal_sdp_derate_percent / 100.0)
			/ utm_soc_bb->return_bus_width_bytes;
	urgent_dcfclk_khz = (double)idle_entry->urgent_bandwidth_KBps
			/ (utm_soc_bb->urgent_sdp_derate_percent / 100.0)
			/ utm_soc_bb->return_bus_width_bytes;
	optimal = (uint32_t)math_ceil(math_max2(urgent_dcfclk_khz, nominal_dcfclk_khz));

	if (optimal > utm_soc_bb->max_dcfclk_khz)
		optimal = utm_soc_bb->max_dcfclk_khz;
	else if (optimal < utm_soc_bb->min_dcfclk_khz)
		optimal = utm_soc_bb->min_dcfclk_khz;

	return optimal;
}

static void dml2_utm_soc_bb_dcn6_v3_build_sop_table(struct dml2_sop_table *table,
		const struct dml2_utm_soc_bb *utm_soc_bb)
{
	const struct utm_qos_model_dchub_v3 *dchub = utm_soc_bb->qos_model.dchub_v3;
	unsigned int i;

	table->get_highest_sop_index = dcn6_v3_sop_table_get_highest_index;
	table->get_sop_constraint_at_index = dcn6_v3_sop_table_get_sop_constraint_at_index;
	table->is_bw_supported_at_index = dcn6_v3_sop_table_is_bandwidth_supported_at_index;
	table->get_max_sop = dcn6_v3_sop_table_get_max_sop;
	table->get_min_sop = dcn6_v3_sop_table_get_min_sop;
	table->model = &utm_soc_bb->qos_model;

	DML_ASSERT_MSG(dchub->sop_count > 0, "qos_model must contain at least 1 sop\n");

	for (i = 0; i < dchub->sop_count; i++) {
		const struct utm_qos_model_dchub_v3_sop_entry *idle_entry =
				&dchub->sops[UTM_QOS_MODEL_V3_LOAD_LEVEL_IDLE][i];
		const struct utm_qos_model_dchub_v3_sop_entry *active_entry =
				&dchub->sops[UTM_QOS_MODEL_V3_LOAD_LEVEL_ACTIVE_ALTERNATE_PSTATE][i];

		table->sop_optimal_dcfclks_khz[i] =
				dcn6_v3_sop_table_calculate_optimal_dcfclk_khz(utm_soc_bb, idle_entry);
		table->sop_min_available_urgent_bandwidths_KBps[i] =
				active_entry->urgent_bandwidth_KBps;
	}
}

static void dcn6_copy_utm_qos_model(struct utm_qos_model *dest, struct utm_qos_model_dchub_v2 *dest_dchub, const struct utm_qos_model *src)
{
	*dest = *src;
	*dest_dchub = *src->dchub_v2;
	dest->dchub_v2 = dest_dchub;
}

static void dcn6_initialize_from_soc_bb(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb)
{
	DML_ASSERT_MSG(soc_bb->clk_table.dcfclk.num_clk_values == 2, "soc_bb must provide min and max dcfclk values!\n");

	/* initialize based on soc bb */
	utm_soc_bb->max_dispclk_khz = soc_bb->clk_table.dispclk.clk_values_khz[soc_bb->clk_table.dispclk.num_clk_values - 1];
	utm_soc_bb->max_dppclk_khz = soc_bb->clk_table.dppclk.clk_values_khz[soc_bb->clk_table.dppclk.num_clk_values - 1];
	utm_soc_bb->max_dtbclk_khz = (soc_bb->clk_table.dtbclk.num_clk_values > 0) ?
		soc_bb->clk_table.dtbclk.clk_values_khz[soc_bb->clk_table.dtbclk.num_clk_values - 1] : 0;
	utm_soc_bb->max_phyclk_khz = (soc_bb->clk_table.phyclk.num_clk_values > 0) ?
		soc_bb->clk_table.phyclk.clk_values_khz[soc_bb->clk_table.phyclk.num_clk_values - 1] : 0;
	utm_soc_bb->max_dscclk_khz = (soc_bb->clk_table.dscclk.num_clk_values > 0) ?
		soc_bb->clk_table.dscclk.clk_values_khz[soc_bb->clk_table.dscclk.num_clk_values - 1] : 0;
	utm_soc_bb->max_phyclk_d18_khz = (soc_bb->clk_table.phyclk_d18.num_clk_values > 0) ?
		soc_bb->clk_table.phyclk_d18.clk_values_khz[soc_bb->clk_table.phyclk_d18.num_clk_values - 1] : 0;
	utm_soc_bb->max_phyclk_d32_khz = (soc_bb->clk_table.phyclk_d32.num_clk_values > 0) ?
		soc_bb->clk_table.phyclk_d32.clk_values_khz[soc_bb->clk_table.phyclk_d32.num_clk_values - 1] : 0;
	utm_soc_bb->min_socclk_khz = soc_bb->clk_table.socclk.clk_values_khz[0];
	utm_soc_bb->max_dcfclk_khz = soc_bb->clk_table.dcfclk.clk_values_khz[soc_bb->clk_table.dcfclk.num_clk_values - 1];
	utm_soc_bb->min_dcfclk_khz = soc_bb->clk_table.dcfclk.clk_values_khz[0];
	utm_soc_bb->max_uclk_khz = (soc_bb->clk_table.uclk.num_clk_values > 0) ?
		soc_bb->clk_table.uclk.clk_values_khz[soc_bb->clk_table.uclk.num_clk_values - 1] : 0;
	utm_soc_bb->max_fclk_khz = (soc_bb->clk_table.fclk.num_clk_values > 0) ?
		soc_bb->clk_table.fclk.clk_values_khz[soc_bb->clk_table.fclk.num_clk_values - 1] : 0;
	utm_soc_bb->dram_config.channel_width_bytes = soc_bb->clk_table.dram_config.channel_width_bytes;
	utm_soc_bb->dram_config.channel_count = soc_bb->clk_table.dram_config.channel_count;
	utm_soc_bb->dram_config.transactions_per_clock = soc_bb->clk_table.dram_config.transactions_per_clock;
	utm_soc_bb->power_management_parameters = soc_bb->power_management_parameters;
	utm_soc_bb->writeback_base_latency_us = soc_bb->qos_parameters.writeback.base_latency_us;
	utm_soc_bb->vmin_limit = soc_bb->vmin_limit;
	utm_soc_bb->dchub_refclk_mhz = soc_bb->dchub_refclk_mhz;
	utm_soc_bb->max_outstanding_reqs = soc_bb->max_outstanding_reqs;
	utm_soc_bb->return_bus_width_bytes = soc_bb->return_bus_width_bytes;
	utm_soc_bb->phy_downspread_percent = soc_bb->phy_downspread_percent;
	utm_soc_bb->dcn_downspread_percent = soc_bb->dcn_downspread_percent;
	utm_soc_bb->nominal_sdp_derate_percent = SDP_DERATE_PERCENT_NOMINAL;
	utm_soc_bb->urgent_sdp_derate_percent = SDP_DERATE_PERCENT_URGENT;
	utm_soc_bb->dispclk_dppclk_vco_speed_mhz = soc_bb->dispclk_dppclk_vco_speed_mhz;
	utm_soc_bb->no_dfs = soc_bb->no_dfs;
	utm_soc_bb->mem_word_bytes = soc_bb->mem_word_bytes;
	utm_soc_bb->num_dcc_mcaches = soc_bb->num_dcc_mcaches;
	utm_soc_bb->mcache_size_bytes = soc_bb->mcache_size_bytes;
	utm_soc_bb->mcache_line_size_bytes = soc_bb->mcache_line_size_bytes;
	utm_soc_bb->lower_bound_bandwidth_dchub = soc_bb->lower_bound_bandwidth_dchub;
	utm_soc_bb->fraction_of_urgent_bandwidth_nominal_target = soc_bb->fraction_of_urgent_bandwidth_nominal_target;
	utm_soc_bb->fraction_of_urgent_bandwidth_flip_target = soc_bb->fraction_of_urgent_bandwidth_flip_target;
}

static void dcn6_initialize_from_qos_model(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct utm_qos_model *qos_model)
{
	utm_soc_bb->dram_config.channel_width_bytes = qos_model->socbb.dram_channel_width_bytes;
	utm_soc_bb->dram_config.channel_count = qos_model->socbb.dram_channel_count;
	utm_soc_bb->dram_config.transactions_per_clock = qos_model->socbb.dram_transactions_per_clock;
}

static void dcn6a_initialize_qos_model(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct utm_qos_model *explicit_qos_model)
{
	if (explicit_qos_model)
		dcn6_copy_utm_qos_model(&utm_soc_bb->qos_model, &utm_soc_bb->qos_model_dchub_v2, explicit_qos_model);
	else
		dcn6_test_initialize_utm_qos_model(&utm_soc_bb->qos_model, &utm_soc_bb->qos_model_dchub_v2);

}

static void dcn6b_initialize_qos_model(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct utm_qos_model *explicit_qos_model)
{
	if (explicit_qos_model)
		dcn6_copy_utm_qos_model(&utm_soc_bb->qos_model, &utm_soc_bb->qos_model_dchub_v2, explicit_qos_model);
	else
		dcn6b_test_initialize_utm_qos_model(&utm_soc_bb->qos_model, &utm_soc_bb->qos_model_dchub_v2);
}

static bool dml2_utm_soc_bb_dcn6a_create_legacy(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb, const struct utm_qos_model *explicit_qos_model)
{
	dcn6_initialize_from_soc_bb(utm_soc_bb, soc_bb);
	dcn6a_initialize_qos_model(utm_soc_bb, explicit_qos_model);
	dcn6_initialize_from_qos_model(utm_soc_bb, &utm_soc_bb->qos_model);
	dml2_utm_soc_bb_dcn6_build_sop_table(&utm_soc_bb->sop_table, utm_soc_bb);

	return true;
}

static bool dml2_utm_soc_bb_dcn6b_create_legacy(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb, const struct utm_qos_model *explicit_qos_model)
{
	dcn6_initialize_from_soc_bb(utm_soc_bb, soc_bb);
	dcn6b_initialize_qos_model(utm_soc_bb, explicit_qos_model);
	dcn6_initialize_from_qos_model(utm_soc_bb, &utm_soc_bb->qos_model);
	dml2_utm_soc_bb_dcn6_build_sop_table(&utm_soc_bb->sop_table, utm_soc_bb);

	return true;
}

bool dml2_utm_soc_bb_dcn6a_create(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb, const struct utm_qos_model *explicit_qos_model)
{
	if (explicit_qos_model && explicit_qos_model->version == utm_qos_model_version_v3) {
		dcn6_initialize_from_soc_bb(utm_soc_bb, soc_bb);
		utm_soc_bb->qos_model_dchub_v3 = *explicit_qos_model->dchub_v3;
		utm_soc_bb->qos_model.version = utm_qos_model_version_v3;
		utm_soc_bb->qos_model.dchub_v3 = &utm_soc_bb->qos_model_dchub_v3;
		dml2_utm_soc_bb_dcn6_v3_build_sop_table(&utm_soc_bb->sop_table, utm_soc_bb);
	} else {
		return dml2_utm_soc_bb_dcn6a_create_legacy(utm_soc_bb, soc_bb, explicit_qos_model);
	}

	return true;
}

bool dml2_utm_soc_bb_dcn6b_create(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb, const struct utm_qos_model *explicit_qos_model)
{
	if (explicit_qos_model && explicit_qos_model->version == utm_qos_model_version_v3) {
		dcn6_initialize_from_soc_bb(utm_soc_bb, soc_bb);
		utm_soc_bb->qos_model_dchub_v3 = *explicit_qos_model->dchub_v3;
		utm_soc_bb->qos_model.version = utm_qos_model_version_v3;
		utm_soc_bb->qos_model.dchub_v3 = &utm_soc_bb->qos_model_dchub_v3;
		dml2_utm_soc_bb_dcn6_v3_build_sop_table(&utm_soc_bb->sop_table, utm_soc_bb);
	} else {
		return dml2_utm_soc_bb_dcn6b_create_legacy(utm_soc_bb, soc_bb, explicit_qos_model);
	}

	return true;
}
