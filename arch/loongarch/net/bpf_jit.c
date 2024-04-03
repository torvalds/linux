// SPDX-License-Identifier: GPL-2.0-only
/*
 * BPF JIT compiler for LoongArch
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */
#include "bpf_jit.h"

#define REG_TCC		LOONGARCH_GPR_A6
#define TCC_SAVED	LOONGARCH_GPR_S5

#define SAVE_RA		BIT(0)
#define SAVE_TCC	BIT(1)

static const int regmap[] = {
	/* return value from in-kernel function, and exit value for eBPF program */
	[BPF_REG_0] = LOONGARCH_GPR_A5,
	/* arguments from eBPF program to in-kernel function */
	[BPF_REG_1] = LOONGARCH_GPR_A0,
	[BPF_REG_2] = LOONGARCH_GPR_A1,
	[BPF_REG_3] = LOONGARCH_GPR_A2,
	[BPF_REG_4] = LOONGARCH_GPR_A3,
	[BPF_REG_5] = LOONGARCH_GPR_A4,
	/* callee saved registers that in-kernel function will preserve */
	[BPF_REG_6] = LOONGARCH_GPR_S0,
	[BPF_REG_7] = LOONGARCH_GPR_S1,
	[BPF_REG_8] = LOONGARCH_GPR_S2,
	[BPF_REG_9] = LOONGARCH_GPR_S3,
	/* read-only frame pointer to access stack */
	[BPF_REG_FP] = LOONGARCH_GPR_S4,
	/* temporary register for blinding constants */
	[BPF_REG_AX] = LOONGARCH_GPR_T0,
};

static void mark_call(struct jit_ctx *ctx)
{
	ctx->flags |= SAVE_RA;
}

static void mark_tail_call(struct jit_ctx *ctx)
{
	ctx->flags |= SAVE_TCC;
}

static bool seen_call(struct jit_ctx *ctx)
{
	return (ctx->flags & SAVE_RA);
}

static bool seen_tail_call(struct jit_ctx *ctx)
{
	return (ctx->flags & SAVE_TCC);
}

static u8 tail_call_reg(struct jit_ctx *ctx)
{
	if (seen_call(ctx))
		return TCC_SAVED;

	return REG_TCC;
}

/*
 * eBPF prog stack layout:
 *
 *                                        high
 * original $sp ------------> +-------------------------+ <--LOONGARCH_GPR_FP
 *                            |           $ra           |
 *                            +-------------------------+
 *                            |           $fp           |
 *                            +-------------------------+
 *                            |           $s0           |
 *                            +-------------------------+
 *                            |           $s1           |
 *                            +-------------------------+
 *                            |           $s2           |
 *                            +-------------------------+
 *                            |           $s3           |
 *                            +-------------------------+
 *                            |           $s4           |
 *                            +-------------------------+
 *                            |           $s5           |
 *                            +-------------------------+ <--BPF_REG_FP
 *                            |  prog->aux->stack_depth |
 *                            |        (optional)       |
 * current $sp -------------> +-------------------------+
 *                                        low
 */
static void build_prologue(struct jit_ctx *ctx)
{
	int stack_adjust = 0, store_offset, bpf_stack_adjust;

	bpf_stack_adjust = round_up(ctx->prog->aux->stack_depth, 16);

	/* To store ra, fp, s0, s1, s2, s3, s4 and s5. */
	stack_adjust += sizeof(long) * 8;

	stack_adjust = round_up(stack_adjust, 16);
	stack_adjust += bpf_stack_adjust;

	/*
	 * First instruction initializes the tail call count (TCC).
	 * On tail call we skip this instruction, and the TCC is
	 * passed in REG_TCC from the caller.
	 */
	emit_insn(ctx, addid, REG_TCC, LOONGARCH_GPR_ZERO, MAX_TAIL_CALL_CNT);

	emit_insn(ctx, addid, LOONGARCH_GPR_SP, LOONGARCH_GPR_SP, -stack_adjust);

	store_offset = stack_adjust - sizeof(long);
	emit_insn(ctx, std, LOONGARCH_GPR_RA, LOONGARCH_GPR_SP, store_offset);

	store_offset -= sizeof(long);
	emit_insn(ctx, std, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, store_offset);

	store_offset -= sizeof(long);
	emit_insn(ctx, std, LOONGARCH_GPR_S0, LOONGARCH_GPR_SP, store_offset);

	store_offset -= sizeof(long);
	emit_insn(ctx, std, LOONGARCH_GPR_S1, LOONGARCH_GPR_SP, store_offset);

	store_offset -= sizeof(long);
	emit_insn(ctx, std, LOONGARCH_GPR_S2, LOONGARCH_GPR_SP, store_offset);

	store_offset -= sizeof(long);
	emit_insn(ctx, std, LOONGARCH_GPR_S3, LOONGARCH_GPR_SP, store_offset);

	store_offset -= sizeof(long);
	emit_insn(ctx, std, LOONGARCH_GPR_S4, LOONGARCH_GPR_SP, store_offset);

	store_offset -= sizeof(long);
	emit_insn(ctx, std, LOONGARCH_GPR_S5, LOONGARCH_GPR_SP, store_offset);

	emit_insn(ctx, addid, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, stack_adjust);

	if (bpf_stack_adjust)
		emit_insn(ctx, addid, regmap[BPF_REG_FP], LOONGARCH_GPR_SP, bpf_stack_adjust);

	/*
	 * Program contains calls and tail calls, so REG_TCC need
	 * to be saved across calls.
	 */
	if (seen_tail_call(ctx) && seen_call(ctx))
		move_reg(ctx, TCC_SAVED, REG_TCC);

	ctx->stack_size = stack_adjust;
}

