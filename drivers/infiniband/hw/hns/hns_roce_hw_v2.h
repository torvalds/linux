/*
 * Copyright (c) 2016-2017 Hisilicon Limited.
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
 *        disclaimer in the documentation and/or other materials
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

#ifndef _HNS_ROCE_HW_V2_H
#define _HNS_ROCE_HW_V2_H

#include <linux/bitops.h>

#define HNS_ROCE_VF_QPC_BT_NUM			256
#define HNS_ROCE_VF_SRQC_BT_NUM			64
#define HNS_ROCE_VF_CQC_BT_NUM			64
#define HNS_ROCE_VF_MPT_BT_NUM			64
#define HNS_ROCE_VF_EQC_NUM			64
#define HNS_ROCE_VF_SMAC_NUM			32
#define HNS_ROCE_VF_SGID_NUM			32
#define HNS_ROCE_VF_SL_NUM			8

#define HNS_ROCE_V2_MAX_QP_NUM			0x2000
#define HNS_ROCE_V2_MAX_WQE_NUM			0x8000
#define HNS_ROCE_V2_MAX_CQ_NUM			0x8000
#define HNS_ROCE_V2_MAX_CQE_NUM			0x10000
#define HNS_ROCE_V2_MAX_RQ_SGE_NUM		0x100
#define HNS_ROCE_V2_MAX_SQ_SGE_NUM		0xff
#define HNS_ROCE_V2_MAX_SQ_INLINE		0x20
#define HNS_ROCE_V2_UAR_NUM			256
#define HNS_ROCE_V2_PHY_UAR_NUM			1
#define HNS_ROCE_V2_MAX_IRQ_NUM			65
#define HNS_ROCE_V2_COMP_VEC_NUM		63
#define HNS_ROCE_V2_AEQE_VEC_NUM		1
#define HNS_ROCE_V2_ABNORMAL_VEC_NUM		1
#define HNS_ROCE_V2_MAX_MTPT_NUM		0x8000
#define HNS_ROCE_V2_MAX_MTT_SEGS		0x1000000
#define HNS_ROCE_V2_MAX_CQE_SEGS		0x1000000
#define HNS_ROCE_V2_MAX_PD_NUM			0x1000000
#define HNS_ROCE_V2_MAX_QP_INIT_RDMA		128
#define HNS_ROCE_V2_MAX_QP_DEST_RDMA		128
#define HNS_ROCE_V2_MAX_SQ_DESC_SZ		64
#define HNS_ROCE_V2_MAX_RQ_DESC_SZ		16
#define HNS_ROCE_V2_MAX_SRQ_DESC_SZ		64
#define HNS_ROCE_V2_QPC_ENTRY_SZ		256
#define HNS_ROCE_V2_IRRL_ENTRY_SZ		64
#define HNS_ROCE_V2_TRRL_ENTRY_SZ		48
#define HNS_ROCE_V2_CQC_ENTRY_SZ		64
#define HNS_ROCE_V2_MTPT_ENTRY_SZ		64
#define HNS_ROCE_V2_MTT_ENTRY_SZ		64
#define HNS_ROCE_V2_CQE_ENTRY_SIZE		32
#define HNS_ROCE_V2_PAGE_SIZE_SUPPORTED		0xFFFFF000
#define HNS_ROCE_V2_MAX_INNER_MTPT_NUM		2
#define HNS_ROCE_INVALID_LKEY			0x100
#define HNS_ROCE_CMQ_TX_TIMEOUT			30000
#define HNS_ROCE_V2_UC_RC_SGE_NUM_IN_WQE	2

#define HNS_ROCE_CONTEXT_HOP_NUM		1
#define HNS_ROCE_MTT_HOP_NUM			1
#define HNS_ROCE_CQE_HOP_NUM			1
#define HNS_ROCE_PBL_HOP_NUM			2
#define HNS_ROCE_EQE_HOP_NUM			2

#define HNS_ROCE_V2_GID_INDEX_NUM		256

#define HNS_ROCE_V2_TABLE_CHUNK_SIZE		(1 << 18)

#define HNS_ROCE_CMD_FLAG_IN_VALID_SHIFT	0
#define HNS_ROCE_CMD_FLAG_OUT_VALID_SHIFT	1
#define HNS_ROCE_CMD_FLAG_NEXT_SHIFT		2
#define HNS_ROCE_CMD_FLAG_WR_OR_RD_SHIFT	3
#define HNS_ROCE_CMD_FLAG_NO_INTR_SHIFT		4
#define HNS_ROCE_CMD_FLAG_ERR_INTR_SHIFT	5

#define HNS_ROCE_CMD_FLAG_IN		BIT(HNS_ROCE_CMD_FLAG_IN_VALID_SHIFT)
#define HNS_ROCE_CMD_FLAG_OUT		BIT(HNS_ROCE_CMD_FLAG_OUT_VALID_SHIFT)
#define HNS_ROCE_CMD_FLAG_NEXT		BIT(HNS_ROCE_CMD_FLAG_NEXT_SHIFT)
#define HNS_ROCE_CMD_FLAG_WR		BIT(HNS_ROCE_CMD_FLAG_WR_OR_RD_SHIFT)
#define HNS_ROCE_CMD_FLAG_NO_INTR	BIT(HNS_ROCE_CMD_FLAG_NO_INTR_SHIFT)
#define HNS_ROCE_CMD_FLAG_ERR_INTR	BIT(HNS_ROCE_CMD_FLAG_ERR_INTR_SHIFT)

#define HNS_ROCE_CMQ_DESC_NUM_S		3
#define HNS_ROCE_CMQ_EN_B		16
#define HNS_ROCE_CMQ_ENABLE		BIT(HNS_ROCE_CMQ_EN_B)

#define check_whether_last_step(hop_num, step_idx) \
	((step_idx == 0 && hop_num == HNS_ROCE_HOP_NUM_0) || \
	(step_idx == 1 && hop_num == 1) || \
	(step_idx == 2 && hop_num == 2))

#define CMD_CSQ_DESC_NUM		1024
#define CMD_CRQ_DESC_NUM		1024

enum {
	NO_ARMED = 0x0,
	REG_NXT_CEQE = 0x2,
	REG_NXT_SE_CEQE = 0x3
};

#define V2_CQ_DB_REQ_NOT_SOL			0
#define V2_CQ_DB_REQ_NOT			1

#define V2_CQ_STATE_VALID			1
#define V2_QKEY_VAL				0x80010000

#define	GID_LEN_V2				16

#define HNS_ROCE_V2_CQE_QPN_MASK		0x3ffff

enum {
	HNS_ROCE_V2_WQE_OP_SEND				= 0x0,
	HNS_ROCE_V2_WQE_OP_SEND_WITH_INV		= 0x1,
	HNS_ROCE_V2_WQE_OP_SEND_WITH_IMM		= 0x2,
	HNS_ROCE_V2_WQE_OP_RDMA_WRITE			= 0x3,
	HNS_ROCE_V2_WQE_OP_RDMA_WRITE_WITH_IMM		= 0x4,
	HNS_ROCE_V2_WQE_OP_RDMA_READ			= 0x5,
	HNS_ROCE_V2_WQE_OP_ATOM_CMP_AND_SWAP		= 0x6,
	HNS_ROCE_V2_WQE_OP_ATOM_FETCH_AND_ADD		= 0x7,
	HNS_ROCE_V2_WQE_OP_ATOM_MSK_CMP_AND_SWAP	= 0x8,
	HNS_ROCE_V2_WQE_OP_ATOM_MSK_FETCH_AND_ADD	= 0x9,
	HNS_ROCE_V2_WQE_OP_FAST_REG_PMR			= 0xa,
	HNS_ROCE_V2_WQE_OP_LOCAL_INV			= 0xb,
	HNS_ROCE_V2_WQE_OP_BIND_MW_TYPE			= 0xc,
	HNS_ROCE_V2_WQE_OP_MASK				= 0x1f,
};

enum {
	HNS_ROCE_SQ_OPCODE_SEND = 0x0,
	HNS_ROCE_SQ_OPCODE_SEND_WITH_INV = 0x1,
	HNS_ROCE_SQ_OPCODE_SEND_WITH_IMM = 0x2,
	HNS_ROCE_SQ_OPCODE_RDMA_WRITE = 0x3,
	HNS_ROCE_SQ_OPCODE_RDMA_WRITE_WITH_IMM = 0x4,
	HNS_ROCE_SQ_OPCODE_RDMA_READ = 0x5,
	HNS_ROCE_SQ_OPCODE_ATOMIC_COMP_AND_SWAP = 0x6,
	HNS_ROCE_SQ_OPCODE_ATOMIC_FETCH_AND_ADD = 0x7,
	HNS_ROCE_SQ_OPCODE_ATOMIC_MASK_COMP_AND_SWAP = 0x8,
	HNS_ROCE_SQ_OPCODE_ATOMIC_MASK_FETCH_AND_ADD = 0x9,
	HNS_ROCE_SQ_OPCODE_FAST_REG_WR = 0xa,
	HNS_ROCE_SQ_OPCODE_LOCAL_INV = 0xb,
	HNS_ROCE_SQ_OPCODE_BIND_MW = 0xc,
};

enum {
	/* rq operations */
	HNS_ROCE_V2_OPCODE_RDMA_WRITE_IMM = 0x0,
	HNS_ROCE_V2_OPCODE_SEND = 0x1,
	HNS_ROCE_V2_OPCODE_SEND_WITH_IMM = 0x2,
	HNS_ROCE_V2_OPCODE_SEND_WITH_INV = 0x3,
};

enum {
	HNS_ROCE_V2_SQ_DB	= 0x0,
	HNS_ROCE_V2_RQ_DB	= 0x1,
	HNS_ROCE_V2_SRQ_DB	= 0x2,
	HNS_ROCE_V2_CQ_DB_PTR	= 0x3,
	HNS_ROCE_V2_CQ_DB_NTR	= 0x4,
};

