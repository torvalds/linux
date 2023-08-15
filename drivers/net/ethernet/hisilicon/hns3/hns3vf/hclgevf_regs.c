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

#define MAX_SEPARATE_NUM	4
#define SEPARATOR_VALUE		0xFDFCFBFA
#define REG_NUM_PER_LINE	4
#define REG_LEN_PER_LINE	(REG_NUM_PER_LINE * sizeof(u32))

int hclgevf_get_regs_len(struct hnae3_handle *handle)
{
	int cmdq_lines, common_lines, ring_lines, tqp_intr_lines;
	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);

	cmdq_lines = sizeof(cmdq_reg_addr_list) / REG_LEN_PER_LINE + 1;
	common_lines = sizeof(common_reg_addr_list) / REG_LEN_PER_LINE + 1;
	ring_lines = sizeof(ring_reg_addr_list) / REG_LEN_PER_LINE + 1;
	tqp_intr_lines = sizeof(tqp_intr_reg_addr_list) / REG_LEN_PER_LINE + 1;

	return (cmdq_lines + common_lines + ring_lines * hdev->num_tqps +
		tqp_intr_lines * (hdev->num_msi_used - 1)) * REG_LEN_PER_LINE;
}

void hclgevf_get_regs(struct hnae3_handle *handle, u32 *version,
		      void *data)
{
#define HCLGEVF_RING_REG_OFFSET		0x200
#define HCLGEVF_RING_INT_REG_OFFSET	0x4

	struct hclgevf_dev *hdev = hclgevf_ae_get_hdev(handle);
	int i, j, reg_um, separator_num;
	u32 *reg = data;

	*version = hdev->fw_version;

	/* fetching per-VF registers values from VF PCIe register space */
	reg_um = sizeof(cmdq_reg_addr_list) / sizeof(u32);
	separator_num = MAX_SEPARATE_NUM - reg_um % REG_NUM_PER_LINE;
	for (i = 0; i < reg_um; i++)
		*reg++ = hclgevf_read_dev(&hdev->hw, cmdq_reg_addr_list[i]);
	for (i = 0; i < separator_num; i++)
		*reg++ = SEPARATOR_VALUE;

	reg_um = sizeof(common_reg_addr_list) / sizeof(u32);
	separator_num = MAX_SEPARATE_NUM - reg_um % REG_NUM_PER_LINE;
	for (i = 0; i < reg_um; i++)
		*reg++ = hclgevf_read_dev(&hdev->hw, common_reg_addr_list[i]);
	for (i = 0; i < separator_num; i++)
		*reg++ = SEPARATOR_VALUE;

	reg_um = sizeof(ring_reg_addr_list) / sizeof(u32);
	separator_num = MAX_SEPARATE_NUM - reg_um % REG_NUM_PER_LINE;
	for (j = 0; j < hdev->num_tqps; j++) {
		for (i = 0; i < reg_um; i++)
			*reg++ = hclgevf_read_dev(&hdev->hw,
						  ring_reg_addr_list[i] +
						  HCLGEVF_RING_REG_OFFSET * j);
		for (i = 0; i < separator_num; i++)
			*reg++ = SEPARATOR_VALUE;
	}

	reg_um = sizeof(tqp_intr_reg_addr_list) / sizeof(u32);
	separator_num = MAX_SEPARATE_NUM - reg_um % REG_NUM_PER_LINE;
	for (j = 0; j < hdev->num_msi_used - 1; j++) {
		for (i = 0; i < reg_um; i++)
			*reg++ = hclgevf_read_dev(&hdev->hw,
						  tqp_intr_reg_addr_list[i] +
						  HCLGEVF_RING_INT_REG_OFFSET * j);
		for (i = 0; i < separator_num; i++)
			*reg++ = SEPARATOR_VALUE;
	}
}
