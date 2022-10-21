// SPDX-License-Identifier: GPL-2.0-only
/*
 * Just-In-Time compiler for eBPF bytecode on MIPS.
 * Implementation of JIT functions for 64-bit CPUs.
 *
 * Copyright (c) 2021 Anyfi Networks AB.
 * Author: Johan Almbladh <johan.almbladh@gmail.com>
 *
 * Based on code and ideas from
 * Copyright (c) 2017 Cavium, Inc.
 * Copyright (c) 2017 Shubham Bansal <illusionist.neo@gmail.com>
 * Copyright (c) 2011 Mircea Gherzan <mgherzan@gmail.com>
 */

#include <linux/errno.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <asm/cpu-features.h>
#include <asm/isa-rev.h>
#include <asm/uasm.h>

#include "bpf_jit_comp.h"

/* MIPS t0-t3 are not available in the n64 ABI */
#undef MIPS_R_T0
#undef MIPS_R_T1
#undef MIPS_R_T2
#undef MIPS_R_T3

/* Stack is 16-byte aligned in n64 ABI */
#define MIPS_STACK_ALIGNMENT 16

/* Extra 64-bit eBPF registers used by JIT */
#define JIT_REG_TC (MAX_BPF_JIT_REG + 0)
#define JIT_REG_ZX (MAX_BPF_JIT_REG + 1)

/* Number of prologue bytes to skip when doing a tail call */
#define JIT_TCALL_SKIP 4

/* Callee-saved CPU registers that the JIT must preserve */
#define JIT_CALLEE_REGS   \
	(BIT(MIPS_R_S0) | \
	 BIT(MIPS_R_S1) | \
	 BIT(MIPS_R_S2) | \
	 BIT(MIPS_R_S3) | \
	 BIT(MIPS_R_S4) | \
	 BIT(MIPS_R_S5) | \
	 BIT(MIPS_R_S6) | \
	 BIT(MIPS_R_S7) | \
	 BIT(MIPS_R_GP) | \
	 BIT(MIPS_R_FP) | \
	 BIT(MIPS_R_RA))

/* Caller-saved CPU registers available for JIT use */
#define JIT_CALLER_REGS	  \
	(BIT(MIPS_R_A5) | \
	 BIT(MIPS_R_A6) | \
	 BIT(MIPS_R_A7))
/*
 * Mapping of 64-bit eBPF registers to 64-bit native MIPS registers.
 * MIPS registers t4 - t7 may be used by the JIT as temporary registers.
 * MIPS registers t8 - t9 are reserved for single-register common functions.
 */
static const u8 bpf2mips64[] = {
	/* Return value from in-kernel function, and exit value from eBPF */
	[BPF_REG_0] = MIPS_R_V0,
	/* Arguments from eBPF program to in-kernel function */
	[BPF_REG_1] = MIPS_R_A0,
	[BPF_REG_2] = MIPS_R_A1,
	[BPF_REG_3] = MIPS_R_A2,
	[BPF_REG_4] = MIPS_R_A3,
	[BPF_REG_5] = MIPS_R_A4,
	/* Callee-saved registers that in-kernel function will preserve */
	[BPF_REG_6] = MIPS_R_S0,
	[BPF_REG_7] = MIPS_R_S1,
	[BPF_REG_8] = MIPS_R_S2,
	[BPF_REG_9] = MIPS_R_S3,
	/* Read-only frame pointer to access the eBPF stack */
	[BPF_REG_FP] = MIPS_R_FP,
	/* Temporary register for blinding constants */
	[BPF_REG_AX] = MIPS_R_AT,
	/* Tail call count register, caller-saved */
	[JIT_REG_TC] = MIPS_R_A5,
	/* Constant for register zero-extension */
	[JIT_REG_ZX] = MIPS_R_V1,
};

/*
 * MIPS 32-bit operations on 64-bit registers generate a sign-extended
 * result. However, the eBPF ISA mandates zero-extension, so we rely on the
 * verifier to add that for us (emit_zext_ver). In addition, ALU arithmetic
 * operations, right shift and byte swap require properly sign-extended
 * operands or the result is unpredictable. We emit explicit sign-extensions
 * in those cases.
 */

/* Sign extension */
static void emit_sext(struct jit_context *ctx, u8 dst, u8 src)
{
	emit(ctx, sll, dst, src, 0);
	clobber_reg(ctx, dst);
}

/* Zero extension */
static void emit_zext(struct jit_context *ctx, u8 dst)
{
	if (cpu_has_mips64r2 || cpu_has_mips64r6) {
		emit(ctx, dinsu, dst, MIPS_R_ZERO, 32, 32);
	} else {
		emit(ctx, and, dst, dst, bpf2mips64[JIT_REG_ZX]);
		access_reg(ctx, JIT_REG_ZX); /* We need the ZX register */
	}
	clobber_reg(ctx, dst);
}

/* Zero extension, if verifier does not do it for us  */
static void emit_zext_ver(struct jit_context *ctx, u8 dst)
{
	if (!ctx->program->aux->verifier_zext)
		emit_zext(ctx, dst);
}

