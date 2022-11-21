// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2019 Netronome Systems, Inc. */

#include "nfp_app.h"
#include "nfp_net_dp.h"
#include "nfp_net_xsk.h"

/**
 * nfp_net_rx_alloc_one() - Allocate and map page frag for RX
 * @dp:		NFP Net data path struct
 * @dma_addr:	Pointer to storage for DMA address (output param)
 *
 * This function will allcate a new page frag, map it for DMA.
 *
 * Return: allocated page frag or NULL on failure.
 */
void *nfp_net_rx_alloc_one(struct nfp_net_dp *dp, dma_addr_t *dma_addr)
{
	void *frag;

	if (!dp->xdp_prog) {
		frag = netdev_alloc_frag(dp->fl_bufsz);
	} else {
		struct page *page;

		page = alloc_page(GFP_KERNEL);
		frag = page ? page_address(page) : NULL;
	}
	if (!frag) {
		nn_dp_warn(dp, "Failed to alloc receive page frag\n");
		return NULL;
	}

	*dma_addr = nfp_net_dma_map_rx(dp, frag);
	if (dma_mapping_error(dp->dev, *dma_addr)) {
		nfp_net_free_frag(frag, dp->xdp_prog);
		nn_dp_warn(dp, "Failed to map DMA RX buffer\n");
		return NULL;
	}

	return frag;
}

/**
 * nfp_net_tx_ring_init() - Fill in the boilerplate for a TX ring
 * @tx_ring:  TX ring structure
 * @dp:	      NFP Net data path struct
 * @r_vec:    IRQ vector servicing this ring
 * @idx:      Ring index
 * @is_xdp:   Is this an XDP TX ring?
 */
static void
nfp_net_tx_ring_init(struct nfp_net_tx_ring *tx_ring, struct nfp_net_dp *dp,
		     struct nfp_net_r_vector *r_vec, unsigned int idx,
		     bool is_xdp)
{
	struct nfp_net *nn = r_vec->nfp_net;

	tx_ring->idx = idx;
	tx_ring->r_vec = r_vec;
	tx_ring->is_xdp = is_xdp;
	u64_stats_init(&tx_ring->r_vec->tx_sync);

	tx_ring->qcidx = tx_ring->idx * nn->stride_tx;
	tx_ring->txrwb = dp->txrwb ? &dp->txrwb[idx] : NULL;
	tx_ring->qcp_q = nn->tx_bar + NFP_QCP_QUEUE_OFF(tx_ring->qcidx);
}

/**
 * nfp_net_rx_ring_init() - Fill in the boilerplate for a RX ring
 * @rx_ring:  RX ring structure
 * @r_vec:    IRQ vector servicing this ring
 * @idx:      Ring index
 */
static void
nfp_net_rx_ring_init(struct nfp_net_rx_ring *rx_ring,
		     struct nfp_net_r_vector *r_vec, unsigned int idx)
{
	struct nfp_net *nn = r_vec->nfp_net;

	rx_ring->idx = idx;
	rx_ring->r_vec = r_vec;
	u64_stats_init(&rx_ring->r_vec->rx_sync);

	rx_ring->fl_qcidx = rx_ring->idx * nn->stride_rx;
	rx_ring->qcp_fl = nn->rx_bar + NFP_QCP_QUEUE_OFF(rx_ring->fl_qcidx);
}

/**
 * nfp_net_rx_ring_reset() - Reflect in SW state of freelist after disable
 * @rx_ring:	RX ring structure
 *
 * Assumes that the device is stopped, must be idempotent.
 */
void nfp_net_rx_ring_reset(struct nfp_net_rx_ring *rx_ring)
{
	unsigned int wr_idx, last_idx;

	/* wr_p == rd_p means ring was never fed FL bufs.  RX rings are always
	 * kept at cnt - 1 FL bufs.
	 */
	if (rx_ring->wr_p == 0 && rx_ring->rd_p == 0)
		return;

	/* Move the empty entry to the end of the list */
	wr_idx = D_IDX(rx_ring, rx_ring->wr_p);
	last_idx = rx_ring->cnt - 1;
	if (rx_ring->r_vec->xsk_pool) {
		rx_ring->xsk_rxbufs[wr_idx] = rx_ring->xsk_rxbufs[last_idx];
		memset(&rx_ring->xsk_rxbufs[last_idx], 0,
		       sizeof(*rx_ring->xsk_rxbufs));
	} else {
		rx_ring->rxbufs[wr_idx] = rx_ring->rxbufs[last_idx];
		memset(&rx_ring->rxbufs[last_idx], 0, sizeof(*rx_ring->rxbufs));
	}

	memset(rx_ring->rxds, 0, rx_ring->size);
	rx_ring->wr_p = 0;
	rx_ring->rd_p = 0;
}

