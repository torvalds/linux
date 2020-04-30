// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include "aq_hw.h"
#include "aq_hw_utils.h"
#include "aq_nic.h"
#include "hw_atl/hw_atl_utils.h"
#include "hw_atl/hw_atl_llh.h"
#include "hw_atl2_utils.h"
#include "hw_atl2_llh.h"
#include "hw_atl2_internal.h"

static int hw_atl2_act_rslvr_table_set(struct aq_hw_s *self, u8 location,
				       u32 tag, u32 mask, u32 action);

#define DEFAULT_BOARD_BASIC_CAPABILITIES \
	.is_64_dma = true,		  \
	.msix_irqs = 8U,		  \
	.irq_mask = ~0U,		  \
	.vecs = HW_ATL2_RSS_MAX,	  \
	.tcs = HW_ATL2_TC_MAX,	  \
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
			NETIF_F_GSO_PARTIAL,          \
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
			  AQ_NIC_RATE_2GS |
			  AQ_NIC_RATE_1G  |
			  AQ_NIC_RATE_100M      |
			  AQ_NIC_RATE_10M,
};

static u32 hw_atl2_sem_act_rslvr_get(struct aq_hw_s *self)
{
	return hw_atl_reg_glb_cpu_sem_get(self, HW_ATL2_FW_SM_ACT_RSLVR);
}