enum {
	HNS_ROCE_CQE_V2_SUCCESS				= 0x00,
	HNS_ROCE_CQE_V2_LOCAL_LENGTH_ERR		= 0x01,
	HNS_ROCE_CQE_V2_LOCAL_QP_OP_ERR			= 0x02,
	HNS_ROCE_CQE_V2_LOCAL_PROT_ERR			= 0x04,
	HNS_ROCE_CQE_V2_WR_FLUSH_ERR			= 0x05,
	HNS_ROCE_CQE_V2_MW_BIND_ERR			= 0x06,
	HNS_ROCE_CQE_V2_BAD_RESP_ERR			= 0x10,
	HNS_ROCE_CQE_V2_LOCAL_ACCESS_ERR		= 0x11,
	HNS_ROCE_CQE_V2_REMOTE_INVAL_REQ_ERR		= 0x12,
	HNS_ROCE_CQE_V2_REMOTE_ACCESS_ERR		= 0x13,
	HNS_ROCE_CQE_V2_REMOTE_OP_ERR			= 0x14,
	HNS_ROCE_CQE_V2_TRANSPORT_RETRY_EXC_ERR		= 0x15,
	HNS_ROCE_CQE_V2_RNR_RETRY_EXC_ERR		= 0x16,
	HNS_ROCE_CQE_V2_REMOTE_ABORT_ERR		= 0x22,

	HNS_ROCE_V2_CQE_STATUS_MASK			= 0xff,
};

/* CMQ command */
enum hns_roce_opcode_type {
	HNS_ROCE_OPC_QUERY_HW_VER			= 0x8000,
	HNS_ROCE_OPC_CFG_GLOBAL_PARAM			= 0x8001,
	HNS_ROCE_OPC_ALLOC_PF_RES			= 0x8004,
	HNS_ROCE_OPC_QUERY_PF_RES			= 0x8400,
	HNS_ROCE_OPC_ALLOC_VF_RES			= 0x8401,
	HNS_ROCE_OPC_CFG_EXT_LLM			= 0x8403,
	HNS_ROCE_OPC_CFG_TMOUT_LLM			= 0x8404,
	HNS_ROCE_OPC_CFG_SGID_TB			= 0x8500,
	HNS_ROCE_OPC_CFG_SMAC_TB			= 0x8501,
	HNS_ROCE_OPC_CFG_BT_ATTR			= 0x8506,
};

enum {
	TYPE_CRQ,
	TYPE_CSQ,
};

enum hns_roce_cmd_return_status {
	CMD_EXEC_SUCCESS	= 0,
	CMD_NO_AUTH		= 1,
	CMD_NOT_EXEC		= 2,
	CMD_QUEUE_FULL		= 3,
};

enum hns_roce_sgid_type {
	GID_TYPE_FLAG_ROCE_V1 = 0,
	GID_TYPE_FLAG_ROCE_V2_IPV4,
	GID_TYPE_FLAG_ROCE_V2_IPV6,
};

struct hns_roce_v2_cq_context {
	__le32	byte_4_pg_ceqn;
	__le32	byte_8_cqn;
	__le32	cqe_cur_blk_addr;
	__le32	byte_16_hop_addr;
	__le32	cqe_nxt_blk_addr;
	__le32	byte_24_pgsz_addr;
	__le32	byte_28_cq_pi;
	__le32	byte_32_cq_ci;
	__le32	cqe_ba;
	__le32	byte_40_cqe_ba;
	__le32	byte_44_db_record;
	__le32	db_record_addr;
	__le32	byte_52_cqe_cnt;
	__le32	byte_56_cqe_period_maxcnt;
	__le32	cqe_report_timer;
	__le32	byte_64_se_cqe_idx;
};
#define HNS_ROCE_V2_CQ_DEFAULT_BURST_NUM 0x0
#define HNS_ROCE_V2_CQ_DEFAULT_INTERVAL	0x0

#define	V2_CQC_BYTE_4_CQ_ST_S 0
#define V2_CQC_BYTE_4_CQ_ST_M GENMASK(1, 0)

#define	V2_CQC_BYTE_4_POLL_S 2

#define	V2_CQC_BYTE_4_SE_S 3

#define	V2_CQC_BYTE_4_OVER_IGNORE_S 4

#define	V2_CQC_BYTE_4_COALESCE_S 5

#define	V2_CQC_BYTE_4_ARM_ST_S 6
#define V2_CQC_BYTE_4_ARM_ST_M GENMASK(7, 6)

#define	V2_CQC_BYTE_4_SHIFT_S 8
#define V2_CQC_BYTE_4_SHIFT_M GENMASK(12, 8)

#define	V2_CQC_BYTE_4_CMD_SN_S 13
#define V2_CQC_BYTE_4_CMD_SN_M GENMASK(14, 13)

#define	V2_CQC_BYTE_4_CEQN_S 15
#define V2_CQC_BYTE_4_CEQN_M GENMASK(23, 15)

#define	V2_CQC_BYTE_4_PAGE_OFFSET_S 24
#define V2_CQC_BYTE_4_PAGE_OFFSET_M GENMASK(31, 24)

#define	V2_CQC_BYTE_8_CQN_S 0
#define V2_CQC_BYTE_8_CQN_M GENMASK(23, 0)

#define	V2_CQC_BYTE_16_CQE_CUR_BLK_ADDR_S 0
#define V2_CQC_BYTE_16_CQE_CUR_BLK_ADDR_M GENMASK(19, 0)

#define	V2_CQC_BYTE_16_CQE_HOP_NUM_S 30
#define V2_CQC_BYTE_16_CQE_HOP_NUM_M GENMASK(31, 30)

#define	V2_CQC_BYTE_24_CQE_NXT_BLK_ADDR_S 0
#define V2_CQC_BYTE_24_CQE_NXT_BLK_ADDR_M GENMASK(19, 0)

#define	V2_CQC_BYTE_24_CQE_BA_PG_SZ_S 24
#define V2_CQC_BYTE_24_CQE_BA_PG_SZ_M GENMASK(27, 24)

#define	V2_CQC_BYTE_24_CQE_BUF_PG_SZ_S 28
#define V2_CQC_BYTE_24_CQE_BUF_PG_SZ_M GENMASK(31, 28)

#define	V2_CQC_BYTE_28_CQ_PRODUCER_IDX_S 0
#define V2_CQC_BYTE_28_CQ_PRODUCER_IDX_M GENMASK(23, 0)

#define	V2_CQC_BYTE_32_CQ_CONSUMER_IDX_S 0
#define V2_CQC_BYTE_32_CQ_CONSUMER_IDX_M GENMASK(23, 0)

#define	V2_CQC_BYTE_40_CQE_BA_S 0
#define V2_CQC_BYTE_40_CQE_BA_M GENMASK(28, 0)

#define	V2_CQC_BYTE_44_DB_RECORD_EN_S 0

#define	V2_CQC_BYTE_44_DB_RECORD_ADDR_S 1
#define	V2_CQC_BYTE_44_DB_RECORD_ADDR_M GENMASK(31, 1)

#define	V2_CQC_BYTE_52_CQE_CNT_S 0
#define	V2_CQC_BYTE_52_CQE_CNT_M GENMASK(23, 0)

#define	V2_CQC_BYTE_56_CQ_MAX_CNT_S 0
#define V2_CQC_BYTE_56_CQ_MAX_CNT_M GENMASK(15, 0)

#define	V2_CQC_BYTE_56_CQ_PERIOD_S 16
#define V2_CQC_BYTE_56_CQ_PERIOD_M GENMASK(31, 16)

#define	V2_CQC_BYTE_64_SE_CQE_IDX_S 0
#define	V2_CQC_BYTE_64_SE_CQE_IDX_M GENMASK(23, 0)

enum{
	V2_MPT_ST_VALID = 0x1,
};

enum hns_roce_v2_qp_state {
	HNS_ROCE_QP_ST_RST,
	HNS_ROCE_QP_ST_INIT,
	HNS_ROCE_QP_ST_RTR,
	HNS_ROCE_QP_ST_RTS,
	HNS_ROCE_QP_ST_SQER,
	HNS_ROCE_QP_ST_SQD,
	HNS_ROCE_QP_ST_ERR,
	HNS_ROCE_QP_ST_SQ_DRAINING,
	HNS_ROCE_QP_NUM_ST
};

struct hns_roce_v2_qp_context {
	__le32	byte_4_sqpn_tst;
	__le32	wqe_sge_ba;
	__le32	byte_12_sq_hop;
	__le32	byte_16_buf_ba_pg_sz;
	__le32	byte_20_smac_sgid_idx;
	__le32	byte_24_mtu_tc;
	__le32	byte_28_at_fl;
	u8	dgid[GID_LEN_V2];
	__le32	dmac;
	__le32	byte_52_udpspn_dmac;
	__le32	byte_56_dqpn_err;
	__le32	byte_60_qpst_mapid;
	__le32	qkey_xrcd;
	__le32	byte_68_rq_db;
	__le32	rq_db_record_addr;
	__le32	byte_76_srqn_op_en;
	__le32	byte_80_rnr_rx_cqn;
	__le32	byte_84_rq_ci_pi;
	__le32	rq_cur_blk_addr;
	__le32	byte_92_srq_info;
	__le32	byte_96_rx_reqmsn;
	__le32	rq_nxt_blk_addr;
	__le32	byte_104_rq_sge;
	__le32	byte_108_rx_reqepsn;
	__le32	rq_rnr_timer;
	__le32	rx_msg_len;
	__le32	rx_rkey_pkt_info;
	__le64	rx_va;
	__le32	byte_132_trrl;
	__le32	trrl_ba;
	__le32	byte_140_raq;
	__le32	byte_144_raq;
	__le32	byte_148_raq;
	__le32	byte_152_raq;
	__le32	byte_156_raq;
	__le32	byte_160_sq_ci_pi;
	__le32	sq_cur_blk_addr;
	__le32	byte_168_irrl_idx;
	__le32	byte_172_sq_psn;
	__le32	byte_176_msg_pktn;
	__le32	sq_cur_sge_blk_addr;
	__le32	byte_184_irrl_idx;
	__le32	cur_sge_offset;
	__le32	byte_192_ext_sge;
	__le32	byte_196_sq_psn;
	__le32	byte_200_sq_max;
	__le32	irrl_ba;
	__le32	byte_208_irrl;
	__le32	byte_212_lsn;
	__le32	sq_timer;
	__le32	byte_220_retry_psn_msn;
	__le32	byte_224_retry_msg;
	__le32	rx_sq_cur_blk_addr;
	__le32	byte_232_irrl_sge;
	__le32	irrl_cur_sge_offset;
	__le32	byte_240_irrl_tail;
	__le32	byte_244_rnr_rxack;
	__le32	byte_248_ack_psn;
	__le32	byte_252_err_txcqn;
	__le32	byte_256_sqflush_rqcqe;
};

