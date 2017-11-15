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

#define pr_fmt(fmt)	"NFP net bpf: " fmt

#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/kernel.h>
#include <linux/pkt_cls.h>

#include "main.h"

/* Analyzer/verifier definitions */
struct nfp_bpf_analyzer_priv {
	struct nfp_prog *prog;
	struct nfp_insn_meta *meta;
};

static struct nfp_insn_meta *
nfp_bpf_goto_meta(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		  unsigned int insn_idx, unsigned int n_insns)
{
	unsigned int forward, backward, i;

	backward = meta->n - insn_idx;
	forward = insn_idx - meta->n;

	if (min(forward, backward) > n_insns - insn_idx - 1) {
		backward = n_insns - insn_idx - 1;
		meta = nfp_prog_last_meta(nfp_prog);
	}
	if (min(forward, backward) > insn_idx && backward > insn_idx) {
		forward = insn_idx;
		meta = nfp_prog_first_meta(nfp_prog);
	}

	if (forward < backward)
		for (i = 0; i < forward; i++)
			meta = nfp_meta_next(meta);
	else
		for (i = 0; i < backward; i++)
			meta = nfp_meta_prev(meta);

	return meta;
}

static int
nfp_bpf_check_exit(struct nfp_prog *nfp_prog,
		   const struct bpf_verifier_env *env)
{
	const struct bpf_reg_state *reg0 = &env->cur_state.regs[0];
	u64 imm;

	if (nfp_prog->act == NN_ACT_XDP)
		return 0;

	if (!(reg0->type == SCALAR_VALUE && tnum_is_const(reg0->var_off))) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg0->var_off);
		pr_info("unsupported exit state: %d, var_off: %s\n",
			reg0->type, tn_buf);
		return -EINVAL;
	}

	imm = reg0->var_off.value;
	if (nfp_prog->act != NN_ACT_DIRECT && imm != 0 && (imm & ~0U) != ~0U) {
		pr_info("unsupported exit state: %d, imm: %llx\n",
			reg0->type, imm);
		return -EINVAL;
	}

	if (nfp_prog->act == NN_ACT_DIRECT && imm <= TC_ACT_REDIRECT &&
	    imm != TC_ACT_SHOT && imm != TC_ACT_STOLEN &&
	    imm != TC_ACT_QUEUED) {
		pr_info("unsupported exit state: %d, imm: %llx\n",
			reg0->type, imm);
		return -EINVAL;
	}

	return 0;
}

static int
nfp_bpf_check_ctx_ptr(struct nfp_prog *nfp_prog,
		      const struct bpf_verifier_env *env, u8 reg)
{
	if (env->cur_state.regs[reg].type != PTR_TO_CTX)
		return -EINVAL;

	return 0;
}

static int
nfp_verify_insn(struct bpf_verifier_env *env, int insn_idx, int prev_insn_idx)
{
	struct nfp_bpf_analyzer_priv *priv = env->analyzer_priv;
	struct nfp_insn_meta *meta = priv->meta;

	meta = nfp_bpf_goto_meta(priv->prog, meta, insn_idx, env->prog->len);
	priv->meta = meta;

	if (meta->insn.src_reg == BPF_REG_10 ||
	    meta->insn.dst_reg == BPF_REG_10) {
		pr_err("stack not yet supported\n");
		return -EINVAL;
	}
	if (meta->insn.src_reg >= MAX_BPF_REG ||
	    meta->insn.dst_reg >= MAX_BPF_REG) {
		pr_err("program uses extended registers - jit hardening?\n");
		return -EINVAL;
	}

	if (meta->insn.code == (BPF_JMP | BPF_EXIT))
		return nfp_bpf_check_exit(priv->prog, env);

	if ((meta->insn.code & ~BPF_SIZE_MASK) == (BPF_LDX | BPF_MEM))
		return nfp_bpf_check_ctx_ptr(priv->prog, env,
					     meta->insn.src_reg);
	if ((meta->insn.code & ~BPF_SIZE_MASK) == (BPF_STX | BPF_MEM))
		return nfp_bpf_check_ctx_ptr(priv->prog, env,
					     meta->insn.dst_reg);

	return 0;
}

static const struct bpf_ext_analyzer_ops nfp_bpf_analyzer_ops = {
	.insn_hook = nfp_verify_insn,
};

int nfp_prog_verify(struct nfp_prog *nfp_prog, struct bpf_prog *prog)
{
	struct nfp_bpf_analyzer_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->prog = nfp_prog;
	priv->meta = nfp_prog_first_meta(nfp_prog);

	ret = bpf_analyzer(prog, &nfp_bpf_analyzer_ops, priv);

	kfree(priv);

	return ret;
}
