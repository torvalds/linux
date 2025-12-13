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

#define BNGE_TC_TO_RING_BASE(bd, tc)	\
	((tc) * (bd)->tx_nr_rings_per_tc)

static void bnge_free_stats_mem(struct bnge_net *bn,
				struct bnge_stats_mem *stats)
{
	struct bnge_dev *bd = bn->bd;

	if (stats->hw_stats) {
		dma_free_coherent(bd->dev, stats->len, stats->hw_stats,
				  stats->hw_stats_map);
		stats->hw_stats = NULL;
	}
}

static int bnge_alloc_stats_mem(struct bnge_net *bn,
				struct bnge_stats_mem *stats)
{
	struct bnge_dev *bd = bn->bd;

	stats->hw_stats = dma_alloc_coherent(bd->dev, stats->len,
					     &stats->hw_stats_map, GFP_KERNEL);
	if (!stats->hw_stats)
		return -ENOMEM;

	return 0;
}

static void bnge_free_ring_stats(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i;

	if (!bn->bnapi)
		return;

	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];
		struct bnge_nq_ring_info *nqr = &bnapi->nq_ring;

		bnge_free_stats_mem(bn, &nqr->stats);
	}
}

static int bnge_alloc_ring_stats(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	u32 size, i;
	int rc;

	size = bd->hw_ring_stats_size;

	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];
		struct bnge_nq_ring_info *nqr = &bnapi->nq_ring;

		nqr->stats.len = size;
		rc = bnge_alloc_stats_mem(bn, &nqr->stats);
		if (rc)
			goto err_free_ring_stats;

		nqr->hw_stats_ctx_id = INVALID_STATS_CTX_ID;
	}
	return 0;

err_free_ring_stats:
	bnge_free_ring_stats(bn);
	return rc;
}

static void bnge_free_nq_desc_arr(struct bnge_nq_ring_info *nqr)
{
	struct bnge_ring_struct *ring = &nqr->ring_struct;

	kfree(nqr->desc_ring);
	nqr->desc_ring = NULL;
	ring->ring_mem.pg_arr = NULL;
	kfree(nqr->desc_mapping);
	nqr->desc_mapping = NULL;
	ring->ring_mem.dma_arr = NULL;
}

static void bnge_free_cp_desc_arr(struct bnge_cp_ring_info *cpr)
{
	struct bnge_ring_struct *ring = &cpr->ring_struct;

	kfree(cpr->desc_ring);
	cpr->desc_ring = NULL;
	ring->ring_mem.pg_arr = NULL;
	kfree(cpr->desc_mapping);
	cpr->desc_mapping = NULL;
	ring->ring_mem.dma_arr = NULL;
}

static int bnge_alloc_nq_desc_arr(struct bnge_nq_ring_info *nqr, int n)
{
	nqr->desc_ring = kcalloc(n, sizeof(*nqr->desc_ring), GFP_KERNEL);
	if (!nqr->desc_ring)
		return -ENOMEM;

	nqr->desc_mapping = kcalloc(n, sizeof(*nqr->desc_mapping), GFP_KERNEL);
	if (!nqr->desc_mapping)
		goto err_free_desc_ring;
	return 0;

err_free_desc_ring:
	kfree(nqr->desc_ring);
	nqr->desc_ring = NULL;
	return -ENOMEM;
}

static int bnge_alloc_cp_desc_arr(struct bnge_cp_ring_info *cpr, int n)
{
	cpr->desc_ring = kcalloc(n, sizeof(*cpr->desc_ring), GFP_KERNEL);
	if (!cpr->desc_ring)
		return -ENOMEM;

	cpr->desc_mapping = kcalloc(n, sizeof(*cpr->desc_mapping), GFP_KERNEL);
	if (!cpr->desc_mapping)
		goto err_free_desc_ring;
	return 0;

err_free_desc_ring:
	kfree(cpr->desc_ring);
	cpr->desc_ring = NULL;
	return -ENOMEM;
}

static void bnge_free_nq_arrays(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i;

	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];

		bnge_free_nq_desc_arr(&bnapi->nq_ring);
	}
}

static int bnge_alloc_nq_arrays(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i, rc;

	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];

		rc = bnge_alloc_nq_desc_arr(&bnapi->nq_ring, bn->cp_nr_pages);
		if (rc)
			goto err_free_nq_arrays;
	}
	return 0;

err_free_nq_arrays:
	bnge_free_nq_arrays(bn);
	return rc;
}

static void bnge_free_nq_tree(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i;

	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];
		struct bnge_nq_ring_info *nqr;
		struct bnge_ring_struct *ring;
		int j;

		nqr = &bnapi->nq_ring;
		ring = &nqr->ring_struct;

		bnge_free_ring(bd, &ring->ring_mem);

		if (!nqr->cp_ring_arr)
			continue;

		for (j = 0; j < nqr->cp_ring_count; j++) {
			struct bnge_cp_ring_info *cpr = &nqr->cp_ring_arr[j];

			ring = &cpr->ring_struct;
			bnge_free_ring(bd, &ring->ring_mem);
			bnge_free_cp_desc_arr(cpr);
		}
		kfree(nqr->cp_ring_arr);
		nqr->cp_ring_arr = NULL;
		nqr->cp_ring_count = 0;
	}
}

static int alloc_one_cp_ring(struct bnge_net *bn,
			     struct bnge_cp_ring_info *cpr)
{
	struct bnge_ring_mem_info *rmem;
	struct bnge_ring_struct *ring;
	struct bnge_dev *bd = bn->bd;
	int rc;

	rc = bnge_alloc_cp_desc_arr(cpr, bn->cp_nr_pages);
	if (rc)
		return -ENOMEM;
	ring = &cpr->ring_struct;
	rmem = &ring->ring_mem;
	rmem->nr_pages = bn->cp_nr_pages;
	rmem->page_size = HW_CMPD_RING_SIZE;
	rmem->pg_arr = (void **)cpr->desc_ring;
	rmem->dma_arr = cpr->desc_mapping;
	rmem->flags = BNGE_RMEM_RING_PTE_FLAG;
	rc = bnge_alloc_ring(bd, rmem);
	if (rc)
		goto err_free_cp_desc_arr;
	return rc;

err_free_cp_desc_arr:
	bnge_free_cp_desc_arr(cpr);
	return rc;
}

static int bnge_alloc_nq_tree(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i, j, ulp_msix, rc;
	int tcs = 1;

	ulp_msix = bnge_aux_get_msix(bd);
	for (i = 0, j = 0; i < bd->nq_nr_rings; i++) {
		bool sh = !!(bd->flags & BNGE_EN_SHARED_CHNL);
		struct bnge_napi *bnapi = bn->bnapi[i];
		struct bnge_nq_ring_info *nqr;
		struct bnge_cp_ring_info *cpr;
		struct bnge_ring_struct *ring;
		int cp_count = 0, k;
		int rx = 0, tx = 0;

		nqr = &bnapi->nq_ring;
		nqr->bnapi = bnapi;
		ring = &nqr->ring_struct;

		rc = bnge_alloc_ring(bd, &ring->ring_mem);
		if (rc)
			goto err_free_nq_tree;

		ring->map_idx = ulp_msix + i;

		if (i < bd->rx_nr_rings) {
			cp_count++;
			rx = 1;
		}

		if ((sh && i < bd->tx_nr_rings) ||
		    (!sh && i >= bd->rx_nr_rings)) {
			cp_count += tcs;
			tx = 1;
		}

		nqr->cp_ring_arr = kcalloc(cp_count, sizeof(*cpr),
					   GFP_KERNEL);
		if (!nqr->cp_ring_arr) {
			rc = -ENOMEM;
			goto err_free_nq_tree;
		}

		nqr->cp_ring_count = cp_count;

		for (k = 0; k < cp_count; k++) {
			cpr = &nqr->cp_ring_arr[k];
			rc = alloc_one_cp_ring(bn, cpr);
			if (rc)
				goto err_free_nq_tree;

			cpr->bnapi = bnapi;
			cpr->cp_idx = k;
			if (!k && rx) {
				bn->rx_ring[i].rx_cpr = cpr;
				cpr->cp_ring_type = BNGE_NQ_HDL_TYPE_RX;
			} else {
				int n, tc = k - rx;

				n = BNGE_TC_TO_RING_BASE(bd, tc) + j;
				bn->tx_ring[n].tx_cpr = cpr;
				cpr->cp_ring_type = BNGE_NQ_HDL_TYPE_TX;
			}
		}
		if (tx)
			j++;
	}
	return 0;

err_free_nq_tree:
	bnge_free_nq_tree(bn);
	return rc;
}

