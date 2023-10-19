// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2019 Netronome Systems, Inc. */

#include <linux/seq_file.h>

#include "../nfp_net.h"
#include "../nfp_net_dp.h"
#include "../nfp_net_xsk.h"
#include "nfd3.h"

static void nfp_nfd3_xsk_tx_bufs_free(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_nfd3_tx_buf *txbuf;
	unsigned int idx;

	while (tx_ring->rd_p != tx_ring->wr_p) {
		idx = D_IDX(tx_ring, tx_ring->rd_p);
		txbuf = &tx_ring->txbufs[idx];

		txbuf->real_len = 0;

		tx_ring->qcp_rd_p++;
		tx_ring->rd_p++;

		if (tx_ring->r_vec->xsk_pool) {
			if (txbuf->is_xsk_tx)
				nfp_nfd3_xsk_tx_free(txbuf);

			xsk_tx_completed(tx_ring->r_vec->xsk_pool, 1);
		}
	}
}

/**
 * nfp_nfd3_tx_ring_reset() - Free any untransmitted buffers and reset pointers
 * @dp:		NFP Net data path struct
 * @tx_ring:	TX ring structure
 *
 * Assumes that the device is stopped, must be idempotent.
 */
static void
nfp_nfd3_tx_ring_reset(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring)
{
	struct netdev_queue *nd_q;
	const skb_frag_t *frag;

	while (!tx_ring->is_xdp && tx_ring->rd_p != tx_ring->wr_p) {
		struct nfp_nfd3_tx_buf *tx_buf;
		struct sk_buff *skb;
		int idx, nr_frags;

		idx = D_IDX(tx_ring, tx_ring->rd_p);
		tx_buf = &tx_ring->txbufs[idx];

		skb = tx_ring->txbufs[idx].skb;
		nr_frags = skb_shinfo(skb)->nr_frags;

		if (tx_buf->fidx == -1) {
			/* unmap head */
			dma_unmap_single(dp->dev, tx_buf->dma_addr,
					 skb_headlen(skb), DMA_TO_DEVICE);
		} else {
			/* unmap fragment */
			frag = &skb_shinfo(skb)->frags[tx_buf->fidx];
			dma_unmap_page(dp->dev, tx_buf->dma_addr,
				       skb_frag_size(frag), DMA_TO_DEVICE);
		}

		/* check for last gather fragment */
		if (tx_buf->fidx == nr_frags - 1)
			dev_kfree_skb_any(skb);

		tx_buf->dma_addr = 0;
		tx_buf->skb = NULL;
		tx_buf->fidx = -2;

		tx_ring->qcp_rd_p++;
		tx_ring->rd_p++;
	}

	if (tx_ring->is_xdp)
		nfp_nfd3_xsk_tx_bufs_free(tx_ring);

	memset(tx_ring->txds, 0, tx_ring->size);
	tx_ring->wr_p = 0;
	tx_ring->rd_p = 0;
	tx_ring->qcp_rd_p = 0;
	tx_ring->wr_ptr_add = 0;

	if (tx_ring->is_xdp || !dp->netdev)
		return;

	nd_q = netdev_get_tx_queue(dp->netdev, tx_ring->idx);
	netdev_tx_reset_queue(nd_q);
}

/**
 * nfp_nfd3_tx_ring_free() - Free resources allocated to a TX ring
 * @tx_ring:   TX ring to free
 */
static void nfp_nfd3_tx_ring_free(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;

	kvfree(tx_ring->txbufs);

	if (tx_ring->txds)
		dma_free_coherent(dp->dev, tx_ring->size,
				  tx_ring->txds, tx_ring->dma);

	tx_ring->cnt = 0;
	tx_ring->txbufs = NULL;
	tx_ring->txds = NULL;
	tx_ring->dma = 0;
	tx_ring->size = 0;
}

