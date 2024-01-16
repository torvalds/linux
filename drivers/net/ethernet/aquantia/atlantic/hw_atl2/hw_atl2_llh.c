// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include "hw_atl2_llh.h"
#include "hw_atl2_llh_internal.h"
#include "aq_hw_utils.h"

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
