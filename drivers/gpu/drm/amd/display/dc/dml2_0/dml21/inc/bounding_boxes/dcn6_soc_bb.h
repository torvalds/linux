// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DCN6_SOC_BB_H__
#define __DCN6_SOC_BB_H__
#include "dml2_external_lib_deps.h"
#include "utm_qos_model_dchub_v2.h"
#include "utm_qos_model_dchub_v3.h"
#include "dml_top_soc_parameter_types.h"

static inline void dcn6_test_initialize_soc_bb(struct dml2_soc_bb *soc_bb)
{
	memset(soc_bb, 0, sizeof(struct dml2_soc_bb));
}

static inline void dcn6b_test_initialize_soc_bb(struct dml2_soc_bb *soc_bb)
{
	dcn6_test_initialize_soc_bb(soc_bb);
}

static inline void dcn6_test_initialize_ip_caps(struct dml2_ip_capabilities *ip_caps)
{
	memset(ip_caps, 0, sizeof(struct dml2_ip_capabilities));
}

static inline void dcn6_initialize_utm_qos_model_with_fixed_allocation(struct utm_qos_model *qos_model, struct utm_qos_model_dchub_v2 *dchub)
{
	memset(qos_model, 0, sizeof(struct utm_qos_model));
	memset(dchub, 0, sizeof(struct utm_qos_model_dchub_v2));
	qos_model->dchub_v2 = dchub;
}

static inline void dcn6_test_initialize_utm_qos_model(struct utm_qos_model *qos_model, struct utm_qos_model_dchub_v2 *dchub)
{
}

static inline void dcn6b_test_initialize_utm_qos_model(struct utm_qos_model *qos_model, struct utm_qos_model_dchub_v2 *dchub)
{

	dcn6_initialize_utm_qos_model_with_fixed_allocation(qos_model, dchub);
}

static inline void dcn6_n_minus_1_initialize_utm_qos_model(
		struct utm_qos_model *qos_model, struct utm_qos_model_dchub_v2 *dchub)
{
	memset(qos_model, 0, sizeof(struct utm_qos_model));
	memset(dchub, 0, sizeof(struct utm_qos_model_dchub_v2));
	qos_model->dchub_v2 = dchub;
	qos_model->sops[0].fclk_khz = 300000;
	qos_model->sops[0].uclk_khz = 97000;
	qos_model->sops[1].fclk_khz = 503684;
	qos_model->sops[1].uclk_khz = 435000;
	qos_model->sops[2].fclk_khz = 1007368;
	qos_model->sops[2].uclk_khz = 521000;
	qos_model->sops[3].fclk_khz = 1206526;
	qos_model->sops[3].uclk_khz = 731000;
	qos_model->sops[4].fclk_khz = 1250000;
	qos_model->sops[4].uclk_khz = 822000;
	qos_model->sops[5].fclk_khz = 1250000;
	qos_model->sops[5].uclk_khz = 962000;
	qos_model->sops[6].fclk_khz = 1250000;
	qos_model->sops[6].uclk_khz = 1069000;
	qos_model->sops[7].fclk_khz = 1250000;
	qos_model->sops[7].uclk_khz = 1187000;
	qos_model->socbb.fabric_datapath_to_dcn_data_return_bytes = 64;
	qos_model->socbb.dram_channel_width_bytes = 2;
	qos_model->socbb.dram_channel_count = 16;
	qos_model->socbb.dram_transactions_per_clock = 16;
	qos_model->socbb.fabric_derate_percent_nominal = 57;
	qos_model->socbb.fabric_derate_percent_urgent = 75;
	qos_model->socbb.dram_derate_percent_nominal = 17;
	qos_model->socbb.dram_derate_percent_urgent = 22;
	qos_model->sop_count = 8;

