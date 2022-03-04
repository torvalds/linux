/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2018 Netronome Systems, Inc */
/* Copyright (C) 2021 Corigine, Inc */

#ifndef _NFP_XSK_H_
#define _NFP_XSK_H_

#include <net/xdp_sock_drv.h>

#define NFP_NET_XSK_TX_BATCH 16		/* XSK TX transmission batch size. */

static inline bool nfp_net_has_xsk_pool_slow(struct nfp_net_dp *dp,
					     unsigned int qid)
{
	return dp->xdp_prog && dp->xsk_pools[qid];
}

int nfp_net_xsk_setup_pool(struct net_device *netdev, struct xsk_buff_pool *pool,
			   u16 queue_id);

void nfp_net_xsk_tx_bufs_free(struct nfp_net_tx_ring *tx_ring);
void nfp_net_xsk_rx_bufs_free(struct nfp_net_rx_ring *rx_ring);

void nfp_net_xsk_rx_ring_fill_freelist(struct nfp_net_rx_ring *rx_ring);

int nfp_net_xsk_wakeup(struct net_device *netdev, u32 queue_id, u32 flags);
int nfp_net_xsk_poll(struct napi_struct *napi, int budget);

#endif /* _NFP_XSK_H_ */
