/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __QED_HSI_RDMA__
#define __QED_HSI_RDMA__

#include <linux/qed/rdma_common.h>

/* rdma completion notification queue element */
struct rdma_cnqe {
	struct regpair	cq_handle;
};

struct rdma_cqe_responder {
	struct regpair srq_wr_id;
	struct regpair qp_handle;
	__le32 imm_data_or_inv_r_Key;
	__le32 length;
	__le32 imm_data_hi;
	__le16 rq_cons;
	u8 flags;
#define RDMA_CQE_RESPONDER_TOGGLE_BIT_MASK  0x1
#define RDMA_CQE_RESPONDER_TOGGLE_BIT_SHIFT 0
#define RDMA_CQE_RESPONDER_TYPE_MASK        0x3
#define RDMA_CQE_RESPONDER_TYPE_SHIFT       1
#define RDMA_CQE_RESPONDER_INV_FLG_MASK     0x1
#define RDMA_CQE_RESPONDER_INV_FLG_SHIFT    3
#define RDMA_CQE_RESPONDER_IMM_FLG_MASK     0x1
#define RDMA_CQE_RESPONDER_IMM_FLG_SHIFT    4
#define RDMA_CQE_RESPONDER_RDMA_FLG_MASK    0x1
#define RDMA_CQE_RESPONDER_RDMA_FLG_SHIFT   5
#define RDMA_CQE_RESPONDER_RESERVED2_MASK   0x3
#define RDMA_CQE_RESPONDER_RESERVED2_SHIFT  6
	u8 status;
};

struct rdma_cqe_requester {
	__le16 sq_cons;
	__le16 reserved0;
	__le32 reserved1;
	struct regpair qp_handle;
	struct regpair reserved2;
	__le32 reserved3;
	__le16 reserved4;
	u8 flags;
#define RDMA_CQE_REQUESTER_TOGGLE_BIT_MASK  0x1
#define RDMA_CQE_REQUESTER_TOGGLE_BIT_SHIFT 0
#define RDMA_CQE_REQUESTER_TYPE_MASK        0x3
#define RDMA_CQE_REQUESTER_TYPE_SHIFT       1
#define RDMA_CQE_REQUESTER_RESERVED5_MASK   0x1F
#define RDMA_CQE_REQUESTER_RESERVED5_SHIFT  3
	u8 status;
};

struct rdma_cqe_common {
	struct regpair reserved0;
	struct regpair qp_handle;
	__le16 reserved1[7];
	u8 flags;
#define RDMA_CQE_COMMON_TOGGLE_BIT_MASK  0x1
#define RDMA_CQE_COMMON_TOGGLE_BIT_SHIFT 0
#define RDMA_CQE_COMMON_TYPE_MASK        0x3
#define RDMA_CQE_COMMON_TYPE_SHIFT       1
#define RDMA_CQE_COMMON_RESERVED2_MASK   0x1F
#define RDMA_CQE_COMMON_RESERVED2_SHIFT  3
	u8 status;
};

/* rdma completion queue element */
union rdma_cqe {
	struct rdma_cqe_responder resp;
	struct rdma_cqe_requester req;
	struct rdma_cqe_common cmn;
};

/* * CQE requester status enumeration */
enum rdma_cqe_requester_status_enum {
	RDMA_CQE_REQ_STS_OK,
	RDMA_CQE_REQ_STS_BAD_RESPONSE_ERR,
	RDMA_CQE_REQ_STS_LOCAL_LENGTH_ERR,
	RDMA_CQE_REQ_STS_LOCAL_QP_OPERATION_ERR,
	RDMA_CQE_REQ_STS_LOCAL_PROTECTION_ERR,
	RDMA_CQE_REQ_STS_MEMORY_MGT_OPERATION_ERR,
	RDMA_CQE_REQ_STS_REMOTE_INVALID_REQUEST_ERR,
	RDMA_CQE_REQ_STS_REMOTE_ACCESS_ERR,
	RDMA_CQE_REQ_STS_REMOTE_OPERATION_ERR,
	RDMA_CQE_REQ_STS_RNR_NAK_RETRY_CNT_ERR,
	RDMA_CQE_REQ_STS_TRANSPORT_RETRY_CNT_ERR,
	RDMA_CQE_REQ_STS_WORK_REQUEST_FLUSHED_ERR,
	MAX_RDMA_CQE_REQUESTER_STATUS_ENUM
};

/* CQE responder status enumeration */
enum rdma_cqe_responder_status_enum {
	RDMA_CQE_RESP_STS_OK,
	RDMA_CQE_RESP_STS_LOCAL_ACCESS_ERR,
	RDMA_CQE_RESP_STS_LOCAL_LENGTH_ERR,
	RDMA_CQE_RESP_STS_LOCAL_QP_OPERATION_ERR,
	RDMA_CQE_RESP_STS_LOCAL_PROTECTION_ERR,
	RDMA_CQE_RESP_STS_MEMORY_MGT_OPERATION_ERR,
	RDMA_CQE_RESP_STS_REMOTE_INVALID_REQUEST_ERR,
	RDMA_CQE_RESP_STS_WORK_REQUEST_FLUSHED_ERR,
	MAX_RDMA_CQE_RESPONDER_STATUS_ENUM
};

/* CQE type enumeration */
enum rdma_cqe_type {
	RDMA_CQE_TYPE_REQUESTER,
	RDMA_CQE_TYPE_RESPONDER_RQ,
	RDMA_CQE_TYPE_RESPONDER_SRQ,
	RDMA_CQE_TYPE_INVALID,
	MAX_RDMA_CQE_TYPE
};

struct rdma_sq_sge {
	__le32 length;
	struct regpair	addr;
	__le32 l_key;
};

struct rdma_rq_sge {
	struct regpair addr;
	__le32 length;
	__le32 flags;
};

struct rdma_srq_sge {
	struct regpair addr;
	__le32 length;
	__le32 l_key;
};

/* Rdma doorbell data for SQ and RQ */
struct rdma_pwm_val16_data {
	__le16 icid;
	__le16 value;
};

union rdma_pwm_val16_data_union {
	struct rdma_pwm_val16_data as_struct;
	__le32 as_dword;
};

/* Rdma doorbell data for CQ */
struct rdma_pwm_val32_data {
	__le16 icid;
	u8 agg_flags;
	u8 params;
#define RDMA_PWM_VAL32_DATA_AGG_CMD_MASK    0x3
#define RDMA_PWM_VAL32_DATA_AGG_CMD_SHIFT   0
#define RDMA_PWM_VAL32_DATA_BYPASS_EN_MASK  0x1
#define RDMA_PWM_VAL32_DATA_BYPASS_EN_SHIFT 2
#define RDMA_PWM_VAL32_DATA_RESERVED_MASK   0x1F
#define RDMA_PWM_VAL32_DATA_RESERVED_SHIFT  3
	__le32 value;
};

#endif /* __QED_HSI_RDMA__ */
