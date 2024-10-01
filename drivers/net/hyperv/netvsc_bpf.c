// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019, Microsoft Corporation.
 *
 * Author:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/netpoll.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/kernel.h>
#include <net/xdp.h>

#include <linux/mutex.h>
#include <linux/rtnetlink.h>

#include "hyperv_net.h"

u32 netvsc_run_xdp(struct net_device *ndev, struct netvsc_channel *nvchan,
		   struct xdp_buff *xdp)
{
	struct netvsc_stats_rx *rx_stats = &nvchan->rx_stats;
	void *data = nvchan->rsc.data[0];
	u32 len = nvchan->rsc.len[0];
	struct page *page = NULL;
	struct bpf_prog *prog;
	u32 act = XDP_PASS;
	bool drop = true;

	xdp->data_hard_start = NULL;

	rcu_read_lock();
	prog = rcu_dereference(nvchan->bpf_prog);

	if (!prog)
		goto out;

	/* Ensure that the below memcpy() won't overflow the page buffer. */
	if (len > ndev->mtu + ETH_HLEN) {
		act = XDP_DROP;
		goto out;
	}

	/* allocate page buffer for data */
	page = alloc_page(GFP_ATOMIC);
	if (!page) {
		act = XDP_DROP;
		goto out;
	}

	xdp_init_buff(xdp, PAGE_SIZE, &nvchan->xdp_rxq);
	xdp_prepare_buff(xdp, page_address(page), NETVSC_XDP_HDRM, len, false);

	memcpy(xdp->data, data, len);

	act = bpf_prog_run_xdp(prog, xdp);