static void __build_epilogue(struct jit_ctx *ctx, bool is_tail_call)
{
	int stack_adjust = ctx->stack_size;
	int load_offset;

	load_offset = stack_adjust - sizeof(long);
	emit_insn(ctx, ldd, LOONGARCH_GPR_RA, LOONGARCH_GPR_SP, load_offset);

	load_offset -= sizeof(long);
	emit_insn(ctx, ldd, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, load_offset);

	load_offset -= sizeof(long);
	emit_insn(ctx, ldd, LOONGARCH_GPR_S0, LOONGARCH_GPR_SP, load_offset);

	load_offset -= sizeof(long);
	emit_insn(ctx, ldd, LOONGARCH_GPR_S1, LOONGARCH_GPR_SP, load_offset);

	load_offset -= sizeof(long);
	emit_insn(ctx, ldd, LOONGARCH_GPR_S2, LOONGARCH_GPR_SP, load_offset);

	load_offset -= sizeof(long);
	emit_insn(ctx, ldd, LOONGARCH_GPR_S3, LOONGARCH_GPR_SP, load_offset);

	load_offset -= sizeof(long);
	emit_insn(ctx, ldd, LOONGARCH_GPR_S4, LOONGARCH_GPR_SP, load_offset);

	load_offset -= sizeof(long);
	emit_insn(ctx, ldd, LOONGARCH_GPR_S5, LOONGARCH_GPR_SP, load_offset);

	emit_insn(ctx, addid, LOONGARCH_GPR_SP, LOONGARCH_GPR_SP, stack_adjust);

	if (!is_tail_call) {
		/* Set return value */
		move_reg(ctx, LOONGARCH_GPR_A0, regmap[BPF_REG_0]);
		/* Return to the caller */
		emit_insn(ctx, jirl, LOONGARCH_GPR_RA, LOONGARCH_GPR_ZERO, 0);
	} else {
		/*
		 * Call the next bpf prog and skip the first instruction
		 * of TCC initialization.
		 */
		emit_insn(ctx, jirl, LOONGARCH_GPR_T3, LOONGARCH_GPR_ZERO, 1);
	}
}

static void build_epilogue(struct jit_ctx *ctx)
{
	__build_epilogue(ctx, false);
}

bool bpf_jit_supports_kfunc_call(void)
{
	return true;
}

bool bpf_jit_supports_far_kfunc_call(void)
{
	return true;
}

/* initialized on the first pass of build_body() */
static int out_offset = -1;
static int emit_bpf_tail_call(struct jit_ctx *ctx)
{
	int off;
	u8 tcc = tail_call_reg(ctx);
	u8 a1 = LOONGARCH_GPR_A1;
	u8 a2 = LOONGARCH_GPR_A2;
	u8 t1 = LOONGARCH_GPR_T1;
	u8 t2 = LOONGARCH_GPR_T2;
	u8 t3 = LOONGARCH_GPR_T3;
	const int idx0 = ctx->idx;

#define cur_offset (ctx->idx - idx0)
#define jmp_offset (out_offset - (cur_offset))

	/*
	 * a0: &ctx
	 * a1: &array
	 * a2: index
	 *
	 * if (index >= array->map.max_entries)
	 *	 goto out;
	 */
	off = offsetof(struct bpf_array, map.max_entries);
	emit_insn(ctx, ldwu, t1, a1, off);
	/* bgeu $a2, $t1, jmp_offset */
	if (emit_tailcall_jmp(ctx, BPF_JGE, a2, t1, jmp_offset) < 0)
		goto toofar;

	/*
	 * if (--TCC < 0)
	 *	 goto out;
	 */
	emit_insn(ctx, addid, REG_TCC, tcc, -1);
	if (emit_tailcall_jmp(ctx, BPF_JSLT, REG_TCC, LOONGARCH_GPR_ZERO, jmp_offset) < 0)
		goto toofar;

	/*
	 * prog = array->ptrs[index];
	 * if (!prog)
	 *	 goto out;
	 */
	emit_insn(ctx, alsld, t2, a2, a1, 2);
	off = offsetof(struct bpf_array, ptrs);
	emit_insn(ctx, ldd, t2, t2, off);
	/* beq $t2, $zero, jmp_offset */
	if (emit_tailcall_jmp(ctx, BPF_JEQ, t2, LOONGARCH_GPR_ZERO, jmp_offset) < 0)
		goto toofar;

	/* goto *(prog->bpf_func + 4); */
	off = offsetof(struct bpf_prog, bpf_func);
	emit_insn(ctx, ldd, t3, t2, off);
	__build_epilogue(ctx, true);

	/* out: */
	if (out_offset == -1)
		out_offset = cur_offset;
	if (cur_offset != out_offset) {
		pr_err_once("tail_call out_offset = %d, expected %d!\n",
			    cur_offset, out_offset);
		return -1;
	}

	return 0;

toofar:
	pr_info_once("tail_call: jump too far\n");
	return -1;
#undef cur_offset
#undef jmp_offset
}

