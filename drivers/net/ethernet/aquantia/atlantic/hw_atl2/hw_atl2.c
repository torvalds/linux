// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include "aq_hw.h"
#include "aq_hw_utils.h"
#include "aq_ring.h"
#include "aq_nic.h"
#include "hw_atl/hw_atl_b0.h"
#include "hw_atl/hw_atl_utils.h"
#include "hw_atl/hw_atl_llh.h"
#include "hw_atl/hw_atl_llh_internal.h"
#include "hw_atl2_utils.h"
#include "hw_atl2_llh.h"
#include "hw_atl2_internal.h"
#include "hw_atl2_llh_internal.h"

static int hw_atl2_act_rslvr_table_set(struct aq_hw_s *self, u8 location,
				       u32 tag, u32 mask, u32 action);

#define DEFAULT_BOARD_BASIC_CAPABILITIES \
	.is_64_dma = true,		  \
	.op64bit = true,		  \
	.msix_irqs = 8U,		  \
	.irq_mask = ~0U,		  \
	.vecs = HW_ATL2_RSS_MAX,	  \
	.tcs_max = HW_ATL2_TC_MAX,	  \
	.rxd_alignment = 1U,		  \
	.rxd_size = HW_ATL2_RXD_SIZE,   \
	.rxds_max = HW_ATL2_MAX_RXD,    \
	.rxds_min = HW_ATL2_MIN_RXD,    \
	.txd_alignment = 1U,		  \
	.txd_size = HW_ATL2_TXD_SIZE,   \
	.txds_max = HW_ATL2_MAX_TXD,    \
	.txds_min = HW_ATL2_MIN_TXD,    \
	.txhwb_alignment = 4096U,	  \
	.tx_rings = HW_ATL2_TX_RINGS,   \
	.rx_rings = HW_ATL2_RX_RINGS,   \
	.hw_features = NETIF_F_HW_CSUM |  \
			NETIF_F_RXCSUM |  \
			NETIF_F_RXHASH |  \
			NETIF_F_SG |      \
			NETIF_F_TSO |     \
			NETIF_F_TSO6 |    \
			NETIF_F_LRO |     \
			NETIF_F_NTUPLE |  \
			NETIF_F_HW_VLAN_CTAG_FILTER | \
			NETIF_F_HW_VLAN_CTAG_RX |     \
			NETIF_F_HW_VLAN_CTAG_TX |     \
			NETIF_F_GSO_UDP_L4      |     \
			NETIF_F_GSO_PARTIAL     |     \
			NETIF_F_HW_TC,                \
	.hw_priv_flags = IFF_UNICAST_FLT, \
	.flow_control = true,		  \
	.mtu = HW_ATL2_MTU_JUMBO,	  \
	.mac_regs_count = 72,		  \
	.hw_alive_check_addr = 0x10U,     \
	.priv_data_len = sizeof(struct hw_atl2_priv)

const struct aq_hw_caps_s hw_atl2_caps_aqc113 = {
	DEFAULT_BOARD_BASIC_CAPABILITIES,
	.media_type = AQ_HW_MEDIA_TYPE_TP,
	.link_speed_msk = AQ_NIC_RATE_10G |
			  AQ_NIC_RATE_5G  |
			  AQ_NIC_RATE_2G5 |
			  AQ_NIC_RATE_1G  |
			  AQ_NIC_RATE_1G_HALF   |
			  AQ_NIC_RATE_100M      |
			  AQ_NIC_RATE_100M_HALF |
			  AQ_NIC_RATE_10M       |
			  AQ_NIC_RATE_10M_HALF,
};

static u32 hw_atl2_sem_act_rslvr_get(struct aq_hw_s *self)
{
	return hw_atl_reg_glb_cpu_sem_get(self, HW_ATL2_FW_SM_ACT_RSLVR);
}

static int hw_atl2_hw_reset(struct aq_hw_s *self)
{
	struct hw_atl2_priv *priv = (struct hw_atl2_priv *)self->priv;
	int err;

	err = hw_atl2_utils_soft_reset(self);
	if (err)
		return err;

	memset(priv, 0, sizeof(*priv));

	self->aq_fw_ops->set_state(self, MPI_RESET);

	err = aq_hw_err_from_flags(self);

	return err;
}

