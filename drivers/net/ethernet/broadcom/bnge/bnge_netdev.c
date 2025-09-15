// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <asm/byteorder.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if.h>
#include <net/ip.h>
#include <linux/skbuff.h>

#include "bnge.h"
#include "bnge_hwrm_lib.h"
#include "bnge_ethtool.h"

static netdev_tx_t bnge_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static int bnge_open(struct net_device *dev)
{
	return 0;
}

static int bnge_close(struct net_device *dev)
{
	return 0;
}

static const struct net_device_ops bnge_netdev_ops = {
	.ndo_open		= bnge_open,
	.ndo_stop		= bnge_close,
	.ndo_start_xmit		= bnge_start_xmit,
};

static void bnge_init_mac_addr(struct bnge_dev *bd)
{
	eth_hw_addr_set(bd->netdev, bd->pf.mac_addr);
}

static void bnge_set_tpa_flags(struct bnge_dev *bd)
{
	struct bnge_net *bn = netdev_priv(bd->netdev);

	bn->priv_flags &= ~BNGE_NET_EN_TPA;

	if (bd->netdev->features & NETIF_F_LRO)
		bn->priv_flags |= BNGE_NET_EN_LRO;
	else if (bd->netdev->features & NETIF_F_GRO_HW)
		bn->priv_flags |= BNGE_NET_EN_GRO;
}

static void bnge_init_l2_fltr_tbl(struct bnge_net *bn)
{
	int i;

	for (i = 0; i < BNGE_L2_FLTR_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&bn->l2_fltr_hash_tbl[i]);
	get_random_bytes(&bn->hash_seed, sizeof(bn->hash_seed));
}

void bnge_set_ring_params(struct bnge_dev *bd)
{
	struct bnge_net *bn = netdev_priv(bd->netdev);
	u32 ring_size, rx_size, rx_space, max_rx_cmpl;
	u32 agg_factor = 0, agg_ring_size = 0;

	/* 8 for CRC and VLAN */
	rx_size = SKB_DATA_ALIGN(bn->netdev->mtu + ETH_HLEN + NET_IP_ALIGN + 8);

	rx_space = rx_size + ALIGN(NET_SKB_PAD, 8) +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	bn->rx_copy_thresh = BNGE_RX_COPY_THRESH;
	ring_size = bn->rx_ring_size;
	bn->rx_agg_ring_size = 0;
	bn->rx_agg_nr_pages = 0;

	if (bn->priv_flags & BNGE_NET_EN_TPA)
		agg_factor = min_t(u32, 4, 65536 / BNGE_RX_PAGE_SIZE);

	bn->priv_flags &= ~BNGE_NET_EN_JUMBO;
	if (rx_space > PAGE_SIZE) {
		u32 jumbo_factor;

		bn->priv_flags |= BNGE_NET_EN_JUMBO;
		jumbo_factor = PAGE_ALIGN(bn->netdev->mtu - 40) >> PAGE_SHIFT;
		if (jumbo_factor > agg_factor)
			agg_factor = jumbo_factor;
	}
	if (agg_factor) {
		if (ring_size > BNGE_MAX_RX_DESC_CNT_JUM_ENA) {
			ring_size = BNGE_MAX_RX_DESC_CNT_JUM_ENA;
			netdev_warn(bn->netdev, "RX ring size reduced from %d to %d due to jumbo ring\n",
				    bn->rx_ring_size, ring_size);
			bn->rx_ring_size = ring_size;
		}
		agg_ring_size = ring_size * agg_factor;

		bn->rx_agg_nr_pages = bnge_adjust_pow_two(agg_ring_size,
							  RX_DESC_CNT);
		if (bn->rx_agg_nr_pages > MAX_RX_AGG_PAGES) {
			u32 tmp = agg_ring_size;

			bn->rx_agg_nr_pages = MAX_RX_AGG_PAGES;
			agg_ring_size = MAX_RX_AGG_PAGES * RX_DESC_CNT - 1;
			netdev_warn(bn->netdev, "RX agg ring size %d reduced to %d.\n",
				    tmp, agg_ring_size);
		}
		bn->rx_agg_ring_size = agg_ring_size;
		bn->rx_agg_ring_mask = (bn->rx_agg_nr_pages * RX_DESC_CNT) - 1;

		rx_size = SKB_DATA_ALIGN(BNGE_RX_COPY_THRESH + NET_IP_ALIGN);
		rx_space = rx_size + NET_SKB_PAD +
			SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	}

	bn->rx_buf_use_size = rx_size;
	bn->rx_buf_size = rx_space;

	bn->rx_nr_pages = bnge_adjust_pow_two(ring_size, RX_DESC_CNT);
	bn->rx_ring_mask = (bn->rx_nr_pages * RX_DESC_CNT) - 1;

	ring_size = bn->tx_ring_size;
	bn->tx_nr_pages = bnge_adjust_pow_two(ring_size, TX_DESC_CNT);
	bn->tx_ring_mask = (bn->tx_nr_pages * TX_DESC_CNT) - 1;

	max_rx_cmpl = bn->rx_ring_size;

	if (bn->priv_flags & BNGE_NET_EN_TPA)
		max_rx_cmpl += bd->max_tpa_v2;
	ring_size = max_rx_cmpl * 2 + agg_ring_size + bn->tx_ring_size;
	bn->cp_ring_size = ring_size;

	bn->cp_nr_pages = bnge_adjust_pow_two(ring_size, CP_DESC_CNT);
	if (bn->cp_nr_pages > MAX_CP_PAGES) {
		bn->cp_nr_pages = MAX_CP_PAGES;
		bn->cp_ring_size = MAX_CP_PAGES * CP_DESC_CNT - 1;
		netdev_warn(bn->netdev, "completion ring size %d reduced to %d.\n",
			    ring_size, bn->cp_ring_size);
	}
	bn->cp_bit = bn->cp_nr_pages * CP_DESC_CNT;
	bn->cp_ring_mask = bn->cp_bit - 1;
}

