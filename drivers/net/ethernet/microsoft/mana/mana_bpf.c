// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/mm.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <net/xdp.h>

#include "mana.h"

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

u32 mana_run_xdp(struct net_device *ndev, struct mana_rxq *rxq,
		 struct xdp_buff *xdp, void *buf_va, uint pkt_len)
{
	struct bpf_prog *prog;
	u32 act = XDP_PASS;

	rcu_read_lock();
	prog = rcu_dereference(rxq->bpf_prog);

	if (!prog)
		goto out;

	xdp_init_buff(xdp, PAGE_SIZE, &rxq->xdp_rxq);
	xdp_prepare_buff(xdp, buf_va, XDP_PACKET_HEADROOM, pkt_len, false);

	act = bpf_prog_run_xdp(prog, xdp);

	switch (act) {
	case XDP_PASS:
	case XDP_TX:
	case XDP_DROP:
		break;

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

static unsigned int mana_xdp_fraglen(unsigned int len)
{
	return SKB_DATA_ALIGN(len) +
	       SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
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
	int buf_max;

	old_prog = mana_xdp_get(apc);

	if (!old_prog && !prog)
		return 0;

	buf_max = XDP_PACKET_HEADROOM + mana_xdp_fraglen(ndev->mtu + ETH_HLEN);
	if (prog && buf_max > PAGE_SIZE) {
		netdev_err(ndev, "XDP: mtu:%u too large, buf_max:%u\n",
			   ndev->mtu, buf_max);
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
