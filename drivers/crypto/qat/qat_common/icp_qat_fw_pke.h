/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef _ICP_QAT_FW_PKE_
#define _ICP_QAT_FW_PKE_

#include "icp_qat_fw.h"

struct icp_qat_fw_req_hdr_pke_cd_pars {
	__u64 content_desc_addr;
	__u32 content_desc_resrvd;
	__u32 func_id;
};

struct icp_qat_fw_req_pke_mid {
	__u64 opaque;
	__u64 src_data_addr;
	__u64 dest_data_addr;
};

struct icp_qat_fw_req_pke_hdr {
	__u8 resrvd1;
	__u8 resrvd2;
	__u8 service_type;
	__u8 hdr_flags;
	__u16 comn_req_flags;
	__u16 resrvd4;
	struct icp_qat_fw_req_hdr_pke_cd_pars cd_pars;
};

struct icp_qat_fw_pke_request {
	struct icp_qat_fw_req_pke_hdr pke_hdr;
	struct icp_qat_fw_req_pke_mid pke_mid;
	__u8 output_param_count;
	__u8 input_param_count;
	__u16 resrvd1;
	__u32 resrvd2;
	__u64 next_req_adr;
};

struct icp_qat_fw_resp_pke_hdr {
	__u8 resrvd1;
	__u8 resrvd2;
	__u8 response_type;
	__u8 hdr_flags;
	__u16 comn_resp_flags;
	__u16 resrvd4;
};

struct icp_qat_fw_pke_resp {
	struct icp_qat_fw_resp_pke_hdr pke_resp_hdr;
	__u64 opaque;
	__u64 src_data_addr;
	__u64 dest_data_addr;
};

#define ICP_QAT_FW_PKE_HDR_VALID_FLAG_BITPOS              7
#define ICP_QAT_FW_PKE_HDR_VALID_FLAG_MASK                0x1
#define ICP_QAT_FW_PKE_RESP_PKE_STAT_GET(status_word) \
	QAT_FIELD_GET(((status_word >> ICP_QAT_FW_COMN_ONE_BYTE_SHIFT) & \
		ICP_QAT_FW_COMN_SINGLE_BYTE_MASK), \
		QAT_COMN_RESP_PKE_STATUS_BITPOS, \
		QAT_COMN_RESP_PKE_STATUS_MASK)

#define ICP_QAT_FW_PKE_HDR_VALID_FLAG_SET(hdr_t, val) \
	QAT_FIELD_SET((hdr_t.hdr_flags), (val), \
		ICP_QAT_FW_PKE_HDR_VALID_FLAG_BITPOS, \
		ICP_QAT_FW_PKE_HDR_VALID_FLAG_MASK)
#endif
