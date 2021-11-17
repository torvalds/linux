// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 1999 - 2018 Intel Corporation. */

#include "ixgbe.h"
#include "ixgbe_sriov.h"

#ifdef CONFIG_IXGBE_DCB
/**
 * ixgbe_cache_ring_dcb_sriov - Descriptor ring to register mapping for SR-IOV
 * @adapter: board private structure to initialize
 *
 * Cache the descriptor ring offsets for SR-IOV to the assigned rings.  It
 * will also try to cache the proper offsets if RSS/FCoE are enabled along
 * with VMDq.
 *
 **/
static bool ixgbe_cache_ring_dcb_sriov(struct ixgbe_adapter *adapter)
{
#ifdef IXGBE_FCOE
	struct ixgbe_ring_feature *fcoe = &adapter->ring_feature[RING_F_FCOE];
#endif /* IXGBE_FCOE */
	struct ixgbe_ring_feature *vmdq = &adapter->ring_feature[RING_F_VMDQ];
	int i;
	u16 reg_idx, pool;
	u8 tcs = adapter->hw_tcs;

	/* verify we have DCB queueing enabled before proceeding */
	if (tcs <= 1)
		return false;

	/* verify we have VMDq enabled before proceeding */
	if (!(adapter->flags & IXGBE_FLAG_SRIOV_ENABLED))
		return false;

	/* start at VMDq register offset for SR-IOV enabled setups */
	reg_idx = vmdq->offset * __ALIGN_MASK(1, ~vmdq->mask);
	for (i = 0, pool = 0; i < adapter->num_rx_queues; i++, reg_idx++) {
		/* If we are greater than indices move to next pool */
		if ((reg_idx & ~vmdq->mask) >= tcs) {
			pool++;
			reg_idx = __ALIGN_MASK(reg_idx, ~vmdq->mask);
		}
		adapter->rx_ring[i]->reg_idx = reg_idx;
		adapter->rx_ring[i]->netdev = pool ? NULL : adapter->netdev;
	}

	reg_idx = vmdq->offset * __ALIGN_MASK(1, ~vmdq->mask);
	for (i = 0; i < adapter->num_tx_queues; i++, reg_idx++) {
		/* If we are greater than indices move to next pool */
		if ((reg_idx & ~vmdq->mask) >= tcs)
			reg_idx = __ALIGN_MASK(reg_idx, ~vmdq->mask);
		adapter->tx_ring[i]->reg_idx = reg_idx;
	}

#ifdef IXGBE_FCOE
	/* nothing to do if FCoE is disabled */
	if (!(adapter->flags & IXGBE_FLAG_FCOE_ENABLED))
		return true;

	/* The work is already done if the FCoE ring is shared */
	if (fcoe->offset < tcs)
		return true;

	/* The FCoE rings exist separately, we need to move their reg_idx */
	if (fcoe->indices) {
		u16 queues_per_pool = __ALIGN_MASK(1, ~vmdq->mask);
		u8 fcoe_tc = ixgbe_fcoe_get_tc(adapter);

		reg_idx = (vmdq->offset + vmdq->indices) * queues_per_pool;
		for (i = fcoe->offset; i < adapter->num_rx_queues; i++) {
			reg_idx = __ALIGN_MASK(reg_idx, ~vmdq->mask) + fcoe_tc;
			adapter->rx_ring[i]->reg_idx = reg_idx;
			adapter->rx_ring[i]->netdev = adapter->netdev;
			reg_idx++;
		}

		reg_idx = (vmdq->offset + vmdq->indices) * queues_per_pool;
		for (i = fcoe->offset; i < adapter->num_tx_queues; i++) {
			reg_idx = __ALIGN_MASK(reg_idx, ~vmdq->mask) + fcoe_tc;
			adapter->tx_ring[i]->reg_idx = reg_idx;
			reg_idx++;
		}
	}

#endif /* IXGBE_FCOE */
	return true;
}

/* ixgbe_get_first_reg_idx - Return first register index associated with ring */
static void ixgbe_get_first_reg_idx(struct ixgbe_adapter *adapter, u8 tc,
				    unsigned int *tx, unsigned int *rx)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u8 num_tcs = adapter->hw_tcs;

	*tx = 0;
	*rx = 0;

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		/* TxQs/TC: 4	RxQs/TC: 8 */
		*tx = tc << 2; /* 0, 4,  8, 12, 16, 20, 24, 28 */
		*rx = tc << 3; /* 0, 8, 16, 24, 32, 40, 48, 56 */
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_x550em_a:
		if (num_tcs > 4) {
			/*
			 * TCs    : TC0/1 TC2/3 TC4-7
			 * TxQs/TC:    32    16     8
			 * RxQs/TC:    16    16    16
			 */
			*rx = tc << 4;
			if (tc < 3)
				*tx = tc << 5;		/*   0,  32,  64 */
			else if (tc < 5)
				*tx = (tc + 2) << 4;	/*  80,  96 */
			else
				*tx = (tc + 8) << 3;	/* 104, 112, 120 */
		} else {
			/*
			 * TCs    : TC0 TC1 TC2/3
			 * TxQs/TC:  64  32    16
			 * RxQs/TC:  32  32    32
			 */
			*rx = tc << 5;
			if (tc < 2)
				*tx = tc << 6;		/*  0,  64 */
			else
				*tx = (tc + 4) << 4;	/* 96, 112 */
		}
		break;
	default:
		break;
	}
}

/**
 * ixgbe_cache_ring_dcb - Descriptor ring to register mapping for DCB
 * @adapter: board private structure to initialize
 *
 * Cache the descriptor ring offsets for DCB to the assigned rings.
 *
 **/
