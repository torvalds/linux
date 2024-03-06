// SPDX-License-Identifier: GPL-2.0
/*
 * BPF JIT compiler for PA-RISC (32-bit)
 *
 * Copyright (c) 2023 Helge Deller <deller@gmx.de>
 *
 * The code is based on the BPF JIT compiler for RV64 by Björn Töpel and
 * the BPF JIT compiler for 32-bit ARM by Shubham Bansal and Mircea Gherzan.
 */

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/libgcc.h>
#include "bpf_jit.h"

/*
 * Stack layout during BPF program execution (note: stack grows up):
 *
 *                     high
 *   HPPA32 sp =>  +----------+ <= HPPA32 fp
 *                 | saved sp |
 *                 | saved rp |
 *                 |   ...    | HPPA32 callee-saved registers
 *                 | curr args|
 *                 | local var|
 *                 +----------+ <= (sp - 4 * NR_SAVED_REGISTERS)
 *                 |  lo(R9)  |
 *                 |  hi(R9)  |
 *                 |  lo(FP)  | JIT scratch space for BPF registers
 *                 |  hi(FP)  |
 *                 |   ...    |
 *                 +----------+ <= (sp - 4 * NR_SAVED_REGISTERS
 *                 |          |        - 4 * BPF_JIT_SCRATCH_REGS)
 *                 |          |
 *                 |   ...    | BPF program stack
 *                 |          |
 *                 |   ...    | Function call stack
 *                 |          |
 *                 +----------+
 *                     low
 */

enum {
	/* Stack layout - these are offsets from top of JIT scratch space. */
	BPF_R8_HI,
	BPF_R8_LO,
	BPF_R9_HI,
	BPF_R9_LO,
	BPF_FP_HI,
	BPF_FP_LO,
	BPF_AX_HI,
	BPF_AX_LO,
	BPF_R0_TEMP_HI,
	BPF_R0_TEMP_LO,
	BPF_JIT_SCRATCH_REGS,
};

/* Number of callee-saved registers stored to stack: rp, r3-r18. */
#define NR_SAVED_REGISTERS	(18 - 3 + 1 + 8)

/* Offset from fp for BPF registers stored on stack. */
#define STACK_OFFSET(k)	(- (NR_SAVED_REGISTERS + k + 1))
#define STACK_ALIGN	FRAME_SIZE

#define EXIT_PTR_LOAD(reg)	hppa_ldw(-0x08, HPPA_REG_SP, reg)
#define EXIT_PTR_STORE(reg)	hppa_stw(reg, -0x08, HPPA_REG_SP)
#define EXIT_PTR_JUMP(reg, nop)	hppa_bv(HPPA_REG_ZERO, reg, nop)

#define TMP_REG_1	(MAX_BPF_JIT_REG + 0)
#define TMP_REG_2	(MAX_BPF_JIT_REG + 1)
#define TMP_REG_R0	(MAX_BPF_JIT_REG + 2)

static const s8 regmap[][2] = {
	/* Return value from in-kernel function, and exit value from eBPF. */
	[BPF_REG_0] = {HPPA_REG_RET0, HPPA_REG_RET1},		/* HI/LOW */

	/* Arguments from eBPF program to in-kernel function. */
	[BPF_REG_1] = {HPPA_R(3), HPPA_R(4)},
	[BPF_REG_2] = {HPPA_R(5), HPPA_R(6)},
	[BPF_REG_3] = {HPPA_R(7), HPPA_R(8)},
	[BPF_REG_4] = {HPPA_R(9), HPPA_R(10)},
	[BPF_REG_5] = {HPPA_R(11), HPPA_R(12)},

	[BPF_REG_6] = {HPPA_R(13), HPPA_R(14)},
	[BPF_REG_7] = {HPPA_R(15), HPPA_R(16)},
	/*
	 * Callee-saved registers that in-kernel function will preserve.
	 * Stored on the stack.
	 */
	[BPF_REG_8] = {STACK_OFFSET(BPF_R8_HI), STACK_OFFSET(BPF_R8_LO)},
	[BPF_REG_9] = {STACK_OFFSET(BPF_R9_HI), STACK_OFFSET(BPF_R9_LO)},

	/* Read-only frame pointer to access BPF stack. Not needed. */
	[BPF_REG_FP] = {STACK_OFFSET(BPF_FP_HI), STACK_OFFSET(BPF_FP_LO)},

	/* Temporary register for blinding constants. Stored on the stack. */
	[BPF_REG_AX] = {STACK_OFFSET(BPF_AX_HI), STACK_OFFSET(BPF_AX_LO)},
	/*
	 * Temporary registers used by the JIT to operate on registers stored
	 * on the stack. Save t0 and t1 to be used as temporaries in generated
	 * code.
	 */
	[TMP_REG_1] = {HPPA_REG_T3, HPPA_REG_T2},
	[TMP_REG_2] = {HPPA_REG_T5, HPPA_REG_T4},

	/* temporary space for BPF_R0 during libgcc and millicode calls */
	[TMP_REG_R0] = {STACK_OFFSET(BPF_R0_TEMP_HI), STACK_OFFSET(BPF_R0_TEMP_LO)},
};

static s8 hi(const s8 *r)
{
	return r[0];
}

static s8 lo(const s8 *r)
{
	return r[1];
}

static void emit_hppa_copy(const s8 rs, const s8 rd, struct hppa_jit_context *ctx)
{
	REG_SET_SEEN(ctx, rd);
	if (OPTIMIZE_HPPA && (rs == rd))
		return;
	REG_SET_SEEN(ctx, rs);
	emit(hppa_copy(rs, rd), ctx);
}

static void emit_hppa_xor(const s8 r1, const s8 r2, const s8 r3, struct hppa_jit_context *ctx)
{
	REG_SET_SEEN(ctx, r1);
	REG_SET_SEEN(ctx, r2);
	REG_SET_SEEN(ctx, r3);
	if (OPTIMIZE_HPPA && (r1 == r2)) {
		emit(hppa_copy(HPPA_REG_ZERO, r3), ctx);
	} else {
		emit(hppa_xor(r1, r2, r3), ctx);
	}
}

static void emit_imm(const s8 rd, s32 imm, struct hppa_jit_context *ctx)
{
	u32 lower = im11(imm);

	REG_SET_SEEN(ctx, rd);
	if (OPTIMIZE_HPPA && relative_bits_ok(imm, 14)) {
		emit(hppa_ldi(imm, rd), ctx);
		return;
	}
	emit(hppa_ldil(imm, rd), ctx);
	if (OPTIMIZE_HPPA && (lower == 0))
		return;
	emit(hppa_ldo(lower, rd, rd), ctx);
}

static void emit_imm32(const s8 *rd, s32 imm, struct hppa_jit_context *ctx)
{
	/* Emit immediate into lower bits. */
	REG_SET_SEEN(ctx, lo(rd));
	emit_imm(lo(rd), imm, ctx);

	/* Sign-extend into upper bits. */
	REG_SET_SEEN(ctx, hi(rd));
	if (imm >= 0)
		emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
	else
		emit(hppa_ldi(-1, hi(rd)), ctx);
}

static void emit_imm64(const s8 *rd, s32 imm_hi, s32 imm_lo,
		       struct hppa_jit_context *ctx)
{
	emit_imm(hi(rd), imm_hi, ctx);
	emit_imm(lo(rd), imm_lo, ctx);
}

static void __build_epilogue(bool is_tail_call, struct hppa_jit_context *ctx)
{
	const s8 *r0 = regmap[BPF_REG_0];
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
	/* exit point is either directly below, or the outest TCC exit function */
	emit(EXIT_PTR_LOAD(HPPA_REG_RP), ctx);
	emit(EXIT_PTR_JUMP(HPPA_REG_RP, NOP_NEXT_INSTR), ctx);

	/* NOTE: we are 32-bit and big-endian, so return lower 32-bit value */
	emit_hppa_copy(lo(r0), HPPA_REG_RET0, ctx);

	/* Restore callee-saved registers. */
	for (i = 3; i <= 18; i++) {
		if (OPTIMIZE_HPPA && !REG_WAS_SEEN(ctx, HPPA_R(i)))
			continue;
		emit(hppa_ldw(-REG_SIZE * (8 + (i-3)), HPPA_REG_SP, HPPA_R(i)), ctx);
	}

	/* load original return pointer (stored by outest TCC function) */
	emit(hppa_ldw(-0x14, HPPA_REG_SP, HPPA_REG_RP), ctx);
	emit(hppa_bv(HPPA_REG_ZERO, HPPA_REG_RP, EXEC_NEXT_INSTR), ctx);
	/* in delay slot: */
	emit(hppa_ldw(-0x04, HPPA_REG_SP, HPPA_REG_SP), ctx);
}

