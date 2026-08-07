// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.
#ifndef UTM_QOS_MODEL_DCHUB_V3_H
#define UTM_QOS_MODEL_DCHUB_V3_H

/* Must match DALSMC_MAX_UTM_SOP_COUNT in dalsmc.h without including it */
#define UTM_QOS_MODEL_V3_MAX_LOAD_LEVEL_COUNT 3
#define UTM_QOS_MODEL_V3_MAX_SOP_COUNT        5

#define UTM_QOS_MODEL_V3_LOAD_LEVEL_IDLE                   0
#define UTM_QOS_MODEL_V3_LOAD_LEVEL_ACTIVE_ALTERNATE_PSTATE 1
#define UTM_QOS_MODEL_V3_LOAD_LEVEL_ACTIVE                 2

/**
 * utm_qos_model_dchub_v3_sop_entry - Per-SOP QoS parameters for one load level.
 *
 * All latency fields are in picoseconds. All bandwidth fields are in KBps.
 * Budget percentage and derate are pre-applied — callers use values
 * directly without further scaling.
 */
struct utm_qos_model_dchub_v3_sop_entry {
	/* latencies */
	uint32_t urgent_ramp_ps;
	uint32_t t_trip_ps;
	uint32_t meta_trip_to_mem_ps;
	uint32_t max_req_latency_urg_ps;
	uint32_t avg_req_latency_urg_ps;
	uint32_t max_req_latency_non_urg_ps;
	uint32_t avg_req_latency_non_urg_ps;
	uint32_t df_response_time_ps;
	/* bandwidths (budget allocation and derate pre-applied) */
	uint32_t urgent_bandwidth_KBps;
	uint32_t nominal_bandwidth_KBps;
	uint32_t lsdma_bandwidth_KBps;
};

/**
 * utm_qos_model_dchub_v3 - DCN6 flat UTM QoS table.
 *
 * Indexed as sops[load_level][sop_index]. Load level constants:
 *   UTM_QOS_MODEL_V3_LOAD_LEVEL_IDLE                    (max budget %)
 *   UTM_QOS_MODEL_V3_LOAD_LEVEL_ACTIVE_ALTERNATE_PSTATE (min budget %)
 *   UTM_QOS_MODEL_V3_LOAD_LEVEL_ACTIVE                  (same as alt pstate, lsdma=0)
 */
struct utm_qos_model_dchub_v3 {
	uint8_t load_level_count;
	uint8_t sop_count;
	struct utm_qos_model_dchub_v3_sop_entry
		sops[UTM_QOS_MODEL_V3_MAX_LOAD_LEVEL_COUNT][UTM_QOS_MODEL_V3_MAX_SOP_COUNT];
};

#endif /* #ifndef UTM_QOS_MODEL_DCHUB_V3_H */