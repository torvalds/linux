// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#include <linux/device.h>

#include "hclge_debugfs.h"
#include "hclge_err.h"
#include "hclge_main.h"
#include "hclge_tm.h"
#include "hnae3.h"

static const char * const state_str[] = { "off", "on" };
static const char * const hclge_mac_state_str[] = {
	"TO_ADD", "TO_DEL", "ACTIVE"
};

static const struct hclge_dbg_reg_type_info hclge_dbg_reg_info[] = {
	{ .cmd = HNAE3_DBG_CMD_REG_BIOS_COMMON,
	  .dfx_msg = &hclge_dbg_bios_common_reg[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_bios_common_reg),
		       .offset = HCLGE_DBG_DFX_BIOS_OFFSET,
		       .cmd = HCLGE_OPC_DFX_BIOS_COMMON_REG } },
	{ .cmd = HNAE3_DBG_CMD_REG_SSU,
	  .dfx_msg = &hclge_dbg_ssu_reg_0[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_ssu_reg_0),
		       .offset = HCLGE_DBG_DFX_SSU_0_OFFSET,
		       .cmd = HCLGE_OPC_DFX_SSU_REG_0 } },
	{ .cmd = HNAE3_DBG_CMD_REG_SSU,
	  .dfx_msg = &hclge_dbg_ssu_reg_1[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_ssu_reg_1),
		       .offset = HCLGE_DBG_DFX_SSU_1_OFFSET,
		       .cmd = HCLGE_OPC_DFX_SSU_REG_1 } },
	{ .cmd = HNAE3_DBG_CMD_REG_SSU,
	  .dfx_msg = &hclge_dbg_ssu_reg_2[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_ssu_reg_2),
		       .offset = HCLGE_DBG_DFX_SSU_2_OFFSET,
		       .cmd = HCLGE_OPC_DFX_SSU_REG_2 } },
	{ .cmd = HNAE3_DBG_CMD_REG_IGU_EGU,
	  .dfx_msg = &hclge_dbg_igu_egu_reg[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_igu_egu_reg),
		       .offset = HCLGE_DBG_DFX_IGU_OFFSET,
		       .cmd = HCLGE_OPC_DFX_IGU_EGU_REG } },
	{ .cmd = HNAE3_DBG_CMD_REG_RPU,
	  .dfx_msg = &hclge_dbg_rpu_reg_0[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_rpu_reg_0),
		       .offset = HCLGE_DBG_DFX_RPU_0_OFFSET,
		       .cmd = HCLGE_OPC_DFX_RPU_REG_0 } },
	{ .cmd = HNAE3_DBG_CMD_REG_RPU,
	  .dfx_msg = &hclge_dbg_rpu_reg_1[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_rpu_reg_1),
		       .offset = HCLGE_DBG_DFX_RPU_1_OFFSET,
		       .cmd = HCLGE_OPC_DFX_RPU_REG_1 } },
	{ .cmd = HNAE3_DBG_CMD_REG_NCSI,
	  .dfx_msg = &hclge_dbg_ncsi_reg[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_ncsi_reg),
		       .offset = HCLGE_DBG_DFX_NCSI_OFFSET,
		       .cmd = HCLGE_OPC_DFX_NCSI_REG } },
	{ .cmd = HNAE3_DBG_CMD_REG_RTC,
	  .dfx_msg = &hclge_dbg_rtc_reg[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_rtc_reg),
		       .offset = HCLGE_DBG_DFX_RTC_OFFSET,
		       .cmd = HCLGE_OPC_DFX_RTC_REG } },
	{ .cmd = HNAE3_DBG_CMD_REG_PPP,
	  .dfx_msg = &hclge_dbg_ppp_reg[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_ppp_reg),
		       .offset = HCLGE_DBG_DFX_PPP_OFFSET,
		       .cmd = HCLGE_OPC_DFX_PPP_REG } },
	{ .cmd = HNAE3_DBG_CMD_REG_RCB,
	  .dfx_msg = &hclge_dbg_rcb_reg[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_rcb_reg),
		       .offset = HCLGE_DBG_DFX_RCB_OFFSET,
		       .cmd = HCLGE_OPC_DFX_RCB_REG } },
	{ .cmd = HNAE3_DBG_CMD_REG_TQP,
	  .dfx_msg = &hclge_dbg_tqp_reg[0],
	  .reg_msg = { .msg_num = ARRAY_SIZE(hclge_dbg_tqp_reg),
		       .offset = HCLGE_DBG_DFX_TQP_OFFSET,
		       .cmd = HCLGE_OPC_DFX_TQP_REG } },
};

static void hclge_dbg_fill_content(char *content, u16 len,
				   const struct hclge_dbg_item *items,
				   const char **result, u16 size)
{
	char *pos = content;
	u16 i;

	memset(content, ' ', len);
	for (i = 0; i < size; i++) {
		if (result)
			strncpy(pos, result[i], strlen(result[i]));
		else
			strncpy(pos, items[i].name, strlen(items[i].name));
		pos += strlen(items[i].name) + items[i].interval;
	}
	*pos++ = '\n';
	*pos++ = '\0';
}

static char *hclge_dbg_get_func_id_str(char *buf, u8 id)
{
	if (id)
		sprintf(buf, "vf%u", id - 1);
	else
		sprintf(buf, "pf");

	return buf;
}

static int hclge_dbg_get_dfx_bd_num(struct hclge_dev *hdev, int offset,
				    u32 *bd_num)
{
	struct hclge_desc desc[HCLGE_GET_DFX_REG_TYPE_CNT];
	int entries_per_desc;
	int index;
	int ret;

	ret = hclge_query_bd_num_cmd_send(hdev, desc);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get dfx bd_num, offset = %d, ret = %d\n",
			offset, ret);
		return ret;
	}

	entries_per_desc = ARRAY_SIZE(desc[0].data);
	index = offset % entries_per_desc;

	*bd_num = le32_to_cpu(desc[offset / entries_per_desc].data[index]);
	if (!(*bd_num)) {
		dev_err(&hdev->pdev->dev, "The value of dfx bd_num is 0!\n");
		return -EINVAL;
	}

	return 0;
}

static int hclge_dbg_cmd_send(struct hclge_dev *hdev,
			      struct hclge_desc *desc_src,
			      int index, int bd_num,
			      enum hclge_opcode_type cmd)
{
	struct hclge_desc *desc = desc_src;
	int ret, i;

	hclge_cmd_setup_basic_desc(desc, cmd, true);
	desc->data[0] = cpu_to_le32(index);

	for (i = 1; i < bd_num; i++) {
		desc->flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
		desc++;
		hclge_cmd_setup_basic_desc(desc, cmd, true);
	}

	ret = hclge_cmd_send(&hdev->hw, desc_src, bd_num);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"cmd(0x%x) send fail, ret = %d\n", cmd, ret);
	return ret;
}

static int
hclge_dbg_dump_reg_tqp(struct hclge_dev *hdev,
		       const struct hclge_dbg_reg_type_info *reg_info,
		       char *buf, int len, int *pos)
{
	const struct hclge_dbg_dfx_message *dfx_message = reg_info->dfx_msg;
	const struct hclge_dbg_reg_common_msg *reg_msg = &reg_info->reg_msg;
	struct hclge_desc *desc_src;
	u32 index, entry, i, cnt;
	int bd_num, min_num, ret;
	struct hclge_desc *desc;

	ret = hclge_dbg_get_dfx_bd_num(hdev, reg_msg->offset, &bd_num);
	if (ret)
		return ret;

	desc_src = kcalloc(bd_num, sizeof(struct hclge_desc), GFP_KERNEL);
	if (!desc_src)
		return -ENOMEM;

	min_num = min_t(int, bd_num * HCLGE_DESC_DATA_LEN, reg_msg->msg_num);

	for (i = 0, cnt = 0; i < min_num; i++, dfx_message++)
		*pos += scnprintf(buf + *pos, len - *pos, "item%u = %s\n",
				  cnt++, dfx_message->message);

	for (i = 0; i < cnt; i++)
		*pos += scnprintf(buf + *pos, len - *pos, "item%u\t", i);

	*pos += scnprintf(buf + *pos, len - *pos, "\n");

	for (index = 0; index < hdev->vport[0].alloc_tqps; index++) {
		dfx_message = reg_info->dfx_msg;
		desc = desc_src;
		ret = hclge_dbg_cmd_send(hdev, desc, index, bd_num,
					 reg_msg->cmd);
		if (ret)
			break;

		for (i = 0; i < min_num; i++, dfx_message++) {
			entry = i % HCLGE_DESC_DATA_LEN;
			if (i > 0 && !entry)
				desc++;

			*pos += scnprintf(buf + *pos, len - *pos, "%#x\t",
					  le32_to_cpu(desc->data[entry]));
		}
		*pos += scnprintf(buf + *pos, len - *pos, "\n");
	}

	kfree(desc_src);
	return ret;
}

static int
hclge_dbg_dump_reg_common(struct hclge_dev *hdev,
			  const struct hclge_dbg_reg_type_info *reg_info,
			  char *buf, int len, int *pos)
{
	const struct hclge_dbg_reg_common_msg *reg_msg = &reg_info->reg_msg;
	const struct hclge_dbg_dfx_message *dfx_message = reg_info->dfx_msg;
	struct hclge_desc *desc_src;
	int bd_num, min_num, ret;
	struct hclge_desc *desc;
	u32 entry, i;

	ret = hclge_dbg_get_dfx_bd_num(hdev, reg_msg->offset, &bd_num);
	if (ret)
		return ret;

	desc_src = kcalloc(bd_num, sizeof(struct hclge_desc), GFP_KERNEL);
	if (!desc_src)
		return -ENOMEM;

	desc = desc_src;

	ret = hclge_dbg_cmd_send(hdev, desc, 0, bd_num, reg_msg->cmd);
	if (ret) {
		kfree(desc);
		return ret;
	}

	min_num = min_t(int, bd_num * HCLGE_DESC_DATA_LEN, reg_msg->msg_num);

	for (i = 0; i < min_num; i++, dfx_message++) {
		entry = i % HCLGE_DESC_DATA_LEN;
		if (i > 0 && !entry)
			desc++;
		if (!dfx_message->flag)
			continue;

		*pos += scnprintf(buf + *pos, len - *pos, "%s: %#x\n",
				  dfx_message->message,
				  le32_to_cpu(desc->data[entry]));
	}

	kfree(desc_src);
	return 0;
}

