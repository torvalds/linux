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
#define HNS_ROCE_V2_MAX_CQE_NUM			0x400000
#define HNS_ROCE_V2_MAX_RQ_SGE_NUM		0x100
#define HNS_ROCE_V2_MAX_SQ_SGE_NUM		0xff
#define HNS_ROCE_V2_MAX_SQ_INLINE		0x20
#define HNS_ROCE_V2_UAR_NUM			256
#define HNS_ROCE_V2_PHY_UAR_NUM			1
#define HNS_ROCE_V2_MAX_MTPT_NUM		0x8000
#define HNS_ROCE_V2_MAX_MTT_SEGS		0x100000
#define HNS_ROCE_V2_MAX_CQE_SEGS		0x10000
#define HNS_ROCE_V2_MAX_PD_NUM			0x400000
#define HNS_ROCE_V2_MAX_QP_INIT_RDMA		128
#define HNS_ROCE_V2_MAX_QP_DEST_RDMA		128
#define HNS_ROCE_V2_MAX_SQ_DESC_SZ		64
#define HNS_ROCE_V2_MAX_RQ_DESC_SZ		16
#define HNS_ROCE_V2_MAX_SRQ_DESC_SZ		64
#define HNS_ROCE_V2_QPC_ENTRY_SZ		256
#define HNS_ROCE_V2_IRRL_ENTRY_SZ		64
#define HNS_ROCE_V2_CQC_ENTRY_SZ		64
#define HNS_ROCE_V2_MTPT_ENTRY_SZ		64
#define HNS_ROCE_V2_MTT_ENTRY_SZ		64
#define HNS_ROCE_V2_CQE_ENTRY_SIZE		32
#define HNS_ROCE_V2_PAGE_SIZE_SUPPORTED		0xFFFFF000
#define HNS_ROCE_V2_MAX_INNER_MTPT_NUM		2
#define HNS_ROCE_CMQ_TX_TIMEOUT			200

#define HNS_ROCE_CONTEXT_HOP_NUM		1
#define HNS_ROCE_MTT_HOP_NUM			1
#define HNS_ROCE_CQE_HOP_NUM			1
#define HNS_ROCE_PBL_HOP_NUM			2

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

#define V2_CQ_DB_REQ_NOT_SOL			0
#define V2_CQ_DB_REQ_NOT			1

#define V2_CQ_STATE_VALID			1

#define HNS_ROCE_V2_CQE_QPN_MASK		0x3ffff

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

struct hns_roce_v2_cq_context {
	u32	byte_4_pg_ceqn;
	u32	byte_8_cqn;
	u32	cqe_cur_blk_addr;
	u32	byte_16_hop_addr;
	u32	cqe_nxt_blk_addr;
	u32	byte_24_pgsz_addr;
	u32	byte_28_cq_pi;
	u32	byte_32_cq_ci;
	u32	cqe_ba;
	u32	byte_40_cqe_ba;
	u32	byte_44_db_record;
	u32	db_record_addr;
	u32	byte_52_cqe_cnt;
	u32	byte_56_cqe_period_maxcnt;
	u32	cqe_report_timer;
	u32	byte_64_se_cqe_idx;
};
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

#define	V2_CQC_BYTE_52_CQE_CNT_S 0
#define	V2_CQC_BYTE_52_CQE_CNT_M GENMASK(23, 0)

#define	V2_CQC_BYTE_56_CQ_MAX_CNT_S 0
#define V2_CQC_BYTE_56_CQ_MAX_CNT_M GENMASK(15, 0)

#define	V2_CQC_BYTE_56_CQ_PERIOD_S 16
#define V2_CQC_BYTE_56_CQ_PERIOD_M GENMASK(31, 16)

#define	V2_CQC_BYTE_64_SE_CQE_IDX_S 0
#define	V2_CQC_BYTE_64_SE_CQE_IDX_M GENMASK(23, 0)

struct hns_roce_v2_cqe {
	u32	byte_4;
	u32	rkey_immtdata;
	u32	byte_12;
	u32	byte_16;
	u32	byte_cnt;
	u32	smac;
	u32	byte_28;
	u32	byte_32;
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

#define	V2_CQE_BYTE_32_RMT_QPN_S 0
#define V2_CQE_BYTE_32_RMT_QPN_M GENMASK(23, 0)

#define	V2_CQE_BYTE_32_SL_S 24
#define V2_CQE_BYTE_32_SL_M GENMASK(26, 24)

#define	V2_CQE_BYTE_32_PORTN_S 27
#define V2_CQE_BYTE_32_PORTN_M GENMASK(29, 27)

#define	V2_CQE_BYTE_32_GRH_S 30

#define	V2_CQE_BYTE_32_LPK_S 31

#define	V2_DB_BYTE_4_TAG_S 0
#define V2_DB_BYTE_4_TAG_M GENMASK(23, 0)

#define	V2_DB_BYTE_4_CMD_S 24
#define V2_DB_BYTE_4_CMD_M GENMASK(27, 24)

struct hns_roce_v2_cq_db {
	u32	byte_4;
	u32	parameter;
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

struct hns_roce_query_version {
	__le16 rocee_vendor_id;
	__le16 rocee_hw_version;
	__le32 rsv[5];
};

struct hns_roce_cfg_global_param {
	__le32 time_cfg_udp_port;
	__le32 rsv[5];
};

#define CFG_GLOBAL_PARAM_DATA_0_ROCEE_TIME_1US_CFG_S 0
#define CFG_GLOBAL_PARAM_DATA_0_ROCEE_TIME_1US_CFG_M GENMASK(9, 0)

#define CFG_GLOBAL_PARAM_DATA_0_ROCEE_UDP_PORT_S 16
#define CFG_GLOBAL_PARAM_DATA_0_ROCEE_UDP_PORT_M GENMASK(31, 16)

struct hns_roce_pf_res {
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

struct hns_roce_vf_res_a {
	u32 vf_id;
	u32 vf_qpc_bt_idx_num;
	u32 vf_srqc_bt_idx_num;
	u32 vf_cqc_bt_idx_num;
	u32 vf_mpt_bt_idx_num;
	u32 vf_eqc_bt_idx_num;
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
	u32 rsv0;
	u32 vf_smac_idx_num;
	u32 vf_sgid_idx_num;
	u32 vf_qid_idx_sl_num;
	u32 rsv[2];
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

/* Reg field definition */
#define ROCEE_VF_SMAC_CFG1_VF_SMAC_H_S 0
#define ROCEE_VF_SMAC_CFG1_VF_SMAC_H_M GENMASK(15, 0)

#define ROCEE_VF_SGID_CFG4_SGID_TYPE_S 0
#define ROCEE_VF_SGID_CFG4_SGID_TYPE_M GENMASK(1, 0)

struct hns_roce_cfg_bt_attr {
	u32 vf_qpc_cfg;
	u32 vf_srqc_cfg;
	u32 vf_cqc_cfg;
	u32 vf_mpt_cfg;
	u32 rsv[2];
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

struct hns_roce_cmq_desc {
	u16 opcode;
	u16 flag;
	u16 retval;
	u16 rsv;
	u32 data[6];
};

#define ROCEE_VF_MB_CFG0_REG		0x40
#define ROCEE_VF_MB_STATUS_REG		0x58

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

struct hns_roce_v2_priv {
	struct hns_roce_v2_cmq cmq;
};

#endif