static bool is_stacked(s8 reg)
{
	return reg < 0;
}

static const s8 *bpf_get_reg64_offset(const s8 *reg, const s8 *tmp,
		u16 offset_sp, struct hppa_jit_context *ctx)
{
	if (is_stacked(hi(reg))) {
		emit(hppa_ldw(REG_SIZE * hi(reg) - offset_sp, HPPA_REG_SP, hi(tmp)), ctx);
		emit(hppa_ldw(REG_SIZE * lo(reg) - offset_sp, HPPA_REG_SP, lo(tmp)), ctx);
		reg = tmp;
	}
	REG_SET_SEEN(ctx, hi(reg));
	REG_SET_SEEN(ctx, lo(reg));
	return reg;
}

static const s8 *bpf_get_reg64(const s8 *reg, const s8 *tmp,
			       struct hppa_jit_context *ctx)
{
	return bpf_get_reg64_offset(reg, tmp, 0, ctx);
}

static const s8 *bpf_get_reg64_ref(const s8 *reg, const s8 *tmp,
		bool must_load, struct hppa_jit_context *ctx)
{
	if (!OPTIMIZE_HPPA)
		return bpf_get_reg64(reg, tmp, ctx);

	if (is_stacked(hi(reg))) {
		if (must_load)
			emit(hppa_ldw(REG_SIZE * hi(reg), HPPA_REG_SP, hi(tmp)), ctx);
		reg = tmp;
	}
	REG_SET_SEEN(ctx, hi(reg));
	REG_SET_SEEN(ctx, lo(reg));
	return reg;
}


static void bpf_put_reg64(const s8 *reg, const s8 *src,
			  struct hppa_jit_context *ctx)
{
	if (is_stacked(hi(reg))) {
		emit(hppa_stw(hi(src), REG_SIZE * hi(reg), HPPA_REG_SP), ctx);
		emit(hppa_stw(lo(src), REG_SIZE * lo(reg), HPPA_REG_SP), ctx);
	}
}

static void bpf_save_R0(struct hppa_jit_context *ctx)
{
	bpf_put_reg64(regmap[TMP_REG_R0], regmap[BPF_REG_0], ctx);
}

static void bpf_restore_R0(struct hppa_jit_context *ctx)
{
	bpf_get_reg64(regmap[TMP_REG_R0], regmap[BPF_REG_0], ctx);
}


static const s8 *bpf_get_reg32(const s8 *reg, const s8 *tmp,
			       struct hppa_jit_context *ctx)
{
	if (is_stacked(lo(reg))) {
		emit(hppa_ldw(REG_SIZE * lo(reg), HPPA_REG_SP, lo(tmp)), ctx);
		reg = tmp;
	}
	REG_SET_SEEN(ctx, lo(reg));
	return reg;
}

static const s8 *bpf_get_reg32_ref(const s8 *reg, const s8 *tmp,
		struct hppa_jit_context *ctx)
{
	if (!OPTIMIZE_HPPA)
		return bpf_get_reg32(reg, tmp, ctx);

	if (is_stacked(hi(reg))) {
		reg = tmp;
	}
	REG_SET_SEEN(ctx, lo(reg));
	return reg;
}

static void bpf_put_reg32(const s8 *reg, const s8 *src,
			  struct hppa_jit_context *ctx)
{
	if (is_stacked(lo(reg))) {
		REG_SET_SEEN(ctx, lo(src));
		emit(hppa_stw(lo(src), REG_SIZE * lo(reg), HPPA_REG_SP), ctx);
		if (1 && !ctx->prog->aux->verifier_zext) {
			REG_SET_SEEN(ctx, hi(reg));
			emit(hppa_stw(HPPA_REG_ZERO, REG_SIZE * hi(reg), HPPA_REG_SP), ctx);
		}
	} else if (1 && !ctx->prog->aux->verifier_zext) {
		REG_SET_SEEN(ctx, hi(reg));
		emit_hppa_copy(HPPA_REG_ZERO, hi(reg), ctx);
	}
}

/* extern hppa millicode functions */
extern void $$mulI(void);
extern void $$divU(void);
extern void $$remU(void);

static void emit_call_millicode(void *func, const s8 arg0,
		const s8 arg1, u8 opcode, struct hppa_jit_context *ctx)
{
	u32 func_addr;

	emit_hppa_copy(arg0, HPPA_REG_ARG0, ctx);
	emit_hppa_copy(arg1, HPPA_REG_ARG1, ctx);

	/* libcgcc overwrites HPPA_REG_RET0/1, save temp. in dest. */
	if (arg0 != HPPA_REG_RET1)
		bpf_save_R0(ctx);

	func_addr = (uintptr_t) dereference_function_descriptor(func);
	emit(hppa_ldil(func_addr, HPPA_REG_R31), ctx);
	/* skip the following be_l instruction if divisor is zero. */
	if (BPF_OP(opcode) == BPF_DIV || BPF_OP(opcode) == BPF_MOD) {
		if (BPF_OP(opcode) == BPF_DIV)
			emit_hppa_copy(HPPA_REG_ZERO, HPPA_REG_RET1, ctx);
		else
			emit_hppa_copy(HPPA_REG_ARG0, HPPA_REG_RET1, ctx);
		emit(hppa_or_cond(HPPA_REG_ARG1, HPPA_REG_ZERO, 1, 0, HPPA_REG_ZERO), ctx);
	}
	/* Note: millicode functions use r31 as return pointer instead of rp */
	emit(hppa_be_l(im11(func_addr) >> 2, HPPA_REG_R31, NOP_NEXT_INSTR), ctx);
	emit(hppa_nop(), ctx); /* this nop is needed here for delay slot */

	/* Note: millicode functions return result in RET1, not RET0 */
	emit_hppa_copy(HPPA_REG_RET1, arg0, ctx);

	/* restore HPPA_REG_RET0/1, temp. save in dest. */
	if (arg0 != HPPA_REG_RET1)
		bpf_restore_R0(ctx);
}

static void emit_call_libgcc_ll(void *func, const s8 *arg0,
		const s8 *arg1, u8 opcode, struct hppa_jit_context *ctx)
{
	u32 func_addr;

	emit_hppa_copy(lo(arg0), HPPA_REG_ARG0, ctx);
	emit_hppa_copy(hi(arg0), HPPA_REG_ARG1, ctx);
	emit_hppa_copy(lo(arg1), HPPA_REG_ARG2, ctx);
	emit_hppa_copy(hi(arg1), HPPA_REG_ARG3, ctx);

	/* libcgcc overwrites HPPA_REG_RET0/_RET1, so keep copy of R0 on stack */
	if (hi(arg0) != HPPA_REG_RET0)
		bpf_save_R0(ctx);

	/* prepare stack */
	emit(hppa_ldo(2 * FRAME_SIZE, HPPA_REG_SP, HPPA_REG_SP), ctx);

	func_addr = (uintptr_t) dereference_function_descriptor(func);
	emit(hppa_ldil(func_addr, HPPA_REG_R31), ctx);
        /* zero out the following be_l instruction if divisor is 0 (and set default values) */
	if (BPF_OP(opcode) == BPF_DIV || BPF_OP(opcode) == BPF_MOD) {
		emit_hppa_copy(HPPA_REG_ZERO, HPPA_REG_RET0, ctx);
		if (BPF_OP(opcode) == BPF_DIV)
			emit_hppa_copy(HPPA_REG_ZERO, HPPA_REG_RET1, ctx);
		else
			emit_hppa_copy(HPPA_REG_ARG0, HPPA_REG_RET1, ctx);
		emit(hppa_or_cond(HPPA_REG_ARG2, HPPA_REG_ARG3, 1, 0, HPPA_REG_ZERO), ctx);
	}
	emit(hppa_be_l(im11(func_addr) >> 2, HPPA_REG_R31, EXEC_NEXT_INSTR), ctx);
	emit_hppa_copy(HPPA_REG_R31, HPPA_REG_RP, ctx);

	/* restore stack */
	emit(hppa_ldo(-2 * FRAME_SIZE, HPPA_REG_SP, HPPA_REG_SP), ctx);

	emit_hppa_copy(HPPA_REG_RET0, hi(arg0), ctx);
	emit_hppa_copy(HPPA_REG_RET1, lo(arg0), ctx);

	/* restore HPPA_REG_RET0/_RET1 */
	if (hi(arg0) != HPPA_REG_RET0)
		bpf_restore_R0(ctx);
}

