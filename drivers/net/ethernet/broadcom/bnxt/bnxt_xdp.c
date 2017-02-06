/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016-2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/filter.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_xdp.h"

/* returns the following:
 * true    - packet consumed by XDP and new buffer is allocated.
 * false   - packet should be passed to the stack.
 */
bool bnxt_rx_xdp(struct bnxt *bp, struct bnxt_rx_ring_info *rxr, u16 cons,
		 struct page *page, u8 **data_ptr, unsigned int *len, u8 *event)
{
	struct bpf_prog *xdp_prog = READ_ONCE(rxr->xdp_prog);
	struct bnxt_sw_rx_bd *rx_buf;
	struct pci_dev *pdev;
	struct xdp_buff xdp;
	dma_addr_t mapping;
	void *orig_data;
	u32 offset;
	u32 act;

	if (!xdp_prog)
		return false;

	pdev = bp->pdev;
	rx_buf = &rxr->rx_buf_ring[cons];
	offset = bp->rx_offset;

	xdp.data_hard_start = *data_ptr - offset;
	xdp.data = *data_ptr;
	xdp.data_end = *data_ptr + *len;
	orig_data = xdp.data;
	mapping = rx_buf->mapping - bp->rx_dma_offset;

	dma_sync_single_for_cpu(&pdev->dev, mapping + offset, *len, bp->rx_dir);

	rcu_read_lock();
	act = bpf_prog_run_xdp(xdp_prog, &xdp);
	rcu_read_unlock();

	if (orig_data != xdp.data) {
		offset = xdp.data - xdp.data_hard_start;
		*data_ptr = xdp.data_hard_start + offset;
		*len = xdp.data_end - xdp.data;
	}
	switch (act) {
	case XDP_PASS:
		return false;

	default:
		bpf_warn_invalid_xdp_action(act);
		/* Fall thru */
	case XDP_ABORTED:
		trace_xdp_exception(bp->dev, xdp_prog, act);
		/* Fall thru */
	case XDP_DROP:
		bnxt_reuse_rx_data(rxr, cons, page);
		break;
	}
	return true;
}

/* Under rtnl_lock */
static int bnxt_xdp_set(struct bnxt *bp, struct bpf_prog *prog)
{
	struct net_device *dev = bp->dev;
	int tx_xdp = 0, rc, tc;
	struct bpf_prog *old;

	if (prog && bp->dev->mtu > BNXT_MAX_PAGE_MODE_MTU) {
		netdev_warn(dev, "MTU %d larger than largest XDP supported MTU %d.\n",
			    bp->dev->mtu, BNXT_MAX_PAGE_MODE_MTU);
		return -EOPNOTSUPP;
	}
	if (!(bp->flags & BNXT_FLAG_SHARED_RINGS)) {
		netdev_warn(dev, "ethtool rx/tx channels must be combined to support XDP.\n");
		return -EOPNOTSUPP;
	}
	if (prog)
		tx_xdp = bp->rx_nr_rings;

	tc = netdev_get_num_tc(dev);
	if (!tc)
		tc = 1;
	rc = bnxt_reserve_rings(bp, bp->tx_nr_rings_per_tc, bp->rx_nr_rings,
				tc, tx_xdp);
	if (rc) {
		netdev_warn(dev, "Unable to reserve enough TX rings to support XDP.\n");
		return rc;
	}
	if (netif_running(dev))
		bnxt_close_nic(bp, true, false);

	old = xchg(&bp->xdp_prog, prog);
	if (old)
		bpf_prog_put(old);

	if (prog) {
		bnxt_set_rx_skb_mode(bp, true);
	} else {
		int rx, tx;

		bnxt_set_rx_skb_mode(bp, false);
		bnxt_get_max_rings(bp, &rx, &tx, true);
		if (rx > 1) {
			bp->flags &= ~BNXT_FLAG_NO_AGG_RINGS;
			bp->dev->hw_features |= NETIF_F_LRO;
		}
	}
	bp->tx_nr_rings_xdp = tx_xdp;
	bp->tx_nr_rings = bp->tx_nr_rings_per_tc * tc + tx_xdp;
	bp->cp_nr_rings = max_t(int, bp->tx_nr_rings, bp->rx_nr_rings);
	bp->num_stat_ctxs = bp->cp_nr_rings;
	bnxt_set_tpa_flags(bp);
	bnxt_set_ring_params(bp);

	if (netif_running(dev))
		return bnxt_open_nic(bp, true, false);

	return 0;
}

int bnxt_xdp(struct net_device *dev, struct netdev_xdp *xdp)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		rc = bnxt_xdp_set(bp, xdp->prog);
		break;
	case XDP_QUERY_PROG:
		xdp->prog_attached = !!bp->xdp_prog;
		rc = 0;
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}
