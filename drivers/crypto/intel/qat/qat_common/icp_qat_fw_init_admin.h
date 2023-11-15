/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef _ICP_QAT_FW_INIT_ADMIN_H_
#define _ICP_QAT_FW_INIT_ADMIN_H_

#include "icp_qat_fw.h"

#define RL_MAX_RP_IDS 16

enum icp_qat_fw_init_admin_cmd_id {
	ICP_QAT_FW_INIT_AE = 0,
	ICP_QAT_FW_TRNG_ENABLE = 1,
	ICP_QAT_FW_TRNG_DISABLE = 2,
	ICP_QAT_FW_CONSTANTS_CFG = 3,
	ICP_QAT_FW_STATUS_GET = 4,
	ICP_QAT_FW_COUNTERS_GET = 5,
	ICP_QAT_FW_LOOPBACK = 6,
	ICP_QAT_FW_HEARTBEAT_SYNC = 7,
	ICP_QAT_FW_HEARTBEAT_GET = 8,
	ICP_QAT_FW_COMP_CAPABILITY_GET = 9,
	ICP_QAT_FW_CRYPTO_CAPABILITY_GET = 10,
	ICP_QAT_FW_DC_CHAIN_INIT = 11,
	ICP_QAT_FW_HEARTBEAT_TIMER_SET = 13,
	ICP_QAT_FW_RL_INIT = 15,
	ICP_QAT_FW_TIMER_GET = 19,
	ICP_QAT_FW_CNV_STATS_GET = 20,
	ICP_QAT_FW_PM_STATE_CONFIG = 128,
	ICP_QAT_FW_PM_INFO = 129,
	ICP_QAT_FW_RL_ADD = 134,
	ICP_QAT_FW_RL_UPDATE = 135,
	ICP_QAT_FW_RL_REMOVE = 136,
};

enum icp_qat_fw_init_admin_resp_status {
	ICP_QAT_FW_INIT_RESP_STATUS_SUCCESS = 0,
	ICP_QAT_FW_INIT_RESP_STATUS_FAIL
};

struct icp_qat_fw_init_admin_slice_cnt {
	__u8 cpr_cnt;
	__u8 xlt_cnt;
	__u8 dcpr_cnt;
	__u8 pke_cnt;
	__u8 wat_cnt;
	__u8 wcp_cnt;
	__u8 ucs_cnt;
	__u8 cph_cnt;
	__u8 ath_cnt;
};

struct icp_qat_fw_init_admin_sla_config_params {
	__u32 pcie_in_cir;
	__u32 pcie_in_pir;
	__u32 pcie_out_cir;
	__u32 pcie_out_pir;
	__u32 slice_util_cir;
	__u32 slice_util_pir;
	__u32 ae_util_cir;
	__u32 ae_util_pir;
	__u16 rp_ids[RL_MAX_RP_IDS];
};

struct icp_qat_fw_init_admin_req {
	__u16 init_cfg_sz;
	__u8 resrvd1;
	__u8 cmd_id;
	__u32 resrvd2;
	__u64 opaque_data;
	__u64 init_cfg_ptr;

	union {
		struct {
			__u16 ibuf_size_in_kb;
			__u16 resrvd3;
		};
		struct {
			__u32 int_timer_ticks;
		};
		struct {
			__u32 heartbeat_ticks;
		};
		struct {
			__u16 node_id;
			__u8 node_type;
			__u8 svc_type;
			__u8 resrvd5[3];
			__u8 rp_count;
		};
		__u32 idle_filter;
	};

	__u32 resrvd4;
} __packed;

struct icp_qat_fw_init_admin_resp {
	__u8 flags;
	__u8 resrvd1;
	__u8 status;
	__u8 cmd_id;
	union {
		__u32 resrvd2;
		struct {
			__u16 version_minor_num;
			__u16 version_major_num;
		};
		__u32 extended_features;
		struct {
			__u16 error_count;
			__u16 latest_error;
		};
	};
	__u64 opaque_data;
	union {
		__u32 resrvd3[ICP_QAT_FW_NUM_LONGWORDS_4];
		struct {
			__u32 version_patch_num;
			__u8 context_id;
			__u8 ae_id;
			__u16 resrvd4;
			__u64 resrvd5;
		};
		struct {
			__u64 req_rec_count;
			__u64 resp_sent_count;
		};
		struct {
			__u16 compression_algos;
			__u16 checksum_algos;
			__u32 deflate_capabilities;
			__u32 resrvd6;
			__u32 lzs_capabilities;
		};
		struct {
			__u32 cipher_algos;
			__u32 hash_algos;
			__u16 keygen_algos;
			__u16 other;
			__u16 public_key_algos;
			__u16 prime_algos;
		};
		struct {
			__u64 timestamp;
			__u64 resrvd7;
		};
		struct {
			__u32 successful_count;
			__u32 unsuccessful_count;
			__u64 resrvd8;
		};
		struct icp_qat_fw_init_admin_slice_cnt slices;
		__u16 fw_capabilities;
	};
} __packed;

#define ICP_QAT_FW_SYNC ICP_QAT_FW_HEARTBEAT_SYNC
#define ICP_QAT_FW_CAPABILITIES_GET ICP_QAT_FW_CRYPTO_CAPABILITY_GET

#define ICP_QAT_NUMBER_OF_PM_EVENTS 8

struct icp_qat_fw_init_admin_pm_info {
	__u16 max_pwrreq;
	__u16 min_pwrreq;
	__u16 resvrd1;
	__u8 pwr_state;
	__u8 resvrd2;
	__u32 fusectl0;
	struct_group(event_counters,
		__u32 sys_pm;
		__u32 host_msg;
		__u32 unknown;
		__u32 local_ssm;
		__u32 timer;
	);
	__u32 event_log[ICP_QAT_NUMBER_OF_PM_EVENTS];
	struct_group(pm,
		__u32 fw_init;
		__u32 pwrreq;
		__u32 status;
		__u32 main;
		__u32 thread;
	);
	struct_group(ssm,
		__u32 pm_enable;
		__u32 pm_active_status;
		__u32 pm_managed_status;
		__u32 pm_domain_status;
		__u32 active_constraint;
	);
	__u32 resvrd3[6];
};

#endif
