// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#include <linux/device.h>

#include "hclge_debugfs.h"
#include "hclge_cmd.h"
#include "hclge_main.h"
#include "hclge_tm.h"
#include "hnae3.h"

static void hclge_title_idx_print(struct hclge_dev *hdev, bool flag, int index,
				  char *title_buf, char *true_buf,
				  char *false_buf)
{
	if (flag)
		dev_info(&hdev->pdev->dev, "%s(%d): %s\n", title_buf, index,
			 true_buf);
	else
		dev_info(&hdev->pdev->dev, "%s(%d): %s\n", title_buf, index,
			 false_buf);
}

static void hclge_dbg_dump_tc(struct hclge_dev *hdev)
{
	struct hclge_ets_tc_weight_cmd *ets_weight;
	struct hclge_desc desc;
	int i, ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_ETS_TC_WEIGHT, true);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev, "dump tc fail, status is %d.\n", ret);
		return;
	}

	ets_weight = (struct hclge_ets_tc_weight_cmd *)desc.data;

	dev_info(&hdev->pdev->dev, "dump tc\n");
	dev_info(&hdev->pdev->dev, "weight_offset: %u\n",
		 ets_weight->weight_offset);

	for (i = 0; i < HNAE3_MAX_TC; i++)
		hclge_title_idx_print(hdev, ets_weight->tc_weight[i], i,
				      "tc", "no sp mode", "sp mode");
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
		 pg_shap_cfg_cmd->pg_shapping_para);

	cmd = HCLGE_OPC_TM_PG_P_SHAPPING;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	pg_shap_cfg_cmd = (struct hclge_pg_shapping_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "PG_P pg_id: %u\n", pg_shap_cfg_cmd->pg_id);
	dev_info(&hdev->pdev->dev, "PG_P pg_shapping: 0x%x\n",
		 pg_shap_cfg_cmd->pg_shapping_para);

	cmd = HCLGE_OPC_TM_PORT_SHAPPING;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	port_shap_cfg_cmd = (struct hclge_port_shapping_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "PORT port_shapping: 0x%x\n",
		 port_shap_cfg_cmd->port_shapping_para);

	cmd = HCLGE_OPC_TM_PG_SCH_MODE_CFG;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	dev_info(&hdev->pdev->dev, "PG_SCH pg_id: %u\n", desc.data[0]);

	cmd = HCLGE_OPC_TM_PRI_SCH_MODE_CFG;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	dev_info(&hdev->pdev->dev, "PRI_SCH pg_id: %u\n", desc.data[0]);

	cmd = HCLGE_OPC_TM_QS_SCH_MODE_CFG;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	dev_info(&hdev->pdev->dev, "QS_SCH pg_id: %u\n", desc.data[0]);

	cmd = HCLGE_OPC_TM_BP_TO_QSET_MAPPING;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_pg_cmd_send;

	bp_to_qs_map_cmd = (struct hclge_bp_to_qs_map_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "BP_TO_QSET pg_id: %u\n",
		 bp_to_qs_map_cmd->tc_id);
	dev_info(&hdev->pdev->dev, "BP_TO_QSET pg_shapping: 0x%x\n",
		 bp_to_qs_map_cmd->qs_group_id);
	dev_info(&hdev->pdev->dev, "BP_TO_QSET qs_bit_map: 0x%x\n",
		 bp_to_qs_map_cmd->qs_bit_map);
	return;

err_tm_pg_cmd_send:
	dev_err(&hdev->pdev->dev, "dump tm_pg fail(0x%x), status is %d\n",
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
		 qs_to_pri_map->qs_id);
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
	dev_info(&hdev->pdev->dev, "NQ_TO_QS nq_id: %u\n", nq_to_qs_map->nq_id);
	dev_info(&hdev->pdev->dev, "NQ_TO_QS qset_id: %u\n",
		 nq_to_qs_map->qset_id);

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
	dev_info(&hdev->pdev->dev, "QS qs_id: %u\n", qs_weight->qs_id);
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
		 shap_cfg_cmd->pri_shapping_para);

	cmd = HCLGE_OPC_TM_PRI_P_SHAPPING;
	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		goto err_tm_cmd_send;

	shap_cfg_cmd = (struct hclge_pri_shapping_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "PRI_P pri_id: %u\n", shap_cfg_cmd->pri_id);
	dev_info(&hdev->pdev->dev, "PRI_P pri_shapping: 0x%x\n",
		 shap_cfg_cmd->pri_shapping_para);

	hclge_dbg_dump_tm_pg(hdev);

	return;

