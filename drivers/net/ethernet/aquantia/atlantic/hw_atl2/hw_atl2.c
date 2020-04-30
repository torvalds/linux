// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include "aq_hw.h"
#include "hw_atl2_utils.h"
#include "hw_atl2_internal.h"

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
	.hw_interrupt_moderation_set = hw_atl2_hw_interrupt_moderation_set,
	.hw_rss_set                  = hw_atl2_hw_rss_set,
	.hw_rss_hash_set             = hw_atl2_hw_rss_hash_set,
	.hw_get_hw_stats             = hw_atl2_utils_get_hw_stats,
	.hw_set_offload              = hw_atl2_hw_offload_set,
};
