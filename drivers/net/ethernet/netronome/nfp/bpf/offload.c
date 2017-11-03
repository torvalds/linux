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

static int
nfp_net_bpf_offload_prepare(struct nfp_net *nn,
			    struct tc_cls_bpf_offload *cls_bpf,
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
	if (cls_bpf->prog->aux->stack_depth > stack_size) {
		nn_info(nn, "stack too large: program %dB > FW stack %dB\n",
			cls_bpf->prog->aux->stack_depth, stack_size);
		return -EOPNOTSUPP;
	}

	*code = dma_zalloc_coherent(nn->dp.dev, code_sz, dma_addr, GFP_KERNEL);
	if (!*code)
		return -ENOMEM;

	ret = nfp_bpf_jit(cls_bpf->prog, *code, start_off, done_off,
			  max_instr, res);
	if (ret)
		goto out;

	return 0;

out:
	dma_free_coherent(nn->dp.dev, code_sz, *code, *dma_addr);
	return ret;
}

static void
nfp_net_bpf_load_and_start(struct nfp_net *nn, u32 tc_flags,
			   void *code, dma_addr_t dma_addr,
			   unsigned int code_sz, unsigned int n_instr)
{
	int err;

	nn->dp.bpf_offload_skip_sw = !!(tc_flags & TCA_CLS_FLAGS_SKIP_SW);

	nn_writew(nn, NFP_NET_CFG_BPF_SIZE, n_instr);
	nn_writeq(nn, NFP_NET_CFG_BPF_ADDR, dma_addr);

	/* Load up the JITed code */
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_BPF);
	if (err)
		nn_err(nn, "FW command error while loading BPF: %d\n", err);

	/* Enable passing packets through BPF function */
	nn->dp.ctrl |= NFP_NET_CFG_CTRL_BPF;
	nn_writel(nn, NFP_NET_CFG_CTRL, nn->dp.ctrl);
	err = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN);
	if (err)
		nn_err(nn, "FW command error while enabling BPF: %d\n", err);

	dma_free_coherent(nn->dp.dev, code_sz, code, dma_addr);
}

static int nfp_net_bpf_stop(struct nfp_net *nn)
{
	if (!(nn->dp.ctrl & NFP_NET_CFG_CTRL_BPF))
		return 0;

	nn->dp.ctrl &= ~NFP_NET_CFG_CTRL_BPF;
	nn_writel(nn, NFP_NET_CFG_CTRL, nn->dp.ctrl);
	nn->dp.bpf_offload_skip_sw = 0;

	return nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_GEN);
}

int nfp_net_bpf_offload(struct nfp_net *nn, struct tc_cls_bpf_offload *cls_bpf)
{
	struct nfp_bpf_result res;
	dma_addr_t dma_addr;
	u16 max_instr;
	void *code;
	int err;

	max_instr = nn_readw(nn, NFP_NET_CFG_BPF_MAX_LEN);

	switch (cls_bpf->command) {
	case TC_CLSBPF_REPLACE:
		/* There is nothing stopping us from implementing seamless
		 * replace but the simple method of loading I adopted in
		 * the firmware does not handle atomic replace (i.e. we have to
		 * stop the BPF offload and re-enable it).  Leaking-in a few
		 * frames which didn't have BPF applied in the hardware should
		 * be fine if software fallback is available, though.
		 */
		if (nn->dp.bpf_offload_skip_sw)
			return -EBUSY;

		err = nfp_net_bpf_offload_prepare(nn, cls_bpf, &res, &code,
						  &dma_addr, max_instr);
		if (err)
			return err;

		nfp_net_bpf_stop(nn);
		nfp_net_bpf_load_and_start(nn, cls_bpf->gen_flags, code,
					   dma_addr, max_instr * sizeof(u64),
					   res.n_instr);
		return 0;

	case TC_CLSBPF_ADD:
		if (nn->dp.ctrl & NFP_NET_CFG_CTRL_BPF)
			return -EBUSY;

		err = nfp_net_bpf_offload_prepare(nn, cls_bpf, &res, &code,
						  &dma_addr, max_instr);
		if (err)
			return err;

		nfp_net_bpf_load_and_start(nn, cls_bpf->gen_flags, code,
					   dma_addr, max_instr * sizeof(u64),
					   res.n_instr);
		return 0;

	case TC_CLSBPF_DESTROY:
		return nfp_net_bpf_stop(nn);

	default:
		return -EOPNOTSUPP;
	}
}
