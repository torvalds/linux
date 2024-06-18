// SPDX-License-Identifier: GPL-2.0
/*
 * BPF JIT compiler for PA-RISC (64-bit)
 *
 * Copyright(c) 2023 Helge Deller <deller@gmx.de>
 *
 * The code is based on the BPF JIT compiler for RV64 by Björn Töpel.
 *
 * TODO:
 * - check if bpf_jit_needs_zext() is needed (currently enabled)
 * - implement arch_prepare_bpf_trampoline(), poke(), ...
 */

#include <linux/bitfield.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/libgcc.h>
#include "bpf_jit.h"

static const int regmap[] = {
	[BPF_REG_0] =	HPPA_REG_RET0,
	[BPF_REG_1] =	HPPA_R(5),
	[BPF_REG_2] =	HPPA_R(6),
	[BPF_REG_3] =	HPPA_R(7),
	[BPF_REG_4] =	HPPA_R(8),
	[BPF_REG_5] =	HPPA_R(9),
	[BPF_REG_6] =	HPPA_R(10),
	[BPF_REG_7] =	HPPA_R(11),
	[BPF_REG_8] =	HPPA_R(12),
	[BPF_REG_9] =	HPPA_R(13),
	[BPF_REG_FP] =	HPPA_R(14),
	[BPF_REG_AX] =	HPPA_R(15),
};

/*
 * Stack layout during BPF program execution (note: stack grows up):
 *
 *                     high
 *   HPPA64 sp =>  +----------+ <= HPPA64 fp
 *                 | saved sp |
 *                 | saved rp |
 *                 |   ...    | HPPA64 callee-saved registers
 *                 | curr args|
 *                 | local var|
 *                 +----------+ <= (BPF FP)
 *                 |          |
 *                 |   ...    | BPF program stack
 *                 |          |
 *                 |   ...    | Function call stack
 *                 |          |
 *                 +----------+
 *                     low
 */

/* Offset from fp for BPF registers stored on stack. */
#define STACK_ALIGN	FRAME_SIZE

#define EXIT_PTR_LOAD(reg)	hppa64_ldd_im16(-FRAME_SIZE, HPPA_REG_SP, reg)
#define EXIT_PTR_STORE(reg)	hppa64_std_im16(reg, -FRAME_SIZE, HPPA_REG_SP)
#define EXIT_PTR_JUMP(reg, nop)	hppa_bv(HPPA_REG_ZERO, reg, nop)

static u8 bpf_to_hppa_reg(int bpf_reg, struct hppa_jit_context *ctx)
{
	u8 reg = regmap[bpf_reg];

	REG_SET_SEEN(ctx, reg);
	return reg;
};

static void emit_hppa_copy(const s8 rs, const s8 rd, struct hppa_jit_context *ctx)
{
	REG_SET_SEEN(ctx, rd);
	if (OPTIMIZE_HPPA && (rs == rd))
		return;
	REG_SET_SEEN(ctx, rs);
	emit(hppa_copy(rs, rd), ctx);
}

static void emit_hppa64_depd(u8 src, u8 pos, u8 len, u8 target, bool no_zero, struct hppa_jit_context *ctx)
{
	int c;

	pos &= (BITS_PER_LONG - 1);
	pos = 63 - pos;
	len = 64 - len;
	c =  (len < 32)  ? 0x4 : 0;
	c |= (pos >= 32) ? 0x2 : 0;
	c |= (no_zero)   ? 0x1 : 0;
	emit(hppa_t10_insn(0x3c, target, src, 0, c, pos & 0x1f, len & 0x1f), ctx);
}

static void emit_hppa64_shld(u8 src, int num, u8 target, struct hppa_jit_context *ctx)
{
	emit_hppa64_depd(src, 63-num, 64-num, target, 0, ctx);
}

static void emit_hppa64_extrd(u8 src, u8 pos, u8 len, u8 target, bool signed_op, struct hppa_jit_context *ctx)
{
	int c;

	pos &= (BITS_PER_LONG - 1);
	len = 64 - len;
	c =  (len <  32) ? 0x4 : 0;
	c |= (pos >= 32) ? 0x2 : 0;
	c |= signed_op   ? 0x1 : 0;
	emit(hppa_t10_insn(0x36, src, target, 0, c, pos & 0x1f, len & 0x1f), ctx);
}

static void emit_hppa64_extrw(u8 src, u8 pos, u8 len, u8 target, bool signed_op, struct hppa_jit_context *ctx)
{
	int c;

	pos &= (32 - 1);
	len = 32 - len;
	c = 0x06 | (signed_op ? 1 : 0);
	emit(hppa_t10_insn(0x34, src, target, 0, c, pos, len), ctx);
}

#define emit_hppa64_zext32(r, target, ctx) \
	emit_hppa64_extrd(r, 63, 32, target, false, ctx)
#define emit_hppa64_sext32(r, target, ctx) \
	emit_hppa64_extrd(r, 63, 32, target, true, ctx)

static void emit_hppa64_shrd(u8 src, int num, u8 target, bool signed_op, struct hppa_jit_context *ctx)
{
	emit_hppa64_extrd(src, 63-num, 64-num, target, signed_op, ctx);
}

static void emit_hppa64_shrw(u8 src, int num, u8 target, bool signed_op, struct hppa_jit_context *ctx)
{
	emit_hppa64_extrw(src, 31-num, 32-num, target, signed_op, ctx);
}

