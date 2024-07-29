// SPDX-License-Identifier: GPL-2.0
/*
 * The back-end-agnostic part of Just-In-Time compiler for eBPF bytecode.
 *
 * Copyright (c) 2024 Synopsys Inc.
 * Author: Shahab Vahedi <shahab@synopsys.com>
 */
#include <linux/bug.h>
#include "bpf_jit.h"

/*
 * Check for the return value. A pattern used often in this file.
 * There must be a "ret" variable of type "int" in the scope.
 */
#define CHECK_RET(cmd)			\
	do {				\
		ret = (cmd);		\
		if (ret < 0)		\
			return ret;	\
	} while (0)

#ifdef ARC_BPF_JIT_DEBUG
/* Dumps bytes in /var/log/messages at KERN_INFO level (4). */
static void dump_bytes(const u8 *buf, u32 len, const char *header)
{
	u8 line[64];
	size_t i, j;

	pr_info("-----------------[ %s ]-----------------\n", header);

	for (i = 0, j = 0; i < len; i++) {
		/* Last input byte? */
		if (i == len - 1) {
			j += scnprintf(line + j, 64 - j, "0x%02x", buf[i]);
			pr_info("%s\n", line);
			break;
		}
		/* End of line? */
		else if (i % 8 == 7) {
			j += scnprintf(line + j, 64 - j, "0x%02x", buf[i]);
			pr_info("%s\n", line);
			j = 0;
		} else {
			j += scnprintf(line + j, 64 - j, "0x%02x, ", buf[i]);
		}
	}
}
#endif /* ARC_BPF_JIT_DEBUG */

/********************* JIT context ***********************/

/*
 * buf:		Translated instructions end up here.
 * len:		The length of whole block in bytes.
 * index:	The offset at which the _next_ instruction may be put.
 */
struct jit_buffer {
	u8	*buf;
	u32	len;
	u32	index;
};

/*
 * This is a subset of "struct jit_context" that its information is deemed
 * necessary for the next extra pass to come.
 *
 * bpf_header:	Needed to finally lock the region.
 * bpf2insn:	Used to find the translation for instructions of interest.
 *
 * Things like "jit.buf" and "jit.len" can be retrieved respectively from
 * "prog->bpf_func" and "prog->jited_len".
 */
struct arc_jit_data {
	struct bpf_binary_header *bpf_header;
	u32                      *bpf2insn;
};

/*
 * The JIT pertinent context that is used by different functions.
 *
 * prog:		The current eBPF program being handled.
 * orig_prog:		The original eBPF program before any possible change.
 * jit:			The JIT buffer and its length.
 * bpf_header:		The JITed program header. "jit.buf" points inside it.
 * emit:		If set, opcodes are written to memory; else, a dry-run.
 * do_zext:		If true, 32-bit sub-regs must be zero extended.
 * bpf2insn:		Maps BPF insn indices to their counterparts in jit.buf.
 * bpf2insn_valid:	Indicates if "bpf2ins" is populated with the mappings.
 * jit_data:		A piece of memory to transfer data to the next pass.
 * arc_regs_clobbered:	Each bit status determines if that arc reg is clobbered.
 * save_blink:		Whether ARC's "blink" register needs to be saved.
 * frame_size:		Derived from "prog->aux->stack_depth".
 * epilogue_offset:	Used by early "return"s in the code to jump here.
 * need_extra_pass:	A forecast if an "extra_pass" will occur.
 * is_extra_pass:	Indicates if the current pass is an extra pass.
 * user_bpf_prog:	True, if VM opcodes come from a real program.
 * blinded:		True if "constant blinding" step returned a new "prog".
 * success:		Indicates if the whole JIT went OK.
 */
struct jit_context {
	struct bpf_prog			*prog;
	struct bpf_prog			*orig_prog;
	struct jit_buffer		jit;
	struct bpf_binary_header	*bpf_header;
	bool				emit;
	bool				do_zext;
	u32				*bpf2insn;
	bool				bpf2insn_valid;
	struct arc_jit_data		*jit_data;
	u32				arc_regs_clobbered;
	bool				save_blink;
	u16				frame_size;
	u32				epilogue_offset;
	bool				need_extra_pass;
	bool				is_extra_pass;
	bool				user_bpf_prog;
	bool				blinded;
	bool				success;
};

/*
 * If we're in ARC_BPF_JIT_DEBUG mode and the debug level is right, dump the
 * input BPF stream. "bpf_jit_dump()" is not fully suited for this purpose.
 */
static void vm_dump(const struct bpf_prog *prog)
{
#ifdef ARC_BPF_JIT_DEBUG
	if (bpf_jit_enable > 1)
		dump_bytes((u8 *)prog->insns, 8 * prog->len, " VM  ");
#endif
}

/*
 * If the right level of debug is set, dump the bytes. There are 2 variants
 * of this function:
 *
 * 1. Use the standard bpf_jit_dump() which is meant only for JITed code.
 * 2. Use the dump_bytes() to match its "vm_dump()" instance.
 */
static void jit_dump(const struct jit_context *ctx)
{
#ifdef ARC_BPF_JIT_DEBUG
	u8 header[8];
#endif
	const int pass = ctx->is_extra_pass ? 2 : 1;

	if (bpf_jit_enable <= 1 || !ctx->prog->jited)
		return;

#ifdef ARC_BPF_JIT_DEBUG
	scnprintf(header, sizeof(header), "JIT:%d", pass);
	dump_bytes(ctx->jit.buf, ctx->jit.len, header);
	pr_info("\n");
#else
	bpf_jit_dump(ctx->prog->len, ctx->jit.len, pass, ctx->jit.buf);
#endif
}

/* Initialise the context so there's no garbage. */
static int jit_ctx_init(struct jit_context *ctx, struct bpf_prog *prog)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->orig_prog = prog;

	/* If constant blinding was requested but failed, scram. */
	ctx->prog = bpf_jit_blind_constants(prog);
	if (IS_ERR(ctx->prog))
		return PTR_ERR(ctx->prog);
	ctx->blinded = (ctx->prog != ctx->orig_prog);

	/* If the verifier doesn't zero-extend, then we have to do it. */
	ctx->do_zext = !ctx->prog->aux->verifier_zext;

	ctx->is_extra_pass = ctx->prog->jited;
	ctx->user_bpf_prog = ctx->prog->is_func;

	return 0;
}

