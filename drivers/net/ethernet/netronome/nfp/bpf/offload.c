/*
 * Copyright (C) 2016-2017 Netronome Systems, Inc.
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

/*
 * nfp_net_offload.c
 * Netronome network device driver: TC offload functions for PF and VF
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/list.h>

#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_mirred.h>

#include "main.h"
#include "../nfp_net_ctrl.h"
#include "../nfp_net.h"

static int
nfp_prog_prepare(struct nfp_prog *nfp_prog, const struct bpf_insn *prog,
		 unsigned int cnt)
{
	struct nfp_insn_meta *meta;
	unsigned int i;

	for (i = 0; i < cnt; i++) {
		meta = kzalloc(sizeof(*meta), GFP_KERNEL);
		if (!meta)
			return -ENOMEM;

		meta->insn = prog[i];
		meta->n = i;

		list_add_tail(&meta->l, &nfp_prog->insns);
	}

	/* Another pass to record jump information. */
	list_for_each_entry(meta, &nfp_prog->insns, l) {
		u64 code = meta->insn.code;

		if (BPF_CLASS(code) == BPF_JMP && BPF_OP(code) != BPF_EXIT &&
		    BPF_OP(code) != BPF_CALL) {
			struct nfp_insn_meta *dst_meta;
			unsigned short dst_indx;

			dst_indx = meta->n + 1 + meta->insn.off;
			dst_meta = nfp_bpf_goto_meta(nfp_prog, meta, dst_indx,
						     cnt);

			meta->jmp_dst = dst_meta;
			dst_meta->flags |= FLAG_INSN_IS_JUMP_DST;
		}
	}

	return 0;
}

static void nfp_prog_free(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta, *tmp;

	list_for_each_entry_safe(meta, tmp, &nfp_prog->insns, l) {
		list_del(&meta->l);
		kfree(meta);
	}
	kfree(nfp_prog);
}

int nfp_bpf_verifier_prep(struct nfp_app *app, struct nfp_net *nn,
			  struct netdev_bpf *bpf)
{
	struct bpf_prog *prog = bpf->verifier.prog;
	struct nfp_prog *nfp_prog;
	int ret;

	nfp_prog = kzalloc(sizeof(*nfp_prog), GFP_KERNEL);
	if (!nfp_prog)
		return -ENOMEM;
	prog->aux->offload->dev_priv = nfp_prog;

	INIT_LIST_HEAD(&nfp_prog->insns);
	nfp_prog->type = prog->type;

	ret = nfp_prog_prepare(nfp_prog, prog->insnsi, prog->len);
	if (ret)
		goto err_free;

	nfp_prog->verifier_meta = nfp_prog_first_meta(nfp_prog);
	bpf->verifier.ops = &nfp_bpf_analyzer_ops;

	return 0;

err_free:
	nfp_prog_free(nfp_prog);

	return ret;
}

int nfp_bpf_translate(struct nfp_app *app, struct nfp_net *nn,
		      struct bpf_prog *prog)
{
	struct nfp_prog *nfp_prog = prog->aux->offload->dev_priv;
	unsigned int stack_size;
	unsigned int max_instr;

	stack_size = nn_readb(nn, NFP_NET_CFG_BPF_STACK_SZ) * 64;
	if (prog->aux->stack_depth > stack_size) {
		nn_info(nn, "stack too large: program %dB > FW stack %dB\n",
			prog->aux->stack_depth, stack_size);
		return -EOPNOTSUPP;
	}

	nfp_prog->stack_depth = prog->aux->stack_depth;
	nfp_prog->start_off = nn_readw(nn, NFP_NET_CFG_BPF_START);
	nfp_prog->tgt_done = nn_readw(nn, NFP_NET_CFG_BPF_DONE);

	max_instr = nn_readw(nn, NFP_NET_CFG_BPF_MAX_LEN);
	nfp_prog->__prog_alloc_len = max_instr * sizeof(u64);

