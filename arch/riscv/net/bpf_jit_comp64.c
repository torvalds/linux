// SPDX-License-Identifier: GPL-2.0
/* BPF JIT compiler for RV64G
 *
 * Copyright(c) 2019 Björn Töpel <bjorn.topel@gmail.com>
 *
 */

#include <linux/bpf.h>
#include <linux/filter.h>
#include "bpf_jit.h"

#define RV_REG_TCC RV_REG_A6
#define RV_REG_TCC_SAVED RV_REG_S6 /* Store A6 in S6 if program do calls */

static const int regmap[] = {
	[BPF_REG_0] =	RV_REG_A5,
	[BPF_REG_1] =	RV_REG_A0,
	[BPF_REG_2] =	RV_REG_A1,
	[BPF_REG_3] =	RV_REG_A2,
	[BPF_REG_4] =	RV_REG_A3,
	[BPF_REG_5] =	RV_REG_A4,
	[BPF_REG_6] =	RV_REG_S1,
	[BPF_REG_7] =	RV_REG_S2,
	[BPF_REG_8] =	RV_REG_S3,
	[BPF_REG_9] =	RV_REG_S4,
	[BPF_REG_FP] =	RV_REG_S5,
	[BPF_REG_AX] =	RV_REG_T0,
};

enum {
	RV_CTX_F_SEEN_TAIL_CALL =	0,
	RV_CTX_F_SEEN_CALL =		RV_REG_RA,
	RV_CTX_F_SEEN_S1 =		RV_REG_S1,
	RV_CTX_F_SEEN_S2 =		RV_REG_S2,
	RV_CTX_F_SEEN_S3 =		RV_REG_S3,
	RV_CTX_F_SEEN_S4 =		RV_REG_S4,
	RV_CTX_F_SEEN_S5 =		RV_REG_S5,
	RV_CTX_F_SEEN_S6 =		RV_REG_S6,
};

static u8 bpf_to_rv_reg(int bpf_reg, struct rv_jit_context *ctx)
{
	u8 reg = regmap[bpf_reg];

	switch (reg) {
	case RV_CTX_F_SEEN_S1:
	case RV_CTX_F_SEEN_S2:
	case RV_CTX_F_SEEN_S3:
	case RV_CTX_F_SEEN_S4:
	case RV_CTX_F_SEEN_S5:
	case RV_CTX_F_SEEN_S6:
		__set_bit(reg, &ctx->flags);
	}
	return reg;
};

static bool seen_reg(int reg, struct rv_jit_context *ctx)
{
	switch (reg) {
	case RV_CTX_F_SEEN_CALL:
	case RV_CTX_F_SEEN_S1:
	case RV_CTX_F_SEEN_S2:
	case RV_CTX_F_SEEN_S3:
	case RV_CTX_F_SEEN_S4:
	case RV_CTX_F_SEEN_S5:
	case RV_CTX_F_SEEN_S6:
		return test_bit(reg, &ctx->flags);
	}
	return false;
}

static void mark_fp(struct rv_jit_context *ctx)
{
	__set_bit(RV_CTX_F_SEEN_S5, &ctx->flags);
}

static void mark_call(struct rv_jit_context *ctx)
{
	__set_bit(RV_CTX_F_SEEN_CALL, &ctx->flags);
}

static bool seen_call(struct rv_jit_context *ctx)
{
	return test_bit(RV_CTX_F_SEEN_CALL, &ctx->flags);
}

static void mark_tail_call(struct rv_jit_context *ctx)
{
	__set_bit(RV_CTX_F_SEEN_TAIL_CALL, &ctx->flags);
}

static bool seen_tail_call(struct rv_jit_context *ctx)
{
	return test_bit(RV_CTX_F_SEEN_TAIL_CALL, &ctx->flags);
}

static u8 rv_tail_call_reg(struct rv_jit_context *ctx)
{
	mark_tail_call(ctx);

	if (seen_call(ctx)) {
		__set_bit(RV_CTX_F_SEEN_S6, &ctx->flags);
		return RV_REG_S6;
	}
	return RV_REG_A6;
}

static bool is_32b_int(s64 val)
{
	return -(1L << 31) <= val && val < (1L << 31);
}

static bool in_auipc_jalr_range(s64 val)
{
	/*
	 * auipc+jalr can reach any signed PC-relative offset in the range
	 * [-2^31 - 2^11, 2^31 - 2^11).
	 */
	return (-(1L << 31) - (1L << 11)) <= val &&
		val < ((1L << 31) - (1L << 11));
}