static int hw_atl2_hw_queue_to_tc_map_set(struct aq_hw_s *self)
{
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;
	unsigned int tcs, q_per_tc;
	unsigned int tc, q;
	u32 rx_map = 0;
	u32 tx_map = 0;

	hw_atl2_tpb_tx_tc_q_rand_map_en_set(self, 1U);

	switch (cfg->tc_mode) {
	case AQ_TC_MODE_8TCS:
		tcs = 8;
		q_per_tc = 4;
		break;
	case AQ_TC_MODE_4TCS:
		tcs = 4;
		q_per_tc = 8;
		break;
	default:
		return -EINVAL;
	}

	for (tc = 0; tc != tcs; tc++) {
		unsigned int tc_q_offset = tc * q_per_tc;

		for (q = tc_q_offset; q != tc_q_offset + q_per_tc; q++) {
			rx_map |= tc << HW_ATL2_RX_Q_TC_MAP_SHIFT(q);
			if (HW_ATL2_RX_Q_TC_MAP_ADR(q) !=
			    HW_ATL2_RX_Q_TC_MAP_ADR(q + 1)) {
				aq_hw_write_reg(self,
						HW_ATL2_RX_Q_TC_MAP_ADR(q),
						rx_map);
				rx_map = 0;
			}

			tx_map |= tc << HW_ATL2_TX_Q_TC_MAP_SHIFT(q);
			if (HW_ATL2_TX_Q_TC_MAP_ADR(q) !=
			    HW_ATL2_TX_Q_TC_MAP_ADR(q + 1)) {
				aq_hw_write_reg(self,
						HW_ATL2_TX_Q_TC_MAP_ADR(q),
						tx_map);
				tx_map = 0;
			}
		}
	}

	return aq_hw_err_from_flags(self);
}

static int hw_atl2_hw_qos_set(struct aq_hw_s *self)
{
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;
	u32 tx_buff_size = HW_ATL2_TXBUF_MAX;
	u32 rx_buff_size = HW_ATL2_RXBUF_MAX;
	unsigned int prio = 0U;
	u32 tc = 0U;

	/* TPS Descriptor rate init */
	hw_atl_tps_tx_pkt_shed_desc_rate_curr_time_res_set(self, 0x0U);
	hw_atl_tps_tx_pkt_shed_desc_rate_lim_set(self, 0xA);

	/* TPS VM init */
	hw_atl_tps_tx_pkt_shed_desc_vm_arb_mode_set(self, 0U);

	tx_buff_size /= cfg->tcs;
	rx_buff_size /= cfg->tcs;
	for (tc = 0; tc < cfg->tcs; tc++) {
		u32 threshold = 0U;

		/* Tx buf size TC0 */
		hw_atl_tpb_tx_pkt_buff_size_per_tc_set(self, tx_buff_size, tc);

		threshold = (tx_buff_size * (1024 / 32U) * 66U) / 100U;
		hw_atl_tpb_tx_buff_hi_threshold_per_tc_set(self, threshold, tc);

		threshold = (tx_buff_size * (1024 / 32U) * 50U) / 100U;
		hw_atl_tpb_tx_buff_lo_threshold_per_tc_set(self, threshold, tc);

		/* QoS Rx buf size per TC */
		hw_atl_rpb_rx_pkt_buff_size_per_tc_set(self, rx_buff_size, tc);

		threshold = (rx_buff_size * (1024U / 32U) * 66U) / 100U;
		hw_atl_rpb_rx_buff_hi_threshold_per_tc_set(self, threshold, tc);

		threshold = (rx_buff_size * (1024U / 32U) * 50U) / 100U;
		hw_atl_rpb_rx_buff_lo_threshold_per_tc_set(self, threshold, tc);

		hw_atl_b0_set_fc(self, self->aq_nic_cfg->fc.req, tc);
	}

	/* QoS 802.1p priority -> TC mapping */
	for (prio = 0; prio < 8; ++prio)
		hw_atl_rpf_rpb_user_priority_tc_map_set(self, prio,
							cfg->prio_tc_map[prio]);

	/* ATL2 Apply ring to TC mapping */
	hw_atl2_hw_queue_to_tc_map_set(self);

	return aq_hw_err_from_flags(self);
}

static int hw_atl2_hw_rss_set(struct aq_hw_s *self,
			      struct aq_rss_parameters *rss_params)
{
	u8 *indirection_table = rss_params->indirection_table;
	const u32 num_tcs = aq_hw_num_tcs(self);
	u32 rpf_redir2_enable;
	int tc;
	int i;

	rpf_redir2_enable = num_tcs > 4 ? 1 : 0;

	hw_atl2_rpf_redirection_table2_select_set(self, rpf_redir2_enable);

	for (i = HW_ATL2_RSS_REDIRECTION_MAX; i--;) {
		for (tc = 0; tc != num_tcs; tc++) {
			hw_atl2_new_rpf_rss_redir_set(self, tc, i,
						      tc *
						      aq_hw_q_per_tc(self) +
						      indirection_table[i]);
		}
	}

	return aq_hw_err_from_flags(self);
}

