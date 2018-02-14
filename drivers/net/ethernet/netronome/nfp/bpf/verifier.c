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

#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/kernel.h>
#include <linux/pkt_cls.h>

#include "fw.h"
#include "main.h"

#define pr_vlog(env, fmt, ...)	\
	bpf_verifier_log_write(env, "[nfp] " fmt, ##__VA_ARGS__)

struct nfp_insn_meta *
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

static void
nfp_record_adjust_head(struct nfp_app_bpf *bpf, struct nfp_prog *nfp_prog,
		       struct nfp_insn_meta *meta,
		       const struct bpf_reg_state *reg2)
{
	unsigned int location =	UINT_MAX;
	int imm;

	/* Datapath usually can give us guarantees on how much adjust head
	 * can be done without the need for any checks.  Optimize the simple
	 * case where there is only one adjust head by a constant.
	 */
	if (reg2->type != SCALAR_VALUE || !tnum_is_const(reg2->var_off))
		goto exit_set_location;
	imm = reg2->var_off.value;
	/* Translator will skip all checks, we need to guarantee min pkt len */
	if (imm > ETH_ZLEN - ETH_HLEN)
		goto exit_set_location;
	if (imm > (int)bpf->adjust_head.guaranteed_add ||
	    imm < -bpf->adjust_head.guaranteed_sub)
		goto exit_set_location;

	if (nfp_prog->adjust_head_location) {
		/* Only one call per program allowed */
		if (nfp_prog->adjust_head_location != meta->n)
			goto exit_set_location;

		if (meta->arg2.var_off.value != imm)
			goto exit_set_location;
	}

	location = meta->n;
exit_set_location:
	nfp_prog->adjust_head_location = location;
}

static int
nfp_bpf_check_call(struct nfp_prog *nfp_prog, struct bpf_verifier_env *env,
		   struct nfp_insn_meta *meta)
{
	const struct bpf_reg_state *reg1 = cur_regs(env) + BPF_REG_1;
	const struct bpf_reg_state *reg2 = cur_regs(env) + BPF_REG_2;
	struct nfp_app_bpf *bpf = nfp_prog->bpf;
	u32 func_id = meta->insn.imm;
	s64 off, old_off;

	switch (func_id) {
	case BPF_FUNC_xdp_adjust_head:
		if (!bpf->adjust_head.off_max) {
			pr_vlog(env, "adjust_head not supported by FW\n");
			return -EOPNOTSUPP;
		}
		if (!(bpf->adjust_head.flags & NFP_BPF_ADJUST_HEAD_NO_META)) {
			pr_vlog(env, "adjust_head: FW requires shifting metadata, not supported by the driver\n");
			return -EOPNOTSUPP;
		}

		nfp_record_adjust_head(bpf, nfp_prog, meta, reg2);
		break;

	case BPF_FUNC_map_lookup_elem:
		if (!bpf->helpers.map_lookup) {
			pr_vlog(env, "map_lookup: not supported by FW\n");
			return -EOPNOTSUPP;
		}
		if (reg2->type != PTR_TO_STACK) {
			pr_vlog(env,
				"map_lookup: unsupported key ptr type %d\n",
				reg2->type);
			return -EOPNOTSUPP;
		}
		if (!tnum_is_const(reg2->var_off)) {
			pr_vlog(env, "map_lookup: variable key pointer\n");
			return -EOPNOTSUPP;
		}

		off = reg2->var_off.value + reg2->off;
		if (-off % 4) {
			pr_vlog(env,
				"map_lookup: unaligned stack pointer %lld\n",
				-off);
			return -EOPNOTSUPP;
		}

		/* Rest of the checks is only if we re-parse the same insn */
		if (!meta->func_id)
			break;

		old_off = meta->arg2.var_off.value + meta->arg2.off;
		meta->arg2_var_off |= off != old_off;

		if (meta->arg1.map_ptr != reg1->map_ptr) {
			pr_vlog(env, "map_lookup: called for different map\n");
			return -EOPNOTSUPP;
		}
		break;
	default:
		pr_vlog(env, "unsupported function id: %d\n", func_id);
		return -EOPNOTSUPP;
	}

	meta->func_id = func_id;
	meta->arg1 = *reg1;
	meta->arg2 = *reg2;

	return 0;
}

static int
nfp_bpf_check_exit(struct nfp_prog *nfp_prog,
		   struct bpf_verifier_env *env)
{
	const struct bpf_reg_state *reg0 = cur_regs(env) + BPF_REG_0;
	u64 imm;

	if (nfp_prog->type == BPF_PROG_TYPE_XDP)
		return 0;

	if (!(reg0->type == SCALAR_VALUE && tnum_is_const(reg0->var_off))) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg0->var_off);
		pr_vlog(env, "unsupported exit state: %d, var_off: %s\n",
			reg0->type, tn_buf);
		return -EINVAL;
	}

	imm = reg0->var_off.value;
	if (nfp_prog->type == BPF_PROG_TYPE_SCHED_CLS &&
	    imm <= TC_ACT_REDIRECT &&
	    imm != TC_ACT_SHOT && imm != TC_ACT_STOLEN &&
	    imm != TC_ACT_QUEUED) {
		pr_vlog(env, "unsupported exit state: %d, imm: %llx\n",
			reg0->type, imm);
		return -EINVAL;
	}

	return 0;
}