static void emit_imm(u8 rd, s64 val, struct rv_jit_context *ctx)
{
	/* Note that the immediate from the add is sign-extended,
	 * which means that we need to compensate this by adding 2^12,
	 * when the 12th bit is set. A simpler way of doing this, and
	 * getting rid of the check, is to just add 2**11 before the
	 * shift. The "Loading a 32-Bit constant" example from the
	 * "Computer Organization and Design, RISC-V edition" book by
	 * Patterson/Hennessy highlights this fact.
	 *
	 * This also means that we need to process LSB to MSB.
	 */
	s64 upper = (val + (1 << 11)) >> 12;
	/* Sign-extend lower 12 bits to 64 bits since immediates for li, addiw,
	 * and addi are signed and RVC checks will perform signed comparisons.
	 */
	s64 lower = ((val & 0xfff) << 52) >> 52;
	int shift;

	if (is_32b_int(val)) {
		if (upper)
			emit_lui(rd, upper, ctx);

		if (!upper) {
			emit_li(rd, lower, ctx);
			return;
		}

		emit_addiw(rd, rd, lower, ctx);
		return;
	}

	shift = __ffs(upper);
	upper >>= shift;
	shift += 12;

	emit_imm(rd, upper, ctx);

	emit_slli(rd, rd, shift, ctx);
	if (lower)
		emit_addi(rd, rd, lower, ctx);
}

static void __build_epilogue(bool is_tail_call, struct rv_jit_context *ctx)
{
	int stack_adjust = ctx->stack_size, store_offset = stack_adjust - 8;

	if (seen_reg(RV_REG_RA, ctx)) {
		emit_ld(RV_REG_RA, store_offset, RV_REG_SP, ctx);
		store_offset -= 8;
	}
	emit_ld(RV_REG_FP, store_offset, RV_REG_SP, ctx);
	store_offset -= 8;
	if (seen_reg(RV_REG_S1, ctx)) {
		emit_ld(RV_REG_S1, store_offset, RV_REG_SP, ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S2, ctx)) {
		emit_ld(RV_REG_S2, store_offset, RV_REG_SP, ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S3, ctx)) {
		emit_ld(RV_REG_S3, store_offset, RV_REG_SP, ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S4, ctx)) {
		emit_ld(RV_REG_S4, store_offset, RV_REG_SP, ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S5, ctx)) {
		emit_ld(RV_REG_S5, store_offset, RV_REG_SP, ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S6, ctx)) {
		emit_ld(RV_REG_S6, store_offset, RV_REG_SP, ctx);
		store_offset -= 8;
	}

	emit_addi(RV_REG_SP, RV_REG_SP, stack_adjust, ctx);
	/* Set return value. */
	if (!is_tail_call)
		emit_mv(RV_REG_A0, RV_REG_A5, ctx);
	emit_jalr(RV_REG_ZERO, is_tail_call ? RV_REG_T3 : RV_REG_RA,
		  is_tail_call ? 4 : 0, /* skip TCC init */
		  ctx);
}

