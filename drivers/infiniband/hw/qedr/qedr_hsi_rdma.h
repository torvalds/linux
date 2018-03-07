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
#define RDMA_RQ_SGE_L_KEY_MASK      0x3FFFFFF
#define RDMA_RQ_SGE_L_KEY_SHIFT     0
#define RDMA_RQ_SGE_NUM_SGES_MASK   0x7
#define RDMA_RQ_SGE_NUM_SGES_SHIFT  26
#define RDMA_RQ_SGE_RESERVED0_MASK  0x7
#define RDMA_RQ_SGE_RESERVED0_SHIFT 29
};

struct rdma_srq_sge {
	struct regpair addr;
	__le32 length;
	__le32 l_key;
};

/* Rdma doorbell data for flags update */
struct rdma_pwm_flags_data {
	__le16 icid; /* internal CID */
	u8 agg_flags; /* aggregative flags */
	u8 reserved;
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
#define RDMA_PWM_VAL32_DATA_AGG_CMD_MASK		0x3
#define RDMA_PWM_VAL32_DATA_AGG_CMD_SHIFT		0
#define RDMA_PWM_VAL32_DATA_BYPASS_EN_MASK		0x1
#define RDMA_PWM_VAL32_DATA_BYPASS_EN_SHIFT		2
#define RDMA_PWM_VAL32_DATA_CONN_TYPE_IS_IWARP_MASK	0x1
#define RDMA_PWM_VAL32_DATA_CONN_TYPE_IS_IWARP_SHIFT	3
#define RDMA_PWM_VAL32_DATA_SET_16B_VAL_MASK		0x1
#define RDMA_PWM_VAL32_DATA_SET_16B_VAL_SHIFT		4
#define RDMA_PWM_VAL32_DATA_RESERVED_MASK		0x7
#define RDMA_PWM_VAL32_DATA_RESERVED_SHIFT		5
	__le32 value;
};

/* DIF Block size options */
enum rdma_dif_block_size {
	RDMA_DIF_BLOCK_512 = 0,
	RDMA_DIF_BLOCK_4096 = 1,
	MAX_RDMA_DIF_BLOCK_SIZE
};

/* DIF CRC initial value */
enum rdma_dif_crc_seed {
	RDMA_DIF_CRC_SEED_0000 = 0,
	RDMA_DIF_CRC_SEED_FFFF = 1,
	MAX_RDMA_DIF_CRC_SEED
};

/* RDMA DIF Error Result Structure */
struct rdma_dif_error_result {
	__le32 error_intervals;
	__le32 dif_error_1st_interval;
	u8 flags;
#define RDMA_DIF_ERROR_RESULT_DIF_ERROR_TYPE_CRC_MASK      0x1
#define RDMA_DIF_ERROR_RESULT_DIF_ERROR_TYPE_CRC_SHIFT     0
#define RDMA_DIF_ERROR_RESULT_DIF_ERROR_TYPE_APP_TAG_MASK  0x1
#define RDMA_DIF_ERROR_RESULT_DIF_ERROR_TYPE_APP_TAG_SHIFT 1
#define RDMA_DIF_ERROR_RESULT_DIF_ERROR_TYPE_REF_TAG_MASK  0x1
#define RDMA_DIF_ERROR_RESULT_DIF_ERROR_TYPE_REF_TAG_SHIFT 2
#define RDMA_DIF_ERROR_RESULT_RESERVED0_MASK               0xF
#define RDMA_DIF_ERROR_RESULT_RESERVED0_SHIFT              3
#define RDMA_DIF_ERROR_RESULT_TOGGLE_BIT_MASK              0x1
#define RDMA_DIF_ERROR_RESULT_TOGGLE_BIT_SHIFT             7
	u8 reserved1[55];
};

/* DIF IO direction */
enum rdma_dif_io_direction_flg {
	RDMA_DIF_DIR_RX = 0,
	RDMA_DIF_DIR_TX = 1,
	MAX_RDMA_DIF_IO_DIRECTION_FLG
};

/* RDMA DIF Runt Result Structure */
struct rdma_dif_runt_result {
	__le16 guard_tag;
	__le16 reserved[3];
};

/* Memory window type enumeration */
enum rdma_mw_type {
	RDMA_MW_TYPE_1,
	RDMA_MW_TYPE_2A,
	MAX_RDMA_MW_TYPE
};

