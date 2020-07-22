// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 *
 * Copyright (C) 2014-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
 */

/* File hw_atl_b0.c: Definition of Atlantic hardware specific functions. */

#include "../aq_hw.h"
#include "../aq_hw_utils.h"
#include "../aq_ring.h"
#include "../aq_nic.h"
#include "../aq_phy.h"
#include "hw_atl_b0.h"
#include "hw_atl_utils.h"
#include "hw_atl_llh.h"
#include "hw_atl_b0_internal.h"
#include "hw_atl_llh_internal.h"

#define DEFAULT_B0_BOARD_BASIC_CAPABILITIES \
	.is_64_dma = true,		  \
	.msix_irqs = 8U,		  \
	.irq_mask = ~0U,		  \
	.vecs = HW_ATL_B0_RSS_MAX,	  \
	.tcs_max = HW_ATL_B0_TC_MAX,	  \
	.rxd_alignment = 1U,		  \
	.rxd_size = HW_ATL_B0_RXD_SIZE,   \
	.rxds_max = HW_ATL_B0_MAX_RXD,    \
	.rxds_min = HW_ATL_B0_MIN_RXD,    \
	.txd_alignment = 1U,		  \
	.txd_size = HW_ATL_B0_TXD_SIZE,   \
	.txds_max = HW_ATL_B0_MAX_TXD,    \
	.txds_min = HW_ATL_B0_MIN_TXD,    \
	.txhwb_alignment = 4096U,	  \
	.tx_rings = HW_ATL_B0_TX_RINGS,   \
	.rx_rings = HW_ATL_B0_RX_RINGS,   \
	.hw_features = NETIF_F_HW_CSUM |  \
			NETIF_F_RXCSUM |  \
			NETIF_F_RXHASH |  \
			NETIF_F_SG |      \
			NETIF_F_TSO |     \
			NETIF_F_LRO |     \
			NETIF_F_NTUPLE |  \
			NETIF_F_HW_VLAN_CTAG_FILTER | \
			NETIF_F_HW_VLAN_CTAG_RX |     \
			NETIF_F_HW_VLAN_CTAG_TX |     \
			NETIF_F_GSO_UDP_L4      |     \
			NETIF_F_GSO_PARTIAL |         \
			NETIF_F_HW_TC,                \
	.hw_priv_flags = IFF_UNICAST_FLT, \
	.flow_control = true,		  \
	.mtu = HW_ATL_B0_MTU_JUMBO,	  \
	.mac_regs_count = 88,		  \
	.hw_alive_check_addr = 0x10U

#define FRAC_PER_NS 0x100000000LL

const struct aq_hw_caps_s hw_atl_b0_caps_aqc100 = {
	DEFAULT_B0_BOARD_BASIC_CAPABILITIES,
	.media_type = AQ_HW_MEDIA_TYPE_FIBRE,
	.link_speed_msk = AQ_NIC_RATE_10G |
			  AQ_NIC_RATE_5G |
			  AQ_NIC_RATE_2G5 |
			  AQ_NIC_RATE_1G |
			  AQ_NIC_RATE_100M,
};

const struct aq_hw_caps_s hw_atl_b0_caps_aqc107 = {
	DEFAULT_B0_BOARD_BASIC_CAPABILITIES,
	.media_type = AQ_HW_MEDIA_TYPE_TP,
	.link_speed_msk = AQ_NIC_RATE_10G |
			  AQ_NIC_RATE_5G |
			  AQ_NIC_RATE_2G5 |
			  AQ_NIC_RATE_1G |
			  AQ_NIC_RATE_100M,
};

const struct aq_hw_caps_s hw_atl_b0_caps_aqc108 = {
	DEFAULT_B0_BOARD_BASIC_CAPABILITIES,
	.media_type = AQ_HW_MEDIA_TYPE_TP,
	.link_speed_msk = AQ_NIC_RATE_5G |
			  AQ_NIC_RATE_2G5 |
			  AQ_NIC_RATE_1G |
			  AQ_NIC_RATE_100M,
};

const struct aq_hw_caps_s hw_atl_b0_caps_aqc109 = {
	DEFAULT_B0_BOARD_BASIC_CAPABILITIES,
	.media_type = AQ_HW_MEDIA_TYPE_TP,
	.link_speed_msk = AQ_NIC_RATE_2G5 |
			  AQ_NIC_RATE_1G |
			  AQ_NIC_RATE_100M,
};

const struct aq_hw_caps_s hw_atl_b0_caps_aqc111 = {
	DEFAULT_B0_BOARD_BASIC_CAPABILITIES,
	.media_type = AQ_HW_MEDIA_TYPE_TP,
	.link_speed_msk = AQ_NIC_RATE_5G |
			  AQ_NIC_RATE_2G5 |
			  AQ_NIC_RATE_1G |
			  AQ_NIC_RATE_100M,
	.quirks = AQ_NIC_QUIRK_BAD_PTP,
};

const struct aq_hw_caps_s hw_atl_b0_caps_aqc112 = {
	DEFAULT_B0_BOARD_BASIC_CAPABILITIES,
	.media_type = AQ_HW_MEDIA_TYPE_TP,
	.link_speed_msk = AQ_NIC_RATE_2G5 |
			  AQ_NIC_RATE_1G  |
			  AQ_NIC_RATE_100M,
	.quirks = AQ_NIC_QUIRK_BAD_PTP,
};

static int hw_atl_b0_hw_reset(struct aq_hw_s *self)
{
	int err = 0;

	err = hw_atl_utils_soft_reset(self);
	if (err)
		return err;

	self->aq_fw_ops->set_state(self, MPI_RESET);

	err = aq_hw_err_from_flags(self);

	return err;
}

static int hw_atl_b0_set_fc(struct aq_hw_s *self, u32 fc, u32 tc)
{
	hw_atl_rpb_rx_xoff_en_per_tc_set(self, !!(fc & AQ_NIC_FC_RX), tc);

	return 0;
}