static void emit_bcc(u8 cond, u8 rd, u8 rs, int rvoff,
		     struct rv_jit_context *ctx)
{
	switch (cond) {
	case BPF_JEQ:
		emit(rv_beq(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JGT:
		emit(rv_bltu(rs, rd, rvoff >> 1), ctx);
		return;
	case BPF_JLT:
		emit(rv_bltu(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JGE:
		emit(rv_bgeu(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JLE:
		emit(rv_bgeu(rs, rd, rvoff >> 1), ctx);
		return;
	case BPF_JNE:
		emit(rv_bne(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JSGT:
		emit(rv_blt(rs, rd, rvoff >> 1), ctx);
		return;
	case BPF_JSLT:
		emit(rv_blt(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JSGE:
		emit(rv_bge(rd, rs, rvoff >> 1), ctx);
		return;
	case BPF_JSLE:
		emit(rv_bge(rs, rd, rvoff >> 1), ctx);
	}
}

static void emit_branch(u8 cond, u8 rd, u8 rs, int rvoff,
			struct rv_jit_context *ctx)
{
	s64 upper, lower;

	if (is_13b_int(rvoff)) {
		emit_bcc(cond, rd, rs, rvoff, ctx);
		return;
	}

	/* Adjust for jal */
	rvoff -= 4;

	/* Transform, e.g.:
	 *   bne rd,rs,foo
	 * to
	 *   beq rd,rs,<.L1>
	 *   (auipc foo)
	 *   jal(r) foo
	 * .L1
	 */
	cond = invert_bpf_cond(cond);
	if (is_21b_int(rvoff)) {
		emit_bcc(cond, rd, rs, 8, ctx);
		emit(rv_jal(RV_REG_ZERO, rvoff >> 1), ctx);
		return;
	}

	/* 32b No need for an additional rvoff adjustment, since we
	 * get that from the auipc at PC', where PC = PC' + 4.
	 */
	upper = (rvoff + (1 << 11)) >> 12;
	lower = rvoff & 0xfff;

	emit_bcc(cond, rd, rs, 12, ctx);
	emit(rv_auipc(RV_REG_T1, upper), ctx);
	emit(rv_jalr(RV_REG_ZERO, RV_REG_T1, lower), ctx);
}

static void emit_zext_32(u8 reg, struct rv_jit_context *ctx)
{
	emit_slli(reg, reg, 32, ctx);
	emit_srli(reg, reg, 32, ctx);
}

static int emit_bpf_tail_call(int insn, struct rv_jit_context *ctx)
{
	int tc_ninsn, off, start_insn = ctx->ninsns;
	u8 tcc = rv_tail_call_reg(ctx);

	/* a0: &ctx
	 * a1: &array
	 * a2: index
	 *
	 * if (index >= array->map.max_entries)
	 *	goto out;
	 */
	tc_ninsn = insn ? ctx->offset[insn] - ctx->offset[insn - 1] :
		   ctx->offset[0];
	emit_zext_32(RV_REG_A2, ctx);

	off = offsetof(struct bpf_array, map.max_entries);
	if (is_12b_check(off, insn))
		return -1;
	emit(rv_lwu(RV_REG_T1, off, RV_REG_A1), ctx);
	off = ninsns_rvoff(tc_ninsn - (ctx->ninsns - start_insn));
	emit_branch(BPF_JGE, RV_REG_A2, RV_REG_T1, off, ctx);

	/* if (TCC-- < 0)
	 *     goto out;
	 */
	emit_addi(RV_REG_T1, tcc, -1, ctx);
	off = ninsns_rvoff(tc_ninsn - (ctx->ninsns - start_insn));
	emit_branch(BPF_JSLT, tcc, RV_REG_ZERO, off, ctx);

	/* prog = array->ptrs[index];
	 * if (!prog)
	 *     goto out;
	 */
	emit_slli(RV_REG_T2, RV_REG_A2, 3, ctx);
	emit_add(RV_REG_T2, RV_REG_T2, RV_REG_A1, ctx);
	off = offsetof(struct bpf_array, ptrs);
	if (is_12b_check(off, insn))
		return -1;
	emit_ld(RV_REG_T2, off, RV_REG_T2, ctx);
	off = ninsns_rvoff(tc_ninsn - (ctx->ninsns - start_insn));
	emit_branch(BPF_JEQ, RV_REG_T2, RV_REG_ZERO, off, ctx);

	/* goto *(prog->bpf_func + 4); */
	off = offsetof(struct bpf_prog, bpf_func);
	if (is_12b_check(off, insn))
		return -1;
	emit_ld(RV_REG_T3, off, RV_REG_T2, ctx);
	emit_mv(RV_REG_TCC, RV_REG_T1, ctx);
	__build_epilogue(true, ctx);
	return 0;
}

static void init_regs(u8 *rd, u8 *rs, const struct bpf_insn *insn,
		      struct rv_jit_context *ctx)
{
	u8 code = insn->code;

	switch (code) {
	case BPF_JMP | BPF_JA:
	case BPF_JMP | BPF_CALL:
	case BPF_JMP | BPF_EXIT:
	case BPF_JMP | BPF_TAIL_CALL:
		break;
	default:
		*rd = bpf_to_rv_reg(insn->dst_reg, ctx);
	}

	if (code & (BPF_ALU | BPF_X) || code & (BPF_ALU64 | BPF_X) ||
	    code & (BPF_JMP | BPF_X) || code & (BPF_JMP32 | BPF_X) ||
	    code & BPF_LDX || code & BPF_STX)
		*rs = bpf_to_rv_reg(insn->src_reg, ctx);
}

static void emit_zext_32_rd_rs(u8 *rd, u8 *rs, struct rv_jit_context *ctx)
{
	emit_mv(RV_REG_T2, *rd, ctx);
	emit_zext_32(RV_REG_T2, ctx);
	emit_mv(RV_REG_T1, *rs, ctx);
	emit_zext_32(RV_REG_T1, ctx);
	*rd = RV_REG_T2;
	*rs = RV_REG_T1;
}

static void emit_sext_32_rd_rs(u8 *rd, u8 *rs, struct rv_jit_context *ctx)
{
	emit_addiw(RV_REG_T2, *rd, 0, ctx);
	emit_addiw(RV_REG_T1, *rs, 0, ctx);
	*rd = RV_REG_T2;
	*rs = RV_REG_T1;
}

static void emit_zext_32_rd_t1(u8 *rd, struct rv_jit_context *ctx)
{
	emit_mv(RV_REG_T2, *rd, ctx);
	emit_zext_32(RV_REG_T2, ctx);
	emit_zext_32(RV_REG_T1, ctx);
	*rd = RV_REG_T2;
}

static void emit_sext_32_rd(u8 *rd, struct rv_jit_context *ctx)
{
	emit_addiw(RV_REG_T2, *rd, 0, ctx);
	*rd = RV_REG_T2;
}

static int emit_jump_and_link(u8 rd, s64 rvoff, bool force_jalr,
			      struct rv_jit_context *ctx)
{
	s64 upper, lower;

	if (rvoff && is_21b_int(rvoff) && !force_jalr) {
		emit(rv_jal(rd, rvoff >> 1), ctx);
		return 0;
	} else if (in_auipc_jalr_range(rvoff)) {
		upper = (rvoff + (1 << 11)) >> 12;
		lower = rvoff & 0xfff;
		emit(rv_auipc(RV_REG_T1, upper), ctx);
		emit(rv_jalr(rd, RV_REG_T1, lower), ctx);
		return 0;
	}

	pr_err("bpf-jit: target offset 0x%llx is out of range\n", rvoff);
	return -ERANGE;
}

static bool is_signed_bpf_cond(u8 cond)
{
	return cond == BPF_JSGT || cond == BPF_JSLT ||
		cond == BPF_JSGE || cond == BPF_JSLE;
}

static int emit_call(bool fixed, u64 addr, struct rv_jit_context *ctx)
{
	s64 off = 0;
	u64 ip;
	u8 rd;
	int ret;

	if (addr && ctx->insns) {
		ip = (u64)(long)(ctx->insns + ctx->ninsns);
		off = addr - ip;
	}

	ret = emit_jump_and_link(RV_REG_RA, off, !fixed, ctx);
	if (ret)
		return ret;
	rd = bpf_to_rv_reg(BPF_REG_0, ctx);
	emit_mv(rd, RV_REG_A0, ctx);
	return 0;
}

int bpf_jit_emit_insn(const struct bpf_insn *insn, struct rv_jit_context *ctx,
		      bool extra_pass)
{
	bool is64 = BPF_CLASS(insn->code) == BPF_ALU64 ||
		    BPF_CLASS(insn->code) == BPF_JMP;
	int s, e, rvoff, ret, i = insn - ctx->prog->insnsi;
	struct bpf_prog_aux *aux = ctx->prog->aux;
	u8 rd = -1, rs = -1, code = insn->code;
	s16 off = insn->off;
	s32 imm = insn->imm;

	init_regs(&rd, &rs, insn, ctx);

	switch (code) {
	/* dst = src */
	case BPF_ALU | BPF_MOV | BPF_X:
	case BPF_ALU64 | BPF_MOV | BPF_X:
		if (imm == 1) {
			/* Special mov32 for zext */
			emit_zext_32(rd, ctx);
			break;
		}
		emit_mv(rd, rs, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = dst OP src */
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_X:
		emit_add(rd, rd, rs, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
		if (is64)
			emit_sub(rd, rd, rs, ctx);
		else
			emit_subw(rd, rd, rs, ctx);

		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_AND | BPF_X:
		emit_and(rd, rd, rs, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
		emit_or(rd, rd, rs, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
		emit_xor(rd, rd, rs, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_X:
	case BPF_ALU64 | BPF_MUL | BPF_X:
		emit(is64 ? rv_mul(rd, rd, rs) : rv_mulw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_DIV | BPF_X:
		emit(is64 ? rv_divu(rd, rd, rs) : rv_divuw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
		emit(is64 ? rv_remu(rd, rd, rs) : rv_remuw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_LSH | BPF_X:
	case BPF_ALU64 | BPF_LSH | BPF_X:
		emit(is64 ? rv_sll(rd, rd, rs) : rv_sllw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_X:
	case BPF_ALU64 | BPF_RSH | BPF_X:
		emit(is64 ? rv_srl(rd, rd, rs) : rv_srlw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_X:
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		emit(is64 ? rv_sra(rd, rd, rs) : rv_sraw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
	case BPF_ALU64 | BPF_NEG:
		emit_sub(rd, RV_REG_ZERO, rd, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = BSWAP##imm(dst) */
	case BPF_ALU | BPF_END | BPF_FROM_LE:
		switch (imm) {
		case 16:
			emit_slli(rd, rd, 48, ctx);
			emit_srli(rd, rd, 48, ctx);
			break;
		case 32:
			if (!aux->verifier_zext)
				emit_zext_32(rd, ctx);
			break;
		case 64:
			/* Do nothing */
			break;
		}
		break;

	case BPF_ALU | BPF_END | BPF_FROM_BE:
		emit_li(RV_REG_T2, 0, ctx);

		emit_andi(RV_REG_T1, rd, 0xff, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
		emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
		emit_srli(rd, rd, 8, ctx);
		if (imm == 16)
			goto out_be;

		emit_andi(RV_REG_T1, rd, 0xff, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
		emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
		emit_srli(rd, rd, 8, ctx);

		emit_andi(RV_REG_T1, rd, 0xff, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
		emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
		emit_srli(rd, rd, 8, ctx);
		if (imm == 32)
			goto out_be;

		emit_andi(RV_REG_T1, rd, 0xff, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
		emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
		emit_srli(rd, rd, 8, ctx);

		emit_andi(RV_REG_T1, rd, 0xff, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
		emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
		emit_srli(rd, rd, 8, ctx);

		emit_andi(RV_REG_T1, rd, 0xff, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
		emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
		emit_srli(rd, rd, 8, ctx);

		emit_andi(RV_REG_T1, rd, 0xff, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);
		emit_slli(RV_REG_T2, RV_REG_T2, 8, ctx);
		emit_srli(rd, rd, 8, ctx);
out_be:
		emit_andi(RV_REG_T1, rd, 0xff, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, RV_REG_T1, ctx);

		emit_mv(rd, RV_REG_T2, ctx);
		break;

	/* dst = imm */
	case BPF_ALU | BPF_MOV | BPF_K:
	case BPF_ALU64 | BPF_MOV | BPF_K:
		emit_imm(rd, imm, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = dst OP imm */
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_ADD | BPF_K:
		if (is_12b_int(imm)) {
			emit_addi(rd, rd, imm, ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit_add(rd, rd, RV_REG_T1, ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
		if (is_12b_int(-imm)) {
			emit_addi(rd, rd, -imm, ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit_sub(rd, rd, RV_REG_T1, ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU64 | BPF_AND | BPF_K:
		if (is_12b_int(imm)) {
			emit_andi(rd, rd, imm, ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit_and(rd, rd, RV_REG_T1, ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_K:
	case BPF_ALU64 | BPF_OR | BPF_K:
		if (is_12b_int(imm)) {
			emit(rv_ori(rd, rd, imm), ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit_or(rd, rd, RV_REG_T1, ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
		if (is_12b_int(imm)) {
			emit(rv_xori(rd, rd, imm), ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit_xor(rd, rd, RV_REG_T1, ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU64 | BPF_MUL | BPF_K:
		emit_imm(RV_REG_T1, imm, ctx);
		emit(is64 ? rv_mul(rd, rd, RV_REG_T1) :
		     rv_mulw(rd, rd, RV_REG_T1), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_DIV | BPF_K:
		emit_imm(RV_REG_T1, imm, ctx);
		emit(is64 ? rv_divu(rd, rd, RV_REG_T1) :
		     rv_divuw(rd, rd, RV_REG_T1), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		emit_imm(RV_REG_T1, imm, ctx);
		emit(is64 ? rv_remu(rd, rd, RV_REG_T1) :
		     rv_remuw(rd, rd, RV_REG_T1), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_LSH | BPF_K:
	case BPF_ALU64 | BPF_LSH | BPF_K:
		emit_slli(rd, rd, imm, ctx);

		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_K:
	case BPF_ALU64 | BPF_RSH | BPF_K:
		if (is64)
			emit_srli(rd, rd, imm, ctx);
		else
			emit(rv_srliw(rd, rd, imm), ctx);

		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_K:
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		if (is64)
			emit_srai(rd, rd, imm, ctx);
		else
			emit(rv_sraiw(rd, rd, imm), ctx);

		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* JUMP off */
	case BPF_JMP | BPF_JA:
		rvoff = rv_offset(i, off, ctx);
		ret = emit_jump_and_link(RV_REG_ZERO, rvoff, false, ctx);
		if (ret)
			return ret;
		break;

	/* IF (dst COND src) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_X:
	case BPF_JMP32 | BPF_JEQ | BPF_X:
	case BPF_JMP | BPF_JGT | BPF_X:
	case BPF_JMP32 | BPF_JGT | BPF_X:
	case BPF_JMP | BPF_JLT | BPF_X:
	case BPF_JMP32 | BPF_JLT | BPF_X:
	case BPF_JMP | BPF_JGE | BPF_X:
	case BPF_JMP32 | BPF_JGE | BPF_X:
	case BPF_JMP | BPF_JLE | BPF_X:
	case BPF_JMP32 | BPF_JLE | BPF_X:
	case BPF_JMP | BPF_JNE | BPF_X:
	case BPF_JMP32 | BPF_JNE | BPF_X:
	case BPF_JMP | BPF_JSGT | BPF_X:
	case BPF_JMP32 | BPF_JSGT | BPF_X:
	case BPF_JMP | BPF_JSLT | BPF_X:
	case BPF_JMP32 | BPF_JSLT | BPF_X:
	case BPF_JMP | BPF_JSGE | BPF_X:
	case BPF_JMP32 | BPF_JSGE | BPF_X:
	case BPF_JMP | BPF_JSLE | BPF_X:
	case BPF_JMP32 | BPF_JSLE | BPF_X:
	case BPF_JMP | BPF_JSET | BPF_X:
	case BPF_JMP32 | BPF_JSET | BPF_X:
		rvoff = rv_offset(i, off, ctx);
		if (!is64) {
			s = ctx->ninsns;
			if (is_signed_bpf_cond(BPF_OP(code)))
				emit_sext_32_rd_rs(&rd, &rs, ctx);
			else
				emit_zext_32_rd_rs(&rd, &rs, ctx);
			e = ctx->ninsns;

			/* Adjust for extra insns */
			rvoff -= ninsns_rvoff(e - s);
		}

		if (BPF_OP(code) == BPF_JSET) {
			/* Adjust for and */
			rvoff -= 4;
			emit_and(RV_REG_T1, rd, rs, ctx);
			emit_branch(BPF_JNE, RV_REG_T1, RV_REG_ZERO, rvoff,
				    ctx);
		} else {
			emit_branch(BPF_OP(code), rd, rs, rvoff, ctx);
		}
		break;

	/* IF (dst COND imm) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_K:
	case BPF_JMP32 | BPF_JEQ | BPF_K:
	case BPF_JMP | BPF_JGT | BPF_K:
	case BPF_JMP32 | BPF_JGT | BPF_K:
	case BPF_JMP | BPF_JLT | BPF_K:
	case BPF_JMP32 | BPF_JLT | BPF_K:
	case BPF_JMP | BPF_JGE | BPF_K:
	case BPF_JMP32 | BPF_JGE | BPF_K:
	case BPF_JMP | BPF_JLE | BPF_K:
	case BPF_JMP32 | BPF_JLE | BPF_K:
	case BPF_JMP | BPF_JNE | BPF_K:
	case BPF_JMP32 | BPF_JNE | BPF_K:
	case BPF_JMP | BPF_JSGT | BPF_K:
	case BPF_JMP32 | BPF_JSGT | BPF_K:
	case BPF_JMP | BPF_JSLT | BPF_K:
	case BPF_JMP32 | BPF_JSLT | BPF_K:
	case BPF_JMP | BPF_JSGE | BPF_K:
	case BPF_JMP32 | BPF_JSGE | BPF_K:
	case BPF_JMP | BPF_JSLE | BPF_K:
	case BPF_JMP32 | BPF_JSLE | BPF_K:
		rvoff = rv_offset(i, off, ctx);
		s = ctx->ninsns;
		if (imm) {
			emit_imm(RV_REG_T1, imm, ctx);
			rs = RV_REG_T1;
		} else {
			/* If imm is 0, simply use zero register. */
			rs = RV_REG_ZERO;
		}
		if (!is64) {
			if (is_signed_bpf_cond(BPF_OP(code)))
				emit_sext_32_rd(&rd, ctx);
			else
				emit_zext_32_rd_t1(&rd, ctx);
		}
		e = ctx->ninsns;

		/* Adjust for extra insns */
		rvoff -= ninsns_rvoff(e - s);
		emit_branch(BPF_OP(code), rd, rs, rvoff, ctx);
		break;

	case BPF_JMP | BPF_JSET | BPF_K:
	case BPF_JMP32 | BPF_JSET | BPF_K:
		rvoff = rv_offset(i, off, ctx);
		s = ctx->ninsns;
		if (is_12b_int(imm)) {
			emit_andi(RV_REG_T1, rd, imm, ctx);
		} else {
			emit_imm(RV_REG_T1, imm, ctx);
			emit_and(RV_REG_T1, rd, RV_REG_T1, ctx);
		}
		/* For jset32, we should clear the upper 32 bits of t1, but
		 * sign-extension is sufficient here and saves one instruction,
		 * as t1 is used only in comparison against zero.
		 */
		if (!is64 && imm < 0)
			emit_addiw(RV_REG_T1, RV_REG_T1, 0, ctx);
		e = ctx->ninsns;
		rvoff -= ninsns_rvoff(e - s);
		emit_branch(BPF_JNE, RV_REG_T1, RV_REG_ZERO, rvoff, ctx);
		break;

	/* function call */
	case BPF_JMP | BPF_CALL:
	{
		bool fixed;
		u64 addr;

		mark_call(ctx);
		ret = bpf_jit_get_func_addr(ctx->prog, insn, extra_pass, &addr,
					    &fixed);
		if (ret < 0)
			return ret;
		ret = emit_call(fixed, addr, ctx);
		if (ret)
			return ret;
		break;
	}
	/* tail call */
	case BPF_JMP | BPF_TAIL_CALL:
		if (emit_bpf_tail_call(i, ctx))
			return -1;
		break;

	/* function return */
	case BPF_JMP | BPF_EXIT:
		if (i == ctx->prog->len - 1)
			break;

		rvoff = epilogue_offset(ctx);
		ret = emit_jump_and_link(RV_REG_ZERO, rvoff, false, ctx);
		if (ret)
			return ret;
		break;

	/* dst = imm64 */
	case BPF_LD | BPF_IMM | BPF_DW:
	{
		struct bpf_insn insn1 = insn[1];
		u64 imm64;

		imm64 = (u64)insn1.imm << 32 | (u32)imm;
		emit_imm(rd, imm64, ctx);
		return 1;
	}

	/* LDX: dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_B:
		if (is_12b_int(off)) {
			emit(rv_lbu(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit_add(RV_REG_T1, RV_REG_T1, rs, ctx);
		emit(rv_lbu(rd, 0, RV_REG_T1), ctx);
		if (insn_is_zext(&insn[1]))
			return 1;
		break;
	case BPF_LDX | BPF_MEM | BPF_H:
		if (is_12b_int(off)) {
			emit(rv_lhu(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit_add(RV_REG_T1, RV_REG_T1, rs, ctx);
		emit(rv_lhu(rd, 0, RV_REG_T1), ctx);
		if (insn_is_zext(&insn[1]))
			return 1;
		break;
	case BPF_LDX | BPF_MEM | BPF_W:
		if (is_12b_int(off)) {
			emit(rv_lwu(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit_add(RV_REG_T1, RV_REG_T1, rs, ctx);
		emit(rv_lwu(rd, 0, RV_REG_T1), ctx);
		if (insn_is_zext(&insn[1]))
			return 1;
		break;
	case BPF_LDX | BPF_MEM | BPF_DW:
		if (is_12b_int(off)) {
			emit_ld(rd, off, rs, ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit_add(RV_REG_T1, RV_REG_T1, rs, ctx);
		emit_ld(rd, 0, RV_REG_T1, ctx);
		break;

	/* speculation barrier */
	case BPF_ST | BPF_NOSPEC:
		break;

	/* ST: *(size *)(dst + off) = imm */
	case BPF_ST | BPF_MEM | BPF_B:
		emit_imm(RV_REG_T1, imm, ctx);
		if (is_12b_int(off)) {
			emit(rv_sb(rd, off, RV_REG_T1), ctx);
			break;
		}

		emit_imm(RV_REG_T2, off, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, rd, ctx);
		emit(rv_sb(RV_REG_T2, 0, RV_REG_T1), ctx);
		break;

	case BPF_ST | BPF_MEM | BPF_H:
		emit_imm(RV_REG_T1, imm, ctx);
		if (is_12b_int(off)) {
			emit(rv_sh(rd, off, RV_REG_T1), ctx);
			break;
		}

		emit_imm(RV_REG_T2, off, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, rd, ctx);
		emit(rv_sh(RV_REG_T2, 0, RV_REG_T1), ctx);
		break;
	case BPF_ST | BPF_MEM | BPF_W:
		emit_imm(RV_REG_T1, imm, ctx);
		if (is_12b_int(off)) {
			emit_sw(rd, off, RV_REG_T1, ctx);
			break;
		}

		emit_imm(RV_REG_T2, off, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, rd, ctx);
		emit_sw(RV_REG_T2, 0, RV_REG_T1, ctx);
		break;
	case BPF_ST | BPF_MEM | BPF_DW:
		emit_imm(RV_REG_T1, imm, ctx);
		if (is_12b_int(off)) {
			emit_sd(rd, off, RV_REG_T1, ctx);
			break;
		}

		emit_imm(RV_REG_T2, off, ctx);
		emit_add(RV_REG_T2, RV_REG_T2, rd, ctx);
		emit_sd(RV_REG_T2, 0, RV_REG_T1, ctx);
		break;

	/* STX: *(size *)(dst + off) = src */
	case BPF_STX | BPF_MEM | BPF_B:
		if (is_12b_int(off)) {
			emit(rv_sb(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit_add(RV_REG_T1, RV_REG_T1, rd, ctx);
		emit(rv_sb(RV_REG_T1, 0, rs), ctx);
		break;
	case BPF_STX | BPF_MEM | BPF_H:
		if (is_12b_int(off)) {
			emit(rv_sh(rd, off, rs), ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit_add(RV_REG_T1, RV_REG_T1, rd, ctx);
		emit(rv_sh(RV_REG_T1, 0, rs), ctx);
		break;
	case BPF_STX | BPF_MEM | BPF_W:
		if (is_12b_int(off)) {
			emit_sw(rd, off, rs, ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit_add(RV_REG_T1, RV_REG_T1, rd, ctx);
		emit_sw(RV_REG_T1, 0, rs, ctx);
		break;
	case BPF_STX | BPF_MEM | BPF_DW:
		if (is_12b_int(off)) {
			emit_sd(rd, off, rs, ctx);
			break;
		}

		emit_imm(RV_REG_T1, off, ctx);
		emit_add(RV_REG_T1, RV_REG_T1, rd, ctx);
		emit_sd(RV_REG_T1, 0, rs, ctx);
		break;
	case BPF_STX | BPF_ATOMIC | BPF_W:
	case BPF_STX | BPF_ATOMIC | BPF_DW:
		if (insn->imm != BPF_ADD) {
			pr_err("bpf-jit: not supported: atomic operation %02x ***\n",
			       insn->imm);
			return -EINVAL;
		}

		/* atomic_add: lock *(u32 *)(dst + off) += src
		 * atomic_add: lock *(u64 *)(dst + off) += src
		 */

		if (off) {
			if (is_12b_int(off)) {
				emit_addi(RV_REG_T1, rd, off, ctx);
			} else {
				emit_imm(RV_REG_T1, off, ctx);
				emit_add(RV_REG_T1, RV_REG_T1, rd, ctx);
			}

			rd = RV_REG_T1;
		}

		emit(BPF_SIZE(code) == BPF_W ?
		     rv_amoadd_w(RV_REG_ZERO, rs, rd, 0, 0) :
		     rv_amoadd_d(RV_REG_ZERO, rs, rd, 0, 0), ctx);
		break;
	default:
		pr_err("bpf-jit: unknown opcode %02x\n", code);
		return -EINVAL;
	}

	return 0;
}

void bpf_jit_build_prologue(struct rv_jit_context *ctx)
{
	int stack_adjust = 0, store_offset, bpf_stack_adjust;

	bpf_stack_adjust = round_up(ctx->prog->aux->stack_depth, 16);
	if (bpf_stack_adjust)
		mark_fp(ctx);

	if (seen_reg(RV_REG_RA, ctx))
		stack_adjust += 8;
	stack_adjust += 8; /* RV_REG_FP */
	if (seen_reg(RV_REG_S1, ctx))
		stack_adjust += 8;
	if (seen_reg(RV_REG_S2, ctx))
		stack_adjust += 8;
	if (seen_reg(RV_REG_S3, ctx))
		stack_adjust += 8;
	if (seen_reg(RV_REG_S4, ctx))
		stack_adjust += 8;
	if (seen_reg(RV_REG_S5, ctx))
		stack_adjust += 8;
	if (seen_reg(RV_REG_S6, ctx))
		stack_adjust += 8;

	stack_adjust = round_up(stack_adjust, 16);
	stack_adjust += bpf_stack_adjust;

	store_offset = stack_adjust - 8;

	/* First instruction is always setting the tail-call-counter
	 * (TCC) register. This instruction is skipped for tail calls.
	 * Force using a 4-byte (non-compressed) instruction.
	 */
	emit(rv_addi(RV_REG_TCC, RV_REG_ZERO, MAX_TAIL_CALL_CNT), ctx);

	emit_addi(RV_REG_SP, RV_REG_SP, -stack_adjust, ctx);

	if (seen_reg(RV_REG_RA, ctx)) {
		emit_sd(RV_REG_SP, store_offset, RV_REG_RA, ctx);
		store_offset -= 8;
	}
	emit_sd(RV_REG_SP, store_offset, RV_REG_FP, ctx);
	store_offset -= 8;
	if (seen_reg(RV_REG_S1, ctx)) {
		emit_sd(RV_REG_SP, store_offset, RV_REG_S1, ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S2, ctx)) {
		emit_sd(RV_REG_SP, store_offset, RV_REG_S2, ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S3, ctx)) {
		emit_sd(RV_REG_SP, store_offset, RV_REG_S3, ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S4, ctx)) {
		emit_sd(RV_REG_SP, store_offset, RV_REG_S4, ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S5, ctx)) {
		emit_sd(RV_REG_SP, store_offset, RV_REG_S5, ctx);
		store_offset -= 8;
	}
	if (seen_reg(RV_REG_S6, ctx)) {
		emit_sd(RV_REG_SP, store_offset, RV_REG_S6, ctx);
		store_offset -= 8;
	}

	emit_addi(RV_REG_FP, RV_REG_SP, stack_adjust, ctx);

	if (bpf_stack_adjust)
		emit_addi(RV_REG_S5, RV_REG_SP, bpf_stack_adjust, ctx);

	/* Program contains calls and tail calls, so RV_REG_TCC need
	 * to be saved across calls.
	 */
	if (seen_tail_call(ctx) && seen_call(ctx))
		emit_mv(RV_REG_TCC_SAVED, RV_REG_TCC, ctx);

	ctx->stack_size = stack_adjust;
}

void bpf_jit_build_epilogue(struct rv_jit_context *ctx)
{
	__build_epilogue(false, ctx);
}