static void emit_jump(s32 paoff, bool force_far,
			       struct hppa_jit_context *ctx)
{
	unsigned long pc, addr;

	/* Note: allocate 2 instructions for jumps if force_far is set. */
	if (relative_bits_ok(paoff - HPPA_BRANCH_DISPLACEMENT, 17)) {
		/* use BL,short branch followed by nop() */
		emit(hppa_bl(paoff - HPPA_BRANCH_DISPLACEMENT, HPPA_REG_ZERO), ctx);
		if (force_far)
			emit(hppa_nop(), ctx);
		return;
	}

	pc = (uintptr_t) &ctx->insns[ctx->ninsns];
	addr = pc + (paoff * HPPA_INSN_SIZE);
	emit(hppa_ldil(addr, HPPA_REG_R31), ctx);
	emit(hppa_be_l(im11(addr) >> 2, HPPA_REG_R31, NOP_NEXT_INSTR), ctx); // be,l,n addr(sr4,r31), %sr0, %r31
}

static void emit_alu_i64(const s8 *dst, s32 imm,
			 struct hppa_jit_context *ctx, const u8 op)
{
	const s8 *tmp1 = regmap[TMP_REG_1];
	const s8 *rd;

	if (0 && op == BPF_MOV)
		rd = bpf_get_reg64_ref(dst, tmp1, false, ctx);
	else
		rd = bpf_get_reg64(dst, tmp1, ctx);

	/* dst = dst OP imm */
	switch (op) {
	case BPF_MOV:
		emit_imm32(rd, imm, ctx);
		break;
	case BPF_AND:
		emit_imm(HPPA_REG_T0, imm, ctx);
		emit(hppa_and(lo(rd), HPPA_REG_T0, lo(rd)), ctx);
		if (imm >= 0)
			emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
		break;
	case BPF_OR:
		emit_imm(HPPA_REG_T0, imm, ctx);
		emit(hppa_or(lo(rd), HPPA_REG_T0, lo(rd)), ctx);
		if (imm < 0)
			emit_imm(hi(rd), -1, ctx);
		break;
	case BPF_XOR:
		emit_imm(HPPA_REG_T0, imm, ctx);
		emit_hppa_xor(lo(rd), HPPA_REG_T0, lo(rd), ctx);
		if (imm < 0) {
			emit_imm(HPPA_REG_T0, -1, ctx);
			emit_hppa_xor(hi(rd), HPPA_REG_T0, hi(rd), ctx);
		}
		break;
	case BPF_LSH:
		if (imm == 0)
			break;
		if (imm > 32) {
			imm -= 32;
			emit(hppa_zdep(lo(rd), imm, imm, hi(rd)), ctx);
			emit_hppa_copy(HPPA_REG_ZERO, lo(rd), ctx);
		} else if (imm == 32) {
			emit_hppa_copy(lo(rd), hi(rd), ctx);
			emit_hppa_copy(HPPA_REG_ZERO, lo(rd), ctx);
		} else {
			emit(hppa_shd(hi(rd), lo(rd), 32 - imm, hi(rd)), ctx);
			emit(hppa_zdep(lo(rd), imm, imm, lo(rd)), ctx);
		}
		break;
	case BPF_RSH:
		if (imm == 0)
			break;
		if (imm > 32) {
			imm -= 32;
			emit(hppa_shr(hi(rd), imm, lo(rd)), ctx);
			emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
		} else if (imm == 32) {
			emit_hppa_copy(hi(rd), lo(rd), ctx);
			emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
		} else {
			emit(hppa_shrpw(hi(rd), lo(rd), imm, lo(rd)), ctx);
			emit(hppa_shr(hi(rd), imm, hi(rd)), ctx);
		}
		break;
	case BPF_ARSH:
		if (imm == 0)
			break;
		if (imm > 32) {
			imm -= 32;
			emit(hppa_extrws(hi(rd), 31 - imm, imm, lo(rd)), ctx);
			emit(hppa_extrws(hi(rd), 0, 31, hi(rd)), ctx);
		} else if (imm == 32) {
			emit_hppa_copy(hi(rd), lo(rd), ctx);
			emit(hppa_extrws(hi(rd), 0, 31, hi(rd)), ctx);
		} else {
			emit(hppa_shrpw(hi(rd), lo(rd), imm, lo(rd)), ctx);
			emit(hppa_extrws(hi(rd), 31 - imm, imm, hi(rd)), ctx);
		}
		break;
	default:
		WARN_ON(1);
	}

	bpf_put_reg64(dst, rd, ctx);
}

static void emit_alu_i32(const s8 *dst, s32 imm,
			 struct hppa_jit_context *ctx, const u8 op)
{
	const s8 *tmp1 = regmap[TMP_REG_1];
	const s8 *rd = bpf_get_reg32(dst, tmp1, ctx);

	if (op == BPF_MOV)
		rd = bpf_get_reg32_ref(dst, tmp1, ctx);
	else
		rd = bpf_get_reg32(dst, tmp1, ctx);

	/* dst = dst OP imm */
	switch (op) {
	case BPF_MOV:
		emit_imm(lo(rd), imm, ctx);
		break;
	case BPF_ADD:
		emit_imm(HPPA_REG_T0, imm, ctx);
		emit(hppa_add(lo(rd), HPPA_REG_T0, lo(rd)), ctx);
		break;
	case BPF_SUB:
		emit_imm(HPPA_REG_T0, imm, ctx);
		emit(hppa_sub(lo(rd), HPPA_REG_T0, lo(rd)), ctx);
		break;
	case BPF_AND:
		emit_imm(HPPA_REG_T0, imm, ctx);
		emit(hppa_and(lo(rd), HPPA_REG_T0, lo(rd)), ctx);
		break;
	case BPF_OR:
		emit_imm(HPPA_REG_T0, imm, ctx);
		emit(hppa_or(lo(rd), HPPA_REG_T0, lo(rd)), ctx);
		break;
	case BPF_XOR:
		emit_imm(HPPA_REG_T0, imm, ctx);
		emit_hppa_xor(lo(rd), HPPA_REG_T0, lo(rd), ctx);
		break;
	case BPF_LSH:
		if (imm != 0)
			emit(hppa_zdep(lo(rd), imm, imm, lo(rd)), ctx);
		break;
	case BPF_RSH:
		if (imm != 0)
			emit(hppa_shr(lo(rd), imm, lo(rd)), ctx);
		break;
	case BPF_ARSH:
		if (imm != 0)
			emit(hppa_extrws(lo(rd), 31 - imm, imm, lo(rd)), ctx);
		break;
	default:
		WARN_ON(1);
	}

	bpf_put_reg32(dst, rd, ctx);
}

static void emit_alu_r64(const s8 *dst, const s8 *src,
			 struct hppa_jit_context *ctx, const u8 op)
{
	const s8 *tmp1 = regmap[TMP_REG_1];
	const s8 *tmp2 = regmap[TMP_REG_2];
	const s8 *rd;
	const s8 *rs = bpf_get_reg64(src, tmp2, ctx);

	if (op == BPF_MOV)
		rd = bpf_get_reg64_ref(dst, tmp1, false, ctx);
	else
		rd = bpf_get_reg64(dst, tmp1, ctx);

