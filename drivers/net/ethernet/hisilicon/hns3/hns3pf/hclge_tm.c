// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#include <linux/etherdevice.h>

#include "hclge_cmd.h"
#include "hclge_main.h"
#include "hclge_tm.h"

enum hclge_shaper_level {
	HCLGE_SHAPER_LVL_PRI	= 0,
	HCLGE_SHAPER_LVL_PG	= 1,
	HCLGE_SHAPER_LVL_PORT	= 2,
	HCLGE_SHAPER_LVL_QSET	= 3,
	HCLGE_SHAPER_LVL_CNT	= 4,
	HCLGE_SHAPER_LVL_VF	= 0,
	HCLGE_SHAPER_LVL_PF	= 1,
};

#define HCLGE_TM_PFC_PKT_GET_CMD_NUM	3
#define HCLGE_TM_PFC_NUM_GET_PER_CMD	3

#define HCLGE_SHAPER_BS_U_DEF	5
#define HCLGE_SHAPER_BS_S_DEF	20

/* hclge_shaper_para_calc: calculate ir parameter for the shaper
 * @ir: Rate to be config, its unit is Mbps
 * @shaper_level: the shaper level. eg: port, pg, priority, queueset
 * @ir_para: parameters of IR shaper
 * @max_tm_rate: max tm rate is available to config
 *
 * the formula:
 *
 *		IR_b * (2 ^ IR_u) * 8
 * IR(Mbps) = -------------------------  *  CLOCK(1000Mbps)
 *		Tick * (2 ^ IR_s)
 *
 * @return: 0: calculate sucessful, negative: fail
 */
static int hclge_shaper_para_calc(u32 ir, u8 shaper_level,
				  struct hclge_shaper_ir_para *ir_para,
				  u32 max_tm_rate)
{
#define DEFAULT_SHAPER_IR_B	126
#define DIVISOR_CLK		(1000 * 8)
#define DEFAULT_DIVISOR_IR_B	(DEFAULT_SHAPER_IR_B * DIVISOR_CLK)

	static const u16 tick_array[HCLGE_SHAPER_LVL_CNT] = {
		6 * 256,        /* Prioriy level */
		6 * 32,         /* Prioriy group level */
		6 * 8,          /* Port level */
		6 * 256         /* Qset level */
	};
	u8 ir_u_calc = 0;
	u8 ir_s_calc = 0;
	u32 ir_calc;
	u32 tick;

	/* Calc tick */
	if (shaper_level >= HCLGE_SHAPER_LVL_CNT ||
	    ir > max_tm_rate)
		return -EINVAL;

	tick = tick_array[shaper_level];

	/**
	 * Calc the speed if ir_b = 126, ir_u = 0 and ir_s = 0
	 * the formula is changed to:
	 *		126 * 1 * 8
	 * ir_calc = ---------------- * 1000
	 *		tick * 1
	 */
	ir_calc = (DEFAULT_DIVISOR_IR_B + (tick >> 1) - 1) / tick;

	if (ir_calc == ir) {
		ir_para->ir_b = DEFAULT_SHAPER_IR_B;
		ir_para->ir_u = 0;
		ir_para->ir_s = 0;

		return 0;
	} else if (ir_calc > ir) {
		/* Increasing the denominator to select ir_s value */
		while (ir_calc >= ir && ir) {
			ir_s_calc++;
			ir_calc = DEFAULT_DIVISOR_IR_B /
				  (tick * (1 << ir_s_calc));
		}

		ir_para->ir_b = (ir * tick * (1 << ir_s_calc) +
				(DIVISOR_CLK >> 1)) / DIVISOR_CLK;
	} else {
		/* Increasing the numerator to select ir_u value */
		u32 numerator;

		while (ir_calc < ir) {
			ir_u_calc++;
			numerator = DEFAULT_DIVISOR_IR_B * (1 << ir_u_calc);
			ir_calc = (numerator + (tick >> 1)) / tick;
		}

		if (ir_calc == ir) {
			ir_para->ir_b = DEFAULT_SHAPER_IR_B;
		} else {
			u32 denominator = DIVISOR_CLK * (1 << --ir_u_calc);
			ir_para->ir_b = (ir * tick + (denominator >> 1)) /
					denominator;
		}
	}

	ir_para->ir_u = ir_u_calc;
	ir_para->ir_s = ir_s_calc;

	return 0;
}

static const u16 hclge_pfc_tx_stats_offset[] = {
	HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri0_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri1_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri2_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri3_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri4_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri5_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri6_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_tx_pfc_pri7_pkt_num)
};

static const u16 hclge_pfc_rx_stats_offset[] = {
	HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri0_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri1_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri2_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri3_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri4_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri5_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri6_pkt_num),
	HCLGE_MAC_STATS_FIELD_OFF(mac_rx_pfc_pri7_pkt_num)
};

static void hclge_pfc_stats_get(struct hclge_dev *hdev, bool tx, u64 *stats)
{
	const u16 *offset;
	int i;

	if (tx)
		offset = hclge_pfc_tx_stats_offset;
	else
		offset = hclge_pfc_rx_stats_offset;

	for (i = 0; i < HCLGE_MAX_TC_NUM; i++)
		stats[i] = HCLGE_STATS_READ(&hdev->mac_stats, offset[i]);
}

void hclge_pfc_rx_stats_get(struct hclge_dev *hdev, u64 *stats)
{
	hclge_pfc_stats_get(hdev, false, stats);
}

void hclge_pfc_tx_stats_get(struct hclge_dev *hdev, u64 *stats)
{
	hclge_pfc_stats_get(hdev, true, stats);
}

int hclge_mac_pause_en_cfg(struct hclge_dev *hdev, bool tx, bool rx)
{
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_MAC_PAUSE_EN, false);

	desc.data[0] = cpu_to_le32((tx ? HCLGE_TX_MAC_PAUSE_EN_MSK : 0) |
		(rx ? HCLGE_RX_MAC_PAUSE_EN_MSK : 0));

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_pfc_pause_en_cfg(struct hclge_dev *hdev, u8 tx_rx_bitmap,
				  u8 pfc_bitmap)
{
	struct hclge_desc desc;
	struct hclge_pfc_en_cmd *pfc = (struct hclge_pfc_en_cmd *)desc.data;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_PFC_PAUSE_EN, false);

	pfc->tx_rx_en_bitmap = tx_rx_bitmap;
	pfc->pri_en_bitmap = pfc_bitmap;

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_pause_param_cfg(struct hclge_dev *hdev, const u8 *addr,
				 u8 pause_trans_gap, u16 pause_trans_time)
{
	struct hclge_cfg_pause_param_cmd *pause_param;
	struct hclge_desc desc;

	pause_param = (struct hclge_cfg_pause_param_cmd *)desc.data;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_MAC_PARA, false);

	ether_addr_copy(pause_param->mac_addr, addr);
	ether_addr_copy(pause_param->mac_addr_extra, addr);
	pause_param->pause_trans_gap = pause_trans_gap;
	pause_param->pause_trans_time = cpu_to_le16(pause_trans_time);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

int hclge_pause_addr_cfg(struct hclge_dev *hdev, const u8 *mac_addr)
{
	struct hclge_cfg_pause_param_cmd *pause_param;
	struct hclge_desc desc;
	u16 trans_time;
	u8 trans_gap;
	int ret;

	pause_param = (struct hclge_cfg_pause_param_cmd *)desc.data;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_MAC_PARA, true);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		return ret;

	trans_gap = pause_param->pause_trans_gap;
	trans_time = le16_to_cpu(pause_param->pause_trans_time);

	return hclge_pause_param_cfg(hdev, mac_addr, trans_gap, trans_time);
}