static bool ixgbe_cache_ring_dcb(struct ixgbe_adapter *adapter)
{
	u8 num_tcs = adapter->hw_tcs;
	unsigned int tx_idx, rx_idx;
	int tc, offset, rss_i, i;

	/* verify we have DCB queueing enabled before proceeding */
	if (num_tcs <= 1)
		return false;

	rss_i = adapter->ring_feature[RING_F_RSS].indices;

	for (tc = 0, offset = 0; tc < num_tcs; tc++, offset += rss_i) {
		ixgbe_get_first_reg_idx(adapter, tc, &tx_idx, &rx_idx);
		for (i = 0; i < rss_i; i++, tx_idx++, rx_idx++) {
			adapter->tx_ring[offset + i]->reg_idx = tx_idx;
			adapter->rx_ring[offset + i]->reg_idx = rx_idx;
			adapter->rx_ring[offset + i]->netdev = adapter->netdev;
			adapter->tx_ring[offset + i]->dcb_tc = tc;
			adapter->rx_ring[offset + i]->dcb_tc = tc;
		}
	}

	return true;
}

#endif
/**
 * ixgbe_cache_ring_sriov - Descriptor ring to register mapping for sriov
 * @adapter: board private structure to initialize
 *
 * SR-IOV doesn't use any descriptor rings but changes the default if
 * no other mapping is used.
 *
 */
static bool ixgbe_cache_ring_sriov(struct ixgbe_adapter *adapter)
{
#ifdef IXGBE_FCOE
	struct ixgbe_ring_feature *fcoe = &adapter->ring_feature[RING_F_FCOE];
#endif /* IXGBE_FCOE */
	struct ixgbe_ring_feature *vmdq = &adapter->ring_feature[RING_F_VMDQ];
	struct ixgbe_ring_feature *rss = &adapter->ring_feature[RING_F_RSS];
	u16 reg_idx, pool;
	int i;

	/* only proceed if VMDq is enabled */
	if (!(adapter->flags & IXGBE_FLAG_VMDQ_ENABLED))
		return false;

	/* start at VMDq register offset for SR-IOV enabled setups */
	pool = 0;
	reg_idx = vmdq->offset * __ALIGN_MASK(1, ~vmdq->mask);
	for (i = 0; i < adapter->num_rx_queues; i++, reg_idx++) {
#ifdef IXGBE_FCOE
		/* Allow first FCoE queue to be mapped as RSS */
		if (fcoe->offset && (i > fcoe->offset))
			break;
#endif
		/* If we are greater than indices move to next pool */
		if ((reg_idx & ~vmdq->mask) >= rss->indices) {
			pool++;
			reg_idx = __ALIGN_MASK(reg_idx, ~vmdq->mask);
		}
		adapter->rx_ring[i]->reg_idx = reg_idx;
		adapter->rx_ring[i]->netdev = pool ? NULL : adapter->netdev;
	}

#ifdef IXGBE_FCOE
	/* FCoE uses a linear block of queues so just assigning 1:1 */
	for (; i < adapter->num_rx_queues; i++, reg_idx++) {
		adapter->rx_ring[i]->reg_idx = reg_idx;
		adapter->rx_ring[i]->netdev = adapter->netdev;
	}

#endif
	reg_idx = vmdq->offset * __ALIGN_MASK(1, ~vmdq->mask);
	for (i = 0; i < adapter->num_tx_queues; i++, reg_idx++) {
#ifdef IXGBE_FCOE
		/* Allow first FCoE queue to be mapped as RSS */
		if (fcoe->offset && (i > fcoe->offset))
			break;
#endif
		/* If we are greater than indices move to next pool */
		if ((reg_idx & rss->mask) >= rss->indices)
			reg_idx = __ALIGN_MASK(reg_idx, ~vmdq->mask);
		adapter->tx_ring[i]->reg_idx = reg_idx;
	}

#ifdef IXGBE_FCOE
	/* FCoE uses a linear block of queues so just assigning 1:1 */
	for (; i < adapter->num_tx_queues; i++, reg_idx++)
		adapter->tx_ring[i]->reg_idx = reg_idx;

#endif

	return true;
}

/**
 * ixgbe_cache_ring_rss - Descriptor ring to register mapping for RSS
 * @adapter: board private structure to initialize
 *
 * Cache the descriptor ring offsets for RSS to the assigned rings.
 *
 **/
static bool ixgbe_cache_ring_rss(struct ixgbe_adapter *adapter)
{
	int i, reg_idx;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		adapter->rx_ring[i]->reg_idx = i;
		adapter->rx_ring[i]->netdev = adapter->netdev;
	}
	for (i = 0, reg_idx = 0; i < adapter->num_tx_queues; i++, reg_idx++)
		adapter->tx_ring[i]->reg_idx = reg_idx;
	for (i = 0; i < adapter->num_xdp_queues; i++, reg_idx++)
		adapter->xdp_ring[i]->reg_idx = reg_idx;

	return true;
}

/**
 * ixgbe_cache_ring_register - Descriptor ring to register mapping
 * @adapter: board private structure to initialize
 *
 * Once we know the feature-set enabled for the device, we'll cache
 * the register offset the descriptor ring is assigned to.
 *
 * Note, the order the various feature calls is important.  It must start with
 * the "most" features enabled at the same time, then trickle down to the
 * least amount of features turned on at once.
 **/
static void ixgbe_cache_ring_register(struct ixgbe_adapter *adapter)
{
	/* start with default case */
	adapter->rx_ring[0]->reg_idx = 0;
	adapter->tx_ring[0]->reg_idx = 0;

#ifdef CONFIG_IXGBE_DCB
	if (ixgbe_cache_ring_dcb_sriov(adapter))
		return;

	if (ixgbe_cache_ring_dcb(adapter))
		return;

#endif
	if (ixgbe_cache_ring_sriov(adapter))
		return;

	ixgbe_cache_ring_rss(adapter);
}

static int ixgbe_xdp_queues(struct ixgbe_adapter *adapter)
{
	return adapter->xdp_prog ? nr_cpu_ids : 0;
}