#define	V2_QPC_BYTE_4_TST_S 0
#define V2_QPC_BYTE_4_TST_M GENMASK(2, 0)

#define	V2_QPC_BYTE_4_SGE_SHIFT_S 3
#define V2_QPC_BYTE_4_SGE_SHIFT_M GENMASK(7, 3)

#define	V2_QPC_BYTE_4_SQPN_S 8
#define V2_QPC_BYTE_4_SQPN_M  GENMASK(31, 8)

#define	V2_QPC_BYTE_12_WQE_SGE_BA_S 0
#define V2_QPC_BYTE_12_WQE_SGE_BA_M GENMASK(28, 0)

#define	V2_QPC_BYTE_12_SQ_HOP_NUM_S 29
#define V2_QPC_BYTE_12_SQ_HOP_NUM_M GENMASK(30, 29)

#define V2_QPC_BYTE_12_RSVD_LKEY_EN_S 31

#define	V2_QPC_BYTE_16_WQE_SGE_BA_PG_SZ_S 0
#define V2_QPC_BYTE_16_WQE_SGE_BA_PG_SZ_M GENMASK(3, 0)

#define	V2_QPC_BYTE_16_WQE_SGE_BUF_PG_SZ_S 4
#define V2_QPC_BYTE_16_WQE_SGE_BUF_PG_SZ_M GENMASK(7, 4)

#define	V2_QPC_BYTE_16_PD_S 8
#define V2_QPC_BYTE_16_PD_M GENMASK(31, 8)

#define	V2_QPC_BYTE_20_RQ_HOP_NUM_S 0
#define V2_QPC_BYTE_20_RQ_HOP_NUM_M GENMASK(1, 0)

#define	V2_QPC_BYTE_20_SGE_HOP_NUM_S 2
#define V2_QPC_BYTE_20_SGE_HOP_NUM_M GENMASK(3, 2)

#define	V2_QPC_BYTE_20_RQWS_S 4
#define V2_QPC_BYTE_20_RQWS_M GENMASK(7, 4)

#define	V2_QPC_BYTE_20_SQ_SHIFT_S 8
#define V2_QPC_BYTE_20_SQ_SHIFT_M GENMASK(11, 8)

#define	V2_QPC_BYTE_20_RQ_SHIFT_S 12
#define V2_QPC_BYTE_20_RQ_SHIFT_M GENMASK(15, 12)

#define	V2_QPC_BYTE_20_SGID_IDX_S 16
#define V2_QPC_BYTE_20_SGID_IDX_M GENMASK(23, 16)

#define	V2_QPC_BYTE_20_SMAC_IDX_S 24
#define V2_QPC_BYTE_20_SMAC_IDX_M GENMASK(31, 24)

#define	V2_QPC_BYTE_24_HOP_LIMIT_S 0
#define V2_QPC_BYTE_24_HOP_LIMIT_M GENMASK(7, 0)

#define	V2_QPC_BYTE_24_TC_S 8
#define V2_QPC_BYTE_24_TC_M GENMASK(15, 8)

#define	V2_QPC_BYTE_24_VLAN_ID_S 16
#define V2_QPC_BYTE_24_VLAN_ID_M GENMASK(27, 16)

#define	V2_QPC_BYTE_24_MTU_S 28
#define V2_QPC_BYTE_24_MTU_M GENMASK(31, 28)

#define	V2_QPC_BYTE_28_FL_S 0
#define V2_QPC_BYTE_28_FL_M GENMASK(19, 0)

#define	V2_QPC_BYTE_28_SL_S 20
#define V2_QPC_BYTE_28_SL_M GENMASK(23, 20)

#define V2_QPC_BYTE_28_CNP_TX_FLAG_S 24

#define V2_QPC_BYTE_28_CE_FLAG_S 25

#define V2_QPC_BYTE_28_LBI_S 26

#define	V2_QPC_BYTE_28_AT_S 27
#define V2_QPC_BYTE_28_AT_M GENMASK(31, 27)

#define	V2_QPC_BYTE_52_DMAC_S 0
#define V2_QPC_BYTE_52_DMAC_M GENMASK(15, 0)

#define V2_QPC_BYTE_52_UDPSPN_S 16
#define V2_QPC_BYTE_52_UDPSPN_M GENMASK(31, 16)

#define	V2_QPC_BYTE_56_DQPN_S 0
#define V2_QPC_BYTE_56_DQPN_M GENMASK(23, 0)

#define	V2_QPC_BYTE_56_SQ_TX_ERR_S 24
#define	V2_QPC_BYTE_56_SQ_RX_ERR_S 25
#define	V2_QPC_BYTE_56_RQ_TX_ERR_S 26
#define	V2_QPC_BYTE_56_RQ_RX_ERR_S 27

#define	V2_QPC_BYTE_56_LP_PKTN_INI_S 28
#define V2_QPC_BYTE_56_LP_PKTN_INI_M GENMASK(31, 28)

#define	V2_QPC_BYTE_60_MAPID_S 0
#define V2_QPC_BYTE_60_MAPID_M GENMASK(12, 0)

#define	V2_QPC_BYTE_60_INNER_MAP_IND_S 13

#define	V2_QPC_BYTE_60_SQ_MAP_IND_S 14

#define	V2_QPC_BYTE_60_RQ_MAP_IND_S 15

#define	V2_QPC_BYTE_60_TEMPID_S 16
#define V2_QPC_BYTE_60_TEMPID_M  GENMASK(22, 16)

#define	V2_QPC_BYTE_60_EXT_MAP_IND_S 23

#define	V2_QPC_BYTE_60_RTY_NUM_INI_BAK_S 24
#define V2_QPC_BYTE_60_RTY_NUM_INI_BAK_M GENMASK(26, 24)

#define V2_QPC_BYTE_60_SQ_RLS_IND_S 27

#define	V2_QPC_BYTE_60_SQ_EXT_IND_S 28

#define	V2_QPC_BYTE_60_QP_ST_S 29
#define V2_QPC_BYTE_60_QP_ST_M GENMASK(31, 29)

#define	V2_QPC_BYTE_68_RQ_RECORD_EN_S 0

#define	V2_QPC_BYTE_68_RQ_DB_RECORD_ADDR_S 1
#define V2_QPC_BYTE_68_RQ_DB_RECORD_ADDR_M GENMASK(31, 1)

#define	V2_QPC_BYTE_76_SRQN_S 0
#define V2_QPC_BYTE_76_SRQN_M GENMASK(23, 0)

#define	V2_QPC_BYTE_76_SRQ_EN_S 24

#define	V2_QPC_BYTE_76_RRE_S 25

#define	V2_QPC_BYTE_76_RWE_S 26

#define	V2_QPC_BYTE_76_ATE_S 27

#define	V2_QPC_BYTE_76_RQIE_S 28

#define	V2_QPC_BYTE_80_RX_CQN_S 0
#define V2_QPC_BYTE_80_RX_CQN_M GENMASK(23, 0)

#define	V2_QPC_BYTE_80_MIN_RNR_TIME_S 27
#define V2_QPC_BYTE_80_MIN_RNR_TIME_M GENMASK(31, 27)

#define	V2_QPC_BYTE_84_RQ_PRODUCER_IDX_S 0
#define V2_QPC_BYTE_84_RQ_PRODUCER_IDX_M GENMASK(15, 0)

#define	V2_QPC_BYTE_84_RQ_CONSUMER_IDX_S 16
#define V2_QPC_BYTE_84_RQ_CONSUMER_IDX_M GENMASK(31, 16)

#define	V2_QPC_BYTE_92_RQ_CUR_BLK_ADDR_S 0
#define V2_QPC_BYTE_92_RQ_CUR_BLK_ADDR_M GENMASK(19, 0)

#define	V2_QPC_BYTE_92_SRQ_INFO_S 20
#define V2_QPC_BYTE_92_SRQ_INFO_M GENMASK(31, 20)

#define	V2_QPC_BYTE_96_RX_REQ_MSN_S 0
#define V2_QPC_BYTE_96_RX_REQ_MSN_M GENMASK(23, 0)

#define	V2_QPC_BYTE_104_RQ_NXT_BLK_ADDR_S 0
#define V2_QPC_BYTE_104_RQ_NXT_BLK_ADDR_M GENMASK(19, 0)

#define	V2_QPC_BYTE_104_RQ_CUR_WQE_SGE_NUM_S 24
#define V2_QPC_BYTE_104_RQ_CUR_WQE_SGE_NUM_M GENMASK(31, 24)

#define V2_QPC_BYTE_108_INV_CREDIT_S 0

#define V2_QPC_BYTE_108_RX_REQ_PSN_ERR_S 3

#define	V2_QPC_BYTE_108_RX_REQ_LAST_OPTYPE_S 4
#define V2_QPC_BYTE_108_RX_REQ_LAST_OPTYPE_M GENMASK(6, 4)

#define V2_QPC_BYTE_108_RX_REQ_RNR_S 7