/* Emit variable-length instructions for 32-bit imm */
static void emit_imm32(u8 rd, s32 imm, struct hppa_jit_context *ctx)
{
	u32 lower = im11(imm);

	REG_SET_SEEN(ctx, rd);
	if (OPTIMIZE_HPPA && relative_bits_ok(imm, 14)) {
		emit(hppa_ldi(imm, rd), ctx);
		return;
	}
	if (OPTIMIZE_HPPA && lower == imm) {
		emit(hppa_ldo(lower, HPPA_REG_ZERO, rd), ctx);
		return;
	}
	emit(hppa_ldil(imm, rd), ctx);
	if (OPTIMIZE_HPPA && (lower == 0))
		return;
	emit(hppa_ldo(lower, rd, rd), ctx);
}

static bool is_32b_int(s64 val)
{
	return val == (s32) val;
}

/* Emit variable-length instructions for 64-bit imm */
static void emit_imm(u8 rd, s64 imm, u8 tmpreg, struct hppa_jit_context *ctx)
{
	u32 upper32;

	/* get lower 32-bits into rd, sign extended */
	emit_imm32(rd, imm, ctx);

	/* do we have upper 32-bits too ? */
	if (OPTIMIZE_HPPA && is_32b_int(imm))
		return;

	/* load upper 32-bits into lower tmpreg and deposit into rd */
	upper32 = imm >> 32;
	if (upper32 || !OPTIMIZE_HPPA) {
		emit_imm32(tmpreg, upper32, ctx);
		emit_hppa64_depd(tmpreg, 31, 32, rd, 1, ctx);
	} else
		emit_hppa64_depd(HPPA_REG_ZERO, 31, 32, rd, 1, ctx);

}

static int emit_jump(signed long paoff, bool force_far,
			       struct hppa_jit_context *ctx)
{
	unsigned long pc, addr;

	/* Note: Use 2 instructions for jumps if force_far is set. */
	if (relative_bits_ok(paoff - HPPA_BRANCH_DISPLACEMENT, 22)) {
		/* use BL,long branch followed by nop() */
		emit(hppa64_bl_long(paoff - HPPA_BRANCH_DISPLACEMENT), ctx);
		if (force_far)
			emit(hppa_nop(), ctx);
		return 0;
	}

	pc = (uintptr_t) &ctx->insns[ctx->ninsns];
	addr = pc + (paoff * HPPA_INSN_SIZE);
	/* even the 64-bit kernel runs in memory below 4GB */
	if (WARN_ON_ONCE(addr >> 32))
		return -E2BIG;
	emit(hppa_ldil(addr, HPPA_REG_R31), ctx);
	emit(hppa_be_l(im11(addr) >> 2, HPPA_REG_R31, NOP_NEXT_INSTR), ctx);
	return 0;
}

static void __build_epilogue(bool is_tail_call, struct hppa_jit_context *ctx)
{
	int i;

	if (is_tail_call) {
		/*
		 * goto *(t0 + 4);
		 * Skips first instruction of prologue which initializes tail
		 * call counter. Assumes t0 contains address of target program,
		 * see emit_bpf_tail_call.
		 */
		emit(hppa_ldo(1 * HPPA_INSN_SIZE, HPPA_REG_T0, HPPA_REG_T0), ctx);
		emit(hppa_bv(HPPA_REG_ZERO, HPPA_REG_T0, EXEC_NEXT_INSTR), ctx);
		/* in delay slot: */
		emit(hppa_copy(HPPA_REG_TCC, HPPA_REG_TCC_IN_INIT), ctx);

		return;
	}

	/* load epilogue function pointer and jump to it. */
	/* exit point is either at next instruction, or the outest TCC exit function */
	emit(EXIT_PTR_LOAD(HPPA_REG_RP), ctx);
	emit(EXIT_PTR_JUMP(HPPA_REG_RP, NOP_NEXT_INSTR), ctx);

	/* NOTE: we are 64-bit and big-endian, so return lower sign-extended 32-bit value */
	emit_hppa64_sext32(regmap[BPF_REG_0], HPPA_REG_RET0, ctx);

	/* Restore callee-saved registers. */
	for (i = 3; i <= 15; i++) {
		if (OPTIMIZE_HPPA && !REG_WAS_SEEN(ctx, HPPA_R(i)))
			continue;
		emit(hppa64_ldd_im16(-REG_SIZE * i, HPPA_REG_SP, HPPA_R(i)), ctx);
	}

	/* load original return pointer (stored by outest TCC function) */
	emit(hppa64_ldd_im16(-2*REG_SIZE, HPPA_REG_SP, HPPA_REG_RP), ctx);
	emit(hppa_bv(HPPA_REG_ZERO, HPPA_REG_RP, EXEC_NEXT_INSTR), ctx);
	/* in delay slot: */
	emit(hppa64_ldd_im5(-REG_SIZE, HPPA_REG_SP, HPPA_REG_SP), ctx);

	emit(hppa_nop(), ctx); // XXX WARUM einer zu wenig ??
}

static int emit_branch(u8 op, u8 rd, u8 rs, signed long paoff,
			struct hppa_jit_context *ctx)
{
	int e, s;
	bool far = false;
	int off;

	if (op == BPF_JSET) {
		/*
		 * BPF_JSET is a special case: it has no inverse so translate
		 * to and() function and compare against zero
		 */
		emit(hppa_and(rd, rs, HPPA_REG_T0), ctx);
		paoff -= 1; /* reduce offset due to hppa_and() above */
		rd = HPPA_REG_T0;
		rs = HPPA_REG_ZERO;
		op = BPF_JNE;
	}

	/* set start after BPF_JSET */
	s = ctx->ninsns;

	if (!relative_branch_ok(paoff - HPPA_BRANCH_DISPLACEMENT + 1, 12)) {
		op = invert_bpf_cond(op);
		far = true;
	}