static int  hclge_dbg_dump_mac_enable_status(struct hclge_dev *hdev, char *buf,
					     int len, int *pos)
{
	struct hclge_config_mac_mode_cmd *req;
	struct hclge_desc desc;
	u32 loop_en;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CONFIG_MAC_MODE, true);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to dump mac enable status, ret = %d\n", ret);
		return ret;
	}

	req = (struct hclge_config_mac_mode_cmd *)desc.data;
	loop_en = le32_to_cpu(req->txrx_pad_fcs_loop_en);

	*pos += scnprintf(buf + *pos, len - *pos, "mac_trans_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_TX_EN_B));
	*pos += scnprintf(buf + *pos, len - *pos, "mac_rcv_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_RX_EN_B));
	*pos += scnprintf(buf + *pos, len - *pos, "pad_trans_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_PAD_TX_B));
	*pos += scnprintf(buf + *pos, len - *pos, "pad_rcv_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_PAD_RX_B));
	*pos += scnprintf(buf + *pos, len - *pos, "1588_trans_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_1588_TX_B));
	*pos += scnprintf(buf + *pos, len - *pos, "1588_rcv_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_1588_RX_B));
	*pos += scnprintf(buf + *pos, len - *pos, "mac_app_loop_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_APP_LP_B));
	*pos += scnprintf(buf + *pos, len - *pos, "mac_line_loop_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_LINE_LP_B));
	*pos += scnprintf(buf + *pos, len - *pos, "mac_fcs_tx_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_FCS_TX_B));
	*pos += scnprintf(buf + *pos, len - *pos,
			  "mac_rx_oversize_truncate_en: %#x\n",
			  hnae3_get_bit(loop_en,
					HCLGE_MAC_RX_OVERSIZE_TRUNCATE_B));
	*pos += scnprintf(buf + *pos, len - *pos, "mac_rx_fcs_strip_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_RX_FCS_STRIP_B));
	*pos += scnprintf(buf + *pos, len - *pos, "mac_rx_fcs_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_RX_FCS_B));
	*pos += scnprintf(buf + *pos, len - *pos,
			  "mac_tx_under_min_err_en: %#x\n",
			  hnae3_get_bit(loop_en, HCLGE_MAC_TX_UNDER_MIN_ERR_B));
	*pos += scnprintf(buf + *pos, len - *pos,
			  "mac_tx_oversize_truncate_en: %#x\n",
			  hnae3_get_bit(loop_en,
					HCLGE_MAC_TX_OVERSIZE_TRUNCATE_B));

	return 0;
}

static int hclge_dbg_dump_mac_frame_size(struct hclge_dev *hdev, char *buf,
					 int len, int *pos)
{
	struct hclge_config_max_frm_size_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CONFIG_MAX_FRM_SIZE, true);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to dump mac frame size, ret = %d\n", ret);
		return ret;
	}

	req = (struct hclge_config_max_frm_size_cmd *)desc.data;

	*pos += scnprintf(buf + *pos, len - *pos, "max_frame_size: %u\n",
			  le16_to_cpu(req->max_frm_size));
	*pos += scnprintf(buf + *pos, len - *pos, "min_frame_size: %u\n",
			  req->min_frm_size);

	return 0;
}

static int hclge_dbg_dump_mac_speed_duplex(struct hclge_dev *hdev, char *buf,
					   int len, int *pos)
{
#define HCLGE_MAC_SPEED_SHIFT	0
#define HCLGE_MAC_SPEED_MASK	GENMASK(5, 0)
#define HCLGE_MAC_DUPLEX_SHIFT	7

	struct hclge_config_mac_speed_dup_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CONFIG_SPEED_DUP, true);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to dump mac speed duplex, ret = %d\n", ret);
		return ret;
	}

	req = (struct hclge_config_mac_speed_dup_cmd *)desc.data;

	*pos += scnprintf(buf + *pos, len - *pos, "speed: %#lx\n",
			  hnae3_get_field(req->speed_dup, HCLGE_MAC_SPEED_MASK,
					  HCLGE_MAC_SPEED_SHIFT));
	*pos += scnprintf(buf + *pos, len - *pos, "duplex: %#x\n",
			  hnae3_get_bit(req->speed_dup,
					HCLGE_MAC_DUPLEX_SHIFT));
	return 0;
}

static int hclge_dbg_dump_mac(struct hclge_dev *hdev, char *buf, int len)
{
	int pos = 0;
	int ret;

	ret = hclge_dbg_dump_mac_enable_status(hdev, buf, len, &pos);
	if (ret)
		return ret;

	ret = hclge_dbg_dump_mac_frame_size(hdev, buf, len, &pos);
	if (ret)
		return ret;

	return hclge_dbg_dump_mac_speed_duplex(hdev, buf, len, &pos);
}

static void hclge_dbg_dump_dcb(struct hclge_dev *hdev, const char *cmd_buf)
{
	struct device *dev = &hdev->pdev->dev;
	struct hclge_dbg_bitmap_cmd *bitmap;
	enum hclge_opcode_type cmd;
	int rq_id, pri_id, qset_id;
	int port_id, nq_id, pg_id;
	struct hclge_desc desc[2];

	int cnt, ret;

	cnt = sscanf(cmd_buf, "%i %i %i %i %i %i",
		     &port_id, &pri_id, &pg_id, &rq_id, &nq_id, &qset_id);
	if (cnt != 6) {
		dev_err(&hdev->pdev->dev,
			"dump dcb: bad command parameter, cnt=%d\n", cnt);
		return;
	}

	cmd = HCLGE_OPC_QSET_DFX_STS;
	ret = hclge_dbg_cmd_send(hdev, desc, qset_id, 1, cmd);
	if (ret)
		goto err_dcb_cmd_send;

	bitmap = (struct hclge_dbg_bitmap_cmd *)&desc[0].data[1];
	dev_info(dev, "roce_qset_mask: 0x%x\n", bitmap->bit0);
	dev_info(dev, "nic_qs_mask: 0x%x\n", bitmap->bit1);
	dev_info(dev, "qs_shaping_pass: 0x%x\n", bitmap->bit2);
	dev_info(dev, "qs_bp_sts: 0x%x\n", bitmap->bit3);

	cmd = HCLGE_OPC_PRI_DFX_STS;
	ret = hclge_dbg_cmd_send(hdev, desc, pri_id, 1, cmd);
	if (ret)
		goto err_dcb_cmd_send;

	bitmap = (struct hclge_dbg_bitmap_cmd *)&desc[0].data[1];
	dev_info(dev, "pri_mask: 0x%x\n", bitmap->bit0);
	dev_info(dev, "pri_cshaping_pass: 0x%x\n", bitmap->bit1);
	dev_info(dev, "pri_pshaping_pass: 0x%x\n", bitmap->bit2);

	cmd = HCLGE_OPC_PG_DFX_STS;
	ret = hclge_dbg_cmd_send(hdev, desc, pg_id, 1, cmd);
	if (ret)
		goto err_dcb_cmd_send;

	bitmap = (struct hclge_dbg_bitmap_cmd *)&desc[0].data[1];
	dev_info(dev, "pg_mask: 0x%x\n", bitmap->bit0);
	dev_info(dev, "pg_cshaping_pass: 0x%x\n", bitmap->bit1);
	dev_info(dev, "pg_pshaping_pass: 0x%x\n", bitmap->bit2);

	cmd = HCLGE_OPC_PORT_DFX_STS;
	ret = hclge_dbg_cmd_send(hdev, desc, port_id, 1, cmd);
	if (ret)
		goto err_dcb_cmd_send;

	bitmap = (struct hclge_dbg_bitmap_cmd *)&desc[0].data[1];
	dev_info(dev, "port_mask: 0x%x\n", bitmap->bit0);
	dev_info(dev, "port_shaping_pass: 0x%x\n", bitmap->bit1);

	cmd = HCLGE_OPC_SCH_NQ_CNT;
	ret = hclge_dbg_cmd_send(hdev, desc, nq_id, 1, cmd);
	if (ret)
		goto err_dcb_cmd_send;

	dev_info(dev, "sch_nq_cnt: 0x%x\n", le32_to_cpu(desc[0].data[1]));

	cmd = HCLGE_OPC_SCH_RQ_CNT;
	ret = hclge_dbg_cmd_send(hdev, desc, nq_id, 1, cmd);
	if (ret)
		goto err_dcb_cmd_send;

	dev_info(dev, "sch_rq_cnt: 0x%x\n", le32_to_cpu(desc[0].data[1]));

	cmd = HCLGE_OPC_TM_INTERNAL_STS;
	ret = hclge_dbg_cmd_send(hdev, desc, 0, 2, cmd);
	if (ret)
		goto err_dcb_cmd_send;

	dev_info(dev, "pri_bp: 0x%x\n", le32_to_cpu(desc[0].data[1]));
	dev_info(dev, "fifo_dfx_info: 0x%x\n", le32_to_cpu(desc[0].data[2]));
	dev_info(dev, "sch_roce_fifo_afull_gap: 0x%x\n",
		 le32_to_cpu(desc[0].data[3]));
	dev_info(dev, "tx_private_waterline: 0x%x\n",
		 le32_to_cpu(desc[0].data[4]));
	dev_info(dev, "tm_bypass_en: 0x%x\n", le32_to_cpu(desc[0].data[5]));
	dev_info(dev, "SSU_TM_BYPASS_EN: 0x%x\n", le32_to_cpu(desc[1].data[0]));
	dev_info(dev, "SSU_RESERVE_CFG: 0x%x\n", le32_to_cpu(desc[1].data[1]));

	cmd = HCLGE_OPC_TM_INTERNAL_CNT;
	ret = hclge_dbg_cmd_send(hdev, desc, port_id, 1, cmd);
	if (ret)
		goto err_dcb_cmd_send;

	dev_info(dev, "SCH_NIC_NUM: 0x%x\n", le32_to_cpu(desc[0].data[1]));
	dev_info(dev, "SCH_ROCE_NUM: 0x%x\n", le32_to_cpu(desc[0].data[2]));

	cmd = HCLGE_OPC_TM_INTERNAL_STS_1;
	ret = hclge_dbg_cmd_send(hdev, desc, port_id, 1, cmd);
	if (ret)
		goto err_dcb_cmd_send;

	dev_info(dev, "TC_MAP_SEL: 0x%x\n", le32_to_cpu(desc[0].data[1]));
	dev_info(dev, "IGU_PFC_PRI_EN: 0x%x\n", le32_to_cpu(desc[0].data[2]));
	dev_info(dev, "MAC_PFC_PRI_EN: 0x%x\n", le32_to_cpu(desc[0].data[3]));
	dev_info(dev, "IGU_PRI_MAP_TC_CFG: 0x%x\n",
		 le32_to_cpu(desc[0].data[4]));
	dev_info(dev, "IGU_TX_PRI_MAP_TC_CFG: 0x%x\n",
		 le32_to_cpu(desc[0].data[5]));
	return;

err_dcb_cmd_send:
	dev_err(&hdev->pdev->dev,
		"failed to dump dcb dfx, cmd = %#x, ret = %d\n",
		cmd, ret);
}

static int hclge_dbg_dump_reg_cmd(struct hclge_dev *hdev,
				  enum hnae3_dbg_cmd cmd, char *buf, int len)
{
	const struct hclge_dbg_reg_type_info *reg_info;
	int pos = 0, ret = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(hclge_dbg_reg_info); i++) {
		reg_info = &hclge_dbg_reg_info[i];
		if (cmd == reg_info->cmd) {
			if (cmd == HNAE3_DBG_CMD_REG_TQP)
				return hclge_dbg_dump_reg_tqp(hdev, reg_info,
							      buf, len, &pos);

			ret = hclge_dbg_dump_reg_common(hdev, reg_info, buf,
							len, &pos);
			if (ret)
				break;
		}
	}

	return ret;
}

static void hclge_print_tc_info(struct hclge_dev *hdev, bool flag, int index)
{
	if (flag)
		dev_info(&hdev->pdev->dev, "tc(%d): no sp mode weight: %u\n",
			 index, hdev->tm_info.pg_info[0].tc_dwrr[index]);
	else
		dev_info(&hdev->pdev->dev, "tc(%d): sp mode\n", index);
}

static void hclge_dbg_dump_tc(struct hclge_dev *hdev)
{
	struct hclge_ets_tc_weight_cmd *ets_weight;
	struct hclge_desc desc;
	int i, ret;

	if (!hnae3_dev_dcb_supported(hdev)) {
		dev_info(&hdev->pdev->dev,
			 "Only DCB-supported dev supports tc\n");
		return;
	}

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_ETS_TC_WEIGHT, true);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev, "dump tc fail, ret = %d\n", ret);
		return;
	}

	ets_weight = (struct hclge_ets_tc_weight_cmd *)desc.data;

	dev_info(&hdev->pdev->dev, "dump tc: %u tc enabled\n",
		 hdev->tm_info.num_tc);
	dev_info(&hdev->pdev->dev, "weight_offset: %u\n",
		 ets_weight->weight_offset);

	for (i = 0; i < HNAE3_MAX_TC; i++)
		hclge_print_tc_info(hdev, ets_weight->tc_weight[i], i);
}

static void hclge_dbg_dump_tm_pg(struct hclge_dev *hdev)
{
	struct hclge_port_shapping_cmd *port_shap_cfg_cmd;
	struct hclge_bp_to_qs_map_cmd *bp_to_qs_map_cmd;
	struct hclge_pg_shapping_cmd *pg_shap_cfg_cmd;
	enum hclge_opcode_type cmd;
	struct hclge_desc desc;
	int ret;

	cmd = HCLGE_OPC_TM_PG_C_SHAPPING;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	pg_shap_cfg_cmd = (struct hclge_pg_shapping_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "PG_C pg_id: %u\n", pg_shap_cfg_cmd->pg_id);
	dev_info(&hdev->pdev->dev, "PG_C pg_shapping: 0x%x\n",
		 le32_to_cpu(pg_shap_cfg_cmd->pg_shapping_para));

	cmd = HCLGE_OPC_TM_PG_P_SHAPPING;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	pg_shap_cfg_cmd = (struct hclge_pg_shapping_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "PG_P pg_id: %u\n", pg_shap_cfg_cmd->pg_id);
	dev_info(&hdev->pdev->dev, "PG_P pg_shapping: 0x%x\n",
		 le32_to_cpu(pg_shap_cfg_cmd->pg_shapping_para));
	dev_info(&hdev->pdev->dev, "PG_P flag: %#x\n", pg_shap_cfg_cmd->flag);
	dev_info(&hdev->pdev->dev, "PG_P pg_rate: %u(Mbps)\n",
		 le32_to_cpu(pg_shap_cfg_cmd->pg_rate));

	cmd = HCLGE_OPC_TM_PORT_SHAPPING;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	port_shap_cfg_cmd = (struct hclge_port_shapping_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "PORT port_shapping: 0x%x\n",
		 le32_to_cpu(port_shap_cfg_cmd->port_shapping_para));
	dev_info(&hdev->pdev->dev, "PORT flag: %#x\n", port_shap_cfg_cmd->flag);
	dev_info(&hdev->pdev->dev, "PORT port_rate: %u(Mbps)\n",
		 le32_to_cpu(port_shap_cfg_cmd->port_rate));

	cmd = HCLGE_OPC_TM_PG_SCH_MODE_CFG;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	dev_info(&hdev->pdev->dev, "PG_SCH pg_id: %u\n",
		 le32_to_cpu(desc.data[0]));

	cmd = HCLGE_OPC_TM_PRI_SCH_MODE_CFG;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	dev_info(&hdev->pdev->dev, "PRI_SCH pri_id: %u\n",
		 le32_to_cpu(desc.data[0]));

	cmd = HCLGE_OPC_TM_QS_SCH_MODE_CFG;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	dev_info(&hdev->pdev->dev, "QS_SCH qs_id: %u\n",
		 le32_to_cpu(desc.data[0]));

	if (!hnae3_dev_dcb_supported(hdev)) {
		dev_info(&hdev->pdev->dev,
			 "Only DCB-supported dev supports tm mapping\n");
		return;
	}

	cmd = HCLGE_OPC_TM_BP_TO_QSET_MAPPING;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	bp_to_qs_map_cmd = (struct hclge_bp_to_qs_map_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "BP_TO_QSET tc_id: %u\n",
		 bp_to_qs_map_cmd->tc_id);
	dev_info(&hdev->pdev->dev, "BP_TO_QSET qs_group_id: 0x%x\n",
		 bp_to_qs_map_cmd->qs_group_id);
	dev_info(&hdev->pdev->dev, "BP_TO_QSET qs_bit_map: 0x%x\n",
		 le32_to_cpu(bp_to_qs_map_cmd->qs_bit_map));
	return;

err_tm_pg_cmd_send:
	dev_err(&hdev->pdev->dev, "dump tm_pg fail(0x%x), ret = %d\n",
		cmd, ret);
}

static void hclge_dbg_dump_tm(struct hclge_dev *hdev)
{
	struct hclge_priority_weight_cmd *priority_weight;
	struct hclge_pg_to_pri_link_cmd *pg_to_pri_map;
	struct hclge_qs_to_pri_link_cmd *qs_to_pri_map;
	struct hclge_nq_to_qs_link_cmd *nq_to_qs_map;
	struct hclge_pri_shapping_cmd *shap_cfg_cmd;
	struct hclge_pg_weight_cmd *pg_weight;
	struct hclge_qs_weight_cmd *qs_weight;
	enum hclge_opcode_type cmd;
	struct hclge_desc desc;
	int ret;

	cmd = HCLGE_OPC_TM_PG_TO_PRI_LINK;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_cmd_send;

	pg_to_pri_map = (struct hclge_pg_to_pri_link_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "dump tm\n");
	dev_info(&hdev->pdev->dev, "PG_TO_PRI gp_id: %u\n",
		 pg_to_pri_map->pg_id);
	dev_info(&hdev->pdev->dev, "PG_TO_PRI map: 0x%x\n",
		 pg_to_pri_map->pri_bit_map);

	cmd = HCLGE_OPC_TM_QS_TO_PRI_LINK;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_cmd_send;

	qs_to_pri_map = (struct hclge_qs_to_pri_link_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "QS_TO_PRI qs_id: %u\n",
		 le16_to_cpu(qs_to_pri_map->qs_id));
	dev_info(&hdev->pdev->dev, "QS_TO_PRI priority: %u\n",
		 qs_to_pri_map->priority);
	dev_info(&hdev->pdev->dev, "QS_TO_PRI link_vld: %u\n",
		 qs_to_pri_map->link_vld);

	cmd = HCLGE_OPC_TM_NQ_TO_QS_LINK;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_cmd_send;

	nq_to_qs_map = (struct hclge_nq_to_qs_link_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "NQ_TO_QS nq_id: %u\n",
		 le16_to_cpu(nq_to_qs_map->nq_id));
	dev_info(&hdev->pdev->dev, "NQ_TO_QS qset_id: 0x%x\n",
		 le16_to_cpu(nq_to_qs_map->qset_id));

	cmd = HCLGE_OPC_TM_PG_WEIGHT;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_cmd_send;

	pg_weight = (struct hclge_pg_weight_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "PG pg_id: %u\n", pg_weight->pg_id);
	dev_info(&hdev->pdev->dev, "PG dwrr: %u\n", pg_weight->dwrr);

	cmd = HCLGE_OPC_TM_QS_WEIGHT;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_cmd_send;

	qs_weight = (struct hclge_qs_weight_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "QS qs_id: %u\n",
		 le16_to_cpu(qs_weight->qs_id));
	dev_info(&hdev->pdev->dev, "QS dwrr: %u\n", qs_weight->dwrr);

	cmd = HCLGE_OPC_TM_PRI_WEIGHT;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_cmd_send;

	priority_weight = (struct hclge_priority_weight_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "PRI pri_id: %u\n", priority_weight->pri_id);
	dev_info(&hdev->pdev->dev, "PRI dwrr: %u\n", priority_weight->dwrr);

	cmd = HCLGE_OPC_TM_PRI_C_SHAPPING;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_cmd_send;

	shap_cfg_cmd = (struct hclge_pri_shapping_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "PRI_C pri_id: %u\n", shap_cfg_cmd->pri_id);
	dev_info(&hdev->pdev->dev, "PRI_C pri_shapping: 0x%x\n",
		 le32_to_cpu(shap_cfg_cmd->pri_shapping_para));
	dev_info(&hdev->pdev->dev, "PRI_C flag: %#x\n", shap_cfg_cmd->flag);
	dev_info(&hdev->pdev->dev, "PRI_C pri_rate: %u(Mbps)\n",
		 le32_to_cpu(shap_cfg_cmd->pri_rate));

	cmd = HCLGE_OPC_TM_PRI_P_SHAPPING;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_cmd_send;

	shap_cfg_cmd = (struct hclge_pri_shapping_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "PRI_P pri_id: %u\n", shap_cfg_cmd->pri_id);
	dev_info(&hdev->pdev->dev, "PRI_P pri_shapping: 0x%x\n",
		 le32_to_cpu(shap_cfg_cmd->pri_shapping_para));
	dev_info(&hdev->pdev->dev, "PRI_P flag: %#x\n", shap_cfg_cmd->flag);
	dev_info(&hdev->pdev->dev, "PRI_P pri_rate: %u(Mbps)\n",
		 le32_to_cpu(shap_cfg_cmd->pri_rate));

	hclge_dbg_dump_tm_pg(hdev);

	return;

err_tm_cmd_send:
	dev_err(&hdev->pdev->dev, "dump tm fail(0x%x), ret = %d\n",
		cmd, ret);
}

static void hclge_dbg_dump_tm_map(struct hclge_dev *hdev,
				  const char *cmd_buf)
{
	struct hclge_bp_to_qs_map_cmd *bp_to_qs_map_cmd;
	struct hclge_nq_to_qs_link_cmd *nq_to_qs_map;
	u32 qset_mapping[HCLGE_BP_EXT_GRP_NUM];
	struct hclge_qs_to_pri_link_cmd *map;
	struct hclge_tqp_tx_queue_tc_cmd *tc;
	u16 group_id, queue_id, qset_id;
	enum hclge_opcode_type cmd;
	u8 grp_num, pri_id, tc_id;
	struct hclge_desc desc;
	u16 qs_id_l;
	u16 qs_id_h;
	int ret;
	u32 i;

	ret = kstrtou16(cmd_buf, 0, &queue_id);
	queue_id = (ret != 0) ? 0 : queue_id;

	cmd = HCLGE_OPC_TM_NQ_TO_QS_LINK;
	nq_to_qs_map = (struct hclge_nq_to_qs_link_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	nq_to_qs_map->nq_id = cpu_to_le16(queue_id);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_map_cmd_send;
	qset_id = le16_to_cpu(nq_to_qs_map->qset_id);

	/* convert qset_id to the following format, drop the vld bit
	 *            | qs_id_h | vld | qs_id_l |
	 * qset_id:   | 15 ~ 11 |  10 |  9 ~ 0  |
	 *             \         \   /         /
	 *              \         \ /         /
	 * qset_id: | 15 | 14 ~ 10 |  9 ~ 0  |
	 */
	qs_id_l = hnae3_get_field(qset_id, HCLGE_TM_QS_ID_L_MSK,
				  HCLGE_TM_QS_ID_L_S);
	qs_id_h = hnae3_get_field(qset_id, HCLGE_TM_QS_ID_H_EXT_MSK,
				  HCLGE_TM_QS_ID_H_EXT_S);
	qset_id = 0;
	hnae3_set_field(qset_id, HCLGE_TM_QS_ID_L_MSK, HCLGE_TM_QS_ID_L_S,
			qs_id_l);
	hnae3_set_field(qset_id, HCLGE_TM_QS_ID_H_MSK, HCLGE_TM_QS_ID_H_S,
			qs_id_h);

	cmd = HCLGE_OPC_TM_QS_TO_PRI_LINK;
	map = (struct hclge_qs_to_pri_link_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	map->qs_id = cpu_to_le16(qset_id);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_map_cmd_send;
	pri_id = map->priority;

	cmd = HCLGE_OPC_TQP_TX_QUEUE_TC;
	tc = (struct hclge_tqp_tx_queue_tc_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	tc->queue_id = cpu_to_le16(queue_id);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_map_cmd_send;
	tc_id = tc->tc_id & 0x7;

	dev_info(&hdev->pdev->dev, "queue_id | qset_id | pri_id | tc_id\n");
	dev_info(&hdev->pdev->dev, "%04u     | %04u    | %02u     | %02u\n",
		 queue_id, qset_id, pri_id, tc_id);

	if (!hnae3_dev_dcb_supported(hdev)) {
		dev_info(&hdev->pdev->dev,
			 "Only DCB-supported dev supports tm mapping\n");
		return;
	}

	grp_num = hdev->num_tqps <= HCLGE_TQP_MAX_SIZE_DEV_V2 ?
		  HCLGE_BP_GRP_NUM : HCLGE_BP_EXT_GRP_NUM;
	cmd = HCLGE_OPC_TM_BP_TO_QSET_MAPPING;
	bp_to_qs_map_cmd = (struct hclge_bp_to_qs_map_cmd *)desc.data;
	for (group_id = 0; group_id < grp_num; group_id++) {
		hclge_cmd_setup_basic_desc(&desc, cmd, true);
		bp_to_qs_map_cmd->tc_id = tc_id;
		bp_to_qs_map_cmd->qs_group_id = group_id;
		ret = hclge_cmd_send(&hdev->hw, &desc, 1);
		if (ret)
			goto err_tm_map_cmd_send;

		qset_mapping[group_id] =
			le32_to_cpu(bp_to_qs_map_cmd->qs_bit_map);
	}

	dev_info(&hdev->pdev->dev, "index | tm bp qset maping:\n");

	i = 0;
	for (group_id = 0; group_id < grp_num / 8; group_id++) {
		dev_info(&hdev->pdev->dev,
			 "%04d  | %08x:%08x:%08x:%08x:%08x:%08x:%08x:%08x\n",
			 group_id * 256, qset_mapping[(u32)(i + 7)],
			 qset_mapping[(u32)(i + 6)], qset_mapping[(u32)(i + 5)],
			 qset_mapping[(u32)(i + 4)], qset_mapping[(u32)(i + 3)],
			 qset_mapping[(u32)(i + 2)], qset_mapping[(u32)(i + 1)],
			 qset_mapping[i]);
		i += 8;
	}

	return;

err_tm_map_cmd_send:
	dev_err(&hdev->pdev->dev, "dump tqp map fail(0x%x), ret = %d\n",
		cmd, ret);
}

static int hclge_dbg_dump_tm_nodes(struct hclge_dev *hdev, char *buf, int len)
{
	struct hclge_tm_nodes_cmd *nodes;
	struct hclge_desc desc;
	int pos = 0;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_NODES, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to dump tm nodes, ret = %d\n", ret);
		return ret;
	}

	nodes = (struct hclge_tm_nodes_cmd *)desc.data;

	pos += scnprintf(buf + pos, len - pos, "       BASE_ID  MAX_NUM\n");
	pos += scnprintf(buf + pos, len - pos, "PG      %4u      %4u\n",
			 nodes->pg_base_id, nodes->pg_num);
	pos += scnprintf(buf + pos, len - pos, "PRI     %4u      %4u\n",
			 nodes->pri_base_id, nodes->pri_num);
	pos += scnprintf(buf + pos, len - pos, "QSET    %4u      %4u\n",
			 le16_to_cpu(nodes->qset_base_id),
			 le16_to_cpu(nodes->qset_num));
	pos += scnprintf(buf + pos, len - pos, "QUEUE   %4u      %4u\n",
			 le16_to_cpu(nodes->queue_base_id),
			 le16_to_cpu(nodes->queue_num));

	return 0;
}

static int hclge_dbg_dump_tm_pri(struct hclge_dev *hdev, char *buf, int len)
{
	struct hclge_pri_shaper_para c_shaper_para;
	struct hclge_pri_shaper_para p_shaper_para;
	u8 pri_num, sch_mode, weight;
	char *sch_mode_str;
	int pos = 0;
	int ret;
	u8 i;

	ret = hclge_tm_get_pri_num(hdev, &pri_num);
	if (ret)
		return ret;

	pos += scnprintf(buf + pos, len - pos,
			 "ID    MODE  DWRR  C_IR_B  C_IR_U  C_IR_S  C_BS_B  ");
	pos += scnprintf(buf + pos, len - pos,
			 "C_BS_S  C_FLAG  C_RATE(Mbps)  P_IR_B  P_IR_U  ");
	pos += scnprintf(buf + pos, len - pos,
			 "P_IR_S  P_BS_B  P_BS_S  P_FLAG  P_RATE(Mbps)\n");

	for (i = 0; i < pri_num; i++) {
		ret = hclge_tm_get_pri_sch_mode(hdev, i, &sch_mode);
		if (ret)
			return ret;

		ret = hclge_tm_get_pri_weight(hdev, i, &weight);
		if (ret)
			return ret;

		ret = hclge_tm_get_pri_shaper(hdev, i,
					      HCLGE_OPC_TM_PRI_C_SHAPPING,
					      &c_shaper_para);
		if (ret)
			return ret;

		ret = hclge_tm_get_pri_shaper(hdev, i,
					      HCLGE_OPC_TM_PRI_P_SHAPPING,
					      &p_shaper_para);
		if (ret)
			return ret;

		sch_mode_str = sch_mode & HCLGE_TM_TX_SCHD_DWRR_MSK ? "dwrr" :
			       "sp";

		pos += scnprintf(buf + pos, len - pos,
				 "%04u  %4s  %3u   %3u     %3u     %3u     ",
				 i, sch_mode_str, weight, c_shaper_para.ir_b,
				 c_shaper_para.ir_u, c_shaper_para.ir_s);
		pos += scnprintf(buf + pos, len - pos,
				 "%3u     %3u       %1u     %6u        ",
				 c_shaper_para.bs_b, c_shaper_para.bs_s,
				 c_shaper_para.flag, c_shaper_para.rate);
		pos += scnprintf(buf + pos, len - pos,
				 "%3u     %3u     %3u     %3u     %3u       ",
				 p_shaper_para.ir_b, p_shaper_para.ir_u,
				 p_shaper_para.ir_s, p_shaper_para.bs_b,
				 p_shaper_para.bs_s);
		pos += scnprintf(buf + pos, len - pos, "%1u     %6u\n",
				 p_shaper_para.flag, p_shaper_para.rate);
	}

	return 0;
}

static int hclge_dbg_dump_tm_qset(struct hclge_dev *hdev, char *buf, int len)
{
	u8 priority, link_vld, sch_mode, weight;
	char *sch_mode_str;
	int ret, pos;
	u16 qset_num;
	u16 i;

	ret = hclge_tm_get_qset_num(hdev, &qset_num);
	if (ret)
		return ret;

	pos = scnprintf(buf, len, "ID    MAP_PRI  LINK_VLD  MODE  DWRR\n");

	for (i = 0; i < qset_num; i++) {
		ret = hclge_tm_get_qset_map_pri(hdev, i, &priority, &link_vld);
		if (ret)
			return ret;

		ret = hclge_tm_get_qset_sch_mode(hdev, i, &sch_mode);
		if (ret)
			return ret;

		ret = hclge_tm_get_qset_weight(hdev, i, &weight);
		if (ret)
			return ret;

		sch_mode_str = sch_mode & HCLGE_TM_TX_SCHD_DWRR_MSK ? "dwrr" :
			       "sp";
		pos += scnprintf(buf + pos, len - pos,
				 "%04u  %4u        %1u      %4s  %3u\n",
				 i, priority, link_vld, sch_mode_str, weight);
	}

	return 0;
}

static void hclge_dbg_dump_qos_pause_cfg(struct hclge_dev *hdev)
{
	struct hclge_cfg_pause_param_cmd *pause_param;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_MAC_PARA, true);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev, "dump checksum fail, ret = %d\n",
			ret);
		return;
	}

	pause_param = (struct hclge_cfg_pause_param_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "dump qos pause cfg\n");
	dev_info(&hdev->pdev->dev, "pause_trans_gap: 0x%x\n",
		 pause_param->pause_trans_gap);
	dev_info(&hdev->pdev->dev, "pause_trans_time: 0x%x\n",
		 le16_to_cpu(pause_param->pause_trans_time));
}

static void hclge_dbg_dump_qos_pri_map(struct hclge_dev *hdev)
{
	struct hclge_qos_pri_map_cmd *pri_map;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_PRI_TO_TC_MAPPING, true);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"dump qos pri map fail, ret = %d\n", ret);
		return;
	}

	pri_map = (struct hclge_qos_pri_map_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "dump qos pri map\n");
	dev_info(&hdev->pdev->dev, "vlan_to_pri: 0x%x\n", pri_map->vlan_pri);
	dev_info(&hdev->pdev->dev, "pri_0_to_tc: 0x%x\n", pri_map->pri0_tc);
	dev_info(&hdev->pdev->dev, "pri_1_to_tc: 0x%x\n", pri_map->pri1_tc);
	dev_info(&hdev->pdev->dev, "pri_2_to_tc: 0x%x\n", pri_map->pri2_tc);
	dev_info(&hdev->pdev->dev, "pri_3_to_tc: 0x%x\n", pri_map->pri3_tc);
	dev_info(&hdev->pdev->dev, "pri_4_to_tc: 0x%x\n", pri_map->pri4_tc);
	dev_info(&hdev->pdev->dev, "pri_5_to_tc: 0x%x\n", pri_map->pri5_tc);
	dev_info(&hdev->pdev->dev, "pri_6_to_tc: 0x%x\n", pri_map->pri6_tc);
	dev_info(&hdev->pdev->dev, "pri_7_to_tc: 0x%x\n", pri_map->pri7_tc);
}

