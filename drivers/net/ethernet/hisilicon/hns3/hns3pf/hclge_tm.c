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

#define HCLGE_ETHER_MAX_RATE	100000

/* hclge_shaper_para_calc: calculate ir parameter for the shaper
 * @ir: Rate to be config, its unit is Mbps
 * @shaper_level: the shaper level. eg: port, pg, priority, queueset
 * @ir_b: IR_B parameter of IR shaper
 * @ir_u: IR_U parameter of IR shaper
 * @ir_s: IR_S parameter of IR shaper
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
				  u8 *ir_b, u8 *ir_u, u8 *ir_s)
{
	const u16 tick_array[HCLGE_SHAPER_LVL_CNT] = {
		6 * 256,        /* Prioriy level */
		6 * 32,         /* Prioriy group level */
		6 * 8,          /* Port level */
		6 * 256         /* Qset level */
	};
	u8 ir_u_calc = 0, ir_s_calc = 0;
	u32 ir_calc;
	u32 tick;

	/* Calc tick */
	if (shaper_level >= HCLGE_SHAPER_LVL_CNT)
		return -EINVAL;

	tick = tick_array[shaper_level];

	/**
	 * Calc the speed if ir_b = 126, ir_u = 0 and ir_s = 0
	 * the formula is changed to:
	 *		126 * 1 * 8
	 * ir_calc = ---------------- * 1000
	 *		tick * 1
	 */
	ir_calc = (1008000 + (tick >> 1) - 1) / tick;

	if (ir_calc == ir) {
		*ir_b = 126;
		*ir_u = 0;
		*ir_s = 0;

		return 0;
	} else if (ir_calc > ir) {
		/* Increasing the denominator to select ir_s value */
		while (ir_calc > ir) {
			ir_s_calc++;
			ir_calc = 1008000 / (tick * (1 << ir_s_calc));
		}

		if (ir_calc == ir)
			*ir_b = 126;
		else
			*ir_b = (ir * tick * (1 << ir_s_calc) + 4000) / 8000;
	} else {
		/* Increasing the numerator to select ir_u value */
		u32 numerator;

		while (ir_calc < ir) {
			ir_u_calc++;
			numerator = 1008000 * (1 << ir_u_calc);
			ir_calc = (numerator + (tick >> 1)) / tick;
		}

		if (ir_calc == ir) {
			*ir_b = 126;
		} else {
			u32 denominator = (8000 * (1 << --ir_u_calc));
			*ir_b = (ir * tick + (denominator >> 1)) / denominator;
		}
	}

	*ir_u = ir_u_calc;
	*ir_s = ir_s_calc;

	return 0;
}

static int hclge_pfc_stats_get(struct hclge_dev *hdev,
			       enum hclge_opcode_type opcode, u64 *stats)
{
	struct hclge_desc desc[HCLGE_TM_PFC_PKT_GET_CMD_NUM];
	int ret, i, j;

	if (!(opcode == HCLGE_OPC_QUERY_PFC_RX_PKT_CNT ||
	      opcode == HCLGE_OPC_QUERY_PFC_TX_PKT_CNT))
		return -EINVAL;

	for (i = 0; i < HCLGE_TM_PFC_PKT_GET_CMD_NUM; i++) {
		hclge_cmd_setup_basic_desc(&desc[i], opcode, true);
		if (i != (HCLGE_TM_PFC_PKT_GET_CMD_NUM - 1))
			desc[i].flag |= cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
		else
			desc[i].flag &= ~cpu_to_le16(HCLGE_CMD_FLAG_NEXT);
	}

	ret = hclge_cmd_send(&hdev->hw, desc, HCLGE_TM_PFC_PKT_GET_CMD_NUM);
	if (ret)
		return ret;