/**
 * nfp_net_rx_ring_bufs_free() - Free any buffers currently on the RX ring
 * @dp:		NFP Net data path struct
 * @rx_ring:	RX ring to remove buffers from
 *
 * Assumes that the device is stopped and buffers are in [0, ring->cnt - 1)
 * entries.  After device is disabled nfp_net_rx_ring_reset() must be called
 * to restore required ring geometry.
 */
static void
nfp_net_rx_ring_bufs_free(struct nfp_net_dp *dp,
			  struct nfp_net_rx_ring *rx_ring)
{
	unsigned int i;

	if (nfp_net_has_xsk_pool_slow(dp, rx_ring->idx))
		return;

	for (i = 0; i < rx_ring->cnt - 1; i++) {
		/* NULL skb can only happen when initial filling of the ring
		 * fails to allocate enough buffers and calls here to free
		 * already allocated ones.
		 */
		if (!rx_ring->rxbufs[i].frag)
			continue;

		nfp_net_dma_unmap_rx(dp, rx_ring->rxbufs[i].dma_addr);
		nfp_net_free_frag(rx_ring->rxbufs[i].frag, dp->xdp_prog);
		rx_ring->rxbufs[i].dma_addr = 0;
		rx_ring->rxbufs[i].frag = NULL;
	}
}

/**
 * nfp_net_rx_ring_bufs_alloc() - Fill RX ring with buffers (don't give to FW)
 * @dp:		NFP Net data path struct
 * @rx_ring:	RX ring to remove buffers from
 */
static int
nfp_net_rx_ring_bufs_alloc(struct nfp_net_dp *dp,
			   struct nfp_net_rx_ring *rx_ring)
{
	struct nfp_net_rx_buf *rxbufs;
	unsigned int i;

	if (nfp_net_has_xsk_pool_slow(dp, rx_ring->idx))
		return 0;

	rxbufs = rx_ring->rxbufs;

	for (i = 0; i < rx_ring->cnt - 1; i++) {
		rxbufs[i].frag = nfp_net_rx_alloc_one(dp, &rxbufs[i].dma_addr);
		if (!rxbufs[i].frag) {
			nfp_net_rx_ring_bufs_free(dp, rx_ring);
			return -ENOMEM;
		}
	}

	return 0;
}

int nfp_net_tx_rings_prepare(struct nfp_net *nn, struct nfp_net_dp *dp)
{
	unsigned int r;

	dp->tx_rings = kcalloc(dp->num_tx_rings, sizeof(*dp->tx_rings),
			       GFP_KERNEL);
	if (!dp->tx_rings)
		return -ENOMEM;

	if (dp->ctrl & NFP_NET_CFG_CTRL_TXRWB) {
		dp->txrwb = dma_alloc_coherent(dp->dev,
					       dp->num_tx_rings * sizeof(u64),
					       &dp->txrwb_dma, GFP_KERNEL);
		if (!dp->txrwb)
			goto err_free_rings;
	}

	for (r = 0; r < dp->num_tx_rings; r++) {
		int bias = 0;

		if (r >= dp->num_stack_tx_rings)
			bias = dp->num_stack_tx_rings;

		nfp_net_tx_ring_init(&dp->tx_rings[r], dp,
				     &nn->r_vecs[r - bias], r, bias);

		if (nfp_net_tx_ring_alloc(dp, &dp->tx_rings[r]))
			goto err_free_prev;

		if (nfp_net_tx_ring_bufs_alloc(dp, &dp->tx_rings[r]))
			goto err_free_ring;
	}

	return 0;

err_free_prev:
	while (r--) {
		nfp_net_tx_ring_bufs_free(dp, &dp->tx_rings[r]);
err_free_ring:
		nfp_net_tx_ring_free(dp, &dp->tx_rings[r]);
	}
	if (dp->txrwb)
		dma_free_coherent(dp->dev, dp->num_tx_rings * sizeof(u64),
				  dp->txrwb, dp->txrwb_dma);
err_free_rings:
	kfree(dp->tx_rings);
	return -ENOMEM;
}