	dchub->latencies[0].urgent_ramp_ps = 9865636;
	dchub->latencies[0].t_trip_ps = 12650172;
	dchub->latencies[0].meta_trip_to_mem_ps = 11227505;
	dchub->latencies[0].max_req_latency_urg_ps = 2531615;
	dchub->latencies[0].avg_req_latency_urg_ps = 1772436;
	dchub->latencies[0].max_req_latency_non_urg_ps = 12650171;
	dchub->latencies[0].avg_req_latency_non_urg_ps = 2548107;
	dchub->latencies[0].df_response_time_ps = 1000000;
	dchub->latencies[1].urgent_ramp_ps = 3411495;
	dchub->latencies[1].t_trip_ps = 3467378;
	dchub->latencies[1].meta_trip_to_mem_ps = 2620137;
	dchub->latencies[1].max_req_latency_urg_ps = 1369447;
	dchub->latencies[1].avg_req_latency_urg_ps = 834368;
	dchub->latencies[1].max_req_latency_non_urg_ps = 3468646;
	dchub->latencies[1].avg_req_latency_non_urg_ps = 970920;
	dchub->latencies[1].df_response_time_ps = 595611;
	dchub->latencies[2].urgent_ramp_ps = 2388169;
	dchub->latencies[2].t_trip_ps = 2594969;
	dchub->latencies[2].meta_trip_to_mem_ps = 2171306;
	dchub->latencies[2].max_req_latency_urg_ps = 875775;
	dchub->latencies[2].avg_req_latency_urg_ps = 526390;
	dchub->latencies[2].max_req_latency_non_urg_ps = 2595139;
	dchub->latencies[2].avg_req_latency_non_urg_ps = 630268;
	dchub->latencies[2].df_response_time_ps = 297805;
	dchub->latencies[3].urgent_ramp_ps = 1896062;
	dchub->latencies[3].t_trip_ps = 1934965;
	dchub->latencies[3].meta_trip_to_mem_ps = 1581238;
	dchub->latencies[3].max_req_latency_urg_ps = 768344;
	dchub->latencies[3].avg_req_latency_urg_ps = 445581;
	dchub->latencies[3].max_req_latency_non_urg_ps = 1935135;
	dchub->latencies[3].avg_req_latency_non_urg_ps = 506977;
	dchub->latencies[3].df_response_time_ps = 248647;
	dchub->latencies[4].urgent_ramp_ps = 1769285;
	dchub->latencies[4].t_trip_ps = 1769285;
	dchub->latencies[4].meta_trip_to_mem_ps = 1416798;
	dchub->latencies[4].max_req_latency_urg_ps = 742910;
	dchub->latencies[4].avg_req_latency_urg_ps = 425284;
	dchub->latencies[4].max_req_latency_non_urg_ps = 1758238;
	dchub->latencies[4].avg_req_latency_non_urg_ps = 475065;
	dchub->latencies[4].df_response_time_ps = 240000;
	dchub->latencies[5].urgent_ramp_ps = 1652902;
	dchub->latencies[5].t_trip_ps = 1652902;
	dchub->latencies[5].meta_trip_to_mem_ps = 1230505;
	dchub->latencies[5].max_req_latency_urg_ps = 734108;
	dchub->latencies[5].avg_req_latency_urg_ps = 416888;
	dchub->latencies[5].max_req_latency_non_urg_ps = 1571945;
	dchub->latencies[5].avg_req_latency_non_urg_ps = 451191;
	dchub->latencies[5].df_response_time_ps = 240000;
	dchub->latencies[6].urgent_ramp_ps = 1582791;
	dchub->latencies[6].t_trip_ps = 1582791;
	dchub->latencies[6].meta_trip_to_mem_ps = 1122960;
	dchub->latencies[6].max_req_latency_urg_ps = 727450;
	dchub->latencies[6].avg_req_latency_urg_ps = 410145;
	dchub->latencies[6].max_req_latency_non_urg_ps = 1464400;
	dchub->latencies[6].avg_req_latency_non_urg_ps = 436076;
	dchub->latencies[6].df_response_time_ps = 240000;
	dchub->latencies[7].urgent_ramp_ps = 1520802;
	dchub->latencies[7].t_trip_ps = 1520802;
	dchub->latencies[7].meta_trip_to_mem_ps = 1022428;
	dchub->latencies[7].max_req_latency_urg_ps = 722083;
	dchub->latencies[7].avg_req_latency_urg_ps = 404088;
	dchub->latencies[7].max_req_latency_non_urg_ps = 1363868;
	dchub->latencies[7].avg_req_latency_non_urg_ps = 422992;
	dchub->latencies[7].df_response_time_ps = 240000;