static int hw_atl2_hw_init_tx_tc_rate_limit(struct aq_hw_s *self)
{
	static const u32 max_weight = BIT(HW_ATL2_TPS_DATA_TCTWEIGHT_WIDTH) - 1;
	/* Scale factor is based on the number of bits in fractional portion */
	static const u32 scale = BIT(HW_ATL_TPS_DESC_RATE_Y_WIDTH);
	static const u32 frac_msk = HW_ATL_TPS_DESC_RATE_Y_MSK >>
				    HW_ATL_TPS_DESC_RATE_Y_SHIFT;
	const u32 link_speed = self->aq_link_status.mbps;
	struct aq_nic_cfg_s *nic_cfg = self->aq_nic_cfg;
	unsigned long num_min_rated_tcs = 0;
	u32 tc_weight[AQ_CFG_TCS_MAX];
	u32 fixed_max_credit_4b;
	u32 fixed_max_credit;
	u8 min_rate_msk = 0;
	u32 sum_weight = 0;
	int tc;

	/* By default max_credit is based upon MTU (in unit of 64b) */
	fixed_max_credit = nic_cfg->aq_hw_caps->mtu / 64;
	/* in unit of 4b */
	fixed_max_credit_4b = nic_cfg->aq_hw_caps->mtu / 4;

	if (link_speed) {
		min_rate_msk = nic_cfg->tc_min_rate_msk &
			       (BIT(nic_cfg->tcs) - 1);
		num_min_rated_tcs = hweight8(min_rate_msk);
	}

	/* First, calculate weights where min_rate is specified */
	if (num_min_rated_tcs) {
		for (tc = 0; tc != nic_cfg->tcs; tc++) {
			if (!nic_cfg->tc_min_rate[tc]) {
				tc_weight[tc] = 0;
				continue;
			}

			tc_weight[tc] = (-1L + link_speed +
					 nic_cfg->tc_min_rate[tc] *
					 max_weight) /
					link_speed;
			tc_weight[tc] = min(tc_weight[tc], max_weight);
			sum_weight += tc_weight[tc];
		}
	}

	/* WSP, if min_rate is set for at least one TC.
	 * RR otherwise.
	 */
	hw_atl2_tps_tx_pkt_shed_data_arb_mode_set(self, min_rate_msk ? 1U : 0U);
	/* Data TC Arbiter takes precedence over Descriptor TC Arbiter,
	 * leave Descriptor TC Arbiter as RR.
	 */
	hw_atl_tps_tx_pkt_shed_desc_tc_arb_mode_set(self, 0U);

	hw_atl_tps_tx_desc_rate_mode_set(self, nic_cfg->is_qos ? 1U : 0U);

	for (tc = 0; tc != nic_cfg->tcs; tc++) {
		const u32 en = (nic_cfg->tc_max_rate[tc] != 0) ? 1U : 0U;
		const u32 desc = AQ_NIC_CFG_TCVEC2RING(nic_cfg, tc, 0);
		u32 weight, max_credit;

		hw_atl_tps_tx_pkt_shed_desc_tc_max_credit_set(self, tc,
							      fixed_max_credit);
		hw_atl_tps_tx_pkt_shed_desc_tc_weight_set(self, tc, 0x1E);

		if (num_min_rated_tcs) {
			weight = tc_weight[tc];

			if (!weight && sum_weight < max_weight)
				weight = (max_weight - sum_weight) /
					 (nic_cfg->tcs - num_min_rated_tcs);
			else if (!weight)
				weight = 0x640;

			max_credit = max(2 * weight, fixed_max_credit_4b);
		} else {
			weight = 0x640;
			max_credit = 0xFFF0;
		}

		hw_atl2_tps_tx_pkt_shed_tc_data_weight_set(self, tc, weight);
		hw_atl2_tps_tx_pkt_shed_tc_data_max_credit_set(self, tc,
							       max_credit);

		hw_atl_tps_tx_desc_rate_en_set(self, desc, en);

		if (en) {
			/* Nominal rate is always 10G */
			const u32 rate = 10000U * scale /
					 nic_cfg->tc_max_rate[tc];
			const u32 rate_int = rate >>
					     HW_ATL_TPS_DESC_RATE_Y_WIDTH;
			const u32 rate_frac = rate & frac_msk;

			hw_atl_tps_tx_desc_rate_x_set(self, desc, rate_int);
			hw_atl_tps_tx_desc_rate_y_set(self, desc, rate_frac);
		} else {
			/* A value of 1 indicates the queue is not
			 * rate controlled.
			 */
			hw_atl_tps_tx_desc_rate_x_set(self, desc, 1U);
			hw_atl_tps_tx_desc_rate_y_set(self, desc, 0U);
		}
	}
	for (tc = nic_cfg->tcs; tc != AQ_CFG_TCS_MAX; tc++) {
		const u32 desc = AQ_NIC_CFG_TCVEC2RING(nic_cfg, tc, 0);

		hw_atl_tps_tx_desc_rate_en_set(self, desc, 0U);
		hw_atl_tps_tx_desc_rate_x_set(self, desc, 1U);
		hw_atl_tps_tx_desc_rate_y_set(self, desc, 0U);
	}

	return aq_hw_err_from_flags(self);
}