static int hclge_dbg_dump_tx_buf_cfg(struct hclge_dev *hdev)
{
	struct hclge_tx_buff_alloc_cmd *tx_buf_cmd;
	struct hclge_desc desc;
	int i, ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TX_BUFF_ALLOC, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		return ret;

	dev_info(&hdev->pdev->dev, "dump qos buf cfg\n");
	tx_buf_cmd = (struct hclge_tx_buff_alloc_cmd *)desc.data;
	for (i = 0; i < HCLGE_MAX_TC_NUM; i++)
		dev_info(&hdev->pdev->dev, "tx_packet_buf_tc_%d: 0x%x\n", i,
			 le16_to_cpu(tx_buf_cmd->tx_pkt_buff[i]));

	return 0;
}

static int hclge_dbg_dump_rx_priv_buf_cfg(struct hclge_dev *hdev)
{
	struct hclge_rx_priv_buff_cmd *rx_buf_cmd;
	struct hclge_desc desc;
	int i, ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RX_PRIV_BUFF_ALLOC, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		return ret;

	dev_info(&hdev->pdev->dev, "\n");
	rx_buf_cmd = (struct hclge_rx_priv_buff_cmd *)desc.data;
	for (i = 0; i < HCLGE_MAX_TC_NUM; i++)
		dev_info(&hdev->pdev->dev, "rx_packet_buf_tc_%d: 0x%x\n", i,
			 le16_to_cpu(rx_buf_cmd->buf_num[i]));

	dev_info(&hdev->pdev->dev, "rx_share_buf: 0x%x\n",
		 le16_to_cpu(rx_buf_cmd->shared_buf));

	return 0;
}