	/* dst = dst OP src */
	switch (op) {
	case BPF_MOV:
		emit_hppa_copy(lo(rs), lo(rd), ctx);
		emit_hppa_copy(hi(rs), hi(rd), ctx);
		break;
	case BPF_ADD:
		emit(hppa_add(lo(rd), lo(rs), lo(rd)), ctx);
		emit(hppa_addc(hi(rd), hi(rs), hi(rd)), ctx);
		break;
	case BPF_SUB:
		emit(hppa_sub(lo(rd), lo(rs), lo(rd)), ctx);
		emit(hppa_subb(hi(rd), hi(rs), hi(rd)), ctx);
		break;
	case BPF_AND:
		emit(hppa_and(lo(rd), lo(rs), lo(rd)), ctx);
		emit(hppa_and(hi(rd), hi(rs), hi(rd)), ctx);
		break;
	case BPF_OR:
		emit(hppa_or(lo(rd), lo(rs), lo(rd)), ctx);
		emit(hppa_or(hi(rd), hi(rs), hi(rd)), ctx);
		break;
	case BPF_XOR:
		emit_hppa_xor(lo(rd), lo(rs), lo(rd), ctx);
		emit_hppa_xor(hi(rd), hi(rs), hi(rd), ctx);
		break;
	case BPF_MUL:
		emit_call_libgcc_ll(__muldi3, rd, rs, op, ctx);
		break;
	case BPF_DIV:
		emit_call_libgcc_ll(&hppa_div64, rd, rs, op, ctx);
		break;
	case BPF_MOD:
		emit_call_libgcc_ll(&hppa_div64_rem, rd, rs, op, ctx);
		break;
	case BPF_LSH:
		emit_call_libgcc_ll(__ashldi3, rd, rs, op, ctx);
		break;
	case BPF_RSH:
		emit_call_libgcc_ll(__lshrdi3, rd, rs, op, ctx);
		break;
	case BPF_ARSH:
		emit_call_libgcc_ll(__ashrdi3, rd, rs, op, ctx);
		break;
	case BPF_NEG:
		emit(hppa_sub(HPPA_REG_ZERO, lo(rd), lo(rd)), ctx);
		emit(hppa_subb(HPPA_REG_ZERO, hi(rd), hi(rd)), ctx);
		break;
	default:
		WARN_ON(1);
	}

	bpf_put_reg64(dst, rd, ctx);
}

static void emit_alu_r32(const s8 *dst, const s8 *src,
			 struct hppa_jit_context *ctx, const u8 op)
{
	const s8 *tmp1 = regmap[TMP_REG_1];
	const s8 *tmp2 = regmap[TMP_REG_2];
	const s8 *rd;
	const s8 *rs = bpf_get_reg32(src, tmp2, ctx);

	if (op == BPF_MOV)
		rd = bpf_get_reg32_ref(dst, tmp1, ctx);
	else
		rd = bpf_get_reg32(dst, tmp1, ctx);

	/* dst = dst OP src */
	switch (op) {
	case BPF_MOV:
		emit_hppa_copy(lo(rs), lo(rd), ctx);
		break;
	case BPF_ADD:
		emit(hppa_add(lo(rd), lo(rs), lo(rd)), ctx);
		break;
	case BPF_SUB:
		emit(hppa_sub(lo(rd), lo(rs), lo(rd)), ctx);
		break;
	case BPF_AND:
		emit(hppa_and(lo(rd), lo(rs), lo(rd)), ctx);
		break;
	case BPF_OR:
		emit(hppa_or(lo(rd), lo(rs), lo(rd)), ctx);
		break;
	case BPF_XOR:
		emit_hppa_xor(lo(rd), lo(rs), lo(rd), ctx);
		break;
	case BPF_MUL:
		emit_call_millicode($$mulI, lo(rd), lo(rs), op, ctx);
		break;
	case BPF_DIV:
		emit_call_millicode($$divU, lo(rd), lo(rs), op, ctx);
		break;
	case BPF_MOD:
		emit_call_millicode($$remU, lo(rd), lo(rs), op, ctx);
		break;
	case BPF_LSH:
		emit(hppa_subi(0x1f, lo(rs), HPPA_REG_T0), ctx);
		emit(hppa_mtsar(HPPA_REG_T0), ctx);
		emit(hppa_depwz_sar(lo(rd), lo(rd)), ctx);
		break;
	case BPF_RSH:
		emit(hppa_mtsar(lo(rs)), ctx);
		emit(hppa_shrpw_sar(lo(rd), lo(rd)), ctx);
		break;
	case BPF_ARSH: /* sign extending arithmetic shift right */
		// emit(hppa_beq(lo(rs), HPPA_REG_ZERO, 2), ctx);
		emit(hppa_subi(0x1f, lo(rs), HPPA_REG_T0), ctx);
		emit(hppa_mtsar(HPPA_REG_T0), ctx);
		emit(hppa_extrws_sar(lo(rd), lo(rd)), ctx);
		break;
	case BPF_NEG:
		emit(hppa_sub(HPPA_REG_ZERO, lo(rd), lo(rd)), ctx);  // sub r0,rd,rd
		break;
	default:
		WARN_ON(1);
	}

	bpf_put_reg32(dst, rd, ctx);
}

static int emit_branch_r64(const s8 *src1, const s8 *src2, s32 paoff,
			   struct hppa_jit_context *ctx, const u8 op)
{
	int e, s = ctx->ninsns;
	const s8 *tmp1 = regmap[TMP_REG_1];
	const s8 *tmp2 = regmap[TMP_REG_2];

	const s8 *rs1 = bpf_get_reg64(src1, tmp1, ctx);
	const s8 *rs2 = bpf_get_reg64(src2, tmp2, ctx);

	/*
	 * NO_JUMP skips over the rest of the instructions and the
	 * emit_jump, meaning the BPF branch is not taken.
	 * JUMP skips directly to the emit_jump, meaning
	 * the BPF branch is taken.
	 *
	 * The fallthrough case results in the BPF branch being taken.
	 */
#define NO_JUMP(idx)	(2 + (idx) - 1)
#define JUMP(idx)	(0 + (idx) - 1)

	switch (op) {
	case BPF_JEQ:
		emit(hppa_bne(hi(rs1), hi(rs2), NO_JUMP(1)), ctx);
		emit(hppa_bne(lo(rs1), lo(rs2), NO_JUMP(0)), ctx);
		break;
	case BPF_JGT:
		emit(hppa_bgtu(hi(rs1), hi(rs2), JUMP(2)), ctx);
		emit(hppa_bltu(hi(rs1), hi(rs2), NO_JUMP(1)), ctx);
		emit(hppa_bleu(lo(rs1), lo(rs2), NO_JUMP(0)), ctx);
		break;
	case BPF_JLT:
		emit(hppa_bltu(hi(rs1), hi(rs2), JUMP(2)), ctx);
		emit(hppa_bgtu(hi(rs1), hi(rs2), NO_JUMP(1)), ctx);
		emit(hppa_bgeu(lo(rs1), lo(rs2), NO_JUMP(0)), ctx);
		break;
	case BPF_JGE:
		emit(hppa_bgtu(hi(rs1), hi(rs2), JUMP(2)), ctx);
		emit(hppa_bltu(hi(rs1), hi(rs2), NO_JUMP(1)), ctx);
		emit(hppa_bltu(lo(rs1), lo(rs2), NO_JUMP(0)), ctx);
		break;
	case BPF_JLE:
		emit(hppa_bltu(hi(rs1), hi(rs2), JUMP(2)), ctx);
		emit(hppa_bgtu(hi(rs1), hi(rs2), NO_JUMP(1)), ctx);
		emit(hppa_bgtu(lo(rs1), lo(rs2), NO_JUMP(0)), ctx);
		break;
	case BPF_JNE:
		emit(hppa_bne(hi(rs1), hi(rs2), JUMP(1)), ctx);
		emit(hppa_beq(lo(rs1), lo(rs2), NO_JUMP(0)), ctx);
		break;
	case BPF_JSGT:
		emit(hppa_bgt(hi(rs1), hi(rs2), JUMP(2)), ctx);
		emit(hppa_blt(hi(rs1), hi(rs2), NO_JUMP(1)), ctx);
		emit(hppa_bleu(lo(rs1), lo(rs2), NO_JUMP(0)), ctx);
		break;
	case BPF_JSLT:
		emit(hppa_blt(hi(rs1), hi(rs2), JUMP(2)), ctx);
		emit(hppa_bgt(hi(rs1), hi(rs2), NO_JUMP(1)), ctx);
		emit(hppa_bgeu(lo(rs1), lo(rs2), NO_JUMP(0)), ctx);
		break;
	case BPF_JSGE:
		emit(hppa_bgt(hi(rs1), hi(rs2), JUMP(2)), ctx);
		emit(hppa_blt(hi(rs1), hi(rs2), NO_JUMP(1)), ctx);
		emit(hppa_bltu(lo(rs1), lo(rs2), NO_JUMP(0)), ctx);
		break;
	case BPF_JSLE:
		emit(hppa_blt(hi(rs1), hi(rs2), JUMP(2)), ctx);
		emit(hppa_bgt(hi(rs1), hi(rs2), NO_JUMP(1)), ctx);
		emit(hppa_bgtu(lo(rs1), lo(rs2), NO_JUMP(0)), ctx);
		break;
	case BPF_JSET:
		emit(hppa_and(hi(rs1), hi(rs2), HPPA_REG_T0), ctx);
		emit(hppa_and(lo(rs1), lo(rs2), HPPA_REG_T1), ctx);
		emit(hppa_bne(HPPA_REG_T0, HPPA_REG_ZERO, JUMP(1)), ctx);
		emit(hppa_beq(HPPA_REG_T1, HPPA_REG_ZERO, NO_JUMP(0)), ctx);
		break;
	default:
		WARN_ON(1);
	}

#undef NO_JUMP
#undef JUMP