#define IXGBE_RSS_64Q_MASK	0x3F
#define IXGBE_RSS_16Q_MASK	0xF
#define IXGBE_RSS_8Q_MASK	0x7
#define IXGBE_RSS_4Q_MASK	0x3
#define IXGBE_RSS_2Q_MASK	0x1
#define IXGBE_RSS_DISABLED_MASK	0x0

#ifdef CONFIG_IXGBE_DCB
/**
 * ixgbe_set_dcb_sriov_queues: Allocate queues for SR-IOV devices w/ DCB
 * @adapter: board private structure to initialize
 *
 * When SR-IOV (Single Root IO Virtualiztion) is enabled, allocate queues
 * and VM pools where appropriate.  Also assign queues based on DCB
 * priorities and map accordingly..
 *
 **/
static bool ixgbe_set_dcb_sriov_queues(struct ixgbe_adapter *adapter)
{
	int i;
	u16 vmdq_i = adapter->ring_feature[RING_F_VMDQ].limit;
	u16 vmdq_m = 0;
#ifdef IXGBE_FCOE
	u16 fcoe_i = 0;
#endif
	u8 tcs = adapter->hw_tcs;

	/* verify we have DCB queueing enabled before proceeding */
	if (tcs <= 1)
		return false;

	/* verify we have VMDq enabled before proceeding */
	if (!(adapter->flags & IXGBE_FLAG_SRIOV_ENABLED))
		return false;

	/* limit VMDq instances on the PF by number of Tx queues */
	vmdq_i = min_t(u16, vmdq_i, MAX_TX_QUEUES / tcs);

	/* Add starting offset to total pool count */
	vmdq_i += adapter->ring_feature[RING_F_VMDQ].offset;

	/* 16 pools w/ 8 TC per pool */
	if (tcs > 4) {
		vmdq_i = min_t(u16, vmdq_i, 16);
		vmdq_m = IXGBE_82599_VMDQ_8Q_MASK;
	/* 32 pools w/ 4 TC per pool */
	} else {
		vmdq_i = min_t(u16, vmdq_i, 32);
		vmdq_m = IXGBE_82599_VMDQ_4Q_MASK;
	}

#ifdef IXGBE_FCOE
	/* queues in the remaining pools are available for FCoE */
	fcoe_i = (128 / __ALIGN_MASK(1, ~vmdq_m)) - vmdq_i;

#endif
	/* remove the starting offset from the pool count */
	vmdq_i -= adapter->ring_feature[RING_F_VMDQ].offset;

	/* save features for later use */
	adapter->ring_feature[RING_F_VMDQ].indices = vmdq_i;
	adapter->ring_feature[RING_F_VMDQ].mask = vmdq_m;

	/*
	 * We do not support DCB, VMDq, and RSS all simultaneously
	 * so we will disable RSS since it is the lowest priority
	 */
	adapter->ring_feature[RING_F_RSS].indices = 1;
	adapter->ring_feature[RING_F_RSS].mask = IXGBE_RSS_DISABLED_MASK;

	/* disable ATR as it is not supported when VMDq is enabled */
	adapter->flags &= ~IXGBE_FLAG_FDIR_HASH_CAPABLE;

	adapter->num_rx_pools = vmdq_i;
	adapter->num_rx_queues_per_pool = tcs;

	adapter->num_tx_queues = vmdq_i * tcs;
	adapter->num_xdp_queues = 0;
	adapter->num_rx_queues = vmdq_i * tcs;

#ifdef IXGBE_FCOE
	if (adapter->flags & IXGBE_FLAG_FCOE_ENABLED) {
		struct ixgbe_ring_feature *fcoe;

		fcoe = &adapter->ring_feature[RING_F_FCOE];

		/* limit ourselves based on feature limits */
		fcoe_i = min_t(u16, fcoe_i, fcoe->limit);

		if (fcoe_i) {
			/* alloc queues for FCoE separately */
			fcoe->indices = fcoe_i;
			fcoe->offset = vmdq_i * tcs;

			/* add queues to adapter */
			adapter->num_tx_queues += fcoe_i;
			adapter->num_rx_queues += fcoe_i;
		} else if (tcs > 1) {
			/* use queue belonging to FcoE TC */
			fcoe->indices = 1;
			fcoe->offset = ixgbe_fcoe_get_tc(adapter);
		} else {
			adapter->flags &= ~IXGBE_FLAG_FCOE_ENABLED;

			fcoe->indices = 0;
			fcoe->offset = 0;
		}
	}

#endif /* IXGBE_FCOE */
	/* configure TC to queue mapping */
	for (i = 0; i < tcs; i++)
		netdev_set_tc_queue(adapter->netdev, i, 1, i);

	return true;
}

static bool ixgbe_set_dcb_queues(struct ixgbe_adapter *adapter)
{
	struct net_device *dev = adapter->netdev;
	struct ixgbe_ring_feature *f;
	int rss_i, rss_m, i;
	int tcs;

	/* Map queue offset and counts onto allocated tx queues */
	tcs = adapter->hw_tcs;

	/* verify we have DCB queueing enabled before proceeding */
	if (tcs <= 1)
		return false;

	/* determine the upper limit for our current DCB mode */
	rss_i = dev->num_tx_queues / tcs;
	if (adapter->hw.mac.type == ixgbe_mac_82598EB) {
		/* 8 TC w/ 4 queues per TC */
		rss_i = min_t(u16, rss_i, 4);
		rss_m = IXGBE_RSS_4Q_MASK;
	} else if (tcs > 4) {
		/* 8 TC w/ 8 queues per TC */
		rss_i = min_t(u16, rss_i, 8);
		rss_m = IXGBE_RSS_8Q_MASK;
	} else {
		/* 4 TC w/ 16 queues per TC */
		rss_i = min_t(u16, rss_i, 16);
		rss_m = IXGBE_RSS_16Q_MASK;
	}

	/* set RSS mask and indices */
	f = &adapter->ring_feature[RING_F_RSS];
	rss_i = min_t(int, rss_i, f->limit);
	f->indices = rss_i;
	f->mask = rss_m;

	/* disable ATR as it is not supported when multiple TCs are enabled */
	adapter->flags &= ~IXGBE_FLAG_FDIR_HASH_CAPABLE;

#ifdef IXGBE_FCOE
	/* FCoE enabled queues require special configuration indexed
	 * by feature specific indices and offset. Here we map FCoE
	 * indices onto the DCB queue pairs allowing FCoE to own
	 * configuration later.
	 */
	if (adapter->flags & IXGBE_FLAG_FCOE_ENABLED) {
		u8 tc = ixgbe_fcoe_get_tc(adapter);

		f = &adapter->ring_feature[RING_F_FCOE];
		f->indices = min_t(u16, rss_i, f->limit);
		f->offset = rss_i * tc;
	}

#endif /* IXGBE_FCOE */
	for (i = 0; i < tcs; i++)
		netdev_set_tc_queue(dev, i, rss_i, rss_i * i);

	adapter->num_tx_queues = rss_i * tcs;
	adapter->num_xdp_queues = 0;
	adapter->num_rx_queues = rss_i * tcs;

	return true;
}