static int hclge_dbg_dump_rx_common_wl_cfg(struct hclge_dev *hdev)
{
	struct hclge_rx_com_wl *rx_com_wl;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RX_COM_WL_ALLOC, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		return ret;

	rx_com_wl = (struct hclge_rx_com_wl *)desc.data;
	dev_info(&hdev->pdev->dev, "\n");
	dev_info(&hdev->pdev->dev, "rx_com_wl: high: 0x%x, low: 0x%x\n",
		 le16_to_cpu(rx_com_wl->com_wl.high),
		 le16_to_cpu(rx_com_wl->com_wl.low));

	return 0;
}

static int hclge_dbg_dump_rx_global_pkt_cnt(struct hclge_dev *hdev)
{
	struct hclge_rx_com_wl *rx_packet_cnt;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_RX_GBL_PKT_CNT, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		return ret;

	rx_packet_cnt = (struct hclge_rx_com_wl *)desc.data;
	dev_info(&hdev->pdev->dev,
		 "rx_global_packet_cnt: high: 0x%x, low: 0x%x\n",
		 le16_to_cpu(rx_packet_cnt->com_wl.high),
		 le16_to_cpu(rx_packet_cnt->com_wl.low));

	return 0;
}

static int hclge_dbg_dump_rx_priv_wl_buf_cfg(struct hclge_dev *hdev)
{
	struct hclge_rx_priv_wl_buf *rx_priv_wl;
	struct hclge_desc desc[2];
	int i, ret;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_RX_PRIV_WL_ALLOC, true);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[1], HCLGE_OPC_RX_PRIV_WL_ALLOC, true);
	ret = hclge_cmd_send(&hdev->hw, desc, 2);
	if (ret)
		return ret;

	rx_priv_wl = (struct hclge_rx_priv_wl_buf *)desc[0].data;
	for (i = 0; i < HCLGE_TC_NUM_ONE_DESC; i++)
		dev_info(&hdev->pdev->dev,
			 "rx_priv_wl_tc_%d: high: 0x%x, low: 0x%x\n", i,
			 le16_to_cpu(rx_priv_wl->tc_wl[i].high),
			 le16_to_cpu(rx_priv_wl->tc_wl[i].low));

	rx_priv_wl = (struct hclge_rx_priv_wl_buf *)desc[1].data;
	for (i = 0; i < HCLGE_TC_NUM_ONE_DESC; i++)
		dev_info(&hdev->pdev->dev,
			 "rx_priv_wl_tc_%d: high: 0x%x, low: 0x%x\n",
			 i + HCLGE_TC_NUM_ONE_DESC,
			 le16_to_cpu(rx_priv_wl->tc_wl[i].high),
			 le16_to_cpu(rx_priv_wl->tc_wl[i].low));

	return 0;
}