static int hclge_fill_pri_array(struct hclge_dev *hdev, u8 *pri, u8 pri_id)
{
	u8 tc;

	tc = hdev->tm_info.prio_tc[pri_id];

	if (tc >= hdev->tm_info.num_tc)
		return -EINVAL;

	/**
	 * the register for priority has four bytes, the first bytes includes
	 *  priority0 and priority1, the higher 4bit stands for priority1
	 *  while the lower 4bit stands for priority0, as below:
	 * first byte:	| pri_1 | pri_0 |
	 * second byte:	| pri_3 | pri_2 |
	 * third byte:	| pri_5 | pri_4 |
	 * fourth byte:	| pri_7 | pri_6 |
	 */
	pri[pri_id >> 1] |= tc << ((pri_id & 1) * 4);

	return 0;
}

static int hclge_up_to_tc_map(struct hclge_dev *hdev)
{
	struct hclge_desc desc;
	u8 *pri = (u8 *)desc.data;
	u8 pri_id;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_PRI_TO_TC_MAPPING, false);

	for (pri_id = 0; pri_id < HNAE3_MAX_USER_PRIO; pri_id++) {
		ret = hclge_fill_pri_array(hdev, pri, pri_id);
		if (ret)
			return ret;
	}

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_pg_to_pri_map_cfg(struct hclge_dev *hdev,
				      u8 pg_id, u8 pri_bit_map)
{
	struct hclge_pg_to_pri_link_cmd *map;
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PG_TO_PRI_LINK, false);

	map = (struct hclge_pg_to_pri_link_cmd *)desc.data;

	map->pg_id = pg_id;
	map->pri_bit_map = pri_bit_map;

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_qs_to_pri_map_cfg(struct hclge_dev *hdev,
				      u16 qs_id, u8 pri)
{
	struct hclge_qs_to_pri_link_cmd *map;
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_QS_TO_PRI_LINK, false);

	map = (struct hclge_qs_to_pri_link_cmd *)desc.data;

	map->qs_id = cpu_to_le16(qs_id);
	map->priority = pri;
	map->link_vld = HCLGE_TM_QS_PRI_LINK_VLD_MSK;

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_q_to_qs_map_cfg(struct hclge_dev *hdev,
				    u16 q_id, u16 qs_id)
{
	struct hclge_nq_to_qs_link_cmd *map;
	struct hclge_desc desc;
	u16 qs_id_l;
	u16 qs_id_h;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_NQ_TO_QS_LINK, false);

	map = (struct hclge_nq_to_qs_link_cmd *)desc.data;

	map->nq_id = cpu_to_le16(q_id);

	/* convert qs_id to the following format to support qset_id >= 1024
	 * qs_id: | 15 | 14 ~ 10 |  9 ~ 0   |
	 *            /         / \         \
	 *           /         /   \         \
	 * qset_id: | 15 ~ 11 |  10 |  9 ~ 0  |
	 *          | qs_id_h | vld | qs_id_l |
	 */
	qs_id_l = hnae3_get_field(qs_id, HCLGE_TM_QS_ID_L_MSK,
				  HCLGE_TM_QS_ID_L_S);
	qs_id_h = hnae3_get_field(qs_id, HCLGE_TM_QS_ID_H_MSK,
				  HCLGE_TM_QS_ID_H_S);
	hnae3_set_field(qs_id, HCLGE_TM_QS_ID_L_MSK, HCLGE_TM_QS_ID_L_S,
			qs_id_l);
	hnae3_set_field(qs_id, HCLGE_TM_QS_ID_H_EXT_MSK, HCLGE_TM_QS_ID_H_EXT_S,
			qs_id_h);
	map->qset_id = cpu_to_le16(qs_id | HCLGE_TM_Q_QS_LINK_VLD_MSK);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_pg_weight_cfg(struct hclge_dev *hdev, u8 pg_id,
				  u8 dwrr)
{
	struct hclge_pg_weight_cmd *weight;
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PG_WEIGHT, false);

	weight = (struct hclge_pg_weight_cmd *)desc.data;

	weight->pg_id = pg_id;
	weight->dwrr = dwrr;

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_pri_weight_cfg(struct hclge_dev *hdev, u8 pri_id,
				   u8 dwrr)
{
	struct hclge_priority_weight_cmd *weight;
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PRI_WEIGHT, false);

	weight = (struct hclge_priority_weight_cmd *)desc.data;

	weight->pri_id = pri_id;
	weight->dwrr = dwrr;

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_qs_weight_cfg(struct hclge_dev *hdev, u16 qs_id,
				  u8 dwrr)
{
	struct hclge_qs_weight_cmd *weight;
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_QS_WEIGHT, false);

	weight = (struct hclge_qs_weight_cmd *)desc.data;

	weight->qs_id = cpu_to_le16(qs_id);
	weight->dwrr = dwrr;

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static u32 hclge_tm_get_shapping_para(u8 ir_b, u8 ir_u, u8 ir_s,
				      u8 bs_b, u8 bs_s)
{
	u32 shapping_para = 0;

	hclge_tm_set_field(shapping_para, IR_B, ir_b);
	hclge_tm_set_field(shapping_para, IR_U, ir_u);
	hclge_tm_set_field(shapping_para, IR_S, ir_s);
	hclge_tm_set_field(shapping_para, BS_B, bs_b);
	hclge_tm_set_field(shapping_para, BS_S, bs_s);

	return shapping_para;
}