static int hw_atl2_hw_init_tx_path(struct aq_hw_s *self)
{
	struct aq_nic_cfg_s *nic_cfg = self->aq_nic_cfg;

	/* Tx TC/RSS number config */
	hw_atl_tpb_tps_tx_tc_mode_set(self, nic_cfg->tc_mode);

	hw_atl_thm_lso_tcp_flag_of_first_pkt_set(self, 0x0FF6U);
	hw_atl_thm_lso_tcp_flag_of_middle_pkt_set(self, 0x0FF6U);
	hw_atl_thm_lso_tcp_flag_of_last_pkt_set(self, 0x0F7FU);

	/* Tx interrupts */
	hw_atl_tdm_tx_desc_wr_wb_irq_en_set(self, 1U);

	/* misc */
	hw_atl_tdm_tx_dca_en_set(self, 0U);
	hw_atl_tdm_tx_dca_mode_set(self, 0U);

	hw_atl_tpb_tx_path_scp_ins_en_set(self, 1U);

	hw_atl2_tpb_tx_buf_clk_gate_en_set(self, 0U);

	return aq_hw_err_from_flags(self);
}

static void hw_atl2_hw_init_new_rx_filters(struct aq_hw_s *self)
{
	struct hw_atl2_priv *priv = (struct hw_atl2_priv *)self->priv;
	u8 *prio_tc_map = self->aq_nic_cfg->prio_tc_map;
	u16 action;
	u8 index;
	int i;

	/* Action Resolver Table (ART) is used by RPF to decide which action
	 * to take with a packet based upon input tag and tag mask, where:
	 *  - input tag is a combination of 3-bit VLan Prio (PTP) and
	 *    29-bit concatenation of all tags from filter block;
	 *  - tag mask is a mask used for matching against input tag.
	 * The input_tag is compared with the all the Requested_tags in the
	 * Record table to find a match. Action field of the selected matched
	 * REC entry is used for further processing. If multiple entries match,
	 * the lowest REC entry, Action field will be selected.
	 */
	hw_atl2_rpf_act_rslvr_section_en_set(self, 0xFFFF);
	hw_atl2_rpfl2_uc_flr_tag_set(self, HW_ATL2_RPF_TAG_BASE_UC,
				     HW_ATL2_MAC_UC);
	hw_atl2_rpfl2_bc_flr_tag_set(self, HW_ATL2_RPF_TAG_BASE_UC);

	/* FW reserves the beginning of ART, thus all driver entries must
	 * start from the offset specified in FW caps.
	 */
	index = priv->art_base_index + HW_ATL2_RPF_L2_PROMISC_OFF_INDEX;
	hw_atl2_act_rslvr_table_set(self, index, 0,
				    HW_ATL2_RPF_TAG_UC_MASK |
					HW_ATL2_RPF_TAG_ALLMC_MASK,
				    HW_ATL2_ACTION_DROP);

	index = priv->art_base_index + HW_ATL2_RPF_VLAN_PROMISC_OFF_INDEX;
	hw_atl2_act_rslvr_table_set(self, index, 0,
				    HW_ATL2_RPF_TAG_VLAN_MASK |
					HW_ATL2_RPF_TAG_UNTAG_MASK,
				    HW_ATL2_ACTION_DROP);

	/* Configure ART to map given VLan Prio (PCP) to the TC index for
	 * RSS redirection table.
	 */
	for (i = 0; i < 8; i++) {
		action = HW_ATL2_ACTION_ASSIGN_TC(prio_tc_map[i]);

		index = priv->art_base_index + HW_ATL2_RPF_PCP_TO_TC_INDEX + i;
		hw_atl2_act_rslvr_table_set(self, index,
					    i << HW_ATL2_RPF_TAG_PCP_OFFSET,
					    HW_ATL2_RPF_TAG_PCP_MASK, action);
	}
}

static void hw_atl2_hw_new_rx_filter_vlan_promisc(struct aq_hw_s *self,
						  bool promisc)
{
	u16 off_action = (!promisc &&
			  !hw_atl_rpfl2promiscuous_mode_en_get(self)) ?
				HW_ATL2_ACTION_DROP : HW_ATL2_ACTION_DISABLE;
	struct hw_atl2_priv *priv = (struct hw_atl2_priv *)self->priv;
	u8 index;

	index = priv->art_base_index + HW_ATL2_RPF_VLAN_PROMISC_OFF_INDEX;
	hw_atl2_act_rslvr_table_set(self, index, 0,
				    HW_ATL2_RPF_TAG_VLAN_MASK |
				    HW_ATL2_RPF_TAG_UNTAG_MASK, off_action);
}

