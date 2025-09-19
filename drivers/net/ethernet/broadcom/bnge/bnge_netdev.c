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
#include <net/page_pool/helpers.h>

#include "bnge.h"
#include "bnge_hwrm_lib.h"
#include "bnge_ethtool.h"
#include "bnge_rmem.h"

#define BNGE_RING_TO_TC_OFF(bd, tx)	\
	((tx) % (bd)->tx_nr_rings_per_tc)

#define BNGE_RING_TO_TC(bd, tx)		\
	((tx) / (bd)->tx_nr_rings_per_tc)

static bool bnge_separate_head_pool(struct bnge_rx_ring_info *rxr)
{
	return rxr->need_head_pool || PAGE_SIZE > BNGE_RX_PAGE_SIZE;
}

static void bnge_free_rx_rings(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i;

	for (i = 0; i < bd->rx_nr_rings; i++) {
		struct bnge_rx_ring_info *rxr = &bn->rx_ring[i];
		struct bnge_ring_struct *ring;

		page_pool_destroy(rxr->page_pool);
		page_pool_destroy(rxr->head_pool);
		rxr->page_pool = rxr->head_pool = NULL;

		kfree(rxr->rx_agg_bmap);
		rxr->rx_agg_bmap = NULL;

		ring = &rxr->rx_ring_struct;
		bnge_free_ring(bd, &ring->ring_mem);

		ring = &rxr->rx_agg_ring_struct;
		bnge_free_ring(bd, &ring->ring_mem);
	}
}

static int bnge_alloc_rx_page_pool(struct bnge_net *bn,
				   struct bnge_rx_ring_info *rxr,
				   int numa_node)
{
	const unsigned int agg_size_fac = PAGE_SIZE / BNGE_RX_PAGE_SIZE;
	const unsigned int rx_size_fac = PAGE_SIZE / SZ_4K;
	struct page_pool_params pp = { 0 };
	struct bnge_dev *bd = bn->bd;
	struct page_pool *pool;

	pp.pool_size = bn->rx_agg_ring_size / agg_size_fac;
	pp.nid = numa_node;
	pp.netdev = bn->netdev;
	pp.dev = bd->dev;
	pp.dma_dir = bn->rx_dir;
	pp.max_len = PAGE_SIZE;
	pp.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV |
		   PP_FLAG_ALLOW_UNREADABLE_NETMEM;
	pp.queue_idx = rxr->bnapi->index;

	pool = page_pool_create(&pp);
	if (IS_ERR(pool))
		return PTR_ERR(pool);
	rxr->page_pool = pool;

	rxr->need_head_pool = page_pool_is_unreadable(pool);
	if (bnge_separate_head_pool(rxr)) {
		pp.pool_size = min(bn->rx_ring_size / rx_size_fac, 1024);
		pp.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV;
		pool = page_pool_create(&pp);
		if (IS_ERR(pool))
			goto err_destroy_pp;
	} else {
		page_pool_get(pool);
	}
	rxr->head_pool = pool;
	return 0;

err_destroy_pp:
	page_pool_destroy(rxr->page_pool);
	rxr->page_pool = NULL;
	return PTR_ERR(pool);
}

static void bnge_enable_rx_page_pool(struct bnge_rx_ring_info *rxr)
{
	page_pool_enable_direct_recycling(rxr->head_pool, &rxr->bnapi->napi);
	page_pool_enable_direct_recycling(rxr->page_pool, &rxr->bnapi->napi);
}

static int bnge_alloc_rx_agg_bmap(struct bnge_net *bn,
				  struct bnge_rx_ring_info *rxr)
{
	u16 mem_size;

	rxr->rx_agg_bmap_size = bn->rx_agg_ring_mask + 1;
	mem_size = rxr->rx_agg_bmap_size / 8;
	rxr->rx_agg_bmap = kzalloc(mem_size, GFP_KERNEL);
	if (!rxr->rx_agg_bmap)
		return -ENOMEM;