struct rdma_sq_atomic_wqe {
	__le32 reserved1;
	__le32 length;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_ATOMIC_WQE_COMP_FLG_MASK         0x1
#define RDMA_SQ_ATOMIC_WQE_COMP_FLG_SHIFT        0
#define RDMA_SQ_ATOMIC_WQE_RD_FENCE_FLG_MASK     0x1
#define RDMA_SQ_ATOMIC_WQE_RD_FENCE_FLG_SHIFT    1
#define RDMA_SQ_ATOMIC_WQE_INV_FENCE_FLG_MASK    0x1
#define RDMA_SQ_ATOMIC_WQE_INV_FENCE_FLG_SHIFT   2
#define RDMA_SQ_ATOMIC_WQE_SE_FLG_MASK           0x1
#define RDMA_SQ_ATOMIC_WQE_SE_FLG_SHIFT          3
#define RDMA_SQ_ATOMIC_WQE_INLINE_FLG_MASK       0x1
#define RDMA_SQ_ATOMIC_WQE_INLINE_FLG_SHIFT      4
#define RDMA_SQ_ATOMIC_WQE_DIF_ON_HOST_FLG_MASK  0x1
#define RDMA_SQ_ATOMIC_WQE_DIF_ON_HOST_FLG_SHIFT 5
#define RDMA_SQ_ATOMIC_WQE_RESERVED0_MASK        0x3
#define RDMA_SQ_ATOMIC_WQE_RESERVED0_SHIFT       6
	u8 wqe_size;
	u8 prev_wqe_size;
	struct regpair remote_va;
	__le32 r_key;
	__le32 reserved2;
	struct regpair cmp_data;
	struct regpair swap_data;
};

/* First element (16 bytes) of atomic wqe */
struct rdma_sq_atomic_wqe_1st {
	__le32 reserved1;
	__le32 length;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_ATOMIC_WQE_1ST_COMP_FLG_MASK       0x1
#define RDMA_SQ_ATOMIC_WQE_1ST_COMP_FLG_SHIFT      0
#define RDMA_SQ_ATOMIC_WQE_1ST_RD_FENCE_FLG_MASK   0x1
#define RDMA_SQ_ATOMIC_WQE_1ST_RD_FENCE_FLG_SHIFT  1
#define RDMA_SQ_ATOMIC_WQE_1ST_INV_FENCE_FLG_MASK  0x1
#define RDMA_SQ_ATOMIC_WQE_1ST_INV_FENCE_FLG_SHIFT 2
#define RDMA_SQ_ATOMIC_WQE_1ST_SE_FLG_MASK         0x1
#define RDMA_SQ_ATOMIC_WQE_1ST_SE_FLG_SHIFT        3
#define RDMA_SQ_ATOMIC_WQE_1ST_INLINE_FLG_MASK     0x1
#define RDMA_SQ_ATOMIC_WQE_1ST_INLINE_FLG_SHIFT    4
#define RDMA_SQ_ATOMIC_WQE_1ST_RESERVED0_MASK      0x7
#define RDMA_SQ_ATOMIC_WQE_1ST_RESERVED0_SHIFT     5
	u8 wqe_size;
	u8 prev_wqe_size;
};

/* Second element (16 bytes) of atomic wqe */
struct rdma_sq_atomic_wqe_2nd {
	struct regpair remote_va;
	__le32 r_key;
	__le32 reserved2;
};

/* Third element (16 bytes) of atomic wqe */
struct rdma_sq_atomic_wqe_3rd {
	struct regpair cmp_data;
	struct regpair swap_data;
};

struct rdma_sq_bind_wqe {
	struct regpair addr;
	__le32 l_key;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_BIND_WQE_COMP_FLG_MASK       0x1
#define RDMA_SQ_BIND_WQE_COMP_FLG_SHIFT      0
#define RDMA_SQ_BIND_WQE_RD_FENCE_FLG_MASK   0x1
#define RDMA_SQ_BIND_WQE_RD_FENCE_FLG_SHIFT  1
#define RDMA_SQ_BIND_WQE_INV_FENCE_FLG_MASK  0x1
#define RDMA_SQ_BIND_WQE_INV_FENCE_FLG_SHIFT 2
#define RDMA_SQ_BIND_WQE_SE_FLG_MASK         0x1
#define RDMA_SQ_BIND_WQE_SE_FLG_SHIFT        3
#define RDMA_SQ_BIND_WQE_INLINE_FLG_MASK     0x1
#define RDMA_SQ_BIND_WQE_INLINE_FLG_SHIFT    4
#define RDMA_SQ_BIND_WQE_RESERVED0_MASK      0x7
#define RDMA_SQ_BIND_WQE_RESERVED0_SHIFT     5
	u8 wqe_size;
	u8 prev_wqe_size;
	u8 bind_ctrl;
#define RDMA_SQ_BIND_WQE_ZERO_BASED_MASK     0x1
#define RDMA_SQ_BIND_WQE_ZERO_BASED_SHIFT    0
#define RDMA_SQ_BIND_WQE_MW_TYPE_MASK        0x1
#define RDMA_SQ_BIND_WQE_MW_TYPE_SHIFT       1
#define RDMA_SQ_BIND_WQE_RESERVED1_MASK      0x3F
#define RDMA_SQ_BIND_WQE_RESERVED1_SHIFT     2
	u8 access_ctrl;
#define RDMA_SQ_BIND_WQE_REMOTE_READ_MASK    0x1
#define RDMA_SQ_BIND_WQE_REMOTE_READ_SHIFT   0
#define RDMA_SQ_BIND_WQE_REMOTE_WRITE_MASK   0x1
#define RDMA_SQ_BIND_WQE_REMOTE_WRITE_SHIFT  1
#define RDMA_SQ_BIND_WQE_ENABLE_ATOMIC_MASK  0x1
#define RDMA_SQ_BIND_WQE_ENABLE_ATOMIC_SHIFT 2
#define RDMA_SQ_BIND_WQE_LOCAL_READ_MASK     0x1
#define RDMA_SQ_BIND_WQE_LOCAL_READ_SHIFT    3
#define RDMA_SQ_BIND_WQE_LOCAL_WRITE_MASK    0x1
#define RDMA_SQ_BIND_WQE_LOCAL_WRITE_SHIFT   4
#define RDMA_SQ_BIND_WQE_RESERVED2_MASK      0x7
#define RDMA_SQ_BIND_WQE_RESERVED2_SHIFT     5
	u8 reserved3;
	u8 length_hi;
	__le32 length_lo;
	__le32 parent_l_key;
	__le32 reserved4;
};