static bool bnge_separate_head_pool(struct bnge_rx_ring_info *rxr)
{
	return rxr->need_head_pool || PAGE_SIZE > BNGE_RX_PAGE_SIZE;
}

static void bnge_free_one_rx_ring_bufs(struct bnge_net *bn,
				       struct bnge_rx_ring_info *rxr)
{
	int i, max_idx;

	if (!rxr->rx_buf_ring)
		return;

	max_idx = bn->rx_nr_pages * RX_DESC_CNT;

	for (i = 0; i < max_idx; i++) {
		struct bnge_sw_rx_bd *rx_buf = &rxr->rx_buf_ring[i];
		void *data = rx_buf->data;

		if (!data)
			continue;

		rx_buf->data = NULL;
		page_pool_free_va(rxr->head_pool, data, true);
	}
}

static void bnge_free_one_agg_ring_bufs(struct bnge_net *bn,
					struct bnge_rx_ring_info *rxr)
{
	int i, max_idx;

	if (!rxr->rx_agg_buf_ring)
		return;

	max_idx = bn->rx_agg_nr_pages * RX_DESC_CNT;

	for (i = 0; i < max_idx; i++) {
		struct bnge_sw_rx_agg_bd *rx_agg_buf = &rxr->rx_agg_buf_ring[i];
		netmem_ref netmem = rx_agg_buf->netmem;

		if (!netmem)
			continue;

		rx_agg_buf->netmem = 0;
		__clear_bit(i, rxr->rx_agg_bmap);

		page_pool_recycle_direct_netmem(rxr->page_pool, netmem);
	}
}

static void bnge_free_one_rx_ring_pair_bufs(struct bnge_net *bn,
					    struct bnge_rx_ring_info *rxr)
{
	bnge_free_one_rx_ring_bufs(bn, rxr);
	bnge_free_one_agg_ring_bufs(bn, rxr);
}

static void bnge_free_rx_ring_pair_bufs(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i;

	if (!bn->rx_ring)
		return;

	for (i = 0; i < bd->rx_nr_rings; i++)
		bnge_free_one_rx_ring_pair_bufs(bn, &bn->rx_ring[i]);
}

static void bnge_free_all_rings_bufs(struct bnge_net *bn)
{
	bnge_free_rx_ring_pair_bufs(bn);
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

static void bnge_free_vnic_attributes(struct bnge_net *bn)
{
	struct pci_dev *pdev = bn->bd->pdev;
	struct bnge_vnic_info *vnic;
	int i;

	if (!bn->vnic_info)
		return;

	for (i = 0; i < bn->nr_vnics; i++) {
		vnic = &bn->vnic_info[i];

		kfree(vnic->uc_list);
		vnic->uc_list = NULL;

		if (vnic->mc_list) {
			dma_free_coherent(&pdev->dev, vnic->mc_list_size,
					  vnic->mc_list, vnic->mc_list_mapping);
			vnic->mc_list = NULL;
		}

		if (vnic->rss_table) {
			dma_free_coherent(&pdev->dev, vnic->rss_table_size,
					  vnic->rss_table,
					  vnic->rss_table_dma_addr);
			vnic->rss_table = NULL;
		}

		vnic->rss_hash_key = NULL;
		vnic->flags = 0;
	}
}

static int bnge_alloc_vnic_attributes(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	struct bnge_vnic_info *vnic;
	int i, size;

	for (i = 0; i < bn->nr_vnics; i++) {
		vnic = &bn->vnic_info[i];

		if (vnic->flags & BNGE_VNIC_UCAST_FLAG) {
			int mem_size = (BNGE_MAX_UC_ADDRS - 1) * ETH_ALEN;

			vnic->uc_list = kmalloc(mem_size, GFP_KERNEL);
			if (!vnic->uc_list)
				goto err_free_vnic_attributes;
		}

		if (vnic->flags & BNGE_VNIC_MCAST_FLAG) {
			vnic->mc_list_size = BNGE_MAX_MC_ADDRS * ETH_ALEN;
			vnic->mc_list =
				dma_alloc_coherent(bd->dev,
						   vnic->mc_list_size,
						   &vnic->mc_list_mapping,
						   GFP_KERNEL);
			if (!vnic->mc_list)
				goto err_free_vnic_attributes;
		}

		/* Allocate rss table and hash key */
		size = L1_CACHE_ALIGN(BNGE_MAX_RSS_TABLE_SIZE);

		vnic->rss_table_size = size + HW_HASH_KEY_SIZE;
		vnic->rss_table = dma_alloc_coherent(bd->dev,
						     vnic->rss_table_size,
						     &vnic->rss_table_dma_addr,
						     GFP_KERNEL);
		if (!vnic->rss_table)
			goto err_free_vnic_attributes;

		vnic->rss_hash_key = ((void *)vnic->rss_table) + size;
		vnic->rss_hash_key_dma_addr = vnic->rss_table_dma_addr + size;
	}
	return 0;

err_free_vnic_attributes:
	bnge_free_vnic_attributes(bn);
	return -ENOMEM;
}

static int bnge_alloc_vnics(struct bnge_net *bn)
{
	int num_vnics;

	/* Allocate only 1 VNIC for now
	 * Additional VNICs will be added based on RFS/NTUPLE in future patches
	 */
	num_vnics = 1;

	bn->vnic_info = kcalloc(num_vnics, sizeof(struct bnge_vnic_info),
				GFP_KERNEL);
	if (!bn->vnic_info)
		return -ENOMEM;

	bn->nr_vnics = num_vnics;

	return 0;
}

static void bnge_free_vnics(struct bnge_net *bn)
{
	kfree(bn->vnic_info);
	bn->vnic_info = NULL;
	bn->nr_vnics = 0;
}

static void bnge_free_ring_grps(struct bnge_net *bn)
{
	kfree(bn->grp_info);
	bn->grp_info = NULL;
}

static int bnge_init_ring_grps(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i;

	bn->grp_info = kcalloc(bd->nq_nr_rings,
			       sizeof(struct bnge_ring_grp_info),
			       GFP_KERNEL);
	if (!bn->grp_info)
		return -ENOMEM;
	for (i = 0; i < bd->nq_nr_rings; i++) {
		bn->grp_info[i].fw_stats_ctx = INVALID_HW_RING_ID;
		bn->grp_info[i].fw_grp_id = INVALID_HW_RING_ID;
		bn->grp_info[i].rx_fw_ring_id = INVALID_HW_RING_ID;
		bn->grp_info[i].agg_fw_ring_id = INVALID_HW_RING_ID;
		bn->grp_info[i].nq_fw_ring_id = INVALID_HW_RING_ID;
	}

	return 0;
}

static void bnge_free_core(struct bnge_net *bn)
{
	bnge_free_vnic_attributes(bn);
	bnge_free_tx_rings(bn);
	bnge_free_rx_rings(bn);
	bnge_free_nq_tree(bn);
	bnge_free_nq_arrays(bn);
	bnge_free_ring_stats(bn);
	bnge_free_ring_grps(bn);
	bnge_free_vnics(bn);
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

	rc = bnge_alloc_ring_stats(bn);
	if (rc)
		goto err_free_core;

	rc = bnge_alloc_vnics(bn);
	if (rc)
		goto err_free_core;

	rc = bnge_alloc_nq_arrays(bn);
	if (rc)
		goto err_free_core;

	bnge_init_ring_struct(bn);

	rc = bnge_alloc_rx_rings(bn);
	if (rc)
		goto err_free_core;

	rc = bnge_alloc_tx_rings(bn);
	if (rc)
		goto err_free_core;

	rc = bnge_alloc_nq_tree(bn);
	if (rc)
		goto err_free_core;

	bn->vnic_info[BNGE_VNIC_DEFAULT].flags |= BNGE_VNIC_RSS_FLAG |
						  BNGE_VNIC_MCAST_FLAG |
						  BNGE_VNIC_UCAST_FLAG;
	rc = bnge_alloc_vnic_attributes(bn);
	if (rc)
		goto err_free_core;
	return 0;

err_free_core:
	bnge_free_core(bn);
	return rc;
}

u16 bnge_cp_ring_for_rx(struct bnge_rx_ring_info *rxr)
{
	return rxr->rx_cpr->ring_struct.fw_ring_id;
}

u16 bnge_cp_ring_for_tx(struct bnge_tx_ring_info *txr)
{
	return txr->tx_cpr->ring_struct.fw_ring_id;
}

static void bnge_db_nq(struct bnge_net *bn, struct bnge_db_info *db, u32 idx)
{
	bnge_writeq(bn->bd, db->db_key64 | DBR_TYPE_NQ_MASK |
		    DB_RING_IDX(db, idx), db->doorbell);
}

static void bnge_db_cq(struct bnge_net *bn, struct bnge_db_info *db, u32 idx)
{
	bnge_writeq(bn->bd, db->db_key64 | DBR_TYPE_CQ_ARMALL |
		    DB_RING_IDX(db, idx), db->doorbell);
}

static int bnge_cp_num_to_irq_num(struct bnge_net *bn, int n)
{
	struct bnge_napi *bnapi = bn->bnapi[n];
	struct bnge_nq_ring_info *nqr;

	nqr = &bnapi->nq_ring;

	return nqr->ring_struct.map_idx;
}

static irqreturn_t bnge_msix(int irq, void *dev_instance)
{
	/* NAPI scheduling to be added in a future patch */
	return IRQ_HANDLED;
}

static void bnge_init_nq_tree(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i, j;

	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_nq_ring_info *nqr = &bn->bnapi[i]->nq_ring;
		struct bnge_ring_struct *ring = &nqr->ring_struct;

		ring->fw_ring_id = INVALID_HW_RING_ID;
		for (j = 0; j < nqr->cp_ring_count; j++) {
			struct bnge_cp_ring_info *cpr = &nqr->cp_ring_arr[j];

			ring = &cpr->ring_struct;
			ring->fw_ring_id = INVALID_HW_RING_ID;
		}
	}
}

