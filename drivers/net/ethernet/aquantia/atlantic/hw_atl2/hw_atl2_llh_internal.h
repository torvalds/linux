/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef HW_ATL2_LLH_INTERNAL_H
#define HW_ATL2_LLH_INTERNAL_H
/* RX timestamp_req_desc{D} [1:0] Bitfield Definitions
 */
#define HW_ATL2_RPF_TIMESTAMP_REQ_DESCD_ADR(descr) (0x00005B08 + (descr) * 0x20)
#define HW_ATL2_RPF_TIMESTAMP_REQ_DESCD_MSK 0x00030000
#define HW_ATL2_RPF_TIMESTAMP_REQ_DESCD_SHIFT 16

/* RX pif_rpf_redir_2_en_i Bitfield Definitions
 * PORT="pif_rpf_redir_2_en_i"
 */
#define HW_ATL2_RPF_PIF_RPF_REDIR2_ENI_ADR 0x000054C8
#define HW_ATL2_RPF_PIF_RPF_REDIR2_ENI_MSK 0x00001000
#define HW_ATL2_RPF_PIF_RPF_REDIR2_ENI_MSKN 0xFFFFEFFF
#define HW_ATL2_RPF_PIF_RPF_REDIR2_ENI_SHIFT 12
#define HW_ATL2_RPF_PIF_RPF_REDIR2_ENI_WIDTH 1
#define HW_ATL2_RPF_PIF_RPF_REDIR2_ENI_DEFAULT 0x0

/* RX pif_rpf_rss_hash_type_i Bitfield Definitions
 */
#define HW_ATL2_RPF_PIF_RPF_RSS_HASH_TYPEI_ADR 0x000054C8
#define HW_ATL2_RPF_PIF_RPF_RSS_HASH_TYPEI_MSK 0x000001FF
#define HW_ATL2_RPF_PIF_RPF_RSS_HASH_TYPEI_MSKN 0xFFFFFE00
#define HW_ATL2_RPF_PIF_RPF_RSS_HASH_TYPEI_SHIFT 0
#define HW_ATL2_RPF_PIF_RPF_RSS_HASH_TYPEI_WIDTH 9

/* rx rpf_new_rpf_en bitfield definitions
 * preprocessor definitions for the bitfield "rpf_new_rpf_en_i".
 * port="pif_rpf_new_rpf_en_i
 */

/* register address for bitfield rpf_new_rpf_en */
#define HW_ATL2_RPF_NEW_EN_ADR 0x00005104
/* bitmask for bitfield rpf_new_rpf_en */
#define HW_ATL2_RPF_NEW_EN_MSK 0x00000800
/* inverted bitmask for bitfield rpf_new_rpf_en */
#define HW_ATL2_RPF_NEW_EN_MSKN 0xfffff7ff
/* lower bit position of bitfield rpf_new_rpf_en */
#define HW_ATL2_RPF_NEW_EN_SHIFT 11
/* width of bitfield rpf_new_rpf_en */
#define HW_ATL2_RPF_NEW_EN_WIDTH 1
/* default value of bitfield rpf_new_rpf_en */
#define HW_ATL2_RPF_NEW_EN_DEFAULT 0x0

/* rx l2_uc_req_tag0{f}[5:0] bitfield definitions
 * preprocessor definitions for the bitfield "l2_uc_req_tag0{f}[7:0]".
 * parameter: filter {f} | stride size 0x8 | range [0, 37]
 * port="pif_rpf_l2_uc_req_tag0[5:0]"
 */

/* register address for bitfield l2_uc_req_tag0{f}[2:0] */
#define HW_ATL2_RPFL2UC_TAG_ADR(filter) (0x00005114 + (filter) * 0x8)
/* bitmask for bitfield l2_uc_req_tag0{f}[2:0] */
#define HW_ATL2_RPFL2UC_TAG_MSK 0x0FC00000
/* inverted bitmask for bitfield l2_uc_req_tag0{f}[2:0] */
#define HW_ATL2_RPFL2UC_TAG_MSKN 0xF03FFFFF
/* lower bit position of bitfield l2_uc_req_tag0{f}[2:0] */
#define HW_ATL2_RPFL2UC_TAG_SHIFT 22
/* width of bitfield l2_uc_req_tag0{f}[2:0] */
#define HW_ATL2_RPFL2UC_TAG_WIDTH 6
/* default value of bitfield l2_uc_req_tag0{f}[2:0] */
#define HW_ATL2_RPFL2UC_TAG_DEFAULT 0x0

/* rpf_l2_bc_req_tag[5:0] bitfield definitions
 * preprocessor definitions for the bitfield "rpf_l2_bc_req_tag[5:0]".
 * port="pifrpf_l2_bc_req_tag_i[5:0]"
 */