/* dst = imm (64-bit) */
static void emit_mov_i64(struct jit_context *ctx, u8 dst, u64 imm64)
{
	if (imm64 >= 0xffffffffffff8000ULL || imm64 < 0x8000ULL) {
		emit(ctx, daddiu, dst, MIPS_R_ZERO, (s16)imm64);
	} else if (imm64 >= 0xffffffff80000000ULL ||
		   (imm64 < 0x80000000 && imm64 > 0xffff)) {
		emit(ctx, lui, dst, (s16)(imm64 >> 16));
		emit(ctx, ori, dst, dst, (u16)imm64 & 0xffff);
	} else {
		u8 acc = MIPS_R_ZERO;
		int shift = 0;
		int k;

		for (k = 0; k < 4; k++) {
			u16 half = imm64 >> (48 - 16 * k);

			if (acc == dst)
				shift += 16;

			if (half) {
				if (shift)
					emit(ctx, dsll_safe, dst, dst, shift);
				emit(ctx, ori, dst, acc, half);
				acc = dst;
				shift = 0;
			}
		}
		if (shift)
			emit(ctx, dsll_safe, dst, dst, shift);
	}
	clobber_reg(ctx, dst);
}

/* ALU immediate operation (64-bit) */
static void emit_alu_i64(struct jit_context *ctx, u8 dst, s32 imm, u8 op)
{
	switch (BPF_OP(op)) {
	/* dst = dst | imm */
	case BPF_OR:
		emit(ctx, ori, dst, dst, (u16)imm);
		break;
	/* dst = dst ^ imm */
	case BPF_XOR:
		emit(ctx, xori, dst, dst, (u16)imm);
		break;
	/* dst = -dst */
	case BPF_NEG:
		emit(ctx, dsubu, dst, MIPS_R_ZERO, dst);
		break;
	/* dst = dst << imm */
	case BPF_LSH:
		emit(ctx, dsll_safe, dst, dst, imm);
		break;
	/* dst = dst >> imm */
	case BPF_RSH:
		emit(ctx, dsrl_safe, dst, dst, imm);
		break;
	/* dst = dst >> imm (arithmetic) */
	case BPF_ARSH:
		emit(ctx, dsra_safe, dst, dst, imm);
		break;
	/* dst = dst + imm */
	case BPF_ADD:
		emit(ctx, daddiu, dst, dst, imm);
		break;
	/* dst = dst - imm */
	case BPF_SUB:
		emit(ctx, daddiu, dst, dst, -imm);
		break;
	default:
		/* Width-generic operations */
		emit_alu_i(ctx, dst, imm, op);
	}
	clobber_reg(ctx, dst);
}

/* ALU register operation (64-bit) */
static void emit_alu_r64(struct jit_context *ctx, u8 dst, u8 src, u8 op)
{
	switch (BPF_OP(op)) {
	/* dst = dst << src */
	case BPF_LSH:
		emit(ctx, dsllv, dst, dst, src);
		break;
	/* dst = dst >> src */
	case BPF_RSH:
		emit(ctx, dsrlv, dst, dst, src);
		break;
	/* dst = dst >> src (arithmetic) */
	case BPF_ARSH:
		emit(ctx, dsrav, dst, dst, src);
		break;
	/* dst = dst + src */
	case BPF_ADD:
		emit(ctx, daddu, dst, dst, src);
		break;
	/* dst = dst - src */
	case BPF_SUB:
		emit(ctx, dsubu, dst, dst, src);
		break;
	/* dst = dst * src */
	case BPF_MUL:
		if (cpu_has_mips64r6) {
			emit(ctx, dmulu, dst, dst, src);
		} else {
			emit(ctx, dmultu, dst, src);
			emit(ctx, mflo, dst);
		}
		break;
	/* dst = dst / src */
	case BPF_DIV:
		if (cpu_has_mips64r6) {
			emit(ctx, ddivu_r6, dst, dst, src);
		} else {
			emit(ctx, ddivu, dst, src);
			emit(ctx, mflo, dst);
		}
		break;
	/* dst = dst % src */
	case BPF_MOD:
		if (cpu_has_mips64r6) {
			emit(ctx, dmodu, dst, dst, src);
		} else {
			emit(ctx, ddivu, dst, src);
			emit(ctx, mfhi, dst);
		}
		break;
	default:
		/* Width-generic operations */
		emit_alu_r(ctx, dst, src, op);
	}
	clobber_reg(ctx, dst);
}

/* Swap sub words in a register double word */
static void emit_swap_r64(struct jit_context *ctx, u8 dst, u8 mask, u32 bits)
{
	u8 tmp = MIPS_R_T9;

	emit(ctx, and, tmp, dst, mask);  /* tmp = dst & mask  */
	emit(ctx, dsll, tmp, tmp, bits); /* tmp = tmp << bits */
	emit(ctx, dsrl, dst, dst, bits); /* dst = dst >> bits */
	emit(ctx, and, dst, dst, mask);  /* dst = dst & mask  */
	emit(ctx, or, dst, dst, tmp);    /* dst = dst | tmp   */
}

