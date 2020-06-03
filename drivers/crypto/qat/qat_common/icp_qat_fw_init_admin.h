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
	__u16 init_cfg_sz;
	__u8 resrvd1;
	__u8 init_admin_cmd_id;
	__u32 resrvd2;
	__u64 opaque_data;
	__u64 init_cfg_ptr;
	__u64 resrvd3;
};

struct icp_qat_fw_init_admin_resp_hdr {
	__u8 flags;
	__u8 resrvd1;
	__u8 status;
	__u8 init_admin_cmd_id;
};

struct icp_qat_fw_init_admin_resp_pars {
	union {
		__u32 resrvd1[ICP_QAT_FW_NUM_LONGWORDS_4];
		struct {
			__u32 version_patch_num;
			__u8 context_id;
			__u8 ae_id;
			__u16 resrvd1;
			__u64 resrvd2;
		} s1;
		struct {
			__u64 req_rec_count;
			__u64 resp_sent_count;
		} s2;
	} u;
};

struct icp_qat_fw_init_admin_resp {
	struct icp_qat_fw_init_admin_resp_hdr init_resp_hdr;
	union {
		__u32 resrvd2;
		struct {
			__u16 version_minor_num;
			__u16 version_major_num;
		} s;
	} u;
	__u64 opaque_data;
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