/* register address for bitfield rpf_l2_bc_req_tag */
#define HW_ATL2_RPF_L2_BC_TAG_ADR 0x000050F0
/* bitmask for bitfield rpf_l2_bc_req_tag */
#define HW_ATL2_RPF_L2_BC_TAG_MSK 0x0000003F
/* inverted bitmask for bitfield rpf_l2_bc_req_tag */
#define HW_ATL2_RPF_L2_BC_TAG_MSKN 0xffffffc0
/* lower bit position of bitfield rpf_l2_bc_req_tag */
#define HW_ATL2_RPF_L2_BC_TAG_SHIFT 0
/* width of bitfield rpf_l2_bc_req_tag */
#define HW_ATL2_RPF_L2_BC_TAG_WIDTH 6
/* default value of bitfield rpf_l2_bc_req_tag */
#define HW_ATL2_RPF_L2_BC_TAG_DEFAULT 0x0

/* rx rpf_rss_red1_data_[4:0] bitfield definitions
 * preprocessor definitions for the bitfield "rpf_rss_red1_data[4:0]".
 * port="pif_rpf_rss_red1_data_i[4:0]"
 */

/* register address for bitfield rpf_rss_red1_data[4:0] */
#define HW_ATL2_RPF_RSS_REDIR_ADR(TC, INDEX) (0x00006200 + \
					(0x100 * !!((TC) > 3)) + (INDEX) * 4)
/* bitmask for bitfield rpf_rss_red1_data[4:0] */
#define HW_ATL2_RPF_RSS_REDIR_MSK(TC)  (0x00000001F << (5 * ((TC) % 4)))
/* lower bit position of bitfield rpf_rss_red1_data[4:0] */
#define HW_ATL2_RPF_RSS_REDIR_SHIFT(TC) (5 * ((TC) % 4))
/* width of bitfield rpf_rss_red1_data[4:0] */
#define HW_ATL2_RPF_RSS_REDIR_WIDTH 5
/* default value of bitfield rpf_rss_red1_data[4:0] */
#define HW_ATL2_RPF_RSS_REDIR_DEFAULT 0x0

/* rx vlan_req_tag0{f}[3:0] bitfield definitions
 * preprocessor definitions for the bitfield "vlan_req_tag0{f}[3:0]".
 * parameter: filter {f} | stride size 0x4 | range [0, 15]
 * port="pif_rpf_vlan_req_tag0[3:0]"
 */

/* register address for bitfield vlan_req_tag0{f}[3:0] */
#define HW_ATL2_RPF_VL_TAG_ADR(filter) (0x00005290 + (filter) * 0x4)
/* bitmask for bitfield vlan_req_tag0{f}[3:0] */
#define HW_ATL2_RPF_VL_TAG_MSK 0x0000F000
/* inverted bitmask for bitfield vlan_req_tag0{f}[3:0] */
#define HW_ATL2_RPF_VL_TAG_MSKN 0xFFFF0FFF
/* lower bit position of bitfield vlan_req_tag0{f}[3:0] */
#define HW_ATL2_RPF_VL_TAG_SHIFT 12
/* width of bitfield vlan_req_tag0{f}[3:0] */
#define HW_ATL2_RPF_VL_TAG_WIDTH 4
/* default value of bitfield vlan_req_tag0{f}[3:0] */
#define HW_ATL2_RPF_VL_TAG_DEFAULT 0x0
/* register address for bitfield etype_req_tag0{f}[2:0] */
#define HW_ATL2_RPF_ET_TAG_ADR(filter) (0x00005340 + (filter) * 0x4)
/* bitmask for bitfield etype_req_tag0{f}[2:0] */
#define HW_ATL2_RPF_ET_TAG_MSK 0x00000007
/* lower bit position of bitfield etype_req_tag0{f}[2:0] */
#define HW_ATL2_RPF_ET_TAG_SHIFT 0
/* Lower bit position of bitfield l3_l4_act{F}[2:0] */
#define HW_ATL2_RPF_L3_L4_ACTF_SHIFT 16
/* Bitmask for bitfield l3_l4_rxq{F}[4:0] */
#define HW_ATL2_RPF_L3_L4_RXQF_MSK 0x00001F00u
/* Lower bit position of bitfield l3_l4_rxq{F}[4:0] */
#define HW_ATL2_RPF_L3_L4_RXQF_SHIFT 8
/* Register address for bitfield rpf_l3_v6_sa{F}_dw{D}[1F:0] */
#define HW_ATL2_RPF_L3_SA_DW_ADR(filter, dword) \
	(0x00006400u + (filter) * 0x10 + (dword) * 0x4)

/* Register address for bitfield rpf_l3_v6_da{F}_dw{D}[1F:0] */
#define HW_ATL2_RPF_L3_DA_DW_ADR(filter, dword) \
	(0x00006480u + (filter) * 0x10 + (dword) * 0x4)