	nfp_prog->prog = kmalloc(nfp_prog->__prog_alloc_len, GFP_KERNEL);
	if (!nfp_prog->prog)
		return -ENOMEM;

	return nfp_bpf_jit(nfp_prog);
}

int nfp_bpf_destroy(struct nfp_app *app, struct nfp_net *nn,
		    struct bpf_prog *prog)
{
	struct nfp_prog *nfp_prog = prog->aux->offload->dev_priv;

	kfree(nfp_prog->prog);
	nfp_prog_free(nfp_prog);

	return 0;
}

static int nfp_net_bpf_load(struct nfp_net *nn, struct bpf_prog *prog)
{
	struct nfp_prog *nfp_prog = prog->aux->offload->dev_priv;
	unsigned int max_mtu;
	dma_addr_t dma_addr;
	int err;

	max_mtu = nn_readb(nn, NFP_NET_CFG_BPF_INL_MTU) * 64 - 32;
	if (max_mtu < nn->dp.netdev->mtu) {
		nn_info(nn, "BPF offload not supported with MTU larger than HW packet split boundary\n");
		return -EOPNOTSUPP;
	}

	dma_addr = dma_map_single(nn->dp.dev, nfp_prog->prog,
				  nfp_prog->prog_len * sizeof(u64),
				  DMA_TO_DEVICE);
	if (dma_mapping_error(nn->dp.dev, dma_addr))
		return -ENOMEM;

	nn_writew(nn, NFP_NET_CFG_BPF_SIZE, nfp_prog->prog_len);
	nn_writeq(nn, NFP_NET_CFG_BPF_ADDR, dma_addr);

	/* Load up the JITed code */
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_BPF);
	if (err)
		nn_err(nn, "FW command error while loading BPF: %d\n", err);

	dma_unmap_single(nn->dp.dev, dma_addr, nfp_prog->prog_len * sizeof(u64),
			 DMA_TO_DEVICE);

	return err;
}

static void nfp_net_bpf_start(struct nfp_net *nn)
{
	int err;

	/* Enable passing packets through BPF function */
	nn->dp.ctrl |= NFP_NET_CFG_CTRL_BPF;
	nn_writel(nn, NFP_NET_CFG_CTRL, nn->dp.ctrl);
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN);
	if (err)
		nn_err(nn, "FW command error while enabling BPF: %d\n", err);
}

static int nfp_net_bpf_stop(struct nfp_net *nn)
{
	if (!(nn->dp.ctrl & NFP_NET_CFG_CTRL_BPF))
		return 0;

	nn->dp.ctrl &= ~NFP_NET_CFG_CTRL_BPF;
	nn_writel(nn, NFP_NET_CFG_CTRL, nn->dp.ctrl);

	return nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN);
}

int nfp_net_bpf_offload(struct nfp_net *nn, struct bpf_prog *prog,
			bool old_prog)
{
	int err;

	if (prog) {
		struct bpf_dev_offload *offload = prog->aux->offload;

		if (!offload)
			return -EINVAL;
		if (offload->netdev != nn->dp.netdev)
			return -EINVAL;
	}

	if (prog && old_prog) {
		u8 cap;

		cap = nn_readb(nn, NFP_NET_CFG_BPF_CAP);
		if (!(cap & NFP_NET_BPF_CAP_RELO)) {
			nn_err(nn, "FW does not support live reload\n");
			return -EBUSY;
		}
	}

	/* Something else is loaded, different program type? */
	if (!old_prog && nn->dp.ctrl & NFP_NET_CFG_CTRL_BPF)
		return -EBUSY;

	if (old_prog && !prog)
		return nfp_net_bpf_stop(nn);

	err = nfp_net_bpf_load(nn, prog);
	if (err)
		return err;

	if (!old_prog)
		nfp_net_bpf_start(nn);

	return 0;
}
