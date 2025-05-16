// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/mm.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <net/xdp.h>

#include <net/mana/mana.h>

void mana_xdp_tx(struct sk_buff *skb, struct net_device *ndev)
{
	u16 txq_idx = skb_get_queue_mapping(skb);
	struct netdev_queue *ndevtxq;
	int rc;

	__skb_push(skb, ETH_HLEN);

	ndevtxq = netdev_get_tx_queue(ndev, txq_idx);
	__netif_tx_lock(ndevtxq, smp_processor_id());

	rc = mana_start_xmit(skb, ndev);

	__netif_tx_unlock(ndevtxq);

	if (dev_xmit_complete(rc))
		return;

	dev_kfree_skb_any(skb);
	ndev->stats.tx_dropped++;
}

static int mana_xdp_xmit_fm(struct net_device *ndev, struct xdp_frame *frame,
			    u16 q_idx)
{
	struct sk_buff *skb;

	skb = xdp_build_skb_from_frame(frame, ndev);
	if (unlikely(!skb))
		return -ENOMEM;

	skb_set_queue_mapping(skb, q_idx);

	mana_xdp_tx(skb, ndev);

	return 0;
}

int mana_xdp_xmit(struct net_device *ndev, int n, struct xdp_frame **frames,
		  u32 flags)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	struct mana_stats_tx *tx_stats;
	int i, count = 0;
	u16 q_idx;

	if (unlikely(!apc->port_is_up))
		return 0;

	q_idx = smp_processor_id() % ndev->real_num_tx_queues;

	for (i = 0; i < n; i++) {
		if (mana_xdp_xmit_fm(ndev, frames[i], q_idx))
			break;

		count++;
	}

	tx_stats = &apc->tx_qp[q_idx].txq.stats;

	u64_stats_update_begin(&tx_stats->syncp);
	tx_stats->xdp_xmit += count;
	u64_stats_update_end(&tx_stats->syncp);

	return count;
}

u32 mana_run_xdp(struct net_device *ndev, struct mana_rxq *rxq,
		 struct xdp_buff *xdp, void *buf_va, uint pkt_len)
{
	struct mana_stats_rx *rx_stats;
	struct bpf_prog *prog;
	u32 act = XDP_PASS;

	rcu_read_lock();
	prog = rcu_dereference(rxq->bpf_prog);

	if (!prog)
		goto out;

	xdp_init_buff(xdp, PAGE_SIZE, &rxq->xdp_rxq);
	xdp_prepare_buff(xdp, buf_va, XDP_PACKET_HEADROOM, pkt_len, true);

	act = bpf_prog_run_xdp(prog, xdp);

	rx_stats = &rxq->stats;

	switch (act) {
	case XDP_PASS:
	case XDP_TX:
	case XDP_DROP:
		break;

	case XDP_REDIRECT:
		rxq->xdp_rc = xdp_do_redirect(ndev, xdp, prog);
		if (!rxq->xdp_rc) {
			rxq->xdp_flush = true;

			u64_stats_update_begin(&rx_stats->syncp);
			rx_stats->packets++;
			rx_stats->bytes += pkt_len;
			rx_stats->xdp_redirect++;
			u64_stats_update_end(&rx_stats->syncp);

			break;
		}

		fallthrough;

	case XDP_ABORTED:
		trace_xdp_exception(ndev, prog, act);
		break;

	default:
		bpf_warn_invalid_xdp_action(ndev, prog, act);
	}

out:
	rcu_read_unlock();

	return act;
}

struct bpf_prog *mana_xdp_get(struct mana_port_context *apc)
{
	ASSERT_RTNL();

	return apc->bpf_prog;
}

static struct bpf_prog *mana_chn_xdp_get(struct mana_port_context *apc)
{
	return rtnl_dereference(apc->rxqs[0]->bpf_prog);
}

/* Set xdp program on channels */
void mana_chn_setxdp(struct mana_port_context *apc, struct bpf_prog *prog)
{
	struct bpf_prog *old_prog = mana_chn_xdp_get(apc);
	unsigned int num_queues = apc->num_queues;
	int i;

	ASSERT_RTNL();

	if (old_prog == prog)
		return;

	if (prog)
		bpf_prog_add(prog, num_queues);

	for (i = 0; i < num_queues; i++)
		rcu_assign_pointer(apc->rxqs[i]->bpf_prog, prog);

	if (old_prog)
		for (i = 0; i < num_queues; i++)
			bpf_prog_put(old_prog);
}

static int mana_xdp_set(struct net_device *ndev, struct bpf_prog *prog,
			struct netlink_ext_ack *extack)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	struct bpf_prog *old_prog;
	struct gdma_context *gc;

	gc = apc->ac->gdma_dev->gdma_context;

	old_prog = mana_xdp_get(apc);

	if (!old_prog && !prog)
		return 0;

	if (prog && ndev->mtu > MANA_XDP_MTU_MAX) {
		netdev_err(ndev, "XDP: mtu:%u too large, mtu_max:%lu\n",
			   ndev->mtu, MANA_XDP_MTU_MAX);
		NL_SET_ERR_MSG_MOD(extack, "XDP: mtu too large");

		return -EOPNOTSUPP;
	}

	/* One refcnt of the prog is hold by the caller already, so
	 * don't increase refcnt for this one.
	 */
	apc->bpf_prog = prog;

	if (old_prog)
		bpf_prog_put(old_prog);

	if (apc->port_is_up)
		mana_chn_setxdp(apc, prog);

	if (prog)
		ndev->max_mtu = MANA_XDP_MTU_MAX;
	else
		ndev->max_mtu = gc->adapter_mtu - ETH_HLEN;

	return 0;
}

int mana_bpf(struct net_device *ndev, struct netdev_bpf *bpf)
{
	struct netlink_ext_ack *extack = bpf->extack;
	int ret;

	switch (bpf->command) {
	case XDP_SETUP_PROG:
		return mana_xdp_set(ndev, bpf->prog, extack);

	default:
		return -EOPNOTSUPP;
	}

	return ret;
}
