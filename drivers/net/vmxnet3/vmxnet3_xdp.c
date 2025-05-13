// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux driver for VMware's vmxnet3 ethernet NIC.
 * Copyright (C) 2008-2023, VMware, Inc. All Rights Reserved.
 * Maintained by: pv-drivers@vmware.com
 *
 */

#include "vmxnet3_int.h"
#include "vmxnet3_xdp.h"

static void
vmxnet3_xdp_exchange_program(struct vmxnet3_adapter *adapter,
			     struct bpf_prog *prog)
{
	rcu_assign_pointer(adapter->xdp_bpf_prog, prog);
}

static inline struct vmxnet3_tx_queue *
vmxnet3_xdp_get_tq(struct vmxnet3_adapter *adapter)
{
	struct vmxnet3_tx_queue *tq;
	int tq_number;
	int cpu;

	tq_number = adapter->num_tx_queues;
	cpu = smp_processor_id();
	if (likely(cpu < tq_number))
		tq = &adapter->tx_queue[cpu];
	else
		tq = &adapter->tx_queue[cpu % tq_number];

	return tq;
}

static int
vmxnet3_xdp_set(struct net_device *netdev, struct netdev_bpf *bpf,
		struct netlink_ext_ack *extack)
{
	struct vmxnet3_adapter *adapter = netdev_priv(netdev);
	struct bpf_prog *new_bpf_prog = bpf->prog;
	struct bpf_prog *old_bpf_prog;
	bool need_update;
	bool running;
	int err;

	if (new_bpf_prog && netdev->mtu > VMXNET3_XDP_MAX_MTU) {
		NL_SET_ERR_MSG_FMT_MOD(extack, "MTU %u too large for XDP",
				       netdev->mtu);
		return -EOPNOTSUPP;
	}

	if (adapter->netdev->features & NETIF_F_LRO) {
		NL_SET_ERR_MSG_MOD(extack, "LRO is not supported with XDP");
		adapter->netdev->features &= ~NETIF_F_LRO;
	}

	old_bpf_prog = rcu_dereference(adapter->xdp_bpf_prog);
	if (!new_bpf_prog && !old_bpf_prog)
		return 0;

	running = netif_running(netdev);
	need_update = !!old_bpf_prog != !!new_bpf_prog;

	if (running && need_update)
		vmxnet3_quiesce_dev(adapter);

	vmxnet3_xdp_exchange_program(adapter, new_bpf_prog);
	if (old_bpf_prog)
		bpf_prog_put(old_bpf_prog);

	if (!running || !need_update)
		return 0;

	if (new_bpf_prog)
		xdp_features_set_redirect_target(netdev, false);
	else
		xdp_features_clear_redirect_target(netdev);

	vmxnet3_reset_dev(adapter);
	vmxnet3_rq_destroy_all(adapter);
	vmxnet3_adjust_rx_ring_size(adapter);
	err = vmxnet3_rq_create_all(adapter);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "failed to re-create rx queues for XDP.");
		return -EOPNOTSUPP;
	}
	err = vmxnet3_activate_dev(adapter);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "failed to activate device for XDP.");
		return -EOPNOTSUPP;
	}
	clear_bit(VMXNET3_STATE_BIT_RESETTING, &adapter->state);

	return 0;
}

/* This is the main xdp call used by kernel to set/unset eBPF program. */
int
vmxnet3_xdp(struct net_device *netdev, struct netdev_bpf *bpf)
{
	switch (bpf->command) {
	case XDP_SETUP_PROG:
		return vmxnet3_xdp_set(netdev, bpf, bpf->extack);
	default:
		return -EINVAL;
	}

	return 0;
}

static int
vmxnet3_xdp_xmit_frame(struct vmxnet3_adapter *adapter,
		       struct xdp_frame *xdpf,
		       struct vmxnet3_tx_queue *tq, bool dma_map)
{
	struct vmxnet3_tx_buf_info *tbi = NULL;
	union Vmxnet3_GenericDesc *gdesc;
	struct vmxnet3_tx_ctx ctx;
	int tx_num_deferred;
	struct page *page;
	u32 buf_size;
	u32 dw2;

	spin_lock_irq(&tq->tx_lock);
	dw2 = (tq->tx_ring.gen ^ 0x1) << VMXNET3_TXD_GEN_SHIFT;
	dw2 |= xdpf->len;
	ctx.sop_txd = tq->tx_ring.base + tq->tx_ring.next2fill;
	gdesc = ctx.sop_txd;

	buf_size = xdpf->len;
	tbi = tq->buf_info + tq->tx_ring.next2fill;

	if (vmxnet3_cmd_ring_desc_avail(&tq->tx_ring) == 0) {
		tq->stats.tx_ring_full++;
		spin_unlock_irq(&tq->tx_lock);
		return -ENOSPC;
	}