	/*
	 * TODO: currently both utm budget percent and derate percent are both included in derate percent params. Need
	 * to separate them. So we can use the actual utm budget percent values below.
	 */
	dchub->max_nominal_utm_budget_percent = 100;
	dchub->min_nominal_utm_budget_percent = 100;
	dchub->max_urgent_utm_budget_percent = 100;
	dchub->min_urgent_utm_budget_percent = 100;
}


static inline unsigned int dcn6a_test_initialize_sop_clocks(
		struct utm_soc_operating_point *sop_clocks)
{
	return 0;
}

static inline unsigned int dcn6b_test_initialize_sop_clocks(
		struct utm_soc_operating_point *sop_clocks)
{
	return 0;
}

/**
 * dcn6_test_initialize_v3_sop_latencies - Set latencies for one SOP entry.
 */
static inline void dcn6_test_initialize_v3_sop_latencies(
		struct utm_qos_model_dchub_v3_sop_entry *entry,
		uint32_t urgent_ramp_ps, uint32_t t_trip_ps,
		uint32_t meta_trip_to_mem_ps,
		uint32_t max_urg_ps, uint32_t avg_urg_ps,
		uint32_t max_non_urg_ps, uint32_t avg_non_urg_ps,
		uint32_t df_response_time_ps)
{
	entry->urgent_ramp_ps = urgent_ramp_ps;
	entry->t_trip_ps = t_trip_ps;
	entry->meta_trip_to_mem_ps = meta_trip_to_mem_ps;
	entry->max_req_latency_urg_ps = max_urg_ps;
	entry->avg_req_latency_urg_ps = avg_urg_ps;
	entry->max_req_latency_non_urg_ps = max_non_urg_ps;
	entry->avg_req_latency_non_urg_ps = avg_non_urg_ps;
	entry->df_response_time_ps = df_response_time_ps;
}

/**
 * dcn6_test_initialize_v3_sop_latencies_all_levels - Set identical latencies
 * across all load levels for one SOP index.
 */
static inline void dcn6_test_initialize_v3_sop_latencies_all_levels(
		struct utm_qos_model_dchub_v3 *dchub, unsigned int sop_index,
		uint32_t urgent_ramp_ps, uint32_t t_trip_ps,
		uint32_t meta_trip_to_mem_ps,
		uint32_t max_urg_ps, uint32_t avg_urg_ps,
		uint32_t max_non_urg_ps, uint32_t avg_non_urg_ps,
		uint32_t df_response_time_ps)
{
	unsigned int ll;

	for (ll = 0; ll < dchub->load_level_count; ll++)
		dcn6_test_initialize_v3_sop_latencies(
				&dchub->sops[ll][sop_index],
				urgent_ramp_ps, t_trip_ps,
				meta_trip_to_mem_ps,
				max_urg_ps, avg_urg_ps,
				max_non_urg_ps, avg_non_urg_ps,
				df_response_time_ps);
}

static inline void dcn6_test_initialize_utm_qos_model_v3(
		struct utm_qos_model *qos_model,
		struct utm_qos_model_dchub_v3 *dchub)
{
	memset(dchub, 0, sizeof(struct utm_qos_model_dchub_v3));
	memset(qos_model, 0, sizeof(struct utm_qos_model));
	qos_model->version = utm_qos_model_version_v3;
	qos_model->dchub_v3 = dchub;
}

static inline void dcn6b_test_initialize_utm_qos_model_v3(
		struct utm_qos_model *qos_model,
		struct utm_qos_model_dchub_v3 *dchub)
{
	memset(dchub, 0, sizeof(struct utm_qos_model_dchub_v3));
	memset(qos_model, 0, sizeof(struct utm_qos_model));
	qos_model->version = utm_qos_model_version_v3;
	qos_model->dchub_v3 = dchub;
}

#endif /* __DCN6_SOC_BB_H__ */