static void emit_atomic(const struct bpf_insn *insn, struct jit_ctx *ctx)
{
	const u8 t1 = LOONGARCH_GPR_T1;
	const u8 t2 = LOONGARCH_GPR_T2;
	const u8 t3 = LOONGARCH_GPR_T3;
	const u8 r0 = regmap[BPF_REG_0];
	const u8 src = regmap[insn->src_reg];
	const u8 dst = regmap[insn->dst_reg];
	const s16 off = insn->off;
	const s32 imm = insn->imm;
	const bool isdw = BPF_SIZE(insn->code) == BPF_DW;

	move_imm(ctx, t1, off, false);
	emit_insn(ctx, addd, t1, dst, t1);
	move_reg(ctx, t3, src);

	switch (imm) {
	/* lock *(size *)(dst + off) <op>= src */
	case BPF_ADD:
		if (isdw)
			emit_insn(ctx, amaddd, t2, t1, src);
		else
			emit_insn(ctx, amaddw, t2, t1, src);
		break;
	case BPF_AND:
		if (isdw)
			emit_insn(ctx, amandd, t2, t1, src);
		else
			emit_insn(ctx, amandw, t2, t1, src);
		break;
	case BPF_OR:
		if (isdw)
			emit_insn(ctx, amord, t2, t1, src);
		else
			emit_insn(ctx, amorw, t2, t1, src);
		break;
	case BPF_XOR:
		if (isdw)
			emit_insn(ctx, amxord, t2, t1, src);
		else
			emit_insn(ctx, amxorw, t2, t1, src);
		break;
	/* src = atomic_fetch_<op>(dst + off, src) */
	case BPF_ADD | BPF_FETCH:
		if (isdw) {
			emit_insn(ctx, amaddd, src, t1, t3);
		} else {
			emit_insn(ctx, amaddw, src, t1, t3);
			emit_zext_32(ctx, src, true);
		}
		break;
	case BPF_AND | BPF_FETCH:
		if (isdw) {
			emit_insn(ctx, amandd, src, t1, t3);
		} else {
			emit_insn(ctx, amandw, src, t1, t3);
			emit_zext_32(ctx, src, true);
		}
		break;
	case BPF_OR | BPF_FETCH:
		if (isdw) {
			emit_insn(ctx, amord, src, t1, t3);
		} else {
			emit_insn(ctx, amorw, src, t1, t3);
			emit_zext_32(ctx, src, true);
		}
		break;
	case BPF_XOR | BPF_FETCH:
		if (isdw) {
			emit_insn(ctx, amxord, src, t1, t3);
		} else {
			emit_insn(ctx, amxorw, src, t1, t3);
			emit_zext_32(ctx, src, true);
		}
		break;
	/* src = atomic_xchg(dst + off, src); */
	case BPF_XCHG:
		if (isdw) {
			emit_insn(ctx, amswapd, src, t1, t3);
		} else {
			emit_insn(ctx, amswapw, src, t1, t3);
			emit_zext_32(ctx, src, true);
		}
		break;
	/* r0 = atomic_cmpxchg(dst + off, r0, src); */
	case BPF_CMPXCHG:
		move_reg(ctx, t2, r0);
		if (isdw) {
			emit_insn(ctx, lld, r0, t1, 0);
			emit_insn(ctx, bne, t2, r0, 4);
			move_reg(ctx, t3, src);
			emit_insn(ctx, scd, t3, t1, 0);
			emit_insn(ctx, beq, t3, LOONGARCH_GPR_ZERO, -4);
		} else {
			emit_insn(ctx, llw, r0, t1, 0);
			emit_zext_32(ctx, t2, true);
			emit_zext_32(ctx, r0, true);
			emit_insn(ctx, bne, t2, r0, 4);
			move_reg(ctx, t3, src);
			emit_insn(ctx, scw, t3, t1, 0);
			emit_insn(ctx, beq, t3, LOONGARCH_GPR_ZERO, -6);
			emit_zext_32(ctx, r0, true);
		}
		break;
	}
}

static bool is_signed_bpf_cond(u8 cond)
{
	return cond == BPF_JSGT || cond == BPF_JSLT ||
	       cond == BPF_JSGE || cond == BPF_JSLE;
}

#define BPF_FIXUP_REG_MASK	GENMASK(31, 27)
#define BPF_FIXUP_OFFSET_MASK	GENMASK(26, 0)

bool ex_handler_bpf(const struct exception_table_entry *ex,
		    struct pt_regs *regs)
{
	int dst_reg = FIELD_GET(BPF_FIXUP_REG_MASK, ex->fixup);
	off_t offset = FIELD_GET(BPF_FIXUP_OFFSET_MASK, ex->fixup);

	regs->regs[dst_reg] = 0;
	regs->csr_era = (unsigned long)&ex->fixup - offset;

	return true;
}

/* For accesses to BTF pointers, add an entry to the exception table */
static int add_exception_handler(const struct bpf_insn *insn,
				 struct jit_ctx *ctx,
				 int dst_reg)
{
	unsigned long pc;
	off_t offset;
	struct exception_table_entry *ex;

	if (!ctx->image || !ctx->prog->aux->extable)
		return 0;

	if (BPF_MODE(insn->code) != BPF_PROBE_MEM &&
	    BPF_MODE(insn->code) != BPF_PROBE_MEMSX)
		return 0;

	if (WARN_ON_ONCE(ctx->num_exentries >= ctx->prog->aux->num_exentries))
		return -EINVAL;

	ex = &ctx->prog->aux->extable[ctx->num_exentries];
	pc = (unsigned long)&ctx->image[ctx->idx - 1];

	offset = pc - (long)&ex->insn;
	if (WARN_ON_ONCE(offset >= 0 || offset < INT_MIN))
		return -ERANGE;

	ex->insn = offset;

	/*
	 * Since the extable follows the program, the fixup offset is always
	 * negative and limited to BPF_JIT_REGION_SIZE. Store a positive value
	 * to keep things simple, and put the destination register in the upper
	 * bits. We don't need to worry about buildtime or runtime sort
	 * modifying the upper bits because the table is already sorted, and
	 * isn't part of the main exception table.
	 */
	offset = (long)&ex->fixup - (pc + LOONGARCH_INSN_SIZE);
	if (!FIELD_FIT(BPF_FIXUP_OFFSET_MASK, offset))
		return -ERANGE;

	ex->type = EX_TYPE_BPF;
	ex->fixup = FIELD_PREP(BPF_FIXUP_OFFSET_MASK, offset) | FIELD_PREP(BPF_FIXUP_REG_MASK, dst_reg);

	ctx->num_exentries++;

	return 0;
}