/* First element (16 bytes) of bind wqe */
struct rdma_sq_bind_wqe_1st {
	struct regpair addr;
	__le32 l_key;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_BIND_WQE_1ST_COMP_FLG_MASK       0x1
#define RDMA_SQ_BIND_WQE_1ST_COMP_FLG_SHIFT      0
#define RDMA_SQ_BIND_WQE_1ST_RD_FENCE_FLG_MASK   0x1
#define RDMA_SQ_BIND_WQE_1ST_RD_FENCE_FLG_SHIFT  1
#define RDMA_SQ_BIND_WQE_1ST_INV_FENCE_FLG_MASK  0x1
#define RDMA_SQ_BIND_WQE_1ST_INV_FENCE_FLG_SHIFT 2
#define RDMA_SQ_BIND_WQE_1ST_SE_FLG_MASK         0x1
#define RDMA_SQ_BIND_WQE_1ST_SE_FLG_SHIFT        3
#define RDMA_SQ_BIND_WQE_1ST_INLINE_FLG_MASK     0x1
#define RDMA_SQ_BIND_WQE_1ST_INLINE_FLG_SHIFT    4
#define RDMA_SQ_BIND_WQE_1ST_RESERVED0_MASK      0x7
#define RDMA_SQ_BIND_WQE_1ST_RESERVED0_SHIFT     5
	u8 wqe_size;
	u8 prev_wqe_size;
};

/* Second element (16 bytes) of bind wqe */
struct rdma_sq_bind_wqe_2nd {
	u8 bind_ctrl;
#define RDMA_SQ_BIND_WQE_2ND_ZERO_BASED_MASK     0x1
#define RDMA_SQ_BIND_WQE_2ND_ZERO_BASED_SHIFT    0
#define RDMA_SQ_BIND_WQE_2ND_MW_TYPE_MASK        0x1
#define RDMA_SQ_BIND_WQE_2ND_MW_TYPE_SHIFT       1
#define RDMA_SQ_BIND_WQE_2ND_RESERVED1_MASK      0x3F
#define RDMA_SQ_BIND_WQE_2ND_RESERVED1_SHIFT     2
	u8 access_ctrl;
#define RDMA_SQ_BIND_WQE_2ND_REMOTE_READ_MASK    0x1
#define RDMA_SQ_BIND_WQE_2ND_REMOTE_READ_SHIFT   0
#define RDMA_SQ_BIND_WQE_2ND_REMOTE_WRITE_MASK   0x1
#define RDMA_SQ_BIND_WQE_2ND_REMOTE_WRITE_SHIFT  1
#define RDMA_SQ_BIND_WQE_2ND_ENABLE_ATOMIC_MASK  0x1
#define RDMA_SQ_BIND_WQE_2ND_ENABLE_ATOMIC_SHIFT 2
#define RDMA_SQ_BIND_WQE_2ND_LOCAL_READ_MASK     0x1
#define RDMA_SQ_BIND_WQE_2ND_LOCAL_READ_SHIFT    3
#define RDMA_SQ_BIND_WQE_2ND_LOCAL_WRITE_MASK    0x1
#define RDMA_SQ_BIND_WQE_2ND_LOCAL_WRITE_SHIFT   4
#define RDMA_SQ_BIND_WQE_2ND_RESERVED2_MASK      0x7
#define RDMA_SQ_BIND_WQE_2ND_RESERVED2_SHIFT     5
	u8 reserved3;
	u8 length_hi;
	__le32 length_lo;
	__le32 parent_l_key;
	__le32 reserved4;
};

/* Structure with only the SQ WQE common
 * fields. Size is of one SQ element (16B)
 */