static int hclge_dbg_dump_rx_common_threshold_cfg(struct hclge_dev *hdev)
{
	struct hclge_rx_com_thrd *rx_com_thrd;
	struct hclge_desc desc[2];
	int i, ret;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_RX_COM_THRD_ALLOC, true);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[1], HCLGE_OPC_RX_COM_THRD_ALLOC, true);
	ret = hclge_cmd_send(&hdev->hw, desc, 2);
	if (ret)
		return ret;

	dev_info(&hdev->pdev->dev, "\n");
	rx_com_thrd = (struct hclge_rx_com_thrd *)desc[0].data;
	for (i = 0; i < HCLGE_TC_NUM_ONE_DESC; i++)
		dev_info(&hdev->pdev->dev,
			 "rx_com_thrd_tc_%d: high: 0x%x, low: 0x%x\n", i,
			 le16_to_cpu(rx_com_thrd->com_thrd[i].high),
			 le16_to_cpu(rx_com_thrd->com_thrd[i].low));

	rx_com_thrd = (struct hclge_rx_com_thrd *)desc[1].data;
	for (i = 0; i < HCLGE_TC_NUM_ONE_DESC; i++)
		dev_info(&hdev->pdev->dev,
			 "rx_com_thrd_tc_%d: high: 0x%x, low: 0x%x\n",
			 i + HCLGE_TC_NUM_ONE_DESC,
			 le16_to_cpu(rx_com_thrd->com_thrd[i].high),
			 le16_to_cpu(rx_com_thrd->com_thrd[i].low));

	return 0;
}