static netmem_ref __bnge_alloc_rx_netmem(struct bnge_net *bn,
					 dma_addr_t *mapping,
					 struct bnge_rx_ring_info *rxr,
					 unsigned int *offset,
					 gfp_t gfp)
{
	netmem_ref netmem;

	if (PAGE_SIZE > BNGE_RX_PAGE_SIZE) {
		netmem = page_pool_alloc_frag_netmem(rxr->page_pool, offset,
						     BNGE_RX_PAGE_SIZE, gfp);
	} else {
		netmem = page_pool_alloc_netmems(rxr->page_pool, gfp);
		*offset = 0;
	}
	if (!netmem)
		return 0;

	*mapping = page_pool_get_dma_addr_netmem(netmem) + *offset;
	return netmem;
}

static u8 *__bnge_alloc_rx_frag(struct bnge_net *bn, dma_addr_t *mapping,
				struct bnge_rx_ring_info *rxr,
				gfp_t gfp)
{
	unsigned int offset;
	struct page *page;

	page = page_pool_alloc_frag(rxr->head_pool, &offset,
				    bn->rx_buf_size, gfp);
	if (!page)
		return NULL;

	*mapping = page_pool_get_dma_addr(page) + bn->rx_dma_offset + offset;
	return page_address(page) + offset;
}

static int bnge_alloc_rx_data(struct bnge_net *bn,
			      struct bnge_rx_ring_info *rxr,
			      u16 prod, gfp_t gfp)
{
	struct bnge_sw_rx_bd *rx_buf = &rxr->rx_buf_ring[RING_RX(bn, prod)];
	struct rx_bd *rxbd;
	dma_addr_t mapping;
	u8 *data;

	rxbd = &rxr->rx_desc_ring[RX_RING(bn, prod)][RX_IDX(prod)];
	data = __bnge_alloc_rx_frag(bn, &mapping, rxr, gfp);
	if (!data)
		return -ENOMEM;

	rx_buf->data = data;
	rx_buf->data_ptr = data + bn->rx_offset;
	rx_buf->mapping = mapping;

	rxbd->rx_bd_haddr = cpu_to_le64(mapping);

	return 0;
}

static int bnge_alloc_one_rx_ring_bufs(struct bnge_net *bn,
				       struct bnge_rx_ring_info *rxr,
				       int ring_nr)
{
	u32 prod = rxr->rx_prod;
	int i, rc = 0;

	for (i = 0; i < bn->rx_ring_size; i++) {
		rc = bnge_alloc_rx_data(bn, rxr, prod, GFP_KERNEL);
		if (rc)
			break;
		prod = NEXT_RX(prod);
	}

	/* Abort if not a single buffer can be allocated */
	if (rc && !i) {
		netdev_err(bn->netdev,
			   "RX ring %d: allocated %d/%d buffers, abort\n",
			   ring_nr, i, bn->rx_ring_size);
		return rc;
	}

	rxr->rx_prod = prod;

	if (i < bn->rx_ring_size)
		netdev_warn(bn->netdev,
			    "RX ring %d: allocated %d/%d buffers, continuing\n",
			    ring_nr, i, bn->rx_ring_size);
	return 0;
}

static u16 bnge_find_next_agg_idx(struct bnge_rx_ring_info *rxr, u16 idx)
{
	u16 next, max = rxr->rx_agg_bmap_size;

	next = find_next_zero_bit(rxr->rx_agg_bmap, max, idx);
	if (next >= max)
		next = find_first_zero_bit(rxr->rx_agg_bmap, max);
	return next;
}

static int bnge_alloc_rx_netmem(struct bnge_net *bn,
				struct bnge_rx_ring_info *rxr,
				u16 prod, gfp_t gfp)
{
	struct bnge_sw_rx_agg_bd *rx_agg_buf;
	u16 sw_prod = rxr->rx_sw_agg_prod;
	unsigned int offset = 0;
	struct rx_bd *rxbd;
	dma_addr_t mapping;
	netmem_ref netmem;

	rxbd = &rxr->rx_agg_desc_ring[RX_AGG_RING(bn, prod)][RX_IDX(prod)];
	netmem = __bnge_alloc_rx_netmem(bn, &mapping, rxr, &offset, gfp);
	if (!netmem)
		return -ENOMEM;

	if (unlikely(test_bit(sw_prod, rxr->rx_agg_bmap)))
		sw_prod = bnge_find_next_agg_idx(rxr, sw_prod);

	__set_bit(sw_prod, rxr->rx_agg_bmap);
	rx_agg_buf = &rxr->rx_agg_buf_ring[sw_prod];
	rxr->rx_sw_agg_prod = RING_RX_AGG(bn, NEXT_RX_AGG(sw_prod));

	rx_agg_buf->netmem = netmem;
	rx_agg_buf->offset = offset;
	rx_agg_buf->mapping = mapping;
	rxbd->rx_bd_haddr = cpu_to_le64(mapping);
	rxbd->rx_bd_opaque = sw_prod;
	return 0;
}

static int bnge_alloc_one_agg_ring_bufs(struct bnge_net *bn,
					struct bnge_rx_ring_info *rxr,
					int ring_nr)
{
	u32 prod = rxr->rx_agg_prod;
	int i, rc = 0;

	for (i = 0; i < bn->rx_agg_ring_size; i++) {
		rc = bnge_alloc_rx_netmem(bn, rxr, prod, GFP_KERNEL);
		if (rc)
			break;
		prod = NEXT_RX_AGG(prod);
	}

	if (rc && i < MAX_SKB_FRAGS) {
		netdev_err(bn->netdev,
			   "Agg ring %d: allocated %d/%d buffers (min %d), abort\n",
			   ring_nr, i, bn->rx_agg_ring_size, MAX_SKB_FRAGS);
		goto err_free_one_agg_ring_bufs;
	}

	rxr->rx_agg_prod = prod;

	if (i < bn->rx_agg_ring_size)
		netdev_warn(bn->netdev,
			    "Agg ring %d: allocated %d/%d buffers, continuing\n",
			    ring_nr, i, bn->rx_agg_ring_size);
	return 0;

err_free_one_agg_ring_bufs:
	bnge_free_one_agg_ring_bufs(bn, rxr);
	return -ENOMEM;
}

