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