	return 0;
}

static int bnge_alloc_rx_rings(struct bnge_net *bn)
{
	int i, rc = 0, agg_rings = 0, cpu;
	struct bnge_dev *bd = bn->bd;

	if (bnge_is_agg_reqd(bd))
		agg_rings = 1;

	for (i = 0; i < bd->rx_nr_rings; i++) {
		struct bnge_rx_ring_info *rxr = &bn->rx_ring[i];
		struct bnge_ring_struct *ring;
		int cpu_node;

		ring = &rxr->rx_ring_struct;

		cpu = cpumask_local_spread(i, dev_to_node(bd->dev));
		cpu_node = cpu_to_node(cpu);
		netdev_dbg(bn->netdev, "Allocating page pool for rx_ring[%d] on numa_node: %d\n",
			   i, cpu_node);
		rc = bnge_alloc_rx_page_pool(bn, rxr, cpu_node);
		if (rc)
			goto err_free_rx_rings;
		bnge_enable_rx_page_pool(rxr);

		rc = bnge_alloc_ring(bd, &ring->ring_mem);
		if (rc)
			goto err_free_rx_rings;

		ring->grp_idx = i;
		if (agg_rings) {
			ring = &rxr->rx_agg_ring_struct;
			rc = bnge_alloc_ring(bd, &ring->ring_mem);
			if (rc)
				goto err_free_rx_rings;

			ring->grp_idx = i;
			rc = bnge_alloc_rx_agg_bmap(bn, rxr);
			if (rc)
				goto err_free_rx_rings;
		}
	}
	return rc;

err_free_rx_rings:
	bnge_free_rx_rings(bn);
	return rc;
}

static void bnge_free_tx_rings(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i;

	for (i = 0; i < bd->tx_nr_rings; i++) {
		struct bnge_tx_ring_info *txr = &bn->tx_ring[i];
		struct bnge_ring_struct *ring;

		ring = &txr->tx_ring_struct;

		bnge_free_ring(bd, &ring->ring_mem);
	}
}

static int bnge_alloc_tx_rings(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i, j, rc;

	for (i = 0, j = 0; i < bd->tx_nr_rings; i++) {
		struct bnge_tx_ring_info *txr = &bn->tx_ring[i];
		struct bnge_ring_struct *ring;
		u8 qidx;

		ring = &txr->tx_ring_struct;

		rc = bnge_alloc_ring(bd, &ring->ring_mem);
		if (rc)
			goto err_free_tx_rings;

		ring->grp_idx = txr->bnapi->index;
		qidx = bd->tc_to_qidx[j];
		ring->queue_id = bd->q_info[qidx].queue_id;
		if (BNGE_RING_TO_TC_OFF(bd, i) == (bd->tx_nr_rings_per_tc - 1))
			j++;
	}
	return 0;

err_free_tx_rings:
	bnge_free_tx_rings(bn);
	return rc;
}

static void bnge_free_core(struct bnge_net *bn)
{
	bnge_free_tx_rings(bn);
	bnge_free_rx_rings(bn);
	kfree(bn->tx_ring_map);
	bn->tx_ring_map = NULL;
	kfree(bn->tx_ring);
	bn->tx_ring = NULL;
	kfree(bn->rx_ring);
	bn->rx_ring = NULL;
	kfree(bn->bnapi);
	bn->bnapi = NULL;
}