static int bnge_alloc_one_rx_ring_pair_bufs(struct bnge_net *bn, int ring_nr)
{
	struct bnge_rx_ring_info *rxr = &bn->rx_ring[ring_nr];
	int rc;

	rc = bnge_alloc_one_rx_ring_bufs(bn, rxr, ring_nr);
	if (rc)
		return rc;

	if (bnge_is_agg_reqd(bn->bd)) {
		rc = bnge_alloc_one_agg_ring_bufs(bn, rxr, ring_nr);
		if (rc)
			goto err_free_one_rx_ring_bufs;
	}
	return 0;

err_free_one_rx_ring_bufs:
	bnge_free_one_rx_ring_bufs(bn, rxr);
	return rc;
}

static void bnge_init_rxbd_pages(struct bnge_ring_struct *ring, u32 type)
{
	struct rx_bd **rx_desc_ring;
	u32 prod;
	int i;

	rx_desc_ring = (struct rx_bd **)ring->ring_mem.pg_arr;
	for (i = 0, prod = 0; i < ring->ring_mem.nr_pages; i++) {
		struct rx_bd *rxbd = rx_desc_ring[i];
		int j;

		for (j = 0; j < RX_DESC_CNT; j++, rxbd++, prod++) {
			rxbd->rx_bd_len_flags_type = cpu_to_le32(type);
			rxbd->rx_bd_opaque = prod;
		}
	}
}

static void bnge_init_one_rx_ring_rxbd(struct bnge_net *bn,
				       struct bnge_rx_ring_info *rxr)
{
	struct bnge_ring_struct *ring;
	u32 type;

	type = (bn->rx_buf_use_size << RX_BD_LEN_SHIFT) |
		RX_BD_TYPE_RX_PACKET_BD | RX_BD_FLAGS_EOP;

	if (NET_IP_ALIGN == 2)
		type |= RX_BD_FLAGS_SOP;

	ring = &rxr->rx_ring_struct;
	bnge_init_rxbd_pages(ring, type);
	ring->fw_ring_id = INVALID_HW_RING_ID;
}

static void bnge_init_one_agg_ring_rxbd(struct bnge_net *bn,
					struct bnge_rx_ring_info *rxr)
{
	struct bnge_ring_struct *ring;
	u32 type;

	ring = &rxr->rx_agg_ring_struct;
	ring->fw_ring_id = INVALID_HW_RING_ID;
	if (bnge_is_agg_reqd(bn->bd)) {
		type = ((u32)BNGE_RX_PAGE_SIZE << RX_BD_LEN_SHIFT) |
			RX_BD_TYPE_RX_AGG_BD | RX_BD_FLAGS_SOP;

		bnge_init_rxbd_pages(ring, type);
	}
}

static void bnge_init_one_rx_ring_pair(struct bnge_net *bn, int ring_nr)
{
	struct bnge_rx_ring_info *rxr;

	rxr = &bn->rx_ring[ring_nr];
	bnge_init_one_rx_ring_rxbd(bn, rxr);

	netif_queue_set_napi(bn->netdev, ring_nr, NETDEV_QUEUE_TYPE_RX,
			     &rxr->bnapi->napi);

	bnge_init_one_agg_ring_rxbd(bn, rxr);
}

static int bnge_alloc_rx_ring_pair_bufs(struct bnge_net *bn)
{
	int i, rc;

	for (i = 0; i < bn->bd->rx_nr_rings; i++) {
		rc = bnge_alloc_one_rx_ring_pair_bufs(bn, i);
		if (rc)
			goto err_free_rx_ring_pair_bufs;
	}
	return 0;

err_free_rx_ring_pair_bufs:
	bnge_free_rx_ring_pair_bufs(bn);
	return rc;
}

static void bnge_init_rx_rings(struct bnge_net *bn)
{
	int i;

#define BNGE_RX_OFFSET (NET_SKB_PAD + NET_IP_ALIGN)
#define BNGE_RX_DMA_OFFSET NET_SKB_PAD
	bn->rx_offset = BNGE_RX_OFFSET;
	bn->rx_dma_offset = BNGE_RX_DMA_OFFSET;

	for (i = 0; i < bn->bd->rx_nr_rings; i++)
		bnge_init_one_rx_ring_pair(bn, i);
}

static void bnge_init_tx_rings(struct bnge_net *bn)
{
	int i;

	bn->tx_wake_thresh = max(bn->tx_ring_size / 2, BNGE_MIN_TX_DESC_CNT);

	for (i = 0; i < bn->bd->tx_nr_rings; i++) {
		struct bnge_tx_ring_info *txr = &bn->tx_ring[i];
		struct bnge_ring_struct *ring = &txr->tx_ring_struct;

		ring->fw_ring_id = INVALID_HW_RING_ID;

		netif_queue_set_napi(bn->netdev, i, NETDEV_QUEUE_TYPE_TX,
				     &txr->bnapi->napi);
	}
}

static void bnge_init_vnics(struct bnge_net *bn)
{
	struct bnge_vnic_info *vnic0 = &bn->vnic_info[BNGE_VNIC_DEFAULT];
	int i;

	for (i = 0; i < bn->nr_vnics; i++) {
		struct bnge_vnic_info *vnic = &bn->vnic_info[i];
		int j;

		vnic->fw_vnic_id = INVALID_HW_RING_ID;
		vnic->vnic_id = i;
		for (j = 0; j < BNGE_MAX_CTX_PER_VNIC; j++)
			vnic->fw_rss_cos_lb_ctx[j] = INVALID_HW_RING_ID;

		if (bn->vnic_info[i].rss_hash_key) {
			if (i == BNGE_VNIC_DEFAULT) {
				u8 *key = (void *)vnic->rss_hash_key;
				int k;

				if (!bn->rss_hash_key_valid &&
				    !bn->rss_hash_key_updated) {
					get_random_bytes(bn->rss_hash_key,
							 HW_HASH_KEY_SIZE);
					bn->rss_hash_key_updated = true;
				}

				memcpy(vnic->rss_hash_key, bn->rss_hash_key,
				       HW_HASH_KEY_SIZE);

				if (!bn->rss_hash_key_updated)
					continue;

				bn->rss_hash_key_updated = false;
				bn->rss_hash_key_valid = true;

				bn->toeplitz_prefix = 0;
				for (k = 0; k < 8; k++) {
					bn->toeplitz_prefix <<= 8;
					bn->toeplitz_prefix |= key[k];
				}
			} else {
				memcpy(vnic->rss_hash_key, vnic0->rss_hash_key,
				       HW_HASH_KEY_SIZE);
			}
		}
	}
}

static void bnge_set_db_mask(struct bnge_net *bn, struct bnge_db_info *db,
			     u32 ring_type)
{
	switch (ring_type) {
	case HWRM_RING_ALLOC_TX:
		db->db_ring_mask = bn->tx_ring_mask;
		break;
	case HWRM_RING_ALLOC_RX:
		db->db_ring_mask = bn->rx_ring_mask;
		break;
	case HWRM_RING_ALLOC_AGG:
		db->db_ring_mask = bn->rx_agg_ring_mask;
		break;
	case HWRM_RING_ALLOC_CMPL:
	case HWRM_RING_ALLOC_NQ:
		db->db_ring_mask = bn->cp_ring_mask;
		break;
	}
	db->db_epoch_mask = db->db_ring_mask + 1;
	db->db_epoch_shift = DBR_EPOCH_SFT - ilog2(db->db_epoch_mask);
}