#define	V2_QPC_BYTE_108_RX_REQ_EPSN_S 8
#define V2_QPC_BYTE_108_RX_REQ_EPSN_M GENMASK(31, 8)

#define	V2_QPC_BYTE_132_TRRL_HEAD_MAX_S 0
#define V2_QPC_BYTE_132_TRRL_HEAD_MAX_M GENMASK(7, 0)

#define	V2_QPC_BYTE_132_TRRL_TAIL_MAX_S 8
#define V2_QPC_BYTE_132_TRRL_TAIL_MAX_M GENMASK(15, 8)

#define	V2_QPC_BYTE_132_TRRL_BA_S 16
#define V2_QPC_BYTE_132_TRRL_BA_M GENMASK(31, 16)

#define	V2_QPC_BYTE_140_TRRL_BA_S 0
#define V2_QPC_BYTE_140_TRRL_BA_M GENMASK(11, 0)

#define	V2_QPC_BYTE_140_RR_MAX_S 12
#define V2_QPC_BYTE_140_RR_MAX_M GENMASK(14, 12)

#define	V2_QPC_BYTE_140_RSVD_RAQ_MAP_S 15

#define	V2_QPC_BYTE_140_RAQ_TRRL_HEAD_S 16
#define V2_QPC_BYTE_140_RAQ_TRRL_HEAD_M GENMASK(23, 16)

#define	V2_QPC_BYTE_140_RAQ_TRRL_TAIL_S 24
#define V2_QPC_BYTE_140_RAQ_TRRL_TAIL_M GENMASK(31, 24)

#define	V2_QPC_BYTE_144_RAQ_RTY_INI_PSN_S 0
#define V2_QPC_BYTE_144_RAQ_RTY_INI_PSN_M GENMASK(23, 0)

#define V2_QPC_BYTE_144_RAQ_RTY_INI_IND_S 24

#define V2_QPC_BYTE_144_RAQ_CREDIT_S 25
#define V2_QPC_BYTE_144_RAQ_CREDIT_M GENMASK(29, 25)

#define V2_QPC_BYTE_144_RESP_RTY_FLG_S 31

#define	V2_QPC_BYTE_148_RQ_MSN_S 0
#define V2_QPC_BYTE_148_RQ_MSN_M GENMASK(23, 0)

#define	V2_QPC_BYTE_148_RAQ_SYNDROME_S 24
#define V2_QPC_BYTE_148_RAQ_SYNDROME_M GENMASK(31, 24)

#define	V2_QPC_BYTE_152_RAQ_PSN_S 8
#define V2_QPC_BYTE_152_RAQ_PSN_M GENMASK(31, 8)

#define	V2_QPC_BYTE_152_RAQ_TRRL_RTY_HEAD_S 24
#define V2_QPC_BYTE_152_RAQ_TRRL_RTY_HEAD_M GENMASK(31, 24)

#define	V2_QPC_BYTE_156_RAQ_USE_PKTN_S 0
#define V2_QPC_BYTE_156_RAQ_USE_PKTN_M GENMASK(23, 0)

#define	V2_QPC_BYTE_160_SQ_PRODUCER_IDX_S 0
#define V2_QPC_BYTE_160_SQ_PRODUCER_IDX_M GENMASK(15, 0)

#define	V2_QPC_BYTE_160_SQ_CONSUMER_IDX_S 16
#define V2_QPC_BYTE_160_SQ_CONSUMER_IDX_M GENMASK(31, 16)

#define	V2_QPC_BYTE_168_SQ_CUR_BLK_ADDR_S 0
#define V2_QPC_BYTE_168_SQ_CUR_BLK_ADDR_M GENMASK(19, 0)

#define V2_QPC_BYTE_168_MSG_RTY_LP_FLG_S 20

#define V2_QPC_BYTE_168_SQ_INVLD_FLG_S 21

#define	V2_QPC_BYTE_168_LP_SGEN_INI_S 22
#define V2_QPC_BYTE_168_LP_SGEN_INI_M GENMASK(23, 22)

#define	V2_QPC_BYTE_168_SQ_SHIFT_BAK_S 24
#define V2_QPC_BYTE_168_SQ_SHIFT_BAK_M GENMASK(27, 24)

#define	V2_QPC_BYTE_168_IRRL_IDX_LSB_S 28
#define V2_QPC_BYTE_168_IRRL_IDX_LSB_M GENMASK(31, 28)

#define	V2_QPC_BYTE_172_ACK_REQ_FREQ_S 0
#define V2_QPC_BYTE_172_ACK_REQ_FREQ_M GENMASK(5, 0)

#define V2_QPC_BYTE_172_MSG_RNR_FLG_S 6

#define V2_QPC_BYTE_172_FRE_S 7

#define	V2_QPC_BYTE_172_SQ_CUR_PSN_S 8
#define V2_QPC_BYTE_172_SQ_CUR_PSN_M GENMASK(31, 8)

#define	V2_QPC_BYTE_176_MSG_USE_PKTN_S 0
#define V2_QPC_BYTE_176_MSG_USE_PKTN_M GENMASK(23, 0)

#define	V2_QPC_BYTE_176_IRRL_HEAD_PRE_S 24
#define V2_QPC_BYTE_176_IRRL_HEAD_PRE_M GENMASK(31, 24)

#define	V2_QPC_BYTE_184_SQ_CUR_SGE_BLK_ADDR_S 0
#define V2_QPC_BYTE_184_SQ_CUR_SGE_BLK_ADDR_M GENMASK(19, 0)

#define	V2_QPC_BYTE_184_IRRL_IDX_MSB_S 20
#define V2_QPC_BYTE_184_IRRL_IDX_MSB_M GENMASK(31, 20)

#define	V2_QPC_BYTE_192_CUR_SGE_IDX_S 0
#define V2_QPC_BYTE_192_CUR_SGE_IDX_M GENMASK(23, 0)

#define	V2_QPC_BYTE_192_EXT_SGE_NUM_LEFT_S 24
#define V2_QPC_BYTE_192_EXT_SGE_NUM_LEFT_M GENMASK(31, 24)

#define	V2_QPC_BYTE_196_IRRL_HEAD_S 0
#define V2_QPC_BYTE_196_IRRL_HEAD_M GENMASK(7, 0)

#define	V2_QPC_BYTE_196_SQ_MAX_PSN_S 8
#define V2_QPC_BYTE_196_SQ_MAX_PSN_M GENMASK(31, 8)

#define	V2_QPC_BYTE_200_SQ_MAX_IDX_S 0
#define V2_QPC_BYTE_200_SQ_MAX_IDX_M GENMASK(15, 0)

#define	V2_QPC_BYTE_200_LCL_OPERATED_CNT_S 16
#define V2_QPC_BYTE_200_LCL_OPERATED_CNT_M GENMASK(31, 16)

#define	V2_QPC_BYTE_208_IRRL_BA_S 0
#define V2_QPC_BYTE_208_IRRL_BA_M GENMASK(25, 0)

#define V2_QPC_BYTE_208_PKT_RNR_FLG_S 26

#define V2_QPC_BYTE_208_PKT_RTY_FLG_S 27

#define V2_QPC_BYTE_208_RMT_E2E_S 28

#define	V2_QPC_BYTE_208_SR_MAX_S 29
#define V2_QPC_BYTE_208_SR_MAX_M GENMASK(31, 29)

#define	V2_QPC_BYTE_212_LSN_S 0
#define V2_QPC_BYTE_212_LSN_M GENMASK(23, 0)

#define	V2_QPC_BYTE_212_RETRY_NUM_INIT_S 24
#define V2_QPC_BYTE_212_RETRY_NUM_INIT_M GENMASK(26, 24)

#define	V2_QPC_BYTE_212_CHECK_FLG_S 27
#define V2_QPC_BYTE_212_CHECK_FLG_M GENMASK(28, 27)

#define	V2_QPC_BYTE_212_RETRY_CNT_S 29
#define V2_QPC_BYTE_212_RETRY_CNT_M GENMASK(31, 29)

#define	V2_QPC_BYTE_220_RETRY_MSG_MSN_S 0
#define V2_QPC_BYTE_220_RETRY_MSG_MSN_M GENMASK(15, 0)

#define	V2_QPC_BYTE_220_RETRY_MSG_PSN_S 16
#define V2_QPC_BYTE_220_RETRY_MSG_PSN_M GENMASK(31, 16)

#define	V2_QPC_BYTE_224_RETRY_MSG_PSN_S 0
#define V2_QPC_BYTE_224_RETRY_MSG_PSN_M GENMASK(7, 0)

#define	V2_QPC_BYTE_224_RETRY_MSG_FPKT_PSN_S 8
#define V2_QPC_BYTE_224_RETRY_MSG_FPKT_PSN_M GENMASK(31, 8)

#define	V2_QPC_BYTE_232_RX_SQ_CUR_BLK_ADDR_S 0
#define V2_QPC_BYTE_232_RX_SQ_CUR_BLK_ADDR_M GENMASK(19, 0)

#define	V2_QPC_BYTE_232_IRRL_SGE_IDX_S 20
#define V2_QPC_BYTE_232_IRRL_SGE_IDX_M GENMASK(28, 20)

#define	V2_QPC_BYTE_240_IRRL_TAIL_REAL_S 0
#define V2_QPC_BYTE_240_IRRL_TAIL_REAL_M GENMASK(7, 0)

#define	V2_QPC_BYTE_240_IRRL_TAIL_RD_S 8
#define V2_QPC_BYTE_240_IRRL_TAIL_RD_M GENMASK(15, 8)

#define	V2_QPC_BYTE_240_RX_ACK_MSN_S 16
#define V2_QPC_BYTE_240_RX_ACK_MSN_M GENMASK(31, 16)

#define	V2_QPC_BYTE_244_RX_ACK_EPSN_S 0
#define V2_QPC_BYTE_244_RX_ACK_EPSN_M GENMASK(23, 0)