/*
 * Only after the first iteration of normal pass (the dry-run),
 * there are valid offsets in ctx->bpf2insn array.
 */
static inline bool offsets_available(const struct jit_context *ctx)
{
	return ctx->bpf2insn_valid;
}

/*
 * "*mem" should be freed when there is no "extra pass" to come,
 * or the compilation terminated abruptly. A few of such memory
 * allocations are: ctx->jit_data and ctx->bpf2insn.
 */
static inline void maybe_free(struct jit_context *ctx, void **mem)
{
	if (*mem) {
		if (!ctx->success || !ctx->need_extra_pass) {
			kfree(*mem);
			*mem = NULL;
		}
	}
}

/*
 * Free memories based on the status of the context.
 *
 * A note about "bpf_header": On successful runs, "bpf_header" is
 * not freed, because "jit.buf", a sub-array of it, is returned as
 * the "bpf_func". However, "bpf_header" is lost and nothing points
 * to it. This should not cause a leakage, because apparently
 * "bpf_header" can be revived by "bpf_jit_binary_hdr()". This is
 * how "bpf_jit_free()" in "kernel/bpf/core.c" releases the memory.
 */
static void jit_ctx_cleanup(struct jit_context *ctx)
{
	if (ctx->blinded) {
		/* if all went well, release the orig_prog. */
		if (ctx->success)
			bpf_jit_prog_release_other(ctx->prog, ctx->orig_prog);
		else
			bpf_jit_prog_release_other(ctx->orig_prog, ctx->prog);
	}

	maybe_free(ctx, (void **)&ctx->bpf2insn);
	maybe_free(ctx, (void **)&ctx->jit_data);

	if (!ctx->bpf2insn)
		ctx->bpf2insn_valid = false;

	/* Freeing "bpf_header" is enough. "jit.buf" is a sub-array of it. */
	if (!ctx->success && ctx->bpf_header) {
		bpf_jit_binary_free(ctx->bpf_header);
		ctx->bpf_header = NULL;
		ctx->jit.buf    = NULL;
		ctx->jit.index  = 0;
		ctx->jit.len    = 0;
	}

	ctx->emit = false;
	ctx->do_zext = false;
}

/*
 * Analyse the register usage and record the frame size.
 * The register usage is determined by consulting the back-end.
 */
static void analyze_reg_usage(struct jit_context *ctx)
{
	size_t i;
	u32 usage = 0;
	const struct bpf_insn *insn = ctx->prog->insnsi;

	for (i = 0; i < ctx->prog->len; i++) {
		u8 bpf_reg;
		bool call;

		bpf_reg = insn[i].dst_reg;
		call = (insn[i].code == (BPF_JMP | BPF_CALL)) ? true : false;
		usage |= mask_for_used_regs(bpf_reg, call);
	}

	ctx->arc_regs_clobbered = usage;
	ctx->frame_size = ctx->prog->aux->stack_depth;
}

/* Verify that no instruction will be emitted when there is no buffer. */
static inline int jit_buffer_check(const struct jit_context *ctx)
{
	if (ctx->emit) {
		if (!ctx->jit.buf) {
			pr_err("bpf-jit: inconsistence state; no "
			       "buffer to emit instructions.\n");
			return -EINVAL;
		} else if (ctx->jit.index > ctx->jit.len) {
			pr_err("bpf-jit: estimated JIT length is less "
			       "than the emitted instructions.\n");
			return -EFAULT;
		}
	}
	return 0;
}

/* On a dry-run (emit=false), "jit.len" is growing gradually. */
static inline void jit_buffer_update(struct jit_context *ctx, u32 n)
{
	if (!ctx->emit)
		ctx->jit.len += n;
	else
		ctx->jit.index += n;
}

/* Based on "emit", determine the address where instructions are emitted. */
static inline u8 *effective_jit_buf(const struct jit_context *ctx)
{
	return ctx->emit ? (ctx->jit.buf + ctx->jit.index) : NULL;
}

/* Prologue based on context variables set by "analyze_reg_usage()". */
static int handle_prologue(struct jit_context *ctx)
{
	int ret;
	u8 *buf = effective_jit_buf(ctx);
	u32 len = 0;

	CHECK_RET(jit_buffer_check(ctx));

	len = arc_prologue(buf, ctx->arc_regs_clobbered, ctx->frame_size);
	jit_buffer_update(ctx, len);

	return 0;
}

/* The counter part for "handle_prologue()". */
static int handle_epilogue(struct jit_context *ctx)
{
	int ret;
	u8 *buf = effective_jit_buf(ctx);
	u32 len = 0;

	CHECK_RET(jit_buffer_check(ctx));

	len = arc_epilogue(buf, ctx->arc_regs_clobbered, ctx->frame_size);
	jit_buffer_update(ctx, len);

	return 0;
}

/* Tell which number of the BPF instruction we are dealing with. */
static inline s32 get_index_for_insn(const struct jit_context *ctx,
				     const struct bpf_insn *insn)
{
	return (insn - ctx->prog->insnsi);
}

/*
 * In most of the cases, the "offset" is read from "insn->off". However,
 * if it is an unconditional BPF_JMP32, then it comes from "insn->imm".
 *
 * (Courtesy of "cpu=v4" support)
 */
static inline s32 get_offset(const struct bpf_insn *insn)
{
	if ((BPF_CLASS(insn->code) == BPF_JMP32) &&
	    (BPF_OP(insn->code) == BPF_JA))
		return insn->imm;
	else
		return insn->off;
}

/*
 * Determine to which number of the BPF instruction we're jumping to.
 *
 * The "offset" is interpreted as the "number" of BPF instructions
 * from the _next_ BPF instruction. e.g.:
 *
 *  4 means 4 instructions after  the next insn
 *  0 means 0 instructions after  the next insn -> fallthrough.
 * -1 means 1 instruction  before the next insn -> jmp to current insn.
 *
 *  Another way to look at this, "offset" is the number of instructions
 *  that exist between the current instruction and the target instruction.
 *
 *  It is worth noting that a "mov r,i64", which is 16-byte long, is
 *  treated as two instructions long, therefore "offset" needn't be
 *  treated specially for those. Everything is uniform.
 */
