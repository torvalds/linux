// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include "hw_atl2_llh.h"
#include "hw_atl2_llh_internal.h"
#include "aq_hw_utils.h"

u32 hw_atl2_phi_ext_tag_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL2_PHI_EXT_TAG_EN_ADR,
				  HW_ATL2_PHI_EXT_TAG_EN_MSK,
				  HW_ATL2_PHI_EXT_TAG_EN_SHIFT);
}

void hw_atl2_rpf_redirection_table2_select_set(struct aq_hw_s *aq_hw,
					       u32 select)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_PIF_RPF_REDIR2_ENI_ADR,
			    HW_ATL2_RPF_PIF_RPF_REDIR2_ENI_MSK,
			    HW_ATL2_RPF_PIF_RPF_REDIR2_ENI_SHIFT, select);
}

void hw_atl2_rpf_rss_hash_type_set(struct aq_hw_s *aq_hw, u32 rss_hash_type)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_PIF_RPF_RSS_HASH_TYPEI_ADR,
			    HW_ATL2_RPF_PIF_RPF_RSS_HASH_TYPEI_MSK,
			    HW_ATL2_RPF_PIF_RPF_RSS_HASH_TYPEI_SHIFT,
			    rss_hash_type);
}

/* rpf */

void hw_atl2_rpf_new_enable_set(struct aq_hw_s *aq_hw, u32 enable)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_NEW_EN_ADR,
			    HW_ATL2_RPF_NEW_EN_MSK,
			    HW_ATL2_RPF_NEW_EN_SHIFT,
			    enable);
}

void hw_atl2_rpfl2_uc_flr_tag_set(struct aq_hw_s *aq_hw, u32 tag, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPFL2UC_TAG_ADR(filter),
			    HW_ATL2_RPFL2UC_TAG_MSK,
			    HW_ATL2_RPFL2UC_TAG_SHIFT,
			    tag);
}

void hw_atl2_rpfl2_bc_flr_tag_set(struct aq_hw_s *aq_hw, u32 tag)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_L2_BC_TAG_ADR,
			    HW_ATL2_RPF_L2_BC_TAG_MSK,
			    HW_ATL2_RPF_L2_BC_TAG_SHIFT,
			    tag);
}

void hw_atl2_new_rpf_rss_redir_set(struct aq_hw_s *aq_hw, u32 tc, u32 index,
				   u32 queue)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_RSS_REDIR_ADR(tc, index),
			    HW_ATL2_RPF_RSS_REDIR_MSK(tc),
			    HW_ATL2_RPF_RSS_REDIR_SHIFT(tc),
			    queue);
}

void hw_atl2_rpf_vlan_flr_tag_set(struct aq_hw_s *aq_hw, u32 tag, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_VL_TAG_ADR(filter),
			    HW_ATL2_RPF_VL_TAG_MSK,
			    HW_ATL2_RPF_VL_TAG_SHIFT,
			    tag);
}

void hw_atl2_rpf_etht_flr_tag_set(struct aq_hw_s *aq_hw, u32 tag, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_ET_TAG_ADR(filter),
			    HW_ATL2_RPF_ET_TAG_MSK,
			    HW_ATL2_RPF_ET_TAG_SHIFT, tag);
}

u32 hw_atl2_rpf_etht_flr_tag_get(struct aq_hw_s *aq_hw, u32 filter)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL2_RPF_ET_TAG_ADR(filter),
				  HW_ATL2_RPF_ET_TAG_MSK,
				  HW_ATL2_RPF_ET_TAG_SHIFT);
}

void hw_atl2_rpf_l3_v4_dest_addr_set(struct aq_hw_s *aq_hw, u32 filter, u32 val)
{
	u32 addr_set = 6 + ((filter < 4) ? 0 : 1);
	u32 dword = filter % 4;

	aq_hw_write_reg(aq_hw, HW_ATL2_RPF_L3_DA_DW_ADR(addr_set, dword), val);
}

void hw_atl2_rpf_l3_v4_src_addr_set(struct aq_hw_s *aq_hw, u32 filter, u32 val)
{
	u32 addr_set = 6 + ((filter < 4) ? 0 : 1);
	u32 dword = filter % 4;

	aq_hw_write_reg(aq_hw, HW_ATL2_RPF_L3_SA_DW_ADR(addr_set, dword), val);
}