#define	V2_QPC_BYTE_244_RNR_NUM_INIT_S 24
#define V2_QPC_BYTE_244_RNR_NUM_INIT_M GENMASK(26, 24)

#define	V2_QPC_BYTE_244_RNR_CNT_S 27
#define V2_QPC_BYTE_244_RNR_CNT_M GENMASK(29, 27)

#define	V2_QPC_BYTE_248_IRRL_PSN_S 0
#define V2_QPC_BYTE_248_IRRL_PSN_M GENMASK(23, 0)

#define V2_QPC_BYTE_248_ACK_PSN_ERR_S 24

#define	V2_QPC_BYTE_248_ACK_LAST_OPTYPE_S 25
#define V2_QPC_BYTE_248_ACK_LAST_OPTYPE_M GENMASK(26, 25)

#define V2_QPC_BYTE_248_IRRL_PSN_VLD_S 27

#define V2_QPC_BYTE_248_RNR_RETRY_FLAG_S 28

#define V2_QPC_BYTE_248_CQ_ERR_IND_S 31

#define	V2_QPC_BYTE_252_TX_CQN_S 0
#define V2_QPC_BYTE_252_TX_CQN_M GENMASK(23, 0)

#define	V2_QPC_BYTE_252_SIG_TYPE_S 24

#define	V2_QPC_BYTE_252_ERR_TYPE_S 25
#define V2_QPC_BYTE_252_ERR_TYPE_M GENMASK(31, 25)

#define	V2_QPC_BYTE_256_RQ_CQE_IDX_S 0
#define V2_QPC_BYTE_256_RQ_CQE_IDX_M GENMASK(15, 0)

#define	V2_QPC_BYTE_256_SQ_FLUSH_IDX_S 16
#define V2_QPC_BYTE_256_SQ_FLUSH_IDX_M GENMASK(31, 16)

struct hns_roce_v2_cqe {
	__le32	byte_4;
	union {
		__le32 rkey;
		__le32 immtdata;
	};
	__le32	byte_12;
	__le32	byte_16;
	__le32	byte_cnt;
	u8	smac[4];
	__le32	byte_28;
	__le32	byte_32;
};

#define	V2_CQE_BYTE_4_OPCODE_S 0
#define V2_CQE_BYTE_4_OPCODE_M GENMASK(4, 0)

#define	V2_CQE_BYTE_4_RQ_INLINE_S 5

#define	V2_CQE_BYTE_4_S_R_S 6

#define	V2_CQE_BYTE_4_OWNER_S 7

#define	V2_CQE_BYTE_4_STATUS_S 8
#define V2_CQE_BYTE_4_STATUS_M GENMASK(15, 8)

#define	V2_CQE_BYTE_4_WQE_INDX_S 16
#define V2_CQE_BYTE_4_WQE_INDX_M GENMASK(31, 16)

#define	V2_CQE_BYTE_12_XRC_SRQN_S 0
#define V2_CQE_BYTE_12_XRC_SRQN_M GENMASK(23, 0)

#define	V2_CQE_BYTE_16_LCL_QPN_S 0
#define V2_CQE_BYTE_16_LCL_QPN_M GENMASK(23, 0)

#define	V2_CQE_BYTE_16_SUB_STATUS_S 24
#define V2_CQE_BYTE_16_SUB_STATUS_M GENMASK(31, 24)

#define	V2_CQE_BYTE_28_SMAC_4_S 0
#define V2_CQE_BYTE_28_SMAC_4_M	GENMASK(7, 0)

#define	V2_CQE_BYTE_28_SMAC_5_S 8
#define V2_CQE_BYTE_28_SMAC_5_M	GENMASK(15, 8)

#define	V2_CQE_BYTE_28_PORT_TYPE_S 16
#define V2_CQE_BYTE_28_PORT_TYPE_M GENMASK(17, 16)

#define V2_CQE_BYTE_28_VID_S 18
#define V2_CQE_BYTE_28_VID_M GENMASK(29, 18)

#define V2_CQE_BYTE_28_VID_VLD_S 30

#define	V2_CQE_BYTE_32_RMT_QPN_S 0
#define V2_CQE_BYTE_32_RMT_QPN_M GENMASK(23, 0)

#define	V2_CQE_BYTE_32_SL_S 24
#define V2_CQE_BYTE_32_SL_M GENMASK(26, 24)

#define	V2_CQE_BYTE_32_PORTN_S 27
#define V2_CQE_BYTE_32_PORTN_M GENMASK(29, 27)

#define	V2_CQE_BYTE_32_GRH_S 30

#define	V2_CQE_BYTE_32_LPK_S 31

struct hns_roce_v2_mpt_entry {
	__le32	byte_4_pd_hop_st;
	__le32	byte_8_mw_cnt_en;
	__le32	byte_12_mw_pa;
	__le32	bound_lkey;
	__le32	len_l;
	__le32	len_h;
	__le32	lkey;
	__le32	va_l;
	__le32	va_h;
	__le32	pbl_size;
	__le32	pbl_ba_l;
	__le32	byte_48_mode_ba;
	__le32	pa0_l;
	__le32	byte_56_pa0_h;
	__le32	pa1_l;
	__le32	byte_64_buf_pa1;
};

#define V2_MPT_BYTE_4_MPT_ST_S 0
#define V2_MPT_BYTE_4_MPT_ST_M GENMASK(1, 0)

#define V2_MPT_BYTE_4_PBL_HOP_NUM_S 2
#define V2_MPT_BYTE_4_PBL_HOP_NUM_M GENMASK(3, 2)

#define V2_MPT_BYTE_4_PBL_BA_PG_SZ_S 4
#define V2_MPT_BYTE_4_PBL_BA_PG_SZ_M GENMASK(7, 4)

#define V2_MPT_BYTE_4_PD_S 8
#define V2_MPT_BYTE_4_PD_M GENMASK(31, 8)

#define V2_MPT_BYTE_8_RA_EN_S 0

#define V2_MPT_BYTE_8_R_INV_EN_S 1

#define V2_MPT_BYTE_8_L_INV_EN_S 2

#define V2_MPT_BYTE_8_BIND_EN_S 3

#define V2_MPT_BYTE_8_ATOMIC_EN_S 4

#define V2_MPT_BYTE_8_RR_EN_S 5

#define V2_MPT_BYTE_8_RW_EN_S 6

#define V2_MPT_BYTE_8_LW_EN_S 7

#define V2_MPT_BYTE_12_PA_S 1

#define V2_MPT_BYTE_12_INNER_PA_VLD_S 7

#define V2_MPT_BYTE_12_MW_BIND_QPN_S 8
#define V2_MPT_BYTE_12_MW_BIND_QPN_M GENMASK(31, 8)

#define V2_MPT_BYTE_48_PBL_BA_H_S 0
#define V2_MPT_BYTE_48_PBL_BA_H_M GENMASK(28, 0)

#define V2_MPT_BYTE_48_BLK_MODE_S 29

#define V2_MPT_BYTE_56_PA0_H_S 0
#define V2_MPT_BYTE_56_PA0_H_M GENMASK(25, 0)

#define V2_MPT_BYTE_64_PA1_H_S 0
#define V2_MPT_BYTE_64_PA1_H_M GENMASK(25, 0)

#define V2_MPT_BYTE_64_PBL_BUF_PG_SZ_S 28
#define V2_MPT_BYTE_64_PBL_BUF_PG_SZ_M GENMASK(31, 28)

#define	V2_DB_BYTE_4_TAG_S 0
#define V2_DB_BYTE_4_TAG_M GENMASK(23, 0)

#define	V2_DB_BYTE_4_CMD_S 24
#define V2_DB_BYTE_4_CMD_M GENMASK(27, 24)

#define V2_DB_PARAMETER_IDX_S 0
#define V2_DB_PARAMETER_IDX_M GENMASK(15, 0)

#define V2_DB_PARAMETER_SL_S 16
#define V2_DB_PARAMETER_SL_M GENMASK(18, 16)

struct hns_roce_v2_cq_db {
	__le32	byte_4;
	__le32	parameter;
};

#define	V2_CQ_DB_BYTE_4_TAG_S 0
#define V2_CQ_DB_BYTE_4_TAG_M GENMASK(23, 0)

#define	V2_CQ_DB_BYTE_4_CMD_S 24
#define V2_CQ_DB_BYTE_4_CMD_M GENMASK(27, 24)

#define V2_CQ_DB_PARAMETER_CONS_IDX_S 0
#define V2_CQ_DB_PARAMETER_CONS_IDX_M GENMASK(23, 0)

#define V2_CQ_DB_PARAMETER_CMD_SN_S 25
#define V2_CQ_DB_PARAMETER_CMD_SN_M GENMASK(26, 25)

#define V2_CQ_DB_PARAMETER_NOTIFY_S 24

struct hns_roce_v2_ud_send_wqe {
	__le32	byte_4;
	__le32	msg_len;
	__le32	immtdata;
	__le32	byte_16;
	__le32	byte_20;
	__le32	byte_24;
	__le32	qkey;
	__le32	byte_32;
	__le32	byte_36;
	__le32	byte_40;
	__le32	dmac;
	__le32	byte_48;
	u8	dgid[GID_LEN_V2];

};
#define	V2_UD_SEND_WQE_BYTE_4_OPCODE_S 0
#define V2_UD_SEND_WQE_BYTE_4_OPCODE_M GENMASK(4, 0)

#define	V2_UD_SEND_WQE_BYTE_4_OWNER_S 7

#define	V2_UD_SEND_WQE_BYTE_4_CQE_S 8

#define	V2_UD_SEND_WQE_BYTE_4_SE_S 11

#define	V2_UD_SEND_WQE_BYTE_16_PD_S 0
#define V2_UD_SEND_WQE_BYTE_16_PD_M GENMASK(23, 0)