	e = ctx->ninsns;
	/* Adjust for extra insns. */
	paoff -= (e - s);
	emit_jump(paoff, true, ctx);
	return 0;
}

static int emit_bcc(u8 op, u8 rd, u8 rs, int paoff, struct hppa_jit_context *ctx)
{
	int e, s;
	bool far = false;
	int off;

	if (op == BPF_JSET) {
		/*
		 * BPF_JSET is a special case: it has no inverse so we always
		 * treat it as a far branch.
		 */
		emit(hppa_and(rd, rs, HPPA_REG_T0), ctx);
		paoff -= 1; /* reduce offset due to hppa_and() above */
		rd = HPPA_REG_T0;
		rs = HPPA_REG_ZERO;
		op = BPF_JNE;
	}

	s = ctx->ninsns;

	if (!relative_bits_ok(paoff - HPPA_BRANCH_DISPLACEMENT, 12)) {
		op = invert_bpf_cond(op);
		far = true;
	}

	/*
	 * For a far branch, the condition is negated and we jump over the
	 * branch itself, and the three instructions from emit_jump.
	 * For a near branch, just use paoff.
	 */
	off = far ? (HPPA_BRANCH_DISPLACEMENT - 1) : paoff - HPPA_BRANCH_DISPLACEMENT;

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
		e = ctx->ninsns;
		/* Adjust for extra insns. */
		paoff -= (e - s);
		emit_jump(paoff, true, ctx);
	}
	return 0;
}

static int emit_branch_r32(const s8 *src1, const s8 *src2, s32 paoff,
			   struct hppa_jit_context *ctx, const u8 op)
{
	int e, s = ctx->ninsns;
	const s8 *tmp1 = regmap[TMP_REG_1];
	const s8 *tmp2 = regmap[TMP_REG_2];

	const s8 *rs1 = bpf_get_reg32(src1, tmp1, ctx);
	const s8 *rs2 = bpf_get_reg32(src2, tmp2, ctx);

	e = ctx->ninsns;
	/* Adjust for extra insns. */
	paoff -= (e - s);

	if (emit_bcc(op, lo(rs1), lo(rs2), paoff, ctx))
		return -1;

	return 0;
}

static void emit_call(bool fixed, u64 addr, struct hppa_jit_context *ctx)
{
	const s8 *tmp = regmap[TMP_REG_1];
	const s8 *r0 = regmap[BPF_REG_0];
	const s8 *reg;
	const int offset_sp = 2 * STACK_ALIGN;

	/* prepare stack */
	emit(hppa_ldo(offset_sp, HPPA_REG_SP, HPPA_REG_SP), ctx);

	/* load R1 & R2 in registers, R3-R5 to stack. */
	reg = bpf_get_reg64_offset(regmap[BPF_REG_5], tmp, offset_sp, ctx);
	emit(hppa_stw(hi(reg), -0x48, HPPA_REG_SP), ctx);
	emit(hppa_stw(lo(reg), -0x44, HPPA_REG_SP), ctx);

	reg = bpf_get_reg64_offset(regmap[BPF_REG_4], tmp, offset_sp, ctx);
	emit(hppa_stw(hi(reg), -0x40, HPPA_REG_SP), ctx);
	emit(hppa_stw(lo(reg), -0x3c, HPPA_REG_SP), ctx);

	reg = bpf_get_reg64_offset(regmap[BPF_REG_3], tmp, offset_sp, ctx);
	emit(hppa_stw(hi(reg), -0x38, HPPA_REG_SP), ctx);
	emit(hppa_stw(lo(reg), -0x34, HPPA_REG_SP), ctx);

	reg = bpf_get_reg64_offset(regmap[BPF_REG_2], tmp, offset_sp, ctx);
	emit_hppa_copy(hi(reg), HPPA_REG_ARG3, ctx);
	emit_hppa_copy(lo(reg), HPPA_REG_ARG2, ctx);

	reg = bpf_get_reg64_offset(regmap[BPF_REG_1], tmp, offset_sp, ctx);
	emit_hppa_copy(hi(reg), HPPA_REG_ARG1, ctx);
	emit_hppa_copy(lo(reg), HPPA_REG_ARG0, ctx);

	/* backup TCC */
	if (REG_WAS_SEEN(ctx, HPPA_REG_TCC))
		emit(hppa_copy(HPPA_REG_TCC, HPPA_REG_TCC_SAVED), ctx);

	/*
	 * Use ldil() to load absolute address. Don't use emit_imm as the
	 * number of emitted instructions should not depend on the value of
	 * addr.
	 */
	emit(hppa_ldil(addr, HPPA_REG_R31), ctx);
	emit(hppa_be_l(im11(addr) >> 2, HPPA_REG_R31, EXEC_NEXT_INSTR), ctx);
	/* set return address in delay slot */
	emit_hppa_copy(HPPA_REG_R31, HPPA_REG_RP, ctx);

	/* restore TCC */
	if (REG_WAS_SEEN(ctx, HPPA_REG_TCC))
		emit(hppa_copy(HPPA_REG_TCC_SAVED, HPPA_REG_TCC), ctx);

	/* restore stack */
	emit(hppa_ldo(-offset_sp, HPPA_REG_SP, HPPA_REG_SP), ctx);

	/* set return value. */
	emit_hppa_copy(HPPA_REG_RET0, hi(r0), ctx);
	emit_hppa_copy(HPPA_REG_RET1, lo(r0), ctx);
}

static int emit_bpf_tail_call(int insn, struct hppa_jit_context *ctx)
{
	/*
	 * R1 -> &ctx
	 * R2 -> &array
	 * R3 -> index
	 */
	int off;
	const s8 *arr_reg = regmap[BPF_REG_2];
	const s8 *idx_reg = regmap[BPF_REG_3];
	struct bpf_array bpfa;
	struct bpf_prog bpfp;

	/* get address of TCC main exit function for error case into rp */
	emit(EXIT_PTR_LOAD(HPPA_REG_RP), ctx);

	/* max_entries = array->map.max_entries; */
	off = offsetof(struct bpf_array, map.max_entries);
	BUILD_BUG_ON(sizeof(bpfa.map.max_entries) != 4);
	emit(hppa_ldw(off, lo(arr_reg), HPPA_REG_T1), ctx);

	/*
	 * if (index >= max_entries)
	 *   goto out;
	 */
	emit(hppa_bltu(lo(idx_reg), HPPA_REG_T1, 2 - HPPA_BRANCH_DISPLACEMENT), ctx);
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
	BUILD_BUG_ON(sizeof(bpfa.ptrs[0]) != 4);
	emit(hppa_sh2add(lo(idx_reg), lo(arr_reg), HPPA_REG_T0), ctx);
	off = offsetof(struct bpf_array, ptrs);
	BUILD_BUG_ON(!relative_bits_ok(off, 11));
	emit(hppa_ldw(off, HPPA_REG_T0, HPPA_REG_T0), ctx);
	emit(hppa_bne(HPPA_REG_T0, HPPA_REG_ZERO, 2 - HPPA_BRANCH_DISPLACEMENT), ctx);
	emit(EXIT_PTR_JUMP(HPPA_REG_RP, NOP_NEXT_INSTR), ctx);

	/*
	 * tcc = temp_tcc;
	 * goto *(prog->bpf_func + 4);
	 */
	off = offsetof(struct bpf_prog, bpf_func);
	BUILD_BUG_ON(!relative_bits_ok(off, 11));
	BUILD_BUG_ON(sizeof(bpfp.bpf_func) != 4);
	emit(hppa_ldw(off, HPPA_REG_T0, HPPA_REG_T0), ctx);
	/* Epilogue jumps to *(t0 + 4). */
	__build_epilogue(true, ctx);
	return 0;
}