static void bnge_set_db(struct bnge_net *bn, struct bnge_db_info *db,
			u32 ring_type, u32 map_idx, u32 xid)
{
	struct bnge_dev *bd = bn->bd;

	switch (ring_type) {
	case HWRM_RING_ALLOC_TX:
		db->db_key64 = DBR_PATH_L2 | DBR_TYPE_SQ;
		break;
	case HWRM_RING_ALLOC_RX:
	case HWRM_RING_ALLOC_AGG:
		db->db_key64 = DBR_PATH_L2 | DBR_TYPE_SRQ;
		break;
	case HWRM_RING_ALLOC_CMPL:
		db->db_key64 = DBR_PATH_L2;
		break;
	case HWRM_RING_ALLOC_NQ:
		db->db_key64 = DBR_PATH_L2;
		break;
	}
	db->db_key64 |= ((u64)xid << DBR_XID_SFT) | DBR_VALID;

	db->doorbell = bd->bar1 + bd->db_offset;
	bnge_set_db_mask(bn, db, ring_type);
}

static int bnge_hwrm_cp_ring_alloc(struct bnge_net *bn,
				   struct bnge_cp_ring_info *cpr)
{
	const u32 type = HWRM_RING_ALLOC_CMPL;
	struct bnge_napi *bnapi = cpr->bnapi;
	struct bnge_ring_struct *ring;
	u32 map_idx = bnapi->index;
	int rc;

	ring = &cpr->ring_struct;
	ring->handle = BNGE_SET_NQ_HDL(cpr);
	rc = hwrm_ring_alloc_send_msg(bn, ring, type, map_idx);
	if (rc)
		return rc;

	bnge_set_db(bn, &cpr->cp_db, type, map_idx, ring->fw_ring_id);
	bnge_db_cq(bn, &cpr->cp_db, cpr->cp_raw_cons);

	return 0;
}

static int bnge_hwrm_tx_ring_alloc(struct bnge_net *bn,
				   struct bnge_tx_ring_info *txr, u32 tx_idx)
{
	struct bnge_ring_struct *ring = &txr->tx_ring_struct;
	const u32 type = HWRM_RING_ALLOC_TX;
	int rc;

	rc = hwrm_ring_alloc_send_msg(bn, ring, type, tx_idx);
	if (rc)
		return rc;

	bnge_set_db(bn, &txr->tx_db, type, tx_idx, ring->fw_ring_id);

	return 0;
}

static int bnge_hwrm_rx_agg_ring_alloc(struct bnge_net *bn,
				       struct bnge_rx_ring_info *rxr)
{
	struct bnge_ring_struct *ring = &rxr->rx_agg_ring_struct;
	u32 type = HWRM_RING_ALLOC_AGG;
	struct bnge_dev *bd = bn->bd;
	u32 grp_idx = ring->grp_idx;
	u32 map_idx;
	int rc;

	map_idx = grp_idx + bd->rx_nr_rings;
	rc = hwrm_ring_alloc_send_msg(bn, ring, type, map_idx);
	if (rc)
		return rc;

	bnge_set_db(bn, &rxr->rx_agg_db, type, map_idx,
		    ring->fw_ring_id);
	bnge_db_write(bn->bd, &rxr->rx_agg_db, rxr->rx_agg_prod);
	bnge_db_write(bn->bd, &rxr->rx_db, rxr->rx_prod);
	bn->grp_info[grp_idx].agg_fw_ring_id = ring->fw_ring_id;

	return 0;
}

static int bnge_hwrm_rx_ring_alloc(struct bnge_net *bn,
				   struct bnge_rx_ring_info *rxr)
{
	struct bnge_ring_struct *ring = &rxr->rx_ring_struct;
	struct bnge_napi *bnapi = rxr->bnapi;
	u32 type = HWRM_RING_ALLOC_RX;
	u32 map_idx = bnapi->index;
	int rc;

	rc = hwrm_ring_alloc_send_msg(bn, ring, type, map_idx);
	if (rc)
		return rc;

	bnge_set_db(bn, &rxr->rx_db, type, map_idx, ring->fw_ring_id);
	bn->grp_info[map_idx].rx_fw_ring_id = ring->fw_ring_id;

	return 0;
}

static int bnge_hwrm_ring_alloc(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	bool agg_rings;
	int i, rc = 0;

	agg_rings = !!(bnge_is_agg_reqd(bd));
	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];
		struct bnge_nq_ring_info *nqr = &bnapi->nq_ring;
		struct bnge_ring_struct *ring = &nqr->ring_struct;
		u32 type = HWRM_RING_ALLOC_NQ;
		u32 map_idx = ring->map_idx;
		unsigned int vector;

		vector = bd->irq_tbl[map_idx].vector;
		disable_irq_nosync(vector);
		rc = hwrm_ring_alloc_send_msg(bn, ring, type, map_idx);
		if (rc) {
			enable_irq(vector);
			goto err_out;
		}
		bnge_set_db(bn, &nqr->nq_db, type, map_idx, ring->fw_ring_id);
		bnge_db_nq(bn, &nqr->nq_db, nqr->nq_raw_cons);
		enable_irq(vector);
		bn->grp_info[i].nq_fw_ring_id = ring->fw_ring_id;

		if (!i) {
			rc = bnge_hwrm_set_async_event_cr(bd, ring->fw_ring_id);
			if (rc)
				netdev_warn(bn->netdev, "Failed to set async event completion ring.\n");
		}
	}

	for (i = 0; i < bd->tx_nr_rings; i++) {
		struct bnge_tx_ring_info *txr = &bn->tx_ring[i];

		rc = bnge_hwrm_cp_ring_alloc(bn, txr->tx_cpr);
		if (rc)
			goto err_out;
		rc = bnge_hwrm_tx_ring_alloc(bn, txr, i);
		if (rc)
			goto err_out;
	}

	for (i = 0; i < bd->rx_nr_rings; i++) {
		struct bnge_rx_ring_info *rxr = &bn->rx_ring[i];
		struct bnge_cp_ring_info *cpr;
		struct bnge_ring_struct *ring;
		struct bnge_napi *bnapi;
		u32 map_idx, type;

		rc = bnge_hwrm_rx_ring_alloc(bn, rxr);
		if (rc)
			goto err_out;
		/* If we have agg rings, post agg buffers first. */
		if (!agg_rings)
			bnge_db_write(bn->bd, &rxr->rx_db, rxr->rx_prod);

		cpr = rxr->rx_cpr;
		bnapi = rxr->bnapi;
		type = HWRM_RING_ALLOC_CMPL;
		map_idx = bnapi->index;

		ring = &cpr->ring_struct;
		ring->handle = BNGE_SET_NQ_HDL(cpr);
		rc = hwrm_ring_alloc_send_msg(bn, ring, type, map_idx);
		if (rc)
			goto err_out;
		bnge_set_db(bn, &cpr->cp_db, type, map_idx,
			    ring->fw_ring_id);
		bnge_db_cq(bn, &cpr->cp_db, cpr->cp_raw_cons);
	}

	if (agg_rings) {
		for (i = 0; i < bd->rx_nr_rings; i++) {
			rc = bnge_hwrm_rx_agg_ring_alloc(bn, &bn->rx_ring[i]);
			if (rc)
				goto err_out;
		}
	}
err_out:
	return rc;
}

void bnge_fill_hw_rss_tbl(struct bnge_net *bn, struct bnge_vnic_info *vnic)
{
	__le16 *ring_tbl = vnic->rss_table;
	struct bnge_rx_ring_info *rxr;
	struct bnge_dev *bd = bn->bd;
	u16 tbl_size, i;

	tbl_size = bnge_get_rxfh_indir_size(bd);

	for (i = 0; i < tbl_size; i++) {
		u16 ring_id, j;

		j = bd->rss_indir_tbl[i];
		rxr = &bn->rx_ring[j];

		ring_id = rxr->rx_ring_struct.fw_ring_id;
		*ring_tbl++ = cpu_to_le16(ring_id);
		ring_id = bnge_cp_ring_for_rx(rxr);
		*ring_tbl++ = cpu_to_le16(ring_id);
	}
}

static int bnge_hwrm_vnic_rss_cfg(struct bnge_net *bn,
				  struct bnge_vnic_info *vnic)
{
	int rc;