	for (i = 0; i < HCLGE_TM_PFC_PKT_GET_CMD_NUM; i++) {
		struct hclge_pfc_stats_cmd *pfc_stats =
				(struct hclge_pfc_stats_cmd *)desc[i].data;

		for (j = 0; j < HCLGE_TM_PFC_NUM_GET_PER_CMD; j++) {
			u32 index = i * HCLGE_TM_PFC_PKT_GET_CMD_NUM + j;

			if (index < HCLGE_MAX_TC_NUM)
				stats[index] =
					le64_to_cpu(pfc_stats->pkt_num[j]);
		}
	}
	return 0;
}

int hclge_pfc_rx_stats_get(struct hclge_dev *hdev, u64 *stats)
{
	return hclge_pfc_stats_get(hdev, HCLGE_OPC_QUERY_PFC_RX_PKT_CNT, stats);
}

int hclge_pfc_tx_stats_get(struct hclge_dev *hdev, u64 *stats)
{
	return hclge_pfc_stats_get(hdev, HCLGE_OPC_QUERY_PFC_TX_PKT_CNT, stats);
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
	struct hclge_pfc_en_cmd *pfc = (struct hclge_pfc_en_cmd *)&desc.data;

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

	pause_param = (struct hclge_cfg_pause_param_cmd *)&desc.data;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_MAC_PARA, false);

	ether_addr_copy(pause_param->mac_addr, addr);
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

	pause_param = (struct hclge_cfg_pause_param_cmd *)&desc.data;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_CFG_MAC_PARA, true);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		return ret;

	trans_gap = pause_param->pause_trans_gap;
	trans_time = le16_to_cpu(pause_param->pause_trans_time);

	return hclge_pause_param_cfg(hdev, mac_addr, trans_gap,
					 trans_time);
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
				    u8 q_id, u16 qs_id)
{
	struct hclge_nq_to_qs_link_cmd *map;
	struct hclge_desc desc;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_NQ_TO_QS_LINK, false);

	map = (struct hclge_nq_to_qs_link_cmd *)desc.data;

	map->nq_id = cpu_to_le16(q_id);
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

static int hclge_tm_pg_shapping_cfg(struct hclge_dev *hdev,
				    enum hclge_shap_bucket bucket, u8 pg_id,
				    u8 ir_b, u8 ir_u, u8 ir_s, u8 bs_b, u8 bs_s)
{
	struct hclge_pg_shapping_cmd *shap_cfg_cmd;
	enum hclge_opcode_type opcode;
	struct hclge_desc desc;
	u32 shapping_para = 0;

	opcode = bucket ? HCLGE_OPC_TM_PG_P_SHAPPING :
		HCLGE_OPC_TM_PG_C_SHAPPING;
	hclge_cmd_setup_basic_desc(&desc, opcode, false);

	shap_cfg_cmd = (struct hclge_pg_shapping_cmd *)desc.data;

	shap_cfg_cmd->pg_id = pg_id;

	hclge_tm_set_field(shapping_para, IR_B, ir_b);
	hclge_tm_set_field(shapping_para, IR_U, ir_u);
	hclge_tm_set_field(shapping_para, IR_S, ir_s);
	hclge_tm_set_field(shapping_para, BS_B, bs_b);
	hclge_tm_set_field(shapping_para, BS_S, bs_s);

	shap_cfg_cmd->pg_shapping_para = cpu_to_le32(shapping_para);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_port_shaper_cfg(struct hclge_dev *hdev)
{
	struct hclge_port_shapping_cmd *shap_cfg_cmd;
	struct hclge_desc desc;
	u32 shapping_para = 0;
	u8 ir_u, ir_b, ir_s;
	int ret;

	ret = hclge_shaper_para_calc(HCLGE_ETHER_MAX_RATE,
				     HCLGE_SHAPER_LVL_PORT,
				     &ir_b, &ir_u, &ir_s);
	if (ret)
		return ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_TM_PORT_SHAPPING, false);
	shap_cfg_cmd = (struct hclge_port_shapping_cmd *)desc.data;

	hclge_tm_set_field(shapping_para, IR_B, ir_b);
	hclge_tm_set_field(shapping_para, IR_U, ir_u);
	hclge_tm_set_field(shapping_para, IR_S, ir_s);
	hclge_tm_set_field(shapping_para, BS_B, HCLGE_SHAPER_BS_U_DEF);
	hclge_tm_set_field(shapping_para, BS_S, HCLGE_SHAPER_BS_S_DEF);

	shap_cfg_cmd->port_shapping_para = cpu_to_le32(shapping_para);

	return hclge_cmd_send(&hdev->hw, &desc, 1);
}