/* Register address for bitfield rpf_l3_cmd{F}[1F:0] */
#define HW_ATL2_RPF_L3_V4_CMD_ADR(filter) (0x00006500u + (filter) * 0x4)
/* Bitmask for bitfield rpf_l3_cmd{F}[F:0] */
#define HW_ATL2_RPF_L3_V4_CMD_MSK 0x0000FFFFu
/* Lower bit position of bitfield rpf_l3_cmd{F}[1F:0] */
#define HW_ATL2_RPF_L3_V4_CMD_SHIFT 0
/* Register address for bitfield rpf_l3_v6_cmd{F}[1F:0] */
#define HW_ATL2_RPF_L3_V6_CMD_ADR(filter) (0x00006500u + (filter) * 0x4)
/* Bitmask for bitfield rpf_l3_v6_cmd{F}[F:0] */
#define HW_ATL2_RPF_L3_V6_CMD_MSK 0xFF7F0000u
/* Lower bit position of bitfield rpf_l3_v6_cmd{F}[1F:0] */
#define HW_ATL2_RPF_L3_V6_CMD_SHIFT 0
/* Register address for bitfield rpf_l3_v6_cmd{F}[F:0] */
#define HW_ATL2_RPF_L3_V6_V4_SELECT_ADR 0x00006500u
/* Bitmask for bitfield pif_rpf_l3_v6_v4_select*/
#define HW_ATL2_RPF_L3_V6_V4_SELECT_MSK 0x00800000u
/* Lower bit position of bitfield pif_rpf_l3_v6_v4_select */
#define HW_ATL2_RPF_L3_V6_V4_SELECT_SHIFT 23
/* Register address for bitfield rpf_l3_v4_req_tag{F}[2:0] */
#define HW_ATL2_RPF_L3_V4_TAG_ADR(filter) (0x00006500u + (filter) * 0x4)
/* Bitmask for bitfield rpf_l3_v4_req_tag{F}[2:0] */
#define HW_ATL2_RPF_L3_V4_TAG_MSK 0x00000070u
/* Lower bit position of bitfield rpf_l3_v4_req_tag{F}[2:0] */
#define HW_ATL2_RPF_L3_V4_TAG_SHIFT 4
/* Register address for bitfield rpf_l3_v6_req_tag{F}[2:0] */
#define HW_ATL2_RPF_L3_V6_TAG_ADR(filter) (0x00006500u + (filter) * 0x4)
/* Bitmask for bitfield rpf_l3_v6_req_tag{F}[2:0] */
#define HW_ATL2_RPF_L3_V6_TAG_MSK 0x00700000
/* Lower bit position of bitfield rpf_l3_v6_req_tag{F}[2:0] */
#define HW_ATL2_RPF_L3_V6_TAG_SHIFT 20
/* Register address for bitfield rpf_l4_cmd{F}[2:0] */
#define HW_ATL2_RPF_L4_CMD_ADR(filter) (0x00006520u + (filter) * 0x4)
/* Bitmask for bitfield rpf_l4_cmd{F}[2:0] */
#define HW_ATL2_RPF_L4_CMD_MSK 0x00000007u
/* Lower bit position of bitfield rpf_l4_cmd{F}[2:0] */
#define HW_ATL2_RPF_L4_CMD_SHIFT 0
/* Register address for bitfield rpf_l4_tag{F}[2:0] */
#define HW_ATL2_RPF_L4_TAG_ADR(filter) (0x00006520u + (filter) * 0x4)
/* Bitmask for bitfield rpf_l4_tag{F}[2:0] */
#define HW_ATL2_RPF_L4_TAG_MSK 0x00000070u
/* Lower bit position of bitfield rpf_l4_tag{F}[2:0] */
#define HW_ATL2_RPF_L4_TAG_SHIFT 4
/* RX rx_q{Q}_tc_map[2:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "rx_q{Q}_tc_map[2:0]".
 * Parameter: Queue {Q} | bit-level stride | range [0, 31]
 * PORT="pif_rx_q0_tc_map_i[2:0]"
 */

/* Register address for bitfield rx_q{Q}_tc_map[2:0] */
#define HW_ATL2_RX_Q_TC_MAP_ADR(queue) \
	(((queue) < 32) ? 0x00005900 + ((queue) / 8) * 4 : 0)
/* Lower bit position of bitfield rx_q{Q}_tc_map[2:0] */
#define HW_ATL2_RX_Q_TC_MAP_SHIFT(queue) \
	(((queue) < 32) ? ((queue) * 4) % 32 : 0)
/* Width of bitfield rx_q{Q}_tc_map[2:0] */
#define HW_ATL2_RX_Q_TC_MAP_WIDTH 3
/* Default value of bitfield rx_q{Q}_tc_map[2:0] */
#define HW_ATL2_RX_Q_TC_MAP_DEFAULT 0x0
/* TX desc{D}_ts_wrb_en Bitfield Definitions
 */
#define HW_ATL2_TDM_DESCD_TS_WRB_EN_ADR(descriptor) \
	(0x00007C08 + (descriptor) * 0x40)
#define HW_ATL2_TDM_DESCD_TS_WRB_EN_MSK 0x00040000
#define HW_ATL2_TDM_DESCD_TS_WRB_EN_SHIFT 18
/* TX desc{D}_ts_en Bitfield Definitions
 */
#define HW_ATL2_TDM_DESCD_TS_EN_ADR(descriptor) \
	(0x00007C08 + (descriptor) * 0x40)
#define HW_ATL2_TDM_DESCD_TS_EN_MSK 0x00020000
#define HW_ATL2_TDM_DESCD_TS_EN_SHIFT 17
/* TX desc{D}_avb_en Bitfield Definitions
 */