#endif
/**
 * ixgbe_set_sriov_queues - Allocate queues for SR-IOV devices
 * @adapter: board private structure to initialize
 *
 * When SR-IOV (Single Root IO Virtualiztion) is enabled, allocate queues
 * and VM pools where appropriate.  If RSS is available, then also try and
 * enable RSS and map accordingly.
 *
 **/
static bool ixgbe_set_sriov_queues(struct ixgbe_adapter *adapter)
{
	u16 vmdq_i = adapter->ring_feature[RING_F_VMDQ].limit;
	u16 vmdq_m = 0;
	u16 rss_i = adapter->ring_feature[RING_F_RSS].limit;
	u16 rss_m = IXGBE_RSS_DISABLED_MASK;
#ifdef IXGBE_FCOE
	u16 fcoe_i = 0;
#endif

	/* only proceed if SR-IOV is enabled */
	if (!(adapter->flags & IXGBE_FLAG_SRIOV_ENABLED))
		return false;

	/* limit l2fwd RSS based on total Tx queue limit */
	rss_i = min_t(u16, rss_i, MAX_TX_QUEUES / vmdq_i);

	/* Add starting offset to total pool count */
	vmdq_i += adapter->ring_feature[RING_F_VMDQ].offset;

	/* double check we are limited to maximum pools */
	vmdq_i = min_t(u16, IXGBE_MAX_VMDQ_INDICES, vmdq_i);

	/* 64 pool mode with 2 queues per pool */
	if (vmdq_i > 32) {
		vmdq_m = IXGBE_82599_VMDQ_2Q_MASK;
		rss_m = IXGBE_RSS_2Q_MASK;
		rss_i = min_t(u16, rss_i, 2);
	/* 32 pool mode with up to 4 queues per pool */
	} else {
		vmdq_m = IXGBE_82599_VMDQ_4Q_MASK;
		rss_m = IXGBE_RSS_4Q_MASK;
		/* We can support 4, 2, or 1 queues */
		rss_i = (rss_i > 3) ? 4 : (rss_i > 1) ? 2 : 1;
	}

#ifdef IXGBE_FCOE
	/* queues in the remaining pools are available for FCoE */
	fcoe_i = 128 - (vmdq_i * __ALIGN_MASK(1, ~vmdq_m));

#endif
	/* remove the starting offset from the pool count */
	vmdq_i -= adapter->ring_feature[RING_F_VMDQ].offset;

	/* save features for later use */
	adapter->ring_feature[RING_F_VMDQ].indices = vmdq_i;
	adapter->ring_feature[RING_F_VMDQ].mask = vmdq_m;

	/* limit RSS based on user input and save for later use */
	adapter->ring_feature[RING_F_RSS].indices = rss_i;
	adapter->ring_feature[RING_F_RSS].mask = rss_m;

	adapter->num_rx_pools = vmdq_i;
	adapter->num_rx_queues_per_pool = rss_i;

	adapter->num_rx_queues = vmdq_i * rss_i;
	adapter->num_tx_queues = vmdq_i * rss_i;
	adapter->num_xdp_queues = 0;

	/* disable ATR as it is not supported when VMDq is enabled */
	adapter->flags &= ~IXGBE_FLAG_FDIR_HASH_CAPABLE;

#ifdef IXGBE_FCOE
	/*
	 * FCoE can use rings from adjacent buffers to allow RSS
	 * like behavior.  To account for this we need to add the
	 * FCoE indices to the total ring count.
	 */
	if (adapter->flags & IXGBE_FLAG_FCOE_ENABLED) {
		struct ixgbe_ring_feature *fcoe;

		fcoe = &adapter->ring_feature[RING_F_FCOE];

		/* limit ourselves based on feature limits */
		fcoe_i = min_t(u16, fcoe_i, fcoe->limit);

		if (vmdq_i > 1 && fcoe_i) {
			/* alloc queues for FCoE separately */
			fcoe->indices = fcoe_i;
			fcoe->offset = vmdq_i * rss_i;
		} else {
			/* merge FCoE queues with RSS queues */
			fcoe_i = min_t(u16, fcoe_i + rss_i, num_online_cpus());

			/* limit indices to rss_i if MSI-X is disabled */
			if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED))
				fcoe_i = rss_i;

			/* attempt to reserve some queues for just FCoE */
			fcoe->indices = min_t(u16, fcoe_i, fcoe->limit);
			fcoe->offset = fcoe_i - fcoe->indices;

			fcoe_i -= rss_i;
		}

		/* add queues to adapter */
		adapter->num_tx_queues += fcoe_i;
		adapter->num_rx_queues += fcoe_i;
	}

#endif
	/* To support macvlan offload we have to use num_tc to
	 * restrict the queues that can be used by the device.
	 * By doing this we can avoid reporting a false number of
	 * queues.
	 */
	if (vmdq_i > 1)
		netdev_set_num_tc(adapter->netdev, 1);

	/* populate TC0 for use by pool 0 */
	netdev_set_tc_queue(adapter->netdev, 0,
			    adapter->num_rx_queues_per_pool, 0);

	return true;
}