void nfp_net_tx_rings_free(struct nfp_net_dp *dp)
{
	unsigned int r;

	for (r = 0; r < dp->num_tx_rings; r++) {
		nfp_net_tx_ring_bufs_free(dp, &dp->tx_rings[r]);
		nfp_net_tx_ring_free(dp, &dp->tx_rings[r]);
	}

	if (dp->txrwb)
		dma_free_coherent(dp->dev, dp->num_tx_rings * sizeof(u64),
				  dp->txrwb, dp->txrwb_dma);
	kfree(dp->tx_rings);
}

/**
 * nfp_net_rx_ring_free() - Free resources allocated to a RX ring
 * @rx_ring:  RX ring to free
 */
static void nfp_net_rx_ring_free(struct nfp_net_rx_ring *rx_ring)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct nfp_net_dp *dp = &r_vec->nfp_net->dp;

	if (dp->netdev)
		xdp_rxq_info_unreg(&rx_ring->xdp_rxq);

	if (nfp_net_has_xsk_pool_slow(dp, rx_ring->idx))
		kvfree(rx_ring->xsk_rxbufs);
	else
		kvfree(rx_ring->rxbufs);

	if (rx_ring->rxds)
		dma_free_coherent(dp->dev, rx_ring->size,
				  rx_ring->rxds, rx_ring->dma);

	rx_ring->cnt = 0;
	rx_ring->rxbufs = NULL;
	rx_ring->xsk_rxbufs = NULL;
	rx_ring->rxds = NULL;
	rx_ring->dma = 0;
	rx_ring->size = 0;
}

/**
 * nfp_net_rx_ring_alloc() - Allocate resource for a RX ring
 * @dp:	      NFP Net data path struct
 * @rx_ring:  RX ring to allocate
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int
nfp_net_rx_ring_alloc(struct nfp_net_dp *dp, struct nfp_net_rx_ring *rx_ring)
{
	enum xdp_mem_type mem_type;
	size_t rxbuf_sw_desc_sz;
	int err;

	if (nfp_net_has_xsk_pool_slow(dp, rx_ring->idx)) {
		mem_type = MEM_TYPE_XSK_BUFF_POOL;
		rxbuf_sw_desc_sz = sizeof(*rx_ring->xsk_rxbufs);
	} else {
		mem_type = MEM_TYPE_PAGE_ORDER0;
		rxbuf_sw_desc_sz = sizeof(*rx_ring->rxbufs);
	}

	if (dp->netdev) {
		err = xdp_rxq_info_reg(&rx_ring->xdp_rxq, dp->netdev,
				       rx_ring->idx, rx_ring->r_vec->napi.napi_id);
		if (err < 0)
			return err;

		err = xdp_rxq_info_reg_mem_model(&rx_ring->xdp_rxq, mem_type, NULL);
		if (err)
			goto err_alloc;
	}

	rx_ring->cnt = dp->rxd_cnt;
	rx_ring->size = array_size(rx_ring->cnt, sizeof(*rx_ring->rxds));
	rx_ring->rxds = dma_alloc_coherent(dp->dev, rx_ring->size,
					   &rx_ring->dma,
					   GFP_KERNEL | __GFP_NOWARN);
	if (!rx_ring->rxds) {
		netdev_warn(dp->netdev, "failed to allocate RX descriptor ring memory, requested descriptor count: %d, consider lowering descriptor count\n",
			    rx_ring->cnt);
		goto err_alloc;
	}

	if (nfp_net_has_xsk_pool_slow(dp, rx_ring->idx)) {
		rx_ring->xsk_rxbufs = kvcalloc(rx_ring->cnt, rxbuf_sw_desc_sz,
					       GFP_KERNEL);
		if (!rx_ring->xsk_rxbufs)
			goto err_alloc;
	} else {
		rx_ring->rxbufs = kvcalloc(rx_ring->cnt, rxbuf_sw_desc_sz,
					   GFP_KERNEL);
		if (!rx_ring->rxbufs)
			goto err_alloc;
	}

	return 0;

err_alloc:
	nfp_net_rx_ring_free(rx_ring);
	return -ENOMEM;
}

int nfp_net_rx_rings_prepare(struct nfp_net *nn, struct nfp_net_dp *dp)
{
	unsigned int r;

	dp->rx_rings = kcalloc(dp->num_rx_rings, sizeof(*dp->rx_rings),
			       GFP_KERNEL);
	if (!dp->rx_rings)
		return -ENOMEM;

	for (r = 0; r < dp->num_rx_rings; r++) {
		nfp_net_rx_ring_init(&dp->rx_rings[r], &nn->r_vecs[r], r);

		if (nfp_net_rx_ring_alloc(dp, &dp->rx_rings[r]))
			goto err_free_prev;

		if (nfp_net_rx_ring_bufs_alloc(dp, &dp->rx_rings[r]))
			goto err_free_ring;
	}

	return 0;

err_free_prev:
	while (r--) {
		nfp_net_rx_ring_bufs_free(dp, &dp->rx_rings[r]);
err_free_ring:
		nfp_net_rx_ring_free(&dp->rx_rings[r]);
	}
	kfree(dp->rx_rings);
	return -ENOMEM;
}

void nfp_net_rx_rings_free(struct nfp_net_dp *dp)
{
	unsigned int r;

	for (r = 0; r < dp->num_rx_rings; r++) {
		nfp_net_rx_ring_bufs_free(dp, &dp->rx_rings[r]);
		nfp_net_rx_ring_free(&dp->rx_rings[r]);
	}

	kfree(dp->rx_rings);
}

void
nfp_net_rx_ring_hw_cfg_write(struct nfp_net *nn,
			     struct nfp_net_rx_ring *rx_ring, unsigned int idx)
{
	/* Write the DMA address, size and MSI-X info to the device */
	nn_writeq(nn, NFP_NET_CFG_RXR_ADDR(idx), rx_ring->dma);
	nn_writeb(nn, NFP_NET_CFG_RXR_SZ(idx), ilog2(rx_ring->cnt));
	nn_writeb(nn, NFP_NET_CFG_RXR_VEC(idx), rx_ring->r_vec->irq_entry);
}