#define	V2_UD_SEND_WQE_BYTE_16_SGE_NUM_S 24
#define V2_UD_SEND_WQE_BYTE_16_SGE_NUM_M GENMASK(31, 24)

#define	V2_UD_SEND_WQE_BYTE_20_MSG_START_SGE_IDX_S 0
#define V2_UD_SEND_WQE_BYTE_20_MSG_START_SGE_IDX_M GENMASK(23, 0)

#define	V2_UD_SEND_WQE_BYTE_24_UDPSPN_S 16
#define V2_UD_SEND_WQE_BYTE_24_UDPSPN_M GENMASK(31, 16)

#define	V2_UD_SEND_WQE_BYTE_32_DQPN_S 0
#define V2_UD_SEND_WQE_BYTE_32_DQPN_M GENMASK(23, 0)

#define	V2_UD_SEND_WQE_BYTE_36_VLAN_S 0
#define V2_UD_SEND_WQE_BYTE_36_VLAN_M GENMASK(15, 0)

#define	V2_UD_SEND_WQE_BYTE_36_HOPLIMIT_S 16
#define V2_UD_SEND_WQE_BYTE_36_HOPLIMIT_M GENMASK(23, 16)

#define	V2_UD_SEND_WQE_BYTE_36_TCLASS_S 24
#define V2_UD_SEND_WQE_BYTE_36_TCLASS_M GENMASK(31, 24)

#define	V2_UD_SEND_WQE_BYTE_40_FLOW_LABEL_S 0
#define V2_UD_SEND_WQE_BYTE_40_FLOW_LABEL_M GENMASK(19, 0)

#define	V2_UD_SEND_WQE_BYTE_40_SL_S 20
#define V2_UD_SEND_WQE_BYTE_40_SL_M GENMASK(23, 20)

#define	V2_UD_SEND_WQE_BYTE_40_PORTN_S 24
#define V2_UD_SEND_WQE_BYTE_40_PORTN_M GENMASK(26, 24)

#define	V2_UD_SEND_WQE_BYTE_40_LBI_S 31

#define	V2_UD_SEND_WQE_DMAC_0_S 0
#define V2_UD_SEND_WQE_DMAC_0_M GENMASK(7, 0)

#define	V2_UD_SEND_WQE_DMAC_1_S 8
#define V2_UD_SEND_WQE_DMAC_1_M GENMASK(15, 8)

#define	V2_UD_SEND_WQE_DMAC_2_S 16
#define V2_UD_SEND_WQE_DMAC_2_M GENMASK(23, 16)

#define	V2_UD_SEND_WQE_DMAC_3_S 24
#define V2_UD_SEND_WQE_DMAC_3_M GENMASK(31, 24)

#define	V2_UD_SEND_WQE_BYTE_48_DMAC_4_S 0
#define V2_UD_SEND_WQE_BYTE_48_DMAC_4_M GENMASK(7, 0)

#define	V2_UD_SEND_WQE_BYTE_48_DMAC_5_S 8
#define V2_UD_SEND_WQE_BYTE_48_DMAC_5_M GENMASK(15, 8)

#define	V2_UD_SEND_WQE_BYTE_48_SGID_INDX_S 16
#define V2_UD_SEND_WQE_BYTE_48_SGID_INDX_M GENMASK(23, 16)

#define	V2_UD_SEND_WQE_BYTE_48_SMAC_INDX_S 24
#define V2_UD_SEND_WQE_BYTE_48_SMAC_INDX_M GENMASK(31, 24)

struct hns_roce_v2_rc_send_wqe {
	__le32		byte_4;
	__le32		msg_len;
	union {
		__le32  inv_key;
		__le32  immtdata;
	};
	__le32		byte_16;
	__le32		byte_20;
	__le32		rkey;
	__le64		va;
};

#define	V2_RC_SEND_WQE_BYTE_4_OPCODE_S 0
#define V2_RC_SEND_WQE_BYTE_4_OPCODE_M GENMASK(4, 0)

#define V2_RC_SEND_WQE_BYTE_4_OWNER_S 7

#define V2_RC_SEND_WQE_BYTE_4_CQE_S 8

#define V2_RC_SEND_WQE_BYTE_4_FENCE_S 9

#define V2_RC_SEND_WQE_BYTE_4_SO_S 10

#define V2_RC_SEND_WQE_BYTE_4_SE_S 11

#define V2_RC_SEND_WQE_BYTE_4_INLINE_S 12

#define	V2_RC_SEND_WQE_BYTE_16_XRC_SRQN_S 0
#define V2_RC_SEND_WQE_BYTE_16_XRC_SRQN_M GENMASK(23, 0)

#define	V2_RC_SEND_WQE_BYTE_16_SGE_NUM_S 24
#define V2_RC_SEND_WQE_BYTE_16_SGE_NUM_M GENMASK(31, 24)

#define V2_RC_SEND_WQE_BYTE_20_MSG_START_SGE_IDX_S 0
#define V2_RC_SEND_WQE_BYTE_20_MSG_START_SGE_IDX_M GENMASK(23, 0)

struct hns_roce_v2_wqe_data_seg {
	__le32    len;
	__le32    lkey;
	__le64    addr;
};

struct hns_roce_v2_db {
	__le32	byte_4;
	__le32	parameter;
};

struct hns_roce_query_version {
	__le16 rocee_vendor_id;
	__le16 rocee_hw_version;
	__le32 rsv[5];
};

struct hns_roce_cfg_llm_a {
	__le32 base_addr_l;
	__le32 base_addr_h;
	__le32 depth_pgsz_init_en;
	__le32 head_ba_l;
	__le32 head_ba_h_nxtptr;
	__le32 head_ptr;
};

#define CFG_LLM_QUE_DEPTH_S 0
#define CFG_LLM_QUE_DEPTH_M GENMASK(12, 0)

#define CFG_LLM_QUE_PGSZ_S 16
#define CFG_LLM_QUE_PGSZ_M GENMASK(19, 16)

#define CFG_LLM_INIT_EN_S 20
#define CFG_LLM_INIT_EN_M GENMASK(20, 20)

#define CFG_LLM_HEAD_PTR_S 0
#define CFG_LLM_HEAD_PTR_M GENMASK(11, 0)

struct hns_roce_cfg_llm_b {
	__le32 tail_ba_l;
	__le32 tail_ba_h;
	__le32 tail_ptr;
	__le32 rsv[3];
};

#define CFG_LLM_TAIL_BA_H_S 0
#define CFG_LLM_TAIL_BA_H_M GENMASK(19, 0)

#define CFG_LLM_TAIL_PTR_S 0
#define CFG_LLM_TAIL_PTR_M GENMASK(11, 0)

struct hns_roce_cfg_global_param {
	__le32 time_cfg_udp_port;
	__le32 rsv[5];
};

#define CFG_GLOBAL_PARAM_DATA_0_ROCEE_TIME_1US_CFG_S 0
#define CFG_GLOBAL_PARAM_DATA_0_ROCEE_TIME_1US_CFG_M GENMASK(9, 0)

#define CFG_GLOBAL_PARAM_DATA_0_ROCEE_UDP_PORT_S 16
#define CFG_GLOBAL_PARAM_DATA_0_ROCEE_UDP_PORT_M GENMASK(31, 16)

struct hns_roce_pf_res_a {
	__le32	rsv;
	__le32	qpc_bt_idx_num;
	__le32	srqc_bt_idx_num;
	__le32	cqc_bt_idx_num;
	__le32	mpt_bt_idx_num;
	__le32	eqc_bt_idx_num;
};

#define PF_RES_DATA_1_PF_QPC_BT_IDX_S 0
#define PF_RES_DATA_1_PF_QPC_BT_IDX_M GENMASK(10, 0)

#define PF_RES_DATA_1_PF_QPC_BT_NUM_S 16
#define PF_RES_DATA_1_PF_QPC_BT_NUM_M GENMASK(27, 16)

#define PF_RES_DATA_2_PF_SRQC_BT_IDX_S 0
#define PF_RES_DATA_2_PF_SRQC_BT_IDX_M GENMASK(8, 0)

#define PF_RES_DATA_2_PF_SRQC_BT_NUM_S 16
#define PF_RES_DATA_2_PF_SRQC_BT_NUM_M GENMASK(25, 16)

#define PF_RES_DATA_3_PF_CQC_BT_IDX_S 0
#define PF_RES_DATA_3_PF_CQC_BT_IDX_M GENMASK(8, 0)

#define PF_RES_DATA_3_PF_CQC_BT_NUM_S 16
#define PF_RES_DATA_3_PF_CQC_BT_NUM_M GENMASK(25, 16)

#define PF_RES_DATA_4_PF_MPT_BT_IDX_S 0
#define PF_RES_DATA_4_PF_MPT_BT_IDX_M GENMASK(8, 0)

#define PF_RES_DATA_4_PF_MPT_BT_NUM_S 16
#define PF_RES_DATA_4_PF_MPT_BT_NUM_M GENMASK(25, 16)

#define PF_RES_DATA_5_PF_EQC_BT_IDX_S 0
#define PF_RES_DATA_5_PF_EQC_BT_IDX_M GENMASK(8, 0)

#define PF_RES_DATA_5_PF_EQC_BT_NUM_S 16
#define PF_RES_DATA_5_PF_EQC_BT_NUM_M GENMASK(25, 16)

struct hns_roce_pf_res_b {
	__le32	rsv0;
	__le32	smac_idx_num;
	__le32	sgid_idx_num;
	__le32	qid_idx_sl_num;
	__le32	rsv[2];
};

#define PF_RES_DATA_1_PF_SMAC_IDX_S 0
#define PF_RES_DATA_1_PF_SMAC_IDX_M GENMASK(7, 0)

#define PF_RES_DATA_1_PF_SMAC_NUM_S 8
#define PF_RES_DATA_1_PF_SMAC_NUM_M GENMASK(16, 8)

#define PF_RES_DATA_2_PF_SGID_IDX_S 0
#define PF_RES_DATA_2_PF_SGID_IDX_M GENMASK(7, 0)

