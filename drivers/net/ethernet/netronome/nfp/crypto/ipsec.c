// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc */
/* Copyright (C) 2021 Corigine, Inc */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <asm/unaligned.h>
#include <linux/ktime.h>
#include <net/xfrm.h>

#include "../nfp_net_ctrl.h"
#include "../nfp_net.h"
#include "crypto.h"

#define NFP_NET_IPSEC_MAX_SA_CNT  (16 * 1024) /* Firmware support a maximum of 16K SA offload */

static int nfp_net_xfrm_add_state(struct xfrm_state *x)
{
	return -EOPNOTSUPP;
}

static void nfp_net_xfrm_del_state(struct xfrm_state *x)
{
}

static bool nfp_net_ipsec_offload_ok(struct sk_buff *skb, struct xfrm_state *x)
{
	return false;
}

static const struct xfrmdev_ops nfp_net_ipsec_xfrmdev_ops = {
	.xdo_dev_state_add = nfp_net_xfrm_add_state,
	.xdo_dev_state_delete = nfp_net_xfrm_del_state,
	.xdo_dev_offload_ok = nfp_net_ipsec_offload_ok,
};

void nfp_net_ipsec_init(struct nfp_net *nn)
{
	if (!(nn->cap_w1 & NFP_NET_CFG_CTRL_IPSEC))
		return;

	xa_init_flags(&nn->xa_ipsec, XA_FLAGS_ALLOC);
	nn->dp.netdev->xfrmdev_ops = &nfp_net_ipsec_xfrmdev_ops;
}

void nfp_net_ipsec_clean(struct nfp_net *nn)
{
	if (!(nn->cap_w1 & NFP_NET_CFG_CTRL_IPSEC))
		return;

	WARN_ON(!xa_empty(&nn->xa_ipsec));
	xa_destroy(&nn->xa_ipsec);
}

bool nfp_net_ipsec_tx_prep(struct nfp_net_dp *dp, struct sk_buff *skb,
			   struct nfp_ipsec_offload *offload_info)
{
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct xfrm_state *x;

	x = xfrm_input_state(skb);
	if (!x)
		return false;

	offload_info->seq_hi = xo->seq.hi;
	offload_info->seq_low = xo->seq.low;
	offload_info->handle = x->xso.offload_handle;

	return true;
}

int nfp_net_ipsec_rx(struct nfp_meta_parsed *meta, struct sk_buff *skb)
{
	struct net_device *netdev = skb->dev;
	struct xfrm_offload *xo;
	struct xfrm_state *x;
	struct sec_path *sp;
	struct nfp_net *nn;
	u32 saidx;

	nn = netdev_priv(netdev);

	saidx = meta->ipsec_saidx - 1;
	if (saidx >= NFP_NET_IPSEC_MAX_SA_CNT)
		return -EINVAL;

	sp = secpath_set(skb);
	if (unlikely(!sp))
		return -ENOMEM;

	xa_lock(&nn->xa_ipsec);
	x = xa_load(&nn->xa_ipsec, saidx);
	xa_unlock(&nn->xa_ipsec);
	if (!x)
		return -EINVAL;

	xfrm_state_hold(x);
	sp->xvec[sp->len++] = x;
	sp->olen++;
	xo = xfrm_offload(skb);
	xo->flags = CRYPTO_DONE;
	xo->status = CRYPTO_SUCCESS;

	return 0;
}