static int build_insn(const struct bpf_insn *insn, struct jit_ctx *ctx, bool extra_pass)
{
	u8 tm = -1;
	u64 func_addr;
	bool func_addr_fixed, sign_extend;
	int i = insn - ctx->prog->insnsi;
	int ret, jmp_offset;
	const u8 code = insn->code;
	const u8 cond = BPF_OP(code);
	const u8 t1 = LOONGARCH_GPR_T1;
	const u8 t2 = LOONGARCH_GPR_T2;
	const u8 src = regmap[insn->src_reg];
	const u8 dst = regmap[insn->dst_reg];
	const s16 off = insn->off;
	const s32 imm = insn->imm;
	const bool is32 = BPF_CLASS(insn->code) == BPF_ALU || BPF_CLASS(insn->code) == BPF_JMP32;

	switch (code) {
	/* dst = src */
	case BPF_ALU | BPF_MOV | BPF_X:
	case BPF_ALU64 | BPF_MOV | BPF_X:
		switch (off) {
		case 0:
			move_reg(ctx, dst, src);
			emit_zext_32(ctx, dst, is32);
			break;
		case 8:
			move_reg(ctx, t1, src);
			emit_insn(ctx, extwb, dst, t1);
			emit_zext_32(ctx, dst, is32);
			break;
		case 16:
			move_reg(ctx, t1, src);
			emit_insn(ctx, extwh, dst, t1);
			emit_zext_32(ctx, dst, is32);
			break;
		case 32:
			emit_insn(ctx, addw, dst, src, LOONGARCH_GPR_ZERO);
			break;
		}
		break;

	/* dst = imm */
	case BPF_ALU | BPF_MOV | BPF_K:
	case BPF_ALU64 | BPF_MOV | BPF_K:
		move_imm(ctx, dst, imm, is32);
		break;

	/* dst = dst + src */
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_X:
		emit_insn(ctx, addd, dst, dst, src);
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst + imm */
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_ADD | BPF_K:
		if (is_signed_imm12(imm)) {
			emit_insn(ctx, addid, dst, dst, imm);
		} else {
			move_imm(ctx, t1, imm, is32);
			emit_insn(ctx, addd, dst, dst, t1);
		}
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst - src */
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
		emit_insn(ctx, subd, dst, dst, src);
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst - imm */
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
		if (is_signed_imm12(-imm)) {
			emit_insn(ctx, addid, dst, dst, -imm);
		} else {
			move_imm(ctx, t1, imm, is32);
			emit_insn(ctx, subd, dst, dst, t1);
		}
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst * src */
	case BPF_ALU | BPF_MUL | BPF_X:
	case BPF_ALU64 | BPF_MUL | BPF_X:
		emit_insn(ctx, muld, dst, dst, src);
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst * imm */
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU64 | BPF_MUL | BPF_K:
		move_imm(ctx, t1, imm, is32);
		emit_insn(ctx, muld, dst, dst, t1);
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst / src */
	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_DIV | BPF_X:
		if (!off) {
			emit_zext_32(ctx, dst, is32);
			move_reg(ctx, t1, src);
			emit_zext_32(ctx, t1, is32);
			emit_insn(ctx, divdu, dst, dst, t1);
			emit_zext_32(ctx, dst, is32);
		} else {
			emit_sext_32(ctx, dst, is32);
			move_reg(ctx, t1, src);
			emit_sext_32(ctx, t1, is32);
			emit_insn(ctx, divd, dst, dst, t1);
			emit_sext_32(ctx, dst, is32);
		}
		break;

	/* dst = dst / imm */
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_DIV | BPF_K:
		if (!off) {
			move_imm(ctx, t1, imm, is32);
			emit_zext_32(ctx, dst, is32);
			emit_insn(ctx, divdu, dst, dst, t1);
			emit_zext_32(ctx, dst, is32);
		} else {
			move_imm(ctx, t1, imm, false);
			emit_sext_32(ctx, t1, is32);
			emit_sext_32(ctx, dst, is32);
			emit_insn(ctx, divd, dst, dst, t1);
			emit_sext_32(ctx, dst, is32);
		}
		break;

	/* dst = dst % src */
	case BPF_ALU | BPF_MOD | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
		if (!off) {
			emit_zext_32(ctx, dst, is32);
			move_reg(ctx, t1, src);
			emit_zext_32(ctx, t1, is32);
			emit_insn(ctx, moddu, dst, dst, t1);
			emit_zext_32(ctx, dst, is32);
		} else {
			emit_sext_32(ctx, dst, is32);
			move_reg(ctx, t1, src);
			emit_sext_32(ctx, t1, is32);
			emit_insn(ctx, modd, dst, dst, t1);
			emit_sext_32(ctx, dst, is32);
		}
		break;

	/* dst = dst % imm */
	case BPF_ALU | BPF_MOD | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		if (!off) {
			move_imm(ctx, t1, imm, is32);
			emit_zext_32(ctx, dst, is32);
			emit_insn(ctx, moddu, dst, dst, t1);
			emit_zext_32(ctx, dst, is32);
		} else {
			move_imm(ctx, t1, imm, false);
			emit_sext_32(ctx, t1, is32);
			emit_sext_32(ctx, dst, is32);
			emit_insn(ctx, modd, dst, dst, t1);
			emit_sext_32(ctx, dst, is32);
		}
		break;

	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
	case BPF_ALU64 | BPF_NEG:
		move_imm(ctx, t1, imm, is32);
		emit_insn(ctx, subd, dst, LOONGARCH_GPR_ZERO, dst);
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst & src */
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_AND | BPF_X:
		emit_insn(ctx, and, dst, dst, src);
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst & imm */
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU64 | BPF_AND | BPF_K:
		if (is_unsigned_imm12(imm)) {
			emit_insn(ctx, andi, dst, dst, imm);
		} else {
			move_imm(ctx, t1, imm, is32);
			emit_insn(ctx, and, dst, dst, t1);
		}
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst | src */
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
		emit_insn(ctx, or, dst, dst, src);
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst | imm */
	case BPF_ALU | BPF_OR | BPF_K:
	case BPF_ALU64 | BPF_OR | BPF_K:
		if (is_unsigned_imm12(imm)) {
			emit_insn(ctx, ori, dst, dst, imm);
		} else {
			move_imm(ctx, t1, imm, is32);
			emit_insn(ctx, or, dst, dst, t1);
		}
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst ^ src */
	case BPF_ALU | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
		emit_insn(ctx, xor, dst, dst, src);
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst ^ imm */
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
		if (is_unsigned_imm12(imm)) {
			emit_insn(ctx, xori, dst, dst, imm);
		} else {
			move_imm(ctx, t1, imm, is32);
			emit_insn(ctx, xor, dst, dst, t1);
		}
		emit_zext_32(ctx, dst, is32);
		break;

	/* dst = dst << src (logical) */
	case BPF_ALU | BPF_LSH | BPF_X:
		emit_insn(ctx, sllw, dst, dst, src);
		emit_zext_32(ctx, dst, is32);
		break;

	case BPF_ALU64 | BPF_LSH | BPF_X:
		emit_insn(ctx, slld, dst, dst, src);
		break;

	/* dst = dst << imm (logical) */
	case BPF_ALU | BPF_LSH | BPF_K:
		emit_insn(ctx, slliw, dst, dst, imm);
		emit_zext_32(ctx, dst, is32);
		break;

	case BPF_ALU64 | BPF_LSH | BPF_K:
		emit_insn(ctx, sllid, dst, dst, imm);
		break;

	/* dst = dst >> src (logical) */
	case BPF_ALU | BPF_RSH | BPF_X:
		emit_insn(ctx, srlw, dst, dst, src);
		emit_zext_32(ctx, dst, is32);
		break;

	case BPF_ALU64 | BPF_RSH | BPF_X:
		emit_insn(ctx, srld, dst, dst, src);
		break;

	/* dst = dst >> imm (logical) */
	case BPF_ALU | BPF_RSH | BPF_K:
		emit_insn(ctx, srliw, dst, dst, imm);
		emit_zext_32(ctx, dst, is32);
		break;

	case BPF_ALU64 | BPF_RSH | BPF_K:
		emit_insn(ctx, srlid, dst, dst, imm);
		break;

	/* dst = dst >> src (arithmetic) */
	case BPF_ALU | BPF_ARSH | BPF_X:
		emit_insn(ctx, sraw, dst, dst, src);
		emit_zext_32(ctx, dst, is32);
		break;

	case BPF_ALU64 | BPF_ARSH | BPF_X:
		emit_insn(ctx, srad, dst, dst, src);
		break;

	/* dst = dst >> imm (arithmetic) */
	case BPF_ALU | BPF_ARSH | BPF_K:
		emit_insn(ctx, sraiw, dst, dst, imm);
		emit_zext_32(ctx, dst, is32);
		break;

	case BPF_ALU64 | BPF_ARSH | BPF_K:
		emit_insn(ctx, sraid, dst, dst, imm);
		break;

	/* dst = BSWAP##imm(dst) */
	case BPF_ALU | BPF_END | BPF_FROM_LE:
		switch (imm) {
		case 16:
			/* zero-extend 16 bits into 64 bits */
			emit_insn(ctx, bstrpickd, dst, dst, 15, 0);
			break;
		case 32:
			/* zero-extend 32 bits into 64 bits */
			emit_zext_32(ctx, dst, is32);
			break;
		case 64:
			/* do nothing */
			break;
		}
		break;

	case BPF_ALU | BPF_END | BPF_FROM_BE:
	case BPF_ALU64 | BPF_END | BPF_FROM_LE:
		switch (imm) {
		case 16:
			emit_insn(ctx, revb2h, dst, dst);
			/* zero-extend 16 bits into 64 bits */
			emit_insn(ctx, bstrpickd, dst, dst, 15, 0);
			break;
		case 32:
			emit_insn(ctx, revb2w, dst, dst);
			/* clear the upper 32 bits */
			emit_zext_32(ctx, dst, true);
			break;
		case 64:
			emit_insn(ctx, revbd, dst, dst);
			break;
		}
		break;

	/* PC += off if dst cond src */
	case BPF_JMP | BPF_JEQ | BPF_X:
	case BPF_JMP | BPF_JNE | BPF_X:
	case BPF_JMP | BPF_JGT | BPF_X:
	case BPF_JMP | BPF_JGE | BPF_X:
	case BPF_JMP | BPF_JLT | BPF_X:
	case BPF_JMP | BPF_JLE | BPF_X:
	case BPF_JMP | BPF_JSGT | BPF_X:
	case BPF_JMP | BPF_JSGE | BPF_X:
	case BPF_JMP | BPF_JSLT | BPF_X:
	case BPF_JMP | BPF_JSLE | BPF_X:
	case BPF_JMP32 | BPF_JEQ | BPF_X:
	case BPF_JMP32 | BPF_JNE | BPF_X:
	case BPF_JMP32 | BPF_JGT | BPF_X:
	case BPF_JMP32 | BPF_JGE | BPF_X:
	case BPF_JMP32 | BPF_JLT | BPF_X:
	case BPF_JMP32 | BPF_JLE | BPF_X:
	case BPF_JMP32 | BPF_JSGT | BPF_X:
	case BPF_JMP32 | BPF_JSGE | BPF_X:
	case BPF_JMP32 | BPF_JSLT | BPF_X:
	case BPF_JMP32 | BPF_JSLE | BPF_X:
		jmp_offset = bpf2la_offset(i, off, ctx);
		move_reg(ctx, t1, dst);
		move_reg(ctx, t2, src);
		if (is_signed_bpf_cond(BPF_OP(code))) {
			emit_sext_32(ctx, t1, is32);
			emit_sext_32(ctx, t2, is32);
		} else {
			emit_zext_32(ctx, t1, is32);
			emit_zext_32(ctx, t2, is32);
		}
		if (emit_cond_jmp(ctx, cond, t1, t2, jmp_offset) < 0)
			goto toofar;
		break;

	/* PC += off if dst cond imm */
	case BPF_JMP | BPF_JEQ | BPF_K:
	case BPF_JMP | BPF_JNE | BPF_K:
	case BPF_JMP | BPF_JGT | BPF_K:
	case BPF_JMP | BPF_JGE | BPF_K:
	case BPF_JMP | BPF_JLT | BPF_K:
	case BPF_JMP | BPF_JLE | BPF_K:
	case BPF_JMP | BPF_JSGT | BPF_K:
	case BPF_JMP | BPF_JSGE | BPF_K:
	case BPF_JMP | BPF_JSLT | BPF_K:
	case BPF_JMP | BPF_JSLE | BPF_K:
	case BPF_JMP32 | BPF_JEQ | BPF_K:
	case BPF_JMP32 | BPF_JNE | BPF_K:
	case BPF_JMP32 | BPF_JGT | BPF_K:
	case BPF_JMP32 | BPF_JGE | BPF_K:
	case BPF_JMP32 | BPF_JLT | BPF_K:
	case BPF_JMP32 | BPF_JLE | BPF_K:
	case BPF_JMP32 | BPF_JSGT | BPF_K:
	case BPF_JMP32 | BPF_JSGE | BPF_K:
	case BPF_JMP32 | BPF_JSLT | BPF_K:
	case BPF_JMP32 | BPF_JSLE | BPF_K:
		jmp_offset = bpf2la_offset(i, off, ctx);
		if (imm) {
			move_imm(ctx, t1, imm, false);
			tm = t1;
		} else {
			/* If imm is 0, simply use zero register. */
			tm = LOONGARCH_GPR_ZERO;
		}
		move_reg(ctx, t2, dst);
		if (is_signed_bpf_cond(BPF_OP(code))) {
			emit_sext_32(ctx, tm, is32);
			emit_sext_32(ctx, t2, is32);
		} else {
			emit_zext_32(ctx, tm, is32);
			emit_zext_32(ctx, t2, is32);
		}
		if (emit_cond_jmp(ctx, cond, t2, tm, jmp_offset) < 0)
			goto toofar;
		break;

	/* PC += off if dst & src */
	case BPF_JMP | BPF_JSET | BPF_X:
	case BPF_JMP32 | BPF_JSET | BPF_X:
		jmp_offset = bpf2la_offset(i, off, ctx);
		emit_insn(ctx, and, t1, dst, src);
		emit_zext_32(ctx, t1, is32);
		if (emit_cond_jmp(ctx, cond, t1, LOONGARCH_GPR_ZERO, jmp_offset) < 0)
			goto toofar;
		break;

	/* PC += off if dst & imm */
	case BPF_JMP | BPF_JSET | BPF_K:
	case BPF_JMP32 | BPF_JSET | BPF_K:
		jmp_offset = bpf2la_offset(i, off, ctx);
		move_imm(ctx, t1, imm, is32);
		emit_insn(ctx, and, t1, dst, t1);
		emit_zext_32(ctx, t1, is32);
		if (emit_cond_jmp(ctx, cond, t1, LOONGARCH_GPR_ZERO, jmp_offset) < 0)
			goto toofar;
		break;

	/* PC += off */
	case BPF_JMP | BPF_JA:
	case BPF_JMP32 | BPF_JA:
		if (BPF_CLASS(code) == BPF_JMP)
			jmp_offset = bpf2la_offset(i, off, ctx);
		else
			jmp_offset = bpf2la_offset(i, imm, ctx);
		if (emit_uncond_jmp(ctx, jmp_offset) < 0)
			goto toofar;
		break;

	/* function call */
	case BPF_JMP | BPF_CALL:
		mark_call(ctx);
		ret = bpf_jit_get_func_addr(ctx->prog, insn, extra_pass,
					    &func_addr, &func_addr_fixed);
		if (ret < 0)
			return ret;

		move_addr(ctx, t1, func_addr);
		emit_insn(ctx, jirl, t1, LOONGARCH_GPR_RA, 0);
		move_reg(ctx, regmap[BPF_REG_0], LOONGARCH_GPR_A0);
		break;

	/* tail call */
	case BPF_JMP | BPF_TAIL_CALL:
		mark_tail_call(ctx);
		if (emit_bpf_tail_call(ctx) < 0)
			return -EINVAL;
		break;

	/* function return */
	case BPF_JMP | BPF_EXIT:
		if (i == ctx->prog->len - 1)
			break;

		jmp_offset = epilogue_offset(ctx);
		if (emit_uncond_jmp(ctx, jmp_offset) < 0)
			goto toofar;
		break;

	/* dst = imm64 */
	case BPF_LD | BPF_IMM | BPF_DW:
	{
		const u64 imm64 = (u64)(insn + 1)->imm << 32 | (u32)insn->imm;

		move_imm(ctx, dst, imm64, is32);
		return 1;
	}

	/* dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_DW:
	case BPF_LDX | BPF_PROBE_MEM | BPF_DW:
	case BPF_LDX | BPF_PROBE_MEM | BPF_W:
	case BPF_LDX | BPF_PROBE_MEM | BPF_H:
	case BPF_LDX | BPF_PROBE_MEM | BPF_B:
	/* dst_reg = (s64)*(signed size *)(src_reg + off) */
	case BPF_LDX | BPF_MEMSX | BPF_B:
	case BPF_LDX | BPF_MEMSX | BPF_H:
	case BPF_LDX | BPF_MEMSX | BPF_W:
	case BPF_LDX | BPF_PROBE_MEMSX | BPF_B:
	case BPF_LDX | BPF_PROBE_MEMSX | BPF_H:
	case BPF_LDX | BPF_PROBE_MEMSX | BPF_W:
		sign_extend = BPF_MODE(insn->code) == BPF_MEMSX ||
			      BPF_MODE(insn->code) == BPF_PROBE_MEMSX;
		switch (BPF_SIZE(code)) {
		case BPF_B:
			if (is_signed_imm12(off)) {
				if (sign_extend)
					emit_insn(ctx, ldb, dst, src, off);
				else
					emit_insn(ctx, ldbu, dst, src, off);
			} else {
				move_imm(ctx, t1, off, is32);
				if (sign_extend)
					emit_insn(ctx, ldxb, dst, src, t1);
				else
					emit_insn(ctx, ldxbu, dst, src, t1);
			}
			break;
		case BPF_H:
			if (is_signed_imm12(off)) {
				if (sign_extend)
					emit_insn(ctx, ldh, dst, src, off);
				else
					emit_insn(ctx, ldhu, dst, src, off);
			} else {
				move_imm(ctx, t1, off, is32);
				if (sign_extend)
					emit_insn(ctx, ldxh, dst, src, t1);
				else
					emit_insn(ctx, ldxhu, dst, src, t1);
			}
			break;
		case BPF_W:
			if (is_signed_imm12(off)) {
				if (sign_extend)
					emit_insn(ctx, ldw, dst, src, off);
				else
					emit_insn(ctx, ldwu, dst, src, off);
			} else {
				move_imm(ctx, t1, off, is32);
				if (sign_extend)
					emit_insn(ctx, ldxw, dst, src, t1);
				else
					emit_insn(ctx, ldxwu, dst, src, t1);
			}
			break;
		case BPF_DW:
			move_imm(ctx, t1, off, is32);
			emit_insn(ctx, ldxd, dst, src, t1);
			break;
		}

		ret = add_exception_handler(insn, ctx, dst);
		if (ret)
			return ret;
		break;

	/* *(size *)(dst + off) = imm */
	case BPF_ST | BPF_MEM | BPF_B:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_DW:
		switch (BPF_SIZE(code)) {
		case BPF_B:
			move_imm(ctx, t1, imm, is32);
			if (is_signed_imm12(off)) {
				emit_insn(ctx, stb, t1, dst, off);
			} else {
				move_imm(ctx, t2, off, is32);
				emit_insn(ctx, stxb, t1, dst, t2);
			}
			break;
		case BPF_H:
			move_imm(ctx, t1, imm, is32);
			if (is_signed_imm12(off)) {
				emit_insn(ctx, sth, t1, dst, off);
			} else {
				move_imm(ctx, t2, off, is32);
				emit_insn(ctx, stxh, t1, dst, t2);
			}
			break;
		case BPF_W:
			move_imm(ctx, t1, imm, is32);
			if (is_signed_imm12(off)) {
				emit_insn(ctx, stw, t1, dst, off);
			} else if (is_signed_imm14(off)) {
				emit_insn(ctx, stptrw, t1, dst, off);
			} else {
				move_imm(ctx, t2, off, is32);
				emit_insn(ctx, stxw, t1, dst, t2);
			}
			break;
		case BPF_DW:
			move_imm(ctx, t1, imm, is32);
			if (is_signed_imm12(off)) {
				emit_insn(ctx, std, t1, dst, off);
			} else if (is_signed_imm14(off)) {
				emit_insn(ctx, stptrd, t1, dst, off);
			} else {
				move_imm(ctx, t2, off, is32);
				emit_insn(ctx, stxd, t1, dst, t2);
			}
			break;
		}
		break;

	/* *(size *)(dst + off) = src */
	case BPF_STX | BPF_MEM | BPF_B:
	case BPF_STX | BPF_MEM | BPF_H:
	case BPF_STX | BPF_MEM | BPF_W:
	case BPF_STX | BPF_MEM | BPF_DW:
		switch (BPF_SIZE(code)) {
		case BPF_B:
			if (is_signed_imm12(off)) {
				emit_insn(ctx, stb, src, dst, off);
			} else {
				move_imm(ctx, t1, off, is32);
				emit_insn(ctx, stxb, src, dst, t1);
			}
			break;
		case BPF_H:
			if (is_signed_imm12(off)) {
				emit_insn(ctx, sth, src, dst, off);
			} else {
				move_imm(ctx, t1, off, is32);
				emit_insn(ctx, stxh, src, dst, t1);
			}
			break;
		case BPF_W:
			if (is_signed_imm12(off)) {
				emit_insn(ctx, stw, src, dst, off);
			} else if (is_signed_imm14(off)) {
				emit_insn(ctx, stptrw, src, dst, off);
			} else {
				move_imm(ctx, t1, off, is32);
				emit_insn(ctx, stxw, src, dst, t1);
			}
			break;
		case BPF_DW:
			if (is_signed_imm12(off)) {
				emit_insn(ctx, std, src, dst, off);
			} else if (is_signed_imm14(off)) {
				emit_insn(ctx, stptrd, src, dst, off);
			} else {
				move_imm(ctx, t1, off, is32);
				emit_insn(ctx, stxd, src, dst, t1);
			}
			break;
		}
		break;

	case BPF_STX | BPF_ATOMIC | BPF_W:
	case BPF_STX | BPF_ATOMIC | BPF_DW:
		emit_atomic(insn, ctx);
		break;

	/* Speculation barrier */
	case BPF_ST | BPF_NOSPEC:
		break;

	default:
		pr_err("bpf_jit: unknown opcode %02x\n", code);
		return -EINVAL;
	}

	return 0;

toofar:
	pr_info_once("bpf_jit: opcode %02x, jump too far\n", code);
	return -E2BIG;
}