#define HW_ATL2_TDM_DESCD_AVB_EN_ADR(descriptor) \
	(0x00007C08 + (descriptor) * 0x40)
#define HW_ATL2_TDM_DESCD_AVB_EN_MSK 0x00010000
#define HW_ATL2_TDM_DESCD_AVB_EN_SHIFT 16
/* tx tx_tc_q_rand_map_en bitfield definitions
 * preprocessor definitions for the bitfield "tx_tc_q_rand_map_en".
 * port="pif_tpb_tx_tc_q_rand_map_en_i"
 */

/* register address for bitfield tx_tc_q_rand_map_en */
#define HW_ATL2_TPB_TX_TC_Q_RAND_MAP_EN_ADR 0x00007900
/* bitmask for bitfield tx_tc_q_rand_map_en */
#define HW_ATL2_TPB_TX_TC_Q_RAND_MAP_EN_MSK 0x00000200
/* inverted bitmask for bitfield tx_tc_q_rand_map_en */
#define HW_ATL2_TPB_TX_TC_Q_RAND_MAP_EN_MSKN 0xFFFFFDFF
/* lower bit position of bitfield tx_tc_q_rand_map_en */
#define HW_ATL2_TPB_TX_TC_Q_RAND_MAP_EN_SHIFT 9
/* width of bitfield tx_tc_q_rand_map_en */
#define HW_ATL2_TPB_TX_TC_Q_RAND_MAP_EN_WIDTH 1
/* default value of bitfield tx_tc_q_rand_map_en */
#define HW_ATL2_TPB_TX_TC_Q_RAND_MAP_EN_DEFAULT 0x0

/* tx tx_buffer_clk_gate_en bitfield definitions
 * preprocessor definitions for the bitfield "tx_buffer_clk_gate_en".
 * port="pif_tpb_tx_buffer_clk_gate_en_i"
 */

/* register address for bitfield tx_buffer_clk_gate_en */
#define HW_ATL2_TPB_TX_BUF_CLK_GATE_EN_ADR 0x00007900
/* bitmask for bitfield tx_buffer_clk_gate_en */
#define HW_ATL2_TPB_TX_BUF_CLK_GATE_EN_MSK 0x00000020
/* inverted bitmask for bitfield tx_buffer_clk_gate_en */
#define HW_ATL2_TPB_TX_BUF_CLK_GATE_EN_MSKN 0xffffffdf
/* lower bit position of bitfield tx_buffer_clk_gate_en */
#define HW_ATL2_TPB_TX_BUF_CLK_GATE_EN_SHIFT 5
/* width of bitfield tx_buffer_clk_gate_en */
#define HW_ATL2_TPB_TX_BUF_CLK_GATE_EN_WIDTH 1
/* default value of bitfield tx_buffer_clk_gate_en */
#define HW_ATL2_TPB_TX_BUF_CLK_GATE_EN_DEFAULT 0x0

/* tx tx_q_tc_map{q} bitfield definitions
 * preprocessor definitions for the bitfield "tx_q_tc_map{q}".
 * parameter: queue {q} | bit-level stride | range [0, 31]
 * port="pif_tpb_tx_q_tc_map0_i[2:0]"
 */

/* register address for bitfield tx_q_tc_map{q} */
#define HW_ATL2_TX_Q_TC_MAP_ADR(queue) \
	(((queue) < 32) ? 0x0000799C + ((queue) / 4) * 4 : 0)
/* lower bit position of bitfield tx_q_tc_map{q} */
#define HW_ATL2_TX_Q_TC_MAP_SHIFT(queue) \
	(((queue) < 32) ? ((queue) * 8) % 32 : 0)
/* width of bitfield tx_q_tc_map{q} */
#define HW_ATL2_TX_Q_TC_MAP_WIDTH 3
/* default value of bitfield tx_q_tc_map{q} */
#define HW_ATL2_TX_Q_TC_MAP_DEFAULT 0x0

/* tx data_tc_arb_mode bitfield definitions
 * preprocessor definitions for the bitfield "data_tc_arb_mode".
 * port="pif_tps_data_tc_arb_mode_i"
 */

/* register address for bitfield data_tc_arb_mode */
#define HW_ATL2_TPS_DATA_TC_ARB_MODE_ADR 0x00007100
/* bitmask for bitfield data_tc_arb_mode */
#define HW_ATL2_TPS_DATA_TC_ARB_MODE_MSK 0x00000003
/* inverted bitmask for bitfield data_tc_arb_mode */
#define HW_ATL2_TPS_DATA_TC_ARB_MODE_MSKN 0xfffffffc
/* lower bit position of bitfield data_tc_arb_mode */
#define HW_ATL2_TPS_DATA_TC_ARB_MODE_SHIFT 0
/* width of bitfield data_tc_arb_mode */
#define HW_ATL2_TPS_DATA_TC_ARB_MODE_WIDTH 2
/* default value of bitfield data_tc_arb_mode */
#define HW_ATL2_TPS_DATA_TC_ARB_MODE_DEFAULT 0x0