struct rdma_sq_common_wqe {
	__le32 reserved1[3];
	u8 req_type;
	u8 flags;
#define RDMA_SQ_COMMON_WQE_COMP_FLG_MASK       0x1
#define RDMA_SQ_COMMON_WQE_COMP_FLG_SHIFT      0
#define RDMA_SQ_COMMON_WQE_RD_FENCE_FLG_MASK   0x1
#define RDMA_SQ_COMMON_WQE_RD_FENCE_FLG_SHIFT  1
#define RDMA_SQ_COMMON_WQE_INV_FENCE_FLG_MASK  0x1
#define RDMA_SQ_COMMON_WQE_INV_FENCE_FLG_SHIFT 2
#define RDMA_SQ_COMMON_WQE_SE_FLG_MASK         0x1
#define RDMA_SQ_COMMON_WQE_SE_FLG_SHIFT        3
#define RDMA_SQ_COMMON_WQE_INLINE_FLG_MASK     0x1
#define RDMA_SQ_COMMON_WQE_INLINE_FLG_SHIFT    4
#define RDMA_SQ_COMMON_WQE_RESERVED0_MASK      0x7
#define RDMA_SQ_COMMON_WQE_RESERVED0_SHIFT     5
	u8 wqe_size;
	u8 prev_wqe_size;
};

struct rdma_sq_fmr_wqe {
	struct regpair addr;
	__le32 l_key;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_FMR_WQE_COMP_FLG_MASK                0x1
#define RDMA_SQ_FMR_WQE_COMP_FLG_SHIFT               0
#define RDMA_SQ_FMR_WQE_RD_FENCE_FLG_MASK            0x1
#define RDMA_SQ_FMR_WQE_RD_FENCE_FLG_SHIFT           1
#define RDMA_SQ_FMR_WQE_INV_FENCE_FLG_MASK           0x1
#define RDMA_SQ_FMR_WQE_INV_FENCE_FLG_SHIFT          2
#define RDMA_SQ_FMR_WQE_SE_FLG_MASK                  0x1
#define RDMA_SQ_FMR_WQE_SE_FLG_SHIFT                 3
#define RDMA_SQ_FMR_WQE_INLINE_FLG_MASK              0x1
#define RDMA_SQ_FMR_WQE_INLINE_FLG_SHIFT             4
#define RDMA_SQ_FMR_WQE_DIF_ON_HOST_FLG_MASK         0x1
#define RDMA_SQ_FMR_WQE_DIF_ON_HOST_FLG_SHIFT        5
#define RDMA_SQ_FMR_WQE_RESERVED0_MASK               0x3
#define RDMA_SQ_FMR_WQE_RESERVED0_SHIFT              6
	u8 wqe_size;
	u8 prev_wqe_size;
	u8 fmr_ctrl;
#define RDMA_SQ_FMR_WQE_PAGE_SIZE_LOG_MASK           0x1F
#define RDMA_SQ_FMR_WQE_PAGE_SIZE_LOG_SHIFT          0
#define RDMA_SQ_FMR_WQE_ZERO_BASED_MASK              0x1
#define RDMA_SQ_FMR_WQE_ZERO_BASED_SHIFT             5
#define RDMA_SQ_FMR_WQE_BIND_EN_MASK                 0x1
#define RDMA_SQ_FMR_WQE_BIND_EN_SHIFT                6
#define RDMA_SQ_FMR_WQE_RESERVED1_MASK               0x1
#define RDMA_SQ_FMR_WQE_RESERVED1_SHIFT              7
	u8 access_ctrl;
#define RDMA_SQ_FMR_WQE_REMOTE_READ_MASK             0x1
#define RDMA_SQ_FMR_WQE_REMOTE_READ_SHIFT            0
#define RDMA_SQ_FMR_WQE_REMOTE_WRITE_MASK            0x1
#define RDMA_SQ_FMR_WQE_REMOTE_WRITE_SHIFT           1
#define RDMA_SQ_FMR_WQE_ENABLE_ATOMIC_MASK           0x1
#define RDMA_SQ_FMR_WQE_ENABLE_ATOMIC_SHIFT          2
#define RDMA_SQ_FMR_WQE_LOCAL_READ_MASK              0x1
#define RDMA_SQ_FMR_WQE_LOCAL_READ_SHIFT             3
#define RDMA_SQ_FMR_WQE_LOCAL_WRITE_MASK             0x1
#define RDMA_SQ_FMR_WQE_LOCAL_WRITE_SHIFT            4
#define RDMA_SQ_FMR_WQE_RESERVED2_MASK               0x7
#define RDMA_SQ_FMR_WQE_RESERVED2_SHIFT              5
	u8 reserved3;
	u8 length_hi;
	__le32 length_lo;
	struct regpair pbl_addr;
	__le32 dif_base_ref_tag;
	__le16 dif_app_tag;
	__le16 dif_app_tag_mask;
	__le16 dif_runt_crc_value;
	__le16 dif_flags;
#define RDMA_SQ_FMR_WQE_DIF_IO_DIRECTION_FLG_MASK	0x1
#define RDMA_SQ_FMR_WQE_DIF_IO_DIRECTION_FLG_SHIFT	0
#define RDMA_SQ_FMR_WQE_DIF_BLOCK_SIZE_MASK		0x1
#define RDMA_SQ_FMR_WQE_DIF_BLOCK_SIZE_SHIFT		1
#define RDMA_SQ_FMR_WQE_DIF_RUNT_VALID_FLG_MASK		0x1
#define RDMA_SQ_FMR_WQE_DIF_RUNT_VALID_FLG_SHIFT	2
#define RDMA_SQ_FMR_WQE_DIF_VALIDATE_CRC_GUARD_MASK	0x1
#define RDMA_SQ_FMR_WQE_DIF_VALIDATE_CRC_GUARD_SHIFT	3
#define RDMA_SQ_FMR_WQE_DIF_VALIDATE_REF_TAG_MASK	0x1
#define RDMA_SQ_FMR_WQE_DIF_VALIDATE_REF_TAG_SHIFT	4
#define RDMA_SQ_FMR_WQE_DIF_VALIDATE_APP_TAG_MASK	0x1
#define RDMA_SQ_FMR_WQE_DIF_VALIDATE_APP_TAG_SHIFT	5
#define RDMA_SQ_FMR_WQE_DIF_CRC_SEED_MASK		0x1
#define RDMA_SQ_FMR_WQE_DIF_CRC_SEED_SHIFT		6
#define RDMA_SQ_FMR_WQE_DIF_RX_REF_TAG_CONST_MASK	0x1
#define RDMA_SQ_FMR_WQE_DIF_RX_REF_TAG_CONST_SHIFT	7
#define RDMA_SQ_FMR_WQE_RESERVED4_MASK			0xFF
#define RDMA_SQ_FMR_WQE_RESERVED4_SHIFT			8
	__le32 reserved5;
};