/* Swap bytes and truncate a register double word, word or half word */
static void emit_bswap_r64(struct jit_context *ctx, u8 dst, u32 width)
{
	switch (width) {
	/* Swap bytes in a double word */
	case 64:
		if (cpu_has_mips64r2 || cpu_has_mips64r6) {
			emit(ctx, dsbh, dst, dst);
			emit(ctx, dshd, dst, dst);
		} else {
			u8 t1 = MIPS_R_T6;
			u8 t2 = MIPS_R_T7;

			emit(ctx, dsll32, t2, dst, 0);  /* t2 = dst << 32    */
			emit(ctx, dsrl32, dst, dst, 0); /* dst = dst >> 32   */
			emit(ctx, or, dst, dst, t2);    /* dst = dst | t2    */

			emit(ctx, ori, t2, MIPS_R_ZERO, 0xffff);
			emit(ctx, dsll32, t1, t2, 0);   /* t1 = t2 << 32     */
			emit(ctx, or, t1, t1, t2);      /* t1 = t1 | t2      */
			emit_swap_r64(ctx, dst, t1, 16);/* dst = swap16(dst) */

			emit(ctx, lui, t2, 0xff);       /* t2 = 0x00ff0000   */
			emit(ctx, ori, t2, t2, 0xff);   /* t2 = t2 | 0x00ff  */
			emit(ctx, dsll32, t1, t2, 0);   /* t1 = t2 << 32     */
			emit(ctx, or, t1, t1, t2);      /* t1 = t1 | t2      */
			emit_swap_r64(ctx, dst, t1, 8); /* dst = swap8(dst)  */
		}
		break;
	/* Swap bytes in a half word */
	/* Swap bytes in a word */
	case 32:
	case 16:
		emit_sext(ctx, dst, dst);
		emit_bswap_r(ctx, dst, width);
		if (cpu_has_mips64r2 || cpu_has_mips64r6)
			emit_zext(ctx, dst);
		break;
	}
	clobber_reg(ctx, dst);
}

/* Truncate a register double word, word or half word */
static void emit_trunc_r64(struct jit_context *ctx, u8 dst, u32 width)
{
	switch (width) {
	case 64:
		break;
	/* Zero-extend a word */
	case 32:
		emit_zext(ctx, dst);
		break;
	/* Zero-extend a half word */
	case 16:
		emit(ctx, andi, dst, dst, 0xffff);
		break;
	}
	clobber_reg(ctx, dst);
}

/* Load operation: dst = *(size*)(src + off) */
static void emit_ldx(struct jit_context *ctx, u8 dst, u8 src, s16 off, u8 size)
{
	switch (size) {
	/* Load a byte */
	case BPF_B:
		emit(ctx, lbu, dst, off, src);
		break;
	/* Load a half word */
	case BPF_H:
		emit(ctx, lhu, dst, off, src);
		break;
	/* Load a word */
	case BPF_W:
		emit(ctx, lwu, dst, off, src);
		break;
	/* Load a double word */
	case BPF_DW:
		emit(ctx, ld, dst, off, src);
		break;
	}
	clobber_reg(ctx, dst);
}

/* Store operation: *(size *)(dst + off) = src */
static void emit_stx(struct jit_context *ctx, u8 dst, u8 src, s16 off, u8 size)
{
	switch (size) {
	/* Store a byte */
	case BPF_B:
		emit(ctx, sb, src, off, dst);
		break;
	/* Store a half word */
	case BPF_H:
		emit(ctx, sh, src, off, dst);
		break;
	/* Store a word */
	case BPF_W:
		emit(ctx, sw, src, off, dst);
		break;
	/* Store a double word */
	case BPF_DW:
		emit(ctx, sd, src, off, dst);
		break;
	}
}

/* Atomic read-modify-write */
static void emit_atomic_r64(struct jit_context *ctx,
			    u8 dst, u8 src, s16 off, u8 code)
{
	u8 t1 = MIPS_R_T6;
	u8 t2 = MIPS_R_T7;

	LLSC_sync(ctx);
	emit(ctx, lld, t1, off, dst);
	switch (code) {
	case BPF_ADD:
	case BPF_ADD | BPF_FETCH:
		emit(ctx, daddu, t2, t1, src);
		break;
	case BPF_AND:
	case BPF_AND | BPF_FETCH:
		emit(ctx, and, t2, t1, src);
		break;
	case BPF_OR:
	case BPF_OR | BPF_FETCH:
		emit(ctx, or, t2, t1, src);
		break;
	case BPF_XOR:
	case BPF_XOR | BPF_FETCH:
		emit(ctx, xor, t2, t1, src);
		break;
	case BPF_XCHG:
		emit(ctx, move, t2, src);
		break;
	}
	emit(ctx, scd, t2, off, dst);
	emit(ctx, LLSC_beqz, t2, -16 - LLSC_offset);
	emit(ctx, nop); /* Delay slot */

	if (code & BPF_FETCH) {
		emit(ctx, move, src, t1);
		clobber_reg(ctx, src);
	}
}

/* Atomic compare-and-exchange */
static void emit_cmpxchg_r64(struct jit_context *ctx, u8 dst, u8 src, s16 off)
{
	u8 r0 = bpf2mips64[BPF_REG_0];
	u8 t1 = MIPS_R_T6;
	u8 t2 = MIPS_R_T7;

	LLSC_sync(ctx);
	emit(ctx, lld, t1, off, dst);
	emit(ctx, bne, t1, r0, 12);
	emit(ctx, move, t2, src);      /* Delay slot */
	emit(ctx, scd, t2, off, dst);
	emit(ctx, LLSC_beqz, t2, -20 - LLSC_offset);
	emit(ctx, move, r0, t1);       /* Delay slot */

	clobber_reg(ctx, r0);
}

