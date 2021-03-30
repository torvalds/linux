/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2016-2017 Hisilicon Limited.

#ifndef __HCLGE_TM_H
#define __HCLGE_TM_H

#include <linux/types.h>

/* MAC Pause */
#define HCLGE_TX_MAC_PAUSE_EN_MSK	BIT(0)
#define HCLGE_RX_MAC_PAUSE_EN_MSK	BIT(1)

#define HCLGE_TM_PORT_BASE_MODE_MSK	BIT(0)

#define HCLGE_DEFAULT_PAUSE_TRANS_GAP	0x7F
#define HCLGE_DEFAULT_PAUSE_TRANS_TIME	0xFFFF

/* SP or DWRR */
#define HCLGE_TM_TX_SCHD_DWRR_MSK	BIT(0)
#define HCLGE_TM_TX_SCHD_SP_MSK		0xFE

#define HCLGE_ETHER_MAX_RATE	100000

#define HCLGE_TM_PF_MAX_PRI_NUM		8
#define HCLGE_TM_PF_MAX_QSET_NUM	8

struct hclge_pg_to_pri_link_cmd {
	u8 pg_id;
	u8 rsvd1[3];
	u8 pri_bit_map;
};

struct hclge_qs_to_pri_link_cmd {
	__le16 qs_id;
	__le16 rsvd;
	u8 priority;
#define HCLGE_TM_QS_PRI_LINK_VLD_MSK	BIT(0)
	u8 link_vld;
};

struct hclge_nq_to_qs_link_cmd {
	__le16 nq_id;
	__le16 rsvd;
#define HCLGE_TM_Q_QS_LINK_VLD_MSK	BIT(10)
#define HCLGE_TM_QS_ID_L_MSK		GENMASK(9, 0)
#define HCLGE_TM_QS_ID_L_S		0
#define HCLGE_TM_QS_ID_H_MSK		GENMASK(14, 10)
#define HCLGE_TM_QS_ID_H_S		10
#define HCLGE_TM_QS_ID_H_EXT_S		11
#define HCLGE_TM_QS_ID_H_EXT_MSK	GENMASK(15, 11)
	__le16 qset_id;
};

struct hclge_tqp_tx_queue_tc_cmd {
	__le16 queue_id;
	__le16 rsvd;
	u8 tc_id;
	u8 rev[3];
};

struct hclge_pg_weight_cmd {
	u8 pg_id;
	u8 dwrr;
};

struct hclge_priority_weight_cmd {
	u8 pri_id;
	u8 dwrr;
};

struct hclge_pri_sch_mode_cfg_cmd {
	u8 pri_id;
	u8 rsvd[3];
	u8 sch_mode;
};

struct hclge_qs_sch_mode_cfg_cmd {
	__le16 qs_id;
	u8 rsvd[2];
	u8 sch_mode;
};

struct hclge_qs_weight_cmd {
	__le16 qs_id;
	u8 dwrr;
};

struct hclge_ets_tc_weight_cmd {
	u8 tc_weight[HNAE3_MAX_TC];
	u8 weight_offset;
	u8 rsvd[15];
};

#define HCLGE_TM_SHAP_IR_B_MSK  GENMASK(7, 0)
#define HCLGE_TM_SHAP_IR_B_LSH	0
#define HCLGE_TM_SHAP_IR_U_MSK  GENMASK(11, 8)
#define HCLGE_TM_SHAP_IR_U_LSH	8
#define HCLGE_TM_SHAP_IR_S_MSK  GENMASK(15, 12)
#define HCLGE_TM_SHAP_IR_S_LSH	12
#define HCLGE_TM_SHAP_BS_B_MSK  GENMASK(20, 16)
#define HCLGE_TM_SHAP_BS_B_LSH	16
#define HCLGE_TM_SHAP_BS_S_MSK  GENMASK(25, 21)
#define HCLGE_TM_SHAP_BS_S_LSH	21

enum hclge_shap_bucket {
	HCLGE_TM_SHAP_C_BUCKET = 0,
	HCLGE_TM_SHAP_P_BUCKET,
};

/* set bit HCLGE_TM_RATE_VLD to 1 means use 'rate' to config shaping */
#define HCLGE_TM_RATE_VLD	0

struct hclge_pri_shapping_cmd {
	u8 pri_id;
	u8 rsvd[3];
	__le32 pri_shapping_para;
	u8 flag;
	u8 rsvd1[3];
	__le32 pri_rate;
};

struct hclge_pg_shapping_cmd {
	u8 pg_id;
	u8 rsvd[3];
	__le32 pg_shapping_para;
	u8 flag;
	u8 rsvd1[3];
	__le32 pg_rate;
};

struct hclge_qs_shapping_cmd {
	__le16 qs_id;
	u8 rsvd[2];
	__le32 qs_shapping_para;
	u8 flag;
	u8 rsvd1[3];
	__le32 qs_rate;
};

