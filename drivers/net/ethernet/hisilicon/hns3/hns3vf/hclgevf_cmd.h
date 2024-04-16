/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2016-2017 Hisilicon Limited. */

#ifndef __HCLGEVF_CMD_H
#define __HCLGEVF_CMD_H
#include <linux/io.h>
#include <linux/types.h>
#include "hnae3.h"
#include "hclge_comm_cmd.h"

#define HCLGEVF_CMDQ_RX_INVLD_B		0
#define HCLGEVF_CMDQ_RX_OUTVLD_B	1

struct hclgevf_hw;
struct hclgevf_dev;

#define HCLGEVF_SYNC_RX_RING_HEAD_EN_B	4

#define HCLGEVF_TQP_REG_OFFSET		0x80000
#define HCLGEVF_TQP_REG_SIZE		0x200

#define HCLGEVF_TQP_MAX_SIZE_DEV_V2	1024
#define HCLGEVF_TQP_EXT_REG_OFFSET	0x100

struct hclgevf_tqp_map {
	__le16 tqp_id;	/* Absolute tqp id for in this pf */
	u8 tqp_vf; /* VF id */
#define HCLGEVF_TQP_MAP_TYPE_PF		0
#define HCLGEVF_TQP_MAP_TYPE_VF		1
#define HCLGEVF_TQP_MAP_TYPE_B		0
#define HCLGEVF_TQP_MAP_EN_B		1
	u8 tqp_flag;	/* Indicate it's pf or vf tqp */
	__le16 tqp_vid; /* Virtual id in this pf/vf */
	u8 rsv[18];
};

#define HCLGEVF_VECTOR_ELEMENTS_PER_CMD	10

enum hclgevf_int_type {
	HCLGEVF_INT_TX = 0,
	HCLGEVF_INT_RX,
	HCLGEVF_INT_EVENT,
};

struct hclgevf_ctrl_vector_chain {
	u8 int_vector_id;
	u8 int_cause_num;
#define HCLGEVF_INT_TYPE_S	0
#define HCLGEVF_INT_TYPE_M	0x3
#define HCLGEVF_TQP_ID_S	2
#define HCLGEVF_TQP_ID_M	(0x3fff << HCLGEVF_TQP_ID_S)
	__le16 tqp_type_and_id[HCLGEVF_VECTOR_ELEMENTS_PER_CMD];
	u8 vfid;
	u8 resv;
};

#define HCLGEVF_MSIX_OFT_ROCEE_S       0
#define HCLGEVF_MSIX_OFT_ROCEE_M       (0xffff << HCLGEVF_MSIX_OFT_ROCEE_S)
#define HCLGEVF_VEC_NUM_S              0
#define HCLGEVF_VEC_NUM_M              (0xff << HCLGEVF_VEC_NUM_S)
struct hclgevf_query_res_cmd {
	__le16 tqp_num;
	__le16 reserved;
	__le16 msixcap_localid_ba_nic;
	__le16 msixcap_localid_ba_rocee;
	__le16 vf_intr_vector_number;
	__le16 rsv[7];
};

#define HCLGEVF_GRO_EN_B               0
struct hclgevf_cfg_gro_status_cmd {
	u8 gro_en;
	u8 rsv[23];
};

#define HCLGEVF_LINK_STS_B	0
#define HCLGEVF_LINK_STATUS	BIT(HCLGEVF_LINK_STS_B)
struct hclgevf_link_status_cmd {
	u8 status;
	u8 rsv[23];
};

#define HCLGEVF_RING_ID_MASK	0x3ff
#define HCLGEVF_TQP_ENABLE_B	0

struct hclgevf_cfg_com_tqp_queue_cmd {
	__le16 tqp_id;
	__le16 stream_id;
	u8 enable;
	u8 rsv[19];
};

struct hclgevf_cfg_tx_queue_pointer_cmd {
	__le16 tqp_id;
	__le16 tx_tail;
	__le16 tx_head;
	__le16 fbd_num;
	__le16 ring_offset;
	u8 rsv[14];
};

/* this bit indicates that the driver is ready for hardware reset */
#define HCLGEVF_NIC_SW_RST_RDY_B	16
#define HCLGEVF_NIC_SW_RST_RDY		BIT(HCLGEVF_NIC_SW_RST_RDY_B)

#define HCLGEVF_NIC_CMQ_DESC_NUM	1024
#define HCLGEVF_NIC_CMQ_DESC_NUM_S	3

#define HCLGEVF_QUERY_DEV_SPECS_BD_NUM		4

#define hclgevf_cmd_setup_basic_desc(desc, opcode, is_read) \
	hclge_comm_cmd_setup_basic_desc(desc, opcode, is_read)

struct hclgevf_dev_specs_0_cmd {
	__le32 rsv0;
	__le32 mac_entry_num;
	__le32 mng_entry_num;
	__le16 rss_ind_tbl_size;
	__le16 rss_key_size;
	__le16 int_ql_max;
	u8 max_non_tso_bd_num;
	u8 rsv1[5];
};

#define HCLGEVF_DEF_MAX_INT_GL		0x1FE0U

struct hclgevf_dev_specs_1_cmd {
	__le16 max_frm_size;
	__le16 rsv0;
	__le16 max_int_gl;
	u8 rsv1[18];
};

int hclgevf_cmd_send(struct hclgevf_hw *hw, struct hclge_desc *desc, int num);
void hclgevf_arq_init(struct hclgevf_dev *hdev);
#endif