void hw_atl2_rpf_l3_v6_dest_addr_set(struct aq_hw_s *aq_hw, u8 location,
				     u32 *ipv6_dst)
{
	int i;

	for (i = 0; i < 4; ++i)
		aq_hw_write_reg(aq_hw,
				HW_ATL2_RPF_L3_DA_DW_ADR(location, 3 - i),
				ipv6_dst[i]);
}

void hw_atl2_rpf_l3_v6_src_addr_set(struct aq_hw_s *aq_hw, u8 location,
				    u32 *ipv6_src)
{
	int i;

	for (i = 0; i < 4; ++i)
		aq_hw_write_reg(aq_hw,
				HW_ATL2_RPF_L3_SA_DW_ADR(location, 3 - i),
				ipv6_src[i]);
}

void hw_atl2_rpf_l3_v4_cmd_set(struct aq_hw_s *aq_hw, u32 val, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_L3_V4_CMD_ADR(filter),
			    HW_ATL2_RPF_L3_V4_CMD_MSK,
			    HW_ATL2_RPF_L3_V4_CMD_SHIFT, val);
}

void hw_atl2_rpf_l3_v6_cmd_set(struct aq_hw_s *aq_hw, u32 val, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_L3_V6_CMD_ADR(filter),
			    HW_ATL2_RPF_L3_V6_CMD_MSK,
			    HW_ATL2_RPF_L3_V6_CMD_SHIFT, val);
}

void hw_atl2_rpf_l3_v6_v4_select_set(struct aq_hw_s *aq_hw, u32 val)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_L3_V6_V4_SELECT_ADR,
			    HW_ATL2_RPF_L3_V6_V4_SELECT_MSK,
			    HW_ATL2_RPF_L3_V6_V4_SELECT_SHIFT, val);
}

void hw_atl2_rpf_l3_v4_tag_set(struct aq_hw_s *aq_hw, u32 val, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_L3_V4_TAG_ADR(filter),
			    HW_ATL2_RPF_L3_V4_TAG_MSK,
			    HW_ATL2_RPF_L3_V4_TAG_SHIFT, val);
}

void hw_atl2_rpf_l3_v6_tag_set(struct aq_hw_s *aq_hw, u32 val, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_L3_V6_TAG_ADR(filter),
			    HW_ATL2_RPF_L3_V6_TAG_MSK,
			    HW_ATL2_RPF_L3_V6_TAG_SHIFT, val);
}

void hw_atl2_rpf_l4_tag_set(struct aq_hw_s *aq_hw, u32 val, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_L4_TAG_ADR(filter),
			    HW_ATL2_RPF_L4_TAG_MSK,
			    HW_ATL2_RPF_L4_TAG_SHIFT, val);
}

void hw_atl2_rpf_l4_cmd_set(struct aq_hw_s *aq_hw, u32 val, u32 filter)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_L4_CMD_ADR(filter),
			    HW_ATL2_RPF_L4_CMD_MSK,
			    HW_ATL2_RPF_L4_CMD_SHIFT, val);
}

/* tsg */
static void hw_atl2_clock_modif_value_set(struct aq_hw_s *aq_hw,
					  u32 clock_sel, u64 ns)
{
	aq_hw_write_reg64(aq_hw,
			  HW_ATL2_TSG_REG_ADR(clock_sel, CLOCK_MODIF_VAL_LSW),
			  ns);
}

void hw_atl2_tsg_clock_en(struct aq_hw_s *aq_hw,
			  u32 clock_sel, u32 clock_enable)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TSG_REG_ADR(clock_sel, CLOCK_CFG),
			    HW_ATL2_TSG_CLOCK_EN_MSK,
			    HW_ATL2_TSG_CLOCK_EN_SHIFT,
			    clock_enable);
}

void hw_atl2_tsg_clock_reset(struct aq_hw_s *aq_hw, u32 clock_sel)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TSG_REG_ADR(clock_sel, CLOCK_CFG),
			    HW_ATL2_TSG_SYNC_RESET_MSK,
			    HW_ATL2_TSG_SYNC_RESET_SHIFT, 1);
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TSG_REG_ADR(clock_sel, CLOCK_CFG),
			    HW_ATL2_TSG_SYNC_RESET_MSK,
			    HW_ATL2_TSG_SYNC_RESET_SHIFT, 0);
}