/* Function call */
static int emit_call(struct jit_context *ctx, const struct bpf_insn *insn)
{
	u8 zx = bpf2mips64[JIT_REG_ZX];
	u8 tmp = MIPS_R_T6;
	bool fixed;
	u64 addr;

	/* Decode the call address */
	if (bpf_jit_get_func_addr(ctx->program, insn, false,
				  &addr, &fixed) < 0)
		return -1;
	if (!fixed)
		return -1;

	/* Push caller-saved registers on stack */
	push_regs(ctx, ctx->clobbered & JIT_CALLER_REGS, 0, 0);

	/* Emit function call */
	emit_mov_i64(ctx, tmp, addr & JALR_MASK);
	emit(ctx, jalr, MIPS_R_RA, tmp);
	emit(ctx, nop); /* Delay slot */

	/* Restore caller-saved registers */
	pop_regs(ctx, ctx->clobbered & JIT_CALLER_REGS, 0, 0);

	/* Re-initialize the JIT zero-extension register if accessed */
	if (ctx->accessed & BIT(JIT_REG_ZX)) {
		emit(ctx, daddiu, zx, MIPS_R_ZERO, -1);
		emit(ctx, dsrl32, zx, zx, 0);
	}

	clobber_reg(ctx, MIPS_R_RA);
	clobber_reg(ctx, MIPS_R_V0);
	clobber_reg(ctx, MIPS_R_V1);
	return 0;
}

/* Function tail call */
static int emit_tail_call(struct jit_context *ctx)
{
	u8 ary = bpf2mips64[BPF_REG_2];
	u8 ind = bpf2mips64[BPF_REG_3];
	u8 tcc = bpf2mips64[JIT_REG_TC];
	u8 tmp = MIPS_R_T6;
	int off;

	/*
	 * Tail call:
	 * eBPF R1 - function argument (context ptr), passed in a0-a1
	 * eBPF R2 - ptr to object with array of function entry points
	 * eBPF R3 - array index of function to be called
	 */

	/* if (ind >= ary->map.max_entries) goto out */
	off = offsetof(struct bpf_array, map.max_entries);
	if (off > 0x7fff)
		return -1;
	emit(ctx, lwu, tmp, off, ary);            /* tmp = ary->map.max_entrs*/
	emit(ctx, sltu, tmp, ind, tmp);           /* tmp = ind < t1          */
	emit(ctx, beqz, tmp, get_offset(ctx, 1)); /* PC += off(1) if tmp == 0*/

	/* if (--TCC < 0) goto out */
	emit(ctx, daddiu, tcc, tcc, -1);          /* tcc-- (delay slot)      */
	emit(ctx, bltz, tcc, get_offset(ctx, 1)); /* PC += off(1) if tcc < 0 */
						  /* (next insn delay slot)  */
	/* prog = ary->ptrs[ind] */
	off = offsetof(struct bpf_array, ptrs);
	if (off > 0x7fff)
		return -1;
	emit(ctx, dsll, tmp, ind, 3);             /* tmp = ind << 3          */
	emit(ctx, daddu, tmp, tmp, ary);          /* tmp += ary              */
	emit(ctx, ld, tmp, off, tmp);             /* tmp = *(tmp + off)      */

	/* if (prog == 0) goto out */
	emit(ctx, beqz, tmp, get_offset(ctx, 1)); /* PC += off(1) if tmp == 0*/
	emit(ctx, nop);                           /* Delay slot              */

	/* func = prog->bpf_func + 8 (prologue skip offset) */
	off = offsetof(struct bpf_prog, bpf_func);
	if (off > 0x7fff)
		return -1;
	emit(ctx, ld, tmp, off, tmp);                /* tmp = *(tmp + off)   */
	emit(ctx, daddiu, tmp, tmp, JIT_TCALL_SKIP); /* tmp += skip (4)      */

	/* goto func */
	build_epilogue(ctx, tmp);
	access_reg(ctx, JIT_REG_TC);
	return 0;
}

/*
 * Stack frame layout for a JITed program (stack grows down).
 *
 * Higher address  : Previous stack frame      :
 *                 +===========================+  <--- MIPS sp before call
 *                 | Callee-saved registers,   |
 *                 | including RA and FP       |
 *                 +---------------------------+  <--- eBPF FP (MIPS fp)
 *                 | Local eBPF variables      |
 *                 | allocated by program      |
 *                 +---------------------------+
 *                 | Reserved for caller-saved |
 *                 | registers                 |
 * Lower address   +===========================+  <--- MIPS sp
 */

