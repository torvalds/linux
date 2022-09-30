// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <linux/seq_file.h>

#include "../nfp_net.h"
#include "../nfp_net_dp.h"
#include "nfdk.h"

static void
nfp_nfdk_tx_ring_reset(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring)
{
	struct device *dev = dp->dev;
	struct netdev_queue *nd_q;

	while (!tx_ring->is_xdp && tx_ring->rd_p != tx_ring->wr_p) {
		const skb_frag_t *frag, *fend;
		unsigned int size, n_descs = 1;
		struct nfp_nfdk_tx_buf *txbuf;
		int nr_frags, rd_idx;
		struct sk_buff *skb;

		rd_idx = D_IDX(tx_ring, tx_ring->rd_p);
		txbuf = &tx_ring->ktxbufs[rd_idx];

		skb = txbuf->skb;
		if (!skb) {
			n_descs = D_BLOCK_CPL(tx_ring->rd_p);
			goto next;
		}

		nr_frags = skb_shinfo(skb)->nr_frags;
		txbuf++;

		/* Unmap head */
		size = skb_headlen(skb);
		dma_unmap_single(dev, txbuf->dma_addr, size, DMA_TO_DEVICE);
		n_descs += nfp_nfdk_headlen_to_segs(size);
		txbuf++;

		frag = skb_shinfo(skb)->frags;
		fend = frag + nr_frags;
		for (; frag < fend; frag++) {
			size = skb_frag_size(frag);
			dma_unmap_page(dev, txbuf->dma_addr,
				       skb_frag_size(frag), DMA_TO_DEVICE);
			n_descs += DIV_ROUND_UP(size,
						NFDK_TX_MAX_DATA_PER_DESC);
			txbuf++;
		}

		if (skb_is_gso(skb))
			n_descs++;

		dev_kfree_skb_any(skb);
next:
		tx_ring->rd_p += n_descs;
	}

	memset(tx_ring->txds, 0, tx_ring->size);
	tx_ring->data_pending = 0;
	tx_ring->wr_p = 0;
	tx_ring->rd_p = 0;
	tx_ring->qcp_rd_p = 0;
	tx_ring->wr_ptr_add = 0;

	if (tx_ring->is_xdp || !dp->netdev)
		return;

	nd_q = netdev_get_tx_queue(dp->netdev, tx_ring->idx);
	netdev_tx_reset_queue(nd_q);
}

static void nfp_nfdk_tx_ring_free(struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;

	kvfree(tx_ring->ktxbufs);

	if (tx_ring->ktxds)
		dma_free_coherent(dp->dev, tx_ring->size,
				  tx_ring->ktxds, tx_ring->dma);

	tx_ring->cnt = 0;
	tx_ring->txbufs = NULL;
	tx_ring->txds = NULL;
	tx_ring->dma = 0;
	tx_ring->size = 0;
}

static int
nfp_nfdk_tx_ring_alloc(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring)
{
	struct nfp_net_r_vector *r_vec = tx_ring->r_vec;

	tx_ring->cnt = dp->txd_cnt * NFDK_TX_DESC_PER_SIMPLE_PKT;
	tx_ring->size = array_size(tx_ring->cnt, sizeof(*tx_ring->ktxds));
	tx_ring->ktxds = dma_alloc_coherent(dp->dev, tx_ring->size,
					    &tx_ring->dma,
					    GFP_KERNEL | __GFP_NOWARN);
	if (!tx_ring->ktxds) {
		netdev_warn(dp->netdev, "failed to allocate TX descriptor ring memory, requested descriptor count: %d, consider lowering descriptor count\n",
			    tx_ring->cnt);
		goto err_alloc;
	}

	tx_ring->ktxbufs = kvcalloc(tx_ring->cnt, sizeof(*tx_ring->ktxbufs),
				    GFP_KERNEL);
	if (!tx_ring->ktxbufs)
		goto err_alloc;

	if (!tx_ring->is_xdp && dp->netdev)
		netif_set_xps_queue(dp->netdev, &r_vec->affinity_mask,
				    tx_ring->idx);

	return 0;

err_alloc:
	nfp_nfdk_tx_ring_free(tx_ring);
	return -ENOMEM;
}