	tbi->map_type = VMXNET3_MAP_XDP;
	if (dma_map) { /* ndo_xdp_xmit */
		tbi->dma_addr = dma_map_single(&adapter->pdev->dev,
					       xdpf->data, buf_size,
					       DMA_TO_DEVICE);
		if (dma_mapping_error(&adapter->pdev->dev, tbi->dma_addr)) {
			spin_unlock_irq(&tq->tx_lock);
			return -EFAULT;
		}
		tbi->map_type |= VMXNET3_MAP_SINGLE;
	} else { /* XDP buffer from page pool */
		page = virt_to_page(xdpf->data);
		tbi->dma_addr = page_pool_get_dma_addr(page) +
				(xdpf->data - (void *)xdpf);
		dma_sync_single_for_device(&adapter->pdev->dev,
					   tbi->dma_addr, buf_size,
					   DMA_TO_DEVICE);
	}
	tbi->xdpf = xdpf;
	tbi->len = buf_size;

	gdesc = tq->tx_ring.base + tq->tx_ring.next2fill;
	WARN_ON_ONCE(gdesc->txd.gen == tq->tx_ring.gen);

	gdesc->txd.addr = cpu_to_le64(tbi->dma_addr);
	gdesc->dword[2] = cpu_to_le32(dw2);

	/* Setup the EOP desc */
	gdesc->dword[3] = cpu_to_le32(VMXNET3_TXD_CQ | VMXNET3_TXD_EOP);

	gdesc->txd.om = 0;
	gdesc->txd.msscof = 0;
	gdesc->txd.hlen = 0;
	gdesc->txd.ti = 0;

	tx_num_deferred = le32_to_cpu(tq->shared->txNumDeferred);
	le32_add_cpu(&tq->shared->txNumDeferred, 1);
	tx_num_deferred++;

	vmxnet3_cmd_ring_adv_next2fill(&tq->tx_ring);

	/* set the last buf_info for the pkt */
	tbi->sop_idx = ctx.sop_txd - tq->tx_ring.base;

	dma_wmb();
	gdesc->dword[2] = cpu_to_le32(le32_to_cpu(gdesc->dword[2]) ^
						  VMXNET3_TXD_GEN);
	spin_unlock_irq(&tq->tx_lock);

	/* No need to handle the case when tx_num_deferred doesn't reach
	 * threshold. Backend driver at hypervisor side will poll and reset
	 * tq->shared->txNumDeferred to 0.
	 */
	if (tx_num_deferred >= le32_to_cpu(tq->shared->txThreshold)) {
		tq->shared->txNumDeferred = 0;
		VMXNET3_WRITE_BAR0_REG(adapter,
				       VMXNET3_REG_TXPROD + tq->qid * 8,
				       tq->tx_ring.next2fill);
	}

	return 0;
}

static int
vmxnet3_xdp_xmit_back(struct vmxnet3_adapter *adapter,
		      struct xdp_frame *xdpf)
{
	struct vmxnet3_tx_queue *tq;
	struct netdev_queue *nq;
	int err;

	tq = vmxnet3_xdp_get_tq(adapter);
	if (tq->stopped)
		return -ENETDOWN;

	nq = netdev_get_tx_queue(adapter->netdev, tq->qid);

	__netif_tx_lock(nq, smp_processor_id());
	err = vmxnet3_xdp_xmit_frame(adapter, xdpf, tq, false);
	__netif_tx_unlock(nq);

	return err;
}

/* ndo_xdp_xmit */
int
vmxnet3_xdp_xmit(struct net_device *dev,
		 int n, struct xdp_frame **frames, u32 flags)
{
	struct vmxnet3_adapter *adapter = netdev_priv(dev);
	struct vmxnet3_tx_queue *tq;
	struct netdev_queue *nq;
	int i;

	if (unlikely(test_bit(VMXNET3_STATE_BIT_QUIESCED, &adapter->state)))
		return -ENETDOWN;
	if (unlikely(test_bit(VMXNET3_STATE_BIT_RESETTING, &adapter->state)))
		return -EINVAL;

	tq = vmxnet3_xdp_get_tq(adapter);
	if (tq->stopped)
		return -ENETDOWN;

	nq = netdev_get_tx_queue(adapter->netdev, tq->qid);

	__netif_tx_lock(nq, smp_processor_id());
	for (i = 0; i < n; i++) {
		if (vmxnet3_xdp_xmit_frame(adapter, frames[i], tq, true)) {
			tq->stats.xdp_xmit_err++;
			break;
		}
	}
	tq->stats.xdp_xmit += i;
	__netif_tx_unlock(nq);

	return i;
}

static int
vmxnet3_run_xdp(struct vmxnet3_rx_queue *rq, struct xdp_buff *xdp,
		struct bpf_prog *prog)
{
	struct xdp_frame *xdpf;
	struct page *page;
	int err;
	u32 act;

	rq->stats.xdp_packets++;
	act = bpf_prog_run_xdp(prog, xdp);
	page = virt_to_page(xdp->data_hard_start);