/* First element (16 bytes) of fmr wqe */
struct rdma_sq_fmr_wqe_1st {
	struct regpair addr;
	__le32 l_key;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_FMR_WQE_1ST_COMP_FLG_MASK         0x1
#define RDMA_SQ_FMR_WQE_1ST_COMP_FLG_SHIFT        0
#define RDMA_SQ_FMR_WQE_1ST_RD_FENCE_FLG_MASK     0x1
#define RDMA_SQ_FMR_WQE_1ST_RD_FENCE_FLG_SHIFT    1
#define RDMA_SQ_FMR_WQE_1ST_INV_FENCE_FLG_MASK    0x1
#define RDMA_SQ_FMR_WQE_1ST_INV_FENCE_FLG_SHIFT   2
#define RDMA_SQ_FMR_WQE_1ST_SE_FLG_MASK           0x1
#define RDMA_SQ_FMR_WQE_1ST_SE_FLG_SHIFT          3
#define RDMA_SQ_FMR_WQE_1ST_INLINE_FLG_MASK       0x1
#define RDMA_SQ_FMR_WQE_1ST_INLINE_FLG_SHIFT      4
#define RDMA_SQ_FMR_WQE_1ST_DIF_ON_HOST_FLG_MASK  0x1
#define RDMA_SQ_FMR_WQE_1ST_DIF_ON_HOST_FLG_SHIFT 5
#define RDMA_SQ_FMR_WQE_1ST_RESERVED0_MASK        0x3
#define RDMA_SQ_FMR_WQE_1ST_RESERVED0_SHIFT       6
	u8 wqe_size;
	u8 prev_wqe_size;
};

/* Second element (16 bytes) of fmr wqe */
struct rdma_sq_fmr_wqe_2nd {
	u8 fmr_ctrl;
#define RDMA_SQ_FMR_WQE_2ND_PAGE_SIZE_LOG_MASK  0x1F
#define RDMA_SQ_FMR_WQE_2ND_PAGE_SIZE_LOG_SHIFT 0
#define RDMA_SQ_FMR_WQE_2ND_ZERO_BASED_MASK     0x1
#define RDMA_SQ_FMR_WQE_2ND_ZERO_BASED_SHIFT    5
#define RDMA_SQ_FMR_WQE_2ND_BIND_EN_MASK        0x1
#define RDMA_SQ_FMR_WQE_2ND_BIND_EN_SHIFT       6
#define RDMA_SQ_FMR_WQE_2ND_RESERVED1_MASK      0x1
#define RDMA_SQ_FMR_WQE_2ND_RESERVED1_SHIFT     7
	u8 access_ctrl;
#define RDMA_SQ_FMR_WQE_2ND_REMOTE_READ_MASK    0x1
#define RDMA_SQ_FMR_WQE_2ND_REMOTE_READ_SHIFT   0
#define RDMA_SQ_FMR_WQE_2ND_REMOTE_WRITE_MASK   0x1
#define RDMA_SQ_FMR_WQE_2ND_REMOTE_WRITE_SHIFT  1
#define RDMA_SQ_FMR_WQE_2ND_ENABLE_ATOMIC_MASK  0x1
#define RDMA_SQ_FMR_WQE_2ND_ENABLE_ATOMIC_SHIFT 2
#define RDMA_SQ_FMR_WQE_2ND_LOCAL_READ_MASK     0x1
#define RDMA_SQ_FMR_WQE_2ND_LOCAL_READ_SHIFT    3
#define RDMA_SQ_FMR_WQE_2ND_LOCAL_WRITE_MASK    0x1
#define RDMA_SQ_FMR_WQE_2ND_LOCAL_WRITE_SHIFT   4
#define RDMA_SQ_FMR_WQE_2ND_RESERVED2_MASK      0x7
#define RDMA_SQ_FMR_WQE_2ND_RESERVED2_SHIFT     5
	u8 reserved3;
	u8 length_hi;
	__le32 length_lo;
	struct regpair pbl_addr;
};

