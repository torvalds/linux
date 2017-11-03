/*
 * Copyright (C) 2016 Netronome Systems, Inc.
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

int
nfp_prog_prepare(struct nfp_prog *nfp_prog, const struct bpf_insn *prog,
		 unsigned int cnt)
{
	unsigned int i;

	for (i = 0; i < cnt; i++) {
		struct nfp_insn_meta *meta;

		meta = kzalloc(sizeof(*meta), GFP_KERNEL);
		if (!meta)
			return -ENOMEM;

		meta->insn = prog[i];
		meta->n = i;

		list_add_tail(&meta->l, &nfp_prog->insns);
	}

	return 0;
}

void nfp_prog_free(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta, *tmp;

	list_for_each_entry_safe(meta, tmp, &nfp_prog->insns, l) {
		list_del(&meta->l);
		kfree(meta);
	}
	kfree(nfp_prog);
}

static int
nfp_net_bpf_offload_prepare(struct nfp_net *nn, struct bpf_prog *prog,
			    struct nfp_bpf_result *res,
			    void **code, dma_addr_t *dma_addr, u16 max_instr)
{
	unsigned int code_sz = max_instr * sizeof(u64);
	unsigned int stack_size;
	u16 start_off, done_off;
	unsigned int max_mtu;
	int ret;

	max_mtu = nn_readb(nn, NFP_NET_CFG_BPF_INL_MTU) * 64 - 32;
	if (max_mtu < nn->dp.netdev->mtu) {
		nn_info(nn, "BPF offload not supported with MTU larger than HW packet split boundary\n");
		return -EOPNOTSUPP;
	}

	start_off = nn_readw(nn, NFP_NET_CFG_BPF_START);
	done_off = nn_readw(nn, NFP_NET_CFG_BPF_DONE);

	stack_size = nn_readb(nn, NFP_NET_CFG_BPF_STACK_SZ) * 64;
	if (prog->aux->stack_depth > stack_size) {
		nn_info(nn, "stack too large: program %dB > FW stack %dB\n",
			prog->aux->stack_depth, stack_size);
		return -EOPNOTSUPP;
	}

	*code = dma_zalloc_coherent(nn->dp.dev, code_sz, dma_addr, GFP_KERNEL);
	if (!*code)
		return -ENOMEM;

	ret = nfp_bpf_jit(prog, *code, start_off, done_off, max_instr, res);
	if (ret)
		goto out;

	return 0;

out:
	dma_free_coherent(nn->dp.dev, code_sz, *code, *dma_addr);
	return ret;
}

static void
nfp_net_bpf_load(struct nfp_net *nn, void *code, dma_addr_t dma_addr,
		 unsigned int code_sz, unsigned int n_instr)
{
	int err;

	nn_writew(nn, NFP_NET_CFG_BPF_SIZE, n_instr);
	nn_writeq(nn, NFP_NET_CFG_BPF_ADDR, dma_addr);

	/* Load up the JITed code */
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_BPF);
	if (err)
		nn_err(nn, "FW command error while loading BPF: %d\n", err);

	dma_free_coherent(nn->dp.dev, code_sz, code, dma_addr);
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
	struct nfp_bpf_result res;
	dma_addr_t dma_addr;
	u16 max_instr;
	void *code;
	int err;

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

	max_instr = nn_readw(nn, NFP_NET_CFG_BPF_MAX_LEN);

	err = nfp_net_bpf_offload_prepare(nn, prog, &res, &code, &dma_addr,
					  max_instr);
	if (err)
		return err;

	nfp_net_bpf_load(nn, code, dma_addr, max_instr * sizeof(u64),
			 res.n_instr);
	if (!old_prog)
		nfp_net_bpf_start(nn);

	return 0;
}