static void hclge_dbg_dump_qos_buf_cfg(struct hclge_dev *hdev)
{
	enum hclge_opcode_type cmd;
	int ret;

	cmd = HCLGE_OPC_TX_BUFF_ALLOC;
	ret = hclge_dbg_dump_tx_buf_cfg(hdev);
	if (ret)
		goto err_qos_cmd_send;

	cmd = HCLGE_OPC_RX_PRIV_BUFF_ALLOC;
	ret = hclge_dbg_dump_rx_priv_buf_cfg(hdev);
	if (ret)
		goto err_qos_cmd_send;

	cmd = HCLGE_OPC_RX_COM_WL_ALLOC;
	ret = hclge_dbg_dump_rx_common_wl_cfg(hdev);
	if (ret)
		goto err_qos_cmd_send;

	cmd = HCLGE_OPC_RX_GBL_PKT_CNT;
	ret = hclge_dbg_dump_rx_global_pkt_cnt(hdev);
	if (ret)
		goto err_qos_cmd_send;

	dev_info(&hdev->pdev->dev, "\n");
	if (!hnae3_dev_dcb_supported(hdev)) {
		dev_info(&hdev->pdev->dev,
			 "Only DCB-supported dev supports rx priv wl\n");
		return;
	}

	cmd = HCLGE_OPC_RX_PRIV_WL_ALLOC;
	ret = hclge_dbg_dump_rx_priv_wl_buf_cfg(hdev);
	if (ret)
		goto err_qos_cmd_send;

	cmd = HCLGE_OPC_RX_COM_THRD_ALLOC;
	ret = hclge_dbg_dump_rx_common_threshold_cfg(hdev);
	if (ret)
		goto err_qos_cmd_send;

	return;

err_qos_cmd_send:
	dev_err(&hdev->pdev->dev,
		"dump qos buf cfg fail(0x%x), ret = %d\n", cmd, ret);
}

static int hclge_dbg_dump_mng_table(struct hclge_dev *hdev, char *buf, int len)
{
	struct hclge_mac_ethertype_idx_rd_cmd *req0;
	struct hclge_desc desc;
	u32 msg_egress_port;
	int pos = 0;
	int ret, i;

	pos += scnprintf(buf + pos, len - pos,
			 "entry  mac_addr          mask  ether  ");
	pos += scnprintf(buf + pos, len - pos,
			 "mask  vlan  mask  i_map  i_dir  e_type  ");
	pos += scnprintf(buf + pos, len - pos, "pf_id  vf_id  q_id  drop\n");

	for (i = 0; i < HCLGE_DBG_MNG_TBL_MAX; i++) {
		hclge_cmd_setup_basic_desc(&desc, HCLGE_MAC_ETHERTYPE_IDX_RD,
					   true);
		req0 = (struct hclge_mac_ethertype_idx_rd_cmd *)&desc.data;
		req0->index = cpu_to_le16(i);

		ret = hclge_cmd_send(&hdev->hw, &desc, 1);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"failed to dump manage table, ret = %d\n", ret);
			return ret;
		}

		if (!req0->resp_code)
			continue;

		pos += scnprintf(buf + pos, len - pos, "%02u     %pM ",
				 le16_to_cpu(req0->index), req0->mac_addr);

		pos += scnprintf(buf + pos, len - pos,
				 "%x     %04x   %x     %04x  ",
				 !!(req0->flags & HCLGE_DBG_MNG_MAC_MASK_B),
				 le16_to_cpu(req0->ethter_type),
				 !!(req0->flags & HCLGE_DBG_MNG_ETHER_MASK_B),
				 le16_to_cpu(req0->vlan_tag) &
				 HCLGE_DBG_MNG_VLAN_TAG);

		pos += scnprintf(buf + pos, len - pos,
				 "%x     %02x     %02x     ",
				 !!(req0->flags & HCLGE_DBG_MNG_VLAN_MASK_B),
				 req0->i_port_bitmap, req0->i_port_direction);

		msg_egress_port = le16_to_cpu(req0->egress_port);
		pos += scnprintf(buf + pos, len - pos,
				 "%x       %x      %02x     %04x  %x\n",
				 !!(msg_egress_port & HCLGE_DBG_MNG_E_TYPE_B),
				 msg_egress_port & HCLGE_DBG_MNG_PF_ID,
				 (msg_egress_port >> 3) & HCLGE_DBG_MNG_VF_ID,
				 le16_to_cpu(req0->egress_queue),
				 !!(msg_egress_port & HCLGE_DBG_MNG_DROP_B));
	}

	return 0;
}

static int hclge_dbg_fd_tcam_read(struct hclge_dev *hdev, u8 stage,
				  bool sel_x, u32 loc)
{
	struct hclge_fd_tcam_config_1_cmd *req1;
	struct hclge_fd_tcam_config_2_cmd *req2;
	struct hclge_fd_tcam_config_3_cmd *req3;
	struct hclge_desc desc[3];
	int ret, i;
	u32 *req;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_FD_TCAM_OP, true);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[1], HCLGE_OPC_FD_TCAM_OP, true);
	desc[1].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[2], HCLGE_OPC_FD_TCAM_OP, true);

	req1 = (struct hclge_fd_tcam_config_1_cmd *)desc[0].data;
	req2 = (struct hclge_fd_tcam_config_2_cmd *)desc[1].data;
	req3 = (struct hclge_fd_tcam_config_3_cmd *)desc[2].data;

	req1->stage  = stage;
	req1->xy_sel = sel_x ? 1 : 0;
	req1->index  = cpu_to_le32(loc);

	ret = hclge_cmd_send(&hdev->hw, desc, 3);
	if (ret)
		return ret;

	dev_info(&hdev->pdev->dev, " read result tcam key %s(%u):\n",
		 sel_x ? "x" : "y", loc);

	/* tcam_data0 ~ tcam_data1 */
	req = (u32 *)req1->tcam_data;
	for (i = 0; i < 2; i++)
		dev_info(&hdev->pdev->dev, "%08x\n", *req++);

	/* tcam_data2 ~ tcam_data7 */
	req = (u32 *)req2->tcam_data;
	for (i = 0; i < 6; i++)
		dev_info(&hdev->pdev->dev, "%08x\n", *req++);

	/* tcam_data8 ~ tcam_data12 */
	req = (u32 *)req3->tcam_data;
	for (i = 0; i < 5; i++)
		dev_info(&hdev->pdev->dev, "%08x\n", *req++);

	return ret;
}

static int hclge_dbg_get_rules_location(struct hclge_dev *hdev, u16 *rule_locs)
{
	struct hclge_fd_rule *rule;
	struct hlist_node *node;
	int cnt = 0;

	spin_lock_bh(&hdev->fd_rule_lock);
	hlist_for_each_entry_safe(rule, node, &hdev->fd_rule_list, rule_node) {
		rule_locs[cnt] = rule->location;
		cnt++;
	}
	spin_unlock_bh(&hdev->fd_rule_lock);

	if (cnt != hdev->hclge_fd_rule_num)
		return -EINVAL;

	return cnt;
}

static void hclge_dbg_fd_tcam(struct hclge_dev *hdev)
{
	int i, ret, rule_cnt;
	u16 *rule_locs;

	if (!hnae3_dev_fd_supported(hdev)) {
		dev_err(&hdev->pdev->dev,
			"Only FD-supported dev supports dump fd tcam\n");
		return;
	}

	if (!hdev->hclge_fd_rule_num ||
	    !hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1])
		return;

	rule_locs = kcalloc(hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1],
			    sizeof(u16), GFP_KERNEL);
	if (!rule_locs)
		return;

	rule_cnt = hclge_dbg_get_rules_location(hdev, rule_locs);
	if (rule_cnt <= 0) {
		dev_err(&hdev->pdev->dev,
			"failed to get rule number, ret = %d\n", rule_cnt);
		kfree(rule_locs);
		return;
	}

	for (i = 0; i < rule_cnt; i++) {
		ret = hclge_dbg_fd_tcam_read(hdev, 0, true, rule_locs[i]);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"failed to get fd tcam key x, ret = %d\n", ret);
			kfree(rule_locs);
			return;
		}

		ret = hclge_dbg_fd_tcam_read(hdev, 0, false, rule_locs[i]);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"failed to get fd tcam key y, ret = %d\n", ret);
			kfree(rule_locs);
			return;
		}
	}

	kfree(rule_locs);
}

int hclge_dbg_dump_rst_info(struct hclge_dev *hdev, char *buf, int len)
{
	int pos = 0;

	pos += scnprintf(buf + pos, len - pos, "PF reset count: %u\n",
			 hdev->rst_stats.pf_rst_cnt);
	pos += scnprintf(buf + pos, len - pos, "FLR reset count: %u\n",
			 hdev->rst_stats.flr_rst_cnt);
	pos += scnprintf(buf + pos, len - pos, "GLOBAL reset count: %u\n",
			 hdev->rst_stats.global_rst_cnt);
	pos += scnprintf(buf + pos, len - pos, "IMP reset count: %u\n",
			 hdev->rst_stats.imp_rst_cnt);
	pos += scnprintf(buf + pos, len - pos, "reset done count: %u\n",
			 hdev->rst_stats.reset_done_cnt);
	pos += scnprintf(buf + pos, len - pos, "HW reset done count: %u\n",
			 hdev->rst_stats.hw_reset_done_cnt);
	pos += scnprintf(buf + pos, len - pos, "reset count: %u\n",
			 hdev->rst_stats.reset_cnt);
	pos += scnprintf(buf + pos, len - pos, "reset fail count: %u\n",
			 hdev->rst_stats.reset_fail_cnt);
	pos += scnprintf(buf + pos, len - pos,
			 "vector0 interrupt enable status: 0x%x\n",
			 hclge_read_dev(&hdev->hw, HCLGE_MISC_VECTOR_REG_BASE));
	pos += scnprintf(buf + pos, len - pos, "reset interrupt source: 0x%x\n",
			 hclge_read_dev(&hdev->hw, HCLGE_MISC_RESET_STS_REG));
	pos += scnprintf(buf + pos, len - pos, "reset interrupt status: 0x%x\n",
			 hclge_read_dev(&hdev->hw, HCLGE_MISC_VECTOR_INT_STS));
	pos += scnprintf(buf + pos, len - pos, "RAS interrupt status: 0x%x\n",
			 hclge_read_dev(&hdev->hw,
					HCLGE_RAS_PF_OTHER_INT_STS_REG));
	pos += scnprintf(buf + pos, len - pos, "hardware reset status: 0x%x\n",
			 hclge_read_dev(&hdev->hw, HCLGE_GLOBAL_RESET_REG));
	pos += scnprintf(buf + pos, len - pos, "handshake status: 0x%x\n",
			 hclge_read_dev(&hdev->hw, HCLGE_NIC_CSQ_DEPTH_REG));
	pos += scnprintf(buf + pos, len - pos, "function reset status: 0x%x\n",
			 hclge_read_dev(&hdev->hw, HCLGE_FUN_RST_ING));
	pos += scnprintf(buf + pos, len - pos, "hdev state: 0x%lx\n",
			 hdev->state);

	return 0;
}