static void hw_atl2_hw_new_rx_filter_promisc(struct aq_hw_s *self, bool promisc)
{
	u16 off_action = promisc ? HW_ATL2_ACTION_DISABLE : HW_ATL2_ACTION_DROP;
	struct hw_atl2_priv *priv = (struct hw_atl2_priv *)self->priv;
	bool vlan_promisc_enable;
	u8 index;

	index = priv->art_base_index + HW_ATL2_RPF_L2_PROMISC_OFF_INDEX;
	hw_atl2_act_rslvr_table_set(self, index, 0,
				    HW_ATL2_RPF_TAG_UC_MASK |
				    HW_ATL2_RPF_TAG_ALLMC_MASK,
				    off_action);

	/* turn VLAN promisc mode too */
	vlan_promisc_enable = hw_atl_rpf_vlan_prom_mode_en_get(self);
	hw_atl2_hw_new_rx_filter_vlan_promisc(self, promisc |
					      vlan_promisc_enable);
}

static int hw_atl2_act_rslvr_table_set(struct aq_hw_s *self, u8 location,
				       u32 tag, u32 mask, u32 action)
{
	u32 val;
	int err;

	err = readx_poll_timeout_atomic(hw_atl2_sem_act_rslvr_get,
					self, val, val == 1,
					1, 10000U);
	if (err)
		return err;

	hw_atl2_rpf_act_rslvr_record_set(self, location, tag, mask,
					 action);

	hw_atl_reg_glb_cpu_sem_set(self, 1, HW_ATL2_FW_SM_ACT_RSLVR);

	return err;
}

static int hw_atl2_hw_init_rx_path(struct aq_hw_s *self)
{
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;
	int i;

	/* Rx TC/RSS number config */
	hw_atl_rpb_rpf_rx_traf_class_mode_set(self, cfg->tc_mode);

	/* Rx flow control */
	hw_atl_rpb_rx_flow_ctl_mode_set(self, 1U);

	hw_atl2_rpf_rss_hash_type_set(self, HW_ATL2_RPF_RSS_HASH_TYPE_ALL);

	/* RSS Ring selection */
	hw_atl_b0_hw_init_rx_rss_ctrl1(self);

	/* Multicast filters */
	for (i = HW_ATL2_MAC_MAX; i--;) {
		hw_atl_rpfl2_uc_flr_en_set(self, (i == 0U) ? 1U : 0U, i);
		hw_atl_rpfl2unicast_flr_act_set(self, 1U, i);
	}

	hw_atl_reg_rx_flr_mcst_flr_msk_set(self, 0x00000000U);
	hw_atl_reg_rx_flr_mcst_flr_set(self, HW_ATL_MCAST_FLT_ANY_TO_HOST, 0U);

	/* Vlan filters */
	hw_atl_rpf_vlan_outer_etht_set(self, ETH_P_8021AD);
	hw_atl_rpf_vlan_inner_etht_set(self, ETH_P_8021Q);

	hw_atl_rpf_vlan_prom_mode_en_set(self, 1);

	/* Always accept untagged packets */
	hw_atl_rpf_vlan_accept_untagged_packets_set(self, 1U);
	hw_atl_rpf_vlan_untagged_act_set(self, 1U);

	hw_atl2_hw_init_new_rx_filters(self);

	/* Rx Interrupts */
	hw_atl_rdm_rx_desc_wr_wb_irq_en_set(self, 1U);

	hw_atl_rpfl2broadcast_flr_act_set(self, 1U);
	hw_atl_rpfl2broadcast_count_threshold_set(self, 0xFFFFU & (~0U / 256U));

	hw_atl_rdm_rx_dca_en_set(self, 0U);
	hw_atl_rdm_rx_dca_mode_set(self, 0U);

	return aq_hw_err_from_flags(self);
}

