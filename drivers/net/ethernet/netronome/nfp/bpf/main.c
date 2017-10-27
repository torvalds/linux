/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <net/pkt_cls.h>

#include "../nfpcore/nfp_cpp.h"
#include "../nfp_app.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "../nfp_port.h"
#include "main.h"

static bool nfp_net_ebpf_capable(struct nfp_net *nn)
{
	if (nn->cap & NFP_NET_CFG_CTRL_BPF &&
	    nn_readb(nn, NFP_NET_CFG_BPF_ABI) == NFP_NET_BPF_ABI)
		return true;
	return false;
}

static int
nfp_bpf_xdp_offload(struct nfp_app *app, struct nfp_net *nn,
		    struct bpf_prog *prog)
{
	struct tc_cls_bpf_offload cmd = {
		.prog = prog,
	};
	int ret;

	if (!nfp_net_ebpf_capable(nn))
		return -EINVAL;

	if (nn->dp.ctrl & NFP_NET_CFG_CTRL_BPF) {
		if (!nn->dp.bpf_offload_xdp)
			return prog ? -EBUSY : 0;
		cmd.command = prog ? TC_CLSBPF_REPLACE : TC_CLSBPF_DESTROY;
	} else {
		if (!prog)
			return 0;
		cmd.command = TC_CLSBPF_ADD;
	}

	ret = nfp_net_bpf_offload(nn, &cmd);
	/* Stop offload if replace not possible */
	if (ret && cmd.command == TC_CLSBPF_REPLACE)
		nfp_bpf_xdp_offload(app, nn, NULL);
	nn->dp.bpf_offload_xdp = prog && !ret;
	return ret;
}

static const char *nfp_bpf_extra_cap(struct nfp_app *app, struct nfp_net *nn)
{
	return nfp_net_ebpf_capable(nn) ? "BPF" : "";
}

static int
nfp_bpf_vnic_alloc(struct nfp_app *app, struct nfp_net *nn, unsigned int id)
{
	struct nfp_net_bpf_priv *priv;
	int ret;

	/* Limit to single port, otherwise it's just a NIC */
	if (id > 0) {
		nfp_warn(app->cpp,
			 "BPF NIC doesn't support more than one port right now\n");
		nn->port = nfp_port_alloc(app, NFP_PORT_INVALID, nn->dp.netdev);
		return PTR_ERR_OR_ZERO(nn->port);
	}

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	nn->app_priv = priv;
	spin_lock_init(&priv->rx_filter_lock);
	setup_timer(&priv->rx_filter_stats_timer,
		    nfp_net_filter_stats_timer, (unsigned long)nn);

	ret = nfp_app_nic_vnic_alloc(app, nn, id);
	if (ret)
		kfree(priv);

	return ret;
}

static void nfp_bpf_vnic_free(struct nfp_app *app, struct nfp_net *nn)
{
	if (nn->dp.bpf_offload_xdp)
		nfp_bpf_xdp_offload(app, nn, NULL);
	kfree(nn->app_priv);
}

static int nfp_bpf_setup_tc(struct nfp_app *app, struct net_device *netdev,
			    enum tc_setup_type type, void *type_data)
{
	struct tc_cls_bpf_offload *cls_bpf = type_data;
	struct nfp_net *nn = netdev_priv(netdev);

	if (type != TC_SETUP_CLSBPF || !nfp_net_ebpf_capable(nn) ||
	    !is_classid_clsact_ingress(cls_bpf->common.classid) ||
	    cls_bpf->common.protocol != htons(ETH_P_ALL) ||
	    cls_bpf->common.chain_index)
		return -EOPNOTSUPP;

	if (nn->dp.bpf_offload_xdp)
		return -EBUSY;

	return nfp_net_bpf_offload(nn, cls_bpf);
}

static bool nfp_bpf_tc_busy(struct nfp_app *app, struct nfp_net *nn)
{
	return nn->dp.ctrl & NFP_NET_CFG_CTRL_BPF;
}

const struct nfp_app_type app_bpf = {
	.id		= NFP_APP_BPF_NIC,
	.name		= "ebpf",

	.extra_cap	= nfp_bpf_extra_cap,

	.vnic_alloc	= nfp_bpf_vnic_alloc,
	.vnic_free	= nfp_bpf_vnic_free,

	.setup_tc	= nfp_bpf_setup_tc,
	.tc_busy	= nfp_bpf_tc_busy,
	.xdp_offload	= nfp_bpf_xdp_offload,
};