static void
nfp_nfdk_tx_ring_bufs_free(struct nfp_net_dp *dp,
			   struct nfp_net_tx_ring *tx_ring)
{
}

static int
nfp_nfdk_tx_ring_bufs_alloc(struct nfp_net_dp *dp,
			    struct nfp_net_tx_ring *tx_ring)
{
	return 0;
}

static void
nfp_nfdk_print_tx_descs(struct seq_file *file,
			struct nfp_net_r_vector *r_vec,
			struct nfp_net_tx_ring *tx_ring,
			u32 d_rd_p, u32 d_wr_p)
{
	struct nfp_nfdk_tx_desc *txd;
	u32 txd_cnt = tx_ring->cnt;
	int i;

	for (i = 0; i < txd_cnt; i++) {
		txd = &tx_ring->ktxds[i];

		seq_printf(file, "%04d: 0x%08x 0x%08x 0x%016llx", i,
			   txd->vals[0], txd->vals[1], tx_ring->ktxbufs[i].raw);

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

#define NFP_NFDK_CFG_CTRL_SUPPORTED					\
	(NFP_NET_CFG_CTRL_ENABLE | NFP_NET_CFG_CTRL_PROMISC |		\
	 NFP_NET_CFG_CTRL_L2BC | NFP_NET_CFG_CTRL_L2MC |		\
	 NFP_NET_CFG_CTRL_RXCSUM | NFP_NET_CFG_CTRL_TXCSUM |		\
	 NFP_NET_CFG_CTRL_RXVLAN |					\
	 NFP_NET_CFG_CTRL_RXVLAN_V2 | NFP_NET_CFG_CTRL_RXQINQ |		\
	 NFP_NET_CFG_CTRL_TXVLAN_V2 |					\
	 NFP_NET_CFG_CTRL_GATHER | NFP_NET_CFG_CTRL_LSO |		\
	 NFP_NET_CFG_CTRL_CTAG_FILTER | NFP_NET_CFG_CTRL_CMSG_DATA |	\
	 NFP_NET_CFG_CTRL_RINGCFG | NFP_NET_CFG_CTRL_IRQMOD |		\
	 NFP_NET_CFG_CTRL_TXRWB | NFP_NET_CFG_CTRL_VEPA |		\
	 NFP_NET_CFG_CTRL_VXLAN | NFP_NET_CFG_CTRL_NVGRE |		\
	 NFP_NET_CFG_CTRL_BPF | NFP_NET_CFG_CTRL_LSO2 |			\
	 NFP_NET_CFG_CTRL_RSS2 | NFP_NET_CFG_CTRL_CSUM_COMPLETE |	\
	 NFP_NET_CFG_CTRL_LIVE_ADDR)

const struct nfp_dp_ops nfp_nfdk_ops = {
	.version		= NFP_NFD_VER_NFDK,
	.tx_min_desc_per_pkt	= NFDK_TX_DESC_PER_SIMPLE_PKT,
	.cap_mask		= NFP_NFDK_CFG_CTRL_SUPPORTED,
	.dma_mask		= DMA_BIT_MASK(48),
	.poll			= nfp_nfdk_poll,
	.ctrl_poll		= nfp_nfdk_ctrl_poll,
	.xmit			= nfp_nfdk_tx,
	.ctrl_tx_one		= nfp_nfdk_ctrl_tx_one,
	.rx_ring_fill_freelist	= nfp_nfdk_rx_ring_fill_freelist,
	.tx_ring_alloc		= nfp_nfdk_tx_ring_alloc,
	.tx_ring_reset		= nfp_nfdk_tx_ring_reset,
	.tx_ring_free		= nfp_nfdk_tx_ring_free,
	.tx_ring_bufs_alloc	= nfp_nfdk_tx_ring_bufs_alloc,
	.tx_ring_bufs_free	= nfp_nfdk_tx_ring_bufs_free,
	.print_tx_descs		= nfp_nfdk_print_tx_descs
};