void
nfp_net_tx_ring_hw_cfg_write(struct nfp_net *nn,
			     struct nfp_net_tx_ring *tx_ring, unsigned int idx)
{
	nn_writeq(nn, NFP_NET_CFG_TXR_ADDR(idx), tx_ring->dma);
	if (tx_ring->txrwb) {
		*tx_ring->txrwb = 0;
		nn_writeq(nn, NFP_NET_CFG_TXR_WB_ADDR(idx),
			  nn->dp.txrwb_dma + idx * sizeof(u64));
	}
	nn_writeb(nn, NFP_NET_CFG_TXR_SZ(idx), ilog2(tx_ring->cnt));
	nn_writeb(nn, NFP_NET_CFG_TXR_VEC(idx), tx_ring->r_vec->irq_entry);
}

void nfp_net_vec_clear_ring_data(struct nfp_net *nn, unsigned int idx)
{
	nn_writeq(nn, NFP_NET_CFG_RXR_ADDR(idx), 0);
	nn_writeb(nn, NFP_NET_CFG_RXR_SZ(idx), 0);
	nn_writeb(nn, NFP_NET_CFG_RXR_VEC(idx), 0);

	nn_writeq(nn, NFP_NET_CFG_TXR_ADDR(idx), 0);
	nn_writeq(nn, NFP_NET_CFG_TXR_WB_ADDR(idx), 0);
	nn_writeb(nn, NFP_NET_CFG_TXR_SZ(idx), 0);
	nn_writeb(nn, NFP_NET_CFG_TXR_VEC(idx), 0);
}

netdev_tx_t nfp_net_tx(struct sk_buff *skb, struct net_device *netdev)
{
	struct nfp_net *nn = netdev_priv(netdev);

	return nn->dp.ops->xmit(skb, netdev);
}

bool __nfp_ctrl_tx(struct nfp_net *nn, struct sk_buff *skb)
{
	struct nfp_net_r_vector *r_vec = &nn->r_vecs[0];

	return nn->dp.ops->ctrl_tx_one(nn, r_vec, skb, false);
}

bool nfp_ctrl_tx(struct nfp_net *nn, struct sk_buff *skb)
{
	struct nfp_net_r_vector *r_vec = &nn->r_vecs[0];
	bool ret;

	spin_lock_bh(&r_vec->lock);
	ret = nn->dp.ops->ctrl_tx_one(nn, r_vec, skb, false);
	spin_unlock_bh(&r_vec->lock);

	return ret;
}
