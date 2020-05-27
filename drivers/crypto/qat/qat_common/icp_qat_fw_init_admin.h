/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef _ICP_QAT_FW_INIT_ADMIN_H_
#define _ICP_QAT_FW_INIT_ADMIN_H_

#include "icp_qat_fw.h"

enum icp_qat_fw_init_admin_cmd_id {
	ICP_QAT_FW_INIT_ME = 0,
	ICP_QAT_FW_TRNG_ENABLE = 1,
	ICP_QAT_FW_TRNG_DISABLE = 2,
	ICP_QAT_FW_CONSTANTS_CFG = 3,
	ICP_QAT_FW_STATUS_GET = 4,
	ICP_QAT_FW_COUNTERS_GET = 5,
	ICP_QAT_FW_LOOPBACK = 6,
	ICP_QAT_FW_HEARTBEAT_SYNC = 7,
	ICP_QAT_FW_HEARTBEAT_GET = 8
};

enum icp_qat_fw_init_admin_resp_status {
	ICP_QAT_FW_INIT_RESP_STATUS_SUCCESS = 0,
	ICP_QAT_FW_INIT_RESP_STATUS_FAIL
};

struct icp_qat_fw_init_admin_req {
	uint16_t init_cfg_sz;
	uint8_t resrvd1;
	uint8_t init_admin_cmd_id;
	uint32_t resrvd2;
	uint64_t opaque_data;
	uint64_t init_cfg_ptr;
	uint64_t resrvd3;
};

struct icp_qat_fw_init_admin_resp_hdr {
	uint8_t flags;
	uint8_t resrvd1;
	uint8_t status;
	uint8_t init_admin_cmd_id;
};

struct icp_qat_fw_init_admin_resp_pars {
	union {
		uint32_t resrvd1[ICP_QAT_FW_NUM_LONGWORDS_4];
		struct {
			uint32_t version_patch_num;
			uint8_t context_id;
			uint8_t ae_id;
			uint16_t resrvd1;
			uint64_t resrvd2;
		} s1;
		struct {
			uint64_t req_rec_count;
			uint64_t resp_sent_count;
		} s2;
	} u;
};

struct icp_qat_fw_init_admin_resp {
	struct icp_qat_fw_init_admin_resp_hdr init_resp_hdr;
	union {
		uint32_t resrvd2;
		struct {
			uint16_t version_minor_num;
			uint16_t version_major_num;
		} s;
	} u;
	uint64_t opaque_data;
	struct icp_qat_fw_init_admin_resp_pars init_resp_pars;
};

#define ICP_QAT_FW_COMN_HEARTBEAT_OK 0
#define ICP_QAT_FW_COMN_HEARTBEAT_BLOCKED 1
#define ICP_QAT_FW_COMN_HEARTBEAT_FLAG_BITPOS 0
#define ICP_QAT_FW_COMN_HEARTBEAT_FLAG_MASK 0x1
#define ICP_QAT_FW_COMN_STATUS_RESRVD_FLD_MASK 0xFE
#define ICP_QAT_FW_COMN_HEARTBEAT_HDR_FLAG_GET(hdr_t) \
	ICP_QAT_FW_COMN_HEARTBEAT_FLAG_GET(hdr_t.flags)

#define ICP_QAT_FW_COMN_HEARTBEAT_HDR_FLAG_SET(hdr_t, val) \
	ICP_QAT_FW_COMN_HEARTBEAT_FLAG_SET(hdr_t, val)

#define ICP_QAT_FW_COMN_HEARTBEAT_FLAG_GET(flags) \
	QAT_FIELD_GET(flags, \
		 ICP_QAT_FW_COMN_HEARTBEAT_FLAG_BITPOS, \
		 ICP_QAT_FW_COMN_HEARTBEAT_FLAG_MASK)
#endif
