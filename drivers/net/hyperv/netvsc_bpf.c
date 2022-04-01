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
	void *data = nvchan->rsc.data[0];
	u32 len = nvchan->rsc.len[0];
	struct page *page = NULL;
	struct bpf_prog *prog;
	u32 act = XDP_PASS;

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

	if (page && act != XDP_PASS && act != XDP_TX) {
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
	bpf_op_t ndo_bpf;
	int ret;

	ASSERT_RTNL();

	if (!vf_netdev)
		return 0;

	ndo_bpf = vf_netdev->netdev_ops->ndo_bpf;
	if (!ndo_bpf)
		return 0;

	memset(&xdp, 0, sizeof(xdp));

	if (prog)
		bpf_prog_inc(prog);

	xdp.command = XDP_SETUP_PROG;
	xdp.prog = prog;

	ret = ndo_bpf(vf_netdev, &xdp);

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
