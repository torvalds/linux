/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_XSK_H_
#define _ICE_XSK_H_
#include "ice_txrx.h"

#define PKTS_PER_BATCH 8

#ifdef __clang__
#define loop_unrolled_for _Pragma("clang loop unroll_count(8)") for
#elif __GNUC__ >= 8
#define loop_unrolled_for _Pragma("GCC unroll 8") for
#else
#define loop_unrolled_for for
#endif

struct ice_vsi;

#ifdef CONFIG_XDP_SOCKETS
int ice_xsk_pool_setup(struct ice_vsi *vsi, struct xsk_buff_pool *pool,
		       u16 qid);
int ice_clean_rx_irq_zc(struct ice_rx_ring *rx_ring, int budget);
int ice_xsk_wakeup(struct net_device *netdev, u32 queue_id, u32 flags);
bool ice_alloc_rx_bufs_zc(struct ice_rx_ring *rx_ring, u16 count);
bool ice_xsk_any_rx_ring_ena(struct ice_vsi *vsi);
void ice_xsk_clean_rx_ring(struct ice_rx_ring *rx_ring);
void ice_xsk_clean_xdp_ring(struct ice_tx_ring *xdp_ring);
bool ice_xmit_zc(struct ice_tx_ring *xdp_ring);
int ice_realloc_zc_buf(struct ice_vsi *vsi, bool zc);
#else
static inline bool ice_xmit_zc(struct ice_tx_ring __always_unused *xdp_ring)
{
	return false;
}

static inline int
ice_xsk_pool_setup(struct ice_vsi __always_unused *vsi,
		   struct xsk_buff_pool __always_unused *pool,
		   u16 __always_unused qid)
{
	return -EOPNOTSUPP;
}

static inline int
ice_clean_rx_irq_zc(struct ice_rx_ring __always_unused *rx_ring,
		    int __always_unused budget)
{
	return 0;
}

static inline bool
ice_alloc_rx_bufs_zc(struct ice_rx_ring __always_unused *rx_ring,
		     u16 __always_unused count)
{
	return false;
}

static inline bool ice_xsk_any_rx_ring_ena(struct ice_vsi __always_unused *vsi)
{
	return false;
}

static inline int
ice_xsk_wakeup(struct net_device __always_unused *netdev,
	       u32 __always_unused queue_id, u32 __always_unused flags)
{
	return -EOPNOTSUPP;
}

static inline void ice_xsk_clean_rx_ring(struct ice_rx_ring *rx_ring) { }
static inline void ice_xsk_clean_xdp_ring(struct ice_tx_ring *xdp_ring) { }

static inline int
ice_realloc_zc_buf(struct ice_vsi __always_unused *vsi,
		   bool __always_unused zc)
{
	return 0;
}
#endif /* CONFIG_XDP_SOCKETS */
#endif /* !_ICE_XSK_H_ */