static int hclge_tm_pg_shapping_cfg(struct hclge_dev *hdev,
				    enum hclge_shap_bucket bucket, u8 pg_id,
				    u32 shapping_para, u32 rate)
{
	struct hclge_pg_shapping_cmd *shap_cfg_cmd;
	enum hclge_opcode_type opcode;
	struct hclge_desc desc;

	opcode = bucket ? HCLGE_OPC_TM_PG_P_SHAPPING :
		 HCLGE_OPC_TM_PG_C_SHAPPING;
	hclge_cmd_setup_basic_desc(&desc, opcode, false);

	shap_cfg_cmd = (struct hclge_pg_shapping_cmd *)desc.data;

	shap_cfg_cmd->pg_id = pg_id;

	shap_cfg_cmd->pg_shapping_para = cpu_to_le32(shapping_para);

	hnae3_set_bit(shap_cfg_cmd->flag, HCLGE_TM_RATE_VLD, 1);

	shap_cfg_cmd->pg_rate = cpu_to_le32(rate);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_port_shaper_cfg(struct hclge_dev *hdev)
{
	struct hclge_port_shapping_cmd *shap_cfg_cmd;
	struct hclge_shaper_ir_para ir_para;
	struct hclge_desc desc;
	u32 shapping_para;
	int ret;

	ret = hclge_shaper_para_calc(hdev->hw.mac.speed, HCLGE_SHAPER_LVL_PORT,
				     &ir_para,
				     hdev->ae_dev->dev_specs.max_tm_rate);
	if (ret)
		return ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PORT_SHAPPING, false);
	shap_cfg_cmd = (struct hclge_port_shapping_cmd *)desc.data;

	shapping_para = hclge_tm_get_shapping_para(ir_para.ir_b, ir_para.ir_u,
						   ir_para.ir_s,
						   HCLGE_SHAPER_BS_U_DEF,
						   HCLGE_SHAPER_BS_S_DEF);

	shap_cfg_cmd->port_shapping_para = cpu_to_le32(shapping_para);

	hnae3_set_bit(shap_cfg_cmd->flag, HCLGE_TM_RATE_VLD, 1);

	shap_cfg_cmd->port_rate = cpu_to_le32(hdev->hw.mac.speed);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_pri_shapping_cfg(struct hclge_dev *hdev,
				     enum hclge_shap_bucket bucket, u8 pri_id,
				     u32 shapping_para, u32 rate)
{
	struct hclge_pri_shapping_cmd *shap_cfg_cmd;
	enum hclge_opcode_type opcode;
	struct hclge_desc desc;

	opcode = bucket ? HCLGE_OPC_TM_PRI_P_SHAPPING :
		 HCLGE_OPC_TM_PRI_C_SHAPPING;

	hclge_cmd_setup_basic_desc(&desc, opcode, false);

	shap_cfg_cmd = (struct hclge_pri_shapping_cmd *)desc.data;

	shap_cfg_cmd->pri_id = pri_id;

	shap_cfg_cmd->pri_shapping_para = cpu_to_le32(shapping_para);

	hnae3_set_bit(shap_cfg_cmd->flag, HCLGE_TM_RATE_VLD, 1);

	shap_cfg_cmd->pri_rate = cpu_to_le32(rate);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_pg_schd_mode_cfg(struct hclge_dev *hdev, u8 pg_id)
{
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PG_SCH_MODE_CFG, false);

	if (hdev->tm_info.pg_info[pg_id].pg_sch_mode == HCLGE_SCH_MODE_DWRR)
		desc.data[1] = cpu_to_le32(HCLGE_TM_TX_SCHD_DWRR_MSK);
	else
		desc.data[1] = 0;

	desc.data[0] = cpu_to_le32(pg_id);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_pri_schd_mode_cfg(struct hclge_dev *hdev, u8 pri_id)
{
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PRI_SCH_MODE_CFG, false);

	if (hdev->tm_info.tc_info[pri_id].tc_sch_mode == HCLGE_SCH_MODE_DWRR)
		desc.data[1] = cpu_to_le32(HCLGE_TM_TX_SCHD_DWRR_MSK);
	else
		desc.data[1] = 0;

	desc.data[0] = cpu_to_le32(pri_id);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_qs_schd_mode_cfg(struct hclge_dev *hdev, u16 qs_id, u8 mode)
{
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_QS_SCH_MODE_CFG, false);

	if (mode == HCLGE_SCH_MODE_DWRR)
		desc.data[1] = cpu_to_le32(HCLGE_TM_TX_SCHD_DWRR_MSK);
	else
		desc.data[1] = 0;

	desc.data[0] = cpu_to_le32(qs_id);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_qs_bp_cfg(struct hclge_dev *hdev, u8 tc, u8 grp_id,
			      u32 bit_map)
{
	struct hclge_bp_to_qs_map_cmd *bp_to_qs_map_cmd;
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_BP_TO_QSET_MAPPING,
				   false);

	bp_to_qs_map_cmd = (struct hclge_bp_to_qs_map_cmd *)desc.data;

	bp_to_qs_map_cmd->tc_id = tc;
	bp_to_qs_map_cmd->qs_group_id = grp_id;
	bp_to_qs_map_cmd->qs_bit_map = cpu_to_le32(bit_map);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

int hclge_tm_qs_shaper_cfg(struct hclge_vport *vport, int max_tx_rate)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_qs_shapping_cmd *shap_cfg_cmd;
	struct hclge_shaper_ir_para ir_para;
	struct hclge_dev *hdev = vport->back;
	struct hclge_desc desc;
	u32 shaper_para;
	int ret, i;

	if (!max_tx_rate)
		max_tx_rate = hdev->ae_dev->dev_specs.max_tm_rate;

	ret = hclge_shaper_para_calc(max_tx_rate, HCLGE_SHAPER_LVL_QSET,
				     &ir_para,
				     hdev->ae_dev->dev_specs.max_tm_rate);
	if (ret)
		return ret;

	shaper_para = hclge_tm_get_shapping_para(ir_para.ir_b, ir_para.ir_u,
						 ir_para.ir_s,
						 HCLGE_SHAPER_BS_U_DEF,
						 HCLGE_SHAPER_BS_S_DEF);

	for (i = 0; i < kinfo->tc_info.num_tc; i++) {
		hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QCN_SHAPPING_CFG,
					   false);

		shap_cfg_cmd = (struct hclge_qs_shapping_cmd *)desc.data;
		shap_cfg_cmd->qs_id = cpu_to_le16(vport->qs_offset + i);
		shap_cfg_cmd->qs_shapping_para = cpu_to_le32(shaper_para);

		hnae3_set_bit(shap_cfg_cmd->flag, HCLGE_TM_RATE_VLD, 1);
		shap_cfg_cmd->qs_rate = cpu_to_le32(max_tx_rate);

		ret = hclge_cmd_send(&hdev->hw, &desc, 1);
		if (ret) {
			dev_err(&hdev->pdev->dev,
				"vport%u, qs%u failed to set tx_rate:%d, ret=%d\n",
				vport->vport_id, shap_cfg_cmd->qs_id,
				max_tx_rate, ret);
			return ret;
		}
	}

	return 0;
}

static u16 hclge_vport_get_max_rss_size(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hnae3_tc_info *tc_info = &kinfo->tc_info;
	struct hclge_dev *hdev = vport->back;
	u16 max_rss_size = 0;
	int i;

	if (!tc_info->mqprio_active)
		return vport->alloc_tqps / tc_info->num_tc;

	for (i = 0; i < HNAE3_MAX_TC; i++) {
		if (!(hdev->hw_tc_map & BIT(i)) || i >= tc_info->num_tc)
			continue;
		if (max_rss_size < tc_info->tqp_count[i])
			max_rss_size = tc_info->tqp_count[i];
	}

	return max_rss_size;
}

static u16 hclge_vport_get_tqp_num(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hnae3_tc_info *tc_info = &kinfo->tc_info;
	struct hclge_dev *hdev = vport->back;
	int sum = 0;
	int i;

	if (!tc_info->mqprio_active)
		return kinfo->rss_size * tc_info->num_tc;

	for (i = 0; i < HNAE3_MAX_TC; i++) {
		if (hdev->hw_tc_map & BIT(i) && i < tc_info->num_tc)
			sum += tc_info->tqp_count[i];
	}

	return sum;
}

static void hclge_tm_update_kinfo_rss_size(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	u16 vport_max_rss_size;
	u16 max_rss_size;

	/* TC configuration is shared by PF/VF in one port, only allow
	 * one tc for VF for simplicity. VF's vport_id is non zero.
	 */
	if (vport->vport_id) {
		kinfo->tc_info.num_tc = 1;
		vport->qs_offset = HNAE3_MAX_TC +
				   vport->vport_id - HCLGE_VF_VPORT_START_NUM;
		vport_max_rss_size = hdev->vf_rss_size_max;
	} else {
		kinfo->tc_info.num_tc =
			min_t(u16, vport->alloc_tqps, hdev->tm_info.num_tc);
		vport->qs_offset = 0;
		vport_max_rss_size = hdev->pf_rss_size_max;
	}

	max_rss_size = min_t(u16, vport_max_rss_size,
			     hclge_vport_get_max_rss_size(vport));

	/* Set to user value, no larger than max_rss_size. */
	if (kinfo->req_rss_size != kinfo->rss_size && kinfo->req_rss_size &&
	    kinfo->req_rss_size <= max_rss_size) {
		dev_info(&hdev->pdev->dev, "rss changes from %u to %u\n",
			 kinfo->rss_size, kinfo->req_rss_size);
		kinfo->rss_size = kinfo->req_rss_size;
	} else if (kinfo->rss_size > max_rss_size ||
		   (!kinfo->req_rss_size && kinfo->rss_size < max_rss_size)) {
		/* Set to the maximum specification value (max_rss_size). */
		kinfo->rss_size = max_rss_size;
	}
}

static void hclge_tm_vport_tc_info_update(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	u8 i;

	hclge_tm_update_kinfo_rss_size(vport);
	kinfo->num_tqps = hclge_vport_get_tqp_num(vport);
	vport->dwrr = 100;  /* 100 percent as init */
	vport->bw_limit = hdev->tm_info.pg_info[0].bw_limit;
	hdev->rss_cfg.rss_size = kinfo->rss_size;

	/* when enable mqprio, the tc_info has been updated. */
	if (kinfo->tc_info.mqprio_active)
		return;

	for (i = 0; i < HNAE3_MAX_TC; i++) {
		if (hdev->hw_tc_map & BIT(i) && i < kinfo->tc_info.num_tc) {
			kinfo->tc_info.tqp_offset[i] = i * kinfo->rss_size;
			kinfo->tc_info.tqp_count[i] = kinfo->rss_size;
		} else {
			/* Set to default queue if TC is disable */
			kinfo->tc_info.tqp_offset[i] = 0;
			kinfo->tc_info.tqp_count[i] = 1;
		}
	}

	memcpy(kinfo->tc_info.prio_tc, hdev->tm_info.prio_tc,
	       sizeof_field(struct hnae3_tc_info, prio_tc));
}

