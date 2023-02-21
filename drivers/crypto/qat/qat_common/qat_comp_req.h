/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2022 Intel Corporation */
#ifndef _QAT_COMP_REQ_H_
#define _QAT_COMP_REQ_H_

#include "icp_qat_fw_comp.h"

#define QAT_COMP_REQ_SIZE (sizeof(struct icp_qat_fw_comp_req))
#define QAT_COMP_CTX_SIZE (QAT_COMP_REQ_SIZE * 2)

static inline void qat_comp_create_req(void *ctx, void *req, u64 src, u32 slen,
				       u64 dst, u32 dlen, u64 opaque)
{
	struct icp_qat_fw_comp_req *fw_tmpl = ctx;
	struct icp_qat_fw_comp_req *fw_req = req;
	struct icp_qat_fw_comp_req_params *req_pars = &fw_req->comp_pars;

	memcpy(fw_req, fw_tmpl, sizeof(*fw_req));
	fw_req->comn_mid.src_data_addr = src;
	fw_req->comn_mid.src_length = slen;
	fw_req->comn_mid.dest_data_addr = dst;
	fw_req->comn_mid.dst_length = dlen;
	fw_req->comn_mid.opaque_data = opaque;
	req_pars->comp_len = slen;
	req_pars->out_buffer_sz = dlen;
}

static inline void qat_comp_override_dst(void *req, u64 dst, u32 dlen)
{
	struct icp_qat_fw_comp_req *fw_req = req;
	struct icp_qat_fw_comp_req_params *req_pars = &fw_req->comp_pars;

	fw_req->comn_mid.dest_data_addr = dst;
	fw_req->comn_mid.dst_length = dlen;
	req_pars->out_buffer_sz = dlen;
}

static inline void qat_comp_create_compression_req(void *ctx, void *req,
						   u64 src, u32 slen,
						   u64 dst, u32 dlen,
						   u64 opaque)
{
	qat_comp_create_req(ctx, req, src, slen, dst, dlen, opaque);
}

static inline void qat_comp_create_decompression_req(void *ctx, void *req,
						     u64 src, u32 slen,
						     u64 dst, u32 dlen,
						     u64 opaque)
{
	struct icp_qat_fw_comp_req *fw_tmpl = ctx;

	fw_tmpl++;
	qat_comp_create_req(fw_tmpl, req, src, slen, dst, dlen, opaque);
}

static inline u32 qat_comp_get_consumed_ctr(void *resp)
{
	struct icp_qat_fw_comp_resp *qat_resp = resp;

	return qat_resp->comp_resp_pars.input_byte_counter;
}

static inline u32 qat_comp_get_produced_ctr(void *resp)
{
	struct icp_qat_fw_comp_resp *qat_resp = resp;

	return qat_resp->comp_resp_pars.output_byte_counter;
}

static inline u32 qat_comp_get_produced_adler32(void *resp)
{
	struct icp_qat_fw_comp_resp *qat_resp = resp;

	return qat_resp->comp_resp_pars.crc.legacy.curr_adler_32;
}

static inline u64 qat_comp_get_opaque(void *resp)
{
	struct icp_qat_fw_comp_resp *qat_resp = resp;

	return qat_resp->opaque_data;
}

static inline s8 qat_comp_get_cmp_err(void *resp)
{
	struct icp_qat_fw_comp_resp *qat_resp = resp;

	return qat_resp->comn_resp.comn_error.cmp_err_code;
}

static inline s8 qat_comp_get_xlt_err(void *resp)
{
	struct icp_qat_fw_comp_resp *qat_resp = resp;

	return qat_resp->comn_resp.comn_error.xlat_err_code;
}

static inline s8 qat_comp_get_cmp_status(void *resp)
{
	struct icp_qat_fw_comp_resp *qat_resp = resp;
	u8 stat_filed = qat_resp->comn_resp.comn_status;

	return ICP_QAT_FW_COMN_RESP_CMP_STAT_GET(stat_filed);
}

static inline s8 qat_comp_get_xlt_status(void *resp)
{
	struct icp_qat_fw_comp_resp *qat_resp = resp;
	u8 stat_filed = qat_resp->comn_resp.comn_status;

	return ICP_QAT_FW_COMN_RESP_XLAT_STAT_GET(stat_filed);
}

static inline u8 qat_comp_get_cmp_cnv_flag(void *resp)
{
	struct icp_qat_fw_comp_resp *qat_resp = resp;
	u8 flags = qat_resp->comn_resp.hdr_flags;

	return ICP_QAT_FW_COMN_HDR_CNV_FLAG_GET(flags);
}

#endif