static int hw_atl2_hw_init(struct aq_hw_s *self, u8 *mac_addr)
{
	static u32 aq_hw_atl2_igcr_table_[4][2] = {
		[AQ_HW_IRQ_INVALID] = { 0x20000000U, 0x20000000U },
		[AQ_HW_IRQ_LEGACY]  = { 0x20000080U, 0x20000080U },
		[AQ_HW_IRQ_MSI]     = { 0x20000021U, 0x20000025U },
		[AQ_HW_IRQ_MSIX]    = { 0x20000022U, 0x20000026U },
	};

	struct hw_atl2_priv *priv = (struct hw_atl2_priv *)self->priv;
	struct aq_nic_cfg_s *aq_nic_cfg = self->aq_nic_cfg;
	u8 base_index, count;
	int err;

	err = hw_atl2_utils_get_action_resolve_table_caps(self, &base_index,
							  &count);
	if (err)
		return err;

	priv->art_base_index = 8 * base_index;

	hw_atl2_init_launchtime(self);

	hw_atl2_hw_init_tx_path(self);
	hw_atl2_hw_init_rx_path(self);

	hw_atl_b0_hw_mac_addr_set(self, mac_addr);

	self->aq_fw_ops->set_link_speed(self, aq_nic_cfg->link_speed_msk);
	self->aq_fw_ops->set_state(self, MPI_INIT);

	hw_atl2_hw_qos_set(self);
	hw_atl2_hw_rss_set(self, &aq_nic_cfg->aq_rss);
	hw_atl_b0_hw_rss_hash_set(self, &aq_nic_cfg->aq_rss);

	hw_atl2_rpf_new_enable_set(self, 1);

	/* Reset link status and read out initial hardware counters */
	self->aq_link_status.mbps = 0;
	self->aq_fw_ops->update_stats(self);

	err = aq_hw_err_from_flags(self);
	if (err < 0)
		goto err_exit;

	/* Interrupts */
	hw_atl_reg_irq_glb_ctl_set(self,
				   aq_hw_atl2_igcr_table_[aq_nic_cfg->irq_type]
						 [(aq_nic_cfg->vecs > 1U) ?
						  1 : 0]);

	hw_atl_itr_irq_auto_masklsw_set(self, aq_nic_cfg->aq_hw_caps->irq_mask);

	/* Interrupts */
	hw_atl_reg_gen_irq_map_set(self,
				   ((HW_ATL2_ERR_INT << 0x18) |
				    (1U << 0x1F)) |
				   ((HW_ATL2_ERR_INT << 0x10) |
				    (1U << 0x17)), 0U);

	hw_atl_b0_hw_offload_set(self, aq_nic_cfg);

err_exit:
	return err;
}

static int hw_atl2_hw_ring_rx_init(struct aq_hw_s *self,
				   struct aq_ring_s *aq_ring,
				   struct aq_ring_param_s *aq_ring_param)
{
	return hw_atl_b0_hw_ring_rx_init(self, aq_ring, aq_ring_param);
}

static int hw_atl2_hw_ring_tx_init(struct aq_hw_s *self,
				   struct aq_ring_s *aq_ring,
				   struct aq_ring_param_s *aq_ring_param)
{
	return hw_atl_b0_hw_ring_tx_init(self, aq_ring, aq_ring_param);
}

#define IS_FILTER_ENABLED(_F_) ((packet_filter & (_F_)) ? 1U : 0U)

static int hw_atl2_hw_packet_filter_set(struct aq_hw_s *self,
					unsigned int packet_filter)
{
	hw_atl2_hw_new_rx_filter_promisc(self, IS_FILTER_ENABLED(IFF_PROMISC));

	return hw_atl_b0_hw_packet_filter_set(self, packet_filter);
}

#undef IS_FILTER_ENABLED

static int hw_atl2_hw_multicast_list_set(struct aq_hw_s *self,
					 u8 ar_mac
					 [AQ_HW_MULTICAST_ADDRESS_MAX]
					 [ETH_ALEN],
					 u32 count)
{
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;
	int err = 0;

	if (count > (HW_ATL2_MAC_MAX - HW_ATL2_MAC_MIN)) {
		err = -EBADRQC;
		goto err_exit;
	}
	for (cfg->mc_list_count = 0U;
			cfg->mc_list_count < count;
			++cfg->mc_list_count) {
		u32 i = cfg->mc_list_count;
		u32 h = (ar_mac[i][0] << 8) | (ar_mac[i][1]);
		u32 l = (ar_mac[i][2] << 24) | (ar_mac[i][3] << 16) |
					(ar_mac[i][4] << 8) | ar_mac[i][5];

		hw_atl_rpfl2_uc_flr_en_set(self, 0U, HW_ATL2_MAC_MIN + i);

		hw_atl_rpfl2unicast_dest_addresslsw_set(self, l,
							HW_ATL2_MAC_MIN + i);

		hw_atl_rpfl2unicast_dest_addressmsw_set(self, h,
							HW_ATL2_MAC_MIN + i);

		hw_atl2_rpfl2_uc_flr_tag_set(self, 1, HW_ATL2_MAC_MIN + i);

		hw_atl_rpfl2_uc_flr_en_set(self, (cfg->is_mc_list_enabled),
					   HW_ATL2_MAC_MIN + i);
	}

	err = aq_hw_err_from_flags(self);

err_exit:
	return err;
}

