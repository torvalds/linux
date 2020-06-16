/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_XSK_H_
#define _ICE_XSK_H_
#include "ice_txrx.h"
#include "ice.h"

struct ice_vsi;

#ifdef CONFIG_XDP_SOCKETS
int ice_xsk_umem_setup(struct ice_vsi *vsi, struct xdp_umem *umem, u16 qid);
int ice_clean_rx_irq_zc(struct ice_ring *rx_ring, int budget);
bool ice_clean_tx_irq_zc(struct ice_ring *xdp_ring, int budget);
int ice_xsk_wakeup(struct net_device *netdev, u32 queue_id, u32 flags);
bool ice_alloc_rx_bufs_zc(struct ice_ring *rx_ring, u16 count);
bool ice_xsk_any_rx_ring_ena(struct ice_vsi *vsi);
void ice_xsk_clean_rx_ring(struct ice_ring *rx_ring);
void ice_xsk_clean_xdp_ring(struct ice_ring *xdp_ring);
#else
static inline int
ice_xsk_umem_setup(struct ice_vsi __always_unused *vsi,
		   struct xdp_umem __always_unused *umem,
		   u16 __always_unused qid)
{
	return -EOPNOTSUPP;
}

static inline int
ice_clean_rx_irq_zc(struct ice_ring __always_unused *rx_ring,
		    int __always_unused budget)
{
	return 0;
}

static inline bool
ice_clean_tx_irq_zc(struct ice_ring __always_unused *xdp_ring,
		    int __always_unused budget)
{
	return false;
}

static inline bool
ice_alloc_rx_bufs_zc(struct ice_ring __always_unused *rx_ring,
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

#define ice_xsk_clean_rx_ring(rx_ring) do {} while (0)
#define ice_xsk_clean_xdp_ring(xdp_ring) do {} while (0)
#endif /* CONFIG_XDP_SOCKETS */
#endif /* !_ICE_XSK_H_ */