/* tx data_tc{t}_credit_max[f:0] bitfield definitions
 * preprocessor definitions for the bitfield "data_tc{t}_credit_max[f:0]".
 * parameter: tc {t} | stride size 0x4 | range [0, 7]
 * port="pif_tps_data_tc0_credit_max_i[15:0]"
 */

/* register address for bitfield data_tc{t}_credit_max[f:0] */
#define HW_ATL2_TPS_DATA_TCTCREDIT_MAX_ADR(tc) (0x00007110 + (tc) * 0x4)
/* bitmask for bitfield data_tc{t}_credit_max[f:0] */
#define HW_ATL2_TPS_DATA_TCTCREDIT_MAX_MSK 0xffff0000
/* inverted bitmask for bitfield data_tc{t}_credit_max[f:0] */
#define HW_ATL2_TPS_DATA_TCTCREDIT_MAX_MSKN 0x0000ffff
/* lower bit position of bitfield data_tc{t}_credit_max[f:0] */
#define HW_ATL2_TPS_DATA_TCTCREDIT_MAX_SHIFT 16
/* width of bitfield data_tc{t}_credit_max[f:0] */
#define HW_ATL2_TPS_DATA_TCTCREDIT_MAX_WIDTH 16
/* default value of bitfield data_tc{t}_credit_max[f:0] */
#define HW_ATL2_TPS_DATA_TCTCREDIT_MAX_DEFAULT 0x0
/* register address for bitfield pif_tpb_highest_prio_tc_en */
#define HW_ATL2_TPB_HIGHEST_PRIO_TC_EN_ADR 0x00007180
/* bitmask for bitfield pif_tpb_highest_prio_tc_en */
#define HW_ATL2_TPB_HIGHEST_PRIO_TC_EN_MSK 0x00000100
/* lower bit position of bitfield pif_tpb_highest_prio_tc_en */
#define HW_ATL2_TPB_HIGHEST_PRIO_TC_EN_SHIFT 8
/* register address for bitfield pif_tpb_highest_prio_tc */
#define HW_ATL2_TPB_HIGHEST_PRIO_TC_ADR 0x00007180
/* bitmask for bitfield pif_tpb_highest_prio_tc */
#define HW_ATL2_TPB_HIGHEST_PRIO_TC_MSK 0x00000007
/* lower bit position of bitfield pif_tpb_highest_prio_tc */
#define HW_ATL2_TPB_HIGHEST_PRIO_TC_SHIFT 0
/* tx data_tc{t}_weight[e:0] bitfield definitions
 * preprocessor definitions for the bitfield "data_tc{t}_weight[e:0]".
 * parameter: tc {t} | stride size 0x4 | range [0, 7]
 * port="pif_tps_data_tc0_weight_i[14:0]"
 */

/* register address for bitfield data_tc{t}_weight[e:0] */
#define HW_ATL2_TPS_DATA_TCTWEIGHT_ADR(tc) (0x00007110 + (tc) * 0x4)
/* bitmask for bitfield data_tc{t}_weight[e:0] */
#define HW_ATL2_TPS_DATA_TCTWEIGHT_MSK 0x00007fff
/* inverted bitmask for bitfield data_tc{t}_weight[e:0] */
#define HW_ATL2_TPS_DATA_TCTWEIGHT_MSKN 0xffff8000
/* lower bit position of bitfield data_tc{t}_weight[e:0] */
#define HW_ATL2_TPS_DATA_TCTWEIGHT_SHIFT 0
/* width of bitfield data_tc{t}_weight[e:0] */
#define HW_ATL2_TPS_DATA_TCTWEIGHT_WIDTH 15
/* default value of bitfield data_tc{t}_weight[e:0] */
#define HW_ATL2_TPS_DATA_TCTWEIGHT_DEFAULT 0x0

/* tx interrupt moderation control register definitions
 * Preprocessor definitions for TX Interrupt Moderation Control Register
 * Base Address: 0x00007c28
 * Parameter: queue {Q} | stride size 0x4 | range [0, 31]
 */

#define HW_ATL2_TX_INTR_MODERATION_CTL_ADR(queue) (0x00007c28u + (queue) * 0x40)
/* TX tx_data_rd_req_limit[7:0] Bitfield Definitions
 */
#define HW_ATL2_TDM_TX_DATA_RD_REQ_LIMIT_ADR 0x00007B04
#define HW_ATL2_TDM_TX_DATA_RD_REQ_LIMIT_MSK 0x0000FF00
#define HW_ATL2_TDM_TX_DATA_RD_REQ_LIMIT_SHIFT 8
/* TX tx_desc_rd_req_limit[4:0] Bitfield Definitions
 */