/* Build program prologue to set up the stack and registers */
void build_prologue(struct jit_context *ctx)
{
	u8 fp = bpf2mips64[BPF_REG_FP];
	u8 tc = bpf2mips64[JIT_REG_TC];
	u8 zx = bpf2mips64[JIT_REG_ZX];
	int stack, saved, locals, reserved;

	/*
	 * In the unlikely event that the TCC limit is raised to more
	 * than 16 bits, it is clamped to the maximum value allowed for
	 * the generated code (0xffff). It is better fail to compile
	 * instead of degrading gracefully.
	 */
	BUILD_BUG_ON(MAX_TAIL_CALL_CNT > 0xffff);

	/*
	 * The first instruction initializes the tail call count register.
	 * On a tail call, the calling function jumps into the prologue
	 * after this instruction.
	 */
	emit(ctx, ori, tc, MIPS_R_ZERO, MAX_TAIL_CALL_CNT);

	/* === Entry-point for tail calls === */

	/*
	 * If the eBPF frame pointer and tail call count registers were
	 * accessed they must be preserved. Mark them as clobbered here
	 * to save and restore them on the stack as needed.
	 */
	if (ctx->accessed & BIT(BPF_REG_FP))
		clobber_reg(ctx, fp);
	if (ctx->accessed & BIT(JIT_REG_TC))
		clobber_reg(ctx, tc);
	if (ctx->accessed & BIT(JIT_REG_ZX))
		clobber_reg(ctx, zx);

	/* Compute the stack space needed for callee-saved registers */
	saved = hweight32(ctx->clobbered & JIT_CALLEE_REGS) * sizeof(u64);
	saved = ALIGN(saved, MIPS_STACK_ALIGNMENT);

	/* Stack space used by eBPF program local data */
	locals = ALIGN(ctx->program->aux->stack_depth, MIPS_STACK_ALIGNMENT);

	/*
	 * If we are emitting function calls, reserve extra stack space for
	 * caller-saved registers needed by the JIT. The required space is
	 * computed automatically during resource usage discovery (pass 1).
	 */
	reserved = ctx->stack_used;

	/* Allocate the stack frame */
	stack = ALIGN(saved + locals + reserved, MIPS_STACK_ALIGNMENT);
	if (stack)
		emit(ctx, daddiu, MIPS_R_SP, MIPS_R_SP, -stack);

	/* Store callee-saved registers on stack */
	push_regs(ctx, ctx->clobbered & JIT_CALLEE_REGS, 0, stack - saved);

	/* Initialize the eBPF frame pointer if accessed */
	if (ctx->accessed & BIT(BPF_REG_FP))
		emit(ctx, daddiu, fp, MIPS_R_SP, stack - saved);

	/* Initialize the ePF JIT zero-extension register if accessed */
	if (ctx->accessed & BIT(JIT_REG_ZX)) {
		emit(ctx, daddiu, zx, MIPS_R_ZERO, -1);
		emit(ctx, dsrl32, zx, zx, 0);
	}

	ctx->saved_size = saved;
	ctx->stack_size = stack;
}

/* Build the program epilogue to restore the stack and registers */
void build_epilogue(struct jit_context *ctx, int dest_reg)
{
	/* Restore callee-saved registers from stack */
	pop_regs(ctx, ctx->clobbered & JIT_CALLEE_REGS, 0,
		 ctx->stack_size - ctx->saved_size);

	/* Release the stack frame */
	if (ctx->stack_size)
		emit(ctx, daddiu, MIPS_R_SP, MIPS_R_SP, ctx->stack_size);

	/* Jump to return address and sign-extend the 32-bit return value */
	emit(ctx, jr, dest_reg);
	emit(ctx, sll, MIPS_R_V0, MIPS_R_V0, 0); /* Delay slot */
}

