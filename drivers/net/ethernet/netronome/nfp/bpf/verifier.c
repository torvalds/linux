// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2016-2018 Netronome Systems, Inc. */

#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/pkt_cls.h>

#include "../nfp_app.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "fw.h"
#include "main.h"

#define pr_vlog(env, fmt, ...)	\
	bpf_verifier_log_write(env, "[nfp] " fmt, ##__VA_ARGS__)

struct nfp_insn_meta *
nfp_bpf_goto_meta(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		  unsigned int insn_idx)
{
	unsigned int forward, backward, i;

	backward = meta->n - insn_idx;
	forward = insn_idx - meta->n;

	if (min(forward, backward) > nfp_prog->n_insns - insn_idx - 1) {
		backward = nfp_prog->n_insns - insn_idx - 1;
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

		if (meta->arg2.reg.var_off.value != imm)
			goto exit_set_location;
	}

	location = meta->n;
exit_set_location:
	nfp_prog->adjust_head_location = location;
}

static bool nfp_bpf_map_update_value_ok(struct bpf_verifier_env *env)
{
	const struct bpf_reg_state *reg1 = cur_regs(env) + BPF_REG_1;
	const struct bpf_reg_state *reg3 = cur_regs(env) + BPF_REG_3;
	struct bpf_offloaded_map *offmap;
	struct bpf_func_state *state;
	struct nfp_bpf_map *nfp_map;
	int off, i;

	state = env->cur_state->frame[reg3->frameno];

	/* We need to record each time update happens with non-zero words,
	 * in case such word is used in atomic operations.
	 * Implicitly depend on nfp_bpf_stack_arg_ok(reg3) being run before.
	 */

	offmap = map_to_offmap(reg1->map_ptr);
	nfp_map = offmap->dev_priv;
	off = reg3->off + reg3->var_off.value;

	for (i = 0; i < offmap->map.value_size; i++) {
		struct bpf_stack_state *stack_entry;
		unsigned int soff;

		soff = -(off + i) - 1;
		stack_entry = &state->stack[soff / BPF_REG_SIZE];
		if (stack_entry->slot_type[soff % BPF_REG_SIZE] == STACK_ZERO)
			continue;

		if (nfp_map->use_map[i / 4].type == NFP_MAP_USE_ATOMIC_CNT) {
			pr_vlog(env, "value at offset %d/%d may be non-zero, bpf_map_update_elem() is required to initialize atomic counters to zero to avoid offload endian issues\n",
				i, soff);
			return false;
		}
		nfp_map->use_map[i / 4].non_zero_update = 1;
	}

	return true;
}

static int
nfp_bpf_stack_arg_ok(const char *fname, struct bpf_verifier_env *env,
		     const struct bpf_reg_state *reg,
		     struct nfp_bpf_reg_state *old_arg)
{
	s64 off, old_off;

	if (reg->type != PTR_TO_STACK) {
		pr_vlog(env, "%s: unsupported ptr type %d\n",
			fname, reg->type);
		return false;
	}
	if (!tnum_is_const(reg->var_off)) {
		pr_vlog(env, "%s: variable pointer\n", fname);
		return false;
	}

	off = reg->var_off.value + reg->off;
	if (-off % 4) {
		pr_vlog(env, "%s: unaligned stack pointer %lld\n", fname, -off);
		return false;
	}

	/* Rest of the checks is only if we re-parse the same insn */
	if (!old_arg)
		return true;

	old_off = old_arg->reg.var_off.value + old_arg->reg.off;
	old_arg->var_off |= off != old_off;

	return true;
}

static bool
nfp_bpf_map_call_ok(const char *fname, struct bpf_verifier_env *env,
		    struct nfp_insn_meta *meta,
		    u32 helper_tgt, const struct bpf_reg_state *reg1)
{
	if (!helper_tgt) {
		pr_vlog(env, "%s: not supported by FW\n", fname);
		return false;
	}

	return true;
}

static int
nfp_bpf_check_helper_call(struct nfp_prog *nfp_prog,
			  struct bpf_verifier_env *env,
			  struct nfp_insn_meta *meta)
{
	const struct bpf_reg_state *reg1 = cur_regs(env) + BPF_REG_1;
	const struct bpf_reg_state *reg2 = cur_regs(env) + BPF_REG_2;
	const struct bpf_reg_state *reg3 = cur_regs(env) + BPF_REG_3;
	struct nfp_app_bpf *bpf = nfp_prog->bpf;
	u32 func_id = meta->insn.imm;

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

	case BPF_FUNC_xdp_adjust_tail:
		if (!bpf->adjust_tail) {
			pr_vlog(env, "adjust_tail not supported by FW\n");
			return -EOPNOTSUPP;
		}
		break;

	case BPF_FUNC_map_lookup_elem:
		if (!nfp_bpf_map_call_ok("map_lookup", env, meta,
					 bpf->helpers.map_lookup, reg1) ||
		    !nfp_bpf_stack_arg_ok("map_lookup", env, reg2,
					  meta->func_id ? &meta->arg2 : NULL))
			return -EOPNOTSUPP;
		break;

	case BPF_FUNC_map_update_elem:
		if (!nfp_bpf_map_call_ok("map_update", env, meta,
					 bpf->helpers.map_update, reg1) ||
		    !nfp_bpf_stack_arg_ok("map_update", env, reg2,
					  meta->func_id ? &meta->arg2 : NULL) ||
		    !nfp_bpf_stack_arg_ok("map_update", env, reg3, NULL) ||
		    !nfp_bpf_map_update_value_ok(env))
			return -EOPNOTSUPP;
		break;

	case BPF_FUNC_map_delete_elem:
		if (!nfp_bpf_map_call_ok("map_delete", env, meta,
					 bpf->helpers.map_delete, reg1) ||
		    !nfp_bpf_stack_arg_ok("map_delete", env, reg2,
					  meta->func_id ? &meta->arg2 : NULL))
			return -EOPNOTSUPP;
		break;

	case BPF_FUNC_get_prandom_u32:
		if (bpf->pseudo_random)
			break;
		pr_vlog(env, "bpf_get_prandom_u32(): FW doesn't support random number generation\n");
		return -EOPNOTSUPP;

	case BPF_FUNC_perf_event_output:
		BUILD_BUG_ON(NFP_BPF_SCALAR_VALUE != SCALAR_VALUE ||
			     NFP_BPF_MAP_VALUE != PTR_TO_MAP_VALUE ||
			     NFP_BPF_STACK != PTR_TO_STACK ||
			     NFP_BPF_PACKET_DATA != PTR_TO_PACKET);

		if (!bpf->helpers.perf_event_output) {
			pr_vlog(env, "event_output: not supported by FW\n");
			return -EOPNOTSUPP;
		}

		/* Force current CPU to make sure we can report the event
		 * wherever we get the control message from FW.
		 */
		if (reg3->var_off.mask & BPF_F_INDEX_MASK ||
		    (reg3->var_off.value & BPF_F_INDEX_MASK) !=
		    BPF_F_CURRENT_CPU) {
			char tn_buf[48];

			tnum_strn(tn_buf, sizeof(tn_buf), reg3->var_off);
			pr_vlog(env, "event_output: must use BPF_F_CURRENT_CPU, var_off: %s\n",
				tn_buf);
			return -EOPNOTSUPP;
		}

		/* Save space in meta, we don't care about arguments other
		 * than 4th meta, shove it into arg1.
		 */
		reg1 = cur_regs(env) + BPF_REG_4;

		if (reg1->type != SCALAR_VALUE /* NULL ptr */ &&
		    reg1->type != PTR_TO_STACK &&
		    reg1->type != PTR_TO_MAP_VALUE &&
		    reg1->type != PTR_TO_PACKET) {
			pr_vlog(env, "event_output: unsupported ptr type: %d\n",
				reg1->type);
			return -EOPNOTSUPP;
		}

		if (reg1->type == PTR_TO_STACK &&
		    !nfp_bpf_stack_arg_ok("event_output", env, reg1, NULL))
			return -EOPNOTSUPP;

		/* Warn user that on offload NFP may return success even if map
		 * is not going to accept the event, since the event output is
		 * fully async and device won't know the state of the map.
		 * There is also FW limitation on the event length.
		 *
		 * Lost events will not show up on the perf ring, driver
		 * won't see them at all.  Events may also get reordered.
		 */
		dev_warn_once(&nfp_prog->bpf->app->pf->pdev->dev,
			      "bpf: note: return codes and behavior of bpf_event_output() helper differs for offloaded programs!\n");
		pr_vlog(env, "warning: return codes and behavior of event_output helper differ for offload!\n");

		if (!meta->func_id)
			break;

		if (reg1->type != meta->arg1.type) {
			pr_vlog(env, "event_output: ptr type changed: %d %d\n",
				meta->arg1.type, reg1->type);
			return -EINVAL;
		}
		break;

	default:
		pr_vlog(env, "unsupported function id: %d\n", func_id);
		return -EOPNOTSUPP;
	}

	meta->func_id = func_id;
	meta->arg1 = *reg1;
	meta->arg2.reg = *reg2;

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

	if (reg->frameno != env->cur_state->curframe)
		meta->flags |= FLAG_INSN_PTR_CALLER_STACK_FRAME;

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

static const char *nfp_bpf_map_use_name(enum nfp_bpf_map_use use)
{
	static const char * const names[] = {
		[NFP_MAP_UNUSED]	= "unused",
		[NFP_MAP_USE_READ]	= "read",
		[NFP_MAP_USE_WRITE]	= "write",
		[NFP_MAP_USE_ATOMIC_CNT] = "atomic",
	};

	if (use >= ARRAY_SIZE(names) || !names[use])
		return "unknown";
	return names[use];
}

static int
nfp_bpf_map_mark_used_one(struct bpf_verifier_env *env,
			  struct nfp_bpf_map *nfp_map,
			  unsigned int off, enum nfp_bpf_map_use use)
{
	if (nfp_map->use_map[off / 4].type != NFP_MAP_UNUSED &&
	    nfp_map->use_map[off / 4].type != use) {
		pr_vlog(env, "map value use type conflict %s vs %s off: %u\n",
			nfp_bpf_map_use_name(nfp_map->use_map[off / 4].type),
			nfp_bpf_map_use_name(use), off);
		return -EOPNOTSUPP;
	}

	if (nfp_map->use_map[off / 4].non_zero_update &&
	    use == NFP_MAP_USE_ATOMIC_CNT) {
		pr_vlog(env, "atomic counter in map value may already be initialized to non-zero value off: %u\n",
			off);
		return -EOPNOTSUPP;
	}

	nfp_map->use_map[off / 4].type = use;

	return 0;
}

static int
nfp_bpf_map_mark_used(struct bpf_verifier_env *env, struct nfp_insn_meta *meta,
		      const struct bpf_reg_state *reg,
		      enum nfp_bpf_map_use use)
{
	struct bpf_offloaded_map *offmap;
	struct nfp_bpf_map *nfp_map;
	unsigned int size, off;
	int i, err;

	if (!tnum_is_const(reg->var_off)) {
		pr_vlog(env, "map value offset is variable\n");
		return -EOPNOTSUPP;
	}

	off = reg->var_off.value + meta->insn.off + reg->off;
	size = BPF_LDST_BYTES(&meta->insn);
	offmap = map_to_offmap(reg->map_ptr);
	nfp_map = offmap->dev_priv;

	if (off + size > offmap->map.value_size) {
		pr_vlog(env, "map value access out-of-bounds\n");
		return -EINVAL;
	}

	for (i = 0; i < size; i += 4 - (off + i) % 4) {
		err = nfp_bpf_map_mark_used_one(env, nfp_map, off + i, use);
		if (err)
			return err;
	}

	return 0;
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
		if (is_mbpf_load(meta)) {
			err = nfp_bpf_map_mark_used(env, meta, reg,
						    NFP_MAP_USE_READ);
			if (err)
				return err;
		}
		if (is_mbpf_store(meta)) {
			pr_vlog(env, "map writes not supported\n");
			return -EOPNOTSUPP;
		}
		if (is_mbpf_atomic(meta)) {
			err = nfp_bpf_map_mark_used(env, meta, reg,
						    NFP_MAP_USE_ATOMIC_CNT);
			if (err)
				return err;
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
nfp_bpf_check_store(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		    struct bpf_verifier_env *env)
{
	const struct bpf_reg_state *reg = cur_regs(env) + meta->insn.dst_reg;

	if (reg->type == PTR_TO_CTX) {
		if (nfp_prog->type == BPF_PROG_TYPE_XDP) {
			/* XDP ctx accesses must be 4B in size */
			switch (meta->insn.off) {
			case offsetof(struct xdp_md, rx_queue_index):
				if (nfp_prog->bpf->queue_select)
					goto exit_check_ptr;
				pr_vlog(env, "queue selection not supported by FW\n");
				return -EOPNOTSUPP;
			}
		}
		pr_vlog(env, "unsupported store to context field\n");
		return -EOPNOTSUPP;
	}
exit_check_ptr:
	return nfp_bpf_check_ptr(nfp_prog, meta, env, meta->insn.dst_reg);
}

static int
nfp_bpf_check_atomic(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		     struct bpf_verifier_env *env)
{
	const struct bpf_reg_state *sreg = cur_regs(env) + meta->insn.src_reg;
	const struct bpf_reg_state *dreg = cur_regs(env) + meta->insn.dst_reg;

	if (meta->insn.imm != BPF_ADD) {
		pr_vlog(env, "atomic op not implemented: %d\n", meta->insn.imm);
		return -EOPNOTSUPP;
	}

	if (dreg->type != PTR_TO_MAP_VALUE) {
		pr_vlog(env, "atomic add not to a map value pointer: %d\n",
			dreg->type);
		return -EOPNOTSUPP;
	}
	if (sreg->type != SCALAR_VALUE) {
		pr_vlog(env, "atomic add not of a scalar: %d\n", sreg->type);
		return -EOPNOTSUPP;
	}

	meta->xadd_over_16bit |=
		sreg->var_off.value > 0xffff || sreg->var_off.mask > 0xffff;
	meta->xadd_maybe_16bit |=
		(sreg->var_off.value & ~sreg->var_off.mask) <= 0xffff;

	return nfp_bpf_check_ptr(nfp_prog, meta, env, meta->insn.dst_reg);
}

static int
nfp_bpf_check_alu(struct nfp_prog *nfp_prog, struct nfp_insn_meta *meta,
		  struct bpf_verifier_env *env)
{
	const struct bpf_reg_state *sreg =
		cur_regs(env) + meta->insn.src_reg;
	const struct bpf_reg_state *dreg =
		cur_regs(env) + meta->insn.dst_reg;

	meta->umin_src = min(meta->umin_src, sreg->umin_value);
	meta->umax_src = max(meta->umax_src, sreg->umax_value);
	meta->umin_dst = min(meta->umin_dst, dreg->umin_value);
	meta->umax_dst = max(meta->umax_dst, dreg->umax_value);

	/* NFP supports u16 and u32 multiplication.
	 *
	 * For ALU64, if either operand is beyond u32's value range, we reject
	 * it. One thing to note, if the source operand is BPF_K, then we need
	 * to check "imm" field directly, and we'd reject it if it is negative.
	 * Because for ALU64, "imm" (with s32 type) is expected to be sign
	 * extended to s64 which NFP mul doesn't support.
	 *
	 * For ALU32, it is fine for "imm" be negative though, because the
	 * result is 32-bits and there is no difference on the low halve of
	 * the result for signed/unsigned mul, so we will get correct result.
	 */
	if (is_mbpf_mul(meta)) {
		if (meta->umax_dst > U32_MAX) {
			pr_vlog(env, "multiplier is not within u32 value range\n");
			return -EINVAL;
		}
		if (mbpf_src(meta) == BPF_X && meta->umax_src > U32_MAX) {
			pr_vlog(env, "multiplicand is not within u32 value range\n");
			return -EINVAL;
		}
		if (mbpf_class(meta) == BPF_ALU64 &&
		    mbpf_src(meta) == BPF_K && meta->insn.imm < 0) {
			pr_vlog(env, "sign extended multiplicand won't be within u32 value range\n");
			return -EINVAL;
		}
	}

	/* NFP doesn't have divide instructions, we support divide by constant
	 * through reciprocal multiplication. Given NFP support multiplication
	 * no bigger than u32, we'd require divisor and dividend no bigger than
	 * that as well.
	 *
	 * Also eBPF doesn't support signed divide and has enforced this on C
	 * language level by failing compilation. However LLVM assembler hasn't
	 * enforced this, so it is possible for negative constant to leak in as
	 * a BPF_K operand through assembly code, we reject such cases as well.
	 */
	if (is_mbpf_div(meta)) {
		if (meta->umax_dst > U32_MAX) {
			pr_vlog(env, "dividend is not within u32 value range\n");
			return -EINVAL;
		}
		if (mbpf_src(meta) == BPF_X) {
			if (meta->umin_src != meta->umax_src) {
				pr_vlog(env, "divisor is not constant\n");
				return -EINVAL;
			}
			if (meta->umax_src > U32_MAX) {
				pr_vlog(env, "divisor is not within u32 value range\n");
				return -EINVAL;
			}
		}
		if (mbpf_src(meta) == BPF_K && meta->insn.imm < 0) {
			pr_vlog(env, "divide by negative constant is not supported\n");
			return -EINVAL;
		}
	}

	return 0;
}

int nfp_verify_insn(struct bpf_verifier_env *env, int insn_idx,
		    int prev_insn_idx)
{
	struct nfp_prog *nfp_prog = env->prog->aux->offload->dev_priv;
	struct nfp_insn_meta *meta = nfp_prog->verifier_meta;

	meta = nfp_bpf_goto_meta(nfp_prog, meta, insn_idx);
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

	if (is_mbpf_helper_call(meta))
		return nfp_bpf_check_helper_call(nfp_prog, env, meta);
	if (meta->insn.code == (BPF_JMP | BPF_EXIT))
		return nfp_bpf_check_exit(nfp_prog, env);

	if (is_mbpf_load(meta))
		return nfp_bpf_check_ptr(nfp_prog, meta, env,
					 meta->insn.src_reg);
	if (is_mbpf_store(meta))
		return nfp_bpf_check_store(nfp_prog, meta, env);

	if (is_mbpf_atomic(meta))
		return nfp_bpf_check_atomic(nfp_prog, meta, env);

	if (is_mbpf_alu(meta))
		return nfp_bpf_check_alu(nfp_prog, meta, env);

	return 0;
}

static int
nfp_assign_subprog_idx_and_regs(struct bpf_verifier_env *env,
				struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta;
	int index = 0;

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		if (nfp_is_subprog_start(meta))
			index++;
		meta->subprog_idx = index;

		if (meta->insn.dst_reg >= BPF_REG_6 &&
		    meta->insn.dst_reg <= BPF_REG_9)
			nfp_prog->subprog[index].needs_reg_push = 1;
	}

	if (index + 1 != nfp_prog->subprog_cnt) {
		pr_vlog(env, "BUG: number of processed BPF functions is not consistent (processed %d, expected %d)\n",
			index + 1, nfp_prog->subprog_cnt);
		return -EFAULT;
	}

	return 0;
}

static unsigned int nfp_bpf_get_stack_usage(struct nfp_prog *nfp_prog)
{
	struct nfp_insn_meta *meta = nfp_prog_first_meta(nfp_prog);
	unsigned int max_depth = 0, depth = 0, frame = 0;
	struct nfp_insn_meta *ret_insn[MAX_CALL_FRAMES];
	unsigned short frame_depths[MAX_CALL_FRAMES];
	unsigned short ret_prog[MAX_CALL_FRAMES];
	unsigned short idx = meta->subprog_idx;

	/* Inspired from check_max_stack_depth() from kernel verifier.
	 * Starting from main subprogram, walk all instructions and recursively
	 * walk all callees that given subprogram can call. Since recursion is
	 * prevented by the kernel verifier, this algorithm only needs a local
	 * stack of MAX_CALL_FRAMES to remember callsites.
	 */
process_subprog:
	frame_depths[frame] = nfp_prog->subprog[idx].stack_depth;
	frame_depths[frame] = round_up(frame_depths[frame], STACK_FRAME_ALIGN);
	depth += frame_depths[frame];
	max_depth = max(max_depth, depth);

continue_subprog:
	for (; meta != nfp_prog_last_meta(nfp_prog) && meta->subprog_idx == idx;
	     meta = nfp_meta_next(meta)) {
		if (!is_mbpf_pseudo_call(meta))
			continue;

		/* We found a call to a subprogram. Remember instruction to
		 * return to and subprog id.
		 */
		ret_insn[frame] = nfp_meta_next(meta);
		ret_prog[frame] = idx;

		/* Find the callee and start processing it. */
		meta = nfp_bpf_goto_meta(nfp_prog, meta,
					 meta->n + 1 + meta->insn.imm);
		idx = meta->subprog_idx;
		frame++;
		goto process_subprog;
	}
	/* End of for() loop means the last instruction of the subprog was
	 * reached. If we popped all stack frames, return; otherwise, go on
	 * processing remaining instructions from the caller.
	 */
	if (frame == 0)
		return max_depth;

	depth -= frame_depths[frame];
	frame--;
	meta = ret_insn[frame];
	idx = ret_prog[frame];
	goto continue_subprog;
}

static void nfp_bpf_insn_flag_zext(struct nfp_prog *nfp_prog,
				   struct bpf_insn_aux_data *aux)
{
	struct nfp_insn_meta *meta;

	list_for_each_entry(meta, &nfp_prog->insns, l) {
		if (aux[meta->n].zext_dst)
			meta->flags |= FLAG_INSN_DO_ZEXT;
	}
}

int nfp_bpf_finalize(struct bpf_verifier_env *env)
{
	struct bpf_subprog_info *info;
	struct nfp_prog *nfp_prog;
	unsigned int max_stack;
	struct nfp_net *nn;
	int i;

	nfp_prog = env->prog->aux->offload->dev_priv;
	nfp_prog->subprog_cnt = env->subprog_cnt;
	nfp_prog->subprog = kcalloc(nfp_prog->subprog_cnt,
				    sizeof(nfp_prog->subprog[0]), GFP_KERNEL);
	if (!nfp_prog->subprog)
		return -ENOMEM;

	nfp_assign_subprog_idx_and_regs(env, nfp_prog);

	info = env->subprog_info;
	for (i = 0; i < nfp_prog->subprog_cnt; i++) {
		nfp_prog->subprog[i].stack_depth = info[i].stack_depth;

		if (i == 0)
			continue;

		/* Account for size of return address. */
		nfp_prog->subprog[i].stack_depth += REG_WIDTH;
		/* Account for size of saved registers, if necessary. */
		if (nfp_prog->subprog[i].needs_reg_push)
			nfp_prog->subprog[i].stack_depth += BPF_REG_SIZE * 4;
	}

	nn = netdev_priv(env->prog->aux->offload->netdev);
	max_stack = nn_readb(nn, NFP_NET_CFG_BPF_STACK_SZ) * 64;
	nfp_prog->stack_size = nfp_bpf_get_stack_usage(nfp_prog);
	if (nfp_prog->stack_size > max_stack) {
		pr_vlog(env, "stack too large: program %dB > FW stack %dB\n",
			nfp_prog->stack_size, max_stack);
		return -EOPNOTSUPP;
	}

	nfp_bpf_insn_flag_zext(nfp_prog, env->insn_aux_data);
	return 0;
}

int nfp_bpf_opt_replace_insn(struct bpf_verifier_env *env, u32 off,
			     struct bpf_insn *insn)
{
	struct nfp_prog *nfp_prog = env->prog->aux->offload->dev_priv;
	struct bpf_insn_aux_data *aux_data = env->insn_aux_data;
	struct nfp_insn_meta *meta = nfp_prog->verifier_meta;

	meta = nfp_bpf_goto_meta(nfp_prog, meta, aux_data[off].orig_idx);
	nfp_prog->verifier_meta = meta;

	/* conditional jump to jump conversion */
	if (is_mbpf_cond_jump(meta) &&
	    insn->code == (BPF_JMP | BPF_JA | BPF_K)) {
		unsigned int tgt_off;

		tgt_off = off + insn->off + 1;

		if (!insn->off) {
			meta->jmp_dst = list_next_entry(meta, l);
			meta->jump_neg_op = false;
		} else if (meta->jmp_dst->n != aux_data[tgt_off].orig_idx) {
			pr_vlog(env, "branch hard wire at %d changes target %d -> %d\n",
				off, meta->jmp_dst->n,
				aux_data[tgt_off].orig_idx);
			return -EINVAL;
		}
		return 0;
	}

	pr_vlog(env, "unsupported instruction replacement %hhx -> %hhx\n",
		meta->insn.code, insn->code);
	return -EINVAL;
}

int nfp_bpf_opt_remove_insns(struct bpf_verifier_env *env, u32 off, u32 cnt)
{
	struct nfp_prog *nfp_prog = env->prog->aux->offload->dev_priv;
	struct bpf_insn_aux_data *aux_data = env->insn_aux_data;
	struct nfp_insn_meta *meta = nfp_prog->verifier_meta;
	unsigned int i;

	meta = nfp_bpf_goto_meta(nfp_prog, meta, aux_data[off].orig_idx);

	for (i = 0; i < cnt; i++) {
		if (WARN_ON_ONCE(&meta->l == &nfp_prog->insns))
			return -EINVAL;

		/* doesn't count if it already has the flag */
		if (meta->flags & FLAG_INSN_SKIP_VERIFIER_OPT)
			i--;

		meta->flags |= FLAG_INSN_SKIP_VERIFIER_OPT;
		meta = list_next_entry(meta, l);
	}

	return 0;
}