/**
 * ixgbe_set_rss_queues - Allocate queues for RSS
 * @adapter: board private structure to initialize
 *
 * This is our "base" multiqueue mode.  RSS (Receive Side Scaling) will try
 * to allocate one Rx queue per CPU, and if available, one Tx queue per CPU.
 *
 **/
static bool ixgbe_set_rss_queues(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_ring_feature *f;
	u16 rss_i;

	/* set mask for 16 queue limit of RSS */
	f = &adapter->ring_feature[RING_F_RSS];
	rss_i = f->limit;

	f->indices = rss_i;

	if (hw->mac.type < ixgbe_mac_X550)
		f->mask = IXGBE_RSS_16Q_MASK;
	else
		f->mask = IXGBE_RSS_64Q_MASK;

	/* disable ATR by default, it will be configured below */
	adapter->flags &= ~IXGBE_FLAG_FDIR_HASH_CAPABLE;

	/*
	 * Use Flow Director in addition to RSS to ensure the best
	 * distribution of flows across cores, even when an FDIR flow
	 * isn't matched.
	 */
	if (rss_i > 1 && adapter->atr_sample_rate) {
		f = &adapter->ring_feature[RING_F_FDIR];

		rss_i = f->indices = f->limit;

		if (!(adapter->flags & IXGBE_FLAG_FDIR_PERFECT_CAPABLE))
			adapter->flags |= IXGBE_FLAG_FDIR_HASH_CAPABLE;
	}

#ifdef IXGBE_FCOE
	/*
	 * FCoE can exist on the same rings as standard network traffic
	 * however it is preferred to avoid that if possible.  In order
	 * to get the best performance we allocate as many FCoE queues
	 * as we can and we place them at the end of the ring array to
	 * avoid sharing queues with standard RSS on systems with 24 or
	 * more CPUs.
	 */
	if (adapter->flags & IXGBE_FLAG_FCOE_ENABLED) {
		struct net_device *dev = adapter->netdev;
		u16 fcoe_i;

		f = &adapter->ring_feature[RING_F_FCOE];

		/* merge FCoE queues with RSS queues */
		fcoe_i = min_t(u16, f->limit + rss_i, num_online_cpus());
		fcoe_i = min_t(u16, fcoe_i, dev->num_tx_queues);

		/* limit indices to rss_i if MSI-X is disabled */
		if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED))
			fcoe_i = rss_i;

		/* attempt to reserve some queues for just FCoE */
		f->indices = min_t(u16, fcoe_i, f->limit);
		f->offset = fcoe_i - f->indices;
		rss_i = max_t(u16, fcoe_i, rss_i);
	}

#endif /* IXGBE_FCOE */
	adapter->num_rx_queues = rss_i;
	adapter->num_tx_queues = rss_i;
	adapter->num_xdp_queues = ixgbe_xdp_queues(adapter);

	return true;
}

/**
 * ixgbe_set_num_queues - Allocate queues for device, feature dependent
 * @adapter: board private structure to initialize
 *
 * This is the top level queue allocation routine.  The order here is very
 * important, starting with the "most" number of features turned on at once,
 * and ending with the smallest set of features.  This way large combinations
 * can be allocated if they're turned on, and smaller combinations are the
 * fallthrough conditions.
 *
 **/
static void ixgbe_set_num_queues(struct ixgbe_adapter *adapter)
{
	/* Start with base case */
	adapter->num_rx_queues = 1;
	adapter->num_tx_queues = 1;
	adapter->num_xdp_queues = 0;
	adapter->num_rx_pools = 1;
	adapter->num_rx_queues_per_pool = 1;

#ifdef CONFIG_IXGBE_DCB
	if (ixgbe_set_dcb_sriov_queues(adapter))
		return;

	if (ixgbe_set_dcb_queues(adapter))
		return;

#endif
	if (ixgbe_set_sriov_queues(adapter))
		return;

	ixgbe_set_rss_queues(adapter);
}

/**
 * ixgbe_acquire_msix_vectors - acquire MSI-X vectors
 * @adapter: board private structure
 *
 * Attempts to acquire a suitable range of MSI-X vector interrupts. Will
 * return a negative error code if unable to acquire MSI-X vectors for any
 * reason.
 */
static int ixgbe_acquire_msix_vectors(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int i, vectors, vector_threshold;

	/* We start by asking for one vector per queue pair with XDP queues
	 * being stacked with TX queues.
	 */
	vectors = max(adapter->num_rx_queues, adapter->num_tx_queues);
	vectors = max(vectors, adapter->num_xdp_queues);

	/* It is easy to be greedy for MSI-X vectors. However, it really
	 * doesn't do much good if we have a lot more vectors than CPUs. We'll
	 * be somewhat conservative and only ask for (roughly) the same number
	 * of vectors as there are CPUs.
	 */
	vectors = min_t(int, vectors, num_online_cpus());

	/* Some vectors are necessary for non-queue interrupts */
	vectors += NON_Q_VECTORS;

	/* Hardware can only support a maximum of hw.mac->max_msix_vectors.
	 * With features such as RSS and VMDq, we can easily surpass the
	 * number of Rx and Tx descriptor queues supported by our device.
	 * Thus, we cap the maximum in the rare cases where the CPU count also
	 * exceeds our vector limit
	 */
	vectors = min_t(int, vectors, hw->mac.max_msix_vectors);

	/* We want a minimum of two MSI-X vectors for (1) a TxQ[0] + RxQ[0]
	 * handler, and (2) an Other (Link Status Change, etc.) handler.
	 */
	vector_threshold = MIN_MSIX_COUNT;

	adapter->msix_entries = kcalloc(vectors,
					sizeof(struct msix_entry),
					GFP_KERNEL);
	if (!adapter->msix_entries)
		return -ENOMEM;

	for (i = 0; i < vectors; i++)
		adapter->msix_entries[i].entry = i;

	vectors = pci_enable_msix_range(adapter->pdev, adapter->msix_entries,
					vector_threshold, vectors);

	if (vectors < 0) {
		/* A negative count of allocated vectors indicates an error in
		 * acquiring within the specified range of MSI-X vectors
		 */
		e_dev_warn("Failed to allocate MSI-X interrupts. Err: %d\n",
			   vectors);

		adapter->flags &= ~IXGBE_FLAG_MSIX_ENABLED;
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;

		return vectors;
	}

	/* we successfully allocated some number of vectors within our
	 * requested range.
	 */
	adapter->flags |= IXGBE_FLAG_MSIX_ENABLED;

	/* Adjust for only the vectors we'll use, which is minimum
	 * of max_q_vectors, or the number of vectors we were allocated.
	 */
	vectors -= NON_Q_VECTORS;
	adapter->num_q_vectors = min_t(int, vectors, adapter->max_q_vectors);

	return 0;
}

