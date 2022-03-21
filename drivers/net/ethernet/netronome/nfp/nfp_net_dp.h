/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#ifndef _NFP_NET_DP_
#define _NFP_NET_DP_

#include "nfp_net.h"
#include "nfd3/nfd3.h"

static inline dma_addr_t nfp_net_dma_map_rx(struct nfp_net_dp *dp, void *frag)
{
	return dma_map_single_attrs(dp->dev, frag + NFP_NET_RX_BUF_HEADROOM,
				    dp->fl_bufsz - NFP_NET_RX_BUF_NON_DATA,
				    dp->rx_dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
}

static inline void
nfp_net_dma_sync_dev_rx(const struct nfp_net_dp *dp, dma_addr_t dma_addr)
{
	dma_sync_single_for_device(dp->dev, dma_addr,
				   dp->fl_bufsz - NFP_NET_RX_BUF_NON_DATA,
				   dp->rx_dma_dir);
}

static inline void nfp_net_dma_unmap_rx(struct nfp_net_dp *dp,
					dma_addr_t dma_addr)
{
	dma_unmap_single_attrs(dp->dev, dma_addr,
			       dp->fl_bufsz - NFP_NET_RX_BUF_NON_DATA,
			       dp->rx_dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
}

static inline void nfp_net_dma_sync_cpu_rx(struct nfp_net_dp *dp,
					   dma_addr_t dma_addr,
					   unsigned int len)
{
	dma_sync_single_for_cpu(dp->dev, dma_addr - NFP_NET_RX_BUF_HEADROOM,
				len, dp->rx_dma_dir);
}

/**
 * nfp_net_tx_full() - check if the TX ring is full
 * @tx_ring: TX ring to check
 * @dcnt:    Number of descriptors that need to be enqueued (must be >= 1)
 *
 * This function checks, based on the *host copy* of read/write
 * pointer if a given TX ring is full.  The real TX queue may have
 * some newly made available slots.
 *
 * Return: True if the ring is full.
 */
static inline int nfp_net_tx_full(struct nfp_net_tx_ring *tx_ring, int dcnt)
{
	return (tx_ring->wr_p - tx_ring->rd_p) >= (tx_ring->cnt - dcnt);
}

static inline void nfp_net_tx_xmit_more_flush(struct nfp_net_tx_ring *tx_ring)
{
	wmb(); /* drain writebuffer */
	nfp_qcp_wr_ptr_add(tx_ring->qcp_q, tx_ring->wr_ptr_add);
	tx_ring->wr_ptr_add = 0;
}

static inline void nfp_net_free_frag(void *frag, bool xdp)
{
	if (!xdp)
		skb_free_frag(frag);
	else
		__free_page(virt_to_page(frag));
}

/**
 * nfp_net_irq_unmask() - Unmask automasked interrupt
 * @nn:       NFP Network structure
 * @entry_nr: MSI-X table entry
 *
 * Clear the ICR for the IRQ entry.
 */
static inline void nfp_net_irq_unmask(struct nfp_net *nn, unsigned int entry_nr)
{
	nn_writeb(nn, NFP_NET_CFG_ICR(entry_nr), NFP_NET_CFG_ICR_UNMASKED);
	nn_pci_flush(nn);
}

struct seq_file;

/* Common */
void
nfp_net_rx_ring_hw_cfg_write(struct nfp_net *nn,
			     struct nfp_net_rx_ring *rx_ring, unsigned int idx);
void
nfp_net_tx_ring_hw_cfg_write(struct nfp_net *nn,
			     struct nfp_net_tx_ring *tx_ring, unsigned int idx);
void nfp_net_vec_clear_ring_data(struct nfp_net *nn, unsigned int idx);

void *nfp_net_rx_alloc_one(struct nfp_net_dp *dp, dma_addr_t *dma_addr);
int nfp_net_rx_rings_prepare(struct nfp_net *nn, struct nfp_net_dp *dp);
int nfp_net_tx_rings_prepare(struct nfp_net *nn, struct nfp_net_dp *dp);
void nfp_net_rx_rings_free(struct nfp_net_dp *dp);
void nfp_net_tx_rings_free(struct nfp_net_dp *dp);
void nfp_net_rx_ring_reset(struct nfp_net_rx_ring *rx_ring);

void
nfp_net_tx_ring_reset(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring);
void nfp_net_rx_ring_fill_freelist(struct nfp_net_dp *dp,
				   struct nfp_net_rx_ring *rx_ring);
int
nfp_net_tx_ring_alloc(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring);
void
nfp_net_tx_ring_free(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring);
int nfp_net_tx_ring_bufs_alloc(struct nfp_net_dp *dp,
			       struct nfp_net_tx_ring *tx_ring);
void nfp_net_tx_ring_bufs_free(struct nfp_net_dp *dp,
			       struct nfp_net_tx_ring *tx_ring);
void
nfp_net_debugfs_print_tx_descs(struct seq_file *file,
			       struct nfp_net_r_vector *r_vec,
			       struct nfp_net_tx_ring *tx_ring,
			       u32 d_rd_p, u32 d_wr_p);
#endif /* _NFP_NET_DP_ */
