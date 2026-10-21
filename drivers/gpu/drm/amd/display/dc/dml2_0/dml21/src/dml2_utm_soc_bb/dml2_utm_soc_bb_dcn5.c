// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_utm_soc_bb_dcn5.h"
#include "bounding_boxes/dcn5_soc_bb.h"
#include "dml2_debug.h"

static unsigned int dcn5_sop_table_get_highest_index(const struct dml2_sop_table *table)
{
	return table->model->sop_count - 1;
}

static void dcn5_sop_table_get_sop_constraint_at_index(const struct dml2_sop_table *table,
		unsigned int index, struct dml2_sop_constraint *constraint)
{
	const struct utm_qos_model *model = table->model;
	const struct utm_qos_model_dchub_v1 *dchub = model->dchub_v1;

	DML_ASSERT_MSG(index < model->sop_count, "unsupported sop index\n");
	constraint->dcn5.clocks.fclk_khz = model->sops[index].fclk_khz;
	constraint->dcn5.clocks.uclk_khz = model->sops[index].uclk_khz;
	constraint->dcn5.clocks.dcfclk_khz = dchub->dcfclks_khz[index];
	constraint->dcn5.clocks.socclk_khz = dchub->socclks_khz[index];
	constraint->dcn5.latency.dcn5.urgent_ramp = dchub->latencies[index].urgent_ramp_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.t_trip = dchub->latencies[index].t_trip_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.meta_trip_to_mem = dchub->latencies[index].meta_trip_to_mem_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.max_req_latency_urg = dchub->latencies[index].max_req_latency_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.avg_req_latency_urg = dchub->latencies[index].avg_req_latency_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.max_req_latency_non_urg = dchub->latencies[index].max_req_latency_non_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.avg_req_latency_non_urg = dchub->latencies[index].avg_req_latency_non_urg_ps / 1000000.0;
	constraint->dcn5.latency.dcn5.df_response_time_us = dchub->latencies[index].df_response_time_ps / 1000000.0;
	constraint->dcn5.min_available_urgent_bandwidth_KBps = dchub->bandwidths[index].urgent_bandwidth_KBps;
	constraint->dcn5.min_sop_index = index;
}

static bool dcn5_sop_table_is_bandwidth_supported_at_index(
		const struct dml2_sop_table *table, const struct dml2_memory_path_bandwidth *bw, unsigned int index)
{
	struct utm_qos_model_dchub_memory_path_bandwidth_v1 qos_bandwidth;
	bool result = true;

	qos_bandwidth.nominal_bandwidth_KBps = (uint32_t) bw->dcn5.non_urgent_bandwidth_kbps;
	qos_bandwidth.urgent_bandwidth_KBps = (uint32_t) bw->dcn5.urgent_bandwidth_kbps;

	if (!dchub_v1_is_qos_bandwidth_supported_by_sop(table->model, &qos_bandwidth, (uint8_t) index))
		result = false;

	return result;
}

static void dcn5_sop_table_get_max_sop(const struct dml2_sop_table *table, struct dml2_soc_operating_point *sop)
{
	const struct utm_qos_model *model = table->model;
	const struct utm_qos_model_dchub_v1 *dchub = model->dchub_v1;

	DML_ASSERT_MSG(model->sop_count > 0, "utm_qos_model must contain at least 1 sop\n");
	if (model->sop_count > 0) {
		sop->fclk_khz = model->sops[model->sop_count-1].fclk_khz;
		sop->uclk_khz = model->sops[model->sop_count-1].uclk_khz;
		sop->dcfclk_khz = dchub->dcfclks_khz[model->sop_count-1];
		sop->socclk_khz = dchub->socclks_khz[model->sop_count-1];
	}
}

static void dcn5_sop_table_get_min_sop(const struct dml2_sop_table *table, struct dml2_soc_operating_point *sop)
{
	const struct utm_qos_model *model = table->model;
	const struct utm_qos_model_dchub_v1 *dchub = model->dchub_v1;

	sop->fclk_khz = model->sops[0].fclk_khz;
	sop->uclk_khz = model->sops[0].uclk_khz;
	sop->dcfclk_khz = dchub->dcfclks_khz[0];
	sop->socclk_khz = dchub->socclks_khz[0];
}

