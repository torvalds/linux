// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2023 Hisilicon Limited.

#include "hclge_cmd.h"
#include "hclge_main.h"
#include "hclge_regs.h"
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
					 HCLGE_COMM_CMDQ_INTR_STS_REG,
					 HCLGE_COMM_CMDQ_INTR_EN_REG,
					 HCLGE_COMM_CMDQ_INTR_GEN_REG};

static const u32 common_reg_addr_list[] = {HCLGE_MISC_VECTOR_REG_BASE,
					   HCLGE_PF_OTHER_INT_REG,
					   HCLGE_MISC_RESET_STS_REG,
					   HCLGE_MISC_VECTOR_INT_STS,
					   HCLGE_GLOBAL_RESET_REG,
					   HCLGE_FUN_RST_ING,
					   HCLGE_GRO_EN_REG};

static const u32 ring_reg_addr_list[] = {HCLGE_RING_RX_ADDR_L_REG,
					 HCLGE_RING_RX_ADDR_H_REG,
					 HCLGE_RING_RX_BD_NUM_REG,
					 HCLGE_RING_RX_BD_LENGTH_REG,
					 HCLGE_RING_RX_MERGE_EN_REG,
					 HCLGE_RING_RX_TAIL_REG,
					 HCLGE_RING_RX_HEAD_REG,
					 HCLGE_RING_RX_FBD_NUM_REG,
					 HCLGE_RING_RX_OFFSET_REG,
					 HCLGE_RING_RX_FBD_OFFSET_REG,
					 HCLGE_RING_RX_STASH_REG,
					 HCLGE_RING_RX_BD_ERR_REG,
					 HCLGE_RING_TX_ADDR_L_REG,
					 HCLGE_RING_TX_ADDR_H_REG,
					 HCLGE_RING_TX_BD_NUM_REG,
					 HCLGE_RING_TX_PRIORITY_REG,
					 HCLGE_RING_TX_TC_REG,
					 HCLGE_RING_TX_MERGE_EN_REG,
					 HCLGE_RING_TX_TAIL_REG,
					 HCLGE_RING_TX_HEAD_REG,
					 HCLGE_RING_TX_FBD_NUM_REG,
					 HCLGE_RING_TX_OFFSET_REG,
					 HCLGE_RING_TX_EBD_NUM_REG,
					 HCLGE_RING_TX_EBD_OFFSET_REG,
					 HCLGE_RING_TX_BD_ERR_REG,
					 HCLGE_RING_EN_REG};

static const u32 tqp_intr_reg_addr_list[] = {HCLGE_TQP_INTR_CTRL_REG,
					     HCLGE_TQP_INTR_GL0_REG,
					     HCLGE_TQP_INTR_GL1_REG,
					     HCLGE_TQP_INTR_GL2_REG,
					     HCLGE_TQP_INTR_RL_REG};

/* Get DFX BD number offset */
#define HCLGE_DFX_BIOS_BD_OFFSET        1
#define HCLGE_DFX_SSU_0_BD_OFFSET       2
#define HCLGE_DFX_SSU_1_BD_OFFSET       3
#define HCLGE_DFX_IGU_BD_OFFSET         4
#define HCLGE_DFX_RPU_0_BD_OFFSET       5
#define HCLGE_DFX_RPU_1_BD_OFFSET       6
#define HCLGE_DFX_NCSI_BD_OFFSET        7
#define HCLGE_DFX_RTC_BD_OFFSET         8
#define HCLGE_DFX_PPP_BD_OFFSET         9
#define HCLGE_DFX_RCB_BD_OFFSET         10
#define HCLGE_DFX_TQP_BD_OFFSET         11
#define HCLGE_DFX_SSU_2_BD_OFFSET       12

static const u32 hclge_dfx_bd_offset_list[] = {
	HCLGE_DFX_BIOS_BD_OFFSET,
	HCLGE_DFX_SSU_0_BD_OFFSET,
	HCLGE_DFX_SSU_1_BD_OFFSET,
	HCLGE_DFX_IGU_BD_OFFSET,
	HCLGE_DFX_RPU_0_BD_OFFSET,
	HCLGE_DFX_RPU_1_BD_OFFSET,
	HCLGE_DFX_NCSI_BD_OFFSET,
	HCLGE_DFX_RTC_BD_OFFSET,
	HCLGE_DFX_PPP_BD_OFFSET,
	HCLGE_DFX_RCB_BD_OFFSET,
	HCLGE_DFX_TQP_BD_OFFSET,
	HCLGE_DFX_SSU_2_BD_OFFSET
};

