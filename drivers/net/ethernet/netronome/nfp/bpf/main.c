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
#ifdef __LITTLE_ENDIAN
	if (nn->cap & NFP_NET_CFG_CTRL_BPF &&
	    nn_readb(nn, NFP_NET_CFG_BPF_ABI) == NFP_NET_BPF_ABI)
		return true;
#endif
	return false;
}

static int
nfp_bpf_xdp_offload(struct nfp_app *app, struct nfp_net *nn,
		    struct bpf_prog *prog)
{
	bool running, xdp_running;
	int ret;

	if (!nfp_net_ebpf_capable(nn))
		return -EINVAL;

	running = nn->dp.ctrl & NFP_NET_CFG_CTRL_BPF;
	xdp_running = running && nn->dp.bpf_offload_xdp;

	if (!prog && !xdp_running)
		return 0;
	if (prog && running && !xdp_running)
		return -EBUSY;

	ret = nfp_net_bpf_offload(nn, prog, running);
	/* Stop offload if replace not possible */
	if (ret && prog)
		nfp_bpf_xdp_offload(app, nn, NULL);

	nn->dp.bpf_offload_xdp = prog && !ret;
	return ret;
}

static const char *nfp_bpf_extra_cap(struct nfp_app *app, struct nfp_net *nn)
{
	return nfp_net_ebpf_capable(nn) ? "BPF" : "";
}

static void nfp_bpf_vnic_free(struct nfp_app *app, struct nfp_net *nn)
{
	if (nn->dp.bpf_offload_xdp)
		nfp_bpf_xdp_offload(app, nn, NULL);
}

static int nfp_bpf_setup_tc_block_cb(enum tc_setup_type type,
				     void *type_data, void *cb_priv)
{
	struct tc_cls_bpf_offload *cls_bpf = type_data;
	struct nfp_net *nn = cb_priv;

	if (type != TC_SETUP_CLSBPF ||
	    !tc_can_offload(nn->dp.netdev) ||
	    !nfp_net_ebpf_capable(nn) ||
	    cls_bpf->common.protocol != htons(ETH_P_ALL) ||
	    cls_bpf->common.chain_index)
		return -EOPNOTSUPP;
	if (nn->dp.bpf_offload_xdp)
		return -EBUSY;

	/* Only support TC direct action */
	if (!cls_bpf->exts_integrated ||
	    tcf_exts_has_actions(cls_bpf->exts)) {
		nn_err(nn, "only direct action with no legacy actions supported\n");
		return -EOPNOTSUPP;
	}

	switch (cls_bpf->command) {
	case TC_CLSBPF_REPLACE:
		return nfp_net_bpf_offload(nn, cls_bpf->prog, true);
	case TC_CLSBPF_ADD:
		return nfp_net_bpf_offload(nn, cls_bpf->prog, false);
	case TC_CLSBPF_DESTROY:
		return nfp_net_bpf_offload(nn, NULL, true);
	default:
		return -EOPNOTSUPP;
	}
}

static int nfp_bpf_setup_tc_block(struct net_device *netdev,
				  struct tc_block_offload *f)
{
	struct nfp_net *nn = netdev_priv(netdev);

	if (f->binder_type != TCF_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	switch (f->command) {
	case TC_BLOCK_BIND:
		return tcf_block_cb_register(f->block,
					     nfp_bpf_setup_tc_block_cb,
					     nn, nn);
	case TC_BLOCK_UNBIND:
		tcf_block_cb_unregister(f->block,
					nfp_bpf_setup_tc_block_cb,
					nn);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int nfp_bpf_setup_tc(struct nfp_app *app, struct net_device *netdev,
			    enum tc_setup_type type, void *type_data)
{
	switch (type) {
	case TC_SETUP_BLOCK:
		return nfp_bpf_setup_tc_block(netdev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static bool nfp_bpf_tc_busy(struct nfp_app *app, struct nfp_net *nn)
{
	return nn->dp.ctrl & NFP_NET_CFG_CTRL_BPF;
}

const struct nfp_app_type app_bpf = {
	.id		= NFP_APP_BPF_NIC,
	.name		= "ebpf",

	.extra_cap	= nfp_bpf_extra_cap,

	.vnic_alloc	= nfp_app_nic_vnic_alloc,
	.vnic_free	= nfp_bpf_vnic_free,

	.setup_tc	= nfp_bpf_setup_tc,
	.tc_busy	= nfp_bpf_tc_busy,
	.xdp_offload	= nfp_bpf_xdp_offload,

	.bpf_verifier_prep	= nfp_bpf_verifier_prep,
	.bpf_translate		= nfp_bpf_translate,
	.bpf_destroy		= nfp_bpf_destroy,
};