static void hclge_tm_vport_info_update(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	u32 i;

	for (i = 0; i < hdev->num_alloc_vport; i++) {
		hclge_tm_vport_tc_info_update(vport);

		vport++;
	}
}

static void hclge_tm_tc_info_init(struct hclge_dev *hdev)
{
	u8 i;

	for (i = 0; i < hdev->tm_info.num_tc; i++) {
		hdev->tm_info.tc_info[i].tc_id = i;
		hdev->tm_info.tc_info[i].tc_sch_mode = HCLGE_SCH_MODE_DWRR;
		hdev->tm_info.tc_info[i].pgid = 0;
		hdev->tm_info.tc_info[i].bw_limit =
			hdev->tm_info.pg_info[0].bw_limit;
	}

	for (i = 0; i < HNAE3_MAX_USER_PRIO; i++)
		hdev->tm_info.prio_tc[i] =
			(i >= hdev->tm_info.num_tc) ? 0 : i;
}

static void hclge_tm_pg_info_init(struct hclge_dev *hdev)
{
#define BW_PERCENT	100

	u8 i;

	for (i = 0; i < hdev->tm_info.num_pg; i++) {
		int k;

		hdev->tm_info.pg_dwrr[i] = i ? 0 : BW_PERCENT;

		hdev->tm_info.pg_info[i].pg_id = i;
		hdev->tm_info.pg_info[i].pg_sch_mode = HCLGE_SCH_MODE_DWRR;

		hdev->tm_info.pg_info[i].bw_limit =
					hdev->ae_dev->dev_specs.max_tm_rate;

		if (i != 0)
			continue;

		hdev->tm_info.pg_info[i].tc_bit_map = hdev->hw_tc_map;
		for (k = 0; k < hdev->tm_info.num_tc; k++)
			hdev->tm_info.pg_info[i].tc_dwrr[k] = BW_PERCENT;
		for (; k < HNAE3_MAX_TC; k++)
			hdev->tm_info.pg_info[i].tc_dwrr[k] = 0;
	}
}

static void hclge_update_fc_mode_by_dcb_flag(struct hclge_dev *hdev)
{
	if (hdev->tm_info.num_tc == 1 && !hdev->tm_info.pfc_en) {
		if (hdev->fc_mode_last_time == HCLGE_FC_PFC)
			dev_warn(&hdev->pdev->dev,
				 "Only 1 tc used, but last mode is FC_PFC\n");

		hdev->tm_info.fc_mode = hdev->fc_mode_last_time;
	} else if (hdev->tm_info.fc_mode != HCLGE_FC_PFC) {
		/* fc_mode_last_time record the last fc_mode when
		 * DCB is enabled, so that fc_mode can be set to
		 * the correct value when DCB is disabled.
		 */
		hdev->fc_mode_last_time = hdev->tm_info.fc_mode;
		hdev->tm_info.fc_mode = HCLGE_FC_PFC;
	}
}

static void hclge_update_fc_mode(struct hclge_dev *hdev)
{
	if (!hdev->tm_info.pfc_en) {
		hdev->tm_info.fc_mode = hdev->fc_mode_last_time;
		return;
	}

	if (hdev->tm_info.fc_mode != HCLGE_FC_PFC) {
		hdev->fc_mode_last_time = hdev->tm_info.fc_mode;
		hdev->tm_info.fc_mode = HCLGE_FC_PFC;
	}
}

void hclge_tm_pfc_info_update(struct hclge_dev *hdev)
{
	if (hdev->ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V3)
		hclge_update_fc_mode(hdev);
	else
		hclge_update_fc_mode_by_dcb_flag(hdev);
}

static void hclge_tm_schd_info_init(struct hclge_dev *hdev)
{
	hclge_tm_pg_info_init(hdev);

	hclge_tm_tc_info_init(hdev);

	hclge_tm_vport_info_update(hdev);

	hclge_tm_pfc_info_update(hdev);
}