u64 hw_atl2_tsg_clock_read(struct aq_hw_s *aq_hw, u32 clock_sel)
{
	return aq_hw_read_reg64(aq_hw,
				HW_ATL2_TSG_REG_ADR(clock_sel,
						    READ_CUR_NS_LSW));
}

void hw_atl2_tsg_clock_add(struct aq_hw_s *aq_hw, u32 clock_sel, u64 ns)
{
	hw_atl2_clock_modif_value_set(aq_hw, clock_sel, ns);
	aq_hw_write_reg(aq_hw,
			HW_ATL2_TSG_REG_ADR(clock_sel, CLOCK_MODIF_CTRL),
			HW_ATL2_TSG_ADD_COUNTER_MSK);
}

void hw_atl2_tsg_clock_sub(struct aq_hw_s *aq_hw, u32 clock_sel, u64 ns)
{
	hw_atl2_clock_modif_value_set(aq_hw, clock_sel, ns);
	aq_hw_write_reg(aq_hw,
			HW_ATL2_TSG_REG_ADR(clock_sel, CLOCK_MODIF_CTRL),
			HW_ATL2_TSG_SUBTRACT_COUNTER_MSK);
}

void hw_atl2_tsg_clock_increment_set(struct aq_hw_s *aq_hw,
				     u32 clock_sel, u32 ns, u32 fns)
{
	u32 nsfns = (ns & 0xff) | (fns & 0xffffff00);

	aq_hw_write_reg(aq_hw,
			HW_ATL2_TSG_REG_ADR(clock_sel, CLOCK_INC_CFG),
			nsfns);
	aq_hw_write_reg(aq_hw,
			HW_ATL2_TSG_REG_ADR(clock_sel, CLOCK_MODIF_CTRL),
			HW_ATL2_TSG_LOAD_INC_CFG_MSK);
}

void hw_atl2_tpb_tps_highest_priority_tc_enable_set(struct aq_hw_s *aq_hw,
						    u32 tps_highest_prio_tc_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TPB_HIGHEST_PRIO_TC_EN_ADR,
			    HW_ATL2_TPB_HIGHEST_PRIO_TC_EN_MSK,
			    HW_ATL2_TPB_HIGHEST_PRIO_TC_EN_SHIFT,
			    tps_highest_prio_tc_en);
}

void hw_atl2_tpb_tps_highest_priority_tc_set(struct aq_hw_s *aq_hw,
					     u32 tps_highest_prio_tc)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TPB_HIGHEST_PRIO_TC_ADR,
			    HW_ATL2_TPB_HIGHEST_PRIO_TC_MSK,
			    HW_ATL2_TPB_HIGHEST_PRIO_TC_SHIFT,
			    tps_highest_prio_tc);
}

void hw_atl2_tsg_gpio_isr_to_host_set(struct aq_hw_s *aq_hw,
				      int on, u32 clock_sel)
{
	aq_hw_write_reg_bit(aq_hw,
			    HW_ATL2_GLOBAL_HIGH_PRIO_INTERRUPT_1_MASK_ADR,
		clock_sel == 1 ? HW_ATL2_TSG_TSG1_GPIO_INTERRUPT_MSK :
			HW_ATL2_TSG_TSG0_GPIO_INTERRUPT_MSK,
		clock_sel == 1 ? HW_ATL2_TSG_TSG1_GPIO_INTERRUPT_SHIFT :
			HW_ATL2_TSG_TSG0_GPIO_INTERRUPT_SHIFT,
		!!on);
}

void hw_atl2_tsg_gpio_clear_status(struct aq_hw_s *aq_hw)
{
	aq_hw_read_reg(aq_hw, HW_ATL2_GLOBAL_INTERNAL_ALARMS_1_ADR);
}

void hw_atl2_tsg_gpio_input_event_info_get(struct aq_hw_s *aq_hw,
					   u32 clock_sel,
					   u32 *event_count,
					   u64 *event_ts)
{
	if (event_count)
		*event_count = aq_hw_read_reg(aq_hw,
					      HW_ATL2_TSG_REG_ADR(clock_sel,
								  EXT_CLK_COUNT));

	if (event_ts)
		*event_ts = aq_hw_read_reg64(aq_hw,
					     HW_ATL2_TSG_REG_ADR(clock_sel,
								 GPIO_EVENT_TS_LSW));
}