/* Third element (16 bytes) of fmr wqe */
struct rdma_sq_fmr_wqe_3rd {
	__le32 dif_base_ref_tag;
	__le16 dif_app_tag;
	__le16 dif_app_tag_mask;
	__le16 dif_runt_crc_value;
	__le16 dif_flags;
#define RDMA_SQ_FMR_WQE_3RD_DIF_IO_DIRECTION_FLG_MASK		0x1
#define RDMA_SQ_FMR_WQE_3RD_DIF_IO_DIRECTION_FLG_SHIFT		0
#define RDMA_SQ_FMR_WQE_3RD_DIF_BLOCK_SIZE_MASK			0x1
#define RDMA_SQ_FMR_WQE_3RD_DIF_BLOCK_SIZE_SHIFT		1
#define RDMA_SQ_FMR_WQE_3RD_DIF_RUNT_VALID_FLG_MASK		0x1
#define RDMA_SQ_FMR_WQE_3RD_DIF_RUNT_VALID_FLG_SHIFT		2
#define RDMA_SQ_FMR_WQE_3RD_DIF_VALIDATE_CRC_GUARD_MASK		0x1
#define RDMA_SQ_FMR_WQE_3RD_DIF_VALIDATE_CRC_GUARD_SHIFT	3
#define RDMA_SQ_FMR_WQE_3RD_DIF_VALIDATE_REF_TAG_MASK		0x1
#define RDMA_SQ_FMR_WQE_3RD_DIF_VALIDATE_REF_TAG_SHIFT		4
#define RDMA_SQ_FMR_WQE_3RD_DIF_VALIDATE_APP_TAG_MASK		0x1
#define RDMA_SQ_FMR_WQE_3RD_DIF_VALIDATE_APP_TAG_SHIFT		5
#define RDMA_SQ_FMR_WQE_3RD_DIF_CRC_SEED_MASK			0x1
#define RDMA_SQ_FMR_WQE_3RD_DIF_CRC_SEED_SHIFT			6
#define RDMA_SQ_FMR_WQE_3RD_DIF_RX_REF_TAG_CONST_MASK		0x1
#define RDMA_SQ_FMR_WQE_3RD_DIF_RX_REF_TAG_CONST_SHIFT		7
#define RDMA_SQ_FMR_WQE_3RD_RESERVED4_MASK			0xFF
#define RDMA_SQ_FMR_WQE_RESERVED4_SHIFT				8
	__le32 reserved5;
};

struct rdma_sq_local_inv_wqe {
	struct regpair reserved;
	__le32 inv_l_key;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_LOCAL_INV_WQE_COMP_FLG_MASK         0x1
#define RDMA_SQ_LOCAL_INV_WQE_COMP_FLG_SHIFT        0
#define RDMA_SQ_LOCAL_INV_WQE_RD_FENCE_FLG_MASK     0x1
#define RDMA_SQ_LOCAL_INV_WQE_RD_FENCE_FLG_SHIFT    1
#define RDMA_SQ_LOCAL_INV_WQE_INV_FENCE_FLG_MASK    0x1
#define RDMA_SQ_LOCAL_INV_WQE_INV_FENCE_FLG_SHIFT   2
#define RDMA_SQ_LOCAL_INV_WQE_SE_FLG_MASK           0x1
#define RDMA_SQ_LOCAL_INV_WQE_SE_FLG_SHIFT          3
#define RDMA_SQ_LOCAL_INV_WQE_INLINE_FLG_MASK       0x1
#define RDMA_SQ_LOCAL_INV_WQE_INLINE_FLG_SHIFT      4
#define RDMA_SQ_LOCAL_INV_WQE_DIF_ON_HOST_FLG_MASK  0x1
#define RDMA_SQ_LOCAL_INV_WQE_DIF_ON_HOST_FLG_SHIFT 5
#define RDMA_SQ_LOCAL_INV_WQE_RESERVED0_MASK        0x3
#define RDMA_SQ_LOCAL_INV_WQE_RESERVED0_SHIFT       6
	u8 wqe_size;
	u8 prev_wqe_size;
};