static void hclge_dbg_dump_serv_info(struct hclge_dev *hdev)
{
	dev_info(&hdev->pdev->dev, "last_serv_processed: %lu\n",
		 hdev->last_serv_processed);
	dev_info(&hdev->pdev->dev, "last_serv_cnt: %lu\n",
		 hdev->serv_processed_cnt);
}

static int hclge_dbg_dump_interrupt(struct hclge_dev *hdev, char *buf, int len)
{
	int pos = 0;

	pos += scnprintf(buf + pos, len - pos, "num_nic_msi: %u\n",
			 hdev->num_nic_msi);
	pos += scnprintf(buf + pos, len - pos, "num_roce_msi: %u\n",
			 hdev->num_roce_msi);
	pos += scnprintf(buf + pos, len - pos, "num_msi_used: %u\n",
			 hdev->num_msi_used);
	pos += scnprintf(buf + pos, len - pos, "num_msi_left: %u\n",
			 hdev->num_msi_left);

	return 0;
}

static void hclge_dbg_imp_info_data_print(struct hclge_desc *desc_src,
					  char *buf, int len, u32 bd_num)
{
#define HCLGE_DBG_IMP_INFO_PRINT_OFFSET 0x2

	struct hclge_desc *desc_index = desc_src;
	u32 offset = 0;
	int pos = 0;
	u32 i, j;

	pos += scnprintf(buf + pos, len - pos, "offset | data\n");

	for (i = 0; i < bd_num; i++) {
		j = 0;
		while (j < HCLGE_DESC_DATA_LEN - 1) {
			pos += scnprintf(buf + pos, len - pos, "0x%04x | ",
					 offset);
			pos += scnprintf(buf + pos, len - pos, "0x%08x  ",
					 le32_to_cpu(desc_index->data[j++]));
			pos += scnprintf(buf + pos, len - pos, "0x%08x\n",
					 le32_to_cpu(desc_index->data[j++]));
			offset += sizeof(u32) * HCLGE_DBG_IMP_INFO_PRINT_OFFSET;
		}
		desc_index++;
	}
}

static int
hclge_dbg_get_imp_stats_info(struct hclge_dev *hdev, char *buf, int len)
{
	struct hclge_get_imp_bd_cmd *req;
	struct hclge_desc *desc_src;
	struct hclge_desc desc;
	u32 bd_num;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_IMP_STATS_BD, true);

	req = (struct hclge_get_imp_bd_cmd *)desc.data;
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get imp statistics bd number, ret = %d\n",
			ret);
		return ret;
	}

	bd_num = le32_to_cpu(req->bd_num);

	desc_src = kcalloc(bd_num, sizeof(struct hclge_desc), GFP_KERNEL);
	if (!desc_src)
		return -ENOMEM;

	ret  = hclge_dbg_cmd_send(hdev, desc_src, 0, bd_num,
				  HCLGE_OPC_IMP_STATS_INFO);
	if (ret) {
		kfree(desc_src);
		dev_err(&hdev->pdev->dev,
			"failed to get imp statistics, ret = %d\n", ret);
		return ret;
	}

	hclge_dbg_imp_info_data_print(desc_src, buf, len, bd_num);

	kfree(desc_src);

	return 0;
}

#define HCLGE_CMD_NCL_CONFIG_BD_NUM	5
#define HCLGE_MAX_NCL_CONFIG_LENGTH	16384

static void hclge_ncl_config_data_print(struct hclge_desc *desc, int *index,
					char *buf, int *len, int *pos)
{
#define HCLGE_CMD_DATA_NUM		6

	int offset = HCLGE_MAX_NCL_CONFIG_LENGTH - *index;
	int i, j;

	for (i = 0; i < HCLGE_CMD_NCL_CONFIG_BD_NUM; i++) {
		for (j = 0; j < HCLGE_CMD_DATA_NUM; j++) {
			if (i == 0 && j == 0)
				continue;

			*pos += scnprintf(buf + *pos, *len - *pos,
					  "0x%04x | 0x%08x\n", offset,
					  le32_to_cpu(desc[i].data[j]));

			offset += sizeof(u32);
			*index -= sizeof(u32);

			if (*index <= 0)
				return;
		}
	}
}

static int
hclge_dbg_dump_ncl_config(struct hclge_dev *hdev, char *buf, int len)
{
#define HCLGE_NCL_CONFIG_LENGTH_IN_EACH_CMD	(20 + 24 * 4)

	struct hclge_desc desc[HCLGE_CMD_NCL_CONFIG_BD_NUM];
	int bd_num = HCLGE_CMD_NCL_CONFIG_BD_NUM;
	int index = HCLGE_MAX_NCL_CONFIG_LENGTH;
	int pos = 0;
	u32 data0;
	int ret;

	pos += scnprintf(buf + pos, len - pos, "offset | data\n");

	while (index > 0) {
		data0 = HCLGE_MAX_NCL_CONFIG_LENGTH - index;
		if (index >= HCLGE_NCL_CONFIG_LENGTH_IN_EACH_CMD)
			data0 |= HCLGE_NCL_CONFIG_LENGTH_IN_EACH_CMD << 16;
		else
			data0 |= (u32)index << 16;
		ret = hclge_dbg_cmd_send(hdev, desc, data0, bd_num,
					 HCLGE_OPC_QUERY_NCL_CONFIG);
		if (ret)
			return ret;

		hclge_ncl_config_data_print(desc, &index, buf, &len, &pos);
	}

	return 0;
}

static int hclge_dbg_dump_loopback(struct hclge_dev *hdev, char *buf, int len)
{
	struct phy_device *phydev = hdev->hw.mac.phydev;
	struct hclge_config_mac_mode_cmd *req_app;
	struct hclge_common_lb_cmd *req_common;
	struct hclge_desc desc;
	u8 loopback_en;
	int pos = 0;
	int ret;

	req_app = (struct hclge_config_mac_mode_cmd *)desc.data;
	req_common = (struct hclge_common_lb_cmd *)desc.data;

	pos += scnprintf(buf + pos, len - pos, "mac id: %u\n",
			 hdev->hw.mac.mac_id);

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CONFIG_MAC_MODE, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to dump app loopback status, ret = %d\n", ret);
		return ret;
	}

	loopback_en = hnae3_get_bit(le32_to_cpu(req_app->txrx_pad_fcs_loop_en),
				    HCLGE_MAC_APP_LP_B);
	pos += scnprintf(buf + pos, len - pos, "app loopback: %s\n",
			 state_str[loopback_en]);

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_COMMON_LOOPBACK, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to dump common loopback status, ret = %d\n",
			ret);
		return ret;
	}

	loopback_en = req_common->enable & HCLGE_CMD_SERDES_SERIAL_INNER_LOOP_B;
	pos += scnprintf(buf + pos, len - pos, "serdes serial loopback: %s\n",
			 state_str[loopback_en]);

	loopback_en = req_common->enable &
			HCLGE_CMD_SERDES_PARALLEL_INNER_LOOP_B ? 1 : 0;
	pos += scnprintf(buf + pos, len - pos, "serdes parallel loopback: %s\n",
			 state_str[loopback_en]);

	if (phydev) {
		loopback_en = phydev->loopback_enabled;
		pos += scnprintf(buf + pos, len - pos, "phy loopback: %s\n",
				 state_str[loopback_en]);
	} else if (hnae3_dev_phy_imp_supported(hdev)) {
		loopback_en = req_common->enable &
			      HCLGE_CMD_GE_PHY_INNER_LOOP_B;
		pos += scnprintf(buf + pos, len - pos, "phy loopback: %s\n",
				 state_str[loopback_en]);
	}

	return 0;
}

/* hclge_dbg_dump_mac_tnl_status: print message about mac tnl interrupt
 * @hdev: pointer to struct hclge_dev
 */
static void hclge_dbg_dump_mac_tnl_status(struct hclge_dev *hdev)
{
#define HCLGE_BILLION_NANO_SECONDS 1000000000

	struct hclge_mac_tnl_stats stats;
	unsigned long rem_nsec;

	dev_info(&hdev->pdev->dev, "Recently generated mac tnl interruption:\n");

	while (kfifo_get(&hdev->mac_tnl_log, &stats)) {
		rem_nsec = do_div(stats.time, HCLGE_BILLION_NANO_SECONDS);
		dev_info(&hdev->pdev->dev, "[%07lu.%03lu] status = 0x%x\n",
			 (unsigned long)stats.time, rem_nsec / 1000,
			 stats.status);
	}
}

static void hclge_dbg_dump_qs_shaper_single(struct hclge_dev *hdev, u16 qsid)
{
	struct hclge_qs_shapping_cmd *shap_cfg_cmd;
	u8 ir_u, ir_b, ir_s, bs_b, bs_s;
	struct hclge_desc desc;
	u32 shapping_para;
	u32 rate;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QCN_SHAPPING_CFG, true);

	shap_cfg_cmd = (struct hclge_qs_shapping_cmd *)desc.data;
	shap_cfg_cmd->qs_id = cpu_to_le16(qsid);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"qs%u failed to get tx_rate, ret=%d\n",
			qsid, ret);
		return;
	}

	shapping_para = le32_to_cpu(shap_cfg_cmd->qs_shapping_para);
	ir_b = hclge_tm_get_field(shapping_para, IR_B);
	ir_u = hclge_tm_get_field(shapping_para, IR_U);
	ir_s = hclge_tm_get_field(shapping_para, IR_S);
	bs_b = hclge_tm_get_field(shapping_para, BS_B);
	bs_s = hclge_tm_get_field(shapping_para, BS_S);
	rate = le32_to_cpu(shap_cfg_cmd->qs_rate);

	dev_info(&hdev->pdev->dev,
		 "qs%u ir_b:%u, ir_u:%u, ir_s:%u, bs_b:%u, bs_s:%u, flag:%#x, rate:%u(Mbps)\n",
		 qsid, ir_b, ir_u, ir_s, bs_b, bs_s, shap_cfg_cmd->flag, rate);
}