	switch (act) {
	case XDP_PASS:
	case XDP_TX:
		drop = false;
		break;

	case XDP_DROP:
		break;

	case XDP_REDIRECT:
		if (!xdp_do_redirect(ndev, xdp, prog)) {
			nvchan->xdp_flush = true;
			drop = false;

			u64_stats_update_begin(&rx_stats->syncp);

			rx_stats->xdp_redirect++;
			rx_stats->packets++;
			rx_stats->bytes += nvchan->rsc.pktlen;

			u64_stats_update_end(&rx_stats->syncp);

			break;
		} else {
			u64_stats_update_begin(&rx_stats->syncp);
			rx_stats->xdp_drop++;
			u64_stats_update_end(&rx_stats->syncp);
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

	if (page && drop) {
		__free_page(page);
		xdp->data_hard_start = NULL;
	}

	return act;
}

unsigned int netvsc_xdp_fraglen(unsigned int len)
{
	return SKB_DATA_ALIGN(len) +
	       SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
}

struct bpf_prog *netvsc_xdp_get(struct netvsc_device *nvdev)
{
	return rtnl_dereference(nvdev->chan_table[0].bpf_prog);
}

int netvsc_xdp_set(struct net_device *dev, struct bpf_prog *prog,
		   struct netlink_ext_ack *extack,
		   struct netvsc_device *nvdev)
{
	struct bpf_prog *old_prog;
	int buf_max, i;

	old_prog = netvsc_xdp_get(nvdev);

	if (!old_prog && !prog)
		return 0;

	buf_max = NETVSC_XDP_HDRM + netvsc_xdp_fraglen(dev->mtu + ETH_HLEN);
	if (prog && buf_max > PAGE_SIZE) {
		netdev_err(dev, "XDP: mtu:%u too large, buf_max:%u\n",
			   dev->mtu, buf_max);
		NL_SET_ERR_MSG_MOD(extack, "XDP: mtu too large");

		return -EOPNOTSUPP;
	}

	if (prog && (dev->features & NETIF_F_LRO)) {
		netdev_err(dev, "XDP: not support LRO\n");
		NL_SET_ERR_MSG_MOD(extack, "XDP: not support LRO");

		return -EOPNOTSUPP;
	}

	if (prog)
		bpf_prog_add(prog, nvdev->num_chn - 1);

	for (i = 0; i < nvdev->num_chn; i++)
		rcu_assign_pointer(nvdev->chan_table[i].bpf_prog, prog);

	if (old_prog)
		for (i = 0; i < nvdev->num_chn; i++)
			bpf_prog_put(old_prog);

	return 0;
}

int netvsc_vf_setxdp(struct net_device *vf_netdev, struct bpf_prog *prog)
{
	struct netdev_bpf xdp;
	int ret;

	ASSERT_RTNL();

	if (!vf_netdev)
		return 0;

	if (!vf_netdev->netdev_ops->ndo_bpf)
		return 0;

	memset(&xdp, 0, sizeof(xdp));

	if (prog)
		bpf_prog_inc(prog);

	xdp.command = XDP_SETUP_PROG;
	xdp.prog = prog;

	ret = dev_xdp_propagate(vf_netdev, &xdp);

	if (ret && prog)
		bpf_prog_put(prog);

	return ret;
}

int netvsc_bpf(struct net_device *dev, struct netdev_bpf *bpf)
{
	struct net_device_context *ndevctx = netdev_priv(dev);
	struct netvsc_device *nvdev = rtnl_dereference(ndevctx->nvdev);
	struct net_device *vf_netdev = rtnl_dereference(ndevctx->vf_netdev);
	struct netlink_ext_ack *extack = bpf->extack;
	int ret;

	if (!nvdev || nvdev->destroy) {
		return -ENODEV;
	}

	switch (bpf->command) {
	case XDP_SETUP_PROG:
		ret = netvsc_xdp_set(dev, bpf->prog, extack, nvdev);

		if (ret)
			return ret;

		ret = netvsc_vf_setxdp(vf_netdev, bpf->prog);

		if (ret) {
			netdev_err(dev, "vf_setxdp failed:%d\n", ret);
			NL_SET_ERR_MSG_MOD(extack, "vf_setxdp failed");

			netvsc_xdp_set(dev, NULL, extack, nvdev);
		}

		return ret;

	default:
		return -EINVAL;
	}
}

static int netvsc_ndoxdp_xmit_fm(struct net_device *ndev,
				 struct xdp_frame *frame, u16 q_idx)
{
	struct sk_buff *skb;

	skb = xdp_build_skb_from_frame(frame, ndev);
	if (unlikely(!skb))
		return -ENOMEM;

	netvsc_get_hash(skb, netdev_priv(ndev));

	skb_record_rx_queue(skb, q_idx);

	netvsc_xdp_xmit(skb, ndev);

	return 0;
}

int netvsc_ndoxdp_xmit(struct net_device *ndev, int n,
		       struct xdp_frame **frames, u32 flags)
{
	struct net_device_context *ndev_ctx = netdev_priv(ndev);
	const struct net_device_ops *vf_ops;
	struct netvsc_stats_tx *tx_stats;
	struct netvsc_device *nvsc_dev;
	struct net_device *vf_netdev;
	int i, count = 0;
	u16 q_idx;

	/* Don't transmit if netvsc_device is gone */
	nvsc_dev = rcu_dereference_bh(ndev_ctx->nvdev);
	if (unlikely(!nvsc_dev || nvsc_dev->destroy))
		return 0;

	/* If VF is present and up then redirect packets to it.
	 * Skip the VF if it is marked down or has no carrier.
	 * If netpoll is in uses, then VF can not be used either.
	 */
	vf_netdev = rcu_dereference_bh(ndev_ctx->vf_netdev);
	if (vf_netdev && netif_running(vf_netdev) &&
	    netif_carrier_ok(vf_netdev) && !netpoll_tx_running(ndev) &&
	    vf_netdev->netdev_ops->ndo_xdp_xmit &&
	    ndev_ctx->data_path_is_vf) {
		vf_ops = vf_netdev->netdev_ops;
		return vf_ops->ndo_xdp_xmit(vf_netdev, n, frames, flags);
	}

	q_idx = smp_processor_id() % ndev->real_num_tx_queues;

	for (i = 0; i < n; i++) {
		if (netvsc_ndoxdp_xmit_fm(ndev, frames[i], q_idx))
			break;

		count++;
	}

	tx_stats = &nvsc_dev->chan_table[q_idx].tx_stats;

	u64_stats_update_begin(&tx_stats->syncp);
	tx_stats->xdp_xmit += count;
	u64_stats_update_end(&tx_stats->syncp);

	return count;
}
