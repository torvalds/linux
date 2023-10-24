/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef _ICP_QAT_FW_INIT_ADMIN_H_
#define _ICP_QAT_FW_INIT_ADMIN_H_

#include "icp_qat_fw.h"

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
	ICP_QAT_FW_HEARTBEAT_TIMER_SET = 13,
	ICP_QAT_FW_TIMER_GET = 19,
	ICP_QAT_FW_PM_STATE_CONFIG = 128,
};

enum icp_qat_fw_init_admin_resp_status {
	ICP_QAT_FW_INIT_RESP_STATUS_SUCCESS = 0,
	ICP_QAT_FW_INIT_RESP_STATUS_FAIL
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
	};
} __packed;

#define ICP_QAT_FW_SYNC ICP_QAT_FW_HEARTBEAT_SYNC

#endif