err_tm_cmd_send:
	dev_err(&hdev->pdev->dev, "dump tm fail(0x%x), status is %d\n",
		cmd, ret);
}

static void hclge_dbg_dump_qos_pause_cfg(struct hclge_dev *hdev)
{
	struct hclge_cfg_pause_param_cmd *pause_param;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_MAC_PARA, true);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev, "dump checksum fail, status is %d.\n",
			ret);
		return;
	}

	pause_param = (struct hclge_cfg_pause_param_cmd *)desc.data;
	dev_info(&hdev->pdev->dev, "dump qos pause cfg\n");
	dev_info(&hdev->pdev->dev, "pause_trans_gap: 0x%x\n",
		 pause_param->pause_trans_gap);
	dev_info(&hdev->pdev->dev, "pause_trans_time: 0x%x\n",
		 pause_param->pause_trans_time);
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
			"dump qos pri map fail, status is %d.\n", ret);
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

static void hclge_dbg_dump_qos_buf_cfg(struct hclge_dev *hdev)
{
	struct hclge_tx_buff_alloc_cmd *tx_buf_cmd;
	struct hclge_rx_priv_buff_cmd *rx_buf_cmd;
	struct hclge_rx_priv_wl_buf *rx_priv_wl;
	struct hclge_rx_com_wl *rx_packet_cnt;
	struct hclge_rx_com_thrd *rx_com_thrd;
	struct hclge_rx_com_wl *rx_com_wl;
	enum hclge_opcode_type cmd;
	struct hclge_desc desc[2];
	int i, ret;

	cmd = HCLGE_OPC_TX_BUFF_ALLOC;
	hclge_cmd_setup_basic_desc(desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, desc, 1);
	if (ret)
		goto err_qos_cmd_send;

	dev_info(&hdev->pdev->dev, "dump qos buf cfg\n");

	tx_buf_cmd = (struct hclge_tx_buff_alloc_cmd *)desc[0].data;
	for (i = 0; i < HCLGE_TC_NUM; i++)
		dev_info(&hdev->pdev->dev, "tx_packet_buf_tc_%d: 0x%x\n", i,
			 tx_buf_cmd->tx_pkt_buff[i]);

	cmd = HCLGE_OPC_RX_PRIV_BUFF_ALLOC;
	hclge_cmd_setup_basic_desc(desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, desc, 1);
	if (ret)
		goto err_qos_cmd_send;

	dev_info(&hdev->pdev->dev, "\n");
	rx_buf_cmd = (struct hclge_rx_priv_buff_cmd *)desc[0].data;
	for (i = 0; i < HCLGE_TC_NUM; i++)
		dev_info(&hdev->pdev->dev, "rx_packet_buf_tc_%d: 0x%x\n", i,
			 rx_buf_cmd->buf_num[i]);

	dev_info(&hdev->pdev->dev, "rx_share_buf: 0x%x\n",
		 rx_buf_cmd->shared_buf);

	cmd = HCLGE_OPC_RX_PRIV_WL_ALLOC;
	hclge_cmd_setup_basic_desc(&desc[0], cmd, true);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[1], cmd, true);
	ret = hclge_cmd_send(&hdev->hw, desc, 2);
	if (ret)
		goto err_qos_cmd_send;

	dev_info(&hdev->pdev->dev, "\n");
	rx_priv_wl = (struct hclge_rx_priv_wl_buf *)desc[0].data;
	for (i = 0; i < HCLGE_TC_NUM_ONE_DESC; i++)
		dev_info(&hdev->pdev->dev,
			 "rx_priv_wl_tc_%d: high: 0x%x, low: 0x%x\n", i,
			 rx_priv_wl->tc_wl[i].high, rx_priv_wl->tc_wl[i].low);

	rx_priv_wl = (struct hclge_rx_priv_wl_buf *)desc[1].data;
	for (i = 0; i < HCLGE_TC_NUM_ONE_DESC; i++)
		dev_info(&hdev->pdev->dev,
			 "rx_priv_wl_tc_%d: high: 0x%x, low: 0x%x\n", i + 4,
			 rx_priv_wl->tc_wl[i].high, rx_priv_wl->tc_wl[i].low);

	cmd = HCLGE_OPC_RX_COM_THRD_ALLOC;
	hclge_cmd_setup_basic_desc(&desc[0], cmd, true);
	desc[0].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[1], cmd, true);
	ret = hclge_cmd_send(&hdev->hw, desc, 2);
	if (ret)
		goto err_qos_cmd_send;

	dev_info(&hdev->pdev->dev, "\n");
	rx_com_thrd = (struct hclge_rx_com_thrd *)desc[0].data;
	for (i = 0; i < HCLGE_TC_NUM_ONE_DESC; i++)
		dev_info(&hdev->pdev->dev,
			 "rx_com_thrd_tc_%d: high: 0x%x, low: 0x%x\n", i,
			 rx_com_thrd->com_thrd[i].high,
			 rx_com_thrd->com_thrd[i].low);

	rx_com_thrd = (struct hclge_rx_com_thrd *)desc[1].data;
	for (i = 0; i < HCLGE_TC_NUM_ONE_DESC; i++)
		dev_info(&hdev->pdev->dev,
			 "rx_com_thrd_tc_%d: high: 0x%x, low: 0x%x\n", i + 4,
			 rx_com_thrd->com_thrd[i].high,
			 rx_com_thrd->com_thrd[i].low);

	cmd = HCLGE_OPC_RX_COM_WL_ALLOC;
	hclge_cmd_setup_basic_desc(desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, desc, 1);
	if (ret)
		goto err_qos_cmd_send;

	rx_com_wl = (struct hclge_rx_com_wl *)desc[0].data;
	dev_info(&hdev->pdev->dev, "\n");
	dev_info(&hdev->pdev->dev, "rx_com_wl: high: 0x%x, low: 0x%x\n",
		 rx_com_wl->com_wl.high, rx_com_wl->com_wl.low);

	cmd = HCLGE_OPC_RX_GBL_PKT_CNT;
	hclge_cmd_setup_basic_desc(desc, cmd, true);
	ret = hclge_cmd_send(&hdev->hw, desc, 1);
	if (ret)
		goto err_qos_cmd_send;

	rx_packet_cnt = (struct hclge_rx_com_wl *)desc[0].data;
	dev_info(&hdev->pdev->dev,
		 "rx_global_packet_cnt: high: 0x%x, low: 0x%x\n",
		 rx_packet_cnt->com_wl.high, rx_packet_cnt->com_wl.low);

	return;