/* Build one eBPF instruction */
int build_insn(const struct bpf_insn *insn, struct jit_context *ctx)
{
	u8 dst = bpf2mips64[insn->dst_reg];
	u8 src = bpf2mips64[insn->src_reg];
	u8 res = bpf2mips64[BPF_REG_0];
	u8 code = insn->code;
	s16 off = insn->off;
	s32 imm = insn->imm;
	s32 val, rel;
	u8 alu, jmp;

	switch (code) {
	/* ALU operations */
	/* dst = imm */
	case BPF_ALU | BPF_MOV | BPF_K:
		emit_mov_i(ctx, dst, imm);
		emit_zext_ver(ctx, dst);
		break;
	/* dst = src */
	case BPF_ALU | BPF_MOV | BPF_X:
		if (imm == 1) {
			/* Special mov32 for zext */
			emit_zext(ctx, dst);
		} else {
			emit_mov_r(ctx, dst, src);
			emit_zext_ver(ctx, dst);
		}
		break;
	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
		emit_sext(ctx, dst, dst);
		emit_alu_i(ctx, dst, 0, BPF_NEG);
		emit_zext_ver(ctx, dst);
		break;
	/* dst = dst & imm */
	/* dst = dst | imm */
	/* dst = dst ^ imm */
	/* dst = dst << imm */
	case BPF_ALU | BPF_OR | BPF_K:
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU | BPF_LSH | BPF_K:
		if (!valid_alu_i(BPF_OP(code), imm)) {
			emit_mov_i(ctx, MIPS_R_T4, imm);
			emit_alu_r(ctx, dst, MIPS_R_T4, BPF_OP(code));
		} else if (rewrite_alu_i(BPF_OP(code), imm, &alu, &val)) {
			emit_alu_i(ctx, dst, val, alu);
		}
		emit_zext_ver(ctx, dst);
		break;
	/* dst = dst >> imm */
	/* dst = dst >> imm (arithmetic) */
	/* dst = dst + imm */
	/* dst = dst - imm */
	/* dst = dst * imm */
	/* dst = dst / imm */
	/* dst = dst % imm */
	case BPF_ALU | BPF_RSH | BPF_K:
	case BPF_ALU | BPF_ARSH | BPF_K:
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU | BPF_MOD | BPF_K:
		if (!valid_alu_i(BPF_OP(code), imm)) {
			emit_sext(ctx, dst, dst);
			emit_mov_i(ctx, MIPS_R_T4, imm);
			emit_alu_r(ctx, dst, MIPS_R_T4, BPF_OP(code));
		} else if (rewrite_alu_i(BPF_OP(code), imm, &alu, &val)) {
			emit_sext(ctx, dst, dst);
			emit_alu_i(ctx, dst, val, alu);
		}
		emit_zext_ver(ctx, dst);
		break;
	/* dst = dst & src */
	/* dst = dst | src */
	/* dst = dst ^ src */
	/* dst = dst << src */
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU | BPF_XOR | BPF_X:
	case BPF_ALU | BPF_LSH | BPF_X:
		emit_alu_r(ctx, dst, src, BPF_OP(code));
		emit_zext_ver(ctx, dst);
		break;
	/* dst = dst >> src */
	/* dst = dst >> src (arithmetic) */
	/* dst = dst + src */
	/* dst = dst - src */
	/* dst = dst * src */
	/* dst = dst / src */
	/* dst = dst % src */
	case BPF_ALU | BPF_RSH | BPF_X:
	case BPF_ALU | BPF_ARSH | BPF_X:
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU | BPF_MUL | BPF_X:
	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU | BPF_MOD | BPF_X:
		emit_sext(ctx, dst, dst);
		emit_sext(ctx, MIPS_R_T4, src);
		emit_alu_r(ctx, dst, MIPS_R_T4, BPF_OP(code));
		emit_zext_ver(ctx, dst);
		break;
	/* dst = imm (64-bit) */
	case BPF_ALU64 | BPF_MOV | BPF_K:
		emit_mov_i(ctx, dst, imm);
		break;
	/* dst = src (64-bit) */
	case BPF_ALU64 | BPF_MOV | BPF_X:
		emit_mov_r(ctx, dst, src);
		break;
	/* dst = -dst (64-bit) */
	case BPF_ALU64 | BPF_NEG:
		emit_alu_i64(ctx, dst, 0, BPF_NEG);
		break;
	/* dst = dst & imm (64-bit) */
	/* dst = dst | imm (64-bit) */
	/* dst = dst ^ imm (64-bit) */
	/* dst = dst << imm (64-bit) */
	/* dst = dst >> imm (64-bit) */
	/* dst = dst >> imm ((64-bit, arithmetic) */
	/* dst = dst + imm (64-bit) */
	/* dst = dst - imm (64-bit) */
	/* dst = dst * imm (64-bit) */
	/* dst = dst / imm (64-bit) */
	/* dst = dst % imm (64-bit) */
	case BPF_ALU64 | BPF_AND | BPF_K:
	case BPF_ALU64 | BPF_OR | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_LSH | BPF_K:
	case BPF_ALU64 | BPF_RSH | BPF_K:
	case BPF_ALU64 | BPF_ARSH | BPF_K:
	case BPF_ALU64 | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
	case BPF_ALU64 | BPF_MUL | BPF_K:
	case BPF_ALU64 | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		if (!valid_alu_i(BPF_OP(code), imm)) {
			emit_mov_i(ctx, MIPS_R_T4, imm);
			emit_alu_r64(ctx, dst, MIPS_R_T4, BPF_OP(code));
		} else if (rewrite_alu_i(BPF_OP(code), imm, &alu, &val)) {
			emit_alu_i64(ctx, dst, val, alu);
		}
		break;
	/* dst = dst & src (64-bit) */
	/* dst = dst | src (64-bit) */
	/* dst = dst ^ src (64-bit) */
	/* dst = dst << src (64-bit) */
	/* dst = dst >> src (64-bit) */
	/* dst = dst >> src (64-bit, arithmetic) */
	/* dst = dst + src (64-bit) */
	/* dst = dst - src (64-bit) */
	/* dst = dst * src (64-bit) */
	/* dst = dst / src (64-bit) */
	/* dst = dst % src (64-bit) */
	case BPF_ALU64 | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_LSH | BPF_X:
	case BPF_ALU64 | BPF_RSH | BPF_X:
	case BPF_ALU64 | BPF_ARSH | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_MUL | BPF_X:
	case BPF_ALU64 | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
		emit_alu_r64(ctx, dst, src, BPF_OP(code));
		break;
	/* dst = htole(dst) */
	/* dst = htobe(dst) */
	case BPF_ALU | BPF_END | BPF_FROM_LE:
	case BPF_ALU | BPF_END | BPF_FROM_BE:
		if (BPF_SRC(code) ==
#ifdef __BIG_ENDIAN
		    BPF_FROM_LE
#else
		    BPF_FROM_BE
#endif
		    )
			emit_bswap_r64(ctx, dst, imm);
		else
			emit_trunc_r64(ctx, dst, imm);
		break;
	/* dst = imm64 */
	case BPF_LD | BPF_IMM | BPF_DW:
		emit_mov_i64(ctx, dst, (u32)imm | ((u64)insn[1].imm << 32));
		return 1;
	/* LDX: dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_DW:
		emit_ldx(ctx, dst, src, off, BPF_SIZE(code));
		break;
	/* ST: *(size *)(dst + off) = imm */
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_B:
	case BPF_ST | BPF_MEM | BPF_DW:
		emit_mov_i(ctx, MIPS_R_T4, imm);
		emit_stx(ctx, dst, MIPS_R_T4, off, BPF_SIZE(code));
		break;
	/* STX: *(size *)(dst + off) = src */
	case BPF_STX | BPF_MEM | BPF_W:
	case BPF_STX | BPF_MEM | BPF_H:
	case BPF_STX | BPF_MEM | BPF_B:
	case BPF_STX | BPF_MEM | BPF_DW:
		emit_stx(ctx, dst, src, off, BPF_SIZE(code));
		break;
	/* Speculation barrier */
	case BPF_ST | BPF_NOSPEC:
		break;
	/* Atomics */
	case BPF_STX | BPF_ATOMIC | BPF_W:
	case BPF_STX | BPF_ATOMIC | BPF_DW:
		switch (imm) {
		case BPF_ADD:
		case BPF_ADD | BPF_FETCH:
		case BPF_AND:
		case BPF_AND | BPF_FETCH:
		case BPF_OR:
		case BPF_OR | BPF_FETCH:
		case BPF_XOR:
		case BPF_XOR | BPF_FETCH:
		case BPF_XCHG:
			if (BPF_SIZE(code) == BPF_DW) {
				emit_atomic_r64(ctx, dst, src, off, imm);
			} else if (imm & BPF_FETCH) {
				u8 tmp = dst;

				if (src == dst) { /* Don't overwrite dst */
					emit_mov_r(ctx, MIPS_R_T4, dst);
					tmp = MIPS_R_T4;
				}
				emit_sext(ctx, src, src);
				emit_atomic_r(ctx, tmp, src, off, imm);
				emit_zext_ver(ctx, src);
			} else { /* 32-bit, no fetch */
				emit_sext(ctx, MIPS_R_T4, src);
				emit_atomic_r(ctx, dst, MIPS_R_T4, off, imm);
			}
			break;
		case BPF_CMPXCHG:
			if (BPF_SIZE(code) == BPF_DW) {
				emit_cmpxchg_r64(ctx, dst, src, off);
			} else {
				u8 tmp = res;

				if (res == dst)   /* Don't overwrite dst */
					tmp = MIPS_R_T4;
				emit_sext(ctx, tmp, res);
				emit_sext(ctx, MIPS_R_T5, src);
				emit_cmpxchg_r(ctx, dst, MIPS_R_T5, tmp, off);
				if (res == dst)   /* Restore result */
					emit_mov_r(ctx, res, MIPS_R_T4);
				/* Result zext inserted by verifier */
			}
			break;
		default:
			goto notyet;
		}
		break;
	/* PC += off if dst == src */
	/* PC += off if dst != src */
	/* PC += off if dst & src */
	/* PC += off if dst > src */
	/* PC += off if dst >= src */
	/* PC += off if dst < src */
	/* PC += off if dst <= src */
	/* PC += off if dst > src (signed) */
	/* PC += off if dst >= src (signed) */
	/* PC += off if dst < src (signed) */
	/* PC += off if dst <= src (signed) */
	case BPF_JMP32 | BPF_JEQ | BPF_X:
	case BPF_JMP32 | BPF_JNE | BPF_X:
	case BPF_JMP32 | BPF_JSET | BPF_X:
	case BPF_JMP32 | BPF_JGT | BPF_X:
	case BPF_JMP32 | BPF_JGE | BPF_X:
	case BPF_JMP32 | BPF_JLT | BPF_X:
	case BPF_JMP32 | BPF_JLE | BPF_X:
	case BPF_JMP32 | BPF_JSGT | BPF_X:
	case BPF_JMP32 | BPF_JSGE | BPF_X:
	case BPF_JMP32 | BPF_JSLT | BPF_X:
	case BPF_JMP32 | BPF_JSLE | BPF_X:
		if (off == 0)
			break;
		setup_jmp_r(ctx, dst == src, BPF_OP(code), off, &jmp, &rel);
		emit_sext(ctx, MIPS_R_T4, dst); /* Sign-extended dst */
		emit_sext(ctx, MIPS_R_T5, src); /* Sign-extended src */
		emit_jmp_r(ctx, MIPS_R_T4, MIPS_R_T5, rel, jmp);
		if (finish_jmp(ctx, jmp, off) < 0)
			goto toofar;
		break;
	/* PC += off if dst == imm */
	/* PC += off if dst != imm */
	/* PC += off if dst & imm */
	/* PC += off if dst > imm */
	/* PC += off if dst >= imm */
	/* PC += off if dst < imm */
	/* PC += off if dst <= imm */
	/* PC += off if dst > imm (signed) */
	/* PC += off if dst >= imm (signed) */
	/* PC += off if dst < imm (signed) */
	/* PC += off if dst <= imm (signed) */
	case BPF_JMP32 | BPF_JEQ | BPF_K:
	case BPF_JMP32 | BPF_JNE | BPF_K:
	case BPF_JMP32 | BPF_JSET | BPF_K:
	case BPF_JMP32 | BPF_JGT | BPF_K:
	case BPF_JMP32 | BPF_JGE | BPF_K:
	case BPF_JMP32 | BPF_JLT | BPF_K:
	case BPF_JMP32 | BPF_JLE | BPF_K:
	case BPF_JMP32 | BPF_JSGT | BPF_K:
	case BPF_JMP32 | BPF_JSGE | BPF_K:
	case BPF_JMP32 | BPF_JSLT | BPF_K:
	case BPF_JMP32 | BPF_JSLE | BPF_K:
		if (off == 0)
			break;
		setup_jmp_i(ctx, imm, 32, BPF_OP(code), off, &jmp, &rel);
		emit_sext(ctx, MIPS_R_T4, dst); /* Sign-extended dst */
		if (valid_jmp_i(jmp, imm)) {
			emit_jmp_i(ctx, MIPS_R_T4, imm, rel, jmp);
		} else {
			/* Move large immediate to register, sign-extended */
			emit_mov_i(ctx, MIPS_R_T5, imm);
			emit_jmp_r(ctx, MIPS_R_T4, MIPS_R_T5, rel, jmp);
		}
		if (finish_jmp(ctx, jmp, off) < 0)
			goto toofar;
		break;
	/* PC += off if dst == src */
	/* PC += off if dst != src */
	/* PC += off if dst & src */
	/* PC += off if dst > src */
	/* PC += off if dst >= src */
	/* PC += off if dst < src */
	/* PC += off if dst <= src */
	/* PC += off if dst > src (signed) */
	/* PC += off if dst >= src (signed) */
	/* PC += off if dst < src (signed) */
	/* PC += off if dst <= src (signed) */
	case BPF_JMP | BPF_JEQ | BPF_X:
	case BPF_JMP | BPF_JNE | BPF_X:
	case BPF_JMP | BPF_JSET | BPF_X:
	case BPF_JMP | BPF_JGT | BPF_X:
	case BPF_JMP | BPF_JGE | BPF_X:
	case BPF_JMP | BPF_JLT | BPF_X:
	case BPF_JMP | BPF_JLE | BPF_X:
	case BPF_JMP | BPF_JSGT | BPF_X:
	case BPF_JMP | BPF_JSGE | BPF_X:
	case BPF_JMP | BPF_JSLT | BPF_X:
	case BPF_JMP | BPF_JSLE | BPF_X:
		if (off == 0)
			break;
		setup_jmp_r(ctx, dst == src, BPF_OP(code), off, &jmp, &rel);
		emit_jmp_r(ctx, dst, src, rel, jmp);
		if (finish_jmp(ctx, jmp, off) < 0)
			goto toofar;
		break;
	/* PC += off if dst == imm */
	/* PC += off if dst != imm */
	/* PC += off if dst & imm */
	/* PC += off if dst > imm */
	/* PC += off if dst >= imm */
	/* PC += off if dst < imm */
	/* PC += off if dst <= imm */
	/* PC += off if dst > imm (signed) */
	/* PC += off if dst >= imm (signed) */
	/* PC += off if dst < imm (signed) */
	/* PC += off if dst <= imm (signed) */
	case BPF_JMP | BPF_JEQ | BPF_K:
	case BPF_JMP | BPF_JNE | BPF_K:
	case BPF_JMP | BPF_JSET | BPF_K:
	case BPF_JMP | BPF_JGT | BPF_K:
	case BPF_JMP | BPF_JGE | BPF_K:
	case BPF_JMP | BPF_JLT | BPF_K:
	case BPF_JMP | BPF_JLE | BPF_K:
	case BPF_JMP | BPF_JSGT | BPF_K:
	case BPF_JMP | BPF_JSGE | BPF_K:
	case BPF_JMP | BPF_JSLT | BPF_K:
	case BPF_JMP | BPF_JSLE | BPF_K:
		if (off == 0)
			break;
		setup_jmp_i(ctx, imm, 64, BPF_OP(code), off, &jmp, &rel);
		if (valid_jmp_i(jmp, imm)) {
			emit_jmp_i(ctx, dst, imm, rel, jmp);
		} else {
			/* Move large immediate to register */
			emit_mov_i(ctx, MIPS_R_T4, imm);
			emit_jmp_r(ctx, dst, MIPS_R_T4, rel, jmp);
		}
		if (finish_jmp(ctx, jmp, off) < 0)
			goto toofar;
		break;
	/* PC += off */
	case BPF_JMP | BPF_JA:
		if (off == 0)
			break;
		if (emit_ja(ctx, off) < 0)
			goto toofar;
		break;
	/* Tail call */
	case BPF_JMP | BPF_TAIL_CALL:
		if (emit_tail_call(ctx) < 0)
			goto invalid;
		break;
	/* Function call */
	case BPF_JMP | BPF_CALL:
		if (emit_call(ctx, insn) < 0)
			goto invalid;
		break;
	/* Function return */
	case BPF_JMP | BPF_EXIT:
		/*
		 * Optimization: when last instruction is EXIT
		 * simply continue to epilogue.
		 */
		if (ctx->bpf_index == ctx->program->len - 1)
			break;
		if (emit_exit(ctx) < 0)
			goto toofar;
		break;

	default:
invalid:
		pr_err_once("unknown opcode %02x\n", code);
		return -EINVAL;
notyet:
		pr_info_once("*** NOT YET: opcode %02x ***\n", code);
		return -EFAULT;
toofar:
		pr_info_once("*** TOO FAR: jump at %u opcode %02x ***\n",
			     ctx->bpf_index, code);
		return -E2BIG;
	}
	return 0;
}