	rc = bnge_hwrm_vnic_set_rss(bn, vnic, true);
	if (rc) {
		netdev_err(bn->netdev, "hwrm vnic %d set rss failure rc: %d\n",
			   vnic->vnic_id, rc);
		return rc;
	}
	rc = bnge_hwrm_vnic_cfg(bn, vnic);
	if (rc)
		netdev_err(bn->netdev, "hwrm vnic %d cfg failure rc: %d\n",
			   vnic->vnic_id, rc);
	return rc;
}

static int bnge_setup_vnic(struct bnge_net *bn, struct bnge_vnic_info *vnic)
{
	struct bnge_dev *bd = bn->bd;
	int rc, i, nr_ctxs;

	nr_ctxs = bnge_cal_nr_rss_ctxs(bd->rx_nr_rings);
	for (i = 0; i < nr_ctxs; i++) {
		rc = bnge_hwrm_vnic_ctx_alloc(bd, vnic, i);
		if (rc) {
			netdev_err(bn->netdev, "hwrm vnic %d ctx %d alloc failure rc: %d\n",
				   vnic->vnic_id, i, rc);
			return -ENOMEM;
		}
		bn->rsscos_nr_ctxs++;
	}

	rc = bnge_hwrm_vnic_rss_cfg(bn, vnic);
	if (rc)
		return rc;

	if (bnge_is_agg_reqd(bd)) {
		rc = bnge_hwrm_vnic_set_hds(bn, vnic);
		if (rc)
			netdev_err(bn->netdev, "hwrm vnic %d set hds failure rc: %d\n",
				   vnic->vnic_id, rc);
	}
	return rc;
}

static void bnge_del_l2_filter(struct bnge_net *bn, struct bnge_l2_filter *fltr)
{
	if (!refcount_dec_and_test(&fltr->refcnt))
		return;
	hlist_del_rcu(&fltr->base.hash);
	kfree_rcu(fltr, base.rcu);
}

static void bnge_init_l2_filter(struct bnge_net *bn,
				struct bnge_l2_filter *fltr,
				struct bnge_l2_key *key, u32 idx)
{
	struct hlist_head *head;

	ether_addr_copy(fltr->l2_key.dst_mac_addr, key->dst_mac_addr);
	fltr->l2_key.vlan = key->vlan;
	fltr->base.type = BNGE_FLTR_TYPE_L2;

	head = &bn->l2_fltr_hash_tbl[idx];
	hlist_add_head_rcu(&fltr->base.hash, head);
	refcount_set(&fltr->refcnt, 1);
}

static struct bnge_l2_filter *__bnge_lookup_l2_filter(struct bnge_net *bn,
						      struct bnge_l2_key *key,
						      u32 idx)
{
	struct bnge_l2_filter *fltr;
	struct hlist_head *head;

	head = &bn->l2_fltr_hash_tbl[idx];
	hlist_for_each_entry_rcu(fltr, head, base.hash) {
		struct bnge_l2_key *l2_key = &fltr->l2_key;

		if (ether_addr_equal(l2_key->dst_mac_addr, key->dst_mac_addr) &&
		    l2_key->vlan == key->vlan)
			return fltr;
	}
	return NULL;
}

static struct bnge_l2_filter *bnge_lookup_l2_filter(struct bnge_net *bn,
						    struct bnge_l2_key *key,
						    u32 idx)
{
	struct bnge_l2_filter *fltr;

	rcu_read_lock();
	fltr = __bnge_lookup_l2_filter(bn, key, idx);
	if (fltr)
		refcount_inc(&fltr->refcnt);
	rcu_read_unlock();
	return fltr;
}

static struct bnge_l2_filter *bnge_alloc_l2_filter(struct bnge_net *bn,
						   struct bnge_l2_key *key,
						   gfp_t gfp)
{
	struct bnge_l2_filter *fltr;
	u32 idx;

	idx = jhash2(&key->filter_key, BNGE_L2_KEY_SIZE, bn->hash_seed) &
	      BNGE_L2_FLTR_HASH_MASK;
	fltr = bnge_lookup_l2_filter(bn, key, idx);
	if (fltr)
		return fltr;

	fltr = kzalloc(sizeof(*fltr), gfp);
	if (!fltr)
		return ERR_PTR(-ENOMEM);

	bnge_init_l2_filter(bn, fltr, key, idx);
	return fltr;
}

static int bnge_hwrm_set_vnic_filter(struct bnge_net *bn, u16 vnic_id, u16 idx,
				     const u8 *mac_addr)
{
	struct bnge_l2_filter *fltr;
	struct bnge_l2_key key;
	int rc;

	ether_addr_copy(key.dst_mac_addr, mac_addr);
	key.vlan = 0;
	fltr = bnge_alloc_l2_filter(bn, &key, GFP_KERNEL);
	if (IS_ERR(fltr))
		return PTR_ERR(fltr);

	fltr->base.fw_vnic_id = bn->vnic_info[vnic_id].fw_vnic_id;
	rc = bnge_hwrm_l2_filter_alloc(bn->bd, fltr);
	if (rc)
		goto err_del_l2_filter;
	bn->vnic_info[vnic_id].l2_filters[idx] = fltr;
	return rc;

err_del_l2_filter:
	bnge_del_l2_filter(bn, fltr);
	return rc;
}

static bool bnge_mc_list_updated(struct bnge_net *bn, u32 *rx_mask)
{
	struct bnge_vnic_info *vnic = &bn->vnic_info[BNGE_VNIC_DEFAULT];
	struct net_device *dev = bn->netdev;
	struct netdev_hw_addr *ha;
	int mc_count = 0, off = 0;
	bool update = false;
	u8 *haddr;

	netdev_for_each_mc_addr(ha, dev) {
		if (mc_count >= BNGE_MAX_MC_ADDRS) {
			*rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
			vnic->mc_list_count = 0;
			return false;
		}
		haddr = ha->addr;
		if (!ether_addr_equal(haddr, vnic->mc_list + off)) {
			memcpy(vnic->mc_list + off, haddr, ETH_ALEN);
			update = true;
		}
		off += ETH_ALEN;
		mc_count++;
	}
	if (mc_count)
		*rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_MCAST;

	if (mc_count != vnic->mc_list_count) {
		vnic->mc_list_count = mc_count;
		update = true;
	}
	return update;
}

static bool bnge_uc_list_updated(struct bnge_net *bn)
{
	struct bnge_vnic_info *vnic = &bn->vnic_info[BNGE_VNIC_DEFAULT];
	struct net_device *dev = bn->netdev;
	struct netdev_hw_addr *ha;
	int off = 0;

	if (netdev_uc_count(dev) != (vnic->uc_filter_count - 1))
		return true;

	netdev_for_each_uc_addr(ha, dev) {
		if (!ether_addr_equal(ha->addr, vnic->uc_list + off))
			return true;

		off += ETH_ALEN;
	}
	return false;
}

static bool bnge_promisc_ok(struct bnge_net *bn)
{
	return true;
}