static inline s32 get_target_index_for_insn(const struct jit_context *ctx,
					    const struct bpf_insn *insn)
{
	return (get_index_for_insn(ctx, insn) + 1) + get_offset(insn);
}

/* Is there an immediate operand encoded in the "insn"? */
static inline bool has_imm(const struct bpf_insn *insn)
{
	return BPF_SRC(insn->code) == BPF_K;
}

/* Is the last BPF instruction? */
static inline bool is_last_insn(const struct bpf_prog *prog, u32 idx)
{
	return idx == (prog->len - 1);
}

/*
 * Invocation of this function, conditionally signals the need for
 * an extra pass. The conditions that must be met are:
 *
 * 1. The current pass itself shouldn't be an extra pass.
 * 2. The stream of bytes being JITed must come from a user program.
 */
static inline void set_need_for_extra_pass(struct jit_context *ctx)
{
	if (!ctx->is_extra_pass)
		ctx->need_extra_pass = ctx->user_bpf_prog;
}

/*
 * Check if the "size" is valid and then transfer the control to
 * the back-end for the swap.
 */
static int handle_swap(u8 *buf, u8 rd, u8 size, u8 endian,
		       bool force, bool do_zext, u8 *len)
{
	/* Sanity check on the size. */
	switch (size) {
	case 16:
	case 32:
	case 64:
		break;
	default:
		pr_err("bpf-jit: invalid size for swap.\n");
		return -EINVAL;
	}

	*len = gen_swap(buf, rd, size, endian, force, do_zext);

	return 0;
}

/* Checks if the (instruction) index is in valid range. */
static inline bool check_insn_idx_valid(const struct jit_context *ctx,
					const s32 idx)
{
	return (idx >= 0 && idx < ctx->prog->len);
}

/*
 * Decouple the back-end from BPF by converting BPF conditions
 * to internal enum. ARC_CC_* start from 0 and are used as index
 * to an array. BPF_J* usage must end after this conversion.
 */
static int bpf_cond_to_arc(const u8 op, u8 *arc_cc)
{
	switch (op) {
	case BPF_JA:
		*arc_cc = ARC_CC_AL;
		break;
	case BPF_JEQ:
		*arc_cc = ARC_CC_EQ;
		break;
	case BPF_JGT:
		*arc_cc = ARC_CC_UGT;
		break;
	case BPF_JGE:
		*arc_cc = ARC_CC_UGE;
		break;
	case BPF_JSET:
		*arc_cc = ARC_CC_SET;
		break;
	case BPF_JNE:
		*arc_cc = ARC_CC_NE;
		break;
	case BPF_JSGT:
		*arc_cc = ARC_CC_SGT;
		break;
	case BPF_JSGE:
		*arc_cc = ARC_CC_SGE;
		break;
	case BPF_JLT:
		*arc_cc = ARC_CC_ULT;
		break;
	case BPF_JLE:
		*arc_cc = ARC_CC_ULE;
		break;
	case BPF_JSLT:
		*arc_cc = ARC_CC_SLT;
		break;
	case BPF_JSLE:
		*arc_cc = ARC_CC_SLE;
		break;
	default:
		pr_err("bpf-jit: can't handle condition 0x%02X\n", op);
		return -EINVAL;
	}
	return 0;
}

/*
 * Check a few things for a supposedly "jump" instruction:
 *
 * 0. "insn" is a "jump" instruction, but not the "call/exit" variant.
 * 1. The current "insn" index is in valid range.
 * 2. The index of target instruction is in valid range.
 */
