// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2023 Hisilicon Limited.

#include "hclgevf_main.h"
#include "hclgevf_regs.h"
#include "hnae3.h"

static const u32 cmdq_reg_addr_list[] = {HCLGE_COMM_NIC_CSQ_BASEADDR_L_REG,
					 HCLGE_COMM_NIC_CSQ_BASEADDR_H_REG,
					 HCLGE_COMM_NIC_CSQ_DEPTH_REG,
					 HCLGE_COMM_NIC_CSQ_TAIL_REG,
					 HCLGE_COMM_NIC_CSQ_HEAD_REG,
					 HCLGE_COMM_NIC_CRQ_BASEADDR_L_REG,
					 HCLGE_COMM_NIC_CRQ_BASEADDR_H_REG,
					 HCLGE_COMM_NIC_CRQ_DEPTH_REG,
					 HCLGE_COMM_NIC_CRQ_TAIL_REG,
					 HCLGE_COMM_NIC_CRQ_HEAD_REG,
					 HCLGE_COMM_VECTOR0_CMDQ_SRC_REG,
					 HCLGE_COMM_VECTOR0_CMDQ_STATE_REG,
					 HCLGE_COMM_CMDQ_INTR_EN_REG,
					 HCLGE_COMM_CMDQ_INTR_GEN_REG};

static const u32 common_reg_addr_list[] = {HCLGEVF_MISC_VECTOR_REG_BASE,
					   HCLGEVF_RST_ING,
					   HCLGEVF_GRO_EN_REG};

static const u32 ring_reg_addr_list[] = {HCLGEVF_RING_RX_ADDR_L_REG,
					 HCLGEVF_RING_RX_ADDR_H_REG,
					 HCLGEVF_RING_RX_BD_NUM_REG,
					 HCLGEVF_RING_RX_BD_LENGTH_REG,
					 HCLGEVF_RING_RX_MERGE_EN_REG,
					 HCLGEVF_RING_RX_TAIL_REG,
					 HCLGEVF_RING_RX_HEAD_REG,
					 HCLGEVF_RING_RX_FBD_NUM_REG,
					 HCLGEVF_RING_RX_OFFSET_REG,
					 HCLGEVF_RING_RX_FBD_OFFSET_REG,
					 HCLGEVF_RING_RX_STASH_REG,
					 HCLGEVF_RING_RX_BD_ERR_REG,
					 HCLGEVF_RING_TX_ADDR_L_REG,
					 HCLGEVF_RING_TX_ADDR_H_REG,
					 HCLGEVF_RING_TX_BD_NUM_REG,
					 HCLGEVF_RING_TX_PRIORITY_REG,
					 HCLGEVF_RING_TX_TC_REG,
					 HCLGEVF_RING_TX_MERGE_EN_REG,
					 HCLGEVF_RING_TX_TAIL_REG,
					 HCLGEVF_RING_TX_HEAD_REG,
					 HCLGEVF_RING_TX_FBD_NUM_REG,
					 HCLGEVF_RING_TX_OFFSET_REG,
					 HCLGEVF_RING_TX_EBD_NUM_REG,
					 HCLGEVF_RING_TX_EBD_OFFSET_REG,
					 HCLGEVF_RING_TX_BD_ERR_REG,
					 HCLGEVF_RING_EN_REG};

static const u32 tqp_intr_reg_addr_list[] = {HCLGEVF_TQP_INTR_CTRL_REG,
					     HCLGEVF_TQP_INTR_GL0_REG,
					     HCLGEVF_TQP_INTR_GL1_REG,
					     HCLGEVF_TQP_INTR_GL2_REG,
					     HCLGEVF_TQP_INTR_RL_REG};

enum hclgevf_reg_tag {
	HCLGEVF_REG_TAG_CMDQ = 0,
	HCLGEVF_REG_TAG_COMMON,
	HCLGEVF_REG_TAG_RING,
	HCLGEVF_REG_TAG_TQP_INTR,
};

#pragma pack(4)
struct hclgevf_reg_tlv {
	u16 tag;
	u16 len;
};

struct hclgevf_reg_header {
	u64 magic_number;
	u8 is_vf;
	u8 rsv[7];
};

#pragma pack()