err_qos_cmd_send:
	dev_err(&hdev->pdev->dev,
		"dump qos buf cfg fail(0x%x), status is %d\n", cmd, ret);
}

static void hclge_dbg_dump_mng_table(struct hclge_dev *hdev)
{
	struct hclge_mac_ethertype_idx_rd_cmd *req0;
	char printf_buf[HCLGE_DBG_BUF_LEN];
	struct hclge_desc desc;
	int ret, i;

	dev_info(&hdev->pdev->dev, "mng tab:\n");
	memset(printf_buf, 0, HCLGE_DBG_BUF_LEN);
	strncat(printf_buf,
		"entry|mac_addr         |mask|ether|mask|vlan|mask",
		HCLGE_DBG_BUF_LEN - 1);
	strncat(printf_buf + strlen(printf_buf),
		"|i_map|i_dir|e_type|pf_id|vf_id|q_id|drop\n",
		HCLGE_DBG_BUF_LEN - strlen(printf_buf) - 1);

	dev_info(&hdev->pdev->dev, "%s", printf_buf);

	for (i = 0; i < HCLGE_DBG_MNG_TBL_MAX; i++) {
		hclge_cmd_setup_basic_desc(&desc, HCLGE_MAC_ETHERTYPE_IDX_RD,
					   true);
		req0 = (struct hclge_mac_ethertype_idx_rd_cmd *)&desc.data;
		req0->index = cpu_to_le16(i);

		ret = hclge_cmd_send(&hdev->hw, &desc, 1);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"call hclge_cmd_send fail, ret = %d\n", ret);
			return;
		}

		if (!req0->resp_code)
			continue;

		memset(printf_buf, 0, HCLGE_DBG_BUF_LEN);
		snprintf(printf_buf, HCLGE_DBG_BUF_LEN,
			 "%02u   |%02x:%02x:%02x:%02x:%02x:%02x|",
			 req0->index, req0->mac_add[0], req0->mac_add[1],
			 req0->mac_add[2], req0->mac_add[3], req0->mac_add[4],
			 req0->mac_add[5]);

		snprintf(printf_buf + strlen(printf_buf),
			 HCLGE_DBG_BUF_LEN - strlen(printf_buf),
			 "%x   |%04x |%x   |%04x|%x   |%02x   |%02x   |",
			 !!(req0->flags & HCLGE_DBG_MNG_MAC_MASK_B),
			 req0->ethter_type,
			 !!(req0->flags & HCLGE_DBG_MNG_ETHER_MASK_B),
			 req0->vlan_tag & HCLGE_DBG_MNG_VLAN_TAG,
			 !!(req0->flags & HCLGE_DBG_MNG_VLAN_MASK_B),
			 req0->i_port_bitmap, req0->i_port_direction);

		snprintf(printf_buf + strlen(printf_buf),
			 HCLGE_DBG_BUF_LEN - strlen(printf_buf),
			 "%d     |%d    |%02d   |%04d|%x\n",
			 !!(req0->egress_port & HCLGE_DBG_MNG_E_TYPE_B),
			 req0->egress_port & HCLGE_DBG_MNG_PF_ID,
			 (req0->egress_port >> 3) & HCLGE_DBG_MNG_VF_ID,
			 req0->egress_queue,
			 !!(req0->egress_port & HCLGE_DBG_MNG_DROP_B));

		dev_info(&hdev->pdev->dev, "%s", printf_buf);
	}
}