/**
 * nfp_nfd3_tx_ring_alloc() - Allocate resource for a TX ring
 * @dp:        NFP Net data path struct
 * @tx_ring:   TX Ring structure to allocate
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int
nfp_nfd3_tx_ring_alloc(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;

	tx_ring->cnt = dp->txd_cnt;

	tx_ring->size = array_size(tx_ring->cnt, sizeof(*tx_ring->txds));
	tx_ring->txds = dma_alloc_coherent(dp->dev, tx_ring->size,
					   &tx_ring->dma,
					   GFP_KERNEL | __GFP_NOWARN);
	if (!tx_ring->txds) {
		netdev_warn(dp->netdev, "failed to allocate TX descriptor ring memory, requested descriptor count: %d, consider lowering descriptor count\n",
			    tx_ring->cnt);
		goto err_alloc;
	}

	tx_ring->txbufs = kvcalloc(tx_ring->cnt, sizeof(*tx_ring->txbufs),
				   GFP_KERNEL);
	if (!tx_ring->txbufs)
		goto err_alloc;

	if (!tx_ring->is_xdp && dp->netdev)
		netif_set_xps_queue(dp->netdev, &r_vec->affinity_mask,
				    tx_ring->idx);

	return 0;

err_alloc:
	nfp_nfd3_tx_ring_free(tx_ring);
	return -ENOMEM;
}

static void
nfp_nfd3_tx_ring_bufs_free(struct nfp_net_dp *dp,
			   struct nfp_net_tx_ring *tx_ring)
{
	unsigned int i;

	if (!tx_ring->is_xdp)
		return;

	for (i = 0; i < tx_ring->cnt; i++) {
		if (!tx_ring->txbufs[i].frag)
			return;

		nfp_net_dma_unmap_rx(dp, tx_ring->txbufs[i].dma_addr);
		__free_page(virt_to_page(tx_ring->txbufs[i].frag));
	}
}

static int
nfp_nfd3_tx_ring_bufs_alloc(struct nfp_net_dp *dp,
			    struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_nfd3_tx_buf *txbufs = tx_ring->txbufs;
	unsigned int i;

	if (!tx_ring->is_xdp)
		return 0;

	for (i = 0; i < tx_ring->cnt; i++) {
		txbufs[i].frag = nfp_net_rx_alloc_one(dp, &txbufs[i].dma_addr);
		if (!txbufs[i].frag) {
			nfp_nfd3_tx_ring_bufs_free(dp, tx_ring);
			return -ENOMEM;
		}
	}

	return 0;
}

static void
nfp_nfd3_print_tx_descs(struct seq_file *file,
			struct nfp_net_r_vector *r_vec,
			struct nfp_net_tx_ring *tx_ring,
			u32 d_rd_p, u32 d_wr_p)
{
	struct nfp_nfd3_tx_desc *txd;
	u32 txd_cnt = tx_ring->cnt;
	int i;

	for (i = 0; i < txd_cnt; i++) {
		struct xdp_buff *xdp;
		struct sk_buff *skb;

		txd = &tx_ring->txds[i];
		seq_printf(file, "%04d: 0x%08x 0x%08x 0x%08x 0x%08x", i,
			   txd->vals[0], txd->vals[1],
			   txd->vals[2], txd->vals[3]);

		if (!tx_ring->is_xdp) {
			skb = READ_ONCE(tx_ring->txbufs[i].skb);
			if (skb)
				seq_printf(file, " skb->head=%p skb->data=%p",
					   skb->head, skb->data);
		} else {
			xdp = READ_ONCE(tx_ring->txbufs[i].xdp);
			if (xdp)
				seq_printf(file, " xdp->data=%p", xdp->data);
		}

		if (tx_ring->txbufs[i].dma_addr)
			seq_printf(file, " dma_addr=%pad",
				   &tx_ring->txbufs[i].dma_addr);

		if (i == tx_ring->rd_p % txd_cnt)
			seq_puts(file, " H_RD");
		if (i == tx_ring->wr_p % txd_cnt)
			seq_puts(file, " H_WR");
		if (i == d_rd_p % txd_cnt)
			seq_puts(file, " D_RD");
		if (i == d_wr_p % txd_cnt)
			seq_puts(file, " D_WR");

		seq_putc(file, '\n');
	}
}

#define NFP_NFD3_CFG_CTRL_SUPPORTED					\
	(NFP_NET_CFG_CTRL_ENABLE | NFP_NET_CFG_CTRL_PROMISC |		\
	 NFP_NET_CFG_CTRL_L2BC | NFP_NET_CFG_CTRL_L2MC |		\
	 NFP_NET_CFG_CTRL_RXCSUM | NFP_NET_CFG_CTRL_TXCSUM |		\
	 NFP_NET_CFG_CTRL_RXVLAN | NFP_NET_CFG_CTRL_TXVLAN |		\
	 NFP_NET_CFG_CTRL_RXVLAN_V2 | NFP_NET_CFG_CTRL_RXQINQ |		\
	 NFP_NET_CFG_CTRL_TXVLAN_V2 |					\
	 NFP_NET_CFG_CTRL_GATHER | NFP_NET_CFG_CTRL_LSO |		\
	 NFP_NET_CFG_CTRL_CTAG_FILTER | NFP_NET_CFG_CTRL_CMSG_DATA |	\
	 NFP_NET_CFG_CTRL_RINGCFG | NFP_NET_CFG_CTRL_RSS |		\
	 NFP_NET_CFG_CTRL_IRQMOD | NFP_NET_CFG_CTRL_TXRWB |		\
	 NFP_NET_CFG_CTRL_VEPA |					\
	 NFP_NET_CFG_CTRL_VXLAN | NFP_NET_CFG_CTRL_NVGRE |		\
	 NFP_NET_CFG_CTRL_BPF | NFP_NET_CFG_CTRL_LSO2 |			\
	 NFP_NET_CFG_CTRL_RSS2 | NFP_NET_CFG_CTRL_CSUM_COMPLETE |	\
	 NFP_NET_CFG_CTRL_LIVE_ADDR)

const struct nfp_dp_ops nfp_nfd3_ops = {
	.version		= NFP_NFD_VER_NFD3,
	.tx_min_desc_per_pkt	= 1,
	.cap_mask		= NFP_NFD3_CFG_CTRL_SUPPORTED,
	.dma_mask		= DMA_BIT_MASK(40),
	.poll			= nfp_nfd3_poll,
	.xsk_poll		= nfp_nfd3_xsk_poll,
	.ctrl_poll		= nfp_nfd3_ctrl_poll,
	.xmit			= nfp_nfd3_tx,
	.ctrl_tx_one		= nfp_nfd3_ctrl_tx_one,
	.rx_ring_fill_freelist	= nfp_nfd3_rx_ring_fill_freelist,
	.tx_ring_alloc		= nfp_nfd3_tx_ring_alloc,
	.tx_ring_reset		= nfp_nfd3_tx_ring_reset,
	.tx_ring_free		= nfp_nfd3_tx_ring_free,
	.tx_ring_bufs_alloc	= nfp_nfd3_tx_ring_bufs_alloc,
	.tx_ring_bufs_free	= nfp_nfd3_tx_ring_bufs_free,
	.print_tx_descs		= nfp_nfd3_print_tx_descs
};