static int
nfp_bpf_check_stack_access(struct nfp_prog *nfp_prog,
			   struct nfp_insn_meta *meta,
			   const struct bpf_reg_state *reg,
			   struct bpf_verifier_env *env)
{
	s32 old_off, new_off;

	if (!tnum_is_const(reg->var_off)) {
		pr_vlog(env, "variable ptr stack access\n");
		return -EINVAL;
	}

	if (meta->ptr.type == NOT_INIT)
		return 0;

	old_off = meta->ptr.off + meta->ptr.var_off.value;
	new_off = reg->off + reg->var_off.value;

	meta->ptr_not_const |= old_off != new_off;

	if (!meta->ptr_not_const)
		return 0;

	if (old_off % 4 == new_off % 4)
		return 0;

	pr_vlog(env, "stack access changed location was:%d is:%d\n",
		old_off, new_off);
	return -EINVAL;
}

static int
nfp_bpf_check_ptr(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		  struct bpf_verifier_env *env, u8 reg_no)
{
	const struct bpf_reg_state *reg = cur_regs(env) + reg_no;
	int err;

	if (reg->type != PTR_TO_CTX &&
	    reg->type != PTR_TO_STACK &&
	    reg->type != PTR_TO_MAP_VALUE &&
	    reg->type != PTR_TO_PACKET) {
		pr_vlog(env, "unsupported ptr type: %d\n", reg->type);
		return -EINVAL;
	}

	if (reg->type == PTR_TO_STACK) {
		err = nfp_bpf_check_stack_access(nfp_prog, meta, reg, env);
		if (err)
			return err;
	}

	if (reg->type == PTR_TO_MAP_VALUE) {
		if (is_mbpf_store(meta)) {
			pr_vlog(env, "map writes not supported\n");
			return -EOPNOTSUPP;
		}
	}

	if (meta->ptr.type != NOT_INIT && meta->ptr.type != reg->type) {
		pr_vlog(env, "ptr type changed for instruction %d -> %d\n",
			meta->ptr.type, reg->type);
		return -EINVAL;
	}

	meta->ptr = *reg;

	return 0;
}

static int
nfp_verify_insn(struct bpf_verifier_env *env, int insn_idx, int prev_insn_idx)
{
	struct nfp_prog *nfp_prog = env->prog->aux->offload->dev_priv;
	struct nfp_insn_meta *meta = nfp_prog->verifier_meta;

	meta = nfp_bpf_goto_meta(nfp_prog, meta, insn_idx, env->prog->len);
	nfp_prog->verifier_meta = meta;

	if (!nfp_bpf_supported_opcode(meta->insn.code)) {
		pr_vlog(env, "instruction %#02x not supported\n",
			meta->insn.code);
		return -EINVAL;
	}

	if (meta->insn.src_reg >= MAX_BPF_REG ||
	    meta->insn.dst_reg >= MAX_BPF_REG) {
		pr_vlog(env, "program uses extended registers - jit hardening?\n");
		return -EINVAL;
	}

	if (meta->insn.code == (BPF_JMP | BPF_CALL))
		return nfp_bpf_check_call(nfp_prog, env, meta);
	if (meta->insn.code == (BPF_JMP | BPF_EXIT))
		return nfp_bpf_check_exit(nfp_prog, env);

	if (is_mbpf_load(meta))
		return nfp_bpf_check_ptr(nfp_prog, meta, env,
					 meta->insn.src_reg);
	if (is_mbpf_store(meta))
		return nfp_bpf_check_ptr(nfp_prog, meta, env,
					 meta->insn.dst_reg);

	return 0;
}

const struct bpf_prog_offload_ops nfp_bpf_analyzer_ops = {
	.insn_hook = nfp_verify_insn,
};