#define HCLGEVF_REG_TLV_SIZE		sizeof(struct hclgevf_reg_tlv)
#define HCLGEVF_REG_HEADER_SIZE		sizeof(struct hclgevf_reg_header)
#define HCLGEVF_REG_TLV_SPACE		(sizeof(struct hclgevf_reg_tlv) / sizeof(u32))
#define HCLGEVF_REG_HEADER_SPACE	(sizeof(struct hclgevf_reg_header) / sizeof(u32))
#define HCLGEVF_REG_MAGIC_NUMBER	0x686e733372656773 /* meaning is hns3regs */

static u32 hclgevf_reg_get_header(void *data)
{
	struct hclgevf_reg_header *header = data;

	header->magic_number = HCLGEVF_REG_MAGIC_NUMBER;
	header->is_vf = 0x1;

	return HCLGEVF_REG_HEADER_SPACE;
}

static u32 hclgevf_reg_get_tlv(u32 tag, u32 regs_num, void *data)
{
	struct hclgevf_reg_tlv *tlv = data;

	tlv->tag = tag;
	tlv->len = regs_num * sizeof(u32) + HCLGEVF_REG_TLV_SIZE;

	return HCLGEVF_REG_TLV_SPACE;
}

int hclgevf_get_regs_len(struct hnae3_handle *handle)
{
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int cmdq_len, common_len, ring_len, tqp_intr_len;

	cmdq_len = HCLGEVF_REG_TLV_SIZE + sizeof(cmdq_reg_addr_list);
	common_len = HCLGEVF_REG_TLV_SIZE + sizeof(common_reg_addr_list);
	ring_len = HCLGEVF_REG_TLV_SIZE + sizeof(ring_reg_addr_list);
	tqp_intr_len = HCLGEVF_REG_TLV_SIZE + sizeof(tqp_intr_reg_addr_list);

	/* return the total length of all register values */
	return HCLGEVF_REG_HEADER_SIZE + cmdq_len + common_len +
	       tqp_intr_len * (hdev->num_msi_used - 1) +
	       ring_len * hdev->num_tqps;
}

void hclgevf_get_regs(struct hnae3_handle *handle, u32 *version,
		      void *data)
{
#define HCLGEVF_RING_REG_OFFSET		0x200
#define HCLGEVF_RING_INT_REG_OFFSET	0x4

	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int i, j, reg_um;
	u32 *reg = data;

	*version = hdev->fw_version;
	reg += hclgevf_reg_get_header(reg);

	/* fetching per-VF registers values from VF PCIe register space */
	reg_um = ARRAY_SIZE(cmdq_reg_addr_list);
	reg += hclgevf_reg_get_tlv(HCLGEVF_REG_TAG_CMDQ, reg_um, reg);
	for (i = 0; i < reg_um; i++)
		*reg++ = hclgevf_read_dev(&hdev->hw, cmdq_reg_addr_list[i]);

	reg_um = ARRAY_SIZE(common_reg_addr_list);
	reg += hclgevf_reg_get_tlv(HCLGEVF_REG_TAG_COMMON, reg_um, reg);
	for (i = 0; i < reg_um; i++)
		*reg++ = hclgevf_read_dev(&hdev->hw, common_reg_addr_list[i]);

	reg_um = ARRAY_SIZE(ring_reg_addr_list);
	for (j = 0; j < hdev->num_tqps; j++) {
		reg += hclgevf_reg_get_tlv(HCLGEVF_REG_TAG_RING, reg_um, reg);
		for (i = 0; i < reg_um; i++)
			*reg++ = hclgevf_read_dev(&hdev->hw,
						  ring_reg_addr_list[i] +
						  HCLGEVF_RING_REG_OFFSET * j);
	}

	reg_um = ARRAY_SIZE(tqp_intr_reg_addr_list);
	for (j = 0; j < hdev->num_msi_used - 1; j++) {
		reg += hclgevf_reg_get_tlv(HCLGEVF_REG_TAG_TQP_INTR, reg_um, reg);
		for (i = 0; i < reg_um; i++)
			*reg++ = hclgevf_read_dev(&hdev->hw,
						  tqp_intr_reg_addr_list[i] +
						  HCLGEVF_RING_INT_REG_OFFSET * j);
	}
}