struct rdma_sq_rdma_wqe {
	__le32 imm_data;
	__le32 length;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_RDMA_WQE_COMP_FLG_MASK		0x1
#define RDMA_SQ_RDMA_WQE_COMP_FLG_SHIFT		0
#define RDMA_SQ_RDMA_WQE_RD_FENCE_FLG_MASK	0x1
#define RDMA_SQ_RDMA_WQE_RD_FENCE_FLG_SHIFT	1
#define RDMA_SQ_RDMA_WQE_INV_FENCE_FLG_MASK	0x1
#define RDMA_SQ_RDMA_WQE_INV_FENCE_FLG_SHIFT	2
#define RDMA_SQ_RDMA_WQE_SE_FLG_MASK		0x1
#define RDMA_SQ_RDMA_WQE_SE_FLG_SHIFT		3
#define RDMA_SQ_RDMA_WQE_INLINE_FLG_MASK	0x1
#define RDMA_SQ_RDMA_WQE_INLINE_FLG_SHIFT	4
#define RDMA_SQ_RDMA_WQE_DIF_ON_HOST_FLG_MASK	0x1
#define RDMA_SQ_RDMA_WQE_DIF_ON_HOST_FLG_SHIFT	5
#define RDMA_SQ_RDMA_WQE_READ_INV_FLG_MASK	0x1
#define RDMA_SQ_RDMA_WQE_READ_INV_FLG_SHIFT	6
#define RDMA_SQ_RDMA_WQE_RESERVED0_MASK		0x1
#define RDMA_SQ_RDMA_WQE_RESERVED0_SHIFT	7
	u8 wqe_size;
	u8 prev_wqe_size;
	struct regpair remote_va;
	__le32 r_key;
	u8 dif_flags;
#define RDMA_SQ_RDMA_WQE_DIF_BLOCK_SIZE_MASK            0x1
#define RDMA_SQ_RDMA_WQE_DIF_BLOCK_SIZE_SHIFT           0
#define RDMA_SQ_RDMA_WQE_DIF_FIRST_RDMA_IN_IO_FLG_MASK  0x1
#define RDMA_SQ_RDMA_WQE_DIF_FIRST_RDMA_IN_IO_FLG_SHIFT 1
#define RDMA_SQ_RDMA_WQE_DIF_LAST_RDMA_IN_IO_FLG_MASK   0x1
#define RDMA_SQ_RDMA_WQE_DIF_LAST_RDMA_IN_IO_FLG_SHIFT  2
#define RDMA_SQ_RDMA_WQE_RESERVED1_MASK                 0x1F
#define RDMA_SQ_RDMA_WQE_RESERVED1_SHIFT                3
	u8 reserved2[3];
};

/* First element (16 bytes) of rdma wqe */
struct rdma_sq_rdma_wqe_1st {
	__le32 imm_data;
	__le32 length;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_RDMA_WQE_1ST_COMP_FLG_MASK         0x1
#define RDMA_SQ_RDMA_WQE_1ST_COMP_FLG_SHIFT        0
#define RDMA_SQ_RDMA_WQE_1ST_RD_FENCE_FLG_MASK     0x1
#define RDMA_SQ_RDMA_WQE_1ST_RD_FENCE_FLG_SHIFT    1
#define RDMA_SQ_RDMA_WQE_1ST_INV_FENCE_FLG_MASK    0x1
#define RDMA_SQ_RDMA_WQE_1ST_INV_FENCE_FLG_SHIFT   2
#define RDMA_SQ_RDMA_WQE_1ST_SE_FLG_MASK           0x1
#define RDMA_SQ_RDMA_WQE_1ST_SE_FLG_SHIFT          3
#define RDMA_SQ_RDMA_WQE_1ST_INLINE_FLG_MASK       0x1
#define RDMA_SQ_RDMA_WQE_1ST_INLINE_FLG_SHIFT      4
#define RDMA_SQ_RDMA_WQE_1ST_DIF_ON_HOST_FLG_MASK  0x1
#define RDMA_SQ_RDMA_WQE_1ST_DIF_ON_HOST_FLG_SHIFT 5
#define RDMA_SQ_RDMA_WQE_1ST_READ_INV_FLG_MASK     0x1
#define RDMA_SQ_RDMA_WQE_1ST_READ_INV_FLG_SHIFT    6
#define RDMA_SQ_RDMA_WQE_1ST_RESERVED0_MASK        0x1
#define RDMA_SQ_RDMA_WQE_1ST_RESERVED0_SHIFT       7
	u8 wqe_size;
	u8 prev_wqe_size;
};