static const enum hclge_opcode_type hclge_dfx_reg_opcode_list[] = {
	HCLGE_OPC_DFX_BIOS_COMMON_REG,
	HCLGE_OPC_DFX_SSU_REG_0,
	HCLGE_OPC_DFX_SSU_REG_1,
	HCLGE_OPC_DFX_IGU_EGU_REG,
	HCLGE_OPC_DFX_RPU_REG_0,
	HCLGE_OPC_DFX_RPU_REG_1,
	HCLGE_OPC_DFX_NCSI_REG,
	HCLGE_OPC_DFX_RTC_REG,
	HCLGE_OPC_DFX_PPP_REG,
	HCLGE_OPC_DFX_RCB_REG,
	HCLGE_OPC_DFX_TQP_REG,
	HCLGE_OPC_DFX_SSU_REG_2
};

enum hclge_reg_tag {
	HCLGE_REG_TAG_CMDQ = 0,
	HCLGE_REG_TAG_COMMON,
	HCLGE_REG_TAG_RING,
	HCLGE_REG_TAG_TQP_INTR,
	HCLGE_REG_TAG_QUERY_32_BIT,
	HCLGE_REG_TAG_QUERY_64_BIT,
	HCLGE_REG_TAG_DFX_BIOS_COMMON,
	HCLGE_REG_TAG_DFX_SSU_0,
	HCLGE_REG_TAG_DFX_SSU_1,
	HCLGE_REG_TAG_DFX_IGU_EGU,
	HCLGE_REG_TAG_DFX_RPU_0,
	HCLGE_REG_TAG_DFX_RPU_1,
	HCLGE_REG_TAG_DFX_NCSI,
	HCLGE_REG_TAG_DFX_RTC,
	HCLGE_REG_TAG_DFX_PPP,
	HCLGE_REG_TAG_DFX_RCB,
	HCLGE_REG_TAG_DFX_TQP,
	HCLGE_REG_TAG_DFX_SSU_2,
	HCLGE_REG_TAG_RPU_TNL,
};

#pragma pack(4)
struct hclge_reg_tlv {
	u16 tag;
	u16 len;
};

struct hclge_reg_header {
	u64 magic_number;
	u8 is_vf;
	u8 rsv[7];
};

#pragma pack()

#define HCLGE_REG_TLV_SIZE	sizeof(struct hclge_reg_tlv)
#define HCLGE_REG_HEADER_SIZE	sizeof(struct hclge_reg_header)
#define HCLGE_REG_TLV_SPACE	(sizeof(struct hclge_reg_tlv) / sizeof(u32))
#define HCLGE_REG_HEADER_SPACE	(sizeof(struct hclge_reg_header) / sizeof(u32))
#define HCLGE_REG_MAGIC_NUMBER	0x686e733372656773 /* meaning is hns3regs */

#define HCLGE_REG_RPU_TNL_ID_0	1

static u32 hclge_reg_get_header(void *data)
{
	struct hclge_reg_header *header = data;

	header->magic_number = HCLGE_REG_MAGIC_NUMBER;
	header->is_vf = 0x0;

	return HCLGE_REG_HEADER_SPACE;
}

static u32 hclge_reg_get_tlv(u32 tag, u32 regs_num, void *data)
{
	struct hclge_reg_tlv *tlv = data;

	tlv->tag = tag;
	tlv->len = regs_num * sizeof(u32) + HCLGE_REG_TLV_SIZE;

	return HCLGE_REG_TLV_SPACE;
}

