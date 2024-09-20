// SPDX-License-Identifier: GPL-2.0
/* BPF JIT compiler for RV64G
 *
 * Copyright(c) 2019 Björn Töpel <bjorn.topel@gmail.com>
 *
 */

#include <linux/bitfield.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/memory.h>
#include <linux/stop_machine.h>
#include <asm/patch.h>
#include <asm/cfi.h>
#include <asm/percpu.h>
#include "bpf_jit.h"

#define RV_MAX_REG_ARGS 8
#define RV_FENTRY_NINSNS 2
#define RV_FENTRY_NBYTES (RV_FENTRY_NINSNS * 4)
/* imm that allows emit_imm to emit max count insns */
#define RV_MAX_COUNT_IMM 0x7FFF7FF7FF7FF7FF

#define RV_REG_TCC RV_REG_A6
#define RV_REG_TCC_SAVED RV_REG_S6 /* Store A6 in S6 if program do calls */
#define RV_REG_ARENA RV_REG_S7 /* For storing arena_vm_start */

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

static const int pt_regmap[] = {
	[RV_REG_A0] = offsetof(struct pt_regs, a0),
	[RV_REG_A1] = offsetof(struct pt_regs, a1),
	[RV_REG_A2] = offsetof(struct pt_regs, a2),
	[RV_REG_A3] = offsetof(struct pt_regs, a3),
	[RV_REG_A4] = offsetof(struct pt_regs, a4),
	[RV_REG_A5] = offsetof(struct pt_regs, a5),
	[RV_REG_S1] = offsetof(struct pt_regs, s1),
	[RV_REG_S2] = offsetof(struct pt_regs, s2),
	[RV_REG_S3] = offsetof(struct pt_regs, s3),
	[RV_REG_S4] = offsetof(struct pt_regs, s4),
	[RV_REG_S5] = offsetof(struct pt_regs, s5),
	[RV_REG_T0] = offsetof(struct pt_regs, t0),
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

/* Modify rd pointer to alternate reg to avoid corrupting original reg */
static void emit_sextw_alt(u8 *rd, u8 ra, struct rv_jit_context *ctx)
{
	emit_sextw(ra, *rd, ctx);
	*rd = ra;
}

static void emit_zextw_alt(u8 *rd, u8 ra, struct rv_jit_context *ctx)
{
	emit_zextw(ra, *rd, ctx);
	*rd = ra;
}

/* Emit fixed-length instructions for address */
static int emit_addr(u8 rd, u64 addr, bool extra_pass, struct rv_jit_context *ctx)
{
	/*
	 * Use the ro_insns(RX) to calculate the offset as the BPF program will
	 * finally run from this memory region.
	 */
	u64 ip = (u64)(ctx->ro_insns + ctx->ninsns);
	s64 off = addr - ip;
	s64 upper = (off + (1 << 11)) >> 12;
	s64 lower = off & 0xfff;

	if (extra_pass && !in_auipc_jalr_range(off)) {
		pr_err("bpf-jit: target offset 0x%llx is out of range\n", off);
		return -ERANGE;
	}

	emit(rv_auipc(rd, upper), ctx);
	emit(rv_addi(rd, rd, lower), ctx);
	return 0;
}

/* Emit variable-length instructions for 32-bit and 64-bit imm */
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
	if (ctx->arena_vm_start) {
		emit_ld(RV_REG_ARENA, store_offset, RV_REG_SP, ctx);
		store_offset -= 8;
	}

	emit_addi(RV_REG_SP, RV_REG_SP, stack_adjust, ctx);
	/* Set return value. */
	if (!is_tail_call)
		emit_addiw(RV_REG_A0, RV_REG_A5, 0, ctx);
	emit_jalr(RV_REG_ZERO, is_tail_call ? RV_REG_T3 : RV_REG_RA,
		  is_tail_call ? (RV_FENTRY_NINSNS + 1) * 4 : 0, /* skip reserved nops and TCC init */
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
	emit_zextw(RV_REG_A2, RV_REG_A2, ctx);

	off = offsetof(struct bpf_array, map.max_entries);
	if (is_12b_check(off, insn))
		return -1;
	emit(rv_lwu(RV_REG_T1, off, RV_REG_A1), ctx);
	off = ninsns_rvoff(tc_ninsn - (ctx->ninsns - start_insn));
	emit_branch(BPF_JGE, RV_REG_A2, RV_REG_T1, off, ctx);

	/* if (--TCC < 0)
	 *     goto out;
	 */
	emit_addi(RV_REG_TCC, tcc, -1, ctx);
	off = ninsns_rvoff(tc_ninsn - (ctx->ninsns - start_insn));
	emit_branch(BPF_JSLT, RV_REG_TCC, RV_REG_ZERO, off, ctx);

	/* prog = array->ptrs[index];
	 * if (!prog)
	 *     goto out;
	 */
	emit_sh3add(RV_REG_T2, RV_REG_A2, RV_REG_A1, ctx);
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

static int emit_jump_and_link(u8 rd, s64 rvoff, bool fixed_addr,
			      struct rv_jit_context *ctx)
{
	s64 upper, lower;

	if (rvoff && fixed_addr && is_21b_int(rvoff)) {
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

static int emit_call(u64 addr, bool fixed_addr, struct rv_jit_context *ctx)
{
	s64 off = 0;
	u64 ip;

	if (addr && ctx->insns && ctx->ro_insns) {
		/*
		 * Use the ro_insns(RX) to calculate the offset as the BPF
		 * program will finally run from this memory region.
		 */
		ip = (u64)(long)(ctx->ro_insns + ctx->ninsns);
		off = addr - ip;
	}

	return emit_jump_and_link(RV_REG_RA, off, fixed_addr, ctx);
}

static inline void emit_kcfi(u32 hash, struct rv_jit_context *ctx)
{
	if (IS_ENABLED(CONFIG_CFI_CLANG))
		emit(hash, ctx);
}

static void emit_atomic(u8 rd, u8 rs, s16 off, s32 imm, bool is64,
			struct rv_jit_context *ctx)
{
	u8 r0;
	int jmp_offset;

	if (off) {
		if (is_12b_int(off)) {
			emit_addi(RV_REG_T1, rd, off, ctx);
		} else {
			emit_imm(RV_REG_T1, off, ctx);
			emit_add(RV_REG_T1, RV_REG_T1, rd, ctx);
		}
		rd = RV_REG_T1;
	}

	switch (imm) {
	/* lock *(u32/u64 *)(dst_reg + off16) <op>= src_reg */
	case BPF_ADD:
		emit(is64 ? rv_amoadd_d(RV_REG_ZERO, rs, rd, 0, 0) :
		     rv_amoadd_w(RV_REG_ZERO, rs, rd, 0, 0), ctx);
		break;
	case BPF_AND:
		emit(is64 ? rv_amoand_d(RV_REG_ZERO, rs, rd, 0, 0) :
		     rv_amoand_w(RV_REG_ZERO, rs, rd, 0, 0), ctx);
		break;
	case BPF_OR:
		emit(is64 ? rv_amoor_d(RV_REG_ZERO, rs, rd, 0, 0) :
		     rv_amoor_w(RV_REG_ZERO, rs, rd, 0, 0), ctx);
		break;
	case BPF_XOR:
		emit(is64 ? rv_amoxor_d(RV_REG_ZERO, rs, rd, 0, 0) :
		     rv_amoxor_w(RV_REG_ZERO, rs, rd, 0, 0), ctx);
		break;
	/* src_reg = atomic_fetch_<op>(dst_reg + off16, src_reg) */
	case BPF_ADD | BPF_FETCH:
		emit(is64 ? rv_amoadd_d(rs, rs, rd, 1, 1) :
		     rv_amoadd_w(rs, rs, rd, 1, 1), ctx);
		if (!is64)
			emit_zextw(rs, rs, ctx);
		break;
	case BPF_AND | BPF_FETCH:
		emit(is64 ? rv_amoand_d(rs, rs, rd, 1, 1) :
		     rv_amoand_w(rs, rs, rd, 1, 1), ctx);
		if (!is64)
			emit_zextw(rs, rs, ctx);
		break;
	case BPF_OR | BPF_FETCH:
		emit(is64 ? rv_amoor_d(rs, rs, rd, 1, 1) :
		     rv_amoor_w(rs, rs, rd, 1, 1), ctx);
		if (!is64)
			emit_zextw(rs, rs, ctx);
		break;
	case BPF_XOR | BPF_FETCH:
		emit(is64 ? rv_amoxor_d(rs, rs, rd, 1, 1) :
		     rv_amoxor_w(rs, rs, rd, 1, 1), ctx);
		if (!is64)
			emit_zextw(rs, rs, ctx);
		break;
	/* src_reg = atomic_xchg(dst_reg + off16, src_reg); */
	case BPF_XCHG:
		emit(is64 ? rv_amoswap_d(rs, rs, rd, 1, 1) :
		     rv_amoswap_w(rs, rs, rd, 1, 1), ctx);
		if (!is64)
			emit_zextw(rs, rs, ctx);
		break;
	/* r0 = atomic_cmpxchg(dst_reg + off16, r0, src_reg); */
	case BPF_CMPXCHG:
		r0 = bpf_to_rv_reg(BPF_REG_0, ctx);
		if (is64)
			emit_mv(RV_REG_T2, r0, ctx);
		else
			emit_addiw(RV_REG_T2, r0, 0, ctx);
		emit(is64 ? rv_lr_d(r0, 0, rd, 0, 0) :
		     rv_lr_w(r0, 0, rd, 0, 0), ctx);
		jmp_offset = ninsns_rvoff(8);
		emit(rv_bne(RV_REG_T2, r0, jmp_offset >> 1), ctx);
		emit(is64 ? rv_sc_d(RV_REG_T3, rs, rd, 0, 0) :
		     rv_sc_w(RV_REG_T3, rs, rd, 0, 0), ctx);
		jmp_offset = ninsns_rvoff(-6);
		emit(rv_bne(RV_REG_T3, 0, jmp_offset >> 1), ctx);
		emit(rv_fence(0x3, 0x3), ctx);
		break;
	}
}

#define BPF_FIXUP_OFFSET_MASK   GENMASK(26, 0)
#define BPF_FIXUP_REG_MASK      GENMASK(31, 27)
#define REG_DONT_CLEAR_MARKER	0	/* RV_REG_ZERO unused in pt_regmap */

bool ex_handler_bpf(const struct exception_table_entry *ex,
		    struct pt_regs *regs)
{
	off_t offset = FIELD_GET(BPF_FIXUP_OFFSET_MASK, ex->fixup);
	int regs_offset = FIELD_GET(BPF_FIXUP_REG_MASK, ex->fixup);

	if (regs_offset != REG_DONT_CLEAR_MARKER)
		*(unsigned long *)((void *)regs + pt_regmap[regs_offset]) = 0;
	regs->epc = (unsigned long)&ex->fixup - offset;

	return true;
}

/* For accesses to BTF pointers, add an entry to the exception table */
static int add_exception_handler(const struct bpf_insn *insn,
				 struct rv_jit_context *ctx,
				 int dst_reg, int insn_len)
{
	struct exception_table_entry *ex;
	unsigned long pc;
	off_t ins_offset;
	off_t fixup_offset;

	if (!ctx->insns || !ctx->ro_insns || !ctx->prog->aux->extable ||
	    (BPF_MODE(insn->code) != BPF_PROBE_MEM && BPF_MODE(insn->code) != BPF_PROBE_MEMSX &&
	     BPF_MODE(insn->code) != BPF_PROBE_MEM32))
		return 0;

	if (WARN_ON_ONCE(ctx->nexentries >= ctx->prog->aux->num_exentries))
		return -EINVAL;

	if (WARN_ON_ONCE(insn_len > ctx->ninsns))
		return -EINVAL;

	if (WARN_ON_ONCE(!rvc_enabled() && insn_len == 1))
		return -EINVAL;

	ex = &ctx->prog->aux->extable[ctx->nexentries];
	pc = (unsigned long)&ctx->ro_insns[ctx->ninsns - insn_len];

	/*
	 * This is the relative offset of the instruction that may fault from
	 * the exception table itself. This will be written to the exception
	 * table and if this instruction faults, the destination register will
	 * be set to '0' and the execution will jump to the next instruction.
	 */
	ins_offset = pc - (long)&ex->insn;
	if (WARN_ON_ONCE(ins_offset >= 0 || ins_offset < INT_MIN))
		return -ERANGE;

	/*
	 * Since the extable follows the program, the fixup offset is always
	 * negative and limited to BPF_JIT_REGION_SIZE. Store a positive value
	 * to keep things simple, and put the destination register in the upper
	 * bits. We don't need to worry about buildtime or runtime sort
	 * modifying the upper bits because the table is already sorted, and
	 * isn't part of the main exception table.
	 *
	 * The fixup_offset is set to the next instruction from the instruction
	 * that may fault. The execution will jump to this after handling the
	 * fault.
	 */
	fixup_offset = (long)&ex->fixup - (pc + insn_len * sizeof(u16));
	if (!FIELD_FIT(BPF_FIXUP_OFFSET_MASK, fixup_offset))
		return -ERANGE;

	/*
	 * The offsets above have been calculated using the RO buffer but we
	 * need to use the R/W buffer for writes.
	 * switch ex to rw buffer for writing.
	 */
	ex = (void *)ctx->insns + ((void *)ex - (void *)ctx->ro_insns);

	ex->insn = ins_offset;

	ex->fixup = FIELD_PREP(BPF_FIXUP_OFFSET_MASK, fixup_offset) |
		FIELD_PREP(BPF_FIXUP_REG_MASK, dst_reg);
	ex->type = EX_TYPE_BPF;

	ctx->nexentries++;
	return 0;
}

static int gen_jump_or_nops(void *target, void *ip, u32 *insns, bool is_call)
{
	s64 rvoff;
	struct rv_jit_context ctx;

	ctx.ninsns = 0;
	ctx.insns = (u16 *)insns;

	if (!target) {
		emit(rv_nop(), &ctx);
		emit(rv_nop(), &ctx);
		return 0;
	}

	rvoff = (s64)(target - ip);
	return emit_jump_and_link(is_call ? RV_REG_T0 : RV_REG_ZERO, rvoff, false, &ctx);
}

int bpf_arch_text_poke(void *ip, enum bpf_text_poke_type poke_type,
		       void *old_addr, void *new_addr)
{
	u32 old_insns[RV_FENTRY_NINSNS], new_insns[RV_FENTRY_NINSNS];
	bool is_call = poke_type == BPF_MOD_CALL;
	int ret;

	if (!is_kernel_text((unsigned long)ip) &&
	    !is_bpf_text_address((unsigned long)ip))
		return -ENOTSUPP;

	ret = gen_jump_or_nops(old_addr, ip, old_insns, is_call);
	if (ret)
		return ret;

	if (memcmp(ip, old_insns, RV_FENTRY_NBYTES))
		return -EFAULT;

	ret = gen_jump_or_nops(new_addr, ip, new_insns, is_call);
	if (ret)
		return ret;

	cpus_read_lock();
	mutex_lock(&text_mutex);
	if (memcmp(ip, new_insns, RV_FENTRY_NBYTES))
		ret = patch_text(ip, new_insns, RV_FENTRY_NBYTES);
	mutex_unlock(&text_mutex);
	cpus_read_unlock();

	return ret;
}

static void store_args(int nr_arg_slots, int args_off, struct rv_jit_context *ctx)
{
	int i;

	for (i = 0; i < nr_arg_slots; i++) {
		if (i < RV_MAX_REG_ARGS) {
			emit_sd(RV_REG_FP, -args_off, RV_REG_A0 + i, ctx);
		} else {
			/* skip slots for T0 and FP of traced function */
			emit_ld(RV_REG_T1, 16 + (i - RV_MAX_REG_ARGS) * 8, RV_REG_FP, ctx);
			emit_sd(RV_REG_FP, -args_off, RV_REG_T1, ctx);
		}
		args_off -= 8;
	}
}

static void restore_args(int nr_reg_args, int args_off, struct rv_jit_context *ctx)
{
	int i;

	for (i = 0; i < nr_reg_args; i++) {
		emit_ld(RV_REG_A0 + i, -args_off, RV_REG_FP, ctx);
		args_off -= 8;
	}
}

static void restore_stack_args(int nr_stack_args, int args_off, int stk_arg_off,
			       struct rv_jit_context *ctx)
{
	int i;

	for (i = 0; i < nr_stack_args; i++) {
		emit_ld(RV_REG_T1, -(args_off - RV_MAX_REG_ARGS * 8), RV_REG_FP, ctx);
		emit_sd(RV_REG_FP, -stk_arg_off, RV_REG_T1, ctx);
		args_off -= 8;
		stk_arg_off -= 8;
	}
}

static int invoke_bpf_prog(struct bpf_tramp_link *l, int args_off, int retval_off,
			   int run_ctx_off, bool save_ret, struct rv_jit_context *ctx)
{
	int ret, branch_off;
	struct bpf_prog *p = l->link.prog;
	int cookie_off = offsetof(struct bpf_tramp_run_ctx, bpf_cookie);

	if (l->cookie) {
		emit_imm(RV_REG_T1, l->cookie, ctx);
		emit_sd(RV_REG_FP, -run_ctx_off + cookie_off, RV_REG_T1, ctx);
	} else {
		emit_sd(RV_REG_FP, -run_ctx_off + cookie_off, RV_REG_ZERO, ctx);
	}

	/* arg1: prog */
	emit_imm(RV_REG_A0, (const s64)p, ctx);
	/* arg2: &run_ctx */
	emit_addi(RV_REG_A1, RV_REG_FP, -run_ctx_off, ctx);
	ret = emit_call((const u64)bpf_trampoline_enter(p), true, ctx);
	if (ret)
		return ret;

	/* store prog start time */
	emit_mv(RV_REG_S1, RV_REG_A0, ctx);

	/* if (__bpf_prog_enter(prog) == 0)
	 *	goto skip_exec_of_prog;
	 */
	branch_off = ctx->ninsns;
	/* nop reserved for conditional jump */
	emit(rv_nop(), ctx);

	/* arg1: &args_off */
	emit_addi(RV_REG_A0, RV_REG_FP, -args_off, ctx);
	if (!p->jited)
		/* arg2: progs[i]->insnsi for interpreter */
		emit_imm(RV_REG_A1, (const s64)p->insnsi, ctx);
	ret = emit_call((const u64)p->bpf_func, true, ctx);
	if (ret)
		return ret;

	if (save_ret) {
		emit_sd(RV_REG_FP, -retval_off, RV_REG_A0, ctx);
		emit_sd(RV_REG_FP, -(retval_off - 8), regmap[BPF_REG_0], ctx);
	}

	/* update branch with beqz */
	if (ctx->insns) {
		int offset = ninsns_rvoff(ctx->ninsns - branch_off);
		u32 insn = rv_beq(RV_REG_A0, RV_REG_ZERO, offset >> 1);
		*(u32 *)(ctx->insns + branch_off) = insn;
	}

	/* arg1: prog */
	emit_imm(RV_REG_A0, (const s64)p, ctx);
	/* arg2: prog start time */
	emit_mv(RV_REG_A1, RV_REG_S1, ctx);
	/* arg3: &run_ctx */
	emit_addi(RV_REG_A2, RV_REG_FP, -run_ctx_off, ctx);
	ret = emit_call((const u64)bpf_trampoline_exit(p), true, ctx);

	return ret;
}

static int __arch_prepare_bpf_trampoline(struct bpf_tramp_image *im,
					 const struct btf_func_model *m,
					 struct bpf_tramp_links *tlinks,
					 void *func_addr, u32 flags,
					 struct rv_jit_context *ctx)
{
	int i, ret, offset;
	int *branches_off = NULL;
	int stack_size = 0, nr_arg_slots = 0;
	int retval_off, args_off, nregs_off, ip_off, run_ctx_off, sreg_off, stk_arg_off;
	struct bpf_tramp_links *fentry = &tlinks[BPF_TRAMP_FENTRY];
	struct bpf_tramp_links *fexit = &tlinks[BPF_TRAMP_FEXIT];
	struct bpf_tramp_links *fmod_ret = &tlinks[BPF_TRAMP_MODIFY_RETURN];
	bool is_struct_ops = flags & BPF_TRAMP_F_INDIRECT;
	void *orig_call = func_addr;
	bool save_ret;
	u32 insn;

	/* Two types of generated trampoline stack layout:
	 *
	 * 1. trampoline called from function entry
	 * --------------------------------------
	 * FP + 8	    [ RA to parent func	] return address to parent
	 *					  function
	 * FP + 0	    [ FP of parent func ] frame pointer of parent
	 *					  function
	 * FP - 8           [ T0 to traced func ] return address of traced
	 *					  function
	 * FP - 16	    [ FP of traced func ] frame pointer of traced
	 *					  function
	 * --------------------------------------
	 *
	 * 2. trampoline called directly
	 * --------------------------------------
	 * FP - 8	    [ RA to caller func ] return address to caller
	 *					  function
	 * FP - 16	    [ FP of caller func	] frame pointer of caller
	 *					  function
	 * --------------------------------------
	 *
	 * FP - retval_off  [ return value      ] BPF_TRAMP_F_CALL_ORIG or
	 *					  BPF_TRAMP_F_RET_FENTRY_RET
	 *                  [ argN              ]
	 *                  [ ...               ]
	 * FP - args_off    [ arg1              ]
	 *
	 * FP - nregs_off   [ regs count        ]
	 *
	 * FP - ip_off      [ traced func	] BPF_TRAMP_F_IP_ARG
	 *
	 * FP - run_ctx_off [ bpf_tramp_run_ctx ]
	 *
	 * FP - sreg_off    [ callee saved reg	]
	 *
	 *		    [ pads              ] pads for 16 bytes alignment
	 *
	 *		    [ stack_argN        ]
	 *		    [ ...               ]
	 * FP - stk_arg_off [ stack_arg1        ] BPF_TRAMP_F_CALL_ORIG
	 */

	if (flags & (BPF_TRAMP_F_ORIG_STACK | BPF_TRAMP_F_SHARE_IPMODIFY))
		return -ENOTSUPP;

	if (m->nr_args > MAX_BPF_FUNC_ARGS)
		return -ENOTSUPP;

	for (i = 0; i < m->nr_args; i++)
		nr_arg_slots += round_up(m->arg_size[i], 8) / 8;

	/* room of trampoline frame to store return address and frame pointer */
	stack_size += 16;

	save_ret = flags & (BPF_TRAMP_F_CALL_ORIG | BPF_TRAMP_F_RET_FENTRY_RET);
	if (save_ret) {
		stack_size += 16; /* Save both A5 (BPF R0) and A0 */
		retval_off = stack_size;
	}

	stack_size += nr_arg_slots * 8;
	args_off = stack_size;

	stack_size += 8;
	nregs_off = stack_size;

	if (flags & BPF_TRAMP_F_IP_ARG) {
		stack_size += 8;
		ip_off = stack_size;
	}

	stack_size += round_up(sizeof(struct bpf_tramp_run_ctx), 8);
	run_ctx_off = stack_size;

	stack_size += 8;
	sreg_off = stack_size;

	if ((flags & BPF_TRAMP_F_CALL_ORIG) && (nr_arg_slots - RV_MAX_REG_ARGS > 0))
		stack_size += (nr_arg_slots - RV_MAX_REG_ARGS) * 8;

	stack_size = round_up(stack_size, STACK_ALIGN);

	/* room for args on stack must be at the top of stack */
	stk_arg_off = stack_size;

	if (!is_struct_ops) {
		/* For the trampoline called from function entry,
		 * the frame of traced function and the frame of
		 * trampoline need to be considered.
		 */
		emit_addi(RV_REG_SP, RV_REG_SP, -16, ctx);
		emit_sd(RV_REG_SP, 8, RV_REG_RA, ctx);
		emit_sd(RV_REG_SP, 0, RV_REG_FP, ctx);
		emit_addi(RV_REG_FP, RV_REG_SP, 16, ctx);

		emit_addi(RV_REG_SP, RV_REG_SP, -stack_size, ctx);
		emit_sd(RV_REG_SP, stack_size - 8, RV_REG_T0, ctx);
		emit_sd(RV_REG_SP, stack_size - 16, RV_REG_FP, ctx);
		emit_addi(RV_REG_FP, RV_REG_SP, stack_size, ctx);
	} else {
		/* emit kcfi hash */
		emit_kcfi(cfi_get_func_hash(func_addr), ctx);
		/* For the trampoline called directly, just handle
		 * the frame of trampoline.
		 */
		emit_addi(RV_REG_SP, RV_REG_SP, -stack_size, ctx);
		emit_sd(RV_REG_SP, stack_size - 8, RV_REG_RA, ctx);
		emit_sd(RV_REG_SP, stack_size - 16, RV_REG_FP, ctx);
		emit_addi(RV_REG_FP, RV_REG_SP, stack_size, ctx);
	}

	/* callee saved register S1 to pass start time */
	emit_sd(RV_REG_FP, -sreg_off, RV_REG_S1, ctx);

	/* store ip address of the traced function */
	if (flags & BPF_TRAMP_F_IP_ARG) {
		emit_imm(RV_REG_T1, (const s64)func_addr, ctx);
		emit_sd(RV_REG_FP, -ip_off, RV_REG_T1, ctx);
	}

	emit_li(RV_REG_T1, nr_arg_slots, ctx);
	emit_sd(RV_REG_FP, -nregs_off, RV_REG_T1, ctx);

	store_args(nr_arg_slots, args_off, ctx);

	/* skip to actual body of traced function */
	if (flags & BPF_TRAMP_F_SKIP_FRAME)
		orig_call += RV_FENTRY_NINSNS * 4;

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		emit_imm(RV_REG_A0, ctx->insns ? (const s64)im : RV_MAX_COUNT_IMM, ctx);
		ret = emit_call((const u64)__bpf_tramp_enter, true, ctx);
		if (ret)
			return ret;
	}

	for (i = 0; i < fentry->nr_links; i++) {
		ret = invoke_bpf_prog(fentry->links[i], args_off, retval_off, run_ctx_off,
				      flags & BPF_TRAMP_F_RET_FENTRY_RET, ctx);
		if (ret)
			return ret;
	}

	if (fmod_ret->nr_links) {
		branches_off = kcalloc(fmod_ret->nr_links, sizeof(int), GFP_KERNEL);
		if (!branches_off)
			return -ENOMEM;

		/* cleanup to avoid garbage return value confusion */
		emit_sd(RV_REG_FP, -retval_off, RV_REG_ZERO, ctx);
		for (i = 0; i < fmod_ret->nr_links; i++) {
			ret = invoke_bpf_prog(fmod_ret->links[i], args_off, retval_off,
					      run_ctx_off, true, ctx);
			if (ret)
				goto out;
			emit_ld(RV_REG_T1, -retval_off, RV_REG_FP, ctx);
			branches_off[i] = ctx->ninsns;
			/* nop reserved for conditional jump */
			emit(rv_nop(), ctx);
		}
	}

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		restore_args(min_t(int, nr_arg_slots, RV_MAX_REG_ARGS), args_off, ctx);
		restore_stack_args(nr_arg_slots - RV_MAX_REG_ARGS, args_off, stk_arg_off, ctx);
		ret = emit_call((const u64)orig_call, true, ctx);
		if (ret)
			goto out;
		emit_sd(RV_REG_FP, -retval_off, RV_REG_A0, ctx);
		emit_sd(RV_REG_FP, -(retval_off - 8), regmap[BPF_REG_0], ctx);
		im->ip_after_call = ctx->ro_insns + ctx->ninsns;
		/* 2 nops reserved for auipc+jalr pair */
		emit(rv_nop(), ctx);
		emit(rv_nop(), ctx);
	}

	/* update branches saved in invoke_bpf_mod_ret with bnez */
	for (i = 0; ctx->insns && i < fmod_ret->nr_links; i++) {
		offset = ninsns_rvoff(ctx->ninsns - branches_off[i]);
		insn = rv_bne(RV_REG_T1, RV_REG_ZERO, offset >> 1);
		*(u32 *)(ctx->insns + branches_off[i]) = insn;
	}

	for (i = 0; i < fexit->nr_links; i++) {
		ret = invoke_bpf_prog(fexit->links[i], args_off, retval_off,
				      run_ctx_off, false, ctx);
		if (ret)
			goto out;
	}

	if (flags & BPF_TRAMP_F_CALL_ORIG) {
		im->ip_epilogue = ctx->ro_insns + ctx->ninsns;
		emit_imm(RV_REG_A0, ctx->insns ? (const s64)im : RV_MAX_COUNT_IMM, ctx);
		ret = emit_call((const u64)__bpf_tramp_exit, true, ctx);
		if (ret)
			goto out;
	}

	if (flags & BPF_TRAMP_F_RESTORE_REGS)
		restore_args(min_t(int, nr_arg_slots, RV_MAX_REG_ARGS), args_off, ctx);

	if (save_ret) {
		emit_ld(RV_REG_A0, -retval_off, RV_REG_FP, ctx);
		emit_ld(regmap[BPF_REG_0], -(retval_off - 8), RV_REG_FP, ctx);
	}

	emit_ld(RV_REG_S1, -sreg_off, RV_REG_FP, ctx);

	if (!is_struct_ops) {
		/* trampoline called from function entry */
		emit_ld(RV_REG_T0, stack_size - 8, RV_REG_SP, ctx);
		emit_ld(RV_REG_FP, stack_size - 16, RV_REG_SP, ctx);
		emit_addi(RV_REG_SP, RV_REG_SP, stack_size, ctx);

		emit_ld(RV_REG_RA, 8, RV_REG_SP, ctx);
		emit_ld(RV_REG_FP, 0, RV_REG_SP, ctx);
		emit_addi(RV_REG_SP, RV_REG_SP, 16, ctx);

		if (flags & BPF_TRAMP_F_SKIP_FRAME)
			/* return to parent function */
			emit_jalr(RV_REG_ZERO, RV_REG_RA, 0, ctx);
		else
			/* return to traced function */
			emit_jalr(RV_REG_ZERO, RV_REG_T0, 0, ctx);
	} else {
		/* trampoline called directly */
		emit_ld(RV_REG_RA, stack_size - 8, RV_REG_SP, ctx);
		emit_ld(RV_REG_FP, stack_size - 16, RV_REG_SP, ctx);
		emit_addi(RV_REG_SP, RV_REG_SP, stack_size, ctx);

		emit_jalr(RV_REG_ZERO, RV_REG_RA, 0, ctx);
	}

	ret = ctx->ninsns;
out:
	kfree(branches_off);
	return ret;
}

int arch_bpf_trampoline_size(const struct btf_func_model *m, u32 flags,
			     struct bpf_tramp_links *tlinks, void *func_addr)
{
	struct bpf_tramp_image im;
	struct rv_jit_context ctx;
	int ret;

	ctx.ninsns = 0;
	ctx.insns = NULL;
	ctx.ro_insns = NULL;
	ret = __arch_prepare_bpf_trampoline(&im, m, tlinks, func_addr, flags, &ctx);

	return ret < 0 ? ret : ninsns_rvoff(ctx.ninsns);
}

void *arch_alloc_bpf_trampoline(unsigned int size)
{
	return bpf_prog_pack_alloc(size, bpf_fill_ill_insns);
}

void arch_free_bpf_trampoline(void *image, unsigned int size)
{
	bpf_prog_pack_free(image, size);
}

int arch_prepare_bpf_trampoline(struct bpf_tramp_image *im, void *ro_image,
				void *ro_image_end, const struct btf_func_model *m,
				u32 flags, struct bpf_tramp_links *tlinks,
				void *func_addr)
{
	int ret;
	void *image, *res;
	struct rv_jit_context ctx;
	u32 size = ro_image_end - ro_image;

	image = kvmalloc(size, GFP_KERNEL);
	if (!image)
		return -ENOMEM;

	ctx.ninsns = 0;
	ctx.insns = image;
	ctx.ro_insns = ro_image;
	ret = __arch_prepare_bpf_trampoline(im, m, tlinks, func_addr, flags, &ctx);
	if (ret < 0)
		goto out;

	if (WARN_ON(size < ninsns_rvoff(ctx.ninsns))) {
		ret = -E2BIG;
		goto out;
	}

	res = bpf_arch_text_copy(ro_image, image, size);
	if (IS_ERR(res)) {
		ret = PTR_ERR(res);
		goto out;
	}

	bpf_flush_icache(ro_image, ro_image_end);
out:
	kvfree(image);
	return ret < 0 ? ret : size;
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
		if (insn_is_cast_user(insn)) {
			emit_mv(RV_REG_T1, rs, ctx);
			emit_zextw(RV_REG_T1, RV_REG_T1, ctx);
			emit_imm(rd, (ctx->user_vm_start >> 32) << 32, ctx);
			emit(rv_beq(RV_REG_T1, RV_REG_ZERO, 4), ctx);
			emit_or(RV_REG_T1, rd, RV_REG_T1, ctx);
			emit_mv(rd, RV_REG_T1, ctx);
			break;
		} else if (insn_is_mov_percpu_addr(insn)) {
			if (rd != rs)
				emit_mv(rd, rs, ctx);
#ifdef CONFIG_SMP
			/* Load current CPU number in T1 */
			emit_ld(RV_REG_T1, offsetof(struct thread_info, cpu),
				RV_REG_TP, ctx);
			/* Load address of __per_cpu_offset array in T2 */
			emit_addr(RV_REG_T2, (u64)&__per_cpu_offset, extra_pass, ctx);
			/* Get address of __per_cpu_offset[cpu] in T1 */
			emit_sh3add(RV_REG_T1, RV_REG_T1, RV_REG_T2, ctx);
			/* Load __per_cpu_offset[cpu] in T1 */
			emit_ld(RV_REG_T1, 0, RV_REG_T1, ctx);
			/* Add the offset to Rd */
			emit_add(rd, rd, RV_REG_T1, ctx);
#endif
		}
		if (imm == 1) {
			/* Special mov32 for zext */
			emit_zextw(rd, rd, ctx);
			break;
		}
		switch (insn->off) {
		case 0:
			emit_mv(rd, rs, ctx);
			break;
		case 8:
			emit_sextb(rd, rs, ctx);
			break;
		case 16:
			emit_sexth(rd, rs, ctx);
			break;
		case 32:
			emit_sextw(rd, rs, ctx);
			break;
		}
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;

	/* dst = dst OP src */
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_X:
		emit_add(rd, rd, rs, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
		if (is64)
			emit_sub(rd, rd, rs, ctx);
		else
			emit_subw(rd, rd, rs, ctx);

		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_AND | BPF_X:
		emit_and(rd, rd, rs, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
		emit_or(rd, rd, rs, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
		emit_xor(rd, rd, rs, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_X:
	case BPF_ALU64 | BPF_MUL | BPF_X:
		emit(is64 ? rv_mul(rd, rd, rs) : rv_mulw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_DIV | BPF_X:
		if (off)
			emit(is64 ? rv_div(rd, rd, rs) : rv_divw(rd, rd, rs), ctx);
		else
			emit(is64 ? rv_divu(rd, rd, rs) : rv_divuw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
		if (off)
			emit(is64 ? rv_rem(rd, rd, rs) : rv_remw(rd, rd, rs), ctx);
		else
			emit(is64 ? rv_remu(rd, rd, rs) : rv_remuw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_LSH | BPF_X:
	case BPF_ALU64 | BPF_LSH | BPF_X:
		emit(is64 ? rv_sll(rd, rd, rs) : rv_sllw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_X:
	case BPF_ALU64 | BPF_RSH | BPF_X:
		emit(is64 ? rv_srl(rd, rd, rs) : rv_srlw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_X:
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		emit(is64 ? rv_sra(rd, rd, rs) : rv_sraw(rd, rd, rs), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;

	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
	case BPF_ALU64 | BPF_NEG:
		emit_sub(rd, RV_REG_ZERO, rd, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;

	/* dst = BSWAP##imm(dst) */
	case BPF_ALU | BPF_END | BPF_FROM_LE:
		switch (imm) {
		case 16:
			emit_zexth(rd, rd, ctx);
			break;
		case 32:
			if (!aux->verifier_zext)
				emit_zextw(rd, rd, ctx);
			break;
		case 64:
			/* Do nothing */
			break;
		}
		break;
	case BPF_ALU | BPF_END | BPF_FROM_BE:
	case BPF_ALU64 | BPF_END | BPF_FROM_LE:
		emit_bswap(rd, imm, ctx);
		break;

	/* dst = imm */
	case BPF_ALU | BPF_MOV | BPF_K:
	case BPF_ALU64 | BPF_MOV | BPF_K:
		emit_imm(rd, imm, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
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
			emit_zextw(rd, rd, ctx);
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
			emit_zextw(rd, rd, ctx);
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
			emit_zextw(rd, rd, ctx);
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
			emit_zextw(rd, rd, ctx);
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
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU64 | BPF_MUL | BPF_K:
		emit_imm(RV_REG_T1, imm, ctx);
		emit(is64 ? rv_mul(rd, rd, RV_REG_T1) :
		     rv_mulw(rd, rd, RV_REG_T1), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_DIV | BPF_K:
		emit_imm(RV_REG_T1, imm, ctx);
		if (off)
			emit(is64 ? rv_div(rd, rd, RV_REG_T1) :
			     rv_divw(rd, rd, RV_REG_T1), ctx);
		else
			emit(is64 ? rv_divu(rd, rd, RV_REG_T1) :
			     rv_divuw(rd, rd, RV_REG_T1), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		emit_imm(RV_REG_T1, imm, ctx);
		if (off)
			emit(is64 ? rv_rem(rd, rd, RV_REG_T1) :
			     rv_remw(rd, rd, RV_REG_T1), ctx);
		else
			emit(is64 ? rv_remu(rd, rd, RV_REG_T1) :
			     rv_remuw(rd, rd, RV_REG_T1), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_LSH | BPF_K:
	case BPF_ALU64 | BPF_LSH | BPF_K:
		emit_slli(rd, rd, imm, ctx);

		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_K:
	case BPF_ALU64 | BPF_RSH | BPF_K:
		if (is64)
			emit_srli(rd, rd, imm, ctx);
		else
			emit(rv_srliw(rd, rd, imm), ctx);

		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_K:
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		if (is64)
			emit_srai(rd, rd, imm, ctx);
		else
			emit(rv_sraiw(rd, rd, imm), ctx);

		if (!is64 && !aux->verifier_zext)
			emit_zextw(rd, rd, ctx);
		break;

	/* JUMP off */
	case BPF_JMP | BPF_JA:
	case BPF_JMP32 | BPF_JA:
		if (BPF_CLASS(code) == BPF_JMP)
			rvoff = rv_offset(i, off, ctx);
		else
			rvoff = rv_offset(i, imm, ctx);
		ret = emit_jump_and_link(RV_REG_ZERO, rvoff, true, ctx);
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
			if (is_signed_bpf_cond(BPF_OP(code))) {
				emit_sextw_alt(&rs, RV_REG_T1, ctx);
				emit_sextw_alt(&rd, RV_REG_T2, ctx);
			} else {
				emit_zextw_alt(&rs, RV_REG_T1, ctx);
				emit_zextw_alt(&rd, RV_REG_T2, ctx);
			}
			e = ctx->ninsns;

			/* Adjust for extra insns */
			rvoff -= ninsns_rvoff(e - s);
		}

		if (BPF_OP(code) == BPF_JSET) {
			/* Adjust for and */
			rvoff -= 4;
			emit_and(RV_REG_T1, rd, rs, ctx);
			emit_branch(BPF_JNE, RV_REG_T1, RV_REG_ZERO, rvoff, ctx);
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
		if (imm)
			emit_imm(RV_REG_T1, imm, ctx);
		rs = imm ? RV_REG_T1 : RV_REG_ZERO;
		if (!is64) {
			if (is_signed_bpf_cond(BPF_OP(code))) {
				emit_sextw_alt(&rd, RV_REG_T2, ctx);
				/* rs has been sign extended */
			} else {
				emit_zextw_alt(&rd, RV_REG_T2, ctx);
				if (imm)
					emit_zextw(rs, rs, ctx);
			}
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
			emit_sextw(RV_REG_T1, RV_REG_T1, ctx);
		e = ctx->ninsns;
		rvoff -= ninsns_rvoff(e - s);
		emit_branch(BPF_JNE, RV_REG_T1, RV_REG_ZERO, rvoff, ctx);
		break;

	/* function call */
	case BPF_JMP | BPF_CALL:
	{
		bool fixed_addr;
		u64 addr;

		/* Inline calls to bpf_get_smp_processor_id()
		 *
		 * RV_REG_TP holds the address of the current CPU's task_struct and thread_info is
		 * at offset 0 in task_struct.
		 * Load cpu from thread_info:
		 *     Set R0 to ((struct thread_info *)(RV_REG_TP))->cpu
		 *
		 * This replicates the implementation of raw_smp_processor_id() on RISCV
		 */
		if (insn->src_reg == 0 && insn->imm == BPF_FUNC_get_smp_processor_id) {
			/* Load current CPU number in R0 */
			emit_ld(bpf_to_rv_reg(BPF_REG_0, ctx), offsetof(struct thread_info, cpu),
				RV_REG_TP, ctx);
			break;
		}

		mark_call(ctx);
		ret = bpf_jit_get_func_addr(ctx->prog, insn, extra_pass,
					    &addr, &fixed_addr);
		if (ret < 0)
			return ret;

		if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL) {
			const struct btf_func_model *fm;
			int idx;

			fm = bpf_jit_find_kfunc_model(ctx->prog, insn);
			if (!fm)
				return -EINVAL;

			for (idx = 0; idx < fm->nr_args; idx++) {
				u8 reg = bpf_to_rv_reg(BPF_REG_1 + idx, ctx);

				if (fm->arg_size[idx] == sizeof(int))
					emit_sextw(reg, reg, ctx);
			}
		}

		ret = emit_call(addr, fixed_addr, ctx);
		if (ret)
			return ret;

		if (insn->src_reg != BPF_PSEUDO_CALL)
			emit_mv(bpf_to_rv_reg(BPF_REG_0, ctx), RV_REG_A0, ctx);
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
		ret = emit_jump_and_link(RV_REG_ZERO, rvoff, true, ctx);
		if (ret)
			return ret;
		break;

	/* dst = imm64 */
	case BPF_LD | BPF_IMM | BPF_DW:
	{
		struct bpf_insn insn1 = insn[1];
		u64 imm64;

		imm64 = (u64)insn1.imm << 32 | (u32)imm;
		if (bpf_pseudo_func(insn)) {
			/* fixed-length insns for extra jit pass */
			ret = emit_addr(rd, imm64, extra_pass, ctx);
			if (ret)
				return ret;
		} else {
			emit_imm(rd, imm64, ctx);
		}

		return 1;
	}

	/* LDX: dst = *(unsigned size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_DW:
	case BPF_LDX | BPF_PROBE_MEM | BPF_B:
	case BPF_LDX | BPF_PROBE_MEM | BPF_H:
	case BPF_LDX | BPF_PROBE_MEM | BPF_W:
	case BPF_LDX | BPF_PROBE_MEM | BPF_DW:
	/* LDSX: dst = *(signed size *)(src + off) */
	case BPF_LDX | BPF_MEMSX | BPF_B:
	case BPF_LDX | BPF_MEMSX | BPF_H:
	case BPF_LDX | BPF_MEMSX | BPF_W:
	case BPF_LDX | BPF_PROBE_MEMSX | BPF_B:
	case BPF_LDX | BPF_PROBE_MEMSX | BPF_H:
	case BPF_LDX | BPF_PROBE_MEMSX | BPF_W:
	/* LDX | PROBE_MEM32: dst = *(unsigned size *)(src + RV_REG_ARENA + off) */
	case BPF_LDX | BPF_PROBE_MEM32 | BPF_B:
	case BPF_LDX | BPF_PROBE_MEM32 | BPF_H:
	case BPF_LDX | BPF_PROBE_MEM32 | BPF_W:
	case BPF_LDX | BPF_PROBE_MEM32 | BPF_DW:
	{
		int insn_len, insns_start;
		bool sign_ext;

		sign_ext = BPF_MODE(insn->code) == BPF_MEMSX ||
			   BPF_MODE(insn->code) == BPF_PROBE_MEMSX;

		if (BPF_MODE(insn->code) == BPF_PROBE_MEM32) {
			emit_add(RV_REG_T2, rs, RV_REG_ARENA, ctx);
			rs = RV_REG_T2;
		}

		switch (BPF_SIZE(code)) {
		case BPF_B:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				if (sign_ext)
					emit(rv_lb(rd, off, rs), ctx);
				else
					emit(rv_lbu(rd, off, rs), ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T1, off, ctx);
			emit_add(RV_REG_T1, RV_REG_T1, rs, ctx);
			insns_start = ctx->ninsns;
			if (sign_ext)
				emit(rv_lb(rd, 0, RV_REG_T1), ctx);
			else
				emit(rv_lbu(rd, 0, RV_REG_T1), ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		case BPF_H:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				if (sign_ext)
					emit(rv_lh(rd, off, rs), ctx);
				else
					emit(rv_lhu(rd, off, rs), ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T1, off, ctx);
			emit_add(RV_REG_T1, RV_REG_T1, rs, ctx);
			insns_start = ctx->ninsns;
			if (sign_ext)
				emit(rv_lh(rd, 0, RV_REG_T1), ctx);
			else
				emit(rv_lhu(rd, 0, RV_REG_T1), ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		case BPF_W:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				if (sign_ext)
					emit(rv_lw(rd, off, rs), ctx);
				else
					emit(rv_lwu(rd, off, rs), ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T1, off, ctx);
			emit_add(RV_REG_T1, RV_REG_T1, rs, ctx);
			insns_start = ctx->ninsns;
			if (sign_ext)
				emit(rv_lw(rd, 0, RV_REG_T1), ctx);
			else
				emit(rv_lwu(rd, 0, RV_REG_T1), ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		case BPF_DW:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				emit_ld(rd, off, rs, ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T1, off, ctx);
			emit_add(RV_REG_T1, RV_REG_T1, rs, ctx);
			insns_start = ctx->ninsns;
			emit_ld(rd, 0, RV_REG_T1, ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		}

		ret = add_exception_handler(insn, ctx, rd, insn_len);
		if (ret)
			return ret;

		if (BPF_SIZE(code) != BPF_DW && insn_is_zext(&insn[1]))
			return 1;
		break;
	}
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

	case BPF_ST | BPF_PROBE_MEM32 | BPF_B:
	case BPF_ST | BPF_PROBE_MEM32 | BPF_H:
	case BPF_ST | BPF_PROBE_MEM32 | BPF_W:
	case BPF_ST | BPF_PROBE_MEM32 | BPF_DW:
	{
		int insn_len, insns_start;

		emit_add(RV_REG_T3, rd, RV_REG_ARENA, ctx);
		rd = RV_REG_T3;

		/* Load imm to a register then store it */
		emit_imm(RV_REG_T1, imm, ctx);

		switch (BPF_SIZE(code)) {
		case BPF_B:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				emit(rv_sb(rd, off, RV_REG_T1), ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T2, off, ctx);
			emit_add(RV_REG_T2, RV_REG_T2, rd, ctx);
			insns_start = ctx->ninsns;
			emit(rv_sb(RV_REG_T2, 0, RV_REG_T1), ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		case BPF_H:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				emit(rv_sh(rd, off, RV_REG_T1), ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T2, off, ctx);
			emit_add(RV_REG_T2, RV_REG_T2, rd, ctx);
			insns_start = ctx->ninsns;
			emit(rv_sh(RV_REG_T2, 0, RV_REG_T1), ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		case BPF_W:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				emit_sw(rd, off, RV_REG_T1, ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T2, off, ctx);
			emit_add(RV_REG_T2, RV_REG_T2, rd, ctx);
			insns_start = ctx->ninsns;
			emit_sw(RV_REG_T2, 0, RV_REG_T1, ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		case BPF_DW:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				emit_sd(rd, off, RV_REG_T1, ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T2, off, ctx);
			emit_add(RV_REG_T2, RV_REG_T2, rd, ctx);
			insns_start = ctx->ninsns;
			emit_sd(RV_REG_T2, 0, RV_REG_T1, ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		}

		ret = add_exception_handler(insn, ctx, REG_DONT_CLEAR_MARKER,
					    insn_len);
		if (ret)
			return ret;

		break;
	}

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
		emit_atomic(rd, rs, off, imm,
			    BPF_SIZE(code) == BPF_DW, ctx);
		break;

	case BPF_STX | BPF_PROBE_MEM32 | BPF_B:
	case BPF_STX | BPF_PROBE_MEM32 | BPF_H:
	case BPF_STX | BPF_PROBE_MEM32 | BPF_W:
	case BPF_STX | BPF_PROBE_MEM32 | BPF_DW:
	{
		int insn_len, insns_start;

		emit_add(RV_REG_T2, rd, RV_REG_ARENA, ctx);
		rd = RV_REG_T2;

		switch (BPF_SIZE(code)) {
		case BPF_B:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				emit(rv_sb(rd, off, rs), ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T1, off, ctx);
			emit_add(RV_REG_T1, RV_REG_T1, rd, ctx);
			insns_start = ctx->ninsns;
			emit(rv_sb(RV_REG_T1, 0, rs), ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		case BPF_H:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				emit(rv_sh(rd, off, rs), ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T1, off, ctx);
			emit_add(RV_REG_T1, RV_REG_T1, rd, ctx);
			insns_start = ctx->ninsns;
			emit(rv_sh(RV_REG_T1, 0, rs), ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		case BPF_W:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				emit_sw(rd, off, rs, ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T1, off, ctx);
			emit_add(RV_REG_T1, RV_REG_T1, rd, ctx);
			insns_start = ctx->ninsns;
			emit_sw(RV_REG_T1, 0, rs, ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		case BPF_DW:
			if (is_12b_int(off)) {
				insns_start = ctx->ninsns;
				emit_sd(rd, off, rs, ctx);
				insn_len = ctx->ninsns - insns_start;
				break;
			}

			emit_imm(RV_REG_T1, off, ctx);
			emit_add(RV_REG_T1, RV_REG_T1, rd, ctx);
			insns_start = ctx->ninsns;
			emit_sd(RV_REG_T1, 0, rs, ctx);
			insn_len = ctx->ninsns - insns_start;
			break;
		}

		ret = add_exception_handler(insn, ctx, REG_DONT_CLEAR_MARKER,
					    insn_len);
		if (ret)
			return ret;

		break;
	}

	default:
		pr_err("bpf-jit: unknown opcode %02x\n", code);
		return -EINVAL;
	}

	return 0;
}

void bpf_jit_build_prologue(struct rv_jit_context *ctx, bool is_subprog)
{
	int i, stack_adjust = 0, store_offset, bpf_stack_adjust;

	bpf_stack_adjust = round_up(ctx->prog->aux->stack_depth, STACK_ALIGN);
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
	if (ctx->arena_vm_start)
		stack_adjust += 8;

	stack_adjust = round_up(stack_adjust, STACK_ALIGN);
	stack_adjust += bpf_stack_adjust;

	store_offset = stack_adjust - 8;

	/* emit kcfi type preamble immediately before the  first insn */
	emit_kcfi(is_subprog ? cfi_bpf_subprog_hash : cfi_bpf_hash, ctx);

	/* nops reserved for auipc+jalr pair */
	for (i = 0; i < RV_FENTRY_NINSNS; i++)
		emit(rv_nop(), ctx);

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
	if (ctx->arena_vm_start) {
		emit_sd(RV_REG_SP, store_offset, RV_REG_ARENA, ctx);
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

	if (ctx->arena_vm_start)
		emit_imm(RV_REG_ARENA, ctx->arena_vm_start, ctx);
}

void bpf_jit_build_epilogue(struct rv_jit_context *ctx)
{
	__build_epilogue(false, ctx);
}

bool bpf_jit_supports_kfunc_call(void)
{
	return true;
}

bool bpf_jit_supports_ptr_xchg(void)
{
	return true;
}

bool bpf_jit_supports_arena(void)
{
	return true;
}

bool bpf_jit_supports_percpu_insn(void)
{
	return true;
}

bool bpf_jit_inlines_helper_call(s32 imm)
{
	switch (imm) {
	case BPF_FUNC_get_smp_processor_id:
		return true;
	default:
		return false;
	}
}