static void hclge_dbg_fd_tcam_read(struct hclge_dev *hdev, u8 stage,
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
		return;

	dev_info(&hdev->pdev->dev, " read result tcam key %s(%u):\n",
		 sel_x ? "x" : "y", loc);

	req = (u32 *)req1->tcam_data;
	for (i = 0; i < 2; i++)
		dev_info(&hdev->pdev->dev, "%08x\n", *req++);

	req = (u32 *)req2->tcam_data;
	for (i = 0; i < 6; i++)
		dev_info(&hdev->pdev->dev, "%08x\n", *req++);

	req = (u32 *)req3->tcam_data;
	for (i = 0; i < 5; i++)
		dev_info(&hdev->pdev->dev, "%08x\n", *req++);
}

static void hclge_dbg_fd_tcam(struct hclge_dev *hdev)
{
	u32 i;

	for (i = 0; i < hdev->fd_cfg.rule_num[0]; i++) {
		hclge_dbg_fd_tcam_read(hdev, 0, true, i);
		hclge_dbg_fd_tcam_read(hdev, 0, false, i);
	}
}

int hclge_dbg_run_cmd(struct hnae3_handle *handle, char *cmd_buf)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	if (strncmp(cmd_buf, "dump fd tcam", 12) == 0) {
		hclge_dbg_fd_tcam(hdev);
	} else if (strncmp(cmd_buf, "dump tc", 7) == 0) {
		hclge_dbg_dump_tc(hdev);
	} else if (strncmp(cmd_buf, "dump tm", 7) == 0) {
		hclge_dbg_dump_tm(hdev);
	} else if (strncmp(cmd_buf, "dump qos pause cfg", 18) == 0) {
		hclge_dbg_dump_qos_pause_cfg(hdev);
	} else if (strncmp(cmd_buf, "dump qos pri map", 16) == 0) {
		hclge_dbg_dump_qos_pri_map(hdev);
	} else if (strncmp(cmd_buf, "dump qos buf cfg", 16) == 0) {
		hclge_dbg_dump_qos_buf_cfg(hdev);
	} else if (strncmp(cmd_buf, "dump mng tbl", 12) == 0) {
		hclge_dbg_dump_mng_table(hdev);
	} else {
		dev_info(&hdev->pdev->dev, "unknown command\n");
		return -EINVAL;
	}

	return 0;
}