void hw_atl2_tsg_ptp_gpio_gen_pulse(struct aq_hw_s *aq_hw, u32 clk_sel,
				    u64 ts, u32 period, u32 hightime)
{
	u32 val = (HW_ATL2_TSG_GPIO_EVENT_MODE_SET_ON_TIME <<
		   (HW_ATL2_TSG_GPIO_EVENT_MODE_SHIFT -
		    HW_ATL2_TSG_GPIO_OUTPUT_EN_SHIFT)) |
		  (HW_ATL2_TSG_GPIO_GEN_OUTPUT_EN_MSK) |
		  (HW_ATL2_TSG_GPIO_OUTPUT_EN_MSK);

	if (ts != 0) {
		aq_hw_write_reg64(aq_hw,
				  HW_ATL2_TSG_REG_ADR(clk_sel,
						      GPIO_EVENT_GEN_TS_LSW),
				  ts);

		aq_hw_write_reg64(aq_hw,
				  HW_ATL2_TSG_REG_ADR(clk_sel,
						      GPIO_EVENT_HIGH_TIME_LSW),
				  hightime);

		aq_hw_write_reg64(aq_hw,
				  HW_ATL2_TSG_REG_ADR(clk_sel,
						      GPIO_EVENT_LOW_TIME_LSW),
				  (period - hightime));
	}

	aq_hw_write_reg_bit(aq_hw,
			    HW_ATL2_TSG_REG_ADR(clk_sel, GPIO_EVENT_GEN_CFG),
			    HW_ATL2_TSG_GPIO_EVENT_MODE_MSK |
				HW_ATL2_TSG_GPIO_OUTPUT_EN_MSK |
				HW_ATL2_TSG_GPIO_GEN_OUTPUT_EN_MSK,
			   HW_ATL2_TSG_GPIO_OUTPUT_EN_SHIFT,
			   (!ts ? 0 : val));
}

void hw_atl2_rpf_rx_desc_timestamp_req_set(struct aq_hw_s *aq_hw, u32 request,
					   u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw,
			    HW_ATL2_RPF_TIMESTAMP_REQ_DESCD_ADR(descriptor),
			    HW_ATL2_RPF_TIMESTAMP_REQ_DESCD_MSK,
			    HW_ATL2_RPF_TIMESTAMP_REQ_DESCD_SHIFT, request);
}

/* TX */

void hw_atl2_tpb_tx_tc_q_rand_map_en_set(struct aq_hw_s *aq_hw,
					 const u32 tc_q_rand_map_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TPB_TX_TC_Q_RAND_MAP_EN_ADR,
			    HW_ATL2_TPB_TX_TC_Q_RAND_MAP_EN_MSK,
			    HW_ATL2_TPB_TX_TC_Q_RAND_MAP_EN_SHIFT,
			    tc_q_rand_map_en);
}

void hw_atl2_tpb_tx_buf_clk_gate_en_set(struct aq_hw_s *aq_hw, u32 clk_gate_en)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TPB_TX_BUF_CLK_GATE_EN_ADR,
			    HW_ATL2_TPB_TX_BUF_CLK_GATE_EN_MSK,
			    HW_ATL2_TPB_TX_BUF_CLK_GATE_EN_SHIFT,
			    clk_gate_en);
}

void hw_atl2_reg_tx_intr_moder_ctrl_set(struct aq_hw_s *aq_hw,
					u32 tx_intr_moderation_ctl,
					u32 queue)
{
	aq_hw_write_reg(aq_hw, HW_ATL2_TX_INTR_MODERATION_CTL_ADR(queue),
			tx_intr_moderation_ctl);
}

void hw_atl2_tdm_tx_desc_timestamp_writeback_en_set(struct aq_hw_s *aq_hw,
						    u32 enable, u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TDM_DESCD_TS_WRB_EN_ADR(descriptor),
			    HW_ATL2_TDM_DESCD_TS_WRB_EN_MSK,
			    HW_ATL2_TDM_DESCD_TS_WRB_EN_SHIFT, enable);
}