#define HW_ATL2_TDM_TX_DESC_RD_REQ_LIMIT_ADR 0x00007B04
#define HW_ATL2_TDM_TX_DESC_RD_REQ_LIMIT_MSK 0x0000001F
#define HW_ATL2_TDM_TX_DESC_RD_REQ_LIMIT_SHIFT 0
/* register address for bitfield uP Force Interrupt */
#define HW_ATL2_GLB_CONTROL_2_ADR 0x00000404
#define HW_ATL2_MIF_INTERRUPT_2_TO_ITR_MSK 0x00000100
/* lower bit position of bitfield MIF Interrupt to ITR */
#define HW_ATL2_MIF_INTERRUPT_TO_ITR_SHIFT 6
#define HW_ATL2_EN_INTERRUPT_MIF2_TO_ITR_MSK 0x00001000
/* lower bit position of bitfield Enable MIF Interrupt to ITR */
#define HW_ATL2_EN_INTERRUPT_TO_ITR_SHIFT 0xA
#define HW_ATL2_GLOBAL_INTERNAL_ALARMS_1_ADR 0x00000924
#define HW_ATL2_GLOBAL_HIGH_PRIO_INTERRUPT_1_MASK_ADR 0x00000964
/* bitmask for bitfield TSG PTM GPIO interrupt */
#define HW_ATL2_TSG_TSG1_GPIO_INTERRUPT_MSK 0x00000200
/* lower bit position of bitfield TSG PTM GPIO interrupt */
#define HW_ATL2_TSG_TSG1_GPIO_INTERRUPT_SHIFT 9
/* bitmask for bitfield TSG0 GPIO interrupt */
#define HW_ATL2_TSG_TSG0_GPIO_INTERRUPT_MSK 0x00000020
/* lower bit position of bitfield TSG0 GPIO interrupt */
#define HW_ATL2_TSG_TSG0_GPIO_INTERRUPT_SHIFT 5
/* TSG registers */
#define HW_ATL2_TSG_REG_ADR(clk, reg_name) \
	((clk) == 0 ? HW_ATL2_CLK0_##reg_name##_ADR :\
		 HW_ATL2_CLK1_##reg_name##_ADR)

#define HW_ATL2_CLK0_CLOCK_CFG_ADR 0x00000CA0u
#define HW_ATL2_CLK1_CLOCK_CFG_ADR 0x00000D50u
#define HW_ATL2_TSG_SYNC_RESET_MSK 0x00000001
#define HW_ATL2_TSG_SYNC_RESET_SHIFT 0x00000000
#define HW_ATL2_TSG_CLOCK_EN_MSK 0x00000002
#define HW_ATL2_TSG_CLOCK_EN_SHIFT 0x00000001
#define HW_ATL2_CLK0_CLOCK_MODIF_CTRL_ADR 0x00000CA4u
#define HW_ATL2_CLK1_CLOCK_MODIF_CTRL_ADR 0x00000D54u
#define HW_ATL2_TSG_SUBTRACT_COUNTER_MSK 0x00000002
#define HW_ATL2_TSG_ADD_COUNTER_MSK 0x00000004
#define HW_ATL2_TSG_LOAD_INC_CFG_MSK 0x00000008
#define HW_ATL2_CLK0_CLOCK_MODIF_VAL_LSW_ADR 0x00000CA8u
#define HW_ATL2_CLK1_CLOCK_MODIF_VAL_LSW_ADR 0x00000D58u
#define HW_ATL2_CLK0_CLOCK_INC_CFG_ADR 0x00000CB0u
#define HW_ATL2_CLK1_CLOCK_INC_CFG_ADR 0x00000D60u
#define HW_ATL2_CLK0_READ_CUR_NS_LSW_ADR 0x00000CB8u
#define HW_ATL2_CLK1_READ_CUR_NS_LSW_ADR 0x00000D68u

#define HW_ATL2_CLK0_GPIO_CFG_ADR 0x00000CC4u
#define HW_ATL2_CLK1_GPIO_CFG_ADR 0x00000D74u
#define HW_ATL2_TSG_GPIO_IN_MONITOR_EN_SHIFT 0x00000000
#define HW_ATL2_TSG_GPIO_IN_MONITOR_EN_MSK 0x00000001
#define HW_ATL2_TSG_GPIO_IN_MODE_SHIFT 0x00000001
#define HW_ATL2_TSG_GPIO_IN_MODE_MSK 0x00000006
#define HW_ATL2_TSG_GPIO_IN_MODE_POSEDGE 0x00000000
#define HW_ATL2_CLK0_EXT_CLK_COUNT_ADR 0x00000CCCu
#define HW_ATL2_CLK1_EXT_CLK_COUNT_ADR 0x00000D7Cu
#define HW_ATL2_CLK0_GPIO_EVENT_TS_LSW_ADR 0x00000CD0u
#define HW_ATL2_CLK1_GPIO_EVENT_TS_LSW_ADR 0x00000D80u
#define HW_ATL2_CLK0_GPIO_EVENT_GEN_TS_LSW_ADR 0x00000CE0u
#define HW_ATL2_CLK1_GPIO_EVENT_GEN_TS_LSW_ADR 0x00000D90u
#define HW_ATL2_CLK0_GPIO_EVENT_GEN_CFG_ADR 0x00000CE8u
#define HW_ATL2_CLK1_GPIO_EVENT_GEN_CFG_ADR 0x00000D98u
#define HW_ATL2_TSG_GPIO_OUTPUT_EN_SHIFT 0x00000000
#define HW_ATL2_TSG_GPIO_OUTPUT_EN_MSK 0x00000001
#define HW_ATL2_TSG_GPIO_EVENT_MODE_SHIFT 0x00000001
#define HW_ATL2_TSG_GPIO_EVENT_MODE_MSK 0x00000006
#define HW_ATL2_TSG_GPIO_EVENT_MODE_SET_ON_TIME 0x00000003
#define HW_ATL2_TSG_GPIO_GEN_OUTPUT_EN_MSK 0x00000008
#define HW_ATL2_CLK0_GPIO_EVENT_HIGH_TIME_LSW_ADR 0x00000CF0u
#define HW_ATL2_CLK1_GPIO_EVENT_HIGH_TIME_LSW_ADR 0x00000DA0u
#define HW_ATL2_CLK0_GPIO_EVENT_LOW_TIME_LSW_ADR 0x00000CF8u
#define HW_ATL2_CLK1_GPIO_EVENT_LOW_TIME_LSW_ADR 0x00000DA8u
/* PCIE Extended tag enable Bitfield Definitions
 */
#define HW_ATL2_PHI_EXT_TAG_EN_ADR 0x00001000
#define HW_ATL2_PHI_EXT_TAG_EN_MSK 0x00000020
#define HW_ATL2_PHI_EXT_TAG_EN_SHIFT 5
/* Launch time control register */
#define HW_ATL2_LT_CTRL_ADR 0x00007a1c

#define HW_ATL2_LT_CTRL_AVB_LEN_CMP_TRSHLD_MSK 0xFFFF0000
#define HW_ATL2_LT_CTRL_AVB_LEN_CMP_TRSHLD_SHIFT 16

#define HW_ATL2_LT_CTRL_CLK_RATIO_MSK 0x0000FF00
#define HW_ATL2_LT_CTRL_CLK_RATIO_SHIFT 8
#define HW_ATL2_LT_CTRL_CLK_RATIO_QUATER_SPEED 4
#define HW_ATL2_LT_CTRL_CLK_RATIO_HALF_SPEED 2
#define HW_ATL2_LT_CTRL_CLK_RATIO_FULL_SPEED 1

#define HW_ATL2_LT_CTRL_25G_MODE_SUPPORT_MSK 0x00000008
#define HW_ATL2_LT_CTRL_25G_MODE_SUPPORT_SHIFT 3

#define HW_ATL2_LT_CTRL_LINK_SPEED_MSK 0x00000007
#define HW_ATL2_LT_CTRL_LINK_SPEED_SHIFT 0

/* FPGA VER register */
#define HW_ATL2_FPGA_VER_ADR 0x000000f4
#define HW_ATL2_FPGA_VER_U32(mj, mi, bl, rv) \
	((((mj) & 0xff) << 24) | \
	 (((mi) & 0xff) << 16) | \
	 (((bl) & 0xff) << 8) | \
	 (((rv) & 0xff) << 0))

/* ahb_mem_addr{f}[31:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "ahb_mem_addr{f}[31:0]".
 * Parameter: filter {f} | stride size 0x10 | range [0, 127]
 * PORT="ahb_mem_addr{f}[31:0]"
 */

/* Register address for bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_REQ_TAG_ADR(filter) \
	(0x00014000u + (filter) * 0x10)
/* Bitmask for bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_REQ_TAG_MSK 0xFFFFFFFFu
/* Inverted bitmask for bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_REQ_TAG_MSKN 0x00000000u
/* Lower bit position of bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_REQ_TAG_SHIFT 0
/* Width of bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_REQ_TAG_WIDTH 31
/* Default value of bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_REQ_TAG_DEFAULT 0x0

/* Register address for bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_TAG_MASK_ADR(filter) \
	(0x00014004u + (filter) * 0x10)
/* Bitmask for bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_TAG_MASK_MSK 0xFFFFFFFFu
/* Inverted bitmask for bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_TAG_MASK_MSKN 0x00000000u
/* Lower bit position of bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_TAG_MASK_SHIFT 0
/* Width of bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_TAG_MASK_WIDTH 31
/* Default value of bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_TAG_MASK_DEFAULT 0x0

/* Register address for bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_ACTN_ADR(filter) \
	(0x00014008u + (filter) * 0x10)
/* Bitmask for bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_ACTN_MSK 0x000007FFu
/* Inverted bitmask for bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_ACTN_MSKN 0xFFFFF800u
/* Lower bit position of bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_ACTN_SHIFT 0
/* Width of bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_ACTN_WIDTH 10
/* Default value of bitfield ahb_mem_addr{f}[31:0] */
#define HW_ATL2_RPF_ACT_RSLVR_ACTN_DEFAULT 0x0

/* rpf_rec_tab_en[15:0] Bitfield Definitions
 * Preprocessor definitions for the bitfield "rpf_rec_tab_en[15:0]".
 * PORT="pif_rpf_rec_tab_en[15:0]"
 */
/* Register address for bitfield rpf_rec_tab_en[15:0] */
#define HW_ATL2_RPF_REC_TAB_EN_ADR 0x00006ff0u
/* Bitmask for bitfield rpf_rec_tab_en[15:0] */
#define HW_ATL2_RPF_REC_TAB_EN_MSK 0x0000FFFFu
/* Inverted bitmask for bitfield rpf_rec_tab_en[15:0] */
#define HW_ATL2_RPF_REC_TAB_EN_MSKN 0xFFFF0000u
/* Lower bit position of bitfield rpf_rec_tab_en[15:0] */
#define HW_ATL2_RPF_REC_TAB_EN_SHIFT 0
/* Width of bitfield rpf_rec_tab_en[15:0] */
#define HW_ATL2_RPF_REC_TAB_EN_WIDTH 16
/* Default value of bitfield rpf_rec_tab_en[15:0] */
#define HW_ATL2_RPF_REC_TAB_EN_DEFAULT 0x0

/* Register address for firmware shared input buffer */
#define HW_ATL2_MIF_SHARED_BUFFER_IN_ADR(dword) (0x00012000U + (dword) * 0x4U)
/* Register address for firmware shared output buffer */
#define HW_ATL2_MIF_SHARED_BUFFER_OUT_ADR(dword) (0x00013000U + (dword) * 0x4U)

/* pif_host_finished_buf_wr_i Bitfield Definitions
 * Preprocessor definitions for the bitfield "pif_host_finished_buf_wr_i".
 * PORT="pif_host_finished_buf_wr_i"
 */
/* Register address for bitfield rpif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_ADR 0x00000e00u
/* Bitmask for bitfield pif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_MSK 0x00000001u
/* Inverted bitmask for bitfield pif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_MSKN 0xFFFFFFFEu
/* Lower bit position of bitfield pif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_SHIFT 0
/* Width of bitfield pif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_WIDTH 1
/* Default value of bitfield pif_host_finished_buf_wr_i */
#define HW_ATL2_MIF_HOST_FINISHED_WRITE_DEFAULT 0x0

/* pif_mcp_finished_buf_rd_i Bitfield Definitions
 * Preprocessor definitions for the bitfield "pif_mcp_finished_buf_rd_i".
 * PORT="pif_mcp_finished_buf_rd_i"
 */
/* Register address for bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_ADR 0x00000e04u
/* Bitmask for bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_MSK 0x00000001u
/* Inverted bitmask for bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_MSKN 0xFFFFFFFEu
/* Lower bit position of bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_SHIFT 0
/* Width of bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_WIDTH 1
/* Default value of bitfield pif_mcp_finished_buf_rd_i */
#define HW_ATL2_MIF_MCP_FINISHED_READ_DEFAULT 0x0

/* Register address for bitfield pif_mcp_boot_reg */
#define HW_ATL2_MIF_BOOT_REG_ADR 0x00003040u

#define HW_ATL2_MCP_HOST_REQ_INT_READY BIT(0)

#define HW_ATL2_MCP_HOST_REQ_INT_ADR 0x00000F00u
#define HW_ATL2_MCP_HOST_REQ_INT_SET_ADR 0x00000F04u
#define HW_ATL2_MCP_HOST_REQ_INT_CLR_ADR 0x00000F08u
/* Register address for bitfield PTP EXT GPIO TS SEL */
#define HW_ATL2_TSG0_EXT_GPIO_TS_INPUT_SEL_ADR 0x00003664
/* Bitmask for bitfield PTP EXT GPIO TS SEL */
#define HW_ATL2_TSG0_EXT_GPIO_TS_INPUT_SEL_MSK 0x00001F00
/* Lower bit position of bitfield PTP EXT GPIO TS SEL */
#define HW_ATL2_TSG0_EXT_GPIO_TS_INPUT_SEL_SHIFT 8
/* Register address for bitfield TSG EXT GPIO TS SEL */
#define HW_ATL2_TSG1_EXT_GPIO_TS_INPUT_SEL_ADR 0x00003660
/* Bitmask for bitfield TSG EXT GPIO TS SEL */
#define HW_ATL2_TSG1_EXT_GPIO_TS_INPUT_SEL_MSK 0x00001F00
/* Lower bit position of bitfield TSG EXT GPIO TS SEL */
#define HW_ATL2_TSG1_EXT_GPIO_TS_INPUT_SEL_SHIFT 8
/* Register address for bitfield GPIO{P} Special Mode */
#define HW_ATL2_GPIO_PIN_SPEC_MODE_ADR(pin) (0x00003698 + (pin) * 0x4)
/* Bitmask for bitfield GPIO{P} Special Mode */
#define HW_ATL2_GPIO_PIN_SPEC_MODE_MSK 0x0000000C
/* Lower bit position of bitfield GPIO{P} Special Mode */
#define HW_ATL2_GPIO_PIN_SPEC_MODE_SHIFT 2
#define HW_ATL2_GPIO_PIN_SPEC_MODE_TSG1_EVENT_OUTPUT 0
#define HW_ATL2_GPIO_PIN_SPEC_MODE_TSG0_EVENT_OUTPUT 2
#define HW_ATL2_GPIO_PIN_SPEC_MODE_GPIO 3
#endif /* HW_ATL2_LLH_INTERNAL_H */