static void hclge_dbg_dump_qs_shaper_all(struct hclge_dev *hdev)
{
	struct hnae3_knic_private_info *kinfo;
	struct hclge_vport *vport;
	int vport_id, i;

	for (vport_id = 0; vport_id <= pci_num_vf(hdev->pdev); vport_id++) {
		vport = &hdev->vport[vport_id];
		kinfo = &vport->nic.kinfo;

		dev_info(&hdev->pdev->dev, "qs cfg of vport%d:\n", vport_id);

		for (i = 0; i < kinfo->tc_info.num_tc; i++) {
			u16 qsid = vport->qs_offset + i;

			hclge_dbg_dump_qs_shaper_single(hdev, qsid);
		}
	}
}

static void hclge_dbg_dump_qs_shaper(struct hclge_dev *hdev,
				     const char *cmd_buf)
{
	u16 qsid;
	int ret;

	ret = kstrtou16(cmd_buf, 0, &qsid);
	if (ret) {
		hclge_dbg_dump_qs_shaper_all(hdev);
		return;
	}

	if (qsid >= hdev->ae_dev->dev_specs.max_qset_num) {
		dev_err(&hdev->pdev->dev, "qsid(%u) out of range[0-%u]\n",
			qsid, hdev->ae_dev->dev_specs.max_qset_num - 1);
		return;
	}

	hclge_dbg_dump_qs_shaper_single(hdev, qsid);
}

static const struct hclge_dbg_item mac_list_items[] = {
	{ "FUNC_ID", 2 },
	{ "MAC_ADDR", 12 },
	{ "STATE", 2 },
};

static void hclge_dbg_dump_mac_list(struct hclge_dev *hdev, char *buf, int len,
				    bool is_unicast)
{
	char data_str[ARRAY_SIZE(mac_list_items)][HCLGE_DBG_DATA_STR_LEN];
	char content[HCLGE_DBG_INFO_LEN], str_id[HCLGE_DBG_ID_LEN];
	char *result[ARRAY_SIZE(mac_list_items)];
	struct hclge_mac_node *mac_node, *tmp;
	struct hclge_vport *vport;
	struct list_head *list;
	u32 func_id;
	int pos = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(mac_list_items); i++)
		result[i] = &data_str[i][0];

	pos += scnprintf(buf + pos, len - pos, "%s MAC_LIST:\n",
			 is_unicast ? "UC" : "MC");
	hclge_dbg_fill_content(content, sizeof(content), mac_list_items,
			       NULL, ARRAY_SIZE(mac_list_items));
	pos += scnprintf(buf + pos, len - pos, "%s", content);

	for (func_id = 0; func_id < hdev->num_alloc_vport; func_id++) {
		vport = &hdev->vport[func_id];
		list = is_unicast ? &vport->uc_mac_list : &vport->mc_mac_list;
		spin_lock_bh(&vport->mac_list_lock);
		list_for_each_entry_safe(mac_node, tmp, list, node) {
			i = 0;
			result[i++] = hclge_dbg_get_func_id_str(str_id,
								func_id);
			sprintf(result[i++], "%pM", mac_node->mac_addr);
			sprintf(result[i++], "%5s",
				hclge_mac_state_str[mac_node->state]);
			hclge_dbg_fill_content(content, sizeof(content),
					       mac_list_items,
					       (const char **)result,
					       ARRAY_SIZE(mac_list_items));
			pos += scnprintf(buf + pos, len - pos, "%s", content);
		}
		spin_unlock_bh(&vport->mac_list_lock);
	}
}

static int hclge_dbg_dump_mac_uc(struct hclge_dev *hdev, char *buf, int len)
{
	hclge_dbg_dump_mac_list(hdev, buf, len, true);

	return 0;
}

static int hclge_dbg_dump_mac_mc(struct hclge_dev *hdev, char *buf, int len)
{
	hclge_dbg_dump_mac_list(hdev, buf, len, false);

	return 0;
}

int hclge_dbg_run_cmd(struct hnae3_handle *handle, const char *cmd_buf)
{
#define DUMP_REG_DCB	"dump reg dcb"
#define DUMP_TM_MAP	"dump tm map"

	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	if (strncmp(cmd_buf, "dump fd tcam", 12) == 0) {
		hclge_dbg_fd_tcam(hdev);
	} else if (strncmp(cmd_buf, "dump tc", 7) == 0) {
		hclge_dbg_dump_tc(hdev);
	} else if (strncmp(cmd_buf, DUMP_TM_MAP, strlen(DUMP_TM_MAP)) == 0) {
		hclge_dbg_dump_tm_map(hdev, &cmd_buf[sizeof(DUMP_TM_MAP)]);
	} else if (strncmp(cmd_buf, "dump tm", 7) == 0) {
		hclge_dbg_dump_tm(hdev);
	} else if (strncmp(cmd_buf, "dump qos pause cfg", 18) == 0) {
		hclge_dbg_dump_qos_pause_cfg(hdev);
	} else if (strncmp(cmd_buf, "dump qos pri map", 16) == 0) {
		hclge_dbg_dump_qos_pri_map(hdev);
	} else if (strncmp(cmd_buf, "dump qos buf cfg", 16) == 0) {
		hclge_dbg_dump_qos_buf_cfg(hdev);
	} else if (strncmp(cmd_buf, DUMP_REG_DCB, strlen(DUMP_REG_DCB)) == 0) {
		hclge_dbg_dump_dcb(hdev, &cmd_buf[sizeof(DUMP_REG_DCB)]);
	} else if (strncmp(cmd_buf, "dump serv info", 14) == 0) {
		hclge_dbg_dump_serv_info(hdev);
	} else if (strncmp(cmd_buf, "dump mac tnl status", 19) == 0) {
		hclge_dbg_dump_mac_tnl_status(hdev);
	} else if (strncmp(cmd_buf, "dump qs shaper", 14) == 0) {
		hclge_dbg_dump_qs_shaper(hdev,
					 &cmd_buf[sizeof("dump qs shaper")]);
	} else {
		dev_info(&hdev->pdev->dev, "unknown command\n");
		return -EINVAL;
	}

	return 0;
}

static const struct hclge_dbg_func hclge_dbg_cmd_func[] = {
	{
		.cmd = HNAE3_DBG_CMD_TM_NODES,
		.dbg_dump = hclge_dbg_dump_tm_nodes,
	},
	{
		.cmd = HNAE3_DBG_CMD_TM_PRI,
		.dbg_dump = hclge_dbg_dump_tm_pri,
	},
	{
		.cmd = HNAE3_DBG_CMD_TM_QSET,
		.dbg_dump = hclge_dbg_dump_tm_qset,
	},
	{
		.cmd = HNAE3_DBG_CMD_MAC_UC,
		.dbg_dump = hclge_dbg_dump_mac_uc,
	},
	{
		.cmd = HNAE3_DBG_CMD_MAC_MC,
		.dbg_dump = hclge_dbg_dump_mac_mc,
	},
	{
		.cmd = HNAE3_DBG_CMD_MNG_TBL,
		.dbg_dump = hclge_dbg_dump_mng_table,
	},
	{
		.cmd = HNAE3_DBG_CMD_LOOPBACK,
		.dbg_dump = hclge_dbg_dump_loopback,
	},
	{
		.cmd = HNAE3_DBG_CMD_INTERRUPT_INFO,
		.dbg_dump = hclge_dbg_dump_interrupt,
	},
	{
		.cmd = HNAE3_DBG_CMD_RESET_INFO,
		.dbg_dump = hclge_dbg_dump_rst_info,
	},
	{
		.cmd = HNAE3_DBG_CMD_IMP_INFO,
		.dbg_dump = hclge_dbg_get_imp_stats_info,
	},
	{
		.cmd = HNAE3_DBG_CMD_NCL_CONFIG,
		.dbg_dump = hclge_dbg_dump_ncl_config,
	},
	{
		.cmd = HNAE3_DBG_CMD_REG_BIOS_COMMON,
		.dbg_dump_reg = hclge_dbg_dump_reg_cmd,
	},
	{
		.cmd = HNAE3_DBG_CMD_REG_SSU,
		.dbg_dump_reg = hclge_dbg_dump_reg_cmd,
	},
	{
		.cmd = HNAE3_DBG_CMD_REG_IGU_EGU,
		.dbg_dump_reg = hclge_dbg_dump_reg_cmd,
	},
	{
		.cmd = HNAE3_DBG_CMD_REG_RPU,
		.dbg_dump_reg = hclge_dbg_dump_reg_cmd,
	},
	{
		.cmd = HNAE3_DBG_CMD_REG_NCSI,
		.dbg_dump_reg = hclge_dbg_dump_reg_cmd,
	},
	{
		.cmd = HNAE3_DBG_CMD_REG_RTC,
		.dbg_dump_reg = hclge_dbg_dump_reg_cmd,
	},
	{
		.cmd = HNAE3_DBG_CMD_REG_PPP,
		.dbg_dump_reg = hclge_dbg_dump_reg_cmd,
	},
	{
		.cmd = HNAE3_DBG_CMD_REG_RCB,
		.dbg_dump_reg = hclge_dbg_dump_reg_cmd,
	},
	{
		.cmd = HNAE3_DBG_CMD_REG_TQP,
		.dbg_dump_reg = hclge_dbg_dump_reg_cmd,
	},
	{
		.cmd = HNAE3_DBG_CMD_REG_MAC,
		.dbg_dump = hclge_dbg_dump_mac,
	},
};

int hclge_dbg_read_cmd(struct hnae3_handle *handle, enum hnae3_dbg_cmd cmd,
		       char *buf, int len)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	const struct hclge_dbg_func *cmd_func;
	struct hclge_dev *hdev = vport->back;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(hclge_dbg_cmd_func); i++) {
		if (cmd == hclge_dbg_cmd_func[i].cmd) {
			cmd_func = &hclge_dbg_cmd_func[i];
			if (cmd_func->dbg_dump)
				return cmd_func->dbg_dump(hdev, buf, len);
			else
				return cmd_func->dbg_dump_reg(hdev, cmd, buf,
							      len);
		}
	}

	dev_err(&hdev->pdev->dev, "invalid command(%d)\n", cmd);
	return -EINVAL;
}