static int bnge_cfg_def_vnic(struct bnge_net *bn)
{
	struct bnge_vnic_info *vnic = &bn->vnic_info[BNGE_VNIC_DEFAULT];
	struct net_device *dev = bn->netdev;
	struct bnge_dev *bd = bn->bd;
	struct netdev_hw_addr *ha;
	int i, off = 0, rc;
	bool uc_update;

	netif_addr_lock_bh(dev);
	uc_update = bnge_uc_list_updated(bn);
	netif_addr_unlock_bh(dev);

	if (!uc_update)
		goto skip_uc;

	for (i = 1; i < vnic->uc_filter_count; i++) {
		struct bnge_l2_filter *fltr = vnic->l2_filters[i];

		bnge_hwrm_l2_filter_free(bd, fltr);
		bnge_del_l2_filter(bn, fltr);
	}

	vnic->uc_filter_count = 1;

	netif_addr_lock_bh(dev);
	if (netdev_uc_count(dev) > (BNGE_MAX_UC_ADDRS - 1)) {
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;
	} else {
		netdev_for_each_uc_addr(ha, dev) {
			memcpy(vnic->uc_list + off, ha->addr, ETH_ALEN);
			off += ETH_ALEN;
			vnic->uc_filter_count++;
		}
	}
	netif_addr_unlock_bh(dev);

	for (i = 1, off = 0; i < vnic->uc_filter_count; i++, off += ETH_ALEN) {
		rc = bnge_hwrm_set_vnic_filter(bn, 0, i, vnic->uc_list + off);
		if (rc) {
			netdev_err(dev, "HWRM vnic filter failure rc: %d\n", rc);
			vnic->uc_filter_count = i;
			return rc;
		}
	}

skip_uc:
	if ((vnic->rx_mask & CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS) &&
	    !bnge_promisc_ok(bn))
		vnic->rx_mask &= ~CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;
	rc = bnge_hwrm_cfa_l2_set_rx_mask(bd, vnic);
	if (rc && (vnic->rx_mask & CFA_L2_SET_RX_MASK_REQ_MASK_MCAST)) {
		netdev_info(dev, "Failed setting MC filters rc: %d, turning on ALL_MCAST mode\n",
			    rc);
		vnic->rx_mask &= ~CFA_L2_SET_RX_MASK_REQ_MASK_MCAST;
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
		vnic->mc_list_count = 0;
		rc = bnge_hwrm_cfa_l2_set_rx_mask(bd, vnic);
	}
	if (rc)
		netdev_err(dev, "HWRM cfa l2 rx mask failure rc: %d\n",
			   rc);

	return rc;
}

static void bnge_hwrm_vnic_free(struct bnge_net *bn)
{
	int i;

	for (i = 0; i < bn->nr_vnics; i++)
		bnge_hwrm_vnic_free_one(bn->bd, &bn->vnic_info[i]);
}

static void bnge_hwrm_vnic_ctx_free(struct bnge_net *bn)
{
	int i, j;

	for (i = 0; i < bn->nr_vnics; i++) {
		struct bnge_vnic_info *vnic = &bn->vnic_info[i];

		for (j = 0; j < BNGE_MAX_CTX_PER_VNIC; j++) {
			if (vnic->fw_rss_cos_lb_ctx[j] != INVALID_HW_RING_ID)
				bnge_hwrm_vnic_ctx_free_one(bn->bd, vnic, j);
		}
	}
	bn->rsscos_nr_ctxs = 0;
}

static void bnge_hwrm_clear_vnic_filter(struct bnge_net *bn)
{
	struct bnge_vnic_info *vnic = &bn->vnic_info[BNGE_VNIC_DEFAULT];
	int i;

	for (i = 0; i < vnic->uc_filter_count; i++) {
		struct bnge_l2_filter *fltr = vnic->l2_filters[i];

		bnge_hwrm_l2_filter_free(bn->bd, fltr);
		bnge_del_l2_filter(bn, fltr);
	}

	vnic->uc_filter_count = 0;
}

static void bnge_clear_vnic(struct bnge_net *bn)
{
	bnge_hwrm_clear_vnic_filter(bn);
	bnge_hwrm_vnic_free(bn);
	bnge_hwrm_vnic_ctx_free(bn);
}

static void bnge_hwrm_rx_ring_free(struct bnge_net *bn,
				   struct bnge_rx_ring_info *rxr,
				   bool close_path)
{
	struct bnge_ring_struct *ring = &rxr->rx_ring_struct;
	u32 grp_idx = rxr->bnapi->index;
	u32 cmpl_ring_id;

	if (ring->fw_ring_id == INVALID_HW_RING_ID)
		return;

	cmpl_ring_id = bnge_cp_ring_for_rx(rxr);
	hwrm_ring_free_send_msg(bn, ring,
				RING_FREE_REQ_RING_TYPE_RX,
				close_path ? cmpl_ring_id :
				INVALID_HW_RING_ID);
	ring->fw_ring_id = INVALID_HW_RING_ID;
	bn->grp_info[grp_idx].rx_fw_ring_id = INVALID_HW_RING_ID;
}

static void bnge_hwrm_rx_agg_ring_free(struct bnge_net *bn,
				       struct bnge_rx_ring_info *rxr,
				       bool close_path)
{
	struct bnge_ring_struct *ring = &rxr->rx_agg_ring_struct;
	u32 grp_idx = rxr->bnapi->index;
	u32 cmpl_ring_id;

	if (ring->fw_ring_id == INVALID_HW_RING_ID)
		return;

	cmpl_ring_id = bnge_cp_ring_for_rx(rxr);
	hwrm_ring_free_send_msg(bn, ring, RING_FREE_REQ_RING_TYPE_RX_AGG,
				close_path ? cmpl_ring_id :
				INVALID_HW_RING_ID);
	ring->fw_ring_id = INVALID_HW_RING_ID;
	bn->grp_info[grp_idx].agg_fw_ring_id = INVALID_HW_RING_ID;
}

static void bnge_hwrm_tx_ring_free(struct bnge_net *bn,
				   struct bnge_tx_ring_info *txr,
				   bool close_path)
{
	struct bnge_ring_struct *ring = &txr->tx_ring_struct;
	u32 cmpl_ring_id;

	if (ring->fw_ring_id == INVALID_HW_RING_ID)
		return;

	cmpl_ring_id = close_path ? bnge_cp_ring_for_tx(txr) :
		       INVALID_HW_RING_ID;
	hwrm_ring_free_send_msg(bn, ring, RING_FREE_REQ_RING_TYPE_TX,
				cmpl_ring_id);
	ring->fw_ring_id = INVALID_HW_RING_ID;
}

static void bnge_hwrm_cp_ring_free(struct bnge_net *bn,
				   struct bnge_cp_ring_info *cpr)
{
	struct bnge_ring_struct *ring;

	ring = &cpr->ring_struct;
	if (ring->fw_ring_id == INVALID_HW_RING_ID)
		return;

	hwrm_ring_free_send_msg(bn, ring, RING_FREE_REQ_RING_TYPE_L2_CMPL,
				INVALID_HW_RING_ID);
	ring->fw_ring_id = INVALID_HW_RING_ID;
}

static void bnge_hwrm_ring_free(struct bnge_net *bn, bool close_path)
{
	struct bnge_dev *bd = bn->bd;
	int i;

	if (!bn->bnapi)
		return;

	for (i = 0; i < bd->tx_nr_rings; i++)
		bnge_hwrm_tx_ring_free(bn, &bn->tx_ring[i], close_path);

	for (i = 0; i < bd->rx_nr_rings; i++) {
		bnge_hwrm_rx_ring_free(bn, &bn->rx_ring[i], close_path);
		bnge_hwrm_rx_agg_ring_free(bn, &bn->rx_ring[i], close_path);
	}

	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];
		struct bnge_nq_ring_info *nqr;
		struct bnge_ring_struct *ring;
		int j;

		nqr = &bnapi->nq_ring;
		for (j = 0; j < nqr->cp_ring_count && nqr->cp_ring_arr; j++)
			bnge_hwrm_cp_ring_free(bn, &nqr->cp_ring_arr[j]);

		ring = &nqr->ring_struct;
		if (ring->fw_ring_id != INVALID_HW_RING_ID) {
			hwrm_ring_free_send_msg(bn, ring,
						RING_FREE_REQ_RING_TYPE_NQ,
						INVALID_HW_RING_ID);
			ring->fw_ring_id = INVALID_HW_RING_ID;
			bn->grp_info[i].nq_fw_ring_id = INVALID_HW_RING_ID;
		}
	}
}

static void bnge_setup_msix(struct bnge_net *bn)
{
	struct net_device *dev = bn->netdev;
	struct bnge_dev *bd = bn->bd;
	int len, i;

	len = sizeof(bd->irq_tbl[0].name);
	for (i = 0; i < bd->nq_nr_rings; i++) {
		int map_idx = bnge_cp_num_to_irq_num(bn, i);
		char *attr;

		if (bd->flags & BNGE_EN_SHARED_CHNL)
			attr = "TxRx";
		else if (i < bd->rx_nr_rings)
			attr = "rx";
		else
			attr = "tx";

		snprintf(bd->irq_tbl[map_idx].name, len, "%s-%s-%d", dev->name,
			 attr, i);
		bd->irq_tbl[map_idx].handler = bnge_msix;
	}
}