static int emit_load_r64(const s8 *dst, const s8 *src, s16 off,
			 struct hppa_jit_context *ctx, const u8 size)
{
	const s8 *tmp1 = regmap[TMP_REG_1];
	const s8 *tmp2 = regmap[TMP_REG_2];
	const s8 *rd = bpf_get_reg64_ref(dst, tmp1, ctx->prog->aux->verifier_zext, ctx);
	const s8 *rs = bpf_get_reg64(src, tmp2, ctx);
	s8 srcreg;

	/* need to calculate address since offset does not fit in 14 bits? */
	if (relative_bits_ok(off, 14))
		srcreg = lo(rs);
	else {
		/* need to use R1 here, since addil puts result into R1 */
		srcreg = HPPA_REG_R1;
		emit(hppa_addil(off, lo(rs)), ctx);
		off = im11(off);
	}

	/* LDX: dst = *(size *)(src + off) */
	switch (size) {
	case BPF_B:
		emit(hppa_ldb(off + 0, srcreg, lo(rd)), ctx);
		if (!ctx->prog->aux->verifier_zext)
			emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
		break;
	case BPF_H:
		emit(hppa_ldh(off + 0, srcreg, lo(rd)), ctx);
		if (!ctx->prog->aux->verifier_zext)
			emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
		break;
	case BPF_W:
		emit(hppa_ldw(off + 0, srcreg, lo(rd)), ctx);
		if (!ctx->prog->aux->verifier_zext)
			emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
		break;
	case BPF_DW:
		emit(hppa_ldw(off + 0, srcreg, hi(rd)), ctx);
		emit(hppa_ldw(off + 4, srcreg, lo(rd)), ctx);
		break;
	}

	bpf_put_reg64(dst, rd, ctx);
	return 0;
}

static int emit_store_r64(const s8 *dst, const s8 *src, s16 off,
			  struct hppa_jit_context *ctx, const u8 size,
			  const u8 mode)
{
	const s8 *tmp1 = regmap[TMP_REG_1];
	const s8 *tmp2 = regmap[TMP_REG_2];
	const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);
	const s8 *rs = bpf_get_reg64(src, tmp2, ctx);
	s8 dstreg;

	/* need to calculate address since offset does not fit in 14 bits? */
	if (relative_bits_ok(off, 14))
		dstreg = lo(rd);
	else {
		/* need to use R1 here, since addil puts result into R1 */
		dstreg = HPPA_REG_R1;
		emit(hppa_addil(off, lo(rd)), ctx);
		off = im11(off);
	}

	/* ST: *(size *)(dst + off) = imm */
	switch (size) {
	case BPF_B:
		emit(hppa_stb(lo(rs), off + 0, dstreg), ctx);
		break;
	case BPF_H:
		emit(hppa_sth(lo(rs), off + 0, dstreg), ctx);
		break;
	case BPF_W:
		emit(hppa_stw(lo(rs), off + 0, dstreg), ctx);
		break;
	case BPF_DW:
		emit(hppa_stw(hi(rs), off + 0, dstreg), ctx);
		emit(hppa_stw(lo(rs), off + 4, dstreg), ctx);
		break;
	}

	return 0;
}

static void emit_rev16(const s8 rd, struct hppa_jit_context *ctx)
{
	emit(hppa_extru(rd, 23, 8, HPPA_REG_T1), ctx);
	emit(hppa_depwz(rd, 23, 8, HPPA_REG_T1), ctx);
	emit(hppa_extru(HPPA_REG_T1, 31, 16, rd), ctx);
}

static void emit_rev32(const s8 rs, const s8 rd, struct hppa_jit_context *ctx)
{
	emit(hppa_shrpw(rs, rs, 16, HPPA_REG_T1), ctx);
	emit(hppa_depwz(HPPA_REG_T1, 15, 8, HPPA_REG_T1), ctx);
	emit(hppa_shrpw(rs, HPPA_REG_T1, 8, rd), ctx);
}

static void emit_zext64(const s8 *dst, struct hppa_jit_context *ctx)
{
	const s8 *rd;
	const s8 *tmp1 = regmap[TMP_REG_1];

	rd = bpf_get_reg64(dst, tmp1, ctx);
	emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
	bpf_put_reg64(dst, rd, ctx);
}