static void ixgbe_add_ring(struct ixgbe_ring *ring,
			   struct ixgbe_ring_container *head)
{
	ring->next = head->ring;
	head->ring = ring;
	head->count++;
	head->next_update = jiffies + 1;
}

/**
 * ixgbe_alloc_q_vector - Allocate memory for a single interrupt vector
 * @adapter: board private structure to initialize
 * @v_count: q_vectors allocated on adapter, used for ring interleaving
 * @v_idx: index of vector in adapter struct
 * @txr_count: total number of Tx rings to allocate
 * @txr_idx: index of first Tx ring to allocate
 * @xdp_count: total number of XDP rings to allocate
 * @xdp_idx: index of first XDP ring to allocate
 * @rxr_count: total number of Rx rings to allocate
 * @rxr_idx: index of first Rx ring to allocate
 *
 * We allocate one q_vector.  If allocation fails we return -ENOMEM.
 **/
static int ixgbe_alloc_q_vector(struct ixgbe_adapter *adapter,
				int v_count, int v_idx,
				int txr_count, int txr_idx,
				int xdp_count, int xdp_idx,
				int rxr_count, int rxr_idx)
{
	int node = dev_to_node(&adapter->pdev->dev);
	struct ixgbe_q_vector *q_vector;
	struct ixgbe_ring *ring;
	int cpu = -1;
	int ring_count;
	u8 tcs = adapter->hw_tcs;

	ring_count = txr_count + rxr_count + xdp_count;

	/* customize cpu for Flow Director mapping */
	if ((tcs <= 1) && !(adapter->flags & IXGBE_FLAG_SRIOV_ENABLED)) {
		u16 rss_i = adapter->ring_feature[RING_F_RSS].indices;
		if (rss_i > 1 && adapter->atr_sample_rate) {
			cpu = cpumask_local_spread(v_idx, node);
			node = cpu_to_node(cpu);
		}
	}

	/* allocate q_vector and rings */
	q_vector = kzalloc_node(struct_size(q_vector, ring, ring_count),
				GFP_KERNEL, node);
	if (!q_vector)
		q_vector = kzalloc(struct_size(q_vector, ring, ring_count),
				   GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	/* setup affinity mask and node */
	if (cpu != -1)
		cpumask_set_cpu(cpu, &q_vector->affinity_mask);
	q_vector->numa_node = node;

#ifdef CONFIG_IXGBE_DCA
	/* initialize CPU for DCA */
	q_vector->cpu = -1;

#endif
	/* initialize NAPI */
	netif_napi_add(adapter->netdev, &q_vector->napi,
		       ixgbe_poll, 64);

	/* tie q_vector and adapter together */
	adapter->q_vector[v_idx] = q_vector;
	q_vector->adapter = adapter;
	q_vector->v_idx = v_idx;

	/* initialize work limits */
	q_vector->tx.work_limit = adapter->tx_work_limit;

	/* Initialize setting for adaptive ITR */
	q_vector->tx.itr = IXGBE_ITR_ADAPTIVE_MAX_USECS |
			   IXGBE_ITR_ADAPTIVE_LATENCY;
	q_vector->rx.itr = IXGBE_ITR_ADAPTIVE_MAX_USECS |
			   IXGBE_ITR_ADAPTIVE_LATENCY;

	/* intialize ITR */
	if (txr_count && !rxr_count) {
		/* tx only vector */
		if (adapter->tx_itr_setting == 1)
			q_vector->itr = IXGBE_12K_ITR;
		else
			q_vector->itr = adapter->tx_itr_setting;
	} else {
		/* rx or rx/tx vector */
		if (adapter->rx_itr_setting == 1)
			q_vector->itr = IXGBE_20K_ITR;
		else
			q_vector->itr = adapter->rx_itr_setting;
	}

	/* initialize pointer to rings */
	ring = q_vector->ring;

	while (txr_count) {
		/* assign generic ring traits */
		ring->dev = &adapter->pdev->dev;
		ring->netdev = adapter->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* update q_vector Tx values */
		ixgbe_add_ring(ring, &q_vector->tx);

		/* apply Tx specific ring traits */
		ring->count = adapter->tx_ring_count;
		ring->queue_index = txr_idx;

		/* assign ring to adapter */
		WRITE_ONCE(adapter->tx_ring[txr_idx], ring);

		/* update count and index */
		txr_count--;
		txr_idx += v_count;

		/* push pointer to next ring */
		ring++;
	}

	while (xdp_count) {
		/* assign generic ring traits */
		ring->dev = &adapter->pdev->dev;
		ring->netdev = adapter->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* update q_vector Tx values */
		ixgbe_add_ring(ring, &q_vector->tx);

		/* apply Tx specific ring traits */
		ring->count = adapter->tx_ring_count;
		ring->queue_index = xdp_idx;
		set_ring_xdp(ring);

		/* assign ring to adapter */
		WRITE_ONCE(adapter->xdp_ring[xdp_idx], ring);

		/* update count and index */
		xdp_count--;
		xdp_idx++;

		/* push pointer to next ring */
		ring++;
	}

	while (rxr_count) {
		/* assign generic ring traits */
		ring->dev = &adapter->pdev->dev;
		ring->netdev = adapter->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* update q_vector Rx values */
		ixgbe_add_ring(ring, &q_vector->rx);

		/*
		 * 82599 errata, UDP frames with a 0 checksum
		 * can be marked as checksum errors.
		 */
		if (adapter->hw.mac.type == ixgbe_mac_82599EB)
			set_bit(__IXGBE_RX_CSUM_UDP_ZERO_ERR, &ring->state);

#ifdef IXGBE_FCOE
		if (adapter->netdev->features & NETIF_F_FCOE_MTU) {
			struct ixgbe_ring_feature *f;
			f = &adapter->ring_feature[RING_F_FCOE];
			if ((rxr_idx >= f->offset) &&
			    (rxr_idx < f->offset + f->indices))
				set_bit(__IXGBE_RX_FCOE, &ring->state);
		}

#endif /* IXGBE_FCOE */
		/* apply Rx specific ring traits */
		ring->count = adapter->rx_ring_count;
		ring->queue_index = rxr_idx;

		/* assign ring to adapter */
		WRITE_ONCE(adapter->rx_ring[rxr_idx], ring);

		/* update count and index */
		rxr_count--;
		rxr_idx += v_count;

		/* push pointer to next ring */
		ring++;
	}

	return 0;
}

/**
 * ixgbe_free_q_vector - Free memory allocated for specific interrupt vector
 * @adapter: board private structure to initialize
 * @v_idx: Index of vector to be freed
 *
 * This function frees the memory allocated to the q_vector.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void ixgbe_free_q_vector(struct ixgbe_adapter *adapter, int v_idx)
{
	struct ixgbe_q_vector *q_vector = adapter->q_vector[v_idx];
	struct ixgbe_ring *ring;

	ixgbe_for_each_ring(ring, q_vector->tx) {
		if (ring_is_xdp(ring))
			WRITE_ONCE(adapter->xdp_ring[ring->queue_index], NULL);
		else
			WRITE_ONCE(adapter->tx_ring[ring->queue_index], NULL);
	}

	ixgbe_for_each_ring(ring, q_vector->rx)
		WRITE_ONCE(adapter->rx_ring[ring->queue_index], NULL);

	adapter->q_vector[v_idx] = NULL;
	__netif_napi_del(&q_vector->napi);

	/*
	 * after a call to __netif_napi_del() napi may still be used and
	 * ixgbe_get_stats64() might access the rings on this vector,
	 * we must wait a grace period before freeing it.
	 */
	kfree_rcu(q_vector, rcu);
}