static int hw_atl2_hw_reset(struct aq_hw_s *self)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_rss_hash_set(struct aq_hw_s *self,
				   struct aq_rss_parameters *rss_params)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_rss_set(struct aq_hw_s *self,
			      struct aq_rss_parameters *rss_params)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_offload_set(struct aq_hw_s *self,
				  struct aq_nic_cfg_s *aq_nic_cfg)
{
	return -EOPNOTSUPP;
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

static int hw_atl2_hw_mac_addr_set(struct aq_hw_s *self, u8 *mac_addr)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_init(struct aq_hw_s *self, u8 *mac_addr)
{
	struct hw_atl2_priv *priv = (struct hw_atl2_priv *)self->priv;
	u8 base_index, count;
	int err;

	err = hw_atl2_utils_get_action_resolve_table_caps(self, &base_index,
							  &count);
	if (err)
		return err;

	priv->art_base_index = 8 * base_index;

	return -EOPNOTSUPP;
}

static int hw_atl2_hw_ring_tx_start(struct aq_hw_s *self,
				    struct aq_ring_s *ring)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_ring_rx_start(struct aq_hw_s *self,
				    struct aq_ring_s *ring)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_start(struct aq_hw_s *self)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_ring_tx_xmit(struct aq_hw_s *self,
				   struct aq_ring_s *ring,
				   unsigned int frags)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_ring_rx_init(struct aq_hw_s *self,
				   struct aq_ring_s *aq_ring,
				   struct aq_ring_param_s *aq_ring_param)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_ring_tx_init(struct aq_hw_s *self,
				   struct aq_ring_s *aq_ring,
				   struct aq_ring_param_s *aq_ring_param)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_ring_rx_fill(struct aq_hw_s *self, struct aq_ring_s *ring,
				   unsigned int sw_tail_old)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_ring_tx_head_update(struct aq_hw_s *self,
					  struct aq_ring_s *ring)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_ring_rx_receive(struct aq_hw_s *self,
				      struct aq_ring_s *ring)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_irq_enable(struct aq_hw_s *self, u64 mask)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_irq_disable(struct aq_hw_s *self, u64 mask)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_irq_read(struct aq_hw_s *self, u64 *mask)
{
	return -EOPNOTSUPP;
}

#define IS_FILTER_ENABLED(_F_) ((packet_filter & (_F_)) ? 1U : 0U)

static int hw_atl2_hw_packet_filter_set(struct aq_hw_s *self,
					unsigned int packet_filter)
{
	struct aq_nic_cfg_s *cfg = self->aq_nic_cfg;
	u32 vlan_promisc;
	u32 l2_promisc;
	unsigned int i;

	l2_promisc = IS_FILTER_ENABLED(IFF_PROMISC) ||
		     !!(cfg->priv_flags & BIT(AQ_HW_LOOPBACK_DMA_NET));
	vlan_promisc = l2_promisc || cfg->is_vlan_force_promisc;

	hw_atl_rpfl2promiscuous_mode_en_set(self, l2_promisc);

	hw_atl_rpf_vlan_prom_mode_en_set(self, vlan_promisc);

	hw_atl2_hw_new_rx_filter_promisc(self, IS_FILTER_ENABLED(IFF_PROMISC));

	hw_atl_rpfl2multicast_flr_en_set(self,
					 IS_FILTER_ENABLED(IFF_ALLMULTI) &&
					 IS_FILTER_ENABLED(IFF_MULTICAST), 0);

	hw_atl_rpfl2_accept_all_mc_packets_set(self,
					      IS_FILTER_ENABLED(IFF_ALLMULTI) &&
					      IS_FILTER_ENABLED(IFF_MULTICAST));

	hw_atl_rpfl2broadcast_en_set(self, IS_FILTER_ENABLED(IFF_BROADCAST));

	for (i = HW_ATL2_MAC_MIN; i < HW_ATL2_MAC_MAX; ++i)
		hw_atl_rpfl2_uc_flr_en_set(self,
					   (cfg->is_mc_list_enabled &&
					    (i <= cfg->mc_list_count)) ?
				    1U : 0U, i);

	return aq_hw_err_from_flags(self);
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
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_stop(struct aq_hw_s *self)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_ring_tx_stop(struct aq_hw_s *self, struct aq_ring_s *ring)
{
	return -EOPNOTSUPP;
}

static int hw_atl2_hw_ring_rx_stop(struct aq_hw_s *self, struct aq_ring_s *ring)
{
	return -EOPNOTSUPP;
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
	.hw_set_mac_address   = hw_atl2_hw_mac_addr_set,
	.hw_init              = hw_atl2_hw_init,
	.hw_reset             = hw_atl2_hw_reset,
	.hw_start             = hw_atl2_hw_start,
	.hw_ring_tx_start     = hw_atl2_hw_ring_tx_start,
	.hw_ring_tx_stop      = hw_atl2_hw_ring_tx_stop,
	.hw_ring_rx_start     = hw_atl2_hw_ring_rx_start,
	.hw_ring_rx_stop      = hw_atl2_hw_ring_rx_stop,
	.hw_stop              = hw_atl2_hw_stop,

	.hw_ring_tx_xmit         = hw_atl2_hw_ring_tx_xmit,
	.hw_ring_tx_head_update  = hw_atl2_hw_ring_tx_head_update,

	.hw_ring_rx_receive      = hw_atl2_hw_ring_rx_receive,
	.hw_ring_rx_fill         = hw_atl2_hw_ring_rx_fill,

	.hw_irq_enable           = hw_atl2_hw_irq_enable,
	.hw_irq_disable          = hw_atl2_hw_irq_disable,
	.hw_irq_read             = hw_atl2_hw_irq_read,

	.hw_ring_rx_init             = hw_atl2_hw_ring_rx_init,
	.hw_ring_tx_init             = hw_atl2_hw_ring_tx_init,
	.hw_packet_filter_set        = hw_atl2_hw_packet_filter_set,
	.hw_filter_vlan_set          = hw_atl2_hw_vlan_set,
	.hw_filter_vlan_ctrl         = hw_atl2_hw_vlan_ctrl,
	.hw_multicast_list_set       = hw_atl2_hw_multicast_list_set,
	.hw_interrupt_moderation_set = hw_atl2_hw_interrupt_moderation_set,
	.hw_rss_set                  = hw_atl2_hw_rss_set,
	.hw_rss_hash_set             = hw_atl2_hw_rss_hash_set,
	.hw_get_hw_stats             = hw_atl2_utils_get_hw_stats,
	.hw_set_offload              = hw_atl2_hw_offload_set,
};