static int bnge_alloc_core(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i, j, size, arr_size;
	int rc = -ENOMEM;
	void *bnapi;

	arr_size = L1_CACHE_ALIGN(sizeof(struct bnge_napi *) *
			bd->nq_nr_rings);
	size = L1_CACHE_ALIGN(sizeof(struct bnge_napi));
	bnapi = kzalloc(arr_size + size * bd->nq_nr_rings, GFP_KERNEL);
	if (!bnapi)
		return rc;

	bn->bnapi = bnapi;
	bnapi += arr_size;
	for (i = 0; i < bd->nq_nr_rings; i++, bnapi += size) {
		struct bnge_nq_ring_info *nqr;

		bn->bnapi[i] = bnapi;
		bn->bnapi[i]->index = i;
		bn->bnapi[i]->bn = bn;
		nqr = &bn->bnapi[i]->nq_ring;
		nqr->ring_struct.ring_mem.flags = BNGE_RMEM_RING_PTE_FLAG;
	}

	bn->rx_ring = kcalloc(bd->rx_nr_rings,
			      sizeof(struct bnge_rx_ring_info),
			      GFP_KERNEL);
	if (!bn->rx_ring)
		goto err_free_core;

	for (i = 0; i < bd->rx_nr_rings; i++) {
		struct bnge_rx_ring_info *rxr = &bn->rx_ring[i];

		rxr->rx_ring_struct.ring_mem.flags =
			BNGE_RMEM_RING_PTE_FLAG;
		rxr->rx_agg_ring_struct.ring_mem.flags =
			BNGE_RMEM_RING_PTE_FLAG;
		rxr->bnapi = bn->bnapi[i];
		bn->bnapi[i]->rx_ring = &bn->rx_ring[i];
	}

	bn->tx_ring = kcalloc(bd->tx_nr_rings,
			      sizeof(struct bnge_tx_ring_info),
			      GFP_KERNEL);
	if (!bn->tx_ring)
		goto err_free_core;

	bn->tx_ring_map = kcalloc(bd->tx_nr_rings, sizeof(u16),
				  GFP_KERNEL);
	if (!bn->tx_ring_map)
		goto err_free_core;

	if (bd->flags & BNGE_EN_SHARED_CHNL)
		j = 0;
	else
		j = bd->rx_nr_rings;

	for (i = 0; i < bd->tx_nr_rings; i++) {
		struct bnge_tx_ring_info *txr = &bn->tx_ring[i];
		struct bnge_napi *bnapi2;
		int k;

		txr->tx_ring_struct.ring_mem.flags = BNGE_RMEM_RING_PTE_FLAG;
		bn->tx_ring_map[i] = i;
		k = j + BNGE_RING_TO_TC_OFF(bd, i);

		bnapi2 = bn->bnapi[k];
		txr->txq_index = i;
		txr->tx_napi_idx =
			BNGE_RING_TO_TC(bd, txr->txq_index);
		bnapi2->tx_ring[txr->tx_napi_idx] = txr;
		txr->bnapi = bnapi2;
	}

	bnge_init_ring_struct(bn);

	rc = bnge_alloc_rx_rings(bn);
	if (rc)
		goto err_free_core;

	rc = bnge_alloc_tx_rings(bn);
	if (rc)
		goto err_free_core;
	return 0;

err_free_core:
	bnge_free_core(bn);
	return rc;
}

static int bnge_open_core(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int rc;

	netif_carrier_off(bn->netdev);

	rc = bnge_reserve_rings(bd);
	if (rc) {
		netdev_err(bn->netdev, "bnge_reserve_rings err: %d\n", rc);
		return rc;
	}

	rc = bnge_alloc_core(bn);
	if (rc) {
		netdev_err(bn->netdev, "bnge_alloc_core err: %d\n", rc);
		return rc;
	}

	set_bit(BNGE_STATE_OPEN, &bd->state);
	return 0;
}

static netdev_tx_t bnge_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static int bnge_open(struct net_device *dev)
{
	struct bnge_net *bn = netdev_priv(dev);
	int rc;

	rc = bnge_open_core(bn);
	if (rc)
		netdev_err(dev, "bnge_open_core err: %d\n", rc);

	return rc;
}

static void bnge_close_core(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;

	clear_bit(BNGE_STATE_OPEN, &bd->state);
	bnge_free_core(bn);
}

static int bnge_close(struct net_device *dev)
{
	struct bnge_net *bn = netdev_priv(dev);

	bnge_close_core(bn);

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
	bn->rx_dir = DMA_FROM_DEVICE;

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