void hw_atl2_tdm_tx_desc_timestamp_en_set(struct aq_hw_s *aq_hw, u32 enable,
					  u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TDM_DESCD_TS_EN_ADR(descriptor),
			    HW_ATL2_TDM_DESCD_TS_EN_MSK,
			    HW_ATL2_TDM_DESCD_TS_EN_SHIFT, enable);
}

void hw_atl2_tdm_tx_desc_avb_en_set(struct aq_hw_s *aq_hw, u32 enable,
				    u32 descriptor)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TDM_DESCD_AVB_EN_ADR(descriptor),
			    HW_ATL2_TDM_DESCD_AVB_EN_MSK,
			    HW_ATL2_TDM_DESCD_AVB_EN_SHIFT, enable);
}

void hw_atl2_tps_tx_pkt_shed_data_arb_mode_set(struct aq_hw_s *aq_hw,
					       const u32 data_arb_mode)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TPS_DATA_TC_ARB_MODE_ADR,
			    HW_ATL2_TPS_DATA_TC_ARB_MODE_MSK,
			    HW_ATL2_TPS_DATA_TC_ARB_MODE_SHIFT,
			    data_arb_mode);
}

void hw_atl2_tps_tx_pkt_shed_tc_data_max_credit_set(struct aq_hw_s *aq_hw,
						    const u32 tc,
						    const u32 max_credit)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TPS_DATA_TCTCREDIT_MAX_ADR(tc),
			    HW_ATL2_TPS_DATA_TCTCREDIT_MAX_MSK,
			    HW_ATL2_TPS_DATA_TCTCREDIT_MAX_SHIFT,
			    max_credit);
}

void hw_atl2_tps_tx_pkt_shed_tc_data_weight_set(struct aq_hw_s *aq_hw,
						const u32 tc,
						const u32 weight)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TPS_DATA_TCTWEIGHT_ADR(tc),
			    HW_ATL2_TPS_DATA_TCTWEIGHT_MSK,
			    HW_ATL2_TPS_DATA_TCTWEIGHT_SHIFT,
			    weight);
}

void hw_atl2_tdm_tx_data_read_req_limit_set(struct aq_hw_s *aq_hw, u32 limit)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TDM_TX_DATA_RD_REQ_LIMIT_ADR,
			    HW_ATL2_TDM_TX_DATA_RD_REQ_LIMIT_MSK,
			    HW_ATL2_TDM_TX_DATA_RD_REQ_LIMIT_SHIFT, limit);
}

void hw_atl2_tdm_tx_desc_read_req_limit_set(struct aq_hw_s *aq_hw, u32 limit)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_TDM_TX_DESC_RD_REQ_LIMIT_ADR,
			    HW_ATL2_TDM_TX_DESC_RD_REQ_LIMIT_MSK,
			    HW_ATL2_TDM_TX_DESC_RD_REQ_LIMIT_SHIFT, limit);
}

u32 hw_atl2_get_hw_version(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, HW_ATL2_FPGA_VER_ADR);
}

void hw_atl2_init_launchtime(struct aq_hw_s *aq_hw)
{
	u32 hw_ver = hw_atl2_get_hw_version(aq_hw);

	aq_hw_write_reg_bit(aq_hw, HW_ATL2_LT_CTRL_ADR,
			    HW_ATL2_LT_CTRL_CLK_RATIO_MSK,
			    HW_ATL2_LT_CTRL_CLK_RATIO_SHIFT,
			    hw_ver  < HW_ATL2_FPGA_VER_U32(1, 0, 0, 0) ?
			    HW_ATL2_LT_CTRL_CLK_RATIO_FULL_SPEED :
			    hw_ver >= HW_ATL2_FPGA_VER_U32(1, 0, 85, 2) ?
			    HW_ATL2_LT_CTRL_CLK_RATIO_HALF_SPEED :
			    HW_ATL2_LT_CTRL_CLK_RATIO_QUATER_SPEED);
}