#define PF_RES_DATA_2_PF_SGID_NUM_S 8
#define PF_RES_DATA_2_PF_SGID_NUM_M GENMASK(16, 8)

#define PF_RES_DATA_3_PF_QID_IDX_S 0
#define PF_RES_DATA_3_PF_QID_IDX_M GENMASK(9, 0)

#define PF_RES_DATA_3_PF_SL_NUM_S 16
#define PF_RES_DATA_3_PF_SL_NUM_M GENMASK(26, 16)

struct hns_roce_vf_res_a {
	__le32 vf_id;
	__le32 vf_qpc_bt_idx_num;
	__le32 vf_srqc_bt_idx_num;
	__le32 vf_cqc_bt_idx_num;
	__le32 vf_mpt_bt_idx_num;
	__le32 vf_eqc_bt_idx_num;
};

#define VF_RES_A_DATA_1_VF_QPC_BT_IDX_S 0
#define VF_RES_A_DATA_1_VF_QPC_BT_IDX_M GENMASK(10, 0)

#define VF_RES_A_DATA_1_VF_QPC_BT_NUM_S 16
#define VF_RES_A_DATA_1_VF_QPC_BT_NUM_M GENMASK(27, 16)

#define VF_RES_A_DATA_2_VF_SRQC_BT_IDX_S 0
#define VF_RES_A_DATA_2_VF_SRQC_BT_IDX_M GENMASK(8, 0)

#define VF_RES_A_DATA_2_VF_SRQC_BT_NUM_S 16
#define VF_RES_A_DATA_2_VF_SRQC_BT_NUM_M GENMASK(25, 16)

#define VF_RES_A_DATA_3_VF_CQC_BT_IDX_S 0
#define VF_RES_A_DATA_3_VF_CQC_BT_IDX_M GENMASK(8, 0)

#define VF_RES_A_DATA_3_VF_CQC_BT_NUM_S 16
#define VF_RES_A_DATA_3_VF_CQC_BT_NUM_M GENMASK(25, 16)

#define VF_RES_A_DATA_4_VF_MPT_BT_IDX_S 0
#define VF_RES_A_DATA_4_VF_MPT_BT_IDX_M GENMASK(8, 0)

#define VF_RES_A_DATA_4_VF_MPT_BT_NUM_S 16
#define VF_RES_A_DATA_4_VF_MPT_BT_NUM_M GENMASK(25, 16)

#define VF_RES_A_DATA_5_VF_EQC_IDX_S 0
#define VF_RES_A_DATA_5_VF_EQC_IDX_M GENMASK(8, 0)

#define VF_RES_A_DATA_5_VF_EQC_NUM_S 16
#define VF_RES_A_DATA_5_VF_EQC_NUM_M GENMASK(25, 16)

struct hns_roce_vf_res_b {
	__le32 rsv0;
	__le32 vf_smac_idx_num;
	__le32 vf_sgid_idx_num;
	__le32 vf_qid_idx_sl_num;
	__le32 rsv[2];
};

#define VF_RES_B_DATA_0_VF_ID_S 0
#define VF_RES_B_DATA_0_VF_ID_M GENMASK(7, 0)

#define VF_RES_B_DATA_1_VF_SMAC_IDX_S 0
#define VF_RES_B_DATA_1_VF_SMAC_IDX_M GENMASK(7, 0)

#define VF_RES_B_DATA_1_VF_SMAC_NUM_S 8
#define VF_RES_B_DATA_1_VF_SMAC_NUM_M GENMASK(16, 8)

#define VF_RES_B_DATA_2_VF_SGID_IDX_S 0
#define VF_RES_B_DATA_2_VF_SGID_IDX_M GENMASK(7, 0)

#define VF_RES_B_DATA_2_VF_SGID_NUM_S 8
#define VF_RES_B_DATA_2_VF_SGID_NUM_M GENMASK(16, 8)

#define VF_RES_B_DATA_3_VF_QID_IDX_S 0
#define VF_RES_B_DATA_3_VF_QID_IDX_M GENMASK(9, 0)

#define VF_RES_B_DATA_3_VF_SL_NUM_S 16
#define VF_RES_B_DATA_3_VF_SL_NUM_M GENMASK(19, 16)

struct hns_roce_cfg_bt_attr {
	__le32 vf_qpc_cfg;
	__le32 vf_srqc_cfg;
	__le32 vf_cqc_cfg;
	__le32 vf_mpt_cfg;
	__le32 rsv[2];
};

#define CFG_BT_ATTR_DATA_0_VF_QPC_BA_PGSZ_S 0
#define CFG_BT_ATTR_DATA_0_VF_QPC_BA_PGSZ_M GENMASK(3, 0)

#define CFG_BT_ATTR_DATA_0_VF_QPC_BUF_PGSZ_S 4
#define CFG_BT_ATTR_DATA_0_VF_QPC_BUF_PGSZ_M GENMASK(7, 4)

#define CFG_BT_ATTR_DATA_0_VF_QPC_HOPNUM_S 8
#define CFG_BT_ATTR_DATA_0_VF_QPC_HOPNUM_M GENMASK(9, 8)

#define CFG_BT_ATTR_DATA_1_VF_SRQC_BA_PGSZ_S 0
#define CFG_BT_ATTR_DATA_1_VF_SRQC_BA_PGSZ_M GENMASK(3, 0)

#define CFG_BT_ATTR_DATA_1_VF_SRQC_BUF_PGSZ_S 4
#define CFG_BT_ATTR_DATA_1_VF_SRQC_BUF_PGSZ_M GENMASK(7, 4)

#define CFG_BT_ATTR_DATA_1_VF_SRQC_HOPNUM_S 8
#define CFG_BT_ATTR_DATA_1_VF_SRQC_HOPNUM_M GENMASK(9, 8)

#define CFG_BT_ATTR_DATA_2_VF_CQC_BA_PGSZ_S 0
#define CFG_BT_ATTR_DATA_2_VF_CQC_BA_PGSZ_M GENMASK(3, 0)

#define CFG_BT_ATTR_DATA_2_VF_CQC_BUF_PGSZ_S 4
#define CFG_BT_ATTR_DATA_2_VF_CQC_BUF_PGSZ_M GENMASK(7, 4)

#define CFG_BT_ATTR_DATA_2_VF_CQC_HOPNUM_S 8
#define CFG_BT_ATTR_DATA_2_VF_CQC_HOPNUM_M GENMASK(9, 8)

#define CFG_BT_ATTR_DATA_3_VF_MPT_BA_PGSZ_S 0
#define CFG_BT_ATTR_DATA_3_VF_MPT_BA_PGSZ_M GENMASK(3, 0)

#define CFG_BT_ATTR_DATA_3_VF_MPT_BUF_PGSZ_S 4
#define CFG_BT_ATTR_DATA_3_VF_MPT_BUF_PGSZ_M GENMASK(7, 4)

#define CFG_BT_ATTR_DATA_3_VF_MPT_HOPNUM_S 8
#define CFG_BT_ATTR_DATA_3_VF_MPT_HOPNUM_M GENMASK(9, 8)

struct hns_roce_cfg_sgid_tb {
	__le32	table_idx_rsv;
	__le32	vf_sgid_l;
	__le32	vf_sgid_ml;
	__le32	vf_sgid_mh;
	__le32	vf_sgid_h;
	__le32	vf_sgid_type_rsv;
};
#define CFG_SGID_TB_TABLE_IDX_S 0
#define CFG_SGID_TB_TABLE_IDX_M GENMASK(7, 0)

#define CFG_SGID_TB_VF_SGID_TYPE_S 0
#define CFG_SGID_TB_VF_SGID_TYPE_M GENMASK(1, 0)

struct hns_roce_cfg_smac_tb {
	__le32	tb_idx_rsv;
	__le32	vf_smac_l;
	__le32	vf_smac_h_rsv;
	__le32	rsv[3];
};
#define CFG_SMAC_TB_IDX_S 0
#define CFG_SMAC_TB_IDX_M GENMASK(7, 0)

#define CFG_SMAC_TB_VF_SMAC_H_S 0
#define CFG_SMAC_TB_VF_SMAC_H_M GENMASK(15, 0)

struct hns_roce_cmq_desc {
	__le16 opcode;
	__le16 flag;
	__le16 retval;
	__le16 rsv;
	__le32 data[6];
};

#define HNS_ROCE_V2_GO_BIT_TIMEOUT_MSECS	10000

#define HNS_ROCE_HW_RUN_BIT_SHIFT	31
#define HNS_ROCE_HW_MB_STATUS_MASK	0xFF

#define HNS_ROCE_VF_MB4_TAG_MASK	0xFFFFFF00
#define HNS_ROCE_VF_MB4_TAG_SHIFT	8

#define HNS_ROCE_VF_MB4_CMD_MASK	0xFF
#define HNS_ROCE_VF_MB4_CMD_SHIFT	0

#define HNS_ROCE_VF_MB5_EVENT_MASK	0x10000
#define HNS_ROCE_VF_MB5_EVENT_SHIFT	16

#define HNS_ROCE_VF_MB5_TOKEN_MASK	0xFFFF
#define HNS_ROCE_VF_MB5_TOKEN_SHIFT	0

struct hns_roce_v2_cmq_ring {
	dma_addr_t desc_dma_addr;
	struct hns_roce_cmq_desc *desc;
	u32 head;
	u32 tail;

	u16 buf_size;
	u16 desc_num;
	int next_to_use;
	int next_to_clean;
	u8 flag;
	spinlock_t lock; /* command queue lock */
};

struct hns_roce_v2_cmq {
	struct hns_roce_v2_cmq_ring csq;
	struct hns_roce_v2_cmq_ring crq;
	u16 tx_timeout;
	u16 last_status;
};

enum hns_roce_link_table_type {
	TSQ_LINK_TABLE,
	TPQ_LINK_TABLE,
};

