// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc */
/* Copyright (C) 2021 Corigine, Inc */

#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <net/xdp_sock_drv.h>
#include <trace/events/xdp.h>

#include "nfp_app.h"
#include "nfp_net.h"
#include "nfp_net_dp.h"
#include "nfp_net_xsk.h"

static void
nfp_net_xsk_rx_bufs_stash(struct nfp_net_rx_ring *rx_ring, unsigned int idx,
			  struct xdp_buff *xdp)
{
	unsigned int headroom;

	headroom = xsk_pool_get_headroom(rx_ring->r_vec->xsk_pool);

	rx_ring->rxds[idx].fld.reserved = 0;
	rx_ring->rxds[idx].fld.meta_len_dd = 0;

	rx_ring->xsk_rxbufs[idx].xdp = xdp;
	rx_ring->xsk_rxbufs[idx].dma_addr =
		xsk_buff_xdp_get_frame_dma(xdp) + headroom;
}

void nfp_net_xsk_rx_unstash(struct nfp_net_xsk_rx_buf *rxbuf)
{
	rxbuf->dma_addr = 0;
	rxbuf->xdp = NULL;
}

void nfp_net_xsk_rx_free(struct nfp_net_xsk_rx_buf *rxbuf)
{
	if (rxbuf->xdp)
		xsk_buff_free(rxbuf->xdp);

	nfp_net_xsk_rx_unstash(rxbuf);
}

void nfp_net_xsk_rx_bufs_free(struct nfp_net_rx_ring *rx_ring)
{
	unsigned int i;

	if (!rx_ring->cnt)
		return;

	for (i = 0; i < rx_ring->cnt - 1; i++)
		nfp_net_xsk_rx_free(&rx_ring->xsk_rxbufs[i]);
}

void nfp_net_xsk_rx_ring_fill_freelist(struct nfp_net_rx_ring *rx_ring)
{
	struct nfp_net_r_vector *r_vec = rx_ring->r_vec;
	struct xsk_buff_pool *pool = r_vec->xsk_pool;
	unsigned int wr_idx, wr_ptr_add = 0;
	struct xdp_buff *xdp;

	while (nfp_net_rx_space(rx_ring)) {
		wr_idx = D_IDX(rx_ring, rx_ring->wr_p);

		xdp = xsk_buff_alloc(pool);
		if (!xdp)
			break;

		nfp_net_xsk_rx_bufs_stash(rx_ring, wr_idx, xdp);

		nfp_desc_set_dma_addr(&rx_ring->rxds[wr_idx].fld,
				      rx_ring->xsk_rxbufs[wr_idx].dma_addr);

		rx_ring->wr_p++;
		wr_ptr_add++;
	}

	/* Ensure all records are visible before incrementing write counter. */
	wmb();
	nfp_qcp_wr_ptr_add(rx_ring->qcp_fl, wr_ptr_add);
}

void nfp_net_xsk_rx_drop(struct nfp_net_r_vector *r_vec,
			 struct nfp_net_xsk_rx_buf *xrxbuf)
{
	u64_stats_update_begin(&r_vec->rx_sync);
	r_vec->rx_drops++;
	u64_stats_update_end(&r_vec->rx_sync);

	nfp_net_xsk_rx_free(xrxbuf);
}

static void nfp_net_xsk_pool_unmap(struct device *dev,
				   struct xsk_buff_pool *pool)
{
	return xsk_pool_dma_unmap(pool, 0);
}

static int nfp_net_xsk_pool_map(struct device *dev, struct xsk_buff_pool *pool)
{
	return xsk_pool_dma_map(pool, dev, 0);
}

int nfp_net_xsk_setup_pool(struct net_device *netdev,
			   struct xsk_buff_pool *pool, u16 queue_id)
{
	struct nfp_net *nn = netdev_priv(netdev);

	struct xsk_buff_pool *prev_pool;
	struct nfp_net_dp *dp;
	int err;

	/* NFDK doesn't implement xsk yet. */
	if (nn->dp.ops->version == NFP_NFD_VER_NFDK)
		return -EOPNOTSUPP;

	/* Reject on old FWs so we can drop some checks on datapath. */
	if (nn->dp.rx_offset != NFP_NET_CFG_RX_OFFSET_DYNAMIC)
		return -EOPNOTSUPP;
	if (!nn->dp.chained_metadata_format)
		return -EOPNOTSUPP;

	/* Install */
	if (pool) {
		err = nfp_net_xsk_pool_map(nn->dp.dev, pool);
		if (err)
			return err;
	}

	/* Reconfig/swap */
	dp = nfp_net_clone_dp(nn);
	if (!dp) {
		err = -ENOMEM;
		goto err_unmap;
	}

	prev_pool = dp->xsk_pools[queue_id];
	dp->xsk_pools[queue_id] = pool;

	err = nfp_net_ring_reconfig(nn, dp, NULL);
	if (err)
		goto err_unmap;

	/* Uninstall */
	if (prev_pool)
		nfp_net_xsk_pool_unmap(nn->dp.dev, prev_pool);

	return 0;
err_unmap:
	if (pool)
		nfp_net_xsk_pool_unmap(nn->dp.dev, pool);

	return err;
}

int nfp_net_xsk_wakeup(struct net_device *netdev, u32 queue_id, u32 flags)
{
	struct nfp_net *nn = netdev_priv(netdev);

	/* queue_id comes from a zero-copy socket, installed with XDP_SETUP_XSK_POOL,
	 * so it must be within our vector range.  Moreover, our napi structs
	 * are statically allocated, so we can always kick them without worrying
	 * if reconfig is in progress or interface down.
	 */
	napi_schedule(&nn->r_vecs[queue_id].napi);

	return 0;
}