int bpf_jit_emit_insn(const struct bpf_insn *insn, struct hppa_jit_context *ctx,
		      bool extra_pass)
{
	bool is64 = BPF_CLASS(insn->code) == BPF_ALU64 ||
		BPF_CLASS(insn->code) == BPF_JMP;
	int s, e, paoff, i = insn - ctx->prog->insnsi;
	u8 code = insn->code;
	s16 off = insn->off;
	s32 imm = insn->imm;

	const s8 *dst = regmap[insn->dst_reg];
	const s8 *src = regmap[insn->src_reg];
	const s8 *tmp1 = regmap[TMP_REG_1];
	const s8 *tmp2 = regmap[TMP_REG_2];

	if (0) printk("CLASS %03d  CODE %#02x ALU64:%d BPF_SIZE %#02x  "
		"BPF_CODE %#02x  src_reg %d  dst_reg %d\n",
		BPF_CLASS(code), code, (code & BPF_ALU64) ? 1:0, BPF_SIZE(code),
		BPF_OP(code), insn->src_reg, insn->dst_reg);

	switch (code) {
	/* dst = src */
	case BPF_ALU64 | BPF_MOV | BPF_X:

	case BPF_ALU64 | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_K:

	case BPF_ALU64 | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_K:

	case BPF_ALU64 | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:

	case BPF_ALU64 | BPF_MUL | BPF_X:
	case BPF_ALU64 | BPF_MUL | BPF_K:

	case BPF_ALU64 | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_DIV | BPF_K:

	case BPF_ALU64 | BPF_MOD | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_K:

	case BPF_ALU64 | BPF_LSH | BPF_X:
	case BPF_ALU64 | BPF_RSH | BPF_X:
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		if (BPF_SRC(code) == BPF_K) {
			emit_imm32(tmp2, imm, ctx);
			src = tmp2;
		}
		emit_alu_r64(dst, src, ctx, BPF_OP(code));
		break;

	/* dst = -dst */
	case BPF_ALU64 | BPF_NEG:
		emit_alu_r64(dst, tmp2, ctx, BPF_OP(code));
		break;

	case BPF_ALU64 | BPF_MOV | BPF_K:
	case BPF_ALU64 | BPF_AND | BPF_K:
	case BPF_ALU64 | BPF_OR | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_LSH | BPF_K:
	case BPF_ALU64 | BPF_RSH | BPF_K:
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		emit_alu_i64(dst, imm, ctx, BPF_OP(code));
		break;

	case BPF_ALU | BPF_MOV | BPF_X:
		if (imm == 1) {
			/* Special mov32 for zext. */
			emit_zext64(dst, ctx);
			break;
		}
		fallthrough;
	/* dst = dst OP src */
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU | BPF_XOR | BPF_X:

	case BPF_ALU | BPF_MUL | BPF_X:
	case BPF_ALU | BPF_MUL | BPF_K:

	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU | BPF_DIV | BPF_K:

	case BPF_ALU | BPF_MOD | BPF_X:
	case BPF_ALU | BPF_MOD | BPF_K:

	case BPF_ALU | BPF_LSH | BPF_X:
	case BPF_ALU | BPF_RSH | BPF_X:
	case BPF_ALU | BPF_ARSH | BPF_X:
		if (BPF_SRC(code) == BPF_K) {
			emit_imm32(tmp2, imm, ctx);
			src = tmp2;
		}
		emit_alu_r32(dst, src, ctx, BPF_OP(code));
		break;

	/* dst = dst OP imm */
	case BPF_ALU | BPF_MOV | BPF_K:
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU | BPF_OR | BPF_K:
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU | BPF_LSH | BPF_K:
	case BPF_ALU | BPF_RSH | BPF_K:
	case BPF_ALU | BPF_ARSH | BPF_K:
		/*
		 * mul,div,mod are handled in the BPF_X case.
		 */
		emit_alu_i32(dst, imm, ctx, BPF_OP(code));
		break;

	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
		/*
		 * src is ignored---choose tmp2 as a dummy register since it
		 * is not on the stack.
		 */
		emit_alu_r32(dst, tmp2, ctx, BPF_OP(code));
		break;

	/* dst = BSWAP##imm(dst) */
	case BPF_ALU | BPF_END | BPF_FROM_BE:
	{
		const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);

		switch (imm) {
		case 16:
			/* zero-extend 16 bits into 64 bits */
			emit(hppa_extru(lo(rd), 31, 16, lo(rd)), ctx);
			fallthrough;
		case 32:
			/* zero-extend 32 bits into 64 bits */
			if (!ctx->prog->aux->verifier_zext)
				emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
			break;
		case 64:
			/* Do nothing. */
			break;
		default:
			pr_err("bpf-jit: BPF_END imm %d invalid\n", imm);
			return -1;
		}

		bpf_put_reg64(dst, rd, ctx);
		break;
	}

	case BPF_ALU | BPF_END | BPF_FROM_LE:
	{
		const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);

		switch (imm) {
		case 16:
			emit_rev16(lo(rd), ctx);
			if (!ctx->prog->aux->verifier_zext)
				emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
			break;
		case 32:
			emit_rev32(lo(rd), lo(rd), ctx);
			if (!ctx->prog->aux->verifier_zext)
				emit_hppa_copy(HPPA_REG_ZERO, hi(rd), ctx);
			break;
		case 64:
			/* Swap upper and lower halves, then each half. */
			emit_hppa_copy(hi(rd), HPPA_REG_T0, ctx);
			emit_rev32(lo(rd), hi(rd), ctx);
			emit_rev32(HPPA_REG_T0, lo(rd), ctx);
			break;
		default:
			pr_err("bpf-jit: BPF_END imm %d invalid\n", imm);
			return -1;
		}

		bpf_put_reg64(dst, rd, ctx);
		break;
	}
	/* JUMP off */
	case BPF_JMP | BPF_JA:
		paoff = hppa_offset(i, off, ctx);
		emit_jump(paoff, false, ctx);
		break;
	/* function call */
	case BPF_JMP | BPF_CALL:
	{
		bool fixed;
		int ret;
		u64 addr;

		ret = bpf_jit_get_func_addr(ctx->prog, insn, extra_pass, &addr,
					    &fixed);
		if (ret < 0)
			return ret;
		emit_call(fixed, addr, ctx);
		break;
	}
	/* tail call */
	case BPF_JMP | BPF_TAIL_CALL:
		REG_SET_SEEN_ALL(ctx);
		if (emit_bpf_tail_call(i, ctx))
			return -1;
		break;
	/* IF (dst COND imm) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_X:
	case BPF_JMP | BPF_JEQ | BPF_K:
	case BPF_JMP32 | BPF_JEQ | BPF_X:
	case BPF_JMP32 | BPF_JEQ | BPF_K:

	case BPF_JMP | BPF_JNE | BPF_X:
	case BPF_JMP | BPF_JNE | BPF_K:
	case BPF_JMP32 | BPF_JNE | BPF_X:
	case BPF_JMP32 | BPF_JNE | BPF_K:

	case BPF_JMP | BPF_JLE | BPF_X:
	case BPF_JMP | BPF_JLE | BPF_K:
	case BPF_JMP32 | BPF_JLE | BPF_X:
	case BPF_JMP32 | BPF_JLE | BPF_K:

	case BPF_JMP | BPF_JLT | BPF_X:
	case BPF_JMP | BPF_JLT | BPF_K:
	case BPF_JMP32 | BPF_JLT | BPF_X:
	case BPF_JMP32 | BPF_JLT | BPF_K:

	case BPF_JMP | BPF_JGE | BPF_X:
	case BPF_JMP | BPF_JGE | BPF_K:
	case BPF_JMP32 | BPF_JGE | BPF_X:
	case BPF_JMP32 | BPF_JGE | BPF_K:

	case BPF_JMP | BPF_JGT | BPF_X:
	case BPF_JMP | BPF_JGT | BPF_K:
	case BPF_JMP32 | BPF_JGT | BPF_X:
	case BPF_JMP32 | BPF_JGT | BPF_K:

	case BPF_JMP | BPF_JSLE | BPF_X:
	case BPF_JMP | BPF_JSLE | BPF_K:
	case BPF_JMP32 | BPF_JSLE | BPF_X:
	case BPF_JMP32 | BPF_JSLE | BPF_K:

	case BPF_JMP | BPF_JSLT | BPF_X:
	case BPF_JMP | BPF_JSLT | BPF_K:
	case BPF_JMP32 | BPF_JSLT | BPF_X:
	case BPF_JMP32 | BPF_JSLT | BPF_K:

	case BPF_JMP | BPF_JSGE | BPF_X:
	case BPF_JMP | BPF_JSGE | BPF_K:
	case BPF_JMP32 | BPF_JSGE | BPF_X:
	case BPF_JMP32 | BPF_JSGE | BPF_K:

	case BPF_JMP | BPF_JSGT | BPF_X:
	case BPF_JMP | BPF_JSGT | BPF_K:
	case BPF_JMP32 | BPF_JSGT | BPF_X:
	case BPF_JMP32 | BPF_JSGT | BPF_K:

	case BPF_JMP | BPF_JSET | BPF_X:
	case BPF_JMP | BPF_JSET | BPF_K:
	case BPF_JMP32 | BPF_JSET | BPF_X:
	case BPF_JMP32 | BPF_JSET | BPF_K:
		paoff = hppa_offset(i, off, ctx);
		if (BPF_SRC(code) == BPF_K) {
			s = ctx->ninsns;
			emit_imm32(tmp2, imm, ctx);
			src = tmp2;
			e = ctx->ninsns;
			paoff -= (e - s);
		}
		if (is64)
			emit_branch_r64(dst, src, paoff, ctx, BPF_OP(code));
		else
			emit_branch_r32(dst, src, paoff, ctx, BPF_OP(code));
		break;
	/* function return */
	case BPF_JMP | BPF_EXIT:
		if (i == ctx->prog->len - 1)
			break;
		/* load epilogue function pointer and jump to it. */
		emit(EXIT_PTR_LOAD(HPPA_REG_RP), ctx);
		emit(EXIT_PTR_JUMP(HPPA_REG_RP, NOP_NEXT_INSTR), ctx);
		break;

	/* dst = imm64 */
	case BPF_LD | BPF_IMM | BPF_DW:
	{
		struct bpf_insn insn1 = insn[1];
		u32 upper = insn1.imm;
		u32 lower = imm;
		const s8 *rd = bpf_get_reg64_ref(dst, tmp1, false, ctx);

		if (0 && bpf_pseudo_func(insn)) {
			WARN_ON(upper); /* we are 32-bit! */
			upper = 0;
			lower = (uintptr_t) dereference_function_descriptor(lower);
		}

		emit_imm64(rd, upper, lower, ctx);
		bpf_put_reg64(dst, rd, ctx);
		return 1;
	}

	/* LDX: dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_DW:
		if (emit_load_r64(dst, src, off, ctx, BPF_SIZE(code)))
			return -1;
		break;

	/* speculation barrier */
	case BPF_ST | BPF_NOSPEC:
		break;

	/* ST: *(size *)(dst + off) = imm */
	case BPF_ST | BPF_MEM | BPF_B:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_DW:

	case BPF_STX | BPF_MEM | BPF_B:
	case BPF_STX | BPF_MEM | BPF_H:
	case BPF_STX | BPF_MEM | BPF_W:
	case BPF_STX | BPF_MEM | BPF_DW:
		if (BPF_CLASS(code) == BPF_ST) {
			emit_imm32(tmp2, imm, ctx);
			src = tmp2;
		}

		if (emit_store_r64(dst, src, off, ctx, BPF_SIZE(code),
				   BPF_MODE(code)))
			return -1;
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
	const s8 *tmp = regmap[TMP_REG_1];
	const s8 *dst, *reg;
	int stack_adjust = 0;
	int i;
	unsigned long addr;
	int bpf_stack_adjust;

	/*
	 * stack on hppa grows up, so if tail calls are used we need to
	 * allocate the maximum stack size
	 */
	if (REG_ALL_SEEN(ctx))
		bpf_stack_adjust = MAX_BPF_STACK;
	else
		bpf_stack_adjust = ctx->prog->aux->stack_depth;
	bpf_stack_adjust = round_up(bpf_stack_adjust, STACK_ALIGN);

	/* make space for callee-saved registers. */
	stack_adjust += NR_SAVED_REGISTERS * REG_SIZE;
	/* make space for BPF registers on stack. */
	stack_adjust += BPF_JIT_SCRATCH_REGS * REG_SIZE;
	/* make space for BPF stack. */
	stack_adjust += bpf_stack_adjust;
	/* round up for stack alignment. */
	stack_adjust = round_up(stack_adjust, STACK_ALIGN);

	/*
	 * The first instruction sets the tail-call-counter (TCC) register.
	 * This instruction is skipped by tail calls.
	 * Use a temporary register instead of a caller-saved register initially.
	 */
	emit(hppa_ldi(MAX_TAIL_CALL_CNT, HPPA_REG_TCC_IN_INIT), ctx);

	/*
	 * skip all initializations when called as BPF TAIL call.
	 */
	emit(hppa_ldi(MAX_TAIL_CALL_CNT, HPPA_REG_R1), ctx);
	emit(hppa_bne(HPPA_REG_TCC_IN_INIT, HPPA_REG_R1, ctx->prologue_len - 2 - HPPA_BRANCH_DISPLACEMENT), ctx);

	/* set up hppa stack frame. */
	emit_hppa_copy(HPPA_REG_SP, HPPA_REG_R1, ctx);			// copy sp,r1 (=prev_sp)
	emit(hppa_ldo(stack_adjust, HPPA_REG_SP, HPPA_REG_SP), ctx);	// ldo stack_adjust(sp),sp (increase stack)
	emit(hppa_stw(HPPA_REG_R1, -REG_SIZE, HPPA_REG_SP), ctx);	// stw prev_sp,-0x04(sp)
	emit(hppa_stw(HPPA_REG_RP, -0x14, HPPA_REG_SP), ctx);		// stw rp,-0x14(sp)

	REG_FORCE_SEEN(ctx, HPPA_REG_T0);
	REG_FORCE_SEEN(ctx, HPPA_REG_T1);
	REG_FORCE_SEEN(ctx, HPPA_REG_T2);
	REG_FORCE_SEEN(ctx, HPPA_REG_T3);
	REG_FORCE_SEEN(ctx, HPPA_REG_T4);
	REG_FORCE_SEEN(ctx, HPPA_REG_T5);

	/* save callee-save registers. */
	for (i = 3; i <= 18; i++) {
		if (OPTIMIZE_HPPA && !REG_WAS_SEEN(ctx, HPPA_R(i)))
			continue;
		emit(hppa_stw(HPPA_R(i), -REG_SIZE * (8 + (i-3)), HPPA_REG_SP), ctx);	// stw ri,-save_area(sp)
	}

	/*
	 * now really set the tail call counter (TCC) register.
	 */
	if (REG_WAS_SEEN(ctx, HPPA_REG_TCC))
		emit(hppa_ldi(MAX_TAIL_CALL_CNT, HPPA_REG_TCC), ctx);

	/*
	 * save epilogue function pointer for outer TCC call chain.
	 * The main TCC call stores the final RP on stack.
	 */
	addr = (uintptr_t) &ctx->insns[ctx->epilogue_offset];
	/* skip first two instructions of exit function, which jump to exit */
	addr += 2 * HPPA_INSN_SIZE;
	emit(hppa_ldil(addr, HPPA_REG_T2), ctx);
	emit(hppa_ldo(im11(addr), HPPA_REG_T2, HPPA_REG_T2), ctx);
	emit(EXIT_PTR_STORE(HPPA_REG_T2), ctx);

	/* load R1 & R2 from registers, R3-R5 from stack. */
	/* use HPPA_REG_R1 which holds the old stack value */
	dst = regmap[BPF_REG_5];
	reg = bpf_get_reg64_ref(dst, tmp, false, ctx);
	if (REG_WAS_SEEN(ctx, lo(reg)) | REG_WAS_SEEN(ctx, hi(reg))) {
		if (REG_WAS_SEEN(ctx, hi(reg)))
			emit(hppa_ldw(-0x48, HPPA_REG_R1, hi(reg)), ctx);
		if (REG_WAS_SEEN(ctx, lo(reg)))
			emit(hppa_ldw(-0x44, HPPA_REG_R1, lo(reg)), ctx);
		bpf_put_reg64(dst, tmp, ctx);
	}

	dst = regmap[BPF_REG_4];
	reg = bpf_get_reg64_ref(dst, tmp, false, ctx);
	if (REG_WAS_SEEN(ctx, lo(reg)) | REG_WAS_SEEN(ctx, hi(reg))) {
		if (REG_WAS_SEEN(ctx, hi(reg)))
			emit(hppa_ldw(-0x40, HPPA_REG_R1, hi(reg)), ctx);
		if (REG_WAS_SEEN(ctx, lo(reg)))
			emit(hppa_ldw(-0x3c, HPPA_REG_R1, lo(reg)), ctx);
		bpf_put_reg64(dst, tmp, ctx);
	}

	dst = regmap[BPF_REG_3];
	reg = bpf_get_reg64_ref(dst, tmp, false, ctx);
	if (REG_WAS_SEEN(ctx, lo(reg)) | REG_WAS_SEEN(ctx, hi(reg))) {
		if (REG_WAS_SEEN(ctx, hi(reg)))
			emit(hppa_ldw(-0x38, HPPA_REG_R1, hi(reg)), ctx);
		if (REG_WAS_SEEN(ctx, lo(reg)))
			emit(hppa_ldw(-0x34, HPPA_REG_R1, lo(reg)), ctx);
		bpf_put_reg64(dst, tmp, ctx);
	}

	dst = regmap[BPF_REG_2];
	reg = bpf_get_reg64_ref(dst, tmp, false, ctx);
	if (REG_WAS_SEEN(ctx, lo(reg)) | REG_WAS_SEEN(ctx, hi(reg))) {
		if (REG_WAS_SEEN(ctx, hi(reg)))
			emit_hppa_copy(HPPA_REG_ARG3, hi(reg), ctx);
		if (REG_WAS_SEEN(ctx, lo(reg)))
			emit_hppa_copy(HPPA_REG_ARG2, lo(reg), ctx);
		bpf_put_reg64(dst, tmp, ctx);
	}

	dst = regmap[BPF_REG_1];
	reg = bpf_get_reg64_ref(dst, tmp, false, ctx);
	if (REG_WAS_SEEN(ctx, lo(reg)) | REG_WAS_SEEN(ctx, hi(reg))) {
		if (REG_WAS_SEEN(ctx, hi(reg)))
			emit_hppa_copy(HPPA_REG_ARG1, hi(reg), ctx);
		if (REG_WAS_SEEN(ctx, lo(reg)))
			emit_hppa_copy(HPPA_REG_ARG0, lo(reg), ctx);
		bpf_put_reg64(dst, tmp, ctx);
	}

	/* Set up BPF frame pointer. */
	dst = regmap[BPF_REG_FP];
	reg = bpf_get_reg64_ref(dst, tmp, false, ctx);
	if (REG_WAS_SEEN(ctx, lo(reg)) | REG_WAS_SEEN(ctx, hi(reg))) {
		if (REG_WAS_SEEN(ctx, lo(reg)))
			emit(hppa_ldo(-REG_SIZE * (NR_SAVED_REGISTERS + BPF_JIT_SCRATCH_REGS),
				HPPA_REG_SP, lo(reg)), ctx);
		if (REG_WAS_SEEN(ctx, hi(reg)))
			emit_hppa_copy(HPPA_REG_ZERO, hi(reg), ctx);
		bpf_put_reg64(dst, tmp, ctx);
	}

	emit(hppa_nop(), ctx);
}

void bpf_jit_build_epilogue(struct hppa_jit_context *ctx)
{
	__build_epilogue(false, ctx);
}
