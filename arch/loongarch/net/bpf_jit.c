// SPDX-License-Identifier: GPL-2.0-only
/*
 * BPF JIT compiler for LoongArch
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */
#include <linux/memory.h>
#include "bpf_jit.h"

#define LOONGARCH_MAX_REG_ARGS 8

#define LOONGARCH_LONG_JUMP_NINSNS 5
#define LOONGARCH_LONG_JUMP_NBYTES (LOONGARCH_LONG_JUMP_NINSNS * 4)

#define LOONGARCH_FENTRY_NINSNS 2
#define LOONGARCH_FENTRY_NBYTES (LOONGARCH_FENTRY_NINSNS * 4)
#define LOONGARCH_BPF_FENTRY_NBYTES (LOONGARCH_LONG_JUMP_NINSNS * 4)

#define REG_TCC		LOONGARCH_GPR_A6
#define BPF_TAIL_CALL_CNT_PTR_STACK_OFF(stack) (round_up(stack, 16) - 80)

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

static void prepare_bpf_tail_call_cnt(struct jit_ctx *ctx, int *store_offset)
{
	const struct bpf_prog *prog = ctx->prog;
	const bool is_main_prog = !bpf_is_subprog(prog);

	if (is_main_prog) {
		/*
		 * LOONGARCH_GPR_T3 = MAX_TAIL_CALL_CNT
		 * if (REG_TCC > T3 )
		 *	std REG_TCC -> LOONGARCH_GPR_SP + store_offset
		 * else
		 *	std REG_TCC -> LOONGARCH_GPR_SP + store_offset
		 *	REG_TCC = LOONGARCH_GPR_SP + store_offset
		 *
		 * std REG_TCC -> LOONGARCH_GPR_SP + store_offset
		 *
		 * The purpose of this code is to first push the TCC into stack,
		 * and then push the address of TCC into stack.
		 * In cases where bpf2bpf and tailcall are used in combination,
		 * the value in REG_TCC may be a count or an address,
		 * these two cases need to be judged and handled separately.
		 */
		emit_insn(ctx, addid, LOONGARCH_GPR_T3, LOONGARCH_GPR_ZERO, MAX_TAIL_CALL_CNT);
		*store_offset -= sizeof(long);

		emit_cond_jmp(ctx, BPF_JGT, REG_TCC, LOONGARCH_GPR_T3, 4);

		/*
		 * If REG_TCC < MAX_TAIL_CALL_CNT, the value in REG_TCC is a count,
		 * push tcc into stack
		 */
		emit_insn(ctx, std, REG_TCC, LOONGARCH_GPR_SP, *store_offset);

		/* Push the address of TCC into the REG_TCC */
		emit_insn(ctx, addid, REG_TCC, LOONGARCH_GPR_SP, *store_offset);

		emit_uncond_jmp(ctx, 2);

		/*
		 * If REG_TCC > MAX_TAIL_CALL_CNT, the value in REG_TCC is an address,
		 * push tcc_ptr into stack
		 */
		emit_insn(ctx, std, REG_TCC, LOONGARCH_GPR_SP, *store_offset);
	} else {
		*store_offset -= sizeof(long);
		emit_insn(ctx, std, REG_TCC, LOONGARCH_GPR_SP, *store_offset);
	}

	/* Push tcc_ptr into stack */
	*store_offset -= sizeof(long);
	emit_insn(ctx, std, REG_TCC, LOONGARCH_GPR_SP, *store_offset);
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
 *                            +-------------------------+
 *                            |           tcc           |
 *                            +-------------------------+
 *                            |           tcc_ptr       |
 *                            +-------------------------+ <--BPF_REG_FP
 *                            |  prog->aux->stack_depth |
 *                            |        (optional)       |
 * current $sp -------------> +-------------------------+
 *                                        low
 */
static void build_prologue(struct jit_ctx *ctx)
{
	int i, stack_adjust = 0, store_offset, bpf_stack_adjust;
	const struct bpf_prog *prog = ctx->prog;
	const bool is_main_prog = !bpf_is_subprog(prog);

	bpf_stack_adjust = round_up(ctx->prog->aux->stack_depth, 16);

	/* To store ra, fp, s0, s1, s2, s3, s4, s5 */
	stack_adjust += sizeof(long) * 8;

	/* To store tcc and tcc_ptr */
	stack_adjust += sizeof(long) * 2;

	stack_adjust = round_up(stack_adjust, 16);
	stack_adjust += bpf_stack_adjust;

	/* Reserve space for the move_imm + jirl instruction */
	for (i = 0; i < LOONGARCH_LONG_JUMP_NINSNS; i++)
		emit_insn(ctx, nop);

	/*
	 * First instruction initializes the tail call count (TCC)
	 * register to zero. On tail call we skip this instruction,
	 * and the TCC is passed in REG_TCC from the caller.
	 */
	if (is_main_prog)
		emit_insn(ctx, addid, REG_TCC, LOONGARCH_GPR_ZERO, 0);

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

	prepare_bpf_tail_call_cnt(ctx, &store_offset);

	emit_insn(ctx, addid, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, stack_adjust);

	if (bpf_stack_adjust)
		emit_insn(ctx, addid, regmap[BPF_REG_FP], LOONGARCH_GPR_SP, bpf_stack_adjust);

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

	/*
	 * When push into the stack, follow the order of tcc then tcc_ptr.
	 * When pop from the stack, first pop tcc_ptr then followed by tcc.
	 */
	load_offset -= 2 * sizeof(long);
	emit_insn(ctx, ldd, REG_TCC, LOONGARCH_GPR_SP, load_offset);

	load_offset += sizeof(long);
	emit_insn(ctx, ldd, REG_TCC, LOONGARCH_GPR_SP, load_offset);

	emit_insn(ctx, addid, LOONGARCH_GPR_SP, LOONGARCH_GPR_SP, stack_adjust);

	if (!is_tail_call) {
		/* Set return value */
		emit_insn(ctx, addiw, LOONGARCH_GPR_A0, regmap[BPF_REG_0], 0);
		/* Return to the caller */
		emit_insn(ctx, jirl, LOONGARCH_GPR_ZERO, LOONGARCH_GPR_RA, 0);
	} else {
		/*
		 * Call the next bpf prog and skip the first instruction
		 * of TCC initialization.
		 */
		emit_insn(ctx, jirl, LOONGARCH_GPR_ZERO, LOONGARCH_GPR_T3, 6);
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

static int emit_bpf_tail_call(struct jit_ctx *ctx, int insn)
{
	int off, tc_ninsn = 0;
	int tcc_ptr_off = BPF_TAIL_CALL_CNT_PTR_STACK_OFF(ctx->stack_size);
	u8 a1 = LOONGARCH_GPR_A1;
	u8 a2 = LOONGARCH_GPR_A2;
	u8 t1 = LOONGARCH_GPR_T1;
	u8 t2 = LOONGARCH_GPR_T2;
	u8 t3 = LOONGARCH_GPR_T3;
	const int idx0 = ctx->idx;

#define cur_offset (ctx->idx - idx0)
#define jmp_offset (tc_ninsn - (cur_offset))

	/*
	 * a0: &ctx
	 * a1: &array
	 * a2: index
	 *
	 * if (index >= array->map.max_entries)
	 *	 goto out;
	 */
	tc_ninsn = insn ? ctx->offset[insn+1] - ctx->offset[insn] : ctx->offset[0];
	off = offsetof(struct bpf_array, map.max_entries);
	emit_insn(ctx, ldwu, t1, a1, off);
	/* bgeu $a2, $t1, jmp_offset */
	if (emit_tailcall_jmp(ctx, BPF_JGE, a2, t1, jmp_offset) < 0)
		goto toofar;

	/*
	 * if ((*tcc_ptr)++ >= MAX_TAIL_CALL_CNT)
	 *      goto out;
	 */
	emit_insn(ctx, ldd, REG_TCC, LOONGARCH_GPR_SP, tcc_ptr_off);
	emit_insn(ctx, ldd, t3, REG_TCC, 0);
	emit_insn(ctx, addid, t3, t3, 1);
	emit_insn(ctx, std, t3, REG_TCC, 0);
	emit_insn(ctx, addid, t2, LOONGARCH_GPR_ZERO, MAX_TAIL_CALL_CNT);
	if (emit_tailcall_jmp(ctx, BPF_JSGT, t3, t2, jmp_offset) < 0)
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
	int ret, jmp_offset, tcc_ptr_off;
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
			emit_insn(ctx, extwb, dst, src);
			emit_zext_32(ctx, dst, is32);
			break;
		case 16:
			emit_insn(ctx, extwh, dst, src);
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
		ret = bpf_jit_get_func_addr(ctx->prog, insn, extra_pass,
					    &func_addr, &func_addr_fixed);
		if (ret < 0)
			return ret;

		if (insn->src_reg == BPF_PSEUDO_CALL) {
			tcc_ptr_off = BPF_TAIL_CALL_CNT_PTR_STACK_OFF(ctx->stack_size);
			emit_insn(ctx, ldd, REG_TCC, LOONGARCH_GPR_SP, tcc_ptr_off);
		}

		move_addr(ctx, t1, func_addr);
		emit_insn(ctx, jirl, LOONGARCH_GPR_RA, t1, 0);

		if (insn->src_reg != BPF_PSEUDO_CALL)
			move_reg(ctx, regmap[BPF_REG_0], LOONGARCH_GPR_A0);

		break;

	/* tail call */
	case BPF_JMP | BPF_TAIL_CALL:
		if (emit_bpf_tail_call(ctx, i) < 0)
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

		if (bpf_pseudo_func(insn))
			move_addr(ctx, dst, imm64);
		else
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

	return 0;
}

static int validate_ctx(struct jit_ctx *ctx)
{
	if (validate_code(ctx))
		return -1;

	if (WARN_ON_ONCE(ctx->num_exentries != ctx->prog->aux->num_exentries))
		return -1;

	return 0;
}

static int emit_jump_and_link(struct jit_ctx *ctx, u8 rd, u64 target)
{
	if (!target) {
		pr_err("bpf_jit: jump target address is error\n");
		return -EFAULT;
	}

	move_imm(ctx, LOONGARCH_GPR_T1, target, false);
	emit_insn(ctx, jirl, rd, LOONGARCH_GPR_T1, 0);

	return 0;
}

static int emit_jump_or_nops(void *target, void *ip, u32 *insns, bool is_call)
{
	int i;
	struct jit_ctx ctx;

	ctx.idx = 0;
	ctx.image = (union loongarch_instruction *)insns;

	if (!target) {
		for (i = 0; i < LOONGARCH_LONG_JUMP_NINSNS; i++)
			emit_insn((&ctx), nop);
		return 0;
	}

	return emit_jump_and_link(&ctx, is_call ? LOONGARCH_GPR_T0 : LOONGARCH_GPR_ZERO, (u64)target);
}

static int emit_call(struct jit_ctx *ctx, u64 addr)
{
	return emit_jump_and_link(ctx, LOONGARCH_GPR_RA, addr);
}

void *bpf_arch_text_copy(void *dst, void *src, size_t len)
{
	int ret;

	mutex_lock(&text_mutex);
	ret = larch_insn_text_copy(dst, src, len);
	mutex_unlock(&text_mutex);

	return ret ? ERR_PTR(-EINVAL) : dst;
}

int bpf_arch_text_poke(void *ip, enum bpf_text_poke_type poke_type,
		       void *old_addr, void *new_addr)
{
	int ret;
	bool is_call = (poke_type == BPF_MOD_CALL);
	u32 old_insns[LOONGARCH_LONG_JUMP_NINSNS] = {[0 ... 4] = INSN_NOP};
	u32 new_insns[LOONGARCH_LONG_JUMP_NINSNS] = {[0 ... 4] = INSN_NOP};

	/* Only poking bpf text is supported. Since kernel function entry
	 * is set up by ftrace, we rely on ftrace to poke kernel functions.
	 */
	if (!is_bpf_text_address((unsigned long)ip))
		return -ENOTSUPP;

	ret = emit_jump_or_nops(old_addr, ip, old_insns, is_call);
	if (ret)
		return ret;

	if (memcmp(ip, old_insns, LOONGARCH_LONG_JUMP_NBYTES))
		return -EFAULT;

	ret = emit_jump_or_nops(new_addr, ip, new_insns, is_call);
	if (ret)
		return ret;

	mutex_lock(&text_mutex);
	if (memcmp(ip, new_insns, LOONGARCH_LONG_JUMP_NBYTES))
		ret = larch_insn_text_copy(ip, new_insns, LOONGARCH_LONG_JUMP_NBYTES);
	mutex_unlock(&text_mutex);

	return ret;
}

int bpf_arch_text_invalidate(void *dst, size_t len)
{
	int i;
	int ret = 0;
	u32 *inst;

	inst = kvmalloc(len, GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	for (i = 0; i < (len / sizeof(u32)); i++)
		inst[i] = INSN_BREAK;

	mutex_lock(&text_mutex);
	if (larch_insn_text_copy(dst, inst, len))
		ret = -EINVAL;
	mutex_unlock(&text_mutex);

	kvfree(inst);

	return ret;
}

static void store_args(struct jit_ctx *ctx, int nargs, int args_off)
{
	int i;

	for (i = 0; i < nargs; i++) {
		emit_insn(ctx, std, LOONGARCH_GPR_A0 + i, LOONGARCH_GPR_FP, -args_off);
		args_off -= 8;
	}
}

static void restore_args(struct jit_ctx *ctx, int nargs, int args_off)
{
	int i;

	for (i = 0; i < nargs; i++) {
		emit_insn(ctx, ldd, LOONGARCH_GPR_A0 + i, LOONGARCH_GPR_FP, -args_off);
		args_off -= 8;
	}
}

static int invoke_bpf_prog(struct jit_ctx *ctx, struct bpf_tramp_link *l,
			   int args_off, int retval_off, int run_ctx_off, bool save_ret)
{
	int ret;
	u32 *branch;
	struct bpf_prog *p = l->link.prog;
	int cookie_off = offsetof(struct bpf_tramp_run_ctx, bpf_cookie);

	if (l->cookie) {
		move_imm(ctx, LOONGARCH_GPR_T1, l->cookie, false);
		emit_insn(ctx, std, LOONGARCH_GPR_T1, LOONGARCH_GPR_FP, -run_ctx_off + cookie_off);
	} else {
		emit_insn(ctx, std, LOONGARCH_GPR_ZERO, LOONGARCH_GPR_FP, -run_ctx_off + cookie_off);
	}

	/* arg1: prog */
	move_imm(ctx, LOONGARCH_GPR_A0, (const s64)p, false);
	/* arg2: &run_ctx */
	emit_insn(ctx, addid, LOONGARCH_GPR_A1, LOONGARCH_GPR_FP, -run_ctx_off);
	ret = emit_call(ctx, (const u64)bpf_trampoline_enter(p));
	if (ret)
		return ret;

	/* store prog start time */
	move_reg(ctx, LOONGARCH_GPR_S1, LOONGARCH_GPR_A0);

	/*
	 * if (__bpf_prog_enter(prog) == 0)
	 *      goto skip_exec_of_prog;
	 */
	branch = (u32 *)ctx->image + ctx->idx;
	/* nop reserved for conditional jump */
	emit_insn(ctx, nop);

	/* arg1: &args_off */
	emit_insn(ctx, addid, LOONGARCH_GPR_A0, LOONGARCH_GPR_FP, -args_off);
	if (!p->jited)
		move_imm(ctx, LOONGARCH_GPR_A1, (const s64)p->insnsi, false);
	ret = emit_call(ctx, (const u64)p->bpf_func);
	if (ret)
		return ret;

	if (save_ret) {
		emit_insn(ctx, std, LOONGARCH_GPR_A0, LOONGARCH_GPR_FP, -retval_off);
		emit_insn(ctx, std, regmap[BPF_REG_0], LOONGARCH_GPR_FP, -(retval_off - 8));
	}

	/* update branch with beqz */
	if (ctx->image) {
		int offset = (void *)(&ctx->image[ctx->idx]) - (void *)branch;
		*branch = larch_insn_gen_beq(LOONGARCH_GPR_A0, LOONGARCH_GPR_ZERO, offset);
	}

	/* arg1: prog */
	move_imm(ctx, LOONGARCH_GPR_A0, (const s64)p, false);
	/* arg2: prog start time */
	move_reg(ctx, LOONGARCH_GPR_A1, LOONGARCH_GPR_S1);
	/* arg3: &run_ctx */
	emit_insn(ctx, addid, LOONGARCH_GPR_A2, LOONGARCH_GPR_FP, -run_ctx_off);
	ret = emit_call(ctx, (const u64)bpf_trampoline_exit(p));

	return ret;
}

static void invoke_bpf_mod_ret(struct jit_ctx *ctx, struct bpf_tramp_links *tl,
			       int args_off, int retval_off, int run_ctx_off, u32 **branches)
{
	int i;

	emit_insn(ctx, std, LOONGARCH_GPR_ZERO, LOONGARCH_GPR_FP, -retval_off);
	for (i = 0; i < tl->nr_links; i++) {
		invoke_bpf_prog(ctx, tl->links[i], args_off, retval_off, run_ctx_off, true);
		emit_insn(ctx, ldd, LOONGARCH_GPR_T1, LOONGARCH_GPR_FP, -retval_off);
		branches[i] = (u32 *)ctx->image + ctx->idx;
		emit_insn(ctx, nop);
	}
}

void *arch_alloc_bpf_trampoline(unsigned int size)
{
	return bpf_prog_pack_alloc(size, jit_fill_hole);
}

void arch_free_bpf_trampoline(void *image, unsigned int size)
{
	bpf_prog_pack_free(image, size);
}

/*
 * Sign-extend the register if necessary
 */
static void sign_extend(struct jit_ctx *ctx, int rd, int rj, u8 size, bool sign)
{
	/* ABI requires unsigned char/short to be zero-extended */
	if (!sign && (size == 1 || size == 2)) {
		if (rd != rj)
			move_reg(ctx, rd, rj);
		return;
	}

	switch (size) {
	case 1:
		emit_insn(ctx, extwb, rd, rj);
		break;
	case 2:
		emit_insn(ctx, extwh, rd, rj);
		break;
	case 4:
		emit_insn(ctx, addiw, rd, rj, 0);
		break;
	case 8:
		if (rd != rj)
			move_reg(ctx, rd, rj);
		break;
	default:
		pr_warn("bpf_jit: invalid size %d for sign_extend\n", size);
	}
}

static int __arch_prepare_bpf_trampoline(struct jit_ctx *ctx, struct bpf_tramp_image *im,
					 const struct btf_func_model *m, struct bpf_tramp_links *tlinks,
					 void *func_addr, u32 flags)
{
	int i, ret, save_ret;
	int stack_size, nargs;
	int retval_off, args_off, nargs_off, ip_off, run_ctx_off, sreg_off, tcc_ptr_off;
	bool is_struct_ops = flags & BPF_TRAMP_F_INDIRECT;
	void *orig_call = func_addr;
	struct bpf_tramp_links *fentry = &tlinks[BPF_TRAMP_FENTRY];
	struct bpf_tramp_links *fexit = &tlinks[BPF_TRAMP_FEXIT];
	struct bpf_tramp_links *fmod_ret = &tlinks[BPF_TRAMP_MODIFY_RETURN];
	u32 **branches = NULL;

	/*
	 * FP + 8       [ RA to parent func ] return address to parent
	 *                    function
	 * FP + 0       [ FP of parent func ] frame pointer of parent
	 *                    function
	 * FP - 8       [ T0 to traced func ] return address of traced
	 *                    function
	 * FP - 16      [ FP of traced func ] frame pointer of traced
	 *                    function
	 *
	 * FP - retval_off  [ return value      ] BPF_TRAMP_F_CALL_ORIG or
	 *                    BPF_TRAMP_F_RET_FENTRY_RET
	 *                  [ argN              ]
	 *                  [ ...               ]
	 * FP - args_off    [ arg1              ]
	 *
	 * FP - nargs_off   [ regs count        ]
	 *
	 * FP - ip_off      [ traced func   ] BPF_TRAMP_F_IP_ARG
	 *
	 * FP - run_ctx_off [ bpf_tramp_run_ctx ]
	 *
	 * FP - sreg_off    [ callee saved reg  ]
	 *
	 * FP - tcc_ptr_off [ tail_call_cnt_ptr ]
	 */

	if (m->nr_args > LOONGARCH_MAX_REG_ARGS)
		return -ENOTSUPP;

	/* FIXME: No support of struct argument */
	for (i = 0; i < m->nr_args; i++) {
		if (m->arg_flags[i] & BTF_FMODEL_STRUCT_ARG)
			return -ENOTSUPP;
	}

	if (flags & (BPF_TRAMP_F_ORIG_STACK | BPF_TRAMP_F_SHARE_IPMODIFY))
		return -ENOTSUPP;

	/* Room of trampoline frame to store return address and frame pointer */
	stack_size = 16;

	save_ret = flags & (BPF_TRAMP_F_CALL_ORIG | BPF_TRAMP_F_RET_FENTRY_RET);
	if (save_ret)
		stack_size += 16; /* Save BPF R0 and A0 */

	retval_off = stack_size;

	/* Room of trampoline frame to store args */
	nargs = m->nr_args;
	stack_size += nargs * 8;
	args_off = stack_size;

	/* Room of trampoline frame to store args number */
	stack_size += 8;
	nargs_off = stack_size;

	/* Room of trampoline frame to store ip address */
	if (flags & BPF_TRAMP_F_IP_ARG) {
		stack_size += 8;
		ip_off = stack_size;
	}

	/* Room of trampoline frame to store struct bpf_tramp_run_ctx */
	stack_size += round_up(sizeof(struct bpf_tramp_run_ctx), 8);
	run_ctx_off = stack_size;

	stack_size += 8;
	sreg_off = stack_size;

	/* Room of trampoline frame to store tail_call_cnt_ptr */
	if (flags & BPF_TRAMP_F_TAIL_CALL_CTX) {
		stack_size += 8;
		tcc_ptr_off = stack_size;
	}

	stack_size = round_up(stack_size, 16);

	if (is_struct_ops) {
		/*
		 * For the trampoline called directly, just handle
		 * the frame of trampoline.
		 */
		emit_insn(ctx, addid, LOONGARCH_GPR_SP, LOONGARCH_GPR_SP, -stack_size);
		emit_insn(ctx, std, LOONGARCH_GPR_RA, LOONGARCH_GPR_SP, stack_size - 8);
		emit_insn(ctx, std, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, stack_size - 16);
		emit_insn(ctx, addid, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, stack_size);
	} else {
		/*
		 * For the trampoline called from function entry,
		 * the frame of traced function and the frame of
		 * trampoline need to be considered.
		 */
		/* RA and FP for parent function */
		emit_insn(ctx, addid, LOONGARCH_GPR_SP, LOONGARCH_GPR_SP, -16);
		emit_insn(ctx, std, LOONGARCH_GPR_RA, LOONGARCH_GPR_SP, 8);
		emit_insn(ctx, std, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, 0);
		emit_insn(ctx, addid, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, 16);

		/* RA and FP for traced function */
		emit_insn(ctx, addid, LOONGARCH_GPR_SP, LOONGARCH_GPR_SP, -stack_size);
		emit_insn(ctx, std, LOONGARCH_GPR_T0, LOONGARCH_GPR_SP, stack_size - 8);
		emit_insn(ctx, std, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, stack_size - 16);
		emit_insn(ctx, addid, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, stack_size);
	}

	if (flags & BPF_TRAMP_F_TAIL_CALL_CTX)
		emit_insn(ctx, std, REG_TCC, LOONGARCH_GPR_FP, -tcc_ptr_off);

	/* callee saved register S1 to pass start time */
	emit_insn(ctx, std, LOONGARCH_GPR_S1, LOONGARCH_GPR_FP, -sreg_off);

	/* store ip address of the traced function */
	if (flags & BPF_TRAMP_F_IP_ARG) {
		move_imm(ctx, LOONGARCH_GPR_T1, (const s64)func_addr, false);
		emit_insn(ctx, std, LOONGARCH_GPR_T1, LOONGARCH_GPR_FP, -ip_off);
	}

	/* store nargs number */
	move_imm(ctx, LOONGARCH_GPR_T1, nargs, false);
	emit_insn(ctx, std, LOONGARCH_GPR_T1, LOONGARCH_GPR_FP, -nargs_off);

	store_args(ctx, nargs, args_off);

	/* To traced function */
	/* Ftrace jump skips 2 NOP instructions */
	if (is_kernel_text((unsigned long)orig_call))
		orig_call += LOONGARCH_FENTRY_NBYTES;
	/* Direct jump skips 5 NOP instructions */
	else if (is_bpf_text_address((unsigned long)orig_call))
		orig_call += LOONGARCH_BPF_FENTRY_NBYTES;
	/* Module tracing not supported - cause kernel lockups */
	else if (is_module_text_address((unsigned long)orig_call))
		return -ENOTSUPP;

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		move_addr(ctx, LOONGARCH_GPR_A0, (const u64)im);
		ret = emit_call(ctx, (const u64)__bpf_tramp_enter);
		if (ret)
			return ret;
	}

	for (i = 0; i < fentry->nr_links; i++) {
		ret = invoke_bpf_prog(ctx, fentry->links[i], args_off, retval_off,
				      run_ctx_off, flags & BPF_TRAMP_F_RET_FENTRY_RET);
		if (ret)
			return ret;
	}
	if (fmod_ret->nr_links) {
		branches  = kcalloc(fmod_ret->nr_links, sizeof(u32 *), GFP_KERNEL);
		if (!branches)
			return -ENOMEM;

		invoke_bpf_mod_ret(ctx, fmod_ret, args_off, retval_off, run_ctx_off, branches);
	}

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		restore_args(ctx, m->nr_args, args_off);

		if (flags & BPF_TRAMP_F_TAIL_CALL_CTX)
			emit_insn(ctx, ldd, REG_TCC, LOONGARCH_GPR_FP, -tcc_ptr_off);

		ret = emit_call(ctx, (const u64)orig_call);
		if (ret)
			goto out;
		emit_insn(ctx, std, LOONGARCH_GPR_A0, LOONGARCH_GPR_FP, -retval_off);
		emit_insn(ctx, std, regmap[BPF_REG_0], LOONGARCH_GPR_FP, -(retval_off - 8));
		im->ip_after_call = ctx->ro_image + ctx->idx;
		/* Reserve space for the move_imm + jirl instruction */
		for (i = 0; i < LOONGARCH_LONG_JUMP_NINSNS; i++)
			emit_insn(ctx, nop);
	}

	for (i = 0; ctx->image && i < fmod_ret->nr_links; i++) {
		int offset = (void *)(&ctx->image[ctx->idx]) - (void *)branches[i];
		*branches[i] = larch_insn_gen_bne(LOONGARCH_GPR_T1, LOONGARCH_GPR_ZERO, offset);
	}

	for (i = 0; i < fexit->nr_links; i++) {
		ret = invoke_bpf_prog(ctx, fexit->links[i], args_off, retval_off, run_ctx_off, false);
		if (ret)
			goto out;
	}

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		im->ip_epilogue = ctx->ro_image + ctx->idx;
		move_addr(ctx, LOONGARCH_GPR_A0, (const u64)im);
		ret = emit_call(ctx, (const u64)__bpf_tramp_exit);
		if (ret)
			goto out;
	}

	if (flags & BPF_TRAMP_F_RESTORE_REGS)
		restore_args(ctx, m->nr_args, args_off);

	if (save_ret) {
		emit_insn(ctx, ldd, regmap[BPF_REG_0], LOONGARCH_GPR_FP, -(retval_off - 8));
		if (is_struct_ops)
			sign_extend(ctx, LOONGARCH_GPR_A0, regmap[BPF_REG_0],
				    m->ret_size, m->ret_flags & BTF_FMODEL_SIGNED_ARG);
		else
			emit_insn(ctx, ldd, LOONGARCH_GPR_A0, LOONGARCH_GPR_FP, -retval_off);
	}

	emit_insn(ctx, ldd, LOONGARCH_GPR_S1, LOONGARCH_GPR_FP, -sreg_off);

	if (flags & BPF_TRAMP_F_TAIL_CALL_CTX)
		emit_insn(ctx, ldd, REG_TCC, LOONGARCH_GPR_FP, -tcc_ptr_off);

	if (is_struct_ops) {
		/* trampoline called directly */
		emit_insn(ctx, ldd, LOONGARCH_GPR_RA, LOONGARCH_GPR_SP, stack_size - 8);
		emit_insn(ctx, ldd, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, stack_size - 16);
		emit_insn(ctx, addid, LOONGARCH_GPR_SP, LOONGARCH_GPR_SP, stack_size);

		emit_insn(ctx, jirl, LOONGARCH_GPR_ZERO, LOONGARCH_GPR_RA, 0);
	} else {
		/* trampoline called from function entry */
		emit_insn(ctx, ldd, LOONGARCH_GPR_T0, LOONGARCH_GPR_SP, stack_size - 8);
		emit_insn(ctx, ldd, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, stack_size - 16);
		emit_insn(ctx, addid, LOONGARCH_GPR_SP, LOONGARCH_GPR_SP, stack_size);

		emit_insn(ctx, ldd, LOONGARCH_GPR_RA, LOONGARCH_GPR_SP, 8);
		emit_insn(ctx, ldd, LOONGARCH_GPR_FP, LOONGARCH_GPR_SP, 0);
		emit_insn(ctx, addid, LOONGARCH_GPR_SP, LOONGARCH_GPR_SP, 16);

		if (flags & BPF_TRAMP_F_SKIP_FRAME)
			/* return to parent function */
			emit_insn(ctx, jirl, LOONGARCH_GPR_ZERO, LOONGARCH_GPR_RA, 0);
		else
			/* return to traced function */
			emit_insn(ctx, jirl, LOONGARCH_GPR_ZERO, LOONGARCH_GPR_T0, 0);
	}

	ret = ctx->idx;
out:
	kfree(branches);

	return ret;
}

int arch_prepare_bpf_trampoline(struct bpf_tramp_image *im, void *ro_image,
				void *ro_image_end, const struct btf_func_model *m,
				u32 flags, struct bpf_tramp_links *tlinks, void *func_addr)
{
	int ret, size;
	void *image, *tmp;
	struct jit_ctx ctx;

	size = ro_image_end - ro_image;
	image = kvmalloc(size, GFP_KERNEL);
	if (!image)
		return -ENOMEM;

	ctx.image = (union loongarch_instruction *)image;
	ctx.ro_image = (union loongarch_instruction *)ro_image;
	ctx.idx = 0;

	jit_fill_hole(image, (unsigned int)(ro_image_end - ro_image));
	ret = __arch_prepare_bpf_trampoline(&ctx, im, m, tlinks, func_addr, flags);
	if (ret < 0)
		goto out;

	if (validate_code(&ctx) < 0) {
		ret = -EINVAL;
		goto out;
	}

	tmp = bpf_arch_text_copy(ro_image, image, size);
	if (IS_ERR(tmp)) {
		ret = PTR_ERR(tmp);
		goto out;
	}

out:
	kvfree(image);
	return ret < 0 ? ret : size;
}

int arch_bpf_trampoline_size(const struct btf_func_model *m, u32 flags,
			     struct bpf_tramp_links *tlinks, void *func_addr)
{
	int ret;
	struct jit_ctx ctx;
	struct bpf_tramp_image im;

	ctx.image = NULL;
	ctx.idx = 0;

	ret = __arch_prepare_bpf_trampoline(&ctx, &im, m, tlinks, func_addr, flags);

	return ret < 0 ? ret : ret * LOONGARCH_INSN_SIZE;
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
	if (validate_ctx(&ctx)) {
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
		int err;

		if (extra_pass && ctx.idx != jit_data->ctx.idx) {
			pr_err_once("multi-func JIT bug %d != %d\n",
				    ctx.idx, jit_data->ctx.idx);
			goto out_free;
		}
		err = bpf_jit_binary_lock_ro(header);
		if (err) {
			pr_err_once("bpf_jit_binary_lock_ro() returned %d\n",
				    err);
			goto out_free;
		}
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


	return prog;

out_free:
	bpf_jit_binary_free(header);
	prog->bpf_func = NULL;
	prog->jited = 0;
	prog->jited_len = 0;
	goto out_offset;
}

bool bpf_jit_bypass_spec_v1(void)
{
	return true;
}

bool bpf_jit_bypass_spec_v4(void)
{
	return true;
}

/* Indicate the JIT backend supports mixing bpf2bpf and tailcalls. */
bool bpf_jit_supports_subprog_tailcalls(void)
{
	return true;
}