void dml2_utm_soc_bb_dcn5_build_sop_table(struct dml2_sop_table *table, const struct dml2_utm_soc_bb *utm_soc_bb)
{
	table->get_highest_sop_index = dcn5_sop_table_get_highest_index;
	table->get_sop_constraint_at_index = dcn5_sop_table_get_sop_constraint_at_index;
	table->is_bw_supported_at_index = dcn5_sop_table_is_bandwidth_supported_at_index;
	table->get_max_sop = dcn5_sop_table_get_max_sop;
	table->get_min_sop = dcn5_sop_table_get_min_sop;
	table->model = &utm_soc_bb->qos_model;

	DML_ASSERT_MSG(table->model->sop_count > 0, "qos_model must contain at least 1 sop\n");
}

static void dcn5_copy_utm_qos_model(struct utm_qos_model *dest, struct utm_qos_model_dchub_v1 *dest_dchub, const struct utm_qos_model *src)
{
	*dest = *src;
	*dest_dchub = *src->dchub_v1;
	dest->dchub_v1 = dest_dchub;
}

bool dml2_utm_soc_bb_dcn5_create(struct dml2_utm_soc_bb *utm_soc_bb,
		const struct dml2_soc_bb *soc_bb, const struct utm_qos_model *explicit_qos_model)
{
	const struct utm_qos_model *qos_model = &utm_soc_bb->qos_model;

	if (explicit_qos_model)
		dcn5_copy_utm_qos_model(&utm_soc_bb->qos_model, &utm_soc_bb->qos_model_dchub_v1, explicit_qos_model);
	else
		dcn5_initialize_utm_qos_model(&utm_soc_bb->qos_model, &utm_soc_bb->qos_model_dchub_v1);


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
	utm_soc_bb->power_management_parameters = soc_bb->power_management_parameters;
	utm_soc_bb->writeback_base_latency_us = soc_bb->qos_parameters.writeback.base_latency_us;
	utm_soc_bb->vmin_limit = soc_bb->vmin_limit;
	utm_soc_bb->dchub_refclk_mhz = soc_bb->dchub_refclk_mhz;
	utm_soc_bb->max_outstanding_reqs = soc_bb->max_outstanding_reqs;
	utm_soc_bb->return_bus_width_bytes = soc_bb->return_bus_width_bytes;
	utm_soc_bb->phy_downspread_percent = soc_bb->phy_downspread_percent;
	utm_soc_bb->dcn_downspread_percent = soc_bb->dcn_downspread_percent;
	utm_soc_bb->dispclk_dppclk_vco_speed_mhz = soc_bb->dispclk_dppclk_vco_speed_mhz;
	utm_soc_bb->no_dfs = soc_bb->no_dfs;
	utm_soc_bb->mem_word_bytes = soc_bb->mem_word_bytes;
	utm_soc_bb->num_dcc_mcaches = soc_bb->num_dcc_mcaches;
	utm_soc_bb->mcache_size_bytes = soc_bb->mcache_size_bytes;
	utm_soc_bb->mcache_line_size_bytes = soc_bb->mcache_line_size_bytes;
	utm_soc_bb->lower_bound_bandwidth_dchub = soc_bb->lower_bound_bandwidth_dchub;

	/* initialize based on qos model */
	utm_soc_bb->dram_config.channel_width_bytes = qos_model->socbb.dram_channel_width_bytes;
	utm_soc_bb->dram_config.channel_count = qos_model->socbb.dram_channel_count;
	utm_soc_bb->dram_config.transactions_per_clock = qos_model->socbb.dram_transactions_per_clock;
	utm_soc_bb->max_dtbclk_khz = qos_model->dchub_v1->dcfclks_khz[qos_model->sop_count-1];
	dml2_utm_soc_bb_dcn5_build_sop_table(&utm_soc_bb->sop_table, utm_soc_bb);

	return true;
}