/* Second element (16 bytes) of rdma wqe */
struct rdma_sq_rdma_wqe_2nd {
	struct regpair remote_va;
	__le32 r_key;
	u8 dif_flags;
#define RDMA_SQ_RDMA_WQE_2ND_DIF_BLOCK_SIZE_MASK         0x1
#define RDMA_SQ_RDMA_WQE_2ND_DIF_BLOCK_SIZE_SHIFT        0
#define RDMA_SQ_RDMA_WQE_2ND_DIF_FIRST_SEGMENT_FLG_MASK  0x1
#define RDMA_SQ_RDMA_WQE_2ND_DIF_FIRST_SEGMENT_FLG_SHIFT 1
#define RDMA_SQ_RDMA_WQE_2ND_DIF_LAST_SEGMENT_FLG_MASK   0x1
#define RDMA_SQ_RDMA_WQE_2ND_DIF_LAST_SEGMENT_FLG_SHIFT  2
#define RDMA_SQ_RDMA_WQE_2ND_RESERVED1_MASK              0x1F
#define RDMA_SQ_RDMA_WQE_2ND_RESERVED1_SHIFT             3
	u8 reserved2[3];
};

/* SQ WQE req type enumeration */
enum rdma_sq_req_type {
	RDMA_SQ_REQ_TYPE_SEND,
	RDMA_SQ_REQ_TYPE_SEND_WITH_IMM,
	RDMA_SQ_REQ_TYPE_SEND_WITH_INVALIDATE,
	RDMA_SQ_REQ_TYPE_RDMA_WR,
	RDMA_SQ_REQ_TYPE_RDMA_WR_WITH_IMM,
	RDMA_SQ_REQ_TYPE_RDMA_RD,
	RDMA_SQ_REQ_TYPE_ATOMIC_CMP_AND_SWAP,
	RDMA_SQ_REQ_TYPE_ATOMIC_ADD,
	RDMA_SQ_REQ_TYPE_LOCAL_INVALIDATE,
	RDMA_SQ_REQ_TYPE_FAST_MR,
	RDMA_SQ_REQ_TYPE_BIND,
	RDMA_SQ_REQ_TYPE_INVALID,
	MAX_RDMA_SQ_REQ_TYPE
};

struct rdma_sq_send_wqe {
	__le32 inv_key_or_imm_data;
	__le32 length;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_SEND_WQE_COMP_FLG_MASK         0x1
#define RDMA_SQ_SEND_WQE_COMP_FLG_SHIFT        0
#define RDMA_SQ_SEND_WQE_RD_FENCE_FLG_MASK     0x1
#define RDMA_SQ_SEND_WQE_RD_FENCE_FLG_SHIFT    1
#define RDMA_SQ_SEND_WQE_INV_FENCE_FLG_MASK    0x1
#define RDMA_SQ_SEND_WQE_INV_FENCE_FLG_SHIFT   2
#define RDMA_SQ_SEND_WQE_SE_FLG_MASK           0x1
#define RDMA_SQ_SEND_WQE_SE_FLG_SHIFT          3
#define RDMA_SQ_SEND_WQE_INLINE_FLG_MASK       0x1
#define RDMA_SQ_SEND_WQE_INLINE_FLG_SHIFT      4
#define RDMA_SQ_SEND_WQE_DIF_ON_HOST_FLG_MASK  0x1
#define RDMA_SQ_SEND_WQE_DIF_ON_HOST_FLG_SHIFT 5
#define RDMA_SQ_SEND_WQE_RESERVED0_MASK        0x3
#define RDMA_SQ_SEND_WQE_RESERVED0_SHIFT       6
	u8 wqe_size;
	u8 prev_wqe_size;
	__le32 reserved1[4];
};

struct rdma_sq_send_wqe_1st {
	__le32 inv_key_or_imm_data;
	__le32 length;
	__le32 xrc_srq;
	u8 req_type;
	u8 flags;
#define RDMA_SQ_SEND_WQE_1ST_COMP_FLG_MASK       0x1
#define RDMA_SQ_SEND_WQE_1ST_COMP_FLG_SHIFT      0
#define RDMA_SQ_SEND_WQE_1ST_RD_FENCE_FLG_MASK   0x1
#define RDMA_SQ_SEND_WQE_1ST_RD_FENCE_FLG_SHIFT  1
#define RDMA_SQ_SEND_WQE_1ST_INV_FENCE_FLG_MASK  0x1
#define RDMA_SQ_SEND_WQE_1ST_INV_FENCE_FLG_SHIFT 2
#define RDMA_SQ_SEND_WQE_1ST_SE_FLG_MASK         0x1
#define RDMA_SQ_SEND_WQE_1ST_SE_FLG_SHIFT        3
#define RDMA_SQ_SEND_WQE_1ST_INLINE_FLG_MASK     0x1
#define RDMA_SQ_SEND_WQE_1ST_INLINE_FLG_SHIFT    4
#define RDMA_SQ_SEND_WQE_1ST_RESERVED0_MASK      0x7
#define RDMA_SQ_SEND_WQE_1ST_RESERVED0_SHIFT     5
	u8 wqe_size;
	u8 prev_wqe_size;
};

struct rdma_sq_send_wqe_2st {
	__le32 reserved1[4];
};

#endif /* __QED_HSI_RDMA__ */