	/*
	 * For a far branch, the condition is negated and we jump over the
	 * branch itself, and the two instructions from emit_jump.
	 * For a near branch, just use paoff.
	 */
	off = far ? (2 - HPPA_BRANCH_DISPLACEMENT) : paoff - HPPA_BRANCH_DISPLACEMENT;

	switch (op) {
	/* IF (dst COND src) JUMP off */
	case BPF_JEQ:
		emit(hppa_beq(rd, rs, off), ctx);
		break;
	case BPF_JGT:
		emit(hppa_bgtu(rd, rs, off), ctx);
		break;
	case BPF_JLT:
		emit(hppa_bltu(rd, rs, off), ctx);
		break;
	case BPF_JGE:
		emit(hppa_bgeu(rd, rs, off), ctx);
		break;
	case BPF_JLE:
		emit(hppa_bleu(rd, rs, off), ctx);
		break;
	case BPF_JNE:
		emit(hppa_bne(rd, rs, off), ctx);
		break;
	case BPF_JSGT:
		emit(hppa_bgt(rd, rs, off), ctx);
		break;
	case BPF_JSLT:
		emit(hppa_blt(rd, rs, off), ctx);
		break;
	case BPF_JSGE:
		emit(hppa_bge(rd, rs, off), ctx);
		break;
	case BPF_JSLE:
		emit(hppa_ble(rd, rs, off), ctx);
		break;
	default:
		WARN_ON(1);
	}

	if (far) {
		int ret;
		e = ctx->ninsns;
		/* Adjust for extra insns. */
		paoff -= (e - s);
		ret = emit_jump(paoff, true, ctx);
		if (ret)
			return ret;
	} else {
		/*
		 * always allocate 2 nops instead of the far branch to
		 * reduce translation loops
		 */
		emit(hppa_nop(), ctx);
		emit(hppa_nop(), ctx);
	}
	return 0;
}

static void emit_zext_32(u8 reg, struct hppa_jit_context *ctx)
{
	emit_hppa64_zext32(reg, reg, ctx);
}

static void emit_bpf_tail_call(int insn, struct hppa_jit_context *ctx)
{
	/*
	 * R1 -> &ctx
	 * R2 -> &array
	 * R3 -> index
	 */
	int off;
	const s8 arr_reg = regmap[BPF_REG_2];
	const s8 idx_reg = regmap[BPF_REG_3];
	struct bpf_array bpfa;
	struct bpf_prog bpfp;

	/* if there is any tail call, we need to save & restore all registers */
	REG_SET_SEEN_ALL(ctx);

	/* get address of TCC main exit function for error case into rp */
	emit(EXIT_PTR_LOAD(HPPA_REG_RP), ctx);

	/* max_entries = array->map.max_entries; */
	off = offsetof(struct bpf_array, map.max_entries);
	BUILD_BUG_ON(sizeof(bpfa.map.max_entries) != 4);
	emit(hppa_ldw(off, arr_reg, HPPA_REG_T1), ctx);

	/*
	 * if (index >= max_entries)
	 *   goto out;
	 */
	emit(hppa_bltu(idx_reg, HPPA_REG_T1, 2 - HPPA_BRANCH_DISPLACEMENT), ctx);
	emit(EXIT_PTR_JUMP(HPPA_REG_RP, NOP_NEXT_INSTR), ctx);

	/*
	 * if (--tcc < 0)
	 *   goto out;
	 */
	REG_FORCE_SEEN(ctx, HPPA_REG_TCC);
	emit(hppa_ldo(-1, HPPA_REG_TCC, HPPA_REG_TCC), ctx);
	emit(hppa_bge(HPPA_REG_TCC, HPPA_REG_ZERO, 2 - HPPA_BRANCH_DISPLACEMENT), ctx);
	emit(EXIT_PTR_JUMP(HPPA_REG_RP, NOP_NEXT_INSTR), ctx);

	/*
	 * prog = array->ptrs[index];
	 * if (!prog)
	 *   goto out;
	 */
	BUILD_BUG_ON(sizeof(bpfa.ptrs[0]) != 8);
	emit(hppa64_shladd(idx_reg, 3, arr_reg, HPPA_REG_T0), ctx);
	off = offsetof(struct bpf_array, ptrs);
	BUILD_BUG_ON(off < 16);
	emit(hppa64_ldd_im16(off, HPPA_REG_T0, HPPA_REG_T0), ctx);
	emit(hppa_bne(HPPA_REG_T0, HPPA_REG_ZERO, 2 - HPPA_BRANCH_DISPLACEMENT), ctx);
	emit(EXIT_PTR_JUMP(HPPA_REG_RP, NOP_NEXT_INSTR), ctx);

	/*
	 * tcc = temp_tcc;
	 * goto *(prog->bpf_func + 4);
	 */
	off = offsetof(struct bpf_prog, bpf_func);
	BUILD_BUG_ON(off < 16);
	BUILD_BUG_ON(sizeof(bpfp.bpf_func) != 8);
	emit(hppa64_ldd_im16(off, HPPA_REG_T0, HPPA_REG_T0), ctx);
	/* Epilogue jumps to *(t0 + 4). */
	__build_epilogue(true, ctx);
}

static void init_regs(u8 *rd, u8 *rs, const struct bpf_insn *insn,
		      struct hppa_jit_context *ctx)
{
	u8 code = insn->code;

	switch (code) {
	case BPF_JMP | BPF_JA:
	case BPF_JMP | BPF_CALL:
	case BPF_JMP | BPF_EXIT:
	case BPF_JMP | BPF_TAIL_CALL:
		break;
	default:
		*rd = bpf_to_hppa_reg(insn->dst_reg, ctx);
	}