static int hclge_get_32_bit_regs(struct hclge_dev *hdev, u32 regs_num,
				 void *data)
{
#define HCLGE_32_BIT_REG_RTN_DATANUM 8
#define HCLGE_32_BIT_DESC_NODATA_LEN 2

	struct hclge_desc *desc;
	u32 *reg_val = data;
	__le32 *desc_data;
	int nodata_num;
	int cmd_num;
	int i, k, n;
	int ret;

	if (regs_num == 0)
		return 0;

	nodata_num = HCLGE_32_BIT_DESC_NODATA_LEN;
	cmd_num = DIV_ROUND_UP(regs_num + nodata_num,
			       HCLGE_32_BIT_REG_RTN_DATANUM);
	desc = kcalloc(cmd_num, sizeof(struct hclge_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_QUERY_32_BIT_REG, true);
	ret = hclge_cmd_send(&hdev->hw, desc, cmd_num);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Query 32 bit register cmd failed, ret = %d.\n", ret);
		kfree(desc);
		return ret;
	}

	for (i = 0; i < cmd_num; i++) {
		if (i == 0) {
			desc_data = (__le32 *)(&desc[i].data[0]);
			n = HCLGE_32_BIT_REG_RTN_DATANUM - nodata_num;
		} else {
			desc_data = (__le32 *)(&desc[i]);
			n = HCLGE_32_BIT_REG_RTN_DATANUM;
		}
		for (k = 0; k < n; k++) {
			*reg_val++ = le32_to_cpu(*desc_data++);

			regs_num--;
			if (!regs_num)
				break;
		}
	}

	kfree(desc);
	return 0;
}

static int hclge_get_64_bit_regs(struct hclge_dev *hdev, u32 regs_num,
				 void *data)
{
#define HCLGE_64_BIT_REG_RTN_DATANUM 4
#define HCLGE_64_BIT_DESC_NODATA_LEN 1

	struct hclge_desc *desc;
	u64 *reg_val = data;
	__le64 *desc_data;
	int nodata_len;
	int cmd_num;
	int i, k, n;
	int ret;

	if (regs_num == 0)
		return 0;

	nodata_len = HCLGE_64_BIT_DESC_NODATA_LEN;
	cmd_num = DIV_ROUND_UP(regs_num + nodata_len,
			       HCLGE_64_BIT_REG_RTN_DATANUM);
	desc = kcalloc(cmd_num, sizeof(struct hclge_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_QUERY_64_BIT_REG, true);
	ret = hclge_cmd_send(&hdev->hw, desc, cmd_num);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Query 64 bit register cmd failed, ret = %d.\n", ret);
		kfree(desc);
		return ret;
	}

	for (i = 0; i < cmd_num; i++) {
		if (i == 0) {
			desc_data = (__le64 *)(&desc[i].data[0]);
			n = HCLGE_64_BIT_REG_RTN_DATANUM - nodata_len;
		} else {
			desc_data = (__le64 *)(&desc[i]);
			n = HCLGE_64_BIT_REG_RTN_DATANUM;
		}
		for (k = 0; k < n; k++) {
			*reg_val++ = le64_to_cpu(*desc_data++);

			regs_num--;
			if (!regs_num)
				break;
		}
	}

	kfree(desc);
	return 0;
}

int hclge_query_bd_num_cmd_send(struct hclge_dev *hdev, struct hclge_desc *desc)
{
	int i;

	/* initialize command BD except the last one */
	for (i = 0; i < HCLGE_GET_DFX_REG_TYPE_CNT - 1; i++) {
		hclge_cmd_setup_basic_desc(&desc[i], HCLGE_OPC_DFX_BD_NUM,
					   true);
		desc[i].flag |= cpu_to_le16(HCLGE_COMM_CMD_FLAG_NEXT);
	}

	/* initialize the last command BD */
	hclge_cmd_setup_basic_desc(&desc[i], HCLGE_OPC_DFX_BD_NUM, true);

	return hclge_cmd_send(&hdev->hw, desc, HCLGE_GET_DFX_REG_TYPE_CNT);
}

static int hclge_get_dfx_reg_bd_num(struct hclge_dev *hdev,
				    int *bd_num_list,
				    u32 type_num)
{
	u32 entries_per_desc, desc_index, index, offset, i;
	struct hclge_desc desc[HCLGE_GET_DFX_REG_TYPE_CNT];
	int ret;

	ret = hclge_query_bd_num_cmd_send(hdev, desc);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get dfx bd num fail, status is %d.\n", ret);
		return ret;
	}

	entries_per_desc = ARRAY_SIZE(desc[0].data);
	for (i = 0; i < type_num; i++) {
		offset = hclge_dfx_bd_offset_list[i];
		index = offset % entries_per_desc;
		desc_index = offset / entries_per_desc;
		bd_num_list[i] = le32_to_cpu(desc[desc_index].data[index]);
	}

	return ret;
}

