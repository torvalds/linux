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

void bnxt_xmit_xdp(struct bnxt *bp, struct bnxt_tx_ring_info *txr,
		   dma_addr_t mapping, u32 len, u16 rx_prod)
{
	struct bnxt_sw_tx_bd *tx_buf;
	struct tx_bd *txbd;
	u32 flags;
	u16 prod;

	prod = txr->tx_prod;
	tx_buf = &txr->tx_buf_ring[prod];
	tx_buf->rx_prod = rx_prod;

	txbd = &txr->tx_desc_ring[TX_RING(prod)][TX_IDX(prod)];
	flags = (len << TX_BD_LEN_SHIFT) | (1 << TX_BD_FLAGS_BD_CNT_SHIFT) |
		TX_BD_FLAGS_PACKET_END | bnxt_lhint_arr[len >> 9];
	txbd->tx_bd_len_flags_type = cpu_to_le32(flags);
	txbd->tx_bd_opaque = prod;
	txbd->tx_bd_haddr = cpu_to_le64(mapping);

	prod = NEXT_TX(prod);
	txr->tx_prod = prod;
}

void bnxt_tx_int_xdp(struct bnxt *bp, struct bnxt_napi *bnapi, int nr_pkts)
{
	struct bnxt_tx_ring_info *txr = bnapi->tx_ring;
	struct bnxt_rx_ring_info *rxr = bnapi->rx_ring;
	struct bnxt_sw_tx_bd *tx_buf;
	u16 tx_cons = txr->tx_cons;
	u16 last_tx_cons = tx_cons;
	u16 rx_prod;
	int i;

	for (i = 0; i < nr_pkts; i++) {
		last_tx_cons = tx_cons;
		tx_cons = NEXT_TX(tx_cons);
	}
	txr->tx_cons = tx_cons;
	if (bnxt_tx_avail(bp, txr) == bp->tx_ring_size) {
		rx_prod = rxr->rx_prod;
	} else {
		tx_buf = &txr->tx_buf_ring[last_tx_cons];
		rx_prod = tx_buf->rx_prod;
	}
	bnxt_db_write(bp, rxr->rx_doorbell, DB_KEY_RX | rx_prod);
}

/* returns the following:
 * true    - packet consumed by XDP and new buffer is allocated.
 * false   - packet should be passed to the stack.
 */
bool bnxt_rx_xdp(struct bnxt *bp, struct bnxt_rx_ring_info *rxr, u16 cons,
		 struct page *page, u8 **data_ptr, unsigned int *len, u8 *event)
{
	struct bpf_prog *xdp_prog = READ_ONCE(rxr->xdp_prog);
	struct bnxt_tx_ring_info *txr;
	struct bnxt_sw_rx_bd *rx_buf;
	struct pci_dev *pdev;
	struct xdp_buff xdp;
	dma_addr_t mapping;
	void *orig_data;
	u32 tx_avail;
	u32 offset;
	u32 act;

	if (!xdp_prog)
		return false;

	pdev = bp->pdev;
	txr = rxr->bnapi->tx_ring;
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

	tx_avail = bnxt_tx_avail(bp, txr);
	/* If the tx ring is not full, we must not update the rx producer yet
	 * because we may still be transmitting on some BDs.
	 */
	if (tx_avail != bp->tx_ring_size)
		*event &= ~BNXT_RX_EVENT;

	if (orig_data != xdp.data) {
		offset = xdp.data - xdp.data_hard_start;
		*data_ptr = xdp.data_hard_start + offset;
		*len = xdp.data_end - xdp.data;
	}
	switch (act) {
	case XDP_PASS:
		return false;

	case XDP_TX:
		if (tx_avail < 1) {
			trace_xdp_exception(bp->dev, xdp_prog, act);
			bnxt_reuse_rx_data(rxr, cons, page);
			return true;
		}

		*event = BNXT_TX_EVENT;
		dma_sync_single_for_device(&pdev->dev, mapping + offset, *len,
					   bp->rx_dir);
		bnxt_xmit_xdp(bp, txr, mapping + offset, *len,
			      NEXT_RX(rxr->rx_prod));
		bnxt_reuse_rx_data(rxr, cons, page);
		return true;
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
				true, tc, tx_xdp);
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
		xdp->prog_id = bp->xdp_prog ? bp->xdp_prog->aux->id : 0;
		rc = 0;
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}
