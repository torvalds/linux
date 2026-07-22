// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.
#ifndef UTM_QOS_MODEL_TYPES_H
#define UTM_QOS_MODEL_TYPES_H

struct utm_soc_operating_point {
	uint32_t uclk_khz;
	uint32_t fclk_khz;
};

struct utm_qos_model_socbb {
	uint8_t fabric_datapath_to_dcn_data_return_bytes;
	uint8_t dram_channel_width_bytes;
	uint8_t dram_channel_count;
	uint8_t dram_transactions_per_clock;
	uint8_t fabric_derate_percent_nominal;
	uint8_t fabric_derate_percent_urgent;
	uint8_t dram_derate_percent_nominal;
	uint8_t dram_derate_percent_urgent;
	uint8_t lsdma_fabric_derate_percent;
	uint8_t lsdma_dram_derate_percent;
	uint8_t fabric_datapath_to_lsdma_data_return_bytes;
};

#define MAX_UTM_SOP_COUNT 20

enum utm_qos_model_version {
	utm_qos_model_version_v1,
	utm_qos_model_version_v2,
	utm_qos_model_version_v3,
};

struct utm_qos_model_dchub_v1;
struct utm_qos_model_dchub_v2;
struct utm_qos_model_dchub_v3;
struct utm_qos_model_lsdma;
struct utm_qos_model {
	int version;
	struct utm_soc_operating_point sops[MAX_UTM_SOP_COUNT];
	union {
		const struct utm_qos_model_dchub_v1 *dchub_v1;
		const struct utm_qos_model_dchub_v2 *dchub_v2;
		const struct utm_qos_model_dchub_v3 *dchub_v3;
	};
	const struct utm_qos_model_lsdma *lsdma;
	struct utm_qos_model_socbb socbb;
	uint8_t sop_count;
};

#endif /* #ifndef UTM_QOS_MODEL_TYPES_H */