static int hw_atl_b0_tc_ptp_set(struct aq_hw_s *self)
{
	/* Init TC2 for PTP_TX */
	hw_atl_tpb_tx_pkt_buff_size_per_tc_set(self, HW_ATL_B0_PTP_TXBUF_SIZE,
					       AQ_HW_PTP_TC);

	/* Init TC2 for PTP_RX */
	hw_atl_rpb_rx_pkt_buff_size_per_tc_set(self, HW_ATL_B0_PTP_RXBUF_SIZE,
					       AQ_HW_PTP_TC);
	/* No flow control for PTP */
	hw_atl_rpb_rx_xoff_en_per_tc_set(self, 0U, AQ_HW_PTP_TC);

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_hw_qos_set(struct aq_hw_s *self)
{
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;
	u32 tx_buff_size = HW_ATL_B0_TXBUF_MAX;
	u32 rx_buff_size = HW_ATL_B0_RXBUF_MAX;
	unsigned int prio = 0U;
	u32 tc = 0U;

	if (cfg->is_ptp) {
		tx_buff_size -= HW_ATL_B0_PTP_TXBUF_SIZE;
		rx_buff_size -= HW_ATL_B0_PTP_RXBUF_SIZE;
	}

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

	if (cfg->is_ptp)
		hw_atl_b0_tc_ptp_set(self);

	/* QoS 802.1p priority -> TC mapping */
	for (prio = 0; prio < 8; ++prio)
		hw_atl_rpf_rpb_user_priority_tc_map_set(self, prio,
							cfg->prio_tc_map[prio]);

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_rss_hash_set(struct aq_hw_s *self,
			      struct aq_rss_parameters *rss_params)
{
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;
	unsigned int addr = 0U;
	unsigned int i = 0U;
	int err = 0;
	u32 val;

	for (i = 10, addr = 0U; i--; ++addr) {
		u32 key_data = cfg->is_rss ?
			__swab32(rss_params->hash_secret_key[i]) : 0U;
		hw_atl_rpf_rss_key_wr_data_set(self, key_data);
		hw_atl_rpf_rss_key_addr_set(self, addr);
		hw_atl_rpf_rss_key_wr_en_set(self, 1U);
		err = readx_poll_timeout_atomic(hw_atl_rpf_rss_key_wr_en_get,
						self, val, val == 0,
						1000U, 10000U);
		if (err < 0)
			goto err_exit;
	}

	err = aq_hw_err_from_flags(self);

err_exit:
	return err;
}

static int hw_atl_b0_hw_rss_set(struct aq_hw_s *self,
				struct aq_rss_parameters *rss_params)
{
	u32 num_rss_queues = max(1U, self->aq_nic_cfg->num_rss_queues);
	u8 *indirection_table =	rss_params->indirection_table;
	u16 bitary[1 + (HW_ATL_B0_RSS_REDIRECTION_MAX *
		   HW_ATL_B0_RSS_REDIRECTION_BITS / 16U)];
	int err = 0;
	u32 i = 0U;
	u32 val;

	memset(bitary, 0, sizeof(bitary));

	for (i = HW_ATL_B0_RSS_REDIRECTION_MAX; i--;) {
		(*(u32 *)(bitary + ((i * 3U) / 16U))) |=
			((indirection_table[i] % num_rss_queues) <<
			((i * 3U) & 0xFU));
	}

	for (i = ARRAY_SIZE(bitary); i--;) {
		hw_atl_rpf_rss_redir_tbl_wr_data_set(self, bitary[i]);
		hw_atl_rpf_rss_redir_tbl_addr_set(self, i);
		hw_atl_rpf_rss_redir_wr_en_set(self, 1U);
		err = readx_poll_timeout_atomic(hw_atl_rpf_rss_redir_wr_en_get,
						self, val, val == 0,
						1000U, 10000U);
		if (err < 0)
			goto err_exit;
	}

	err = aq_hw_err_from_flags(self);

err_exit:
	return err;
}

int hw_atl_b0_hw_offload_set(struct aq_hw_s *self,
			     struct aq_nic_cfg_s *aq_nic_cfg)
{
	u64 rxcsum = !!(aq_nic_cfg->features & NETIF_F_RXCSUM);
	unsigned int i;

	/* TX checksums offloads*/
	hw_atl_tpo_ipv4header_crc_offload_en_set(self, 1);
	hw_atl_tpo_tcp_udp_crc_offload_en_set(self, 1);

	/* RX checksums offloads*/
	hw_atl_rpo_ipv4header_crc_offload_en_set(self, rxcsum);
	hw_atl_rpo_tcp_udp_crc_offload_en_set(self, rxcsum);

	/* LSO offloads*/
	hw_atl_tdm_large_send_offload_en_set(self, 0xFFFFFFFFU);

	/* Outer VLAN tag offload */
	hw_atl_rpo_outer_vlan_tag_mode_set(self, 1U);

	/* LRO offloads */
	{
		unsigned int val = (8U < HW_ATL_B0_LRO_RXD_MAX) ? 0x3U :
			((4U < HW_ATL_B0_LRO_RXD_MAX) ? 0x2U :
			((2U < HW_ATL_B0_LRO_RXD_MAX) ? 0x1U : 0x0));

		for (i = 0; i < HW_ATL_B0_RINGS_MAX; i++)
			hw_atl_rpo_lro_max_num_of_descriptors_set(self, val, i);

		hw_atl_rpo_lro_time_base_divider_set(self, 0x61AU);
		hw_atl_rpo_lro_inactive_interval_set(self, 0);
		/* the LRO timebase divider is 5 uS (0x61a),
		 * which is multiplied by 50(0x32)
		 * to get a maximum coalescing interval of 250 uS,
		 * which is the default value
		 */
		hw_atl_rpo_lro_max_coalescing_interval_set(self, 50);

		hw_atl_rpo_lro_qsessions_lim_set(self, 1U);

		hw_atl_rpo_lro_total_desc_lim_set(self, 2U);

		hw_atl_rpo_lro_patch_optimization_en_set(self, 1U);

		hw_atl_rpo_lro_min_pay_of_first_pkt_set(self, 10U);

		hw_atl_rpo_lro_pkt_lim_set(self, 1U);

		hw_atl_rpo_lro_en_set(self,
				      aq_nic_cfg->is_lro ? 0xFFFFFFFFU : 0U);
		hw_atl_itr_rsc_en_set(self,
				      aq_nic_cfg->is_lro ? 0xFFFFFFFFU : 0U);

		hw_atl_itr_rsc_delay_set(self, 1U);
	}

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_hw_init_tx_tc_rate_limit(struct aq_hw_s *self)
{
	static const u32 max_weight = BIT(HW_ATL_TPS_DATA_TCTWEIGHT_WIDTH) - 1;
	/* Scale factor is based on the number of bits in fractional portion */
	static const u32 scale = BIT(HW_ATL_TPS_DESC_RATE_Y_WIDTH);
	static const u32 frac_msk = HW_ATL_TPS_DESC_RATE_Y_MSK >>
				    HW_ATL_TPS_DESC_RATE_Y_SHIFT;
	const u32 link_speed = self->aq_link_status.mbps;
	struct aq_nic_cfg_s *nic_cfg = self->aq_nic_cfg;
	unsigned long num_min_rated_tcs = 0;
	u32 tc_weight[AQ_CFG_TCS_MAX];
	u32 fixed_max_credit;
	u8 min_rate_msk = 0;
	u32 sum_weight = 0;
	int tc;

	/* By default max_credit is based upon MTU (in unit of 64b) */
	fixed_max_credit = nic_cfg->aq_hw_caps->mtu / 64;

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
	 *
	 * NB! MAC FW sets arb mode itself if PTP is enabled. We shouldn't
	 * overwrite it here in that case.
	 */
	if (!nic_cfg->is_ptp)
		hw_atl_tps_tx_pkt_shed_data_arb_mode_set(self, min_rate_msk ? 1U : 0U);

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
				weight = 0x64;

			max_credit = max(8 * weight, fixed_max_credit);
		} else {
			weight = 0x64;
			max_credit = 0xFFF;
		}

		hw_atl_tps_tx_pkt_shed_tc_data_weight_set(self, tc, weight);
		hw_atl_tps_tx_pkt_shed_tc_data_max_credit_set(self, tc,
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

static int hw_atl_b0_hw_init_tx_path(struct aq_hw_s *self)
{
	struct aq_nic_cfg_s *nic_cfg = self->aq_nic_cfg;

	/* Tx TC/Queue number config */
	hw_atl_tpb_tps_tx_tc_mode_set(self, nic_cfg->tc_mode);

	hw_atl_thm_lso_tcp_flag_of_first_pkt_set(self, 0x0FF6U);
	hw_atl_thm_lso_tcp_flag_of_middle_pkt_set(self, 0x0FF6U);
	hw_atl_thm_lso_tcp_flag_of_last_pkt_set(self, 0x0F7FU);

	/* Tx interrupts */
	hw_atl_tdm_tx_desc_wr_wb_irq_en_set(self, 1U);

	/* misc */
	aq_hw_write_reg(self, 0x00007040U, ATL_HW_IS_CHIP_FEATURE(self, TPO2) ?
			0x00010000U : 0x00000000U);
	hw_atl_tdm_tx_dca_en_set(self, 0U);
	hw_atl_tdm_tx_dca_mode_set(self, 0U);

	hw_atl_tpb_tx_path_scp_ins_en_set(self, 1U);

	return aq_hw_err_from_flags(self);
}

void hw_atl_b0_hw_init_rx_rss_ctrl1(struct aq_hw_s *self)
{
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;
	u32 rss_ctrl1 = HW_ATL_RSS_DISABLED;

	if (cfg->is_rss)
		rss_ctrl1 = (cfg->tc_mode == AQ_TC_MODE_8TCS) ?
			    HW_ATL_RSS_ENABLED_8TCS_2INDEX_BITS :
			    HW_ATL_RSS_ENABLED_4TCS_3INDEX_BITS;

	hw_atl_reg_rx_flr_rss_control1set(self, rss_ctrl1);
}

static int hw_atl_b0_hw_init_rx_path(struct aq_hw_s *self)
{
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;
	int i;

	/* Rx TC/RSS number config */
	hw_atl_rpb_rpf_rx_traf_class_mode_set(self, cfg->tc_mode);

	/* Rx flow control */
	hw_atl_rpb_rx_flow_ctl_mode_set(self, 1U);

	/* RSS Ring selection */
	hw_atl_b0_hw_init_rx_rss_ctrl1(self);

	/* Multicast filters */
	for (i = HW_ATL_B0_MAC_MAX; i--;) {
		hw_atl_rpfl2_uc_flr_en_set(self, (i == 0U) ? 1U : 0U, i);
		hw_atl_rpfl2unicast_flr_act_set(self, 1U, i);
	}

	hw_atl_reg_rx_flr_mcst_flr_msk_set(self, 0x00000000U);
	hw_atl_reg_rx_flr_mcst_flr_set(self, 0x00010FFFU, 0U);

	/* Vlan filters */
	hw_atl_rpf_vlan_outer_etht_set(self, 0x88A8U);
	hw_atl_rpf_vlan_inner_etht_set(self, 0x8100U);

	hw_atl_rpf_vlan_prom_mode_en_set(self, 1);

	// Always accept untagged packets
	hw_atl_rpf_vlan_accept_untagged_packets_set(self, 1U);
	hw_atl_rpf_vlan_untagged_act_set(self, 1U);

	/* Rx Interrupts */
	hw_atl_rdm_rx_desc_wr_wb_irq_en_set(self, 1U);

	/* misc */
	aq_hw_write_reg(self, 0x00005040U, ATL_HW_IS_CHIP_FEATURE(self, RPF2) ?
			0x000F0000U : 0x00000000U);

	hw_atl_rpfl2broadcast_flr_act_set(self, 1U);
	hw_atl_rpfl2broadcast_count_threshold_set(self, 0xFFFFU & (~0U / 256U));

	hw_atl_rdm_rx_dca_en_set(self, 0U);
	hw_atl_rdm_rx_dca_mode_set(self, 0U);

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_mac_addr_set(struct aq_hw_s *self, u8 *mac_addr)
{
	unsigned int h = 0U;
	unsigned int l = 0U;
	int err = 0;

	if (!mac_addr) {
		err = -EINVAL;
		goto err_exit;
	}
	h = (mac_addr[0] << 8) | (mac_addr[1]);
	l = (mac_addr[2] << 24) | (mac_addr[3] << 16) |
		(mac_addr[4] << 8) | mac_addr[5];

	hw_atl_rpfl2_uc_flr_en_set(self, 0U, HW_ATL_B0_MAC);
	hw_atl_rpfl2unicast_dest_addresslsw_set(self, l, HW_ATL_B0_MAC);
	hw_atl_rpfl2unicast_dest_addressmsw_set(self, h, HW_ATL_B0_MAC);
	hw_atl_rpfl2_uc_flr_en_set(self, 1U, HW_ATL_B0_MAC);

	err = aq_hw_err_from_flags(self);

err_exit:
	return err;
}

static int hw_atl_b0_hw_init(struct aq_hw_s *self, u8 *mac_addr)
{
	static u32 aq_hw_atl_igcr_table_[4][2] = {
		[AQ_HW_IRQ_INVALID] = { 0x20000000U, 0x20000000U },
		[AQ_HW_IRQ_LEGACY]  = { 0x20000080U, 0x20000080U },
		[AQ_HW_IRQ_MSI]     = { 0x20000021U, 0x20000025U },
		[AQ_HW_IRQ_MSIX]    = { 0x20000022U, 0x20000026U },
	};
	struct aq_nic_cfg_s *aq_nic_cfg = self->aq_nic_cfg;
	int err = 0;
	u32 val;


	hw_atl_b0_hw_init_tx_path(self);
	hw_atl_b0_hw_init_rx_path(self);

	hw_atl_b0_hw_mac_addr_set(self, mac_addr);

	self->aq_fw_ops->set_link_speed(self, aq_nic_cfg->link_speed_msk);
	self->aq_fw_ops->set_state(self, MPI_INIT);

	hw_atl_b0_hw_qos_set(self);
	hw_atl_b0_hw_rss_set(self, &aq_nic_cfg->aq_rss);
	hw_atl_b0_hw_rss_hash_set(self, &aq_nic_cfg->aq_rss);

	/* Force limit MRRS on RDM/TDM to 2K */
	val = aq_hw_read_reg(self, HW_ATL_PCI_REG_CONTROL6_ADR);
	aq_hw_write_reg(self, HW_ATL_PCI_REG_CONTROL6_ADR,
			(val & ~0x707) | 0x404);

	/* TX DMA total request limit. B0 hardware is not capable to
	 * handle more than (8K-MRRS) incoming DMA data.
	 * Value 24 in 256byte units
	 */
	aq_hw_write_reg(self, HW_ATL_TX_DMA_TOTAL_REQ_LIMIT_ADR, 24);

	/* Reset link status and read out initial hardware counters */
	self->aq_link_status.mbps = 0;
	self->aq_fw_ops->update_stats(self);

	err = aq_hw_err_from_flags(self);
	if (err < 0)
		goto err_exit;

	/* Interrupts */
	hw_atl_reg_irq_glb_ctl_set(self,
				   aq_hw_atl_igcr_table_[aq_nic_cfg->irq_type]
						 [(aq_nic_cfg->vecs > 1U) ?
						 1 : 0]);

	hw_atl_itr_irq_auto_masklsw_set(self, aq_nic_cfg->aq_hw_caps->irq_mask);

	/* Interrupts */
	hw_atl_reg_gen_irq_map_set(self,
				   ((HW_ATL_B0_ERR_INT << 0x18) |
				    (1U << 0x1F)) |
				   ((HW_ATL_B0_ERR_INT << 0x10) |
				    (1U << 0x17)), 0U);

	/* Enable link interrupt */
	if (aq_nic_cfg->link_irq_vec)
		hw_atl_reg_gen_irq_map_set(self, BIT(7) |
					   aq_nic_cfg->link_irq_vec, 3U);

	hw_atl_b0_hw_offload_set(self, aq_nic_cfg);

err_exit:
	return err;
}

int hw_atl_b0_hw_ring_tx_start(struct aq_hw_s *self, struct aq_ring_s *ring)
{
	hw_atl_tdm_tx_desc_en_set(self, 1, ring->idx);

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_ring_rx_start(struct aq_hw_s *self, struct aq_ring_s *ring)
{
	hw_atl_rdm_rx_desc_en_set(self, 1, ring->idx);

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_start(struct aq_hw_s *self)
{
	hw_atl_tpb_tx_buff_en_set(self, 1);
	hw_atl_rpb_rx_buff_en_set(self, 1);

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_hw_tx_ring_tail_update(struct aq_hw_s *self,
					    struct aq_ring_s *ring)
{
	hw_atl_reg_tx_dma_desc_tail_ptr_set(self, ring->sw_tail, ring->idx);

	return 0;
}

int hw_atl_b0_hw_ring_tx_xmit(struct aq_hw_s *self, struct aq_ring_s *ring,
			      unsigned int frags)
{
	struct aq_ring_buff_s *buff = NULL;
	struct hw_atl_txd_s *txd = NULL;
	unsigned int buff_pa_len = 0U;
	unsigned int frag_count = 0U;
	unsigned int pkt_len = 0U;
	bool is_vlan = false;
	bool is_gso = false;

	buff = &ring->buff_ring[ring->sw_tail];
	pkt_len = (buff->is_eop && buff->is_sop) ? buff->len : buff->len_pkt;

	for (frag_count = 0; frag_count < frags; frag_count++) {
		txd = (struct hw_atl_txd_s *)&ring->dx_ring[ring->sw_tail *
						HW_ATL_B0_TXD_SIZE];
		txd->ctl = 0;
		txd->ctl2 = 0;
		txd->buf_addr = 0;

		buff = &ring->buff_ring[ring->sw_tail];

		if (buff->is_gso_tcp || buff->is_gso_udp) {
			if (buff->is_gso_tcp)
				txd->ctl |= HW_ATL_B0_TXD_CTL_CMD_TCP;
			txd->ctl |= HW_ATL_B0_TXD_CTL_DESC_TYPE_TXC;
			txd->ctl |= (buff->len_l3 << 31) |
				    (buff->len_l2 << 24);
			txd->ctl2 |= (buff->mss << 16);
			is_gso = true;

			pkt_len -= (buff->len_l4 +
				    buff->len_l3 +
				    buff->len_l2);
			if (buff->is_ipv6)
				txd->ctl |= HW_ATL_B0_TXD_CTL_CMD_IPV6;
			txd->ctl2 |= (buff->len_l4 << 8) |
				     (buff->len_l3 >> 1);
		}
		if (buff->is_vlan) {
			txd->ctl |= HW_ATL_B0_TXD_CTL_DESC_TYPE_TXC;
			txd->ctl |= buff->vlan_tx_tag << 4;
			is_vlan = true;
		}
		if (!buff->is_gso_tcp && !buff->is_gso_udp && !buff->is_vlan) {
			buff_pa_len = buff->len;

			txd->buf_addr = buff->pa;
			txd->ctl |= (HW_ATL_B0_TXD_CTL_BLEN &
						((u32)buff_pa_len << 4));
			txd->ctl |= HW_ATL_B0_TXD_CTL_DESC_TYPE_TXD;

			/* PAY_LEN */
			txd->ctl2 |= HW_ATL_B0_TXD_CTL2_LEN & (pkt_len << 14);

			if (is_gso || is_vlan) {
				/* enable tx context */
				txd->ctl2 |= HW_ATL_B0_TXD_CTL2_CTX_EN;
			}
			if (is_gso)
				txd->ctl |= HW_ATL_B0_TXD_CTL_CMD_LSO;

			/* Tx checksum offloads */
			if (buff->is_ip_cso)
				txd->ctl |= HW_ATL_B0_TXD_CTL_CMD_IPCSO;

			if (buff->is_udp_cso || buff->is_tcp_cso)
				txd->ctl |= HW_ATL_B0_TXD_CTL_CMD_TUCSO;

			if (is_vlan)
				txd->ctl |= HW_ATL_B0_TXD_CTL_CMD_VLAN;

			if (unlikely(buff->is_eop)) {
				txd->ctl |= HW_ATL_B0_TXD_CTL_EOP;
				txd->ctl |= HW_ATL_B0_TXD_CTL_CMD_WB;
				is_gso = false;
				is_vlan = false;
			}
		}
		ring->sw_tail = aq_ring_next_dx(ring, ring->sw_tail);
	}

	hw_atl_b0_hw_tx_ring_tail_update(self, ring);

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_ring_rx_init(struct aq_hw_s *self, struct aq_ring_s *aq_ring,
			      struct aq_ring_param_s *aq_ring_param)
{
	u32 dma_desc_addr_msw = (u32)(((u64)aq_ring->dx_ring_pa) >> 32);
	u32 vlan_rx_stripping = self->aq_nic_cfg->is_vlan_rx_strip;
	u32 dma_desc_addr_lsw = (u32)aq_ring->dx_ring_pa;

	hw_atl_rdm_rx_desc_en_set(self, false, aq_ring->idx);

	hw_atl_rdm_rx_desc_head_splitting_set(self, 0U, aq_ring->idx);

	hw_atl_reg_rx_dma_desc_base_addresslswset(self, dma_desc_addr_lsw,
						  aq_ring->idx);

	hw_atl_reg_rx_dma_desc_base_addressmswset(self,
						  dma_desc_addr_msw, aq_ring->idx);

	hw_atl_rdm_rx_desc_len_set(self, aq_ring->size / 8U, aq_ring->idx);

	hw_atl_rdm_rx_desc_data_buff_size_set(self,
					      AQ_CFG_RX_FRAME_MAX / 1024U,
				       aq_ring->idx);

	hw_atl_rdm_rx_desc_head_buff_size_set(self, 0U, aq_ring->idx);
	hw_atl_rdm_rx_desc_head_splitting_set(self, 0U, aq_ring->idx);
	hw_atl_rpo_rx_desc_vlan_stripping_set(self, !!vlan_rx_stripping,
					      aq_ring->idx);

	/* Rx ring set mode */

	/* Mapping interrupt vector */
	hw_atl_itr_irq_map_rx_set(self, aq_ring_param->vec_idx, aq_ring->idx);
	hw_atl_itr_irq_map_en_rx_set(self, true, aq_ring->idx);

	hw_atl_rdm_cpu_id_set(self, aq_ring_param->cpu, aq_ring->idx);
	hw_atl_rdm_rx_desc_dca_en_set(self, 0U, aq_ring->idx);
	hw_atl_rdm_rx_head_dca_en_set(self, 0U, aq_ring->idx);
	hw_atl_rdm_rx_pld_dca_en_set(self, 0U, aq_ring->idx);

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_ring_tx_init(struct aq_hw_s *self, struct aq_ring_s *aq_ring,
			      struct aq_ring_param_s *aq_ring_param)
{
	u32 dma_desc_msw_addr = (u32)(((u64)aq_ring->dx_ring_pa) >> 32);
	u32 dma_desc_lsw_addr = (u32)aq_ring->dx_ring_pa;

	hw_atl_reg_tx_dma_desc_base_addresslswset(self, dma_desc_lsw_addr,
						  aq_ring->idx);

	hw_atl_reg_tx_dma_desc_base_addressmswset(self, dma_desc_msw_addr,
						  aq_ring->idx);

	hw_atl_tdm_tx_desc_len_set(self, aq_ring->size / 8U, aq_ring->idx);

	hw_atl_b0_hw_tx_ring_tail_update(self, aq_ring);

	/* Set Tx threshold */
	hw_atl_tdm_tx_desc_wr_wb_threshold_set(self, 0U, aq_ring->idx);

	/* Mapping interrupt vector */
	hw_atl_itr_irq_map_tx_set(self, aq_ring_param->vec_idx, aq_ring->idx);
	hw_atl_itr_irq_map_en_tx_set(self, true, aq_ring->idx);

	hw_atl_tdm_cpu_id_set(self, aq_ring_param->cpu, aq_ring->idx);
	hw_atl_tdm_tx_desc_dca_en_set(self, 0U, aq_ring->idx);

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_ring_rx_fill(struct aq_hw_s *self, struct aq_ring_s *ring,
			      unsigned int sw_tail_old)
{
	for (; sw_tail_old != ring->sw_tail;
		sw_tail_old = aq_ring_next_dx(ring, sw_tail_old)) {
		struct hw_atl_rxd_s *rxd =
			(struct hw_atl_rxd_s *)&ring->dx_ring[sw_tail_old *
							HW_ATL_B0_RXD_SIZE];

		struct aq_ring_buff_s *buff = &ring->buff_ring[sw_tail_old];

		rxd->buf_addr = buff->pa;
		rxd->hdr_addr = 0U;
	}

	hw_atl_reg_rx_dma_desc_tail_ptr_set(self, sw_tail_old, ring->idx);

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_hw_ring_hwts_rx_fill(struct aq_hw_s *self,
					  struct aq_ring_s *ring)
{
	unsigned int i;

	for (i = aq_ring_avail_dx(ring); i--;
			ring->sw_tail = aq_ring_next_dx(ring, ring->sw_tail)) {
		struct hw_atl_rxd_s *rxd =
			(struct hw_atl_rxd_s *)
			&ring->dx_ring[ring->sw_tail * HW_ATL_B0_RXD_SIZE];

		rxd->buf_addr = ring->dx_ring_pa + ring->size * ring->dx_size;
		rxd->hdr_addr = 0U;
	}
	/* Make sure descriptors are updated before bump tail*/
	wmb();

	hw_atl_reg_rx_dma_desc_tail_ptr_set(self, ring->sw_tail, ring->idx);

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_hw_ring_hwts_rx_receive(struct aq_hw_s *self,
					     struct aq_ring_s *ring)
{
	while (ring->hw_head != ring->sw_tail) {
		struct hw_atl_rxd_hwts_wb_s *hwts_wb =
			(struct hw_atl_rxd_hwts_wb_s *)
			(ring->dx_ring + (ring->hw_head * HW_ATL_B0_RXD_SIZE));

		/* RxD is not done */
		if (!(hwts_wb->sec_lw0 & 0x1U))
			break;

		ring->hw_head = aq_ring_next_dx(ring, ring->hw_head);
	}

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_ring_tx_head_update(struct aq_hw_s *self,
				     struct aq_ring_s *ring)
{
	unsigned int hw_head_;
	int err = 0;

	hw_head_ = hw_atl_tdm_tx_desc_head_ptr_get(self, ring->idx);

	if (aq_utils_obj_test(&self->flags, AQ_HW_FLAG_ERR_UNPLUG)) {
		err = -ENXIO;
		goto err_exit;
	}
	ring->hw_head = hw_head_;
	err = aq_hw_err_from_flags(self);

err_exit:
	return err;
}

int hw_atl_b0_hw_ring_rx_receive(struct aq_hw_s *self, struct aq_ring_s *ring)
{
	for (; ring->hw_head != ring->sw_tail;
		ring->hw_head = aq_ring_next_dx(ring, ring->hw_head)) {
		struct aq_ring_buff_s *buff = NULL;
		struct hw_atl_rxd_wb_s *rxd_wb = (struct hw_atl_rxd_wb_s *)
			&ring->dx_ring[ring->hw_head * HW_ATL_B0_RXD_SIZE];

		unsigned int is_rx_check_sum_enabled = 0U;
		unsigned int pkt_type = 0U;
		u8 rx_stat = 0U;

		if (!(rxd_wb->status & 0x1U)) { /* RxD is not done */
			break;
		}

		buff = &ring->buff_ring[ring->hw_head];

		buff->flags = 0U;
		buff->is_hash_l4 = 0U;

		rx_stat = (0x0000003CU & rxd_wb->status) >> 2;

		is_rx_check_sum_enabled = (rxd_wb->type >> 19) & 0x3U;

		pkt_type = (rxd_wb->type & HW_ATL_B0_RXD_WB_STAT_PKTTYPE) >>
			   HW_ATL_B0_RXD_WB_STAT_PKTTYPE_SHIFT;

		if (is_rx_check_sum_enabled & BIT(0) &&
		    (0x0U == (pkt_type & 0x3U)))
			buff->is_ip_cso = (rx_stat & BIT(1)) ? 0U : 1U;

		if (is_rx_check_sum_enabled & BIT(1)) {
			if (0x4U == (pkt_type & 0x1CU))
				buff->is_udp_cso = (rx_stat & BIT(2)) ? 0U :
						   !!(rx_stat & BIT(3));
			else if (0x0U == (pkt_type & 0x1CU))
				buff->is_tcp_cso = (rx_stat & BIT(2)) ? 0U :
						   !!(rx_stat & BIT(3));
		}
		buff->is_cso_err = !!(rx_stat & 0x6);
		/* Checksum offload workaround for small packets */
		if (unlikely(rxd_wb->pkt_len <= 60)) {
			buff->is_ip_cso = 0U;
			buff->is_cso_err = 0U;
		}

		if (self->aq_nic_cfg->is_vlan_rx_strip &&
		    ((pkt_type & HW_ATL_B0_RXD_WB_PKTTYPE_VLAN) ||
		     (pkt_type & HW_ATL_B0_RXD_WB_PKTTYPE_VLAN_DOUBLE))) {
			buff->is_vlan = 1;
			buff->vlan_rx_tag = le16_to_cpu(rxd_wb->vlan);
		}

		if ((rx_stat & BIT(0)) || rxd_wb->type & 0x1000U) {
			/* MAC error or DMA error */
			buff->is_error = 1U;
		}
		if (self->aq_nic_cfg->is_rss) {
			/* last 4 byte */
			u16 rss_type = rxd_wb->type & 0xFU;

			if (rss_type && rss_type < 0x8U) {
				buff->is_hash_l4 = (rss_type == 0x4 ||
				rss_type == 0x5);
				buff->rss_hash = rxd_wb->rss_hash;
			}
		}

		buff->is_lro = !!(HW_ATL_B0_RXD_WB_STAT2_RSCCNT &
				  rxd_wb->status);
		if (HW_ATL_B0_RXD_WB_STAT2_EOP & rxd_wb->status) {
			buff->len = rxd_wb->pkt_len %
				AQ_CFG_RX_FRAME_MAX;
			buff->len = buff->len ?
				buff->len : AQ_CFG_RX_FRAME_MAX;
			buff->next = 0U;
			buff->is_eop = 1U;
		} else {
			buff->len =
				rxd_wb->pkt_len > AQ_CFG_RX_FRAME_MAX ?
				AQ_CFG_RX_FRAME_MAX : rxd_wb->pkt_len;

			if (buff->is_lro) {
				/* LRO */
				buff->next = rxd_wb->next_desc_ptr;
				++ring->stats.rx.lro_packets;
			} else {
				/* jumbo */
				buff->next =
					aq_ring_next_dx(ring,
							ring->hw_head);
				++ring->stats.rx.jumbo_packets;
			}
		}
	}

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_irq_enable(struct aq_hw_s *self, u64 mask)
{
	hw_atl_itr_irq_msk_setlsw_set(self, LODWORD(mask));

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_irq_disable(struct aq_hw_s *self, u64 mask)
{
	hw_atl_itr_irq_msk_clearlsw_set(self, LODWORD(mask));
	hw_atl_itr_irq_status_clearlsw_set(self, LODWORD(mask));

	atomic_inc(&self->dpc);

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_irq_read(struct aq_hw_s *self, u64 *mask)
{
	*mask = hw_atl_itr_irq_statuslsw_get(self);

	return aq_hw_err_from_flags(self);
}

#define IS_FILTER_ENABLED(_F_) ((packet_filter & (_F_)) ? 1U : 0U)

int hw_atl_b0_hw_packet_filter_set(struct aq_hw_s *self,
				   unsigned int packet_filter)
{
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;
	unsigned int i = 0U;
	u32 vlan_promisc;
	u32 l2_promisc;

	l2_promisc = IS_FILTER_ENABLED(IFF_PROMISC) ||
		     !!(cfg->priv_flags & BIT(AQ_HW_LOOPBACK_DMA_NET));
	vlan_promisc = l2_promisc || cfg->is_vlan_force_promisc;

	hw_atl_rpfl2promiscuous_mode_en_set(self, l2_promisc);

	hw_atl_rpf_vlan_prom_mode_en_set(self, vlan_promisc);

	hw_atl_rpfl2multicast_flr_en_set(self,
					 IS_FILTER_ENABLED(IFF_ALLMULTI) &&
					 IS_FILTER_ENABLED(IFF_MULTICAST), 0);

	hw_atl_rpfl2_accept_all_mc_packets_set(self,
					      IS_FILTER_ENABLED(IFF_ALLMULTI) &&
					      IS_FILTER_ENABLED(IFF_MULTICAST));

	hw_atl_rpfl2broadcast_en_set(self, IS_FILTER_ENABLED(IFF_BROADCAST));


	for (i = HW_ATL_B0_MAC_MIN; i < HW_ATL_B0_MAC_MAX; ++i)
		hw_atl_rpfl2_uc_flr_en_set(self,
					   (cfg->is_mc_list_enabled &&
					    (i <= cfg->mc_list_count)) ?
					   1U : 0U, i);

	return aq_hw_err_from_flags(self);
}

#undef IS_FILTER_ENABLED

static int hw_atl_b0_hw_multicast_list_set(struct aq_hw_s *self,
					   u8 ar_mac
					   [AQ_HW_MULTICAST_ADDRESS_MAX]
					   [ETH_ALEN],
					   u32 count)
{
	int err = 0;
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;

	if (count > (HW_ATL_B0_MAC_MAX - HW_ATL_B0_MAC_MIN)) {
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

		hw_atl_rpfl2_uc_flr_en_set(self, 0U, HW_ATL_B0_MAC_MIN + i);

		hw_atl_rpfl2unicast_dest_addresslsw_set(self, l,
							HW_ATL_B0_MAC_MIN + i);

		hw_atl_rpfl2unicast_dest_addressmsw_set(self, h,
							HW_ATL_B0_MAC_MIN + i);

		hw_atl_rpfl2_uc_flr_en_set(self,
					   (cfg->is_mc_list_enabled),
					   HW_ATL_B0_MAC_MIN + i);
	}

	err = aq_hw_err_from_flags(self);

err_exit:
	return err;
}

static int hw_atl_b0_hw_interrupt_moderation_set(struct aq_hw_s *self)
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

			tx_max_timer = min(HW_ATL_INTR_MODER_MAX, tx_max_timer);
			tx_min_timer = min(HW_ATL_INTR_MODER_MIN, tx_min_timer);
			rx_max_timer = min(HW_ATL_INTR_MODER_MAX, rx_max_timer);
			rx_min_timer = min(HW_ATL_INTR_MODER_MIN, rx_min_timer);

			itr_tx |= tx_min_timer << 0x8U;
			itr_tx |= tx_max_timer << 0x10U;
			itr_rx |= rx_min_timer << 0x8U;
			itr_rx |= rx_max_timer << 0x10U;
		} else {
			static unsigned int hw_atl_b0_timers_table_tx_[][2] = {
				{0xfU, 0xffU}, /* 10Gbit */
				{0xfU, 0x1ffU}, /* 5Gbit */
				{0xfU, 0x1ffU}, /* 5Gbit 5GS */
				{0xfU, 0x1ffU}, /* 2.5Gbit */
				{0xfU, 0x1ffU}, /* 1Gbit */
				{0xfU, 0x1ffU}, /* 100Mbit */
			};

			static unsigned int hw_atl_b0_timers_table_rx_[][2] = {
				{0x6U, 0x38U},/* 10Gbit */
				{0xCU, 0x70U},/* 5Gbit */
				{0xCU, 0x70U},/* 5Gbit 5GS */
				{0x18U, 0xE0U},/* 2.5Gbit */
				{0x30U, 0x80U},/* 1Gbit */
				{0x4U, 0x50U},/* 100Mbit */
			};

			unsigned int speed_index =
					hw_atl_utils_mbps_2_speed_index(
						self->aq_link_status.mbps);

			/* Update user visible ITR settings */
			self->aq_nic_cfg->tx_itr = hw_atl_b0_timers_table_tx_
							[speed_index][1] * 2;
			self->aq_nic_cfg->rx_itr = hw_atl_b0_timers_table_rx_
							[speed_index][1] * 2;

			itr_tx |= hw_atl_b0_timers_table_tx_
						[speed_index][0] << 0x8U;
			itr_tx |= hw_atl_b0_timers_table_tx_
						[speed_index][1] << 0x10U;

			itr_rx |= hw_atl_b0_timers_table_rx_
						[speed_index][0] << 0x8U;
			itr_rx |= hw_atl_b0_timers_table_rx_
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

	for (i = HW_ATL_B0_RINGS_MAX; i--;) {
		hw_atl_reg_tx_intr_moder_ctrl_set(self, itr_tx, i);
		hw_atl_reg_rx_intr_moder_ctrl_set(self, itr_rx, i);
	}

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_hw_stop(struct aq_hw_s *self)
{
	int err;
	u32 val;

	hw_atl_b0_hw_irq_disable(self, HW_ATL_B0_INT_MASK);

	/* Invalidate Descriptor Cache to prevent writing to the cached
	 * descriptors and to the data pointer of those descriptors
	 */
	hw_atl_rdm_rx_dma_desc_cache_init_tgl(self);

	err = aq_hw_err_from_flags(self);

	if (err)
		goto err_exit;

	readx_poll_timeout_atomic(hw_atl_rdm_rx_dma_desc_cache_init_done_get,
				  self, val, val == 1, 1000U, 10000U);

err_exit:
	return err;
}

int hw_atl_b0_hw_ring_tx_stop(struct aq_hw_s *self, struct aq_ring_s *ring)
{
	hw_atl_tdm_tx_desc_en_set(self, 0U, ring->idx);

	return aq_hw_err_from_flags(self);
}

int hw_atl_b0_hw_ring_rx_stop(struct aq_hw_s *self, struct aq_ring_s *ring)
{
	hw_atl_rdm_rx_desc_en_set(self, 0U, ring->idx);

	return aq_hw_err_from_flags(self);
}

#define get_ptp_ts_val_u64(self, indx) \
	((u64)(hw_atl_pcs_ptp_clock_get(self, indx) & 0xffff))

static void hw_atl_b0_get_ptp_ts(struct aq_hw_s *self, u64 *stamp)
{
	u64 ns;

	hw_atl_pcs_ptp_clock_read_enable(self, 1);
	hw_atl_pcs_ptp_clock_read_enable(self, 0);
	ns = (get_ptp_ts_val_u64(self, 0) +
	      (get_ptp_ts_val_u64(self, 1) << 16)) * NSEC_PER_SEC +
	     (get_ptp_ts_val_u64(self, 3) +
	      (get_ptp_ts_val_u64(self, 4) << 16));

	*stamp = ns + self->ptp_clk_offset;
}

static void hw_atl_b0_adj_params_get(u64 freq, s64 adj, u32 *ns, u32 *fns)
{
	/* For accuracy, the digit is extended */
	s64 base_ns = ((adj + NSEC_PER_SEC) * NSEC_PER_SEC);
	u64 nsi_frac = 0;
	u64 nsi;

	base_ns = div64_s64(base_ns, freq);
	nsi = div64_u64(base_ns, NSEC_PER_SEC);

	if (base_ns != nsi * NSEC_PER_SEC) {
		s64 divisor = div64_s64((s64)NSEC_PER_SEC * NSEC_PER_SEC,
					base_ns - nsi * NSEC_PER_SEC);
		nsi_frac = div64_s64(FRAC_PER_NS * NSEC_PER_SEC, divisor);
	}

	*ns = (u32)nsi;
	*fns = (u32)nsi_frac;
}

static void
hw_atl_b0_mac_adj_param_calc(struct hw_fw_request_ptp_adj_freq *ptp_adj_freq,
			     u64 phyfreq, u64 macfreq)
{
	s64 adj_fns_val;
	s64 fns_in_sec_phy = phyfreq * (ptp_adj_freq->fns_phy +
					FRAC_PER_NS * ptp_adj_freq->ns_phy);
	s64 fns_in_sec_mac = macfreq * (ptp_adj_freq->fns_mac +
					FRAC_PER_NS * ptp_adj_freq->ns_mac);
	s64 fault_in_sec_phy = FRAC_PER_NS * NSEC_PER_SEC - fns_in_sec_phy;
	s64 fault_in_sec_mac = FRAC_PER_NS * NSEC_PER_SEC - fns_in_sec_mac;
	/* MAC MCP counter freq is macfreq / 4 */
	s64 diff_in_mcp_overflow = (fault_in_sec_mac - fault_in_sec_phy) *
				   4 * FRAC_PER_NS;

	diff_in_mcp_overflow = div64_s64(diff_in_mcp_overflow,
					 AQ_HW_MAC_COUNTER_HZ);
	adj_fns_val = (ptp_adj_freq->fns_mac + FRAC_PER_NS *
		       ptp_adj_freq->ns_mac) + diff_in_mcp_overflow;

	ptp_adj_freq->mac_ns_adj = div64_s64(adj_fns_val, FRAC_PER_NS);
	ptp_adj_freq->mac_fns_adj = adj_fns_val - ptp_adj_freq->mac_ns_adj *
				    FRAC_PER_NS;
}

static int hw_atl_b0_adj_sys_clock(struct aq_hw_s *self, s64 delta)
{
	self->ptp_clk_offset += delta;

	self->aq_fw_ops->adjust_ptp(self, self->ptp_clk_offset);

	return 0;
}

static int hw_atl_b0_set_sys_clock(struct aq_hw_s *self, u64 time, u64 ts)
{
	s64 delta = time - (self->ptp_clk_offset + ts);

	return hw_atl_b0_adj_sys_clock(self, delta);
}

static int hw_atl_b0_ts_to_sys_clock(struct aq_hw_s *self, u64 ts, u64 *time)
{
	*time = self->ptp_clk_offset + ts;
	return 0;
}

static int hw_atl_b0_adj_clock_freq(struct aq_hw_s *self, s32 ppb)
{
	struct hw_fw_request_iface fwreq;
	size_t size;

	memset(&fwreq, 0, sizeof(fwreq));

	fwreq.msg_id = HW_AQ_FW_REQUEST_PTP_ADJ_FREQ;
	hw_atl_b0_adj_params_get(AQ_HW_MAC_COUNTER_HZ, ppb,
				 &fwreq.ptp_adj_freq.ns_mac,
				 &fwreq.ptp_adj_freq.fns_mac);
	hw_atl_b0_adj_params_get(AQ_HW_PHY_COUNTER_HZ, ppb,
				 &fwreq.ptp_adj_freq.ns_phy,
				 &fwreq.ptp_adj_freq.fns_phy);
	hw_atl_b0_mac_adj_param_calc(&fwreq.ptp_adj_freq,
				     AQ_HW_PHY_COUNTER_HZ,
				     AQ_HW_MAC_COUNTER_HZ);

	size = sizeof(fwreq.msg_id) + sizeof(fwreq.ptp_adj_freq);
	return self->aq_fw_ops->send_fw_request(self, &fwreq, size);
}

static int hw_atl_b0_gpio_pulse(struct aq_hw_s *self, u32 index,
				u64 start, u32 period)
{
	struct hw_fw_request_iface fwreq;
	size_t size;

	memset(&fwreq, 0, sizeof(fwreq));

	fwreq.msg_id = HW_AQ_FW_REQUEST_PTP_GPIO_CTRL;
	fwreq.ptp_gpio_ctrl.index = index;
	fwreq.ptp_gpio_ctrl.period = period;
	/* Apply time offset */
	fwreq.ptp_gpio_ctrl.start = start;

	size = sizeof(fwreq.msg_id) + sizeof(fwreq.ptp_gpio_ctrl);
	return self->aq_fw_ops->send_fw_request(self, &fwreq, size);
}

static int hw_atl_b0_extts_gpio_enable(struct aq_hw_s *self, u32 index,
				       u32 enable)
{
	/* Enable/disable Sync1588 GPIO Timestamping */
	aq_phy_write_reg(self, MDIO_MMD_PCS, 0xc611, enable ? 0x71 : 0);

	return 0;
}

static int hw_atl_b0_get_sync_ts(struct aq_hw_s *self, u64 *ts)
{
	u64 sec_l;
	u64 sec_h;
	u64 nsec_l;
	u64 nsec_h;

	if (!ts)
		return -1;

	/* PTP external GPIO clock seconds count 15:0 */
	sec_l = aq_phy_read_reg(self, MDIO_MMD_PCS, 0xc914);
	/* PTP external GPIO clock seconds count 31:16 */
	sec_h = aq_phy_read_reg(self, MDIO_MMD_PCS, 0xc915);
	/* PTP external GPIO clock nanoseconds count 15:0 */
	nsec_l = aq_phy_read_reg(self, MDIO_MMD_PCS, 0xc916);
	/* PTP external GPIO clock nanoseconds count 31:16 */
	nsec_h = aq_phy_read_reg(self, MDIO_MMD_PCS, 0xc917);

	*ts = (nsec_h << 16) + nsec_l + ((sec_h << 16) + sec_l) * NSEC_PER_SEC;

	return 0;
}

static u16 hw_atl_b0_rx_extract_ts(struct aq_hw_s *self, u8 *p,
				   unsigned int len, u64 *timestamp)
{
	unsigned int offset = 14;
	struct ethhdr *eth;
	__be64 sec;
	__be32 ns;
	u8 *ptr;

	if (len <= offset || !timestamp)
		return 0;

	/* The TIMESTAMP in the end of package has following format:
	 * (big-endian)
	 *   struct {
	 *     uint64_t sec;
	 *     uint32_t ns;
	 *     uint16_t stream_id;
	 *   };
	 */
	ptr = p + (len - offset);
	memcpy(&sec, ptr, sizeof(sec));
	ptr += sizeof(sec);
	memcpy(&ns, ptr, sizeof(ns));

	*timestamp = (be64_to_cpu(sec) & 0xffffffffffffllu) * NSEC_PER_SEC +
		     be32_to_cpu(ns) + self->ptp_clk_offset;

	eth = (struct ethhdr *)p;

	return (eth->h_proto == htons(ETH_P_1588)) ? 12 : 14;
}

static int hw_atl_b0_extract_hwts(struct aq_hw_s *self, u8 *p, unsigned int len,
				  u64 *timestamp)
{
	struct hw_atl_rxd_hwts_wb_s *hwts_wb = (struct hw_atl_rxd_hwts_wb_s *)p;
	u64 tmp, sec, ns;

	sec = 0;
	tmp = (hwts_wb->sec_lw0 >> 2) & 0x3ff;
	sec += tmp;
	tmp = (u64)((hwts_wb->sec_lw1 >> 16) & 0xffff) << 10;
	sec += tmp;
	tmp = (u64)(hwts_wb->sec_hw & 0xfff) << 26;
	sec += tmp;
	tmp = (u64)((hwts_wb->sec_hw >> 22) & 0x3ff) << 38;
	sec += tmp;
	ns = sec * NSEC_PER_SEC + hwts_wb->ns;
	if (timestamp)
		*timestamp = ns + self->ptp_clk_offset;
	return 0;
}

static int hw_atl_b0_hw_fl3l4_clear(struct aq_hw_s *self,
				    struct aq_rx_filter_l3l4 *data)
{
	u8 location = data->location;

	if (!data->is_ipv6) {
		hw_atl_rpfl3l4_cmd_clear(self, location);
		hw_atl_rpf_l4_spd_set(self, 0U, location);
		hw_atl_rpf_l4_dpd_set(self, 0U, location);
		hw_atl_rpfl3l4_ipv4_src_addr_clear(self, location);
		hw_atl_rpfl3l4_ipv4_dest_addr_clear(self, location);
	} else {
		int i;

		for (i = 0; i < HW_ATL_RX_CNT_REG_ADDR_IPV6; ++i) {
			hw_atl_rpfl3l4_cmd_clear(self, location + i);
			hw_atl_rpf_l4_spd_set(self, 0U, location + i);
			hw_atl_rpf_l4_dpd_set(self, 0U, location + i);
		}
		hw_atl_rpfl3l4_ipv6_src_addr_clear(self, location);
		hw_atl_rpfl3l4_ipv6_dest_addr_clear(self, location);
	}

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_hw_fl3l4_set(struct aq_hw_s *self,
				  struct aq_rx_filter_l3l4 *data)
{
	u8 location = data->location;

	hw_atl_b0_hw_fl3l4_clear(self, data);

	if (data->cmd & (HW_ATL_RX_ENABLE_CMP_DEST_ADDR_L3 |
			 HW_ATL_RX_ENABLE_CMP_SRC_ADDR_L3)) {
		if (!data->is_ipv6) {
			hw_atl_rpfl3l4_ipv4_dest_addr_set(self,
							  location,
							  data->ip_dst[0]);
			hw_atl_rpfl3l4_ipv4_src_addr_set(self,
							 location,
							 data->ip_src[0]);
		} else {
			hw_atl_rpfl3l4_ipv6_dest_addr_set(self,
							  location,
							  data->ip_dst);
			hw_atl_rpfl3l4_ipv6_src_addr_set(self,
							 location,
							 data->ip_src);
		}
	}

	if (data->cmd & (HW_ATL_RX_ENABLE_CMP_DEST_PORT_L4 |
			 HW_ATL_RX_ENABLE_CMP_SRC_PORT_L4)) {
		hw_atl_rpf_l4_dpd_set(self, data->p_dst, location);
		hw_atl_rpf_l4_spd_set(self, data->p_src, location);
	}

	hw_atl_rpfl3l4_cmd_set(self, location, data->cmd);

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_hw_fl2_set(struct aq_hw_s *self,
				struct aq_rx_filter_l2 *data)
{
	hw_atl_rpf_etht_flr_en_set(self, 1U, data->location);
	hw_atl_rpf_etht_flr_set(self, data->ethertype, data->location);
	hw_atl_rpf_etht_user_priority_en_set(self,
					     !!data->user_priority_en,
					     data->location);
	if (data->user_priority_en)
		hw_atl_rpf_etht_user_priority_set(self,
						  data->user_priority,
						  data->location);

	if (data->queue < 0) {
		hw_atl_rpf_etht_flr_act_set(self, 0U, data->location);
		hw_atl_rpf_etht_rx_queue_en_set(self, 0U, data->location);
	} else {
		hw_atl_rpf_etht_flr_act_set(self, 1U, data->location);
		hw_atl_rpf_etht_rx_queue_en_set(self, 1U, data->location);
		hw_atl_rpf_etht_rx_queue_set(self, data->queue, data->location);
	}

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_hw_fl2_clear(struct aq_hw_s *self,
				  struct aq_rx_filter_l2 *data)
{
	hw_atl_rpf_etht_flr_en_set(self, 0U, data->location);
	hw_atl_rpf_etht_flr_set(self, 0U, data->location);
	hw_atl_rpf_etht_user_priority_en_set(self, 0U, data->location);

	return aq_hw_err_from_flags(self);
}

/**
 * @brief Set VLAN filter table
 * @details Configure VLAN filter table to accept (and assign the queue) traffic
 *  for the particular vlan ids.
 * Note: use this function under vlan promisc mode not to lost the traffic
 *
 * @param aq_hw_s
 * @param aq_rx_filter_vlan VLAN filter configuration
 * @return 0 - OK, <0 - error
 */
static int hw_atl_b0_hw_vlan_set(struct aq_hw_s *self,
				 struct aq_rx_filter_vlan *aq_vlans)
{
	int i;

	for (i = 0; i < AQ_VLAN_MAX_FILTERS; i++) {
		hw_atl_rpf_vlan_flr_en_set(self, 0U, i);
		hw_atl_rpf_vlan_rxq_en_flr_set(self, 0U, i);
		if (aq_vlans[i].enable) {
			hw_atl_rpf_vlan_id_flr_set(self,
						   aq_vlans[i].vlan_id,
						   i);
			hw_atl_rpf_vlan_flr_act_set(self, 1U, i);
			hw_atl_rpf_vlan_flr_en_set(self, 1U, i);
			if (aq_vlans[i].queue != 0xFF) {
				hw_atl_rpf_vlan_rxq_flr_set(self,
							    aq_vlans[i].queue,
							    i);
				hw_atl_rpf_vlan_rxq_en_flr_set(self, 1U, i);
			}
		}
	}

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_hw_vlan_ctrl(struct aq_hw_s *self, bool enable)
{
	/* set promisc in case of disabing the vland filter */
	hw_atl_rpf_vlan_prom_mode_en_set(self, !enable);

	return aq_hw_err_from_flags(self);
}

static int hw_atl_b0_set_loopback(struct aq_hw_s *self, u32 mode, bool enable)
{
	switch (mode) {
	case AQ_HW_LOOPBACK_DMA_SYS:
		hw_atl_tpb_tx_dma_sys_lbk_en_set(self, enable);
		hw_atl_rpb_dma_sys_lbk_set(self, enable);
		break;
	case AQ_HW_LOOPBACK_PKT_SYS:
		hw_atl_tpo_tx_pkt_sys_lbk_en_set(self, enable);
		hw_atl_rpf_tpo_to_rpf_sys_lbk_set(self, enable);
		break;
	case AQ_HW_LOOPBACK_DMA_NET:
		hw_atl_rpf_vlan_prom_mode_en_set(self, enable);
		hw_atl_rpfl2promiscuous_mode_en_set(self, enable);
		hw_atl_tpb_tx_tx_clk_gate_en_set(self, !enable);
		hw_atl_tpb_tx_dma_net_lbk_en_set(self, enable);
		hw_atl_rpb_dma_net_lbk_set(self, enable);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

const struct aq_hw_ops hw_atl_ops_b0 = {
	.hw_soft_reset        = hw_atl_utils_soft_reset,
	.hw_prepare           = hw_atl_utils_initfw,
	.hw_set_mac_address   = hw_atl_b0_hw_mac_addr_set,
	.hw_init              = hw_atl_b0_hw_init,
	.hw_reset             = hw_atl_b0_hw_reset,
	.hw_start             = hw_atl_b0_hw_start,
	.hw_ring_tx_start     = hw_atl_b0_hw_ring_tx_start,
	.hw_ring_tx_stop      = hw_atl_b0_hw_ring_tx_stop,
	.hw_ring_rx_start     = hw_atl_b0_hw_ring_rx_start,
	.hw_ring_rx_stop      = hw_atl_b0_hw_ring_rx_stop,
	.hw_stop              = hw_atl_b0_hw_stop,

	.hw_ring_tx_xmit         = hw_atl_b0_hw_ring_tx_xmit,
	.hw_ring_tx_head_update  = hw_atl_b0_hw_ring_tx_head_update,

	.hw_ring_rx_receive      = hw_atl_b0_hw_ring_rx_receive,
	.hw_ring_rx_fill         = hw_atl_b0_hw_ring_rx_fill,

	.hw_irq_enable           = hw_atl_b0_hw_irq_enable,
	.hw_irq_disable          = hw_atl_b0_hw_irq_disable,
	.hw_irq_read             = hw_atl_b0_hw_irq_read,

	.hw_ring_rx_init             = hw_atl_b0_hw_ring_rx_init,
	.hw_ring_tx_init             = hw_atl_b0_hw_ring_tx_init,
	.hw_packet_filter_set        = hw_atl_b0_hw_packet_filter_set,
	.hw_filter_l2_set            = hw_atl_b0_hw_fl2_set,
	.hw_filter_l2_clear          = hw_atl_b0_hw_fl2_clear,
	.hw_filter_l3l4_set          = hw_atl_b0_hw_fl3l4_set,
	.hw_filter_vlan_set          = hw_atl_b0_hw_vlan_set,
	.hw_filter_vlan_ctrl         = hw_atl_b0_hw_vlan_ctrl,
	.hw_multicast_list_set       = hw_atl_b0_hw_multicast_list_set,
	.hw_interrupt_moderation_set = hw_atl_b0_hw_interrupt_moderation_set,
	.hw_rss_set                  = hw_atl_b0_hw_rss_set,
	.hw_rss_hash_set             = hw_atl_b0_hw_rss_hash_set,
	.hw_tc_rate_limit_set        = hw_atl_b0_hw_init_tx_tc_rate_limit,
	.hw_get_regs                 = hw_atl_utils_hw_get_regs,
	.hw_get_hw_stats             = hw_atl_utils_get_hw_stats,
	.hw_get_fw_version           = hw_atl_utils_get_fw_version,

	.hw_ring_hwts_rx_fill        = hw_atl_b0_hw_ring_hwts_rx_fill,
	.hw_ring_hwts_rx_receive     = hw_atl_b0_hw_ring_hwts_rx_receive,

	.hw_get_ptp_ts           = hw_atl_b0_get_ptp_ts,
	.hw_adj_sys_clock        = hw_atl_b0_adj_sys_clock,
	.hw_set_sys_clock        = hw_atl_b0_set_sys_clock,
	.hw_ts_to_sys_clock      = hw_atl_b0_ts_to_sys_clock,
	.hw_adj_clock_freq       = hw_atl_b0_adj_clock_freq,
	.hw_gpio_pulse           = hw_atl_b0_gpio_pulse,
	.hw_extts_gpio_enable    = hw_atl_b0_extts_gpio_enable,
	.hw_get_sync_ts          = hw_atl_b0_get_sync_ts,
	.rx_extract_ts           = hw_atl_b0_rx_extract_ts,
	.extract_hwts            = hw_atl_b0_extract_hwts,
	.hw_set_offload          = hw_atl_b0_hw_offload_set,
	.hw_set_loopback         = hw_atl_b0_set_loopback,
	.hw_set_fc               = hw_atl_b0_set_fc,
};