/**
 * ixgbe_alloc_q_vectors - Allocate memory for interrupt vectors
 * @adapter: board private structure to initialize
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 **/
static int ixgbe_alloc_q_vectors(struct ixgbe_adapter *adapter)
{
	int q_vectors = adapter->num_q_vectors;
	int rxr_remaining = adapter->num_rx_queues;
	int txr_remaining = adapter->num_tx_queues;
	int xdp_remaining = adapter->num_xdp_queues;
	int rxr_idx = 0, txr_idx = 0, xdp_idx = 0, v_idx = 0;
	int err, i;

	/* only one q_vector if MSI-X is disabled. */
	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED))
		q_vectors = 1;

	if (q_vectors >= (rxr_remaining + txr_remaining + xdp_remaining)) {
		for (; rxr_remaining; v_idx++) {
			err = ixgbe_alloc_q_vector(adapter, q_vectors, v_idx,
						   0, 0, 0, 0, 1, rxr_idx);

			if (err)
				goto err_out;

			/* update counts and index */
			rxr_remaining--;
			rxr_idx++;
		}
	}

	for (; v_idx < q_vectors; v_idx++) {
		int rqpv = DIV_ROUND_UP(rxr_remaining, q_vectors - v_idx);
		int tqpv = DIV_ROUND_UP(txr_remaining, q_vectors - v_idx);
		int xqpv = DIV_ROUND_UP(xdp_remaining, q_vectors - v_idx);

		err = ixgbe_alloc_q_vector(adapter, q_vectors, v_idx,
					   tqpv, txr_idx,
					   xqpv, xdp_idx,
					   rqpv, rxr_idx);

		if (err)
			goto err_out;

		/* update counts and index */
		rxr_remaining -= rqpv;
		txr_remaining -= tqpv;
		xdp_remaining -= xqpv;
		rxr_idx++;
		txr_idx++;
		xdp_idx += xqpv;
	}

	for (i = 0; i < adapter->num_rx_queues; i++) {
		if (adapter->rx_ring[i])
			adapter->rx_ring[i]->ring_idx = i;
	}

	for (i = 0; i < adapter->num_tx_queues; i++) {
		if (adapter->tx_ring[i])
			adapter->tx_ring[i]->ring_idx = i;
	}

	for (i = 0; i < adapter->num_xdp_queues; i++) {
		if (adapter->xdp_ring[i])
			adapter->xdp_ring[i]->ring_idx = i;
	}

	return 0;

err_out:
	adapter->num_tx_queues = 0;
	adapter->num_xdp_queues = 0;
	adapter->num_rx_queues = 0;
	adapter->num_q_vectors = 0;

	while (v_idx--)
		ixgbe_free_q_vector(adapter, v_idx);

	return -ENOMEM;
}