static int check_bpf_jump(const struct jit_context *ctx,
			  const struct bpf_insn *insn)
{
	const u8 class = BPF_CLASS(insn->code);
	const u8 op = BPF_OP(insn->code);

	/* Must be a jmp(32) instruction that is not a "call/exit". */
	if ((class != BPF_JMP && class != BPF_JMP32) ||
	    (op == BPF_CALL || op == BPF_EXIT)) {
		pr_err("bpf-jit: not a jump instruction.\n");
		return -EINVAL;
	}

	if (!check_insn_idx_valid(ctx, get_index_for_insn(ctx, insn))) {
		pr_err("bpf-jit: the bpf jump insn is not in prog.\n");
		return -EINVAL;
	}

	if (!check_insn_idx_valid(ctx, get_target_index_for_insn(ctx, insn))) {
		pr_err("bpf-jit: bpf jump label is out of range.\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * Based on input "insn", consult "ctx->bpf2insn" to get the
 * related index (offset) of the translation in JIT stream.
 */
static u32 get_curr_jit_off(const struct jit_context *ctx,
			    const struct bpf_insn *insn)
{
	const s32 idx = get_index_for_insn(ctx, insn);
#ifdef ARC_BPF_JIT_DEBUG
	BUG_ON(!offsets_available(ctx) || !check_insn_idx_valid(ctx, idx));
#endif
	return ctx->bpf2insn[idx];
}

/*
 * The input "insn" must be a jump instruction.
 *
 * Based on input "insn", consult "ctx->bpf2insn" to get the
 * related JIT index (offset) of "target instruction" that
 * "insn" would jump to.
 */
static u32 get_targ_jit_off(const struct jit_context *ctx,
			    const struct bpf_insn *insn)
{
	const s32 tidx = get_target_index_for_insn(ctx, insn);
#ifdef ARC_BPF_JIT_DEBUG
	BUG_ON(!offsets_available(ctx) || !check_insn_idx_valid(ctx, tidx));
#endif
	return ctx->bpf2insn[tidx];
}

/*
 * This function will return 0 for a feasible jump.
 *
 * Consult the back-end to check if it finds it feasible to emit
 * the necessary instructions based on "cond" and the displacement
 * between the "from_off" and the "to_off".
 */
static int feasible_jit_jump(u32 from_off, u32 to_off, u8 cond, bool j32)
{
	int ret = 0;

	if (j32) {
		if (!check_jmp_32(from_off, to_off, cond))
			ret = -EFAULT;
	} else {
		if (!check_jmp_64(from_off, to_off, cond))
			ret = -EFAULT;
	}

	if (ret != 0)
		pr_err("bpf-jit: the JIT displacement is not OK.\n");

	return ret;
}

/*
 * This jump handler performs the following steps:
 *
 * 1. Compute ARC's internal condition code from BPF's
 * 2. Determine the bitness of the operation (32 vs. 64)
 * 3. Sanity check on BPF stream
 * 4. Sanity check on what is supposed to be JIT's displacement
 * 5. And finally, emit the necessary instructions
 *
 * The last two steps are performed through the back-end.
 * The value of steps 1 and 2 are necessary inputs for the back-end.
 */
static int handle_jumps(const struct jit_context *ctx,
			const struct bpf_insn *insn,
			u8 *len)
{
	u8 cond;
	int ret = 0;
	u8 *buf = effective_jit_buf(ctx);
	const bool j32 = (BPF_CLASS(insn->code) == BPF_JMP32) ? true : false;
	const u8 rd = insn->dst_reg;
	u8 rs = insn->src_reg;
	u32 curr_off = 0, targ_off = 0;

	*len = 0;

	/* Map the BPF condition to internal enum. */
	CHECK_RET(bpf_cond_to_arc(BPF_OP(insn->code), &cond));

	/* Sanity check on the BPF byte stream. */
	CHECK_RET(check_bpf_jump(ctx, insn));

	/*
	 * Move the immediate into a temporary register _now_ for 2 reasons:
	 *
	 * 1. "gen_jmp_{32,64}()" deal with operands in registers.
	 *
	 * 2. The "len" parameter will grow so that the current jit offset
	 *    (curr_off) will have increased to a point where the necessary
	 *    instructions can be inserted by "gen_jmp_{32,64}()".
	 */
	if (has_imm(insn) && cond != ARC_CC_AL) {
		if (j32) {
			*len += mov_r32_i32(BUF(buf, *len), JIT_REG_TMP,
					    insn->imm);
		} else {
			*len += mov_r64_i32(BUF(buf, *len), JIT_REG_TMP,
					    insn->imm);
		}
		rs = JIT_REG_TMP;
	}

	/* If the offsets are known, check if the branch can occur. */
	if (offsets_available(ctx)) {
		curr_off = get_curr_jit_off(ctx, insn) + *len;
		targ_off = get_targ_jit_off(ctx, insn);

		/* Sanity check on the back-end side. */
		CHECK_RET(feasible_jit_jump(curr_off, targ_off, cond, j32));
	}

	if (j32) {
		*len += gen_jmp_32(BUF(buf, *len), rd, rs, cond,
				   curr_off, targ_off);
	} else {
		*len += gen_jmp_64(BUF(buf, *len), rd, rs, cond,
				   curr_off, targ_off);
	}

	return ret;
}

/* Jump to translated epilogue address. */
static int handle_jmp_epilogue(struct jit_context *ctx,
			       const struct bpf_insn *insn, u8 *len)
{
	u8 *buf = effective_jit_buf(ctx);
	u32 curr_off = 0, epi_off = 0;

	/* Check the offset only if the data is available. */
	if (offsets_available(ctx)) {
		curr_off = get_curr_jit_off(ctx, insn);
		epi_off = ctx->epilogue_offset;

		if (!check_jmp_64(curr_off, epi_off, ARC_CC_AL)) {
			pr_err("bpf-jit: epilogue offset is not valid.\n");
			return -EINVAL;
		}
	}

	/* Jump to "epilogue offset" (rd and rs don't matter). */
	*len = gen_jmp_64(buf, 0, 0, ARC_CC_AL, curr_off, epi_off);

	return 0;
}

/* Try to get the resolved address and generate the instructions. */
static int handle_call(struct jit_context *ctx,
		       const struct bpf_insn *insn,
		       u8 *len)
{
	int  ret;
	bool in_kernel_func, fixed = false;
	u64  addr = 0;
	u8  *buf = effective_jit_buf(ctx);

	ret = bpf_jit_get_func_addr(ctx->prog, insn, ctx->is_extra_pass,
				    &addr, &fixed);
	if (ret < 0) {
		pr_err("bpf-jit: can't get the address for call.\n");
		return ret;
	}
	in_kernel_func = (fixed ? true : false);

	/* No valuable address retrieved (yet). */
	if (!fixed && !addr)
		set_need_for_extra_pass(ctx);

	*len = gen_func_call(buf, (ARC_ADDR)addr, in_kernel_func);

	if (insn->src_reg != BPF_PSEUDO_CALL) {
		/* Assigning ABI's return reg to JIT's return reg. */
		*len += arc_to_bpf_return(BUF(buf, *len));
	}

	return 0;
}

/*
 * Try to generate instructions for loading a 64-bit immediate.
 * These sort of instructions are usually associated with the 64-bit
 * relocations: R_BPF_64_64. Therefore, signal the need for an extra
 * pass if the circumstances are right.
 */
static int handle_ld_imm64(struct jit_context *ctx,
			   const struct bpf_insn *insn,
			   u8 *len)
{
	const s32 idx = get_index_for_insn(ctx, insn);
	u8 *buf = effective_jit_buf(ctx);

	/* We're about to consume 2 VM instructions. */
	if (is_last_insn(ctx->prog, idx)) {
		pr_err("bpf-jit: need more data for 64-bit immediate.\n");
		return -EINVAL;
	}

	*len = mov_r64_i64(buf, insn->dst_reg, insn->imm, (insn + 1)->imm);

	if (bpf_pseudo_func(insn))
		set_need_for_extra_pass(ctx);

	return 0;
}

/*
 * Handles one eBPF instruction at a time. To make this function faster,
 * it does not call "jit_buffer_check()". Else, it would call it for every
 * instruction. As a result, it should not be invoked directly. Only
 * "handle_body()", that has already executed the "check", may call this
 * function.
 *
 * If the "ret" value is negative, something has went wrong. Else,
 * it mostly holds the value 0 and rarely 1. Number 1 signals
 * the loop in "handle_body()" to skip the next instruction, because
 * it has been consumed as part of a 64-bit immediate value.
 */
static int handle_insn(struct jit_context *ctx, u32 idx)
{
	const struct bpf_insn *insn = &ctx->prog->insnsi[idx];
	const u8  code = insn->code;
	const u8  dst  = insn->dst_reg;
	const u8  src  = insn->src_reg;
	const s16 off  = insn->off;
	const s32 imm  = insn->imm;
	u8 *buf = effective_jit_buf(ctx);
	u8  len = 0;
	int ret = 0;

	switch (code) {
	/* dst += src (32-bit) */
	case BPF_ALU | BPF_ADD | BPF_X:
		len = add_r32(buf, dst, src);
		break;
	/* dst += imm (32-bit) */
	case BPF_ALU | BPF_ADD | BPF_K:
		len = add_r32_i32(buf, dst, imm);
		break;
	/* dst -= src (32-bit) */
	case BPF_ALU | BPF_SUB | BPF_X:
		len = sub_r32(buf, dst, src);
		break;
	/* dst -= imm (32-bit) */
	case BPF_ALU | BPF_SUB | BPF_K:
		len = sub_r32_i32(buf, dst, imm);
		break;
	/* dst = -dst (32-bit) */
	case BPF_ALU | BPF_NEG:
		len = neg_r32(buf, dst);
		break;
	/* dst *= src (32-bit) */
	case BPF_ALU | BPF_MUL | BPF_X:
		len = mul_r32(buf, dst, src);
		break;
	/* dst *= imm (32-bit) */
	case BPF_ALU | BPF_MUL | BPF_K:
		len = mul_r32_i32(buf, dst, imm);
		break;
	/* dst /= src (32-bit) */
	case BPF_ALU | BPF_DIV | BPF_X:
		len = div_r32(buf, dst, src, off == 1);
		break;
	/* dst /= imm (32-bit) */
	case BPF_ALU | BPF_DIV | BPF_K:
		len = div_r32_i32(buf, dst, imm, off == 1);
		break;
	/* dst %= src (32-bit) */
	case BPF_ALU | BPF_MOD | BPF_X:
		len = mod_r32(buf, dst, src, off == 1);
		break;
	/* dst %= imm (32-bit) */
	case BPF_ALU | BPF_MOD | BPF_K:
		len = mod_r32_i32(buf, dst, imm, off == 1);
		break;
	/* dst &= src (32-bit) */
	case BPF_ALU | BPF_AND | BPF_X:
		len = and_r32(buf, dst, src);
		break;
	/* dst &= imm (32-bit) */
	case BPF_ALU | BPF_AND | BPF_K:
		len = and_r32_i32(buf, dst, imm);
		break;
	/* dst |= src (32-bit) */
	case BPF_ALU | BPF_OR | BPF_X:
		len = or_r32(buf, dst, src);
		break;
	/* dst |= imm (32-bit) */
	case BPF_ALU | BPF_OR | BPF_K:
		len = or_r32_i32(buf, dst, imm);
		break;
	/* dst ^= src (32-bit) */
	case BPF_ALU | BPF_XOR | BPF_X:
		len = xor_r32(buf, dst, src);
		break;
	/* dst ^= imm (32-bit) */
	case BPF_ALU | BPF_XOR | BPF_K:
		len = xor_r32_i32(buf, dst, imm);
		break;
	/* dst <<= src (32-bit) */
	case BPF_ALU | BPF_LSH | BPF_X:
		len = lsh_r32(buf, dst, src);
		break;
	/* dst <<= imm (32-bit) */
	case BPF_ALU | BPF_LSH | BPF_K:
		len = lsh_r32_i32(buf, dst, imm);
		break;
	/* dst >>= src (32-bit) [unsigned] */
	case BPF_ALU | BPF_RSH | BPF_X:
		len = rsh_r32(buf, dst, src);
		break;
	/* dst >>= imm (32-bit) [unsigned] */
	case BPF_ALU | BPF_RSH | BPF_K:
		len = rsh_r32_i32(buf, dst, imm);
		break;
	/* dst >>= src (32-bit) [signed] */
	case BPF_ALU | BPF_ARSH | BPF_X:
		len = arsh_r32(buf, dst, src);
		break;
	/* dst >>= imm (32-bit) [signed] */
	case BPF_ALU | BPF_ARSH | BPF_K:
		len = arsh_r32_i32(buf, dst, imm);
		break;
	/* dst = src (32-bit) */
	case BPF_ALU | BPF_MOV | BPF_X:
		len = mov_r32(buf, dst, src, (u8)off);
		break;
	/* dst = imm32 (32-bit) */
	case BPF_ALU | BPF_MOV | BPF_K:
		len = mov_r32_i32(buf, dst, imm);
		break;
	/* dst = swap(dst) */
	case BPF_ALU   | BPF_END | BPF_FROM_LE:
	case BPF_ALU   | BPF_END | BPF_FROM_BE:
	case BPF_ALU64 | BPF_END | BPF_FROM_LE: {
		CHECK_RET(handle_swap(buf, dst, imm, BPF_SRC(code),
				      BPF_CLASS(code) == BPF_ALU64,
				      ctx->do_zext, &len));
		break;
	}
	/* dst += src (64-bit) */
	case BPF_ALU64 | BPF_ADD | BPF_X:
		len = add_r64(buf, dst, src);
		break;
	/* dst += imm32 (64-bit) */
	case BPF_ALU64 | BPF_ADD | BPF_K:
		len = add_r64_i32(buf, dst, imm);
		break;
	/* dst -= src (64-bit) */
	case BPF_ALU64 | BPF_SUB | BPF_X:
		len = sub_r64(buf, dst, src);
		break;
	/* dst -= imm32 (64-bit) */
	case BPF_ALU64 | BPF_SUB | BPF_K:
		len = sub_r64_i32(buf, dst, imm);
		break;
	/* dst = -dst (64-bit) */
	case BPF_ALU64 | BPF_NEG:
		len = neg_r64(buf, dst);
		break;
	/* dst *= src (64-bit) */
	case BPF_ALU64 | BPF_MUL | BPF_X:
		len = mul_r64(buf, dst, src);
		break;
	/* dst *= imm32 (64-bit) */
	case BPF_ALU64 | BPF_MUL | BPF_K:
		len = mul_r64_i32(buf, dst, imm);
		break;
	/* dst &= src (64-bit) */
	case BPF_ALU64 | BPF_AND | BPF_X:
		len = and_r64(buf, dst, src);
		break;
	/* dst &= imm32 (64-bit) */
	case BPF_ALU64 | BPF_AND | BPF_K:
		len = and_r64_i32(buf, dst, imm);
		break;
	/* dst |= src (64-bit) */
	case BPF_ALU64 | BPF_OR | BPF_X:
		len = or_r64(buf, dst, src);
		break;
	/* dst |= imm32 (64-bit) */
	case BPF_ALU64 | BPF_OR | BPF_K:
		len = or_r64_i32(buf, dst, imm);
		break;
	/* dst ^= src (64-bit) */
	case BPF_ALU64 | BPF_XOR | BPF_X:
		len = xor_r64(buf, dst, src);
		break;
	/* dst ^= imm32 (64-bit) */
	case BPF_ALU64 | BPF_XOR | BPF_K:
		len = xor_r64_i32(buf, dst, imm);
		break;
	/* dst <<= src (64-bit) */
	case BPF_ALU64 | BPF_LSH | BPF_X:
		len = lsh_r64(buf, dst, src);
		break;
	/* dst <<= imm32 (64-bit) */
	case BPF_ALU64 | BPF_LSH | BPF_K:
		len = lsh_r64_i32(buf, dst, imm);
		break;
	/* dst >>= src (64-bit) [unsigned] */
	case BPF_ALU64 | BPF_RSH | BPF_X:
		len = rsh_r64(buf, dst, src);
		break;
	/* dst >>= imm32 (64-bit) [unsigned] */
	case BPF_ALU64 | BPF_RSH | BPF_K:
		len = rsh_r64_i32(buf, dst, imm);
		break;
	/* dst >>= src (64-bit) [signed] */
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		len = arsh_r64(buf, dst, src);
		break;
	/* dst >>= imm32 (64-bit) [signed] */
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		len = arsh_r64_i32(buf, dst, imm);
		break;
	/* dst = src (64-bit) */
	case BPF_ALU64 | BPF_MOV | BPF_X:
		len = mov_r64(buf, dst, src, (u8)off);
		break;
	/* dst = imm32 (sign extend to 64-bit) */
	case BPF_ALU64 | BPF_MOV | BPF_K:
		len = mov_r64_i32(buf, dst, imm);
		break;
	/* dst = imm64 */
	case BPF_LD | BPF_DW | BPF_IMM:
		CHECK_RET(handle_ld_imm64(ctx, insn, &len));
		/* Tell the loop to skip the next instruction. */
		ret = 1;
		break;
	/* dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_DW:
		len = load_r(buf, dst, src, off, BPF_SIZE(code), false);
		break;
	case BPF_LDX | BPF_MEMSX | BPF_W:
	case BPF_LDX | BPF_MEMSX | BPF_H:
	case BPF_LDX | BPF_MEMSX | BPF_B:
		len = load_r(buf, dst, src, off, BPF_SIZE(code), true);
		break;
	/* *(size *)(dst + off) = src */
	case BPF_STX | BPF_MEM | BPF_W:
	case BPF_STX | BPF_MEM | BPF_H:
	case BPF_STX | BPF_MEM | BPF_B:
	case BPF_STX | BPF_MEM | BPF_DW:
		len = store_r(buf, src, dst, off, BPF_SIZE(code));
		break;
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_B:
	case BPF_ST | BPF_MEM | BPF_DW:
		len = store_i(buf, imm, dst, off, BPF_SIZE(code));
		break;
	case BPF_JMP   | BPF_JA:
	case BPF_JMP   | BPF_JEQ  | BPF_X:
	case BPF_JMP   | BPF_JEQ  | BPF_K:
	case BPF_JMP   | BPF_JNE  | BPF_X:
	case BPF_JMP   | BPF_JNE  | BPF_K:
	case BPF_JMP   | BPF_JSET | BPF_X:
	case BPF_JMP   | BPF_JSET | BPF_K:
	case BPF_JMP   | BPF_JGT  | BPF_X:
	case BPF_JMP   | BPF_JGT  | BPF_K:
	case BPF_JMP   | BPF_JGE  | BPF_X:
	case BPF_JMP   | BPF_JGE  | BPF_K:
	case BPF_JMP   | BPF_JSGT | BPF_X:
	case BPF_JMP   | BPF_JSGT | BPF_K:
	case BPF_JMP   | BPF_JSGE | BPF_X:
	case BPF_JMP   | BPF_JSGE | BPF_K:
	case BPF_JMP   | BPF_JLT  | BPF_X:
	case BPF_JMP   | BPF_JLT  | BPF_K:
	case BPF_JMP   | BPF_JLE  | BPF_X:
	case BPF_JMP   | BPF_JLE  | BPF_K:
	case BPF_JMP   | BPF_JSLT | BPF_X:
	case BPF_JMP   | BPF_JSLT | BPF_K:
	case BPF_JMP   | BPF_JSLE | BPF_X:
	case BPF_JMP   | BPF_JSLE | BPF_K:
	case BPF_JMP32 | BPF_JA:
	case BPF_JMP32 | BPF_JEQ  | BPF_X:
	case BPF_JMP32 | BPF_JEQ  | BPF_K:
	case BPF_JMP32 | BPF_JNE  | BPF_X:
	case BPF_JMP32 | BPF_JNE  | BPF_K:
	case BPF_JMP32 | BPF_JSET | BPF_X:
	case BPF_JMP32 | BPF_JSET | BPF_K:
	case BPF_JMP32 | BPF_JGT  | BPF_X:
	case BPF_JMP32 | BPF_JGT  | BPF_K:
	case BPF_JMP32 | BPF_JGE  | BPF_X:
	case BPF_JMP32 | BPF_JGE  | BPF_K:
	case BPF_JMP32 | BPF_JSGT | BPF_X:
	case BPF_JMP32 | BPF_JSGT | BPF_K:
	case BPF_JMP32 | BPF_JSGE | BPF_X:
	case BPF_JMP32 | BPF_JSGE | BPF_K:
	case BPF_JMP32 | BPF_JLT  | BPF_X:
	case BPF_JMP32 | BPF_JLT  | BPF_K:
	case BPF_JMP32 | BPF_JLE  | BPF_X:
	case BPF_JMP32 | BPF_JLE  | BPF_K:
	case BPF_JMP32 | BPF_JSLT | BPF_X:
	case BPF_JMP32 | BPF_JSLT | BPF_K:
	case BPF_JMP32 | BPF_JSLE | BPF_X:
	case BPF_JMP32 | BPF_JSLE | BPF_K:
		CHECK_RET(handle_jumps(ctx, insn, &len));
		break;
	case BPF_JMP | BPF_CALL:
		CHECK_RET(handle_call(ctx, insn, &len));
		break;

	case BPF_JMP | BPF_EXIT:
		/* If this is the last instruction, epilogue will follow. */
		if (is_last_insn(ctx->prog, idx))
			break;
		CHECK_RET(handle_jmp_epilogue(ctx, insn, &len));
		break;
	default:
		pr_err("bpf-jit: can't handle instruction code 0x%02X\n", code);
		return -EOPNOTSUPP;
	}

	if (BPF_CLASS(code) == BPF_ALU) {
		/*
		 * Skip the "swap" instructions. Even 64-bit swaps are of type
		 * BPF_ALU (and not BPF_ALU64). Therefore, for the swaps, one
		 * has to look at the "size" of the operations rather than the
		 * ALU type. "gen_swap()" specifically takes care of that.
		 */
		if (BPF_OP(code) != BPF_END && ctx->do_zext)
			len += zext(BUF(buf, len), dst);
	}

	jit_buffer_update(ctx, len);

	return ret;
}

static int handle_body(struct jit_context *ctx)
{
	int ret;
	bool populate_bpf2insn = false;
	const struct bpf_prog *prog = ctx->prog;

	CHECK_RET(jit_buffer_check(ctx));

	/*
	 * Record the mapping for the instructions during the dry-run.
	 * Doing it this way allows us to have the mapping ready for
	 * the jump instructions during the real compilation phase.
	 */
	if (!ctx->emit)
		populate_bpf2insn = true;

	for (u32 i = 0; i < prog->len; i++) {
		/* During the dry-run, jit.len grows gradually per BPF insn. */
		if (populate_bpf2insn)
			ctx->bpf2insn[i] = ctx->jit.len;

		CHECK_RET(handle_insn(ctx, i));
		if (ret > 0) {
			/* "ret" is 1 if two (64-bit) chunks were consumed. */
			ctx->bpf2insn[i + 1] = ctx->bpf2insn[i];
			i++;
		}
	}

	/* If bpf2insn had to be populated, then it is done at this point. */
	if (populate_bpf2insn)
		ctx->bpf2insn_valid = true;

	return 0;
}

/*
 * Initialize the memory with "unimp_s" which is the mnemonic for
 * "unimplemented" instruction and always raises an exception.
 *
 * The instruction is 2 bytes. If "size" is odd, there is not much
 * that can be done about the last byte in "area". Because, the
 * CPU always fetches instructions in two bytes. Therefore, the
 * byte beyond the last one is going to accompany it during a
 * possible fetch. In the most likely case of a little endian
 * system, that beyond-byte will become the major opcode and
 * we have no control over its initialisation.
 */
static void fill_ill_insn(void *area, unsigned int size)
{
	const u16 unimp_s = 0x79e0;

	if (size & 1) {
		*((u8 *)area + (size - 1)) = 0xff;
		size -= 1;
	}

	memset16(area, unimp_s, size >> 1);
}

/* Piece of memory that can be allocated at the beginning of jit_prepare(). */
static int jit_prepare_early_mem_alloc(struct jit_context *ctx)
{
	ctx->bpf2insn = kcalloc(ctx->prog->len, sizeof(ctx->jit.len),
				GFP_KERNEL);

	if (!ctx->bpf2insn) {
		pr_err("bpf-jit: could not allocate memory for "
		       "mapping of the instructions.\n");
		return -ENOMEM;
	}

	return 0;
}

/*
 * Memory allocations that rely on parameters known at the end of
 * jit_prepare().
 */
static int jit_prepare_final_mem_alloc(struct jit_context *ctx)
{
	const size_t alignment = sizeof(u32);

	ctx->bpf_header = bpf_jit_binary_alloc(ctx->jit.len, &ctx->jit.buf,
					       alignment, fill_ill_insn);
	if (!ctx->bpf_header) {
		pr_err("bpf-jit: could not allocate memory for translation.\n");
		return -ENOMEM;
	}

	if (ctx->need_extra_pass) {
		ctx->jit_data = kzalloc(sizeof(*ctx->jit_data), GFP_KERNEL);
		if (!ctx->jit_data)
			return -ENOMEM;
	}

	return 0;
}

/*
 * The first phase of the translation without actually emitting any
 * instruction. It helps in getting a forecast on some aspects, such
 * as the length of the whole program or where the epilogue starts.
 *
 * Whenever the necessary parameters are known, memories are allocated.
 */
static int jit_prepare(struct jit_context *ctx)
{
	int ret;

	/* Dry run. */
	ctx->emit = false;

	CHECK_RET(jit_prepare_early_mem_alloc(ctx));

	/* Get the length of prologue section after some register analysis. */
	analyze_reg_usage(ctx);
	CHECK_RET(handle_prologue(ctx));

	CHECK_RET(handle_body(ctx));

	/* Record at which offset epilogue begins. */
	ctx->epilogue_offset = ctx->jit.len;

	/* Process the epilogue section now. */
	CHECK_RET(handle_epilogue(ctx));

	CHECK_RET(jit_prepare_final_mem_alloc(ctx));

	return 0;
}

/*
 * jit_compile() is the real compilation phase. jit_prepare() is
 * invoked before jit_compile() as a dry-run to make sure everything
 * will go OK and allocate the necessary memory.
 *
 * In the end, jit_compile() checks if it has produced the same number
 * of instructions as jit_prepare() would.
 */
static int jit_compile(struct jit_context *ctx)
{
	int ret;

	/* Let there be code. */
	ctx->emit = true;

	CHECK_RET(handle_prologue(ctx));

	CHECK_RET(handle_body(ctx));

	CHECK_RET(handle_epilogue(ctx));

	if (ctx->jit.index != ctx->jit.len) {
		pr_err("bpf-jit: divergence between the phases; "
		       "%u vs. %u (bytes).\n",
		       ctx->jit.len, ctx->jit.index);
		return -EFAULT;
	}

	return 0;
}

/*
 * Calling this function implies a successful JIT. A successful
 * translation is signaled by setting the right parameters:
 *
 * prog->jited=1, prog->jited_len=..., prog->bpf_func=...
 */
static int jit_finalize(struct jit_context *ctx)
{
	struct bpf_prog *prog = ctx->prog;

	/* We're going to need this information for the "do_extra_pass()". */
	if (ctx->need_extra_pass) {
		ctx->jit_data->bpf_header = ctx->bpf_header;
		ctx->jit_data->bpf2insn = ctx->bpf2insn;
		prog->aux->jit_data = (void *)ctx->jit_data;
	} else {
		/*
		 * If things seem finalised, then mark the JITed memory
		 * as R-X and flush it.
		 */
		if (bpf_jit_binary_lock_ro(ctx->bpf_header)) {
			pr_err("bpf-jit: Could not lock the JIT memory.\n");
			return -EFAULT;
		}
		flush_icache_range((unsigned long)ctx->bpf_header,
				   (unsigned long)
				   BUF(ctx->jit.buf, ctx->jit.len));
		prog->aux->jit_data = NULL;
		bpf_prog_fill_jited_linfo(prog, ctx->bpf2insn);
	}

	ctx->success = true;
	prog->bpf_func = (void *)ctx->jit.buf;
	prog->jited_len = ctx->jit.len;
	prog->jited = 1;

	jit_ctx_cleanup(ctx);
	jit_dump(ctx);

	return 0;
}

/*
 * A lenient verification for the existence of JIT context in "prog".
 * Apparently the JIT internals, namely jit_subprogs() in bpf/verifier.c,
 * may request for a second compilation although nothing needs to be done.
 */
static inline int check_jit_context(const struct bpf_prog *prog)
{
	if (!prog->aux->jit_data) {
		pr_notice("bpf-jit: no jit data for the extra pass.\n");
		return 1;
	} else {
		return 0;
	}
}

/* Reuse the previous pass's data. */
static int jit_resume_context(struct jit_context *ctx)
{
	struct arc_jit_data *jdata =
		(struct arc_jit_data *)ctx->prog->aux->jit_data;

	if (!jdata) {
		pr_err("bpf-jit: no jit data for the extra pass.\n");
		return -EINVAL;
	}

	ctx->jit.buf = (u8 *)ctx->prog->bpf_func;
	ctx->jit.len = ctx->prog->jited_len;
	ctx->bpf_header = jdata->bpf_header;
	ctx->bpf2insn = (u32 *)jdata->bpf2insn;
	ctx->bpf2insn_valid = ctx->bpf2insn ? true : false;
	ctx->jit_data = jdata;

	return 0;
}

/*
 * Patch in the new addresses. The instructions of interest are:
 *
 * - call
 * - ld r64, imm64
 *
 * For "call"s, it resolves the addresses one more time through the
 * handle_call().
 *
 * For 64-bit immediate loads, it just retranslates them, because the BPF
 * core in kernel might have changed the value since the normal pass.
 */
static int jit_patch_relocations(struct jit_context *ctx)
{
	const u8 bpf_opc_call = BPF_JMP | BPF_CALL;
	const u8 bpf_opc_ldi64 = BPF_LD | BPF_DW | BPF_IMM;
	const struct bpf_prog *prog = ctx->prog;
	int ret;

	ctx->emit = true;
	for (u32 i = 0; i < prog->len; i++) {
		const struct bpf_insn *insn = &prog->insnsi[i];
		u8 dummy;
		/*
		 * Adjust "ctx.jit.index", so "gen_*()" functions below
		 * can use it for their output addresses.
		 */
		ctx->jit.index = ctx->bpf2insn[i];

		if (insn->code == bpf_opc_call) {
			CHECK_RET(handle_call(ctx, insn, &dummy));
		} else if (insn->code == bpf_opc_ldi64) {
			CHECK_RET(handle_ld_imm64(ctx, insn, &dummy));
			/* Skip the next instruction. */
			++i;
		}
	}
	return 0;
}

/*
 * A normal pass that involves a "dry-run" phase, jit_prepare(),
 * to get the necessary data for the real compilation phase,
 * jit_compile().
 */
static struct bpf_prog *do_normal_pass(struct bpf_prog *prog)
{
	struct jit_context ctx;

	/* Bail out if JIT is disabled. */
	if (!prog->jit_requested)
		return prog;

	if (jit_ctx_init(&ctx, prog)) {
		jit_ctx_cleanup(&ctx);
		return prog;
	}

	/* Get the lengths and allocate buffer. */
	if (jit_prepare(&ctx)) {
		jit_ctx_cleanup(&ctx);
		return prog;
	}

	if (jit_compile(&ctx)) {
		jit_ctx_cleanup(&ctx);
		return prog;
	}

	if (jit_finalize(&ctx)) {
		jit_ctx_cleanup(&ctx);
		return prog;
	}

	return ctx.prog;
}

/*
 * If there are multi-function BPF programs that call each other,
 * their translated addresses are not known all at once. Therefore,
 * an extra pass is needed to consult the bpf_jit_get_func_addr()
 * again to get the newly translated addresses in order to resolve
 * the "call"s.
 */
static struct bpf_prog *do_extra_pass(struct bpf_prog *prog)
{
	struct jit_context ctx;

	/* Skip if there's no context to resume from. */
	if (check_jit_context(prog))
		return prog;

	if (jit_ctx_init(&ctx, prog)) {
		jit_ctx_cleanup(&ctx);
		return prog;
	}

	if (jit_resume_context(&ctx)) {
		jit_ctx_cleanup(&ctx);
		return prog;
	}

	if (jit_patch_relocations(&ctx)) {
		jit_ctx_cleanup(&ctx);
		return prog;
	}

	if (jit_finalize(&ctx)) {
		jit_ctx_cleanup(&ctx);
		return prog;
	}

	return ctx.prog;
}

/*
 * This function may be invoked twice for the same stream of BPF
 * instructions. The "extra pass" happens, when there are
 * (re)locations involved that their addresses are not known
 * during the first run.
 */
struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
{
	vm_dump(prog);

	/* Was this program already translated? */
	if (!prog->jited)
		return do_normal_pass(prog);
	else
		return do_extra_pass(prog);

	return prog;
}