static int hclge_dfx_reg_cmd_send(struct hclge_dev *hdev,
				  struct hclge_desc *desc_src, int bd_num,
				  enum hclge_opcode_type cmd)
{
	struct hclge_desc *desc = desc_src;
	int i, ret;

	hclge_cmd_setup_basic_desc(desc, cmd, true);
	for (i = 0; i < bd_num - 1; i++) {
		desc->flag |= cpu_to_le16(HCLGE_COMM_CMD_FLAG_NEXT);
		desc++;
		hclge_cmd_setup_basic_desc(desc, cmd, true);
	}

	desc = desc_src;
	ret = hclge_cmd_send(&hdev->hw, desc, bd_num);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Query dfx reg cmd(0x%x) send fail, status is %d.\n",
			cmd, ret);

	return ret;
}

/* tnl_id = 0 means get sum of all tnl reg's value */
static int hclge_dfx_reg_rpu_tnl_cmd_send(struct hclge_dev *hdev, u32 tnl_id,
					  struct hclge_desc *desc, int bd_num)
{
	int i, ret;

	for (i = 0; i < bd_num; i++) {
		hclge_cmd_setup_basic_desc(&desc[i], HCLGE_OPC_DFX_RPU_REG_0,
					   true);
		if (i != bd_num - 1)
			desc[i].flag |= cpu_to_le16(HCLGE_COMM_CMD_FLAG_NEXT);
	}

	desc[0].data[0] = cpu_to_le32(tnl_id);
	ret = hclge_cmd_send(&hdev->hw, desc, bd_num);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"failed to query dfx rpu tnl reg, ret = %d\n",
			ret);
	return ret;
}

static int hclge_dfx_reg_fetch_data(struct hclge_desc *desc_src, int bd_num,
				    void *data)
{
	int entries_per_desc, reg_num, desc_index, index, i;
	struct hclge_desc *desc = desc_src;
	u32 *reg = data;

	entries_per_desc = ARRAY_SIZE(desc->data);
	reg_num = entries_per_desc * bd_num;
	for (i = 0; i < reg_num; i++) {
		index = i % entries_per_desc;
		desc_index = i / entries_per_desc;
		*reg++ = le32_to_cpu(desc[desc_index].data[index]);
	}

	return reg_num;
}

static int hclge_get_dfx_reg_len(struct hclge_dev *hdev, int *len)
{
	u32 dfx_reg_type_num = ARRAY_SIZE(hclge_dfx_bd_offset_list);
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	int data_len_per_desc;
	int *bd_num_list;
	int ret;
	u32 i;

	bd_num_list = kcalloc(dfx_reg_type_num, sizeof(int), GFP_KERNEL);
	if (!bd_num_list)
		return -ENOMEM;

	ret = hclge_get_dfx_reg_bd_num(hdev, bd_num_list, dfx_reg_type_num);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get dfx reg bd num fail, status is %d.\n", ret);
		goto out;
	}

	data_len_per_desc = sizeof_field(struct hclge_desc, data);
	*len = 0;
	for (i = 0; i < dfx_reg_type_num; i++)
		*len += bd_num_list[i] * data_len_per_desc + HCLGE_REG_TLV_SIZE;

	/**
	 * the num of dfx_rpu_0 is reused by each dfx_rpu_tnl
	 * HCLGE_DFX_BD_OFFSET is starting at 1, but the array subscript is
	 * starting at 0, so offset need '- 1'.
	 */
	*len += (bd_num_list[HCLGE_DFX_RPU_0_BD_OFFSET - 1] * data_len_per_desc +
		 HCLGE_REG_TLV_SIZE) * ae_dev->dev_specs.tnl_num;

out:
	kfree(bd_num_list);
	return ret;
}

static int hclge_get_dfx_rpu_tnl_reg(struct hclge_dev *hdev, u32 *reg,
				     struct hclge_desc *desc_src,
				     int bd_num)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	int ret = 0;
	u8 i;

	for (i = HCLGE_REG_RPU_TNL_ID_0; i <= ae_dev->dev_specs.tnl_num; i++) {
		ret = hclge_dfx_reg_rpu_tnl_cmd_send(hdev, i, desc_src, bd_num);
		if (ret)
			break;

		reg += hclge_reg_get_tlv(HCLGE_REG_TAG_RPU_TNL,
					 ARRAY_SIZE(desc_src->data) * bd_num,
					 reg);
		reg += hclge_dfx_reg_fetch_data(desc_src, bd_num, reg);
	}

	return ret;
}