static int hclge_tm_pg_to_pri_map(struct hclge_dev *hdev)
{
	int ret;
	u32 i;

	if (hdev->tx_sch_mode != HCLGE_FLAG_TC_BASE_SCH_MODE)
		return 0;

	for (i = 0; i < hdev->tm_info.num_pg; i++) {
		/* Cfg mapping */
		ret = hclge_tm_pg_to_pri_map_cfg(
			hdev, i, hdev->tm_info.pg_info[i].tc_bit_map);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_pg_shaper_cfg(struct hclge_dev *hdev)
{
	u32 max_tm_rate = hdev->ae_dev->dev_specs.max_tm_rate;
	struct hclge_shaper_ir_para ir_para;
	u32 shaper_para;
	int ret;
	u32 i;

	/* Cfg pg schd */
	if (hdev->tx_sch_mode != HCLGE_FLAG_TC_BASE_SCH_MODE)
		return 0;

	/* Pg to pri */
	for (i = 0; i < hdev->tm_info.num_pg; i++) {
		u32 rate = hdev->tm_info.pg_info[i].bw_limit;

		/* Calc shaper para */
		ret = hclge_shaper_para_calc(rate, HCLGE_SHAPER_LVL_PG,
					     &ir_para, max_tm_rate);
		if (ret)
			return ret;

		shaper_para = hclge_tm_get_shapping_para(0, 0, 0,
							 HCLGE_SHAPER_BS_U_DEF,
							 HCLGE_SHAPER_BS_S_DEF);
		ret = hclge_tm_pg_shapping_cfg(hdev,
					       HCLGE_TM_SHAP_C_BUCKET, i,
					       shaper_para, rate);
		if (ret)
			return ret;

		shaper_para = hclge_tm_get_shapping_para(ir_para.ir_b,
							 ir_para.ir_u,
							 ir_para.ir_s,
							 HCLGE_SHAPER_BS_U_DEF,
							 HCLGE_SHAPER_BS_S_DEF);
		ret = hclge_tm_pg_shapping_cfg(hdev,
					       HCLGE_TM_SHAP_P_BUCKET, i,
					       shaper_para, rate);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_pg_dwrr_cfg(struct hclge_dev *hdev)
{
	int ret;
	u32 i;

	/* cfg pg schd */
	if (hdev->tx_sch_mode != HCLGE_FLAG_TC_BASE_SCH_MODE)
		return 0;

	/* pg to prio */
	for (i = 0; i < hdev->tm_info.num_pg; i++) {
		/* Cfg dwrr */
		ret = hclge_tm_pg_weight_cfg(hdev, i, hdev->tm_info.pg_dwrr[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_vport_q_to_qs_map(struct hclge_dev *hdev,
				   struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hnae3_tc_info *tc_info = &kinfo->tc_info;
	struct hnae3_queue **tqp = kinfo->tqp;
	u32 i, j;
	int ret;

	for (i = 0; i < tc_info->num_tc; i++) {
		for (j = 0; j < tc_info->tqp_count[i]; j++) {
			struct hnae3_queue *q = tqp[tc_info->tqp_offset[i] + j];

			ret = hclge_tm_q_to_qs_map_cfg(hdev,
						       hclge_get_queue_id(q),
						       vport->qs_offset + i);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int hclge_tm_pri_q_qs_cfg_tc_base(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	u16 i, k;
	int ret;

	/* Cfg qs -> pri mapping, one by one mapping */
	for (k = 0; k < hdev->num_alloc_vport; k++) {
		struct hnae3_knic_private_info *kinfo = &vport[k].nic.kinfo;

		for (i = 0; i < kinfo->tc_info.num_tc; i++) {
			ret = hclge_tm_qs_to_pri_map_cfg(hdev,
							 vport[k].qs_offset + i,
							 i);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int hclge_tm_pri_q_qs_cfg_vnet_base(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	u16 i, k;
	int ret;

	/* Cfg qs -> pri mapping,  qs = tc, pri = vf, 8 qs -> 1 pri */
	for (k = 0; k < hdev->num_alloc_vport; k++)
		for (i = 0; i < HNAE3_MAX_TC; i++) {
			ret = hclge_tm_qs_to_pri_map_cfg(hdev,
							 vport[k].qs_offset + i,
							 k);
			if (ret)
				return ret;
		}

	return 0;
}

static int hclge_tm_pri_q_qs_cfg(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	int ret;
	u32 i;

	if (hdev->tx_sch_mode == HCLGE_FLAG_TC_BASE_SCH_MODE)
		ret = hclge_tm_pri_q_qs_cfg_tc_base(hdev);
	else if (hdev->tx_sch_mode == HCLGE_FLAG_VNET_BASE_SCH_MODE)
		ret = hclge_tm_pri_q_qs_cfg_vnet_base(hdev);
	else
		return -EINVAL;

	if (ret)
		return ret;

	/* Cfg q -> qs mapping */
	for (i = 0; i < hdev->num_alloc_vport; i++) {
		ret = hclge_vport_q_to_qs_map(hdev, vport);
		if (ret)
			return ret;

		vport++;
	}

	return 0;
}

static int hclge_tm_pri_tc_base_shaper_cfg(struct hclge_dev *hdev)
{
	u32 max_tm_rate = hdev->ae_dev->dev_specs.max_tm_rate;
	struct hclge_shaper_ir_para ir_para;
	u32 shaper_para;
	int ret;
	u32 i;

	for (i = 0; i < hdev->tm_info.num_tc; i++) {
		u32 rate = hdev->tm_info.tc_info[i].bw_limit;

		ret = hclge_shaper_para_calc(rate, HCLGE_SHAPER_LVL_PRI,
					     &ir_para, max_tm_rate);
		if (ret)
			return ret;

		shaper_para = hclge_tm_get_shapping_para(0, 0, 0,
							 HCLGE_SHAPER_BS_U_DEF,
							 HCLGE_SHAPER_BS_S_DEF);
		ret = hclge_tm_pri_shapping_cfg(hdev, HCLGE_TM_SHAP_C_BUCKET, i,
						shaper_para, rate);
		if (ret)
			return ret;

		shaper_para = hclge_tm_get_shapping_para(ir_para.ir_b,
							 ir_para.ir_u,
							 ir_para.ir_s,
							 HCLGE_SHAPER_BS_U_DEF,
							 HCLGE_SHAPER_BS_S_DEF);
		ret = hclge_tm_pri_shapping_cfg(hdev, HCLGE_TM_SHAP_P_BUCKET, i,
						shaper_para, rate);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_pri_vnet_base_shaper_pri_cfg(struct hclge_vport *vport)
{
	struct hclge_dev *hdev = vport->back;
	struct hclge_shaper_ir_para ir_para;
	u32 shaper_para;
	int ret;

	ret = hclge_shaper_para_calc(vport->bw_limit, HCLGE_SHAPER_LVL_VF,
				     &ir_para,
				     hdev->ae_dev->dev_specs.max_tm_rate);
	if (ret)
		return ret;

	shaper_para = hclge_tm_get_shapping_para(0, 0, 0,
						 HCLGE_SHAPER_BS_U_DEF,
						 HCLGE_SHAPER_BS_S_DEF);
	ret = hclge_tm_pri_shapping_cfg(hdev, HCLGE_TM_SHAP_C_BUCKET,
					vport->vport_id, shaper_para,
					vport->bw_limit);
	if (ret)
		return ret;

	shaper_para = hclge_tm_get_shapping_para(ir_para.ir_b, ir_para.ir_u,
						 ir_para.ir_s,
						 HCLGE_SHAPER_BS_U_DEF,
						 HCLGE_SHAPER_BS_S_DEF);
	ret = hclge_tm_pri_shapping_cfg(hdev, HCLGE_TM_SHAP_P_BUCKET,
					vport->vport_id, shaper_para,
					vport->bw_limit);
	if (ret)
		return ret;

	return 0;
}

static int hclge_tm_pri_vnet_base_shaper_qs_cfg(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	u32 max_tm_rate = hdev->ae_dev->dev_specs.max_tm_rate;
	struct hclge_shaper_ir_para ir_para;
	u32 i;
	int ret;

	for (i = 0; i < kinfo->tc_info.num_tc; i++) {
		ret = hclge_shaper_para_calc(hdev->tm_info.tc_info[i].bw_limit,
					     HCLGE_SHAPER_LVL_QSET,
					     &ir_para, max_tm_rate);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_pri_vnet_base_shaper_cfg(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	int ret;
	u32 i;

	/* Need config vport shaper */
	for (i = 0; i < hdev->num_alloc_vport; i++) {
		ret = hclge_tm_pri_vnet_base_shaper_pri_cfg(vport);
		if (ret)
			return ret;

		ret = hclge_tm_pri_vnet_base_shaper_qs_cfg(vport);
		if (ret)
			return ret;

		vport++;
	}

	return 0;
}

static int hclge_tm_pri_shaper_cfg(struct hclge_dev *hdev)
{
	int ret;

	if (hdev->tx_sch_mode == HCLGE_FLAG_TC_BASE_SCH_MODE) {
		ret = hclge_tm_pri_tc_base_shaper_cfg(hdev);
		if (ret)
			return ret;
	} else {
		ret = hclge_tm_pri_vnet_base_shaper_cfg(hdev);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_pri_tc_base_dwrr_cfg(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	struct hclge_pg_info *pg_info;
	u8 dwrr;
	int ret;
	u32 i, k;

	for (i = 0; i < hdev->tm_info.num_tc; i++) {
		pg_info =
			&hdev->tm_info.pg_info[hdev->tm_info.tc_info[i].pgid];
		dwrr = pg_info->tc_dwrr[i];

		ret = hclge_tm_pri_weight_cfg(hdev, i, dwrr);
		if (ret)
			return ret;

		for (k = 0; k < hdev->num_alloc_vport; k++) {
			ret = hclge_tm_qs_weight_cfg(
				hdev, vport[k].qs_offset + i,
				vport[k].dwrr);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int hclge_tm_ets_tc_dwrr_cfg(struct hclge_dev *hdev)
{
#define DEFAULT_TC_OFFSET	14

	struct hclge_ets_tc_weight_cmd *ets_weight;
	struct hclge_desc desc;
	unsigned int i;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_ETS_TC_WEIGHT, false);
	ets_weight = (struct hclge_ets_tc_weight_cmd *)desc.data;

	for (i = 0; i < HNAE3_MAX_TC; i++) {
		struct hclge_pg_info *pg_info;

		pg_info = &hdev->tm_info.pg_info[hdev->tm_info.tc_info[i].pgid];
		ets_weight->tc_weight[i] = pg_info->tc_dwrr[i];
	}

	ets_weight->weight_offset = DEFAULT_TC_OFFSET;

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_pri_vnet_base_dwrr_pri_cfg(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	int ret;
	u8 i;

	/* Vf dwrr */
	ret = hclge_tm_pri_weight_cfg(hdev, vport->vport_id, vport->dwrr);
	if (ret)
		return ret;

	/* Qset dwrr */
	for (i = 0; i < kinfo->tc_info.num_tc; i++) {
		ret = hclge_tm_qs_weight_cfg(
			hdev, vport->qs_offset + i,
			hdev->tm_info.pg_info[0].tc_dwrr[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_pri_vnet_base_dwrr_cfg(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	int ret;
	u32 i;

	for (i = 0; i < hdev->num_alloc_vport; i++) {
		ret = hclge_tm_pri_vnet_base_dwrr_pri_cfg(vport);
		if (ret)
			return ret;

		vport++;
	}

	return 0;
}

static int hclge_tm_pri_dwrr_cfg(struct hclge_dev *hdev)
{
	int ret;

	if (hdev->tx_sch_mode == HCLGE_FLAG_TC_BASE_SCH_MODE) {
		ret = hclge_tm_pri_tc_base_dwrr_cfg(hdev);
		if (ret)
			return ret;

		if (!hnae3_dev_dcb_supported(hdev))
			return 0;

		ret = hclge_tm_ets_tc_dwrr_cfg(hdev);
		if (ret == -EOPNOTSUPP) {
			dev_warn(&hdev->pdev->dev,
				 "fw %08x does't support ets tc weight cmd\n",
				 hdev->fw_version);
			ret = 0;
		}

		return ret;
	} else {
		ret = hclge_tm_pri_vnet_base_dwrr_cfg(hdev);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_map_cfg(struct hclge_dev *hdev)
{
	int ret;

	ret = hclge_up_to_tc_map(hdev);
	if (ret)
		return ret;

	ret = hclge_tm_pg_to_pri_map(hdev);
	if (ret)
		return ret;

	return hclge_tm_pri_q_qs_cfg(hdev);
}

static int hclge_tm_shaper_cfg(struct hclge_dev *hdev)
{
	int ret;

	ret = hclge_tm_port_shaper_cfg(hdev);
	if (ret)
		return ret;

	ret = hclge_tm_pg_shaper_cfg(hdev);
	if (ret)
		return ret;

	return hclge_tm_pri_shaper_cfg(hdev);
}

int hclge_tm_dwrr_cfg(struct hclge_dev *hdev)
{
	int ret;

	ret = hclge_tm_pg_dwrr_cfg(hdev);
	if (ret)
		return ret;

	return hclge_tm_pri_dwrr_cfg(hdev);
}

static int hclge_tm_lvl2_schd_mode_cfg(struct hclge_dev *hdev)
{
	int ret;
	u8 i;

	/* Only being config on TC-Based scheduler mode */
	if (hdev->tx_sch_mode == HCLGE_FLAG_VNET_BASE_SCH_MODE)
		return 0;

	for (i = 0; i < hdev->tm_info.num_pg; i++) {
		ret = hclge_tm_pg_schd_mode_cfg(hdev, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_schd_mode_tc_base_cfg(struct hclge_dev *hdev, u8 pri_id)
{
	struct hclge_vport *vport = hdev->vport;
	int ret;
	u16 i;

	ret = hclge_tm_pri_schd_mode_cfg(hdev, pri_id);
	if (ret)
		return ret;

	for (i = 0; i < hdev->num_alloc_vport; i++) {
		ret = hclge_tm_qs_schd_mode_cfg(hdev,
						vport[i].qs_offset + pri_id,
						HCLGE_SCH_MODE_DWRR);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_schd_mode_vnet_base_cfg(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	int ret;
	u8 i;

	if (vport->vport_id >= HNAE3_MAX_TC)
		return -EINVAL;

	ret = hclge_tm_pri_schd_mode_cfg(hdev, vport->vport_id);
	if (ret)
		return ret;

	for (i = 0; i < kinfo->tc_info.num_tc; i++) {
		u8 sch_mode = hdev->tm_info.tc_info[i].tc_sch_mode;

		ret = hclge_tm_qs_schd_mode_cfg(hdev, vport->qs_offset + i,
						sch_mode);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_lvl34_schd_mode_cfg(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	int ret;
	u8 i;

	if (hdev->tx_sch_mode == HCLGE_FLAG_TC_BASE_SCH_MODE) {
		for (i = 0; i < hdev->tm_info.num_tc; i++) {
			ret = hclge_tm_schd_mode_tc_base_cfg(hdev, i);
			if (ret)
				return ret;
		}
	} else {
		for (i = 0; i < hdev->num_alloc_vport; i++) {
			ret = hclge_tm_schd_mode_vnet_base_cfg(vport);
			if (ret)
				return ret;

			vport++;
		}
	}

	return 0;
}

static int hclge_tm_schd_mode_hw(struct hclge_dev *hdev)
{
	int ret;

	ret = hclge_tm_lvl2_schd_mode_cfg(hdev);
	if (ret)
		return ret;

	return hclge_tm_lvl34_schd_mode_cfg(hdev);
}

int hclge_tm_schd_setup_hw(struct hclge_dev *hdev)
{
	int ret;

	/* Cfg tm mapping  */
	ret = hclge_tm_map_cfg(hdev);
	if (ret)
		return ret;

	/* Cfg tm shaper */
	ret = hclge_tm_shaper_cfg(hdev);
	if (ret)
		return ret;

	/* Cfg dwrr */
	ret = hclge_tm_dwrr_cfg(hdev);
	if (ret)
		return ret;

	/* Cfg schd mode for each level schd */
	return hclge_tm_schd_mode_hw(hdev);
}

static int hclge_pause_param_setup_hw(struct hclge_dev *hdev)
{
	struct hclge_mac *mac = &hdev->hw.mac;

	return hclge_pause_param_cfg(hdev, mac->mac_addr,
				     HCLGE_DEFAULT_PAUSE_TRANS_GAP,
				     HCLGE_DEFAULT_PAUSE_TRANS_TIME);
}

static int hclge_pfc_setup_hw(struct hclge_dev *hdev)
{
	u8 enable_bitmap = 0;

	if (hdev->tm_info.fc_mode == HCLGE_FC_PFC)
		enable_bitmap = HCLGE_TX_MAC_PAUSE_EN_MSK |
				HCLGE_RX_MAC_PAUSE_EN_MSK;

	return hclge_pfc_pause_en_cfg(hdev, enable_bitmap,
				      hdev->tm_info.pfc_en);
}

/* for the queues that use for backpress, divides to several groups,
 * each group contains 32 queue sets, which can be represented by u32 bitmap.
 */
static int hclge_bp_setup_hw(struct hclge_dev *hdev, u8 tc)
{
	u16 grp_id_shift = HCLGE_BP_GRP_ID_S;
	u16 grp_id_mask = HCLGE_BP_GRP_ID_M;
	u8 grp_num = HCLGE_BP_GRP_NUM;
	int i;

	if (hdev->num_tqps > HCLGE_TQP_MAX_SIZE_DEV_V2) {
		grp_num = HCLGE_BP_EXT_GRP_NUM;
		grp_id_mask = HCLGE_BP_EXT_GRP_ID_M;
		grp_id_shift = HCLGE_BP_EXT_GRP_ID_S;
	}

	for (i = 0; i < grp_num; i++) {
		u32 qs_bitmap = 0;
		int k, ret;

		for (k = 0; k < hdev->num_alloc_vport; k++) {
			struct hclge_vport *vport = &hdev->vport[k];
			u16 qs_id = vport->qs_offset + tc;
			u8 grp, sub_grp;

			grp = hnae3_get_field(qs_id, grp_id_mask, grp_id_shift);
			sub_grp = hnae3_get_field(qs_id, HCLGE_BP_SUB_GRP_ID_M,
						  HCLGE_BP_SUB_GRP_ID_S);
			if (i == grp)
				qs_bitmap |= (1 << sub_grp);
		}

		ret = hclge_tm_qs_bp_cfg(hdev, tc, i, qs_bitmap);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_mac_pause_setup_hw(struct hclge_dev *hdev)
{
	bool tx_en, rx_en;

	switch (hdev->tm_info.fc_mode) {
	case HCLGE_FC_NONE:
		tx_en = false;
		rx_en = false;
		break;
	case HCLGE_FC_RX_PAUSE:
		tx_en = false;
		rx_en = true;
		break;
	case HCLGE_FC_TX_PAUSE:
		tx_en = true;
		rx_en = false;
		break;
	case HCLGE_FC_FULL:
		tx_en = true;
		rx_en = true;
		break;
	case HCLGE_FC_PFC:
		tx_en = false;
		rx_en = false;
		break;
	default:
		tx_en = true;
		rx_en = true;
	}

	return hclge_mac_pause_en_cfg(hdev, tx_en, rx_en);
}

static int hclge_tm_bp_setup(struct hclge_dev *hdev)
{
	int ret;
	int i;

	for (i = 0; i < hdev->tm_info.num_tc; i++) {
		ret = hclge_bp_setup_hw(hdev, i);
		if (ret)
			return ret;
	}

	return 0;
}

int hclge_pause_setup_hw(struct hclge_dev *hdev, bool init)
{
	int ret;

	ret = hclge_pause_param_setup_hw(hdev);
	if (ret)
		return ret;

	ret = hclge_mac_pause_setup_hw(hdev);
	if (ret)
		return ret;

	/* Only DCB-supported dev supports qset back pressure and pfc cmd */
	if (!hnae3_dev_dcb_supported(hdev))
		return 0;

	/* GE MAC does not support PFC, when driver is initializing and MAC
	 * is in GE Mode, ignore the error here, otherwise initialization
	 * will fail.
	 */
	ret = hclge_pfc_setup_hw(hdev);
	if (init && ret == -EOPNOTSUPP)
		dev_warn(&hdev->pdev->dev, "GE MAC does not support pfc\n");
	else if (ret) {
		dev_err(&hdev->pdev->dev, "config pfc failed! ret = %d\n",
			ret);
		return ret;
	}

	return hclge_tm_bp_setup(hdev);
}

void hclge_tm_prio_tc_info_update(struct hclge_dev *hdev, u8 *prio_tc)
{
	struct hclge_vport *vport = hdev->vport;
	struct hnae3_knic_private_info *kinfo;
	u32 i, k;

	for (i = 0; i < HNAE3_MAX_USER_PRIO; i++) {
		hdev->tm_info.prio_tc[i] = prio_tc[i];

		for (k = 0;  k < hdev->num_alloc_vport; k++) {
			kinfo = &vport[k].nic.kinfo;
			kinfo->tc_info.prio_tc[i] = prio_tc[i];
		}
	}
}

void hclge_tm_schd_info_update(struct hclge_dev *hdev, u8 num_tc)
{
	u8 bit_map = 0;
	u8 i;

	hdev->tm_info.num_tc = num_tc;

	for (i = 0; i < hdev->tm_info.num_tc; i++)
		bit_map |= BIT(i);

	if (!bit_map) {
		bit_map = 1;
		hdev->tm_info.num_tc = 1;
	}

	hdev->hw_tc_map = bit_map;

	hclge_tm_schd_info_init(hdev);
}

int hclge_tm_init_hw(struct hclge_dev *hdev, bool init)
{
	int ret;

	if ((hdev->tx_sch_mode != HCLGE_FLAG_TC_BASE_SCH_MODE) &&
	    (hdev->tx_sch_mode != HCLGE_FLAG_VNET_BASE_SCH_MODE))
		return -ENOTSUPP;

	ret = hclge_tm_schd_setup_hw(hdev);
	if (ret)
		return ret;

	ret = hclge_pause_setup_hw(hdev, init);
	if (ret)
		return ret;

	return 0;
}

int hclge_tm_schd_init(struct hclge_dev *hdev)
{
	/* fc_mode is HCLGE_FC_FULL on reset */
	hdev->tm_info.fc_mode = HCLGE_FC_FULL;
	hdev->fc_mode_last_time = hdev->tm_info.fc_mode;

	if (hdev->tx_sch_mode != HCLGE_FLAG_TC_BASE_SCH_MODE &&
	    hdev->tm_info.num_pg != 1)
		return -EINVAL;

	hclge_tm_schd_info_init(hdev);

	return hclge_tm_init_hw(hdev, true);
}

int hclge_tm_vport_map_update(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	int ret;

	hclge_tm_vport_tc_info_update(vport);

	ret = hclge_vport_q_to_qs_map(hdev, vport);
	if (ret)
		return ret;

	if (hdev->tm_info.num_tc == 1 && !hdev->tm_info.pfc_en)
		return 0;

	return hclge_tm_bp_setup(hdev);
}

int hclge_tm_get_qset_num(struct hclge_dev *hdev, u16 *qset_num)
{
	struct hclge_tm_nodes_cmd *nodes;
	struct hclge_desc desc;
	int ret;

	if (hdev->ae_dev->dev_version <= HNAE3_DEVICE_VERSION_V2) {
		/* Each PF has 8 qsets and each VF has 1 qset */
		*qset_num = HCLGE_TM_PF_MAX_QSET_NUM + pci_num_vf(hdev->pdev);
		return 0;
	}

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_NODES, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get qset num, ret = %d\n", ret);
		return ret;
	}

	nodes = (struct hclge_tm_nodes_cmd *)desc.data;
	*qset_num = le16_to_cpu(nodes->qset_num);
	return 0;
}

int hclge_tm_get_pri_num(struct hclge_dev *hdev, u8 *pri_num)
{
	struct hclge_tm_nodes_cmd *nodes;
	struct hclge_desc desc;
	int ret;

	if (hdev->ae_dev->dev_version <= HNAE3_DEVICE_VERSION_V2) {
		*pri_num = HCLGE_TM_PF_MAX_PRI_NUM;
		return 0;
	}

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_NODES, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get pri num, ret = %d\n", ret);
		return ret;
	}

	nodes = (struct hclge_tm_nodes_cmd *)desc.data;
	*pri_num = nodes->pri_num;
	return 0;
}

int hclge_tm_get_qset_map_pri(struct hclge_dev *hdev, u16 qset_id, u8 *priority,
			      u8 *link_vld)
{
	struct hclge_qs_to_pri_link_cmd *map;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_QS_TO_PRI_LINK, true);
	map = (struct hclge_qs_to_pri_link_cmd *)desc.data;
	map->qs_id = cpu_to_le16(qset_id);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get qset map priority, ret = %d\n", ret);
		return ret;
	}

	*priority = map->priority;
	*link_vld = map->link_vld;
	return 0;
}

int hclge_tm_get_qset_sch_mode(struct hclge_dev *hdev, u16 qset_id, u8 *mode)
{
	struct hclge_qs_sch_mode_cfg_cmd *qs_sch_mode;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_QS_SCH_MODE_CFG, true);
	qs_sch_mode = (struct hclge_qs_sch_mode_cfg_cmd *)desc.data;
	qs_sch_mode->qs_id = cpu_to_le16(qset_id);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get qset sch mode, ret = %d\n", ret);
		return ret;
	}

	*mode = qs_sch_mode->sch_mode;
	return 0;
}

int hclge_tm_get_qset_weight(struct hclge_dev *hdev, u16 qset_id, u8 *weight)
{
	struct hclge_qs_weight_cmd *qs_weight;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_QS_WEIGHT, true);
	qs_weight = (struct hclge_qs_weight_cmd *)desc.data;
	qs_weight->qs_id = cpu_to_le16(qset_id);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get qset weight, ret = %d\n", ret);
		return ret;
	}

	*weight = qs_weight->dwrr;
	return 0;
}

int hclge_tm_get_qset_shaper(struct hclge_dev *hdev, u16 qset_id,
			     struct hclge_tm_shaper_para *para)
{
	struct hclge_qs_shapping_cmd *shap_cfg_cmd;
	struct hclge_desc desc;
	u32 shapping_para;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_QCN_SHAPPING_CFG, true);
	shap_cfg_cmd = (struct hclge_qs_shapping_cmd *)desc.data;
	shap_cfg_cmd->qs_id = cpu_to_le16(qset_id);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get qset %u shaper, ret = %d\n", qset_id,
			ret);
		return ret;
	}

	shapping_para = le32_to_cpu(shap_cfg_cmd->qs_shapping_para);
	para->ir_b = hclge_tm_get_field(shapping_para, IR_B);
	para->ir_u = hclge_tm_get_field(shapping_para, IR_U);
	para->ir_s = hclge_tm_get_field(shapping_para, IR_S);
	para->bs_b = hclge_tm_get_field(shapping_para, BS_B);
	para->bs_s = hclge_tm_get_field(shapping_para, BS_S);
	para->flag = shap_cfg_cmd->flag;
	para->rate = le32_to_cpu(shap_cfg_cmd->qs_rate);
	return 0;
}

int hclge_tm_get_pri_sch_mode(struct hclge_dev *hdev, u8 pri_id, u8 *mode)
{
	struct hclge_pri_sch_mode_cfg_cmd *pri_sch_mode;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PRI_SCH_MODE_CFG, true);
	pri_sch_mode = (struct hclge_pri_sch_mode_cfg_cmd *)desc.data;
	pri_sch_mode->pri_id = pri_id;
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get priority sch mode, ret = %d\n", ret);
		return ret;
	}

	*mode = pri_sch_mode->sch_mode;
	return 0;
}

int hclge_tm_get_pri_weight(struct hclge_dev *hdev, u8 pri_id, u8 *weight)
{
	struct hclge_priority_weight_cmd *priority_weight;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PRI_WEIGHT, true);
	priority_weight = (struct hclge_priority_weight_cmd *)desc.data;
	priority_weight->pri_id = pri_id;
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get priority weight, ret = %d\n", ret);
		return ret;
	}

	*weight = priority_weight->dwrr;
	return 0;
}

int hclge_tm_get_pri_shaper(struct hclge_dev *hdev, u8 pri_id,
			    enum hclge_opcode_type cmd,
			    struct hclge_tm_shaper_para *para)
{
	struct hclge_pri_shapping_cmd *shap_cfg_cmd;
	struct hclge_desc desc;
	u32 shapping_para;
	int ret;

	if (cmd != HCLGE_OPC_TM_PRI_C_SHAPPING &&
	    cmd != HCLGE_OPC_TM_PRI_P_SHAPPING)
		return -EINVAL;

	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	shap_cfg_cmd = (struct hclge_pri_shapping_cmd *)desc.data;
	shap_cfg_cmd->pri_id = pri_id;
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get priority shaper(%#x), ret = %d\n",
			cmd, ret);
		return ret;
	}

	shapping_para = le32_to_cpu(shap_cfg_cmd->pri_shapping_para);
	para->ir_b = hclge_tm_get_field(shapping_para, IR_B);
	para->ir_u = hclge_tm_get_field(shapping_para, IR_U);
	para->ir_s = hclge_tm_get_field(shapping_para, IR_S);
	para->bs_b = hclge_tm_get_field(shapping_para, BS_B);
	para->bs_s = hclge_tm_get_field(shapping_para, BS_S);
	para->flag = shap_cfg_cmd->flag;
	para->rate = le32_to_cpu(shap_cfg_cmd->pri_rate);
	return 0;
}

int hclge_tm_get_q_to_qs_map(struct hclge_dev *hdev, u16 q_id, u16 *qset_id)
{
	struct hclge_nq_to_qs_link_cmd *map;
	struct hclge_desc desc;
	u16 qs_id_l;
	u16 qs_id_h;
	int ret;

	map = (struct hclge_nq_to_qs_link_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_NQ_TO_QS_LINK, true);
	map->nq_id = cpu_to_le16(q_id);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get queue to qset map, ret = %d\n", ret);
		return ret;
	}
	*qset_id = le16_to_cpu(map->qset_id);

	/* convert qset_id to the following format, drop the vld bit
	 *            | qs_id_h | vld | qs_id_l |
	 * qset_id:   | 15 ~ 11 |  10 |  9 ~ 0  |
	 *             \         \   /         /
	 *              \         \ /         /
	 * qset_id: | 15 | 14 ~ 10 |  9 ~ 0  |
	 */
	qs_id_l = hnae3_get_field(*qset_id, HCLGE_TM_QS_ID_L_MSK,
				  HCLGE_TM_QS_ID_L_S);
	qs_id_h = hnae3_get_field(*qset_id, HCLGE_TM_QS_ID_H_EXT_MSK,
				  HCLGE_TM_QS_ID_H_EXT_S);
	*qset_id = 0;
	hnae3_set_field(*qset_id, HCLGE_TM_QS_ID_L_MSK, HCLGE_TM_QS_ID_L_S,
			qs_id_l);
	hnae3_set_field(*qset_id, HCLGE_TM_QS_ID_H_MSK, HCLGE_TM_QS_ID_H_S,
			qs_id_h);
	return 0;
}

int hclge_tm_get_q_to_tc(struct hclge_dev *hdev, u16 q_id, u8 *tc_id)
{
#define HCLGE_TM_TC_MASK		0x7

	struct hclge_tqp_tx_queue_tc_cmd *tc;
	struct hclge_desc desc;
	int ret;

	tc = (struct hclge_tqp_tx_queue_tc_cmd *)desc.data;
	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TQP_TX_QUEUE_TC, true);
	tc->queue_id = cpu_to_le16(q_id);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get queue to tc map, ret = %d\n", ret);
		return ret;
	}

	*tc_id = tc->tc_id & HCLGE_TM_TC_MASK;
	return 0;
}

int hclge_tm_get_pg_to_pri_map(struct hclge_dev *hdev, u8 pg_id,
			       u8 *pri_bit_map)
{
	struct hclge_pg_to_pri_link_cmd *map;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PG_TO_PRI_LINK, true);
	map = (struct hclge_pg_to_pri_link_cmd *)desc.data;
	map->pg_id = pg_id;
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get pg to pri map, ret = %d\n", ret);
		return ret;
	}

	*pri_bit_map = map->pri_bit_map;
	return 0;
}

int hclge_tm_get_pg_weight(struct hclge_dev *hdev, u8 pg_id, u8 *weight)
{
	struct hclge_pg_weight_cmd *pg_weight_cmd;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PG_WEIGHT, true);
	pg_weight_cmd = (struct hclge_pg_weight_cmd *)desc.data;
	pg_weight_cmd->pg_id = pg_id;
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get pg weight, ret = %d\n", ret);
		return ret;
	}

	*weight = pg_weight_cmd->dwrr;
	return 0;
}

int hclge_tm_get_pg_sch_mode(struct hclge_dev *hdev, u8 pg_id, u8 *mode)
{
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PG_SCH_MODE_CFG, true);
	desc.data[0] = cpu_to_le32(pg_id);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get pg sch mode, ret = %d\n", ret);
		return ret;
	}

	*mode = (u8)le32_to_cpu(desc.data[1]);
	return 0;
}

int hclge_tm_get_pg_shaper(struct hclge_dev *hdev, u8 pg_id,
			   enum hclge_opcode_type cmd,
			   struct hclge_tm_shaper_para *para)
{
	struct hclge_pg_shapping_cmd *shap_cfg_cmd;
	struct hclge_desc desc;
	u32 shapping_para;
	int ret;

	if (cmd != HCLGE_OPC_TM_PG_C_SHAPPING &&
	    cmd != HCLGE_OPC_TM_PG_P_SHAPPING)
		return -EINVAL;

	hclge_cmd_setup_basic_desc(&desc, cmd, true);
	shap_cfg_cmd = (struct hclge_pg_shapping_cmd *)desc.data;
	shap_cfg_cmd->pg_id = pg_id;
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get pg shaper(%#x), ret = %d\n",
			cmd, ret);
		return ret;
	}

	shapping_para = le32_to_cpu(shap_cfg_cmd->pg_shapping_para);
	para->ir_b = hclge_tm_get_field(shapping_para, IR_B);
	para->ir_u = hclge_tm_get_field(shapping_para, IR_U);
	para->ir_s = hclge_tm_get_field(shapping_para, IR_S);
	para->bs_b = hclge_tm_get_field(shapping_para, BS_B);
	para->bs_s = hclge_tm_get_field(shapping_para, BS_S);
	para->flag = shap_cfg_cmd->flag;
	para->rate = le32_to_cpu(shap_cfg_cmd->pg_rate);
	return 0;
}

int hclge_tm_get_port_shaper(struct hclge_dev *hdev,
			     struct hclge_tm_shaper_para *para)
{
	struct hclge_port_shapping_cmd *port_shap_cfg_cmd;
	struct hclge_desc desc;
	u32 shapping_para;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PORT_SHAPPING, true);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to get port shaper, ret = %d\n", ret);
		return ret;
	}

	port_shap_cfg_cmd = (struct hclge_port_shapping_cmd *)desc.data;
	shapping_para = le32_to_cpu(port_shap_cfg_cmd->port_shapping_para);
	para->ir_b = hclge_tm_get_field(shapping_para, IR_B);
	para->ir_u = hclge_tm_get_field(shapping_para, IR_U);
	para->ir_s = hclge_tm_get_field(shapping_para, IR_S);
	para->bs_b = hclge_tm_get_field(shapping_para, BS_B);
	para->bs_s = hclge_tm_get_field(shapping_para, BS_S);
	para->flag = port_shap_cfg_cmd->flag;
	para->rate = le32_to_cpu(port_shap_cfg_cmd->port_rate);

	return 0;
}