#define HCLGE_BP_GRP_NUM		32
#define HCLGE_BP_SUB_GRP_ID_S		0
#define HCLGE_BP_SUB_GRP_ID_M		GENMASK(4, 0)
#define HCLGE_BP_GRP_ID_S		5
#define HCLGE_BP_GRP_ID_M		GENMASK(9, 5)

#define HCLGE_BP_EXT_GRP_NUM		40
#define HCLGE_BP_EXT_GRP_ID_S		5
#define HCLGE_BP_EXT_GRP_ID_M		GENMASK(10, 5)

struct hclge_bp_to_qs_map_cmd {
	u8 tc_id;
	u8 rsvd[2];
	u8 qs_group_id;
	__le32 qs_bit_map;
	u32 rsvd1;
};

struct hclge_pfc_en_cmd {
	u8 tx_rx_en_bitmap;
	u8 pri_en_bitmap;
};

struct hclge_cfg_pause_param_cmd {
	u8 mac_addr[ETH_ALEN];
	u8 pause_trans_gap;
	u8 rsvd;
	__le16 pause_trans_time;
	u8 rsvd1[6];
	/* extra mac address to do double check for pause frame */
	u8 mac_addr_extra[ETH_ALEN];
	u16 rsvd2;
};

struct hclge_pfc_stats_cmd {
	__le64 pkt_num[3];
};

struct hclge_port_shapping_cmd {
	__le32 port_shapping_para;
	u8 flag;
	u8 rsvd[3];
	__le32 port_rate;
};

struct hclge_shaper_ir_para {
	u8 ir_b; /* IR_B parameter of IR shaper */
	u8 ir_u; /* IR_U parameter of IR shaper */
	u8 ir_s; /* IR_S parameter of IR shaper */
};

struct hclge_tm_nodes_cmd {
	u8 pg_base_id;
	u8 pri_base_id;
	__le16 qset_base_id;
	__le16 queue_base_id;
	u8 pg_num;
	u8 pri_num;
	__le16 qset_num;
	__le16 queue_num;
};

struct hclge_pri_shaper_para {
	u8 ir_b;
	u8 ir_u;
	u8 ir_s;
	u8 bs_b;
	u8 bs_s;
	u8 flag;
	u32 rate;
};

#define hclge_tm_set_field(dest, string, val) \
			   hnae3_set_field((dest), \
			   (HCLGE_TM_SHAP_##string##_MSK), \
			   (HCLGE_TM_SHAP_##string##_LSH), val)
#define hclge_tm_get_field(src, string) \
			hnae3_get_field((src), HCLGE_TM_SHAP_##string##_MSK, \
					HCLGE_TM_SHAP_##string##_LSH)

int hclge_tm_schd_init(struct hclge_dev *hdev);
int hclge_tm_vport_map_update(struct hclge_dev *hdev);
int hclge_pause_setup_hw(struct hclge_dev *hdev, bool init);
int hclge_tm_schd_setup_hw(struct hclge_dev *hdev);
void hclge_tm_prio_tc_info_update(struct hclge_dev *hdev, u8 *prio_tc);
void hclge_tm_schd_info_update(struct hclge_dev *hdev, u8 num_tc);
void hclge_tm_pfc_info_update(struct hclge_dev *hdev);
int hclge_tm_dwrr_cfg(struct hclge_dev *hdev);
int hclge_tm_init_hw(struct hclge_dev *hdev, bool init);
int hclge_mac_pause_en_cfg(struct hclge_dev *hdev, bool tx, bool rx);
int hclge_pause_addr_cfg(struct hclge_dev *hdev, const u8 *mac_addr);
int hclge_pfc_rx_stats_get(struct hclge_dev *hdev, u64 *stats);
int hclge_pfc_tx_stats_get(struct hclge_dev *hdev, u64 *stats);
int hclge_tm_qs_shaper_cfg(struct hclge_vport *vport, int max_tx_rate);
int hclge_tm_get_qset_num(struct hclge_dev *hdev, u16 *qset_num);
int hclge_tm_get_pri_num(struct hclge_dev *hdev, u8 *pri_num);
int hclge_tm_get_qset_map_pri(struct hclge_dev *hdev, u16 qset_id, u8 *priority,
			      u8 *link_vld);
int hclge_tm_get_qset_sch_mode(struct hclge_dev *hdev, u16 qset_id, u8 *mode);
int hclge_tm_get_qset_weight(struct hclge_dev *hdev, u16 qset_id, u8 *weight);
int hclge_tm_get_pri_sch_mode(struct hclge_dev *hdev, u8 pri_id, u8 *mode);
int hclge_tm_get_pri_weight(struct hclge_dev *hdev, u8 pri_id, u8 *weight);
int hclge_tm_get_pri_shaper(struct hclge_dev *hdev, u8 pri_id,
			    enum hclge_opcode_type cmd,
			    struct hclge_pri_shaper_para *para);
#endif