	if (code & (BPF_ALU | BPF_X) || code & (BPF_ALU64 | BPF_X) ||
	    code & (BPF_JMP | BPF_X) || code & (BPF_JMP32 | BPF_X) ||
	    code & BPF_LDX || code & BPF_STX)
		*rs = bpf_to_hppa_reg(insn->src_reg, ctx);
}

static void emit_zext_32_rd_rs(u8 *rd, u8 *rs, struct hppa_jit_context *ctx)
{
	emit_hppa64_zext32(*rd, HPPA_REG_T2, ctx);
	*rd = HPPA_REG_T2;
	emit_hppa64_zext32(*rs, HPPA_REG_T1, ctx);
	*rs = HPPA_REG_T1;
}

static void emit_sext_32_rd_rs(u8 *rd, u8 *rs, struct hppa_jit_context *ctx)
{
	emit_hppa64_sext32(*rd, HPPA_REG_T2, ctx);
	*rd = HPPA_REG_T2;
	emit_hppa64_sext32(*rs, HPPA_REG_T1, ctx);
	*rs = HPPA_REG_T1;
}

static void emit_zext_32_rd_t1(u8 *rd, struct hppa_jit_context *ctx)
{
	emit_hppa64_zext32(*rd, HPPA_REG_T2, ctx);
	*rd = HPPA_REG_T2;
	emit_zext_32(HPPA_REG_T1, ctx);
}

static void emit_sext_32_rd(u8 *rd, struct hppa_jit_context *ctx)
{
	emit_hppa64_sext32(*rd, HPPA_REG_T2, ctx);
	*rd = HPPA_REG_T2;
}

static bool is_signed_bpf_cond(u8 cond)
{
	return cond == BPF_JSGT || cond == BPF_JSLT ||
		cond == BPF_JSGE || cond == BPF_JSLE;
}

static void emit_call(u64 addr, bool fixed, struct hppa_jit_context *ctx)
{
	const int offset_sp = 2*FRAME_SIZE;

	emit(hppa_ldo(offset_sp, HPPA_REG_SP, HPPA_REG_SP), ctx);

	emit_hppa_copy(regmap[BPF_REG_1], HPPA_REG_ARG0, ctx);
	emit_hppa_copy(regmap[BPF_REG_2], HPPA_REG_ARG1, ctx);
	emit_hppa_copy(regmap[BPF_REG_3], HPPA_REG_ARG2, ctx);
	emit_hppa_copy(regmap[BPF_REG_4], HPPA_REG_ARG3, ctx);
	emit_hppa_copy(regmap[BPF_REG_5], HPPA_REG_ARG4, ctx);

	/* Backup TCC. */
	REG_FORCE_SEEN(ctx, HPPA_REG_TCC_SAVED);
	if (REG_WAS_SEEN(ctx, HPPA_REG_TCC))
		emit(hppa_copy(HPPA_REG_TCC, HPPA_REG_TCC_SAVED), ctx);

	/*
	 * Use ldil() to load absolute address. Don't use emit_imm as the
	 * number of emitted instructions should not depend on the value of
	 * addr.
	 */
	WARN_ON(addr >> 32);
	/* load function address and gp from Elf64_Fdesc descriptor */
	emit(hppa_ldil(addr, HPPA_REG_R31), ctx);
	emit(hppa_ldo(im11(addr), HPPA_REG_R31, HPPA_REG_R31), ctx);
	emit(hppa64_ldd_im16(offsetof(struct elf64_fdesc, addr),
			     HPPA_REG_R31, HPPA_REG_RP), ctx);
	emit(hppa64_bve_l_rp(HPPA_REG_RP), ctx);
	emit(hppa64_ldd_im16(offsetof(struct elf64_fdesc, gp),
			     HPPA_REG_R31, HPPA_REG_GP), ctx);

	/* Restore TCC. */
	if (REG_WAS_SEEN(ctx, HPPA_REG_TCC))
		emit(hppa_copy(HPPA_REG_TCC_SAVED, HPPA_REG_TCC), ctx);

	emit(hppa_ldo(-offset_sp, HPPA_REG_SP, HPPA_REG_SP), ctx);

	/* Set return value. */
	emit_hppa_copy(HPPA_REG_RET0, regmap[BPF_REG_0], ctx);
}