int bnge_netdev_alloc(struct bnge_dev *bd, int max_irqs)
{
	struct net_device *netdev;
	struct bnge_net *bn;
	int rc;

	netdev = alloc_etherdev_mqs(sizeof(*bn), max_irqs * BNGE_MAX_QUEUE,
				    max_irqs);
	if (!netdev)
		return -ENOMEM;

	SET_NETDEV_DEV(netdev, bd->dev);
	bd->netdev = netdev;

	netdev->netdev_ops = &bnge_netdev_ops;

	bnge_set_ethtool_ops(netdev);

	bn = netdev_priv(netdev);
	bn->netdev = netdev;
	bn->bd = bd;

	netdev->min_mtu = ETH_ZLEN;
	netdev->max_mtu = bd->max_mtu;

	netdev->hw_features = NETIF_F_IP_CSUM |
			      NETIF_F_IPV6_CSUM |
			      NETIF_F_SG |
			      NETIF_F_TSO |
			      NETIF_F_TSO6 |
			      NETIF_F_GSO_UDP_TUNNEL |
			      NETIF_F_GSO_GRE |
			      NETIF_F_GSO_IPXIP4 |
			      NETIF_F_GSO_UDP_TUNNEL_CSUM |
			      NETIF_F_GSO_GRE_CSUM |
			      NETIF_F_GSO_PARTIAL |
			      NETIF_F_RXHASH |
			      NETIF_F_RXCSUM |
			      NETIF_F_GRO;

	if (bd->flags & BNGE_EN_UDP_GSO_SUPP)
		netdev->hw_features |= NETIF_F_GSO_UDP_L4;

	if (BNGE_SUPPORTS_TPA(bd))
		netdev->hw_features |= NETIF_F_LRO;

	netdev->hw_enc_features = NETIF_F_IP_CSUM |
				  NETIF_F_IPV6_CSUM |
				  NETIF_F_SG |
				  NETIF_F_TSO |
				  NETIF_F_TSO6 |
				  NETIF_F_GSO_UDP_TUNNEL |
				  NETIF_F_GSO_GRE |
				  NETIF_F_GSO_UDP_TUNNEL_CSUM |
				  NETIF_F_GSO_GRE_CSUM |
				  NETIF_F_GSO_IPXIP4 |
				  NETIF_F_GSO_PARTIAL;

	if (bd->flags & BNGE_EN_UDP_GSO_SUPP)
		netdev->hw_enc_features |= NETIF_F_GSO_UDP_L4;

	netdev->gso_partial_features = NETIF_F_GSO_UDP_TUNNEL_CSUM |
				       NETIF_F_GSO_GRE_CSUM;

	netdev->vlan_features = netdev->hw_features | NETIF_F_HIGHDMA;
	if (bd->fw_cap & BNGE_FW_CAP_VLAN_RX_STRIP)
		netdev->hw_features |= BNGE_HW_FEATURE_VLAN_ALL_RX;
	if (bd->fw_cap & BNGE_FW_CAP_VLAN_TX_INSERT)
		netdev->hw_features |= BNGE_HW_FEATURE_VLAN_ALL_TX;

	if (BNGE_SUPPORTS_TPA(bd))
		netdev->hw_features |= NETIF_F_GRO_HW;

	netdev->features |= netdev->hw_features | NETIF_F_HIGHDMA;

	if (netdev->features & NETIF_F_GRO_HW)
		netdev->features &= ~NETIF_F_LRO;

	netdev->priv_flags |= IFF_UNICAST_FLT;

	netif_set_tso_max_size(netdev, GSO_MAX_SIZE);
	if (bd->tso_max_segs)
		netif_set_tso_max_segs(netdev, bd->tso_max_segs);

	bn->rx_ring_size = BNGE_DEFAULT_RX_RING_SIZE;
	bn->tx_ring_size = BNGE_DEFAULT_TX_RING_SIZE;

	bnge_set_tpa_flags(bd);
	bnge_set_ring_params(bd);

	bnge_init_l2_fltr_tbl(bn);
	bnge_init_mac_addr(bd);

	rc = register_netdev(netdev);
	if (rc) {
		dev_err(bd->dev, "Register netdev failed rc: %d\n", rc);
		goto err_netdev;
	}

	return 0;

err_netdev:
	free_netdev(netdev);
	return rc;
}

void bnge_netdev_free(struct bnge_dev *bd)
{
	struct net_device *netdev = bd->netdev;

	unregister_netdev(netdev);
	free_netdev(netdev);
	bd->netdev = NULL;
}