/**
 * ixgbe_free_q_vectors - Free memory allocated for interrupt vectors
 * @adapter: board private structure to initialize
 *
 * This function frees the memory allocated to the q_vectors.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void ixgbe_free_q_vectors(struct ixgbe_adapter *adapter)
{
	int v_idx = adapter->num_q_vectors;

	adapter->num_tx_queues = 0;
	adapter->num_xdp_queues = 0;
	adapter->num_rx_queues = 0;
	adapter->num_q_vectors = 0;

	while (v_idx--)
		ixgbe_free_q_vector(adapter, v_idx);
}

static void ixgbe_reset_interrupt_capability(struct ixgbe_adapter *adapter)
{
	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		adapter->flags &= ~IXGBE_FLAG_MSIX_ENABLED;
		pci_disable_msix(adapter->pdev);
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
	} else if (adapter->flags & IXGBE_FLAG_MSI_ENABLED) {
		adapter->flags &= ~IXGBE_FLAG_MSI_ENABLED;
		pci_disable_msi(adapter->pdev);
	}
}

/**
 * ixgbe_set_interrupt_capability - set MSI-X or MSI if supported
 * @adapter: board private structure to initialize
 *
 * Attempt to configure the interrupts using the best available
 * capabilities of the hardware and the kernel.
 **/
static void ixgbe_set_interrupt_capability(struct ixgbe_adapter *adapter)
{
	int err;

	/* We will try to get MSI-X interrupts first */
	if (!ixgbe_acquire_msix_vectors(adapter))
		return;

	/* At this point, we do not have MSI-X capabilities. We need to
	 * reconfigure or disable various features which require MSI-X
	 * capability.
	 */

	/* Disable DCB unless we only have a single traffic class */
	if (adapter->hw_tcs > 1) {
		e_dev_warn("Number of DCB TCs exceeds number of available queues. Disabling DCB support.\n");
		netdev_reset_tc(adapter->netdev);

		if (adapter->hw.mac.type == ixgbe_mac_82598EB)
			adapter->hw.fc.requested_mode = adapter->last_lfc_mode;

		adapter->flags &= ~IXGBE_FLAG_DCB_ENABLED;
		adapter->temp_dcb_cfg.pfc_mode_enable = false;
		adapter->dcb_cfg.pfc_mode_enable = false;
	}

	adapter->hw_tcs = 0;
	adapter->dcb_cfg.num_tcs.pg_tcs = 1;
	adapter->dcb_cfg.num_tcs.pfc_tcs = 1;

	/* Disable SR-IOV support */
	e_dev_warn("Disabling SR-IOV support\n");
	ixgbe_disable_sriov(adapter);

	/* Disable RSS */
	e_dev_warn("Disabling RSS support\n");
	adapter->ring_feature[RING_F_RSS].limit = 1;

	/* recalculate number of queues now that many features have been
	 * changed or disabled.
	 */
	ixgbe_set_num_queues(adapter);
	adapter->num_q_vectors = 1;

	err = pci_enable_msi(adapter->pdev);
	if (err)
		e_dev_warn("Failed to allocate MSI interrupt, falling back to legacy. Error: %d\n",
			   err);
	else
		adapter->flags |= IXGBE_FLAG_MSI_ENABLED;
}

/**
 * ixgbe_init_interrupt_scheme - Determine proper interrupt scheme
 * @adapter: board private structure to initialize
 *
 * We determine which interrupt scheme to use based on...
 * - Kernel support (MSI, MSI-X)
 *   - which can be user-defined (via MODULE_PARAM)
 * - Hardware queue count (num_*_queues)
 *   - defined by miscellaneous hardware support/features (RSS, etc.)
 **/
int ixgbe_init_interrupt_scheme(struct ixgbe_adapter *adapter)
{
	int err;

	/* Number of supported queues */
	ixgbe_set_num_queues(adapter);

	/* Set interrupt mode */
	ixgbe_set_interrupt_capability(adapter);

	err = ixgbe_alloc_q_vectors(adapter);
	if (err) {
		e_dev_err("Unable to allocate memory for queue vectors\n");
		goto err_alloc_q_vectors;
	}

	ixgbe_cache_ring_register(adapter);

	e_dev_info("Multiqueue %s: Rx Queue count = %u, Tx Queue count = %u XDP Queue count = %u\n",
		   (adapter->num_rx_queues > 1) ? "Enabled" : "Disabled",
		   adapter->num_rx_queues, adapter->num_tx_queues,
		   adapter->num_xdp_queues);

	set_bit(__IXGBE_DOWN, &adapter->state);

	return 0;

err_alloc_q_vectors:
	ixgbe_reset_interrupt_capability(adapter);
	return err;
}

/**
 * ixgbe_clear_interrupt_scheme - Clear the current interrupt scheme settings
 * @adapter: board private structure to clear interrupt scheme on
 *
 * We go through and clear interrupt specific resources and reset the structure
 * to pre-load conditions
 **/
void ixgbe_clear_interrupt_scheme(struct ixgbe_adapter *adapter)
{
	adapter->num_tx_queues = 0;
	adapter->num_xdp_queues = 0;
	adapter->num_rx_queues = 0;

	ixgbe_free_q_vectors(adapter);
	ixgbe_reset_interrupt_capability(adapter);
}

void ixgbe_tx_ctxtdesc(struct ixgbe_ring *tx_ring, u32 vlan_macip_lens,
		       u32 fceof_saidx, u32 type_tucmd, u32 mss_l4len_idx)
{
	struct ixgbe_adv_tx_context_desc *context_desc;
	u16 i = tx_ring->next_to_use;

	context_desc = IXGBE_TX_CTXTDESC(tx_ring, i);

	i++;
	tx_ring->next_to_use = (i < tx_ring->count) ? i : 0;

	/* set bits to identify this as an advanced context descriptor */
	type_tucmd |= IXGBE_TXD_CMD_DEXT | IXGBE_ADVTXD_DTYP_CTXT;

	context_desc->vlan_macip_lens	= cpu_to_le32(vlan_macip_lens);
	context_desc->fceof_saidx	= cpu_to_le32(fceof_saidx);
	context_desc->type_tucmd_mlhl	= cpu_to_le32(type_tucmd);
	context_desc->mss_l4len_idx	= cpu_to_le32(mss_l4len_idx);
}