static int build_body(struct jit_ctx *ctx, bool extra_pass)
{
	int i;
	const struct bpf_prog *prog = ctx->prog;

	for (i = 0; i < prog->len; i++) {
		const struct bpf_insn *insn = &prog->insnsi[i];
		int ret;

		if (ctx->image == NULL)
			ctx->offset[i] = ctx->idx;

		ret = build_insn(insn, ctx, extra_pass);
		if (ret > 0) {
			i++;
			if (ctx->image == NULL)
				ctx->offset[i] = ctx->idx;
			continue;
		}
		if (ret)
			return ret;
	}

	if (ctx->image == NULL)
		ctx->offset[i] = ctx->idx;

	return 0;
}

/* Fill space with break instructions */
static void jit_fill_hole(void *area, unsigned int size)
{
	u32 *ptr;

	/* We are guaranteed to have aligned memory */
	for (ptr = area; size >= sizeof(u32); size -= sizeof(u32))
		*ptr++ = INSN_BREAK;
}

static int validate_code(struct jit_ctx *ctx)
{
	int i;
	union loongarch_instruction insn;

	for (i = 0; i < ctx->idx; i++) {
		insn = ctx->image[i];
		/* Check INSN_BREAK */
		if (insn.word == INSN_BREAK)
			return -1;
	}

	if (WARN_ON_ONCE(ctx->num_exentries != ctx->prog->aux->num_exentries))
		return -1;

	return 0;
}

struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
{
	bool tmp_blinded = false, extra_pass = false;
	u8 *image_ptr;
	int image_size, prog_size, extable_size;
	struct jit_ctx ctx;
	struct jit_data *jit_data;
	struct bpf_binary_header *header;
	struct bpf_prog *tmp, *orig_prog = prog;

	/*
	 * If BPF JIT was not enabled then we must fall back to
	 * the interpreter.
	 */
	if (!prog->jit_requested)
		return orig_prog;

	tmp = bpf_jit_blind_constants(prog);
	/*
	 * If blinding was requested and we failed during blinding,
	 * we must fall back to the interpreter. Otherwise, we save
	 * the new JITed code.
	 */
	if (IS_ERR(tmp))
		return orig_prog;

	if (tmp != prog) {
		tmp_blinded = true;
		prog = tmp;
	}

	jit_data = prog->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc(sizeof(*jit_data), GFP_KERNEL);
		if (!jit_data) {
			prog = orig_prog;
			goto out;
		}
		prog->aux->jit_data = jit_data;
	}
	if (jit_data->ctx.offset) {
		ctx = jit_data->ctx;
		image_ptr = jit_data->image;
		header = jit_data->header;
		extra_pass = true;
		prog_size = sizeof(u32) * ctx.idx;
		goto skip_init_ctx;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.prog = prog;

	ctx.offset = kvcalloc(prog->len + 1, sizeof(u32), GFP_KERNEL);
	if (ctx.offset == NULL) {
		prog = orig_prog;
		goto out_offset;
	}

	/* 1. Initial fake pass to compute ctx->idx and set ctx->flags */
	build_prologue(&ctx);
	if (build_body(&ctx, extra_pass)) {
		prog = orig_prog;
		goto out_offset;
	}
	ctx.epilogue_offset = ctx.idx;
	build_epilogue(&ctx);

	extable_size = prog->aux->num_exentries * sizeof(struct exception_table_entry);

	/* Now we know the actual image size.
	 * As each LoongArch instruction is of length 32bit,
	 * we are translating number of JITed intructions into
	 * the size required to store these JITed code.
	 */
	prog_size = sizeof(u32) * ctx.idx;
	image_size = prog_size + extable_size;
	/* Now we know the size of the structure to make */
	header = bpf_jit_binary_alloc(image_size, &image_ptr,
				      sizeof(u32), jit_fill_hole);
	if (header == NULL) {
		prog = orig_prog;
		goto out_offset;
	}

	/* 2. Now, the actual pass to generate final JIT code */
	ctx.image = (union loongarch_instruction *)image_ptr;
	if (extable_size)
		prog->aux->extable = (void *)image_ptr + prog_size;

skip_init_ctx:
	ctx.idx = 0;
	ctx.num_exentries = 0;

	build_prologue(&ctx);
	if (build_body(&ctx, extra_pass)) {
		bpf_jit_binary_free(header);
		prog = orig_prog;
		goto out_offset;
	}
	build_epilogue(&ctx);

	/* 3. Extra pass to validate JITed code */
	if (validate_code(&ctx)) {
		bpf_jit_binary_free(header);
		prog = orig_prog;
		goto out_offset;
	}

	/* And we're done */
	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, prog_size, 2, ctx.image);

	/* Update the icache */
	flush_icache_range((unsigned long)header, (unsigned long)(ctx.image + ctx.idx));

	if (!prog->is_func || extra_pass) {
		if (extra_pass && ctx.idx != jit_data->ctx.idx) {
			pr_err_once("multi-func JIT bug %d != %d\n",
				    ctx.idx, jit_data->ctx.idx);
			bpf_jit_binary_free(header);
			prog->bpf_func = NULL;
			prog->jited = 0;
			prog->jited_len = 0;
			goto out_offset;
		}
		bpf_jit_binary_lock_ro(header);
	} else {
		jit_data->ctx = ctx;
		jit_data->image = image_ptr;
		jit_data->header = header;
	}
	prog->jited = 1;
	prog->jited_len = prog_size;
	prog->bpf_func = (void *)ctx.image;

	if (!prog->is_func || extra_pass) {
		int i;

		/* offset[prog->len] is the size of program */
		for (i = 0; i <= prog->len; i++)
			ctx.offset[i] *= LOONGARCH_INSN_SIZE;
		bpf_prog_fill_jited_linfo(prog, ctx.offset + 1);

out_offset:
		kvfree(ctx.offset);
		kfree(jit_data);
		prog->aux->jit_data = NULL;
	}

out:
	if (tmp_blinded)
		bpf_jit_prog_release_other(prog, prog == orig_prog ? tmp : orig_prog);

	out_offset = -1;

	return prog;
}

/* Indicate the JIT backend supports mixing bpf2bpf and tailcalls. */
bool bpf_jit_supports_subprog_tailcalls(void)
{
	return true;
}