static void emit_call_libgcc_ll(void *func, const s8 arg0,
		const s8 arg1, u8 opcode, struct hppa_jit_context *ctx)
{
	u64 func_addr;

	if (BPF_CLASS(opcode) == BPF_ALU) {
		emit_hppa64_zext32(arg0, HPPA_REG_ARG0, ctx);
		emit_hppa64_zext32(arg1, HPPA_REG_ARG1, ctx);
	} else {
		emit_hppa_copy(arg0, HPPA_REG_ARG0, ctx);
		emit_hppa_copy(arg1, HPPA_REG_ARG1, ctx);
	}

	/* libcgcc overwrites HPPA_REG_RET0, so keep copy in HPPA_REG_TCC_SAVED */
	if (arg0 != HPPA_REG_RET0) {
		REG_SET_SEEN(ctx, HPPA_REG_TCC_SAVED);
		emit(hppa_copy(HPPA_REG_RET0, HPPA_REG_TCC_SAVED), ctx);
	}

	/* set up stack */
	emit(hppa_ldo(FRAME_SIZE, HPPA_REG_SP, HPPA_REG_SP), ctx);

	func_addr = (uintptr_t) func;
	/* load function func_address and gp from Elf64_Fdesc descriptor */
	emit_imm(HPPA_REG_R31, func_addr, arg0, ctx);
	emit(hppa64_ldd_im16(offsetof(struct elf64_fdesc, addr),
			     HPPA_REG_R31, HPPA_REG_RP), ctx);
        /* skip the following bve_l instruction if divisor is 0. */
        if (BPF_OP(opcode) == BPF_DIV || BPF_OP(opcode) == BPF_MOD) {
		if (BPF_OP(opcode) == BPF_DIV)
			emit_hppa_copy(HPPA_REG_ZERO, HPPA_REG_RET0, ctx);
		else {
			emit_hppa_copy(HPPA_REG_ARG0, HPPA_REG_RET0, ctx);
		}
		emit(hppa_beq(HPPA_REG_ARG1, HPPA_REG_ZERO, 2 - HPPA_BRANCH_DISPLACEMENT), ctx);
	}
	emit(hppa64_bve_l_rp(HPPA_REG_RP), ctx);
	emit(hppa64_ldd_im16(offsetof(struct elf64_fdesc, gp),
			     HPPA_REG_R31, HPPA_REG_GP), ctx);

	emit(hppa_ldo(-FRAME_SIZE, HPPA_REG_SP, HPPA_REG_SP), ctx);

	emit_hppa_copy(HPPA_REG_RET0, arg0, ctx);

	/* restore HPPA_REG_RET0 */
	if (arg0 != HPPA_REG_RET0)
		emit(hppa_copy(HPPA_REG_TCC_SAVED, HPPA_REG_RET0), ctx);
}

static void emit_store(const s8 rd, const s8 rs, s16 off,
			  struct hppa_jit_context *ctx, const u8 size,
			  const u8 mode)
{
	s8 dstreg;

	/* need to calculate address since offset does not fit in 14 bits? */
	if (relative_bits_ok(off, 14))
		dstreg = rd;
	else {
		/* need to use R1 here, since addil puts result into R1 */
		dstreg = HPPA_REG_R1;
		emit(hppa_addil(off, rd), ctx);
		off = im11(off);
	}

	switch (size) {
	case BPF_B:
		emit(hppa_stb(rs, off, dstreg), ctx);
		break;
	case BPF_H:
		emit(hppa_sth(rs, off, dstreg), ctx);
		break;
	case BPF_W:
		emit(hppa_stw(rs, off, dstreg), ctx);
		break;
	case BPF_DW:
		if (off & 7) {
			emit(hppa_ldo(off, dstreg, HPPA_REG_R1), ctx);
			emit(hppa64_std_im5(rs, 0, HPPA_REG_R1), ctx);
		} else if (off >= -16 && off <= 15)
			emit(hppa64_std_im5(rs, off, dstreg), ctx);
		else
			emit(hppa64_std_im16(rs, off, dstreg), ctx);
		break;
	}
}

