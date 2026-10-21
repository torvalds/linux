// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.
#ifndef UTM_QOS_MODEL_DCHUB_V1_H
#define UTM_QOS_MODEL_DCHUB_V1_H
#include "utm_qos_model_types.h"

struct utm_qos_model_dchub_memory_path_latency_v1 {
	uint32_t urgent_ramp_ps;
	uint32_t t_trip_ps;
	uint32_t meta_trip_to_mem_ps;
	uint32_t max_req_latency_urg_ps;
	uint32_t avg_req_latency_urg_ps;
	uint32_t max_req_latency_non_urg_ps;
	uint32_t avg_req_latency_non_urg_ps;
	uint32_t df_response_time_ps;
};

struct utm_qos_model_dchub_memory_path_bandwidth_v1 {
	uint32_t nominal_bandwidth_KBps;
	uint32_t urgent_bandwidth_KBps;
};

struct utm_qos_model_dchub_memory_path_qos_v1 {
	struct utm_qos_model_dchub_memory_path_latency_v1 latency_upper_bound;
	struct utm_qos_model_dchub_memory_path_bandwidth_v1 bandwidth_lower_bound;
};

struct utm_qos_model_dchub_v1 {
	struct utm_qos_model_dchub_memory_path_latency_v1 latencies[MAX_UTM_SOP_COUNT];
	struct utm_qos_model_dchub_memory_path_bandwidth_v1 bandwidths[MAX_UTM_SOP_COUNT];
	uint32_t dcfclks_khz[MAX_UTM_SOP_COUNT];
	uint32_t socclks_khz[MAX_UTM_SOP_COUNT];
};

static inline bool dchub_v1_is_qos_latency_supported_by_sop(const struct utm_qos_model *model,
		const struct utm_qos_model_dchub_memory_path_latency_v1 *qos_latency,
		uint8_t sop_index)
{
	const struct utm_qos_model_dchub_memory_path_latency_v1 *sop_latency = &model->dchub_v1->latencies[sop_index];

	return (qos_latency->urgent_ramp_ps >= sop_latency->urgent_ramp_ps &&
			qos_latency->t_trip_ps >= sop_latency->t_trip_ps &&
			qos_latency->meta_trip_to_mem_ps >= sop_latency->meta_trip_to_mem_ps &&
			qos_latency->max_req_latency_urg_ps >= sop_latency->max_req_latency_urg_ps &&
			qos_latency->avg_req_latency_urg_ps >= sop_latency->avg_req_latency_urg_ps &&
			qos_latency->max_req_latency_non_urg_ps >= sop_latency->max_req_latency_non_urg_ps &&
			qos_latency->avg_req_latency_non_urg_ps >= sop_latency->avg_req_latency_non_urg_ps &&
			qos_latency->df_response_time_ps >= sop_latency->df_response_time_ps);
}

static inline bool dchub_v1_is_qos_bandwidth_supported_by_sop(
		const struct utm_qos_model *model,
		const struct utm_qos_model_dchub_memory_path_bandwidth_v1 *qos_bandwidth,
		uint8_t sop_index)
{
	return (model->dchub_v1->bandwidths[sop_index].nominal_bandwidth_KBps >= qos_bandwidth->nominal_bandwidth_KBps
			&& model->dchub_v1->bandwidths[sop_index].urgent_bandwidth_KBps >= qos_bandwidth->urgent_bandwidth_KBps);

//	const struct utm_soc_operating_point *sop = &model->sops[sop_index];
//	const struct utm_qos_model_socbb *socbb = &model->socbb;
//	uint64_t available_bandwidth_KBps;
//	uint64_t min_available_bandwidth_KBps;
//
//	min_available_bandwidth_KBps = (uint64_t) sop->uclk_khz
//			* socbb->dram_channel_count
//			* socbb->dram_channel_width_bytes
//			* socbb->dram_transactions_per_clock
//			* socbb->dram_derate_percent_nominal / 100;
//
//	available_bandwidth_KBps = (uint64_t) sop->fclk_khz
//			* socbb->fabric_datapath_to_dcn_data_return_bytes
//			* socbb->fabric_derate_percent_nominal / 100;
//	if (min_available_bandwidth_KBps > available_bandwidth_KBps)
//		min_available_bandwidth_KBps = available_bandwidth_KBps;
//
//	available_bandwidth_KBps = (uint64_t) model->dchub_v1->dcfclks_khz[sop_index]
//			* model->dchub_v1->return_bus_width_bytes
//			* model->dchub_v1->sdp_derate_percent_nominal / 100;
//	if (min_available_bandwidth_KBps > available_bandwidth_KBps)
//		min_available_bandwidth_KBps = available_bandwidth_KBps;
//
//	if ((min_available_bandwidth_KBps * nominal_utm_budget_percent / 100) < qos_bandwidth->nominal_bandwidth_KBps)
//		return false;
//
//	min_available_bandwidth_KBps = (uint64_t) sop->uclk_khz
//			* socbb->dram_channel_count
//			* socbb->dram_channel_width_bytes
//			* socbb->dram_transactions_per_clock
//			* socbb->dram_derate_percent_urgent / 100;
//
//	available_bandwidth_KBps = (uint64_t) sop->fclk_khz
//			* socbb->fabric_datapath_to_dcn_data_return_bytes
//			* socbb->fabric_derate_percent_urgent / 100;
//	if (min_available_bandwidth_KBps > available_bandwidth_KBps)
//		min_available_bandwidth_KBps = available_bandwidth_KBps;
//
//	available_bandwidth_KBps = (uint64_t) model->dchub_v1->dcfclks_khz[sop_index]
//			* model->dchub_v1->return_bus_width_bytes
//			* model->dchub_v1->sdp_derate_percent_urgent / 100;
//	if (min_available_bandwidth_KBps > available_bandwidth_KBps)
//		min_available_bandwidth_KBps = available_bandwidth_KBps;
//
//	if ((min_available_bandwidth_KBps * urgent_utm_budget_percent / 100) < qos_bandwidth->urgent_bandwidth_KBps)
//		return false;
//
//	return true;
}
#endif /* #ifndef UTM_QOS_MODEL_DCHUB_V1_H */
