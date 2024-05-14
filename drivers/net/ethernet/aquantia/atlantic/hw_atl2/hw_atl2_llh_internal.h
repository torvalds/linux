/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef HW_ATL2_LLH_INTERNAL_H
#define HW_ATL2_LLH_INTERNAL_H

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

#endif /* HW_ATL2_LLH_INTERNAL_H */