static int hw_atl2_hw_interrupt_moderation_set(struct aq_hw_s *self)
{
	unsigned int i = 0U;
	u32 itr_tx = 2U;
	u32 itr_rx = 2U;

	switch (self->aq_nic_cfg->itr) {
	case  AQ_CFG_INTERRUPT_MODERATION_ON:
	case  AQ_CFG_INTERRUPT_MODERATION_AUTO:
		hw_atl_tdm_tx_desc_wr_wb_irq_en_set(self, 0U);
		hw_atl_tdm_tdm_intr_moder_en_set(self, 1U);
		hw_atl_rdm_rx_desc_wr_wb_irq_en_set(self, 0U);
		hw_atl_rdm_rdm_intr_moder_en_set(self, 1U);

		if (self->aq_nic_cfg->itr == AQ_CFG_INTERRUPT_MODERATION_ON) {
			/* HW timers are in 2us units */
			int tx_max_timer = self->aq_nic_cfg->tx_itr / 2;
			int tx_min_timer = tx_max_timer / 2;

			int rx_max_timer = self->aq_nic_cfg->rx_itr / 2;
			int rx_min_timer = rx_max_timer / 2;

			tx_max_timer = min(HW_ATL2_INTR_MODER_MAX,
					   tx_max_timer);
			tx_min_timer = min(HW_ATL2_INTR_MODER_MIN,
					   tx_min_timer);
			rx_max_timer = min(HW_ATL2_INTR_MODER_MAX,
					   rx_max_timer);
			rx_min_timer = min(HW_ATL2_INTR_MODER_MIN,
					   rx_min_timer);

			itr_tx |= tx_min_timer << 0x8U;
			itr_tx |= tx_max_timer << 0x10U;
			itr_rx |= rx_min_timer << 0x8U;
			itr_rx |= rx_max_timer << 0x10U;
		} else {
			static unsigned int hw_atl2_timers_table_tx_[][2] = {
				{0xfU, 0xffU}, /* 10Gbit */
				{0xfU, 0x1ffU}, /* 5Gbit */
				{0xfU, 0x1ffU}, /* 5Gbit 5GS */
				{0xfU, 0x1ffU}, /* 2.5Gbit */
				{0xfU, 0x1ffU}, /* 1Gbit */
				{0xfU, 0x1ffU}, /* 100Mbit */
			};
			static unsigned int hw_atl2_timers_table_rx_[][2] = {
				{0x6U, 0x38U},/* 10Gbit */
				{0xCU, 0x70U},/* 5Gbit */
				{0xCU, 0x70U},/* 5Gbit 5GS */
				{0x18U, 0xE0U},/* 2.5Gbit */
				{0x30U, 0x80U},/* 1Gbit */
				{0x4U, 0x50U},/* 100Mbit */
			};
			unsigned int mbps = self->aq_link_status.mbps;
			unsigned int speed_index;

			speed_index = hw_atl_utils_mbps_2_speed_index(mbps);

			/* Update user visible ITR settings */
			self->aq_nic_cfg->tx_itr = hw_atl2_timers_table_tx_
							[speed_index][1] * 2;
			self->aq_nic_cfg->rx_itr = hw_atl2_timers_table_rx_
							[speed_index][1] * 2;

			itr_tx |= hw_atl2_timers_table_tx_
						[speed_index][0] << 0x8U;
			itr_tx |= hw_atl2_timers_table_tx_
						[speed_index][1] << 0x10U;

			itr_rx |= hw_atl2_timers_table_rx_
						[speed_index][0] << 0x8U;
			itr_rx |= hw_atl2_timers_table_rx_
						[speed_index][1] << 0x10U;
		}
		break;
	case AQ_CFG_INTERRUPT_MODERATION_OFF:
		hw_atl_tdm_tx_desc_wr_wb_irq_en_set(self, 1U);
		hw_atl_tdm_tdm_intr_moder_en_set(self, 0U);
		hw_atl_rdm_rx_desc_wr_wb_irq_en_set(self, 1U);
		hw_atl_rdm_rdm_intr_moder_en_set(self, 0U);
		itr_tx = 0U;
		itr_rx = 0U;
		break;
	}

	for (i = HW_ATL2_RINGS_MAX; i--;) {
		hw_atl2_reg_tx_intr_moder_ctrl_set(self, itr_tx, i);
		hw_atl_reg_rx_intr_moder_ctrl_set(self, itr_rx, i);
	}

	return aq_hw_err_from_flags(self);
}

static int hw_atl2_hw_stop(struct aq_hw_s *self)
{
	hw_atl_b0_hw_irq_disable(self, HW_ATL2_INT_MASK);

	return 0;
}

static struct aq_stats_s *hw_atl2_utils_get_hw_stats(struct aq_hw_s *self)
{
	return &self->curr_stats;
}

static int hw_atl2_hw_vlan_set(struct aq_hw_s *self,
			       struct aq_rx_filter_vlan *aq_vlans)
{
	struct hw_atl2_priv *priv = (struct hw_atl2_priv *)self->priv;
	u32 queue;
	u8 index;
	int i;

	hw_atl_rpf_vlan_prom_mode_en_set(self, 1U);