static int hclge_tm_pri_shapping_cfg(struct hclge_dev *hdev,
				     enum hclge_shap_bucket bucket, u8 pri_id,
				     u8 ir_b, u8 ir_u, u8 ir_s,
				     u8 bs_b, u8 bs_s)
{
	struct hclge_pri_shapping_cmd *shap_cfg_cmd;
	enum hclge_opcode_type opcode;
	struct hclge_desc desc;
	u32 shapping_para = 0;

	opcode = bucket ? HCLGE_OPC_TM_PRI_P_SHAPPING :
		HCLGE_OPC_TM_PRI_C_SHAPPING;

	hclge_cmd_setup_basic_desc(&desc, opcode, false);

	shap_cfg_cmd = (struct hclge_pri_shapping_cmd *)desc.data;

	shap_cfg_cmd->pri_id = pri_id;

	hclge_tm_set_field(shapping_para, IR_B, ir_b);
	hclge_tm_set_field(shapping_para, IR_U, ir_u);
	hclge_tm_set_field(shapping_para, IR_S, ir_s);
	hclge_tm_set_field(shapping_para, BS_B, bs_b);
	hclge_tm_set_field(shapping_para, BS_S, bs_s);

	shap_cfg_cmd->pri_shapping_para = cpu_to_le32(shapping_para);

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

static void hclge_tm_vport_tc_info_update(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	u8 i;

	vport->bw_limit = hdev->tm_info.pg_info[0].bw_limit;
	kinfo->num_tc =
		min_t(u16, kinfo->num_tqps, hdev->tm_info.num_tc);
	kinfo->rss_size
		= min_t(u16, hdev->rss_size_max,
			kinfo->num_tqps / kinfo->num_tc);
	vport->qs_offset = hdev->tm_info.num_tc * vport->vport_id;
	vport->dwrr = 100;  /* 100 percent as init */
	vport->alloc_rss_size = kinfo->rss_size;

	for (i = 0; i < kinfo->num_tc; i++) {
		if (hdev->hw_tc_map & BIT(i)) {
			kinfo->tc_info[i].enable = true;
			kinfo->tc_info[i].tqp_offset = i * kinfo->rss_size;
			kinfo->tc_info[i].tqp_count = kinfo->rss_size;
			kinfo->tc_info[i].tc = i;
		} else {
			/* Set to default queue if TC is disable */
			kinfo->tc_info[i].enable = false;
			kinfo->tc_info[i].tqp_offset = 0;
			kinfo->tc_info[i].tqp_count = 1;
			kinfo->tc_info[i].tc = 0;
		}
	}

	memcpy(kinfo->prio_tc, hdev->tm_info.prio_tc,
	       FIELD_SIZEOF(struct hnae3_knic_private_info, prio_tc));
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

	/* DCB is enabled if we have more than 1 TC */
	if (hdev->tm_info.num_tc > 1)
		hdev->flag |= HCLGE_FLAG_DCB_ENABLE;
	else
		hdev->flag &= ~HCLGE_FLAG_DCB_ENABLE;
}

static void hclge_tm_pg_info_init(struct hclge_dev *hdev)
{
	u8 i;

	for (i = 0; i < hdev->tm_info.num_pg; i++) {
		int k;

		hdev->tm_info.pg_dwrr[i] = i ? 0 : 100;

		hdev->tm_info.pg_info[i].pg_id = i;
		hdev->tm_info.pg_info[i].pg_sch_mode = HCLGE_SCH_MODE_DWRR;

		hdev->tm_info.pg_info[i].bw_limit = HCLGE_ETHER_MAX_RATE;

		if (i != 0)
			continue;

		hdev->tm_info.pg_info[i].tc_bit_map = hdev->hw_tc_map;
		for (k = 0; k < hdev->tm_info.num_tc; k++)
			hdev->tm_info.pg_info[i].tc_dwrr[k] = 100;
	}
}

static void hclge_pfc_info_init(struct hclge_dev *hdev)
{
	if (!(hdev->flag & HCLGE_FLAG_DCB_ENABLE)) {
		if (hdev->fc_mode_last_time == HCLGE_FC_PFC)
			dev_warn(&hdev->pdev->dev,
				 "DCB is disable, but last mode is FC_PFC\n");

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

static int hclge_tm_schd_info_init(struct hclge_dev *hdev)
{
	if ((hdev->tx_sch_mode != HCLGE_FLAG_TC_BASE_SCH_MODE) &&
	    (hdev->tm_info.num_pg != 1))
		return -EINVAL;

	hclge_tm_pg_info_init(hdev);

	hclge_tm_tc_info_init(hdev);

	hclge_tm_vport_info_update(hdev);

	hclge_pfc_info_init(hdev);

	return 0;
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
	u8 ir_u, ir_b, ir_s;
	int ret;
	u32 i;

	/* Cfg pg schd */
	if (hdev->tx_sch_mode != HCLGE_FLAG_TC_BASE_SCH_MODE)
		return 0;

	/* Pg to pri */
	for (i = 0; i < hdev->tm_info.num_pg; i++) {
		/* Calc shaper para */
		ret = hclge_shaper_para_calc(
					hdev->tm_info.pg_info[i].bw_limit,
					HCLGE_SHAPER_LVL_PG,
					&ir_b, &ir_u, &ir_s);
		if (ret)
			return ret;

		ret = hclge_tm_pg_shapping_cfg(hdev,
					       HCLGE_TM_SHAP_C_BUCKET, i,
					       0, 0, 0, HCLGE_SHAPER_BS_U_DEF,
					       HCLGE_SHAPER_BS_S_DEF);
		if (ret)
			return ret;

		ret = hclge_tm_pg_shapping_cfg(hdev,
					       HCLGE_TM_SHAP_P_BUCKET, i,
					       ir_b, ir_u, ir_s,
					       HCLGE_SHAPER_BS_U_DEF,
					       HCLGE_SHAPER_BS_S_DEF);
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
		ret = hclge_tm_pg_weight_cfg(hdev, i,
					     hdev->tm_info.pg_dwrr[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_vport_q_to_qs_map(struct hclge_dev *hdev,
				   struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hnae3_queue **tqp = kinfo->tqp;
	struct hnae3_tc_info *v_tc_info;
	u32 i, j;
	int ret;

	for (i = 0; i < kinfo->num_tc; i++) {
		v_tc_info = &kinfo->tc_info[i];
		for (j = 0; j < v_tc_info->tqp_count; j++) {
			struct hnae3_queue *q = tqp[v_tc_info->tqp_offset + j];

			ret = hclge_tm_q_to_qs_map_cfg(hdev,
						       hclge_get_queue_id(q),
						       vport->qs_offset + i);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int hclge_tm_pri_q_qs_cfg(struct hclge_dev *hdev)
{
	struct hclge_vport *vport = hdev->vport;
	int ret;
	u32 i, k;

	if (hdev->tx_sch_mode == HCLGE_FLAG_TC_BASE_SCH_MODE) {
		/* Cfg qs -> pri mapping, one by one mapping */
		for (k = 0; k < hdev->num_alloc_vport; k++)
			for (i = 0; i < hdev->tm_info.num_tc; i++) {
				ret = hclge_tm_qs_to_pri_map_cfg(
					hdev, vport[k].qs_offset + i, i);
				if (ret)
					return ret;
			}
	} else if (hdev->tx_sch_mode == HCLGE_FLAG_VNET_BASE_SCH_MODE) {
		/* Cfg qs -> pri mapping,  qs = tc, pri = vf, 8 qs -> 1 pri */
		for (k = 0; k < hdev->num_alloc_vport; k++)
			for (i = 0; i < HNAE3_MAX_TC; i++) {
				ret = hclge_tm_qs_to_pri_map_cfg(
					hdev, vport[k].qs_offset + i, k);
				if (ret)
					return ret;
			}
	} else {
		return -EINVAL;
	}

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
	u8 ir_u, ir_b, ir_s;
	int ret;
	u32 i;

	for (i = 0; i < hdev->tm_info.num_tc; i++) {
		ret = hclge_shaper_para_calc(
					hdev->tm_info.tc_info[i].bw_limit,
					HCLGE_SHAPER_LVL_PRI,
					&ir_b, &ir_u, &ir_s);
		if (ret)
			return ret;

		ret = hclge_tm_pri_shapping_cfg(
			hdev, HCLGE_TM_SHAP_C_BUCKET, i,
			0, 0, 0, HCLGE_SHAPER_BS_U_DEF,
			HCLGE_SHAPER_BS_S_DEF);
		if (ret)
			return ret;

		ret = hclge_tm_pri_shapping_cfg(
			hdev, HCLGE_TM_SHAP_P_BUCKET, i,
			ir_b, ir_u, ir_s, HCLGE_SHAPER_BS_U_DEF,
			HCLGE_SHAPER_BS_S_DEF);
		if (ret)
			return ret;
	}

	return 0;
}

static int hclge_tm_pri_vnet_base_shaper_pri_cfg(struct hclge_vport *vport)
{
	struct hclge_dev *hdev = vport->back;
	u8 ir_u, ir_b, ir_s;
	int ret;

	ret = hclge_shaper_para_calc(vport->bw_limit, HCLGE_SHAPER_LVL_VF,
				     &ir_b, &ir_u, &ir_s);
	if (ret)
		return ret;

	ret = hclge_tm_pri_shapping_cfg(hdev, HCLGE_TM_SHAP_C_BUCKET,
					vport->vport_id,
					0, 0, 0, HCLGE_SHAPER_BS_U_DEF,
					HCLGE_SHAPER_BS_S_DEF);
	if (ret)
		return ret;

	ret = hclge_tm_pri_shapping_cfg(hdev, HCLGE_TM_SHAP_P_BUCKET,
					vport->vport_id,
					ir_b, ir_u, ir_s,
					HCLGE_SHAPER_BS_U_DEF,
					HCLGE_SHAPER_BS_S_DEF);
	if (ret)
		return ret;

	return 0;
}

static int hclge_tm_pri_vnet_base_shaper_qs_cfg(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	u8 ir_u, ir_b, ir_s;
	u32 i;
	int ret;

	for (i = 0; i < kinfo->num_tc; i++) {
		ret = hclge_shaper_para_calc(
					hdev->tm_info.tc_info[i].bw_limit,
					HCLGE_SHAPER_LVL_QSET,
					&ir_b, &ir_u, &ir_s);
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
	for (i = 0; i < kinfo->num_tc; i++) {
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
	} else {
		ret = hclge_tm_pri_vnet_base_dwrr_cfg(hdev);
		if (ret)
			return ret;
	}

	return 0;
}

int hclge_tm_map_cfg(struct hclge_dev *hdev)
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

static int hclge_tm_schd_mode_vnet_base_cfg(struct hclge_vport *vport)
{
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_dev *hdev = vport->back;
	int ret;
	u8 i;

	ret = hclge_tm_pri_schd_mode_cfg(hdev, vport->vport_id);
	if (ret)
		return ret;

	for (i = 0; i < kinfo->num_tc; i++) {
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
	u8 i, k;

	if (hdev->tx_sch_mode == HCLGE_FLAG_TC_BASE_SCH_MODE) {
		for (i = 0; i < hdev->tm_info.num_tc; i++) {
			ret = hclge_tm_pri_schd_mode_cfg(hdev, i);
			if (ret)
				return ret;

			for (k = 0; k < hdev->num_alloc_vport; k++) {
				ret = hclge_tm_qs_schd_mode_cfg(
					hdev, vport[k].qs_offset + i,
					HCLGE_SCH_MODE_DWRR);
				if (ret)
					return ret;
			}
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

int hclge_tm_schd_mode_hw(struct hclge_dev *hdev)
{
	int ret;

	ret = hclge_tm_lvl2_schd_mode_cfg(hdev);
	if (ret)
		return ret;

	return hclge_tm_lvl34_schd_mode_cfg(hdev);
}

static int hclge_tm_schd_setup_hw(struct hclge_dev *hdev)
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
				      hdev->tm_info.hw_pfc_map);
}

/* Each Tc has a 1024 queue sets to backpress, it divides to
 * 32 group, each group contains 32 queue sets, which can be
 * represented by u32 bitmap.
 */
static int hclge_bp_setup_hw(struct hclge_dev *hdev, u8 tc)
{
	int i;

	for (i = 0; i < HCLGE_BP_GRP_NUM; i++) {
		u32 qs_bitmap = 0;
		int k, ret;

		for (k = 0; k < hdev->num_alloc_vport; k++) {
			struct hclge_vport *vport = &hdev->vport[k];
			u16 qs_id = vport->qs_offset + tc;
			u8 grp, sub_grp;

			grp = hnae3_get_field(qs_id, HCLGE_BP_GRP_ID_M,
					      HCLGE_BP_GRP_ID_S);
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

int hclge_pause_setup_hw(struct hclge_dev *hdev)
{
	int ret;
	u8 i;

	ret = hclge_pause_param_setup_hw(hdev);
	if (ret)
		return ret;

	ret = hclge_mac_pause_setup_hw(hdev);
	if (ret)
		return ret;

	/* Only DCB-supported dev supports qset back pressure and pfc cmd */
	if (!hnae3_dev_dcb_supported(hdev))
		return 0;

	/* When MAC is GE Mode, hdev does not support pfc setting */
	ret = hclge_pfc_setup_hw(hdev);
	if (ret)
		dev_warn(&hdev->pdev->dev, "set pfc pause failed:%d\n", ret);

	for (i = 0; i < hdev->tm_info.num_tc; i++) {
		ret = hclge_bp_setup_hw(hdev, i);
		if (ret)
			return ret;
	}

	return 0;
}

int hclge_tm_prio_tc_info_update(struct hclge_dev *hdev, u8 *prio_tc)
{
	struct hclge_vport *vport = hdev->vport;
	struct hnae3_knic_private_info *kinfo;
	u32 i, k;

	for (i = 0; i < HNAE3_MAX_USER_PRIO; i++) {
		if (prio_tc[i] >= hdev->tm_info.num_tc)
			return -EINVAL;
		hdev->tm_info.prio_tc[i] = prio_tc[i];

		for (k = 0;  k < hdev->num_alloc_vport; k++) {
			kinfo = &vport[k].nic.kinfo;
			kinfo->prio_tc[i] = prio_tc[i];
		}
	}
	return 0;
}

void hclge_tm_schd_info_update(struct hclge_dev *hdev, u8 num_tc)
{
	u8 i, bit_map = 0;

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

int hclge_tm_init_hw(struct hclge_dev *hdev)
{
	int ret;

	if ((hdev->tx_sch_mode != HCLGE_FLAG_TC_BASE_SCH_MODE) &&
	    (hdev->tx_sch_mode != HCLGE_FLAG_VNET_BASE_SCH_MODE))
		return -ENOTSUPP;

	ret = hclge_tm_schd_setup_hw(hdev);
	if (ret)
		return ret;

	ret = hclge_pause_setup_hw(hdev);
	if (ret)
		return ret;

	return 0;
}

int hclge_tm_schd_init(struct hclge_dev *hdev)
{
	int ret;

	/* fc_mode is HCLGE_FC_FULL on reset */
	hdev->tm_info.fc_mode = HCLGE_FC_FULL;
	hdev->fc_mode_last_time = hdev->tm_info.fc_mode;

	ret = hclge_tm_schd_info_init(hdev);
	if (ret)
		return ret;

	return hclge_tm_init_hw(hdev);
}