static int hclge_get_dfx_reg(struct hclge_dev *hdev, void *data)
{
	u32 dfx_reg_type_num = ARRAY_SIZE(hclge_dfx_bd_offset_list);
	int bd_num, bd_num_max, buf_len;
	struct hclge_desc *desc_src;
	int *bd_num_list;
	u32 *reg = data;
	int ret;
	u32 i;

	bd_num_list = kcalloc(dfx_reg_type_num, sizeof(int), GFP_KERNEL);
	if (!bd_num_list)
		return -ENOMEM;

	ret = hclge_get_dfx_reg_bd_num(hdev, bd_num_list, dfx_reg_type_num);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get dfx reg bd num fail, status is %d.\n", ret);
		goto out;
	}

	bd_num_max = bd_num_list[0];
	for (i = 1; i < dfx_reg_type_num; i++)
		bd_num_max = max_t(int, bd_num_max, bd_num_list[i]);

	buf_len = sizeof(*desc_src) * bd_num_max;
	desc_src = kzalloc(buf_len, GFP_KERNEL);
	if (!desc_src) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < dfx_reg_type_num; i++) {
		bd_num = bd_num_list[i];
		ret = hclge_dfx_reg_cmd_send(hdev, desc_src, bd_num,
					     hclge_dfx_reg_opcode_list[i]);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"Get dfx reg fail, status is %d.\n", ret);
			goto free;
		}

		reg += hclge_reg_get_tlv(HCLGE_REG_TAG_DFX_BIOS_COMMON + i,
					 ARRAY_SIZE(desc_src->data) * bd_num,
					 reg);
		reg += hclge_dfx_reg_fetch_data(desc_src, bd_num, reg);
	}

	/**
	 * HCLGE_DFX_BD_OFFSET is starting at 1, but the array subscript is
	 * starting at 0, so offset need '- 1'.
	 */
	bd_num = bd_num_list[HCLGE_DFX_RPU_0_BD_OFFSET - 1];
	ret = hclge_get_dfx_rpu_tnl_reg(hdev, reg, desc_src, bd_num);

free:
	kfree(desc_src);
out:
	kfree(bd_num_list);
	return ret;
}

static int hclge_fetch_pf_reg(struct hclge_dev *hdev, void *data,
			      struct hnae3_knic_private_info *kinfo)
{
#define HCLGE_RING_REG_OFFSET		0x200
#define HCLGE_RING_INT_REG_OFFSET	0x4

	int i, j, reg_num;
	int data_num_sum;
	u32 *reg = data;

	/* fetching per-PF registers valus from PF PCIe register space */
	reg_num = ARRAY_SIZE(cmdq_reg_addr_list);
	reg += hclge_reg_get_tlv(HCLGE_REG_TAG_CMDQ, reg_num, reg);
	for (i = 0; i < reg_num; i++)
		*reg++ = hclge_read_dev(&hdev->hw, cmdq_reg_addr_list[i]);
	data_num_sum = reg_num + HCLGE_REG_TLV_SPACE;

	reg_num = ARRAY_SIZE(common_reg_addr_list);
	reg += hclge_reg_get_tlv(HCLGE_REG_TAG_COMMON, reg_num, reg);
	for (i = 0; i < reg_num; i++)
		*reg++ = hclge_read_dev(&hdev->hw, common_reg_addr_list[i]);
	data_num_sum += reg_num + HCLGE_REG_TLV_SPACE;

	reg_num = ARRAY_SIZE(ring_reg_addr_list);
	for (j = 0; j < kinfo->num_tqps; j++) {
		reg += hclge_reg_get_tlv(HCLGE_REG_TAG_RING, reg_num, reg);
		for (i = 0; i < reg_num; i++)
			*reg++ = hclge_read_dev(&hdev->hw,
						ring_reg_addr_list[i] +
						HCLGE_RING_REG_OFFSET * j);
	}
	data_num_sum += (reg_num + HCLGE_REG_TLV_SPACE) * kinfo->num_tqps;

	reg_num = ARRAY_SIZE(tqp_intr_reg_addr_list);
	for (j = 0; j < hdev->num_msi_used - 1; j++) {
		reg += hclge_reg_get_tlv(HCLGE_REG_TAG_TQP_INTR, reg_num, reg);
		for (i = 0; i < reg_num; i++)
			*reg++ = hclge_read_dev(&hdev->hw,
						tqp_intr_reg_addr_list[i] +
						HCLGE_RING_INT_REG_OFFSET * j);
	}
	data_num_sum += (reg_num + HCLGE_REG_TLV_SPACE) *
			(hdev->num_msi_used - 1);