static int bnge_setup_interrupts(struct bnge_net *bn)
{
	struct net_device *dev = bn->netdev;
	struct bnge_dev *bd = bn->bd;

	bnge_setup_msix(bn);

	return netif_set_real_num_queues(dev, bd->tx_nr_rings, bd->rx_nr_rings);
}

static void bnge_hwrm_resource_free(struct bnge_net *bn, bool close_path)
{
	bnge_clear_vnic(bn);
	bnge_hwrm_ring_free(bn, close_path);
	bnge_hwrm_stat_ctx_free(bn);
}

static void bnge_free_irq(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	struct bnge_irq *irq;
	int i;

	for (i = 0; i < bd->nq_nr_rings; i++) {
		int map_idx = bnge_cp_num_to_irq_num(bn, i);

		irq = &bd->irq_tbl[map_idx];
		if (irq->requested) {
			if (irq->have_cpumask) {
				irq_set_affinity_hint(irq->vector, NULL);
				free_cpumask_var(irq->cpu_mask);
				irq->have_cpumask = 0;
			}
			free_irq(irq->vector, bn->bnapi[i]);
		}

		irq->requested = 0;
	}
}

static int bnge_request_irq(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i, rc;

	rc = bnge_setup_interrupts(bn);
	if (rc) {
		netdev_err(bn->netdev, "bnge_setup_interrupts err: %d\n", rc);
		return rc;
	}
	for (i = 0; i < bd->nq_nr_rings; i++) {
		int map_idx = bnge_cp_num_to_irq_num(bn, i);
		struct bnge_irq *irq = &bd->irq_tbl[map_idx];

		rc = request_irq(irq->vector, irq->handler, 0, irq->name,
				 bn->bnapi[i]);
		if (rc)
			goto err_free_irq;

		netif_napi_set_irq_locked(&bn->bnapi[i]->napi, irq->vector);
		irq->requested = 1;

		if (zalloc_cpumask_var(&irq->cpu_mask, GFP_KERNEL)) {
			int numa_node = dev_to_node(&bd->pdev->dev);

			irq->have_cpumask = 1;
			cpumask_set_cpu(cpumask_local_spread(i, numa_node),
					irq->cpu_mask);
			rc = irq_set_affinity_hint(irq->vector, irq->cpu_mask);
			if (rc) {
				netdev_warn(bn->netdev,
					    "Set affinity failed, IRQ = %d\n",
					    irq->vector);
				goto err_free_irq;
			}
		}
	}
	return 0;

err_free_irq:
	bnge_free_irq(bn);
	return rc;
}

static int bnge_init_chip(struct bnge_net *bn)
{
	struct bnge_vnic_info *vnic = &bn->vnic_info[BNGE_VNIC_DEFAULT];
	struct bnge_dev *bd = bn->bd;
	int rc;

#define BNGE_DEF_STATS_COAL_TICKS	 1000000
	bn->stats_coal_ticks = BNGE_DEF_STATS_COAL_TICKS;

	rc = bnge_hwrm_stat_ctx_alloc(bn);
	if (rc) {
		netdev_err(bn->netdev, "hwrm stat ctx alloc failure rc: %d\n", rc);
		goto err_out;
	}

	rc = bnge_hwrm_ring_alloc(bn);
	if (rc) {
		netdev_err(bn->netdev, "hwrm ring alloc failure rc: %d\n", rc);
		goto err_out;
	}

	rc = bnge_hwrm_vnic_alloc(bd, vnic, bd->rx_nr_rings);
	if (rc) {
		netdev_err(bn->netdev, "hwrm vnic alloc failure rc: %d\n", rc);
		goto err_out;
	}

	rc = bnge_setup_vnic(bn, vnic);
	if (rc)
		goto err_out;

	if (bd->rss_cap & BNGE_RSS_CAP_RSS_HASH_TYPE_DELTA)
		bnge_hwrm_update_rss_hash_cfg(bn);

	/* Filter for default vnic 0 */
	rc = bnge_hwrm_set_vnic_filter(bn, 0, 0, bn->netdev->dev_addr);
	if (rc) {
		netdev_err(bn->netdev, "HWRM vnic filter failure rc: %d\n", rc);
		goto err_out;
	}
	vnic->uc_filter_count = 1;

	vnic->rx_mask = 0;

	if (bn->netdev->flags & IFF_BROADCAST)
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_BCAST;

	if (bn->netdev->flags & IFF_PROMISC)
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS;

	if (bn->netdev->flags & IFF_ALLMULTI) {
		vnic->rx_mask |= CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST;
		vnic->mc_list_count = 0;
	} else if (bn->netdev->flags & IFF_MULTICAST) {
		u32 mask = 0;

		bnge_mc_list_updated(bn, &mask);
		vnic->rx_mask |= mask;
	}

	rc = bnge_cfg_def_vnic(bn);
	if (rc)
		goto err_out;
	return 0;

err_out:
	bnge_hwrm_resource_free(bn, 0);
	return rc;
}

static int bnge_napi_poll(struct napi_struct *napi, int budget)
{
	int work_done = 0;

	/* defer NAPI implementation to next patch series */
	napi_complete_done(napi, work_done);

	return work_done;
}

static void bnge_init_napi(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	struct bnge_napi *bnapi;
	int i;

	for (i = 0; i < bd->nq_nr_rings; i++) {
		bnapi = bn->bnapi[i];
		netif_napi_add_config_locked(bn->netdev, &bnapi->napi,
					     bnge_napi_poll, bnapi->index);
	}
}

static void bnge_del_napi(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;
	int i;

	for (i = 0; i < bd->rx_nr_rings; i++)
		netif_queue_set_napi(bn->netdev, i, NETDEV_QUEUE_TYPE_RX, NULL);
	for (i = 0; i < bd->tx_nr_rings; i++)
		netif_queue_set_napi(bn->netdev, i, NETDEV_QUEUE_TYPE_TX, NULL);

	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];

		__netif_napi_del_locked(&bnapi->napi);
	}

	/* Wait for RCU grace period after removing NAPI instances */
	synchronize_net();
}

static int bnge_init_nic(struct bnge_net *bn)
{
	int rc;

	bnge_init_nq_tree(bn);

	bnge_init_rx_rings(bn);
	rc = bnge_alloc_rx_ring_pair_bufs(bn);
	if (rc)
		return rc;

	bnge_init_tx_rings(bn);

	rc = bnge_init_ring_grps(bn);
	if (rc)
		goto err_free_rx_ring_pair_bufs;

	bnge_init_vnics(bn);

	rc = bnge_init_chip(bn);
	if (rc)
		goto err_free_ring_grps;
	return rc;

err_free_ring_grps:
	bnge_free_ring_grps(bn);
	return rc;

err_free_rx_ring_pair_bufs:
	bnge_free_rx_ring_pair_bufs(bn);
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

	bnge_init_napi(bn);
	rc = bnge_request_irq(bn);
	if (rc) {
		netdev_err(bn->netdev, "bnge_request_irq err: %d\n", rc);
		goto err_del_napi;
	}

	rc = bnge_init_nic(bn);
	if (rc) {
		netdev_err(bn->netdev, "bnge_init_nic err: %d\n", rc);
		goto err_free_irq;
	}
	set_bit(BNGE_STATE_OPEN, &bd->state);
	return 0;

err_free_irq:
	bnge_free_irq(bn);
err_del_napi:
	bnge_del_napi(bn);
	bnge_free_core(bn);
	return rc;
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

static int bnge_shutdown_nic(struct bnge_net *bn)
{
	/* TODO: close_path = 0 until we make NAPI functional */
	bnge_hwrm_resource_free(bn, 0);
	return 0;
}

static void bnge_close_core(struct bnge_net *bn)
{
	struct bnge_dev *bd = bn->bd;

	clear_bit(BNGE_STATE_OPEN, &bd->state);
	bnge_shutdown_nic(bn);
	bnge_free_all_rings_bufs(bn);
	bnge_free_irq(bn);
	bnge_del_napi(bn);

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

	netdev->request_ops_lock = true;
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