/* set action resolver record */
void hw_atl2_rpf_act_rslvr_record_set(struct aq_hw_s *aq_hw, u8 location,
				      u32 tag, u32 mask, u32 action)
{
	aq_hw_write_reg(aq_hw,
			HW_ATL2_RPF_ACT_RSLVR_REQ_TAG_ADR(location),
			tag);
	aq_hw_write_reg(aq_hw,
			HW_ATL2_RPF_ACT_RSLVR_TAG_MASK_ADR(location),
			mask);
	aq_hw_write_reg(aq_hw,
			HW_ATL2_RPF_ACT_RSLVR_ACTN_ADR(location),
			action);
}

void hw_atl2_rpf_act_rslvr_section_en_set(struct aq_hw_s *aq_hw, u32 sections)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_RPF_REC_TAB_EN_ADR,
			    HW_ATL2_RPF_REC_TAB_EN_MSK,
			    HW_ATL2_RPF_REC_TAB_EN_SHIFT,
			    sections);
}

u32 hw_atl2_rpf_act_rslvr_section_en_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL2_RPF_REC_TAB_EN_ADR,
				  HW_ATL2_RPF_REC_TAB_EN_MSK,
				  HW_ATL2_RPF_REC_TAB_EN_SHIFT);
}

void hw_atl2_mif_shared_buf_get(struct aq_hw_s *aq_hw, int offset, u32 *data,
				int len)
{
	int j = 0;
	int i;

	for (i = offset; i < offset + len; i++, j++)
		data[j] = aq_hw_read_reg(aq_hw,
					 HW_ATL2_MIF_SHARED_BUFFER_IN_ADR(i));
}

void hw_atl2_mif_shared_buf_write(struct aq_hw_s *aq_hw, int offset, u32 *data,
				  int len)
{
	int j = 0;
	int i;

	for (i = offset; i < offset + len; i++, j++)
		aq_hw_write_reg(aq_hw, HW_ATL2_MIF_SHARED_BUFFER_IN_ADR(i),
				data[j]);
}

void hw_atl2_mif_shared_buf_read(struct aq_hw_s *aq_hw, int offset, u32 *data,
				 int len)
{
	int j = 0;
	int i;

	for (i = offset; i < offset + len; i++, j++)
		data[j] = aq_hw_read_reg(aq_hw,
					 HW_ATL2_MIF_SHARED_BUFFER_OUT_ADR(i));
}

void hw_atl2_mif_host_finished_write_set(struct aq_hw_s *aq_hw, u32 finish)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_MIF_HOST_FINISHED_WRITE_ADR,
			    HW_ATL2_MIF_HOST_FINISHED_WRITE_MSK,
			    HW_ATL2_MIF_HOST_FINISHED_WRITE_SHIFT,
			    finish);
}

u32 hw_atl2_mif_mcp_finished_read_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg_bit(aq_hw, HW_ATL2_MIF_MCP_FINISHED_READ_ADR,
				  HW_ATL2_MIF_MCP_FINISHED_READ_MSK,
				  HW_ATL2_MIF_MCP_FINISHED_READ_SHIFT);
}

u32 hw_atl2_mif_mcp_boot_reg_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, HW_ATL2_MIF_BOOT_REG_ADR);
}

void hw_atl2_mif_mcp_boot_reg_set(struct aq_hw_s *aq_hw, u32 val)
{
	return aq_hw_write_reg(aq_hw, HW_ATL2_MIF_BOOT_REG_ADR, val);
}

u32 hw_atl2_mif_host_req_int_get(struct aq_hw_s *aq_hw)
{
	return aq_hw_read_reg(aq_hw, HW_ATL2_MCP_HOST_REQ_INT_ADR);
}

void hw_atl2_mif_host_req_int_clr(struct aq_hw_s *aq_hw, u32 val)
{
	return aq_hw_write_reg(aq_hw, HW_ATL2_MCP_HOST_REQ_INT_CLR_ADR,
			       val);
}

void hw_atl2_gpio_special_mode_set(struct aq_hw_s *aq_hw,
				   u32 gpio_special_mode,
				   u32 pin)
{
	aq_hw_write_reg_bit(aq_hw, HW_ATL2_GPIO_PIN_SPEC_MODE_ADR(pin),
			    HW_ATL2_GPIO_PIN_SPEC_MODE_MSK,
			    HW_ATL2_GPIO_PIN_SPEC_MODE_SHIFT,
			    gpio_special_mode);
}