	return data_num_sum;
}

static int hclge_get_regs_num(struct hclge_dev *hdev, u32 *regs_num_32_bit,
			      u32 *regs_num_64_bit)
{
	struct hclge_desc desc;
	u32 total_num;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QUERY_REG_NUM, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Query register number cmd failed, ret = %d.\n", ret);
		return ret;
	}

	*regs_num_32_bit = le32_to_cpu(desc.data[0]);
	*regs_num_64_bit = le32_to_cpu(desc.data[1]);

	total_num = *regs_num_32_bit + *regs_num_64_bit;
	if (!total_num)
		return -EINVAL;

	return 0;
}

int hclge_get_regs_len(struct hnae3_handle *handle)
{
	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hclge_vport *vport = hclge_get_vport(handle);
	int regs_num_32_bit, regs_num_64_bit, dfx_regs_len;
	int cmdq_len, common_len, ring_len, tqp_intr_len;
	int regs_len_32_bit, regs_len_64_bit;
	struct hclge_dev *hdev = vport->back;
	int ret;

	ret = hclge_get_regs_num(hdev, &regs_num_32_bit, &regs_num_64_bit);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get register number failed, ret = %d.\n", ret);
		return ret;
	}

	ret = hclge_get_dfx_reg_len(hdev, &dfx_regs_len);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get dfx reg len failed, ret = %d.\n", ret);
		return ret;
	}

	cmdq_len = HCLGE_REG_TLV_SIZE + sizeof(cmdq_reg_addr_list);
	common_len = HCLGE_REG_TLV_SIZE + sizeof(common_reg_addr_list);
	ring_len = HCLGE_REG_TLV_SIZE + sizeof(ring_reg_addr_list);
	tqp_intr_len = HCLGE_REG_TLV_SIZE + sizeof(tqp_intr_reg_addr_list);
	regs_len_32_bit = HCLGE_REG_TLV_SIZE + regs_num_32_bit * sizeof(u32);
	regs_len_64_bit = HCLGE_REG_TLV_SIZE + regs_num_64_bit * sizeof(u64);

	/* return the total length of all register values */
	return HCLGE_REG_HEADER_SIZE + cmdq_len + common_len + ring_len *
		kinfo->num_tqps + tqp_intr_len * (hdev->num_msi_used - 1) +
		regs_len_32_bit + regs_len_64_bit + dfx_regs_len;
}

void hclge_get_regs(struct hnae3_handle *handle, u32 *version,
		    void *data)
{
#define HCLGE_REG_64_BIT_SPACE_MULTIPLE		2

	struct hnae3_knic_private_info *kinfo = &handle->kinfo;
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	u32 regs_num_32_bit, regs_num_64_bit;
	u32 *reg = data;
	int ret;

	*version = hdev->fw_version;

	ret = hclge_get_regs_num(hdev, &regs_num_32_bit, &regs_num_64_bit);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get register number failed, ret = %d.\n", ret);
		return;
	}

	reg += hclge_reg_get_header(reg);
	reg += hclge_fetch_pf_reg(hdev, reg, kinfo);

	reg += hclge_reg_get_tlv(HCLGE_REG_TAG_QUERY_32_BIT,
				 regs_num_32_bit, reg);
	ret = hclge_get_32_bit_regs(hdev, regs_num_32_bit, reg);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get 32 bit register failed, ret = %d.\n", ret);
		return;
	}
	reg += regs_num_32_bit;

	reg += hclge_reg_get_tlv(HCLGE_REG_TAG_QUERY_64_BIT,
				 regs_num_64_bit *
				 HCLGE_REG_64_BIT_SPACE_MULTIPLE, reg);
	ret = hclge_get_64_bit_regs(hdev, regs_num_64_bit, reg);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"Get 64 bit register failed, ret = %d.\n", ret);
		return;
	}
	reg += regs_num_64_bit * HCLGE_REG_64_BIT_SPACE_MULTIPLE;

	ret = hclge_get_dfx_reg(hdev, reg);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"Get dfx register failed, ret = %d.\n", ret);
}
