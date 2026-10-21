// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.
#ifndef UTM_QOS_MODEL_DCHUB_V2_H
#define UTM_QOS_MODEL_DCHUB_V2_H
#include "utm_qos_model_types.h"

struct utm_qos_model_dchub_memory_path_latency_v2 {
	uint32_t urgent_ramp_ps;
	uint32_t t_trip_ps;
	uint32_t meta_trip_to_mem_ps;
	uint32_t max_req_latency_urg_ps;
	uint32_t avg_req_latency_urg_ps;
	uint32_t max_req_latency_non_urg_ps;
	uint32_t avg_req_latency_non_urg_ps;
	uint32_t df_response_time_ps;
};

struct utm_qos_model_dchub_memory_path_bandwidth_v2 {
	uint32_t nominal_bandwidth_KBps;
	uint32_t urgent_bandwidth_KBps;
};

struct utm_qos_model_dchub_memory_path_qos_v2 {
	struct utm_qos_model_dchub_memory_path_latency_v2 latency_upper_bound;
	struct utm_qos_model_dchub_memory_path_bandwidth_v2 bandwidth_lower_bound;
};

struct utm_qos_model_dchub_v2 {
	struct utm_qos_model_dchub_memory_path_latency_v2 latencies[MAX_UTM_SOP_COUNT];

	uint8_t max_nominal_utm_budget_percent;
	uint8_t min_nominal_utm_budget_percent;
	uint8_t max_urgent_utm_budget_percent;
	uint8_t min_urgent_utm_budget_percent;
};

static inline bool dchub_v2_is_qos_latency_supported_by_sop(const struct utm_qos_model *model,
		const struct utm_qos_model_dchub_memory_path_latency_v2 *qos_latency,
		uint8_t sop_index)
{
	const struct utm_qos_model_dchub_memory_path_latency_v2 *sop_latency = &model->dchub_v2->latencies[sop_index];

	return (qos_latency->urgent_ramp_ps >= sop_latency->urgent_ramp_ps &&
			qos_latency->t_trip_ps >= sop_latency->t_trip_ps &&
			qos_latency->meta_trip_to_mem_ps >= sop_latency->meta_trip_to_mem_ps &&
			qos_latency->max_req_latency_urg_ps >= sop_latency->max_req_latency_urg_ps &&
			qos_latency->avg_req_latency_urg_ps >= sop_latency->avg_req_latency_urg_ps &&
			qos_latency->max_req_latency_non_urg_ps >= sop_latency->max_req_latency_non_urg_ps &&
			qos_latency->avg_req_latency_non_urg_ps >= sop_latency->avg_req_latency_non_urg_ps &&
			qos_latency->df_response_time_ps >= sop_latency->df_response_time_ps);
}

static inline void dchub_v2_get_sop_total_available_bandwidth_KBps(
		const struct utm_qos_model *model,
		struct utm_qos_model_dchub_memory_path_bandwidth_v2 *total_available_bandwidth,
		uint8_t sop_index)
{
	const struct utm_soc_operating_point *sop = &model->sops[sop_index];
	const struct utm_qos_model_socbb *socbb = &model->socbb;
	uint64_t dram_available_bandwidth_KBps_nominal;
	uint64_t fabric_available_bandwidth_KBps_nominal;
	uint64_t dram_available_bandwidth_KBps_urgent;
	uint64_t fabric_available_bandwidth_KBps_urgent;

	dram_available_bandwidth_KBps_nominal = (uint64_t) sop->uclk_khz
			* socbb->dram_channel_count
			* socbb->dram_channel_width_bytes
			* socbb->dram_transactions_per_clock
			* socbb->dram_derate_percent_nominal / 100;
	dram_available_bandwidth_KBps_urgent = (uint64_t) sop->uclk_khz
			* socbb->dram_channel_count
			* socbb->dram_channel_width_bytes
			* socbb->dram_transactions_per_clock
			* socbb->dram_derate_percent_urgent / 100;
	fabric_available_bandwidth_KBps_nominal = (uint64_t) sop->fclk_khz
			* socbb->fabric_datapath_to_dcn_data_return_bytes
			* socbb->fabric_derate_percent_nominal / 100;
	fabric_available_bandwidth_KBps_urgent = (uint64_t) sop->fclk_khz
			* socbb->fabric_datapath_to_dcn_data_return_bytes
			* socbb->fabric_derate_percent_urgent / 100;

	total_available_bandwidth->nominal_bandwidth_KBps =
			dram_available_bandwidth_KBps_nominal < fabric_available_bandwidth_KBps_nominal ?
			(uint32_t) dram_available_bandwidth_KBps_nominal :
			(uint32_t) fabric_available_bandwidth_KBps_nominal;
	total_available_bandwidth->urgent_bandwidth_KBps =
			dram_available_bandwidth_KBps_urgent < fabric_available_bandwidth_KBps_urgent ?
			(uint32_t) dram_available_bandwidth_KBps_urgent :
			(uint32_t) fabric_available_bandwidth_KBps_urgent;
}

static inline bool dchub_v2_is_qos_bandwidth_supported_by_sop(
		const struct utm_qos_model *model,
		const struct utm_qos_model_dchub_memory_path_bandwidth_v2 *qos_bandwidth,
		uint8_t sop_index,
		uint8_t nominal_utm_budget_percent,
		uint8_t urgent_utm_budget_percent)
{
	struct utm_qos_model_dchub_memory_path_bandwidth_v2 available_bandwidth = {0};
	const struct utm_qos_model_dchub_v2 *dchub = model->dchub_v2;
	uint64_t nominal_available_bandwidth_KBps;
	uint64_t urgent_available_bandwidth_KBps;

	if (nominal_utm_budget_percent > dchub->max_nominal_utm_budget_percent ||
			nominal_utm_budget_percent < dchub->min_nominal_utm_budget_percent ||
			urgent_utm_budget_percent > dchub->max_urgent_utm_budget_percent ||
			urgent_utm_budget_percent < dchub->min_urgent_utm_budget_percent)
		return false;

	dchub_v2_get_sop_total_available_bandwidth_KBps(model, &available_bandwidth, sop_index);
	nominal_available_bandwidth_KBps = available_bandwidth.nominal_bandwidth_KBps;
	urgent_available_bandwidth_KBps = available_bandwidth.urgent_bandwidth_KBps;

	if ((nominal_available_bandwidth_KBps * nominal_utm_budget_percent / 100) < qos_bandwidth->nominal_bandwidth_KBps)
		return false;
	else if ((urgent_available_bandwidth_KBps * urgent_utm_budget_percent / 100) < qos_bandwidth->urgent_bandwidth_KBps)
		return false;
	else
		return true;
}
#endif /* #ifndef UTM_QOS_MODEL_DCHUB_V2_H */