struct hns_roce_link_table {
	struct hns_roce_buf_list table;
	struct hns_roce_buf_list *pg_list;
	u32 npages;
	u32 pg_sz;
};

struct hns_roce_link_table_entry {
	u32 blk_ba0;
	u32 blk_ba1_nxt_ptr;
};
#define HNS_ROCE_LINK_TABLE_BA1_S 0
#define HNS_ROCE_LINK_TABLE_BA1_M GENMASK(19, 0)

#define HNS_ROCE_LINK_TABLE_NXT_PTR_S 20
#define HNS_ROCE_LINK_TABLE_NXT_PTR_M GENMASK(31, 20)

struct hns_roce_v2_priv {
	struct hns_roce_v2_cmq cmq;
	struct hns_roce_link_table tsq;
	struct hns_roce_link_table tpq;
};

struct hns_roce_eq_context {
	__le32	byte_4;
	__le32	byte_8;
	__le32	byte_12;
	__le32	eqe_report_timer;
	__le32	eqe_ba0;
	__le32	eqe_ba1;
	__le32	byte_28;
	__le32	byte_32;
	__le32	byte_36;
	__le32	nxt_eqe_ba0;
	__le32	nxt_eqe_ba1;
	__le32	rsv[5];
};

#define HNS_ROCE_AEQ_DEFAULT_BURST_NUM	0x0
#define HNS_ROCE_AEQ_DEFAULT_INTERVAL	0x0
#define HNS_ROCE_CEQ_DEFAULT_BURST_NUM	0x0
#define HNS_ROCE_CEQ_DEFAULT_INTERVAL	0x0

#define HNS_ROCE_V2_EQ_STATE_INVALID		0
#define HNS_ROCE_V2_EQ_STATE_VALID		1
#define HNS_ROCE_V2_EQ_STATE_OVERFLOW		2
#define HNS_ROCE_V2_EQ_STATE_FAILURE		3

#define HNS_ROCE_V2_EQ_OVER_IGNORE_0		0
#define HNS_ROCE_V2_EQ_OVER_IGNORE_1		1

#define HNS_ROCE_V2_EQ_COALESCE_0		0
#define HNS_ROCE_V2_EQ_COALESCE_1		1

#define HNS_ROCE_V2_EQ_FIRED			0
#define HNS_ROCE_V2_EQ_ARMED			1
#define HNS_ROCE_V2_EQ_ALWAYS_ARMED		3

#define HNS_ROCE_EQ_INIT_EQE_CNT		0
#define HNS_ROCE_EQ_INIT_PROD_IDX		0
#define HNS_ROCE_EQ_INIT_REPORT_TIMER		0
#define HNS_ROCE_EQ_INIT_MSI_IDX		0
#define HNS_ROCE_EQ_INIT_CONS_IDX		0
#define HNS_ROCE_EQ_INIT_NXT_EQE_BA		0

#define HNS_ROCE_V2_CEQ_CEQE_OWNER_S		31
#define HNS_ROCE_V2_AEQ_AEQE_OWNER_S		31

#define HNS_ROCE_V2_COMP_EQE_NUM		0x1000
#define HNS_ROCE_V2_ASYNC_EQE_NUM		0x1000

#define HNS_ROCE_V2_VF_INT_ST_AEQ_OVERFLOW_S	0
#define HNS_ROCE_V2_VF_INT_ST_BUS_ERR_S		1
#define HNS_ROCE_V2_VF_INT_ST_OTHER_ERR_S	2

#define HNS_ROCE_EQ_DB_CMD_AEQ			0x0
#define HNS_ROCE_EQ_DB_CMD_AEQ_ARMED		0x1
#define HNS_ROCE_EQ_DB_CMD_CEQ			0x2
#define HNS_ROCE_EQ_DB_CMD_CEQ_ARMED		0x3

#define EQ_ENABLE				1
#define EQ_DISABLE				0

#define EQ_REG_OFFSET				0x4

#define HNS_ROCE_INT_NAME_LEN			32
#define HNS_ROCE_V2_EQN_M GENMASK(23, 0)

#define HNS_ROCE_V2_CONS_IDX_M GENMASK(23, 0)

#define HNS_ROCE_V2_VF_ABN_INT_EN_S 0
#define HNS_ROCE_V2_VF_ABN_INT_EN_M GENMASK(0, 0)
#define HNS_ROCE_V2_VF_ABN_INT_ST_M GENMASK(2, 0)
#define HNS_ROCE_V2_VF_ABN_INT_CFG_M GENMASK(2, 0)
#define HNS_ROCE_V2_VF_EVENT_INT_EN_M GENMASK(0, 0)

/* WORD0 */
#define HNS_ROCE_EQC_EQ_ST_S 0
#define HNS_ROCE_EQC_EQ_ST_M GENMASK(1, 0)

#define HNS_ROCE_EQC_HOP_NUM_S 2
#define HNS_ROCE_EQC_HOP_NUM_M GENMASK(3, 2)

#define HNS_ROCE_EQC_OVER_IGNORE_S 4
#define HNS_ROCE_EQC_OVER_IGNORE_M GENMASK(4, 4)

#define HNS_ROCE_EQC_COALESCE_S 5
#define HNS_ROCE_EQC_COALESCE_M GENMASK(5, 5)

#define HNS_ROCE_EQC_ARM_ST_S 6
#define HNS_ROCE_EQC_ARM_ST_M GENMASK(7, 6)

#define HNS_ROCE_EQC_EQN_S 8
#define HNS_ROCE_EQC_EQN_M GENMASK(15, 8)

#define HNS_ROCE_EQC_EQE_CNT_S 16
#define HNS_ROCE_EQC_EQE_CNT_M GENMASK(31, 16)

/* WORD1 */
#define HNS_ROCE_EQC_BA_PG_SZ_S 0
#define HNS_ROCE_EQC_BA_PG_SZ_M GENMASK(3, 0)

#define HNS_ROCE_EQC_BUF_PG_SZ_S 4
#define HNS_ROCE_EQC_BUF_PG_SZ_M GENMASK(7, 4)

#define HNS_ROCE_EQC_PROD_INDX_S 8
#define HNS_ROCE_EQC_PROD_INDX_M GENMASK(31, 8)

/* WORD2 */
#define HNS_ROCE_EQC_MAX_CNT_S 0
#define HNS_ROCE_EQC_MAX_CNT_M GENMASK(15, 0)

#define HNS_ROCE_EQC_PERIOD_S 16
#define HNS_ROCE_EQC_PERIOD_M GENMASK(31, 16)

/* WORD3 */
#define HNS_ROCE_EQC_REPORT_TIMER_S 0
#define HNS_ROCE_EQC_REPORT_TIMER_M GENMASK(31, 0)

/* WORD4 */
#define HNS_ROCE_EQC_EQE_BA_L_S 0
#define HNS_ROCE_EQC_EQE_BA_L_M GENMASK(31, 0)

/* WORD5 */
#define HNS_ROCE_EQC_EQE_BA_H_S 0
#define HNS_ROCE_EQC_EQE_BA_H_M GENMASK(28, 0)

/* WORD6 */
#define HNS_ROCE_EQC_SHIFT_S 0
#define HNS_ROCE_EQC_SHIFT_M GENMASK(7, 0)

#define HNS_ROCE_EQC_MSI_INDX_S 8
#define HNS_ROCE_EQC_MSI_INDX_M GENMASK(15, 8)

#define HNS_ROCE_EQC_CUR_EQE_BA_L_S 16
#define HNS_ROCE_EQC_CUR_EQE_BA_L_M GENMASK(31, 16)

/* WORD7 */
#define HNS_ROCE_EQC_CUR_EQE_BA_M_S 0
#define HNS_ROCE_EQC_CUR_EQE_BA_M_M GENMASK(31, 0)

/* WORD8 */
#define HNS_ROCE_EQC_CUR_EQE_BA_H_S 0
#define HNS_ROCE_EQC_CUR_EQE_BA_H_M GENMASK(3, 0)

#define HNS_ROCE_EQC_CONS_INDX_S 8
#define HNS_ROCE_EQC_CONS_INDX_M GENMASK(31, 8)

/* WORD9 */
#define HNS_ROCE_EQC_NXT_EQE_BA_L_S 0
#define HNS_ROCE_EQC_NXT_EQE_BA_L_M GENMASK(31, 0)

/* WORD10 */
#define HNS_ROCE_EQC_NXT_EQE_BA_H_S 0
#define HNS_ROCE_EQC_NXT_EQE_BA_H_M GENMASK(19, 0)

#define HNS_ROCE_V2_CEQE_COMP_CQN_S 0
#define HNS_ROCE_V2_CEQE_COMP_CQN_M GENMASK(23, 0)

#define HNS_ROCE_V2_AEQE_EVENT_TYPE_S 0
#define HNS_ROCE_V2_AEQE_EVENT_TYPE_M GENMASK(7, 0)

#define HNS_ROCE_V2_AEQE_SUB_TYPE_S 8
#define HNS_ROCE_V2_AEQE_SUB_TYPE_M GENMASK(15, 8)

#define HNS_ROCE_V2_EQ_DB_CMD_S	16
#define HNS_ROCE_V2_EQ_DB_CMD_M	GENMASK(17, 16)

#define HNS_ROCE_V2_EQ_DB_TAG_S	0
#define HNS_ROCE_V2_EQ_DB_TAG_M	GENMASK(7, 0)

#define HNS_ROCE_V2_EQ_DB_PARA_S 0
#define HNS_ROCE_V2_EQ_DB_PARA_M GENMASK(23, 0)

#define HNS_ROCE_V2_AEQE_EVENT_QUEUE_NUM_S 0
#define HNS_ROCE_V2_AEQE_EVENT_QUEUE_NUM_M GENMASK(23, 0)

struct hns_roce_wqe_atomic_seg {
	__le64          fetchadd_swap_data;
	__le64          cmp_data;
};

#endif