int bpf_jit_emit_insn(const struct bpf_insn *insn, struct hppa_jit_context *ctx,
		      bool extra_pass)
{
	bool is64 = BPF_CLASS(insn->code) == BPF_ALU64 ||
		    BPF_CLASS(insn->code) == BPF_JMP;
	int s, e, ret, i = insn - ctx->prog->insnsi;
	s64 paoff;
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
		if (!is64 && !aux->verifier_zext)
			emit_hppa64_zext32(rs, rd, ctx);
		else
			emit_hppa_copy(rs, rd, ctx);
		break;

	/* dst = dst OP src */
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_X:
                emit(hppa_add(rd, rs, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
                emit(hppa_sub(rd, rs, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_AND | BPF_X:
                emit(hppa_and(rd, rs, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
                emit(hppa_or(rd, rs, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
                emit(hppa_xor(rd, rs, rd), ctx);
		if (!is64 && !aux->verifier_zext && rs != rd)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU64 | BPF_MUL | BPF_K:
		emit_imm(HPPA_REG_T1, is64 ? (s64)(s32)imm : (u32)imm, HPPA_REG_T2, ctx);
		rs = HPPA_REG_T1;
		fallthrough;
	case BPF_ALU | BPF_MUL | BPF_X:
	case BPF_ALU64 | BPF_MUL | BPF_X:
		emit_call_libgcc_ll(__muldi3, rd, rs, code, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_DIV | BPF_K:
		emit_imm(HPPA_REG_T1, is64 ? (s64)(s32)imm : (u32)imm, HPPA_REG_T2, ctx);
		rs = HPPA_REG_T1;
		fallthrough;
	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_DIV | BPF_X:
		emit_call_libgcc_ll(&hppa_div64, rd, rs, code, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		emit_imm(HPPA_REG_T1, is64 ? (s64)(s32)imm : (u32)imm, HPPA_REG_T2, ctx);
		rs = HPPA_REG_T1;
		fallthrough;
	case BPF_ALU | BPF_MOD | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
		emit_call_libgcc_ll(&hppa_div64_rem, rd, rs, code, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	case BPF_ALU | BPF_LSH | BPF_X:
	case BPF_ALU64 | BPF_LSH | BPF_X:
		emit_hppa64_sext32(rs, HPPA_REG_T0, ctx);
		emit(hppa64_mtsarcm(HPPA_REG_T0), ctx);
		if (is64)
			emit(hppa64_depdz_sar(rd, rd), ctx);
		else
			emit(hppa_depwz_sar(rd, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_X:
	case BPF_ALU64 | BPF_RSH | BPF_X:
		emit(hppa_mtsar(rs), ctx);
		if (is64)
			emit(hppa64_shrpd_sar(rd, rd), ctx);
		else
			emit(hppa_shrpw_sar(rd, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_X:
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		emit_hppa64_sext32(rs, HPPA_REG_T0, ctx);
                emit(hppa64_mtsarcm(HPPA_REG_T0), ctx);
		if (is64)
			emit(hppa_extrd_sar(rd, rd, 1), ctx);
		else
			emit(hppa_extrws_sar(rd, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
	case BPF_ALU64 | BPF_NEG:
		emit(hppa_sub(HPPA_REG_ZERO, rd, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = BSWAP##imm(dst) */
	case BPF_ALU | BPF_END | BPF_FROM_BE:
		switch (imm) {
		case 16:
			/* zero-extend 16 bits into 64 bits */
			emit_hppa64_depd(HPPA_REG_ZERO, 63-16, 64-16, rd, 1, ctx);
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

	case BPF_ALU | BPF_END | BPF_FROM_LE:
		switch (imm) {
		case 16:
			emit(hppa_extru(rd, 31 - 8, 8, HPPA_REG_T1), ctx);
			emit(hppa_depwz(rd, 23, 8, HPPA_REG_T1), ctx);
			emit(hppa_extru(HPPA_REG_T1, 31, 16, rd), ctx);
			emit_hppa64_extrd(HPPA_REG_T1, 63, 16, rd, 0, ctx);
			break;
		case 32:
			emit(hppa_shrpw(rd, rd, 16, HPPA_REG_T1), ctx);
			emit_hppa64_depd(HPPA_REG_T1, 63-16, 8, HPPA_REG_T1, 1, ctx);
			emit(hppa_shrpw(rd, HPPA_REG_T1, 8, HPPA_REG_T1), ctx);
			emit_hppa64_extrd(HPPA_REG_T1, 63, 32, rd, 0, ctx);
			break;
		case 64:
			emit(hppa64_permh_3210(rd, HPPA_REG_T1), ctx);
			emit(hppa64_hshl(HPPA_REG_T1, 8, HPPA_REG_T2), ctx);
			emit(hppa64_hshr_u(HPPA_REG_T1, 8, HPPA_REG_T1), ctx);
			emit(hppa_or(HPPA_REG_T2, HPPA_REG_T1, rd), ctx);
			break;
		default:
			pr_err("bpf-jit: BPF_END imm %d invalid\n", imm);
			return -1;
		}
		break;

	/* dst = imm */
	case BPF_ALU | BPF_MOV | BPF_K:
	case BPF_ALU64 | BPF_MOV | BPF_K:
		emit_imm(rd, imm, HPPA_REG_T2, ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* dst = dst OP imm */
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_ADD | BPF_K:
		if (relative_bits_ok(imm, 14)) {
			emit(hppa_ldo(imm, rd, rd), ctx);
		} else {
			emit_imm(HPPA_REG_T1, imm, HPPA_REG_T2, ctx);
			emit(hppa_add(rd, HPPA_REG_T1, rd), ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
		if (relative_bits_ok(-imm, 14)) {
			emit(hppa_ldo(-imm, rd, rd), ctx);
		} else {
			emit_imm(HPPA_REG_T1, imm, HPPA_REG_T2, ctx);
			emit(hppa_sub(rd, HPPA_REG_T1, rd), ctx);
		}
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU64 | BPF_AND | BPF_K:
		emit_imm(HPPA_REG_T1, imm, HPPA_REG_T2, ctx);
                emit(hppa_and(rd, HPPA_REG_T1, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_K:
	case BPF_ALU64 | BPF_OR | BPF_K:
		emit_imm(HPPA_REG_T1, imm, HPPA_REG_T2, ctx);
                emit(hppa_or(rd, HPPA_REG_T1, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
		emit_imm(HPPA_REG_T1, imm, HPPA_REG_T2, ctx);
                emit(hppa_xor(rd, HPPA_REG_T1, rd), ctx);
		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_LSH | BPF_K:
	case BPF_ALU64 | BPF_LSH | BPF_K:
		if (imm != 0) {
			emit_hppa64_shld(rd, imm, rd, ctx);
		}

		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_K:
	case BPF_ALU64 | BPF_RSH | BPF_K:
		if (imm != 0) {
			if (is64)
				emit_hppa64_shrd(rd, imm, rd, false, ctx);
			else
				emit_hppa64_shrw(rd, imm, rd, false, ctx);
		}

		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_K:
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		if (imm != 0) {
			if (is64)
				emit_hppa64_shrd(rd, imm, rd, true, ctx);
			else
				emit_hppa64_shrw(rd, imm, rd, true, ctx);
		}

		if (!is64 && !aux->verifier_zext)
			emit_zext_32(rd, ctx);
		break;

	/* JUMP off */
	case BPF_JMP | BPF_JA:
		paoff = hppa_offset(i, off, ctx);
		ret = emit_jump(paoff, false, ctx);
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
		paoff = hppa_offset(i, off, ctx);
		if (!is64) {
			s = ctx->ninsns;
			if (is_signed_bpf_cond(BPF_OP(code)))
				emit_sext_32_rd_rs(&rd, &rs, ctx);
			else
				emit_zext_32_rd_rs(&rd, &rs, ctx);
			e = ctx->ninsns;

			/* Adjust for extra insns */
			paoff -= (e - s);
		}
		if (BPF_OP(code) == BPF_JSET) {
			/* Adjust for and */
			paoff -= 1;
			emit(hppa_and(rs, rd, HPPA_REG_T1), ctx);
			emit_branch(BPF_JNE, HPPA_REG_T1, HPPA_REG_ZERO, paoff,
				    ctx);
		} else {
			emit_branch(BPF_OP(code), rd, rs, paoff, ctx);
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
		paoff = hppa_offset(i, off, ctx);
		s = ctx->ninsns;
		if (imm) {
			emit_imm(HPPA_REG_T1, imm, HPPA_REG_T2, ctx);
			rs = HPPA_REG_T1;
		} else {
			rs = HPPA_REG_ZERO;
		}
		if (!is64) {
			if (is_signed_bpf_cond(BPF_OP(code)))
				emit_sext_32_rd(&rd, ctx);
			else
				emit_zext_32_rd_t1(&rd, ctx);
		}
		e = ctx->ninsns;

		/* Adjust for extra insns */
		paoff -= (e - s);
		emit_branch(BPF_OP(code), rd, rs, paoff, ctx);
		break;
	case BPF_JMP | BPF_JSET | BPF_K:
	case BPF_JMP32 | BPF_JSET | BPF_K:
		paoff = hppa_offset(i, off, ctx);
		s = ctx->ninsns;
		emit_imm(HPPA_REG_T1, imm, HPPA_REG_T2, ctx);
		emit(hppa_and(HPPA_REG_T1, rd, HPPA_REG_T1), ctx);
		/* For jset32, we should clear the upper 32 bits of t1, but
		 * sign-extension is sufficient here and saves one instruction,
		 * as t1 is used only in comparison against zero.
		 */
		if (!is64 && imm < 0)
			emit_hppa64_sext32(HPPA_REG_T1, HPPA_REG_T1, ctx);
		e = ctx->ninsns;
		paoff -= (e - s);
		emit_branch(BPF_JNE, HPPA_REG_T1, HPPA_REG_ZERO, paoff, ctx);
		break;
	/* function call */
	case BPF_JMP | BPF_CALL:
	{
		bool fixed_addr;
		u64 addr;

		ret = bpf_jit_get_func_addr(ctx->prog, insn, extra_pass,
					    &addr, &fixed_addr);
		if (ret < 0)
			return ret;

		REG_SET_SEEN_ALL(ctx);
		emit_call(addr, fixed_addr, ctx);
		break;
	}
	/* tail call */
	case BPF_JMP | BPF_TAIL_CALL:
		emit_bpf_tail_call(i, ctx);
		break;

	/* function return */
	case BPF_JMP | BPF_EXIT:
		if (i == ctx->prog->len - 1)
			break;

		paoff = epilogue_offset(ctx);
		ret = emit_jump(paoff, false, ctx);
		if (ret)
			return ret;
		break;

	/* dst = imm64 */
	case BPF_LD | BPF_IMM | BPF_DW:
	{
		struct bpf_insn insn1 = insn[1];
		u64 imm64 = (u64)insn1.imm << 32 | (u32)imm;
		if (bpf_pseudo_func(insn))
			imm64 = (uintptr_t)dereference_function_descriptor((void*)imm64);
		emit_imm(rd, imm64, HPPA_REG_T2, ctx);

		return 1;
	}

	/* LDX: dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_DW:
	case BPF_LDX | BPF_PROBE_MEM | BPF_B:
	case BPF_LDX | BPF_PROBE_MEM | BPF_H:
	case BPF_LDX | BPF_PROBE_MEM | BPF_W:
	case BPF_LDX | BPF_PROBE_MEM | BPF_DW:
	{
		u8 srcreg;

		/* need to calculate address since offset does not fit in 14 bits? */
		if (relative_bits_ok(off, 14))
			srcreg = rs;
		else {
			/* need to use R1 here, since addil puts result into R1 */
			srcreg = HPPA_REG_R1;
			BUG_ON(rs == HPPA_REG_R1);
			BUG_ON(rd == HPPA_REG_R1);
			emit(hppa_addil(off, rs), ctx);
			off = im11(off);
		}

		switch (BPF_SIZE(code)) {
		case BPF_B:
			emit(hppa_ldb(off, srcreg, rd), ctx);
			if (insn_is_zext(&insn[1]))
				return 1;
			break;
		case BPF_H:
			emit(hppa_ldh(off, srcreg, rd), ctx);
			if (insn_is_zext(&insn[1]))
				return 1;
			break;
		case BPF_W:
			emit(hppa_ldw(off, srcreg, rd), ctx);
			if (insn_is_zext(&insn[1]))
				return 1;
			break;
		case BPF_DW:
			if (off & 7) {
				emit(hppa_ldo(off, srcreg, HPPA_REG_R1), ctx);
				emit(hppa64_ldd_reg(HPPA_REG_ZERO, HPPA_REG_R1, rd), ctx);
			} else if (off >= -16 && off <= 15)
				emit(hppa64_ldd_im5(off, srcreg, rd), ctx);
			else
				emit(hppa64_ldd_im16(off, srcreg, rd), ctx);
			break;
		}
		break;
	}
	/* speculation barrier */
	case BPF_ST | BPF_NOSPEC:
		break;

	/* ST: *(size *)(dst + off) = imm */
	/* STX: *(size *)(dst + off) = src */
	case BPF_ST | BPF_MEM | BPF_B:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_DW:

	case BPF_STX | BPF_MEM | BPF_B:
	case BPF_STX | BPF_MEM | BPF_H:
	case BPF_STX | BPF_MEM | BPF_W:
	case BPF_STX | BPF_MEM | BPF_DW:
		if (BPF_CLASS(code) == BPF_ST) {
			emit_imm(HPPA_REG_T2, imm, HPPA_REG_T1, ctx);
			rs = HPPA_REG_T2;
		}

		emit_store(rd, rs, off, ctx, BPF_SIZE(code), BPF_MODE(code));
		break;

	case BPF_STX | BPF_ATOMIC | BPF_W:
	case BPF_STX | BPF_ATOMIC | BPF_DW:
		pr_info_once(
			"bpf-jit: not supported: atomic operation %02x ***\n",
			insn->imm);
		return -EFAULT;

	default:
		pr_err("bpf-jit: unknown opcode %02x\n", code);
		return -EINVAL;
	}

	return 0;
}

void bpf_jit_build_prologue(struct hppa_jit_context *ctx)
{
	int bpf_stack_adjust, stack_adjust, i;
	unsigned long addr;
	s8 reg;

	/*
	 * stack on hppa grows up, so if tail calls are used we need to
	 * allocate the maximum stack size
	 */
	if (REG_ALL_SEEN(ctx))
		bpf_stack_adjust = MAX_BPF_STACK;
	else
		bpf_stack_adjust = ctx->prog->aux->stack_depth;
	bpf_stack_adjust = round_up(bpf_stack_adjust, STACK_ALIGN);

	stack_adjust = FRAME_SIZE + bpf_stack_adjust;
	stack_adjust = round_up(stack_adjust, STACK_ALIGN);

	/*
	 * NOTE: We construct an Elf64_Fdesc descriptor here.
	 * The first 4 words initialize the TCC and compares them.
	 * Then follows the virtual address of the eBPF function,
	 * and the gp for this function.
	 *
	 * The first instruction sets the tail-call-counter (TCC) register.
	 * This instruction is skipped by tail calls.
	 * Use a temporary register instead of a caller-saved register initially.
	 */
	REG_FORCE_SEEN(ctx, HPPA_REG_TCC_IN_INIT);
	emit(hppa_ldi(MAX_TAIL_CALL_CNT, HPPA_REG_TCC_IN_INIT), ctx);

	/*
	 * Skip all initializations when called as BPF TAIL call.
	 */
	emit(hppa_ldi(MAX_TAIL_CALL_CNT, HPPA_REG_R1), ctx);
	emit(hppa_beq(HPPA_REG_TCC_IN_INIT, HPPA_REG_R1, 6 - HPPA_BRANCH_DISPLACEMENT), ctx);
	emit(hppa64_bl_long(ctx->prologue_len - 3 - HPPA_BRANCH_DISPLACEMENT), ctx);

	/* store entry address of this eBPF function */
	addr = (uintptr_t) &ctx->insns[0];
	emit(addr >> 32, ctx);
	emit(addr & 0xffffffff, ctx);

	/* store gp of this eBPF function */
	asm("copy %%r27,%0" : "=r" (addr) );
	emit(addr >> 32, ctx);
	emit(addr & 0xffffffff, ctx);

	/* Set up hppa stack frame. */
	emit_hppa_copy(HPPA_REG_SP, HPPA_REG_R1, ctx);
	emit(hppa_ldo(stack_adjust, HPPA_REG_SP, HPPA_REG_SP), ctx);
	emit(hppa64_std_im5 (HPPA_REG_R1, -REG_SIZE, HPPA_REG_SP), ctx);
	emit(hppa64_std_im16(HPPA_REG_RP, -2*REG_SIZE, HPPA_REG_SP), ctx);

	/* Save callee-save registers. */
	for (i = 3; i <= 15; i++) {
		if (OPTIMIZE_HPPA && !REG_WAS_SEEN(ctx, HPPA_R(i)))
			continue;
		emit(hppa64_std_im16(HPPA_R(i), -REG_SIZE * i, HPPA_REG_SP), ctx);
	}

	/* load function parameters; load all if we use tail functions */
	#define LOAD_PARAM(arg, dst) \
		if (REG_WAS_SEEN(ctx, regmap[dst]) ||	\
		    REG_WAS_SEEN(ctx, HPPA_REG_TCC))	\
			emit_hppa_copy(arg, regmap[dst], ctx)
	LOAD_PARAM(HPPA_REG_ARG0, BPF_REG_1);
	LOAD_PARAM(HPPA_REG_ARG1, BPF_REG_2);
	LOAD_PARAM(HPPA_REG_ARG2, BPF_REG_3);
	LOAD_PARAM(HPPA_REG_ARG3, BPF_REG_4);
	LOAD_PARAM(HPPA_REG_ARG4, BPF_REG_5);
	#undef LOAD_PARAM

	REG_FORCE_SEEN(ctx, HPPA_REG_T0);
	REG_FORCE_SEEN(ctx, HPPA_REG_T1);
	REG_FORCE_SEEN(ctx, HPPA_REG_T2);

	/*
	 * Now really set the tail call counter (TCC) register.
	 */
	if (REG_WAS_SEEN(ctx, HPPA_REG_TCC))
		emit(hppa_ldi(MAX_TAIL_CALL_CNT, HPPA_REG_TCC), ctx);

	/*
	 * Save epilogue function pointer for outer TCC call chain.
	 * The main TCC call stores the final RP on stack.
	 */
	addr = (uintptr_t) &ctx->insns[ctx->epilogue_offset];
	/* skip first two instructions which jump to exit */
	addr += 2 * HPPA_INSN_SIZE;
	emit_imm(HPPA_REG_T2, addr, HPPA_REG_T1, ctx);
	emit(EXIT_PTR_STORE(HPPA_REG_T2), ctx);

	/* Set up BPF frame pointer. */
	reg = regmap[BPF_REG_FP];	/* -> HPPA_REG_FP */
	if (REG_WAS_SEEN(ctx, reg)) {
		emit(hppa_ldo(-FRAME_SIZE, HPPA_REG_SP, reg), ctx);
	}
}

void bpf_jit_build_epilogue(struct hppa_jit_context *ctx)
{
	__build_epilogue(false, ctx);
}

bool bpf_jit_supports_kfunc_call(void)
{
	return true;
}