	for (i = 0; i < HW_ATL_VLAN_MAX_FILTERS; i++) {
		queue = HW_ATL2_ACTION_ASSIGN_QUEUE(aq_vlans[i].queue);

		hw_atl_rpf_vlan_flr_en_set(self, 0U, i);
		hw_atl_rpf_vlan_rxq_en_flr_set(self, 0U, i);
		index = priv->art_base_index + HW_ATL2_RPF_VLAN_USER_INDEX + i;
		hw_atl2_act_rslvr_table_set(self, index, 0, 0,
					    HW_ATL2_ACTION_DISABLE);
		if (aq_vlans[i].enable) {
			hw_atl_rpf_vlan_id_flr_set(self,
						   aq_vlans[i].vlan_id, i);
			hw_atl_rpf_vlan_flr_act_set(self, 1U, i);
			hw_atl_rpf_vlan_flr_en_set(self, 1U, i);

			if (aq_vlans[i].queue != 0xFF) {
				hw_atl_rpf_vlan_rxq_flr_set(self,
							    aq_vlans[i].queue,
							    i);
				hw_atl_rpf_vlan_rxq_en_flr_set(self, 1U, i);

				hw_atl2_rpf_vlan_flr_tag_set(self, i + 2, i);

				index = priv->art_base_index +
					HW_ATL2_RPF_VLAN_USER_INDEX + i;
				hw_atl2_act_rslvr_table_set(self, index,
					(i + 2) << HW_ATL2_RPF_TAG_VLAN_OFFSET,
					HW_ATL2_RPF_TAG_VLAN_MASK, queue);
			} else {
				hw_atl2_rpf_vlan_flr_tag_set(self, 1, i);
			}
		}
	}

	return aq_hw_err_from_flags(self);
}

static int hw_atl2_hw_vlan_ctrl(struct aq_hw_s *self, bool enable)
{
	/* set promisc in case of disabing the vlan filter */
	hw_atl_rpf_vlan_prom_mode_en_set(self, !enable);
	hw_atl2_hw_new_rx_filter_vlan_promisc(self, !enable);

	return aq_hw_err_from_flags(self);
}

const struct aq_hw_ops hw_atl2_ops = {
	.hw_soft_reset        = hw_atl2_utils_soft_reset,
	.hw_prepare           = hw_atl2_utils_initfw,
	.hw_set_mac_address   = hw_atl_b0_hw_mac_addr_set,
	.hw_init              = hw_atl2_hw_init,
	.hw_reset             = hw_atl2_hw_reset,
	.hw_start             = hw_atl_b0_hw_start,
	.hw_ring_tx_start     = hw_atl_b0_hw_ring_tx_start,
	.hw_ring_tx_stop      = hw_atl_b0_hw_ring_tx_stop,
	.hw_ring_rx_start     = hw_atl_b0_hw_ring_rx_start,
	.hw_ring_rx_stop      = hw_atl_b0_hw_ring_rx_stop,
	.hw_stop              = hw_atl2_hw_stop,

	.hw_ring_tx_xmit         = hw_atl_b0_hw_ring_tx_xmit,
	.hw_ring_tx_head_update  = hw_atl_b0_hw_ring_tx_head_update,

	.hw_ring_rx_receive      = hw_atl_b0_hw_ring_rx_receive,
	.hw_ring_rx_fill         = hw_atl_b0_hw_ring_rx_fill,

	.hw_irq_enable           = hw_atl_b0_hw_irq_enable,
	.hw_irq_disable          = hw_atl_b0_hw_irq_disable,
	.hw_irq_read             = hw_atl_b0_hw_irq_read,

	.hw_ring_rx_init             = hw_atl2_hw_ring_rx_init,
	.hw_ring_tx_init             = hw_atl2_hw_ring_tx_init,
	.hw_packet_filter_set        = hw_atl2_hw_packet_filter_set,
	.hw_filter_vlan_set          = hw_atl2_hw_vlan_set,
	.hw_filter_vlan_ctrl         = hw_atl2_hw_vlan_ctrl,
	.hw_multicast_list_set       = hw_atl2_hw_multicast_list_set,
	.hw_interrupt_moderation_set = hw_atl2_hw_interrupt_moderation_set,
	.hw_rss_set                  = hw_atl2_hw_rss_set,
	.hw_rss_hash_set             = hw_atl_b0_hw_rss_hash_set,
	.hw_tc_rate_limit_set        = hw_atl2_hw_init_tx_tc_rate_limit,
	.hw_get_hw_stats             = hw_atl2_utils_get_hw_stats,
	.hw_get_fw_version           = hw_atl2_utils_get_fw_version,
	.hw_set_offload              = hw_atl_b0_hw_offload_set,
	.hw_set_loopback             = hw_atl_b0_set_loopback,
	.hw_set_fc                   = hw_atl_b0_set_fc,
};
