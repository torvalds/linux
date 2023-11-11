/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#ifndef _NFP_NET_DP_
#define _NFP_NET_DP_

#include "nfp_net.h"

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

static inline u32
nfp_net_read_tx_cmpl(struct nfp_net_tx_ring *tx_ring, struct nfp_net_dp *dp)
{
	if (tx_ring->txrwb)
		return *tx_ring->txrwb;
	return nfp_qcp_rd_ptr_read(tx_ring->qcp_q);
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
bool nfp_net_vlan_strip(struct sk_buff *skb, const struct nfp_net_rx_desc *rxd,
			const struct nfp_meta_parsed *meta);

enum nfp_nfd_version {
	NFP_NFD_VER_NFD3,
	NFP_NFD_VER_NFDK,
};

/**
 * struct nfp_dp_ops - Hooks to wrap different implementation of different dp
 * @version:			Indicate dp type
 * @tx_min_desc_per_pkt:	Minimal TX descs needed for each packet
 * @cap_mask:			Mask of supported features
 * @dma_mask:			DMA addressing capability
 * @poll:			Napi poll for normal rx/tx
 * @xsk_poll:			Napi poll when xsk is enabled
 * @ctrl_poll:			Tasklet poll for ctrl rx/tx
 * @xmit:			Xmit for normal path
 * @ctrl_tx_one:		Xmit for ctrl path
 * @rx_ring_fill_freelist:	Give buffers from the ring to FW
 * @tx_ring_alloc:		Allocate resource for a TX ring
 * @tx_ring_reset:		Free any untransmitted buffers and reset pointers
 * @tx_ring_free:		Free resources allocated to a TX ring
 * @tx_ring_bufs_alloc:		Allocate resource for each TX buffer
 * @tx_ring_bufs_free:		Free resources allocated to each TX buffer
 * @print_tx_descs:		Show TX ring's info for debug purpose
 */
struct nfp_dp_ops {
	enum nfp_nfd_version version;
	unsigned int tx_min_desc_per_pkt;
	u32 cap_mask;
	u64 dma_mask;

	int (*poll)(struct napi_struct *napi, int budget);
	int (*xsk_poll)(struct napi_struct *napi, int budget);
	void (*ctrl_poll)(struct tasklet_struct *t);
	netdev_tx_t (*xmit)(struct sk_buff *skb, struct net_device *netdev);
	bool (*ctrl_tx_one)(struct nfp_net *nn, struct nfp_net_r_vector *r_vec,
			    struct sk_buff *skb, bool old);
	void (*rx_ring_fill_freelist)(struct nfp_net_dp *dp,
				      struct nfp_net_rx_ring *rx_ring);
	int (*tx_ring_alloc)(struct nfp_net_dp *dp,
			     struct nfp_net_tx_ring *tx_ring);
	void (*tx_ring_reset)(struct nfp_net_dp *dp,
			      struct nfp_net_tx_ring *tx_ring);
	void (*tx_ring_free)(struct nfp_net_tx_ring *tx_ring);
	int (*tx_ring_bufs_alloc)(struct nfp_net_dp *dp,
				  struct nfp_net_tx_ring *tx_ring);
	void (*tx_ring_bufs_free)(struct nfp_net_dp *dp,
				  struct nfp_net_tx_ring *tx_ring);

	void (*print_tx_descs)(struct seq_file *file,
			       struct nfp_net_r_vector *r_vec,
			       struct nfp_net_tx_ring *tx_ring,
			       u32 d_rd_p, u32 d_wr_p);
};

static inline void
nfp_net_tx_ring_reset(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring)
{
	return dp->ops->tx_ring_reset(dp, tx_ring);
}

static inline void
nfp_net_rx_ring_fill_freelist(struct nfp_net_dp *dp,
			      struct nfp_net_rx_ring *rx_ring)
{
	dp->ops->rx_ring_fill_freelist(dp, rx_ring);
}

static inline int
nfp_net_tx_ring_alloc(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring)
{
	return dp->ops->tx_ring_alloc(dp, tx_ring);
}

static inline void
nfp_net_tx_ring_free(struct nfp_net_dp *dp, struct nfp_net_tx_ring *tx_ring)
{
	dp->ops->tx_ring_free(tx_ring);
}

static inline int
nfp_net_tx_ring_bufs_alloc(struct nfp_net_dp *dp,
			   struct nfp_net_tx_ring *tx_ring)
{
	return dp->ops->tx_ring_bufs_alloc(dp, tx_ring);
}

static inline void
nfp_net_tx_ring_bufs_free(struct nfp_net_dp *dp,
			  struct nfp_net_tx_ring *tx_ring)
{
	dp->ops->tx_ring_bufs_free(dp, tx_ring);
}

static inline void
nfp_net_debugfs_print_tx_descs(struct seq_file *file, struct nfp_net_dp *dp,
			       struct nfp_net_r_vector *r_vec,
			       struct nfp_net_tx_ring *tx_ring,
			       u32 d_rd_p, u32 d_wr_p)
{
	dp->ops->print_tx_descs(file, r_vec, tx_ring, d_rd_p, d_wr_p);
}

extern const struct nfp_dp_ops nfp_nfd3_ops;
extern const struct nfp_dp_ops nfp_nfdk_ops;

netdev_tx_t nfp_net_tx(struct sk_buff *skb, struct net_device *netdev);

#endif /* _NFP_NET_DP_ */