	switch (act) {
	case XDP_PASS:
		return act;
	case XDP_REDIRECT:
		err = xdp_do_redirect(rq->adapter->netdev, xdp, prog);
		if (!err) {
			rq->stats.xdp_redirects++;
		} else {
			rq->stats.xdp_drops++;
			page_pool_recycle_direct(rq->page_pool, page);
		}
		return act;
	case XDP_TX:
		xdpf = xdp_convert_buff_to_frame(xdp);
		if (unlikely(!xdpf ||
			     vmxnet3_xdp_xmit_back(rq->adapter, xdpf))) {
			rq->stats.xdp_drops++;
			page_pool_recycle_direct(rq->page_pool, page);
		} else {
			rq->stats.xdp_tx++;
		}
		return act;
	default:
		bpf_warn_invalid_xdp_action(rq->adapter->netdev, prog, act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(rq->adapter->netdev, prog, act);
		rq->stats.xdp_aborted++;
		break;
	case XDP_DROP:
		rq->stats.xdp_drops++;
		break;
	}

	page_pool_recycle_direct(rq->page_pool, page);

	return act;
}

static struct sk_buff *
vmxnet3_build_skb(struct vmxnet3_rx_queue *rq, struct page *page,
		  const struct xdp_buff *xdp)
{
	struct sk_buff *skb;

	skb = build_skb(page_address(page), PAGE_SIZE);
	if (unlikely(!skb)) {
		page_pool_recycle_direct(rq->page_pool, page);
		rq->stats.rx_buf_alloc_failure++;
		return NULL;
	}

	/* bpf prog might change len and data position. */
	skb_reserve(skb, xdp->data - xdp->data_hard_start);
	skb_put(skb, xdp->data_end - xdp->data);
	skb_mark_for_recycle(skb);

	return skb;
}

/* Handle packets from DataRing. */
int
vmxnet3_process_xdp_small(struct vmxnet3_adapter *adapter,
			  struct vmxnet3_rx_queue *rq,
			  void *data, int len,
			  struct sk_buff **skb_xdp_pass)
{
	struct bpf_prog *xdp_prog;
	struct xdp_buff xdp;
	struct page *page;
	int act;

	page = page_pool_alloc_pages(rq->page_pool, GFP_ATOMIC);
	if (unlikely(!page)) {
		rq->stats.rx_buf_alloc_failure++;
		return XDP_DROP;
	}

	xdp_init_buff(&xdp, PAGE_SIZE, &rq->xdp_rxq);
	xdp_prepare_buff(&xdp, page_address(page), rq->page_pool->p.offset,
			 len, false);
	xdp_buff_clear_frags_flag(&xdp);

	/* Must copy the data because it's at dataring. */
	memcpy(xdp.data, data, len);

	xdp_prog = rcu_dereference(rq->adapter->xdp_bpf_prog);
	if (!xdp_prog) {
		act = XDP_PASS;
		goto out_skb;
	}
	act = vmxnet3_run_xdp(rq, &xdp, xdp_prog);
	if (act != XDP_PASS)
		return act;

out_skb:
	*skb_xdp_pass = vmxnet3_build_skb(rq, page, &xdp);
	if (!*skb_xdp_pass)
		return XDP_DROP;

	/* No need to refill. */
	return likely(*skb_xdp_pass) ? act : XDP_DROP;
}

int
vmxnet3_process_xdp(struct vmxnet3_adapter *adapter,
		    struct vmxnet3_rx_queue *rq,
		    struct Vmxnet3_RxCompDesc *rcd,
		    struct vmxnet3_rx_buf_info *rbi,
		    struct Vmxnet3_RxDesc *rxd,
		    struct sk_buff **skb_xdp_pass)
{
	struct bpf_prog *xdp_prog;
	dma_addr_t new_dma_addr;
	struct xdp_buff xdp;
	struct page *page;
	void *new_data;
	int act;

	page = rbi->page;
	dma_sync_single_for_cpu(&adapter->pdev->dev,
				page_pool_get_dma_addr(page) +
				rq->page_pool->p.offset, rbi->len,
				page_pool_get_dma_dir(rq->page_pool));

	xdp_init_buff(&xdp, PAGE_SIZE, &rq->xdp_rxq);
	xdp_prepare_buff(&xdp, page_address(page), rq->page_pool->p.offset,
			 rcd->len, false);
	xdp_buff_clear_frags_flag(&xdp);

	xdp_prog = rcu_dereference(rq->adapter->xdp_bpf_prog);
	if (!xdp_prog) {
		act = XDP_PASS;
		goto out_skb;
	}
	act = vmxnet3_run_xdp(rq, &xdp, xdp_prog);

	if (act == XDP_PASS) {
out_skb:
		*skb_xdp_pass = vmxnet3_build_skb(rq, page, &xdp);
		if (!*skb_xdp_pass)
			act = XDP_DROP;
	}

	new_data = vmxnet3_pp_get_buff(rq->page_pool, &new_dma_addr,
				       GFP_ATOMIC);
	if (!new_data) {
		rq->stats.rx_buf_alloc_failure++;
		return XDP_DROP;
	}
	rbi->page = virt_to_page(new_data);
	rbi->dma_addr = new_dma_addr;
	rxd->addr = cpu_to_le64(rbi->dma_addr);
	rxd->len = rbi->len;

	return act;
}
