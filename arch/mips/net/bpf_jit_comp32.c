// SPDX-License-Identifier: GPL-2.0-only
/*
 * Just-In-Time compiler for eBPF bytecode on MIPS.
 * Implementation of JIT functions for 32-bit CPUs.
 *
 * Copyright (c) 2021 Anyfi Networks AB.
 * Author: Johan Almbladh <johan.almbladh@gmail.com>
 *
 * Based on code and ideas from
 * Copyright (c) 2017 Cavium, Inc.
 * Copyright (c) 2017 Shubham Bansal <illusionist.neo@gmail.com>
 * Copyright (c) 2011 Mircea Gherzan <mgherzan@gmail.com>
 */

#include <linux/math64.h>
#include <linux/errno.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <asm/cpu-features.h>
#include <asm/isa-rev.h>
#include <asm/uasm.h>

#include "bpf_jit_comp.h"

/* MIPS a4-a7 are not available in the o32 ABI */
#undef MIPS_R_A4
#undef MIPS_R_A5
#undef MIPS_R_A6
#undef MIPS_R_A7

/* Stack is 8-byte aligned in o32 ABI */
#define MIPS_STACK_ALIGNMENT 8

/*
 * The top 16 bytes of a stack frame is reserved for the callee in O32 ABI.
 * This corresponds to stack space for register arguments a0-a3.
 */
#define JIT_RESERVED_STACK 16

/* Temporary 64-bit register used by JIT */
#define JIT_REG_TMP MAX_BPF_JIT_REG

/*
 * Number of prologue bytes to skip when doing a tail call.
 * Tail call count (TCC) initialization (8 bytes) always, plus
 * R0-to-v0 assignment (4 bytes) if big endian.
 */
#ifdef __BIG_ENDIAN
#define JIT_TCALL_SKIP 12
#else
#define JIT_TCALL_SKIP 8
#endif

/* CPU registers holding the callee return value */
#define JIT_RETURN_REGS	  \
	(BIT(MIPS_R_V0) | \
	 BIT(MIPS_R_V1))

/* CPU registers arguments passed to callee directly */
#define JIT_ARG_REGS      \
	(BIT(MIPS_R_A0) | \
	 BIT(MIPS_R_A1) | \
	 BIT(MIPS_R_A2) | \
	 BIT(MIPS_R_A3))

/* CPU register arguments passed to callee on stack */
#define JIT_STACK_REGS    \
	(BIT(MIPS_R_T0) | \
	 BIT(MIPS_R_T1) | \
	 BIT(MIPS_R_T2) | \
	 BIT(MIPS_R_T3) | \
	 BIT(MIPS_R_T4) | \
	 BIT(MIPS_R_T5))

/* Caller-saved CPU registers */
#define JIT_CALLER_REGS    \
	(JIT_RETURN_REGS | \
	 JIT_ARG_REGS    | \
	 JIT_STACK_REGS)

/* Callee-saved CPU registers */
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

/*
 * Mapping of 64-bit eBPF registers to 32-bit native MIPS registers.
 *
 * 1) Native register pairs are ordered according to CPU endiannes, following
 *    the MIPS convention for passing 64-bit arguments and return values.
 * 2) The eBPF return value, arguments and callee-saved registers are mapped
 *    to their native MIPS equivalents.
 * 3) Since the 32 highest bits in the eBPF FP register are always zero,
 *    only one general-purpose register is actually needed for the mapping.
 *    We use the fp register for this purpose, and map the highest bits to
 *    the MIPS register r0 (zero).
 * 4) We use the MIPS gp and at registers as internal temporary registers
 *    for constant blinding. The gp register is callee-saved.
 * 5) One 64-bit temporary register is mapped for use when sign-extending
 *    immediate operands. MIPS registers t6-t9 are available to the JIT
 *    for as temporaries when implementing complex 64-bit operations.
 *
 * With this scheme all eBPF registers are being mapped to native MIPS
 * registers without having to use any stack scratch space. The direct
 * register mapping (2) simplifies the handling of function calls.
 */
static const u8 bpf2mips32[][2] = {
	/* Return value from in-kernel function, and exit value from eBPF */
	[BPF_REG_0] = {MIPS_R_V1, MIPS_R_V0},
	/* Arguments from eBPF program to in-kernel function */
	[BPF_REG_1] = {MIPS_R_A1, MIPS_R_A0},
	[BPF_REG_2] = {MIPS_R_A3, MIPS_R_A2},
	/* Remaining arguments, to be passed on the stack per O32 ABI */
	[BPF_REG_3] = {MIPS_R_T1, MIPS_R_T0},
	[BPF_REG_4] = {MIPS_R_T3, MIPS_R_T2},
	[BPF_REG_5] = {MIPS_R_T5, MIPS_R_T4},
	/* Callee-saved registers that in-kernel function will preserve */
	[BPF_REG_6] = {MIPS_R_S1, MIPS_R_S0},
	[BPF_REG_7] = {MIPS_R_S3, MIPS_R_S2},
	[BPF_REG_8] = {MIPS_R_S5, MIPS_R_S4},
	[BPF_REG_9] = {MIPS_R_S7, MIPS_R_S6},
	/* Read-only frame pointer to access the eBPF stack */
#ifdef __BIG_ENDIAN
	[BPF_REG_FP] = {MIPS_R_FP, MIPS_R_ZERO},
#else
	[BPF_REG_FP] = {MIPS_R_ZERO, MIPS_R_FP},
#endif
	/* Temporary register for blinding constants */
	[BPF_REG_AX] = {MIPS_R_GP, MIPS_R_AT},
	/* Temporary register for internal JIT use */
	[JIT_REG_TMP] = {MIPS_R_T7, MIPS_R_T6},
};

/* Get low CPU register for a 64-bit eBPF register mapping */
static inline u8 lo(const u8 reg[])
{
#ifdef __BIG_ENDIAN
	return reg[0];
#else
	return reg[1];
#endif
}

/* Get high CPU register for a 64-bit eBPF register mapping */
static inline u8 hi(const u8 reg[])
{
#ifdef __BIG_ENDIAN
	return reg[1];
#else
	return reg[0];
#endif
}

/*
 * Mark a 64-bit CPU register pair as clobbered, it needs to be
 * saved/restored by the program if callee-saved.
 */
static void clobber_reg64(struct jit_context *ctx, const u8 reg[])
{
	clobber_reg(ctx, reg[0]);
	clobber_reg(ctx, reg[1]);
}

/* dst = imm (sign-extended) */
static void emit_mov_se_i64(struct jit_context *ctx, const u8 dst[], s32 imm)
{
	emit_mov_i(ctx, lo(dst), imm);
	if (imm < 0)
		emit(ctx, addiu, hi(dst), MIPS_R_ZERO, -1);
	else
		emit(ctx, move, hi(dst), MIPS_R_ZERO);
	clobber_reg64(ctx, dst);
}

/* Zero extension, if verifier does not do it for us  */
static void emit_zext_ver(struct jit_context *ctx, const u8 dst[])
{
	if (!ctx->program->aux->verifier_zext) {
		emit(ctx, move, hi(dst), MIPS_R_ZERO);
		clobber_reg(ctx, hi(dst));
	}
}

/* Load delay slot, if ISA mandates it */
static void emit_load_delay(struct jit_context *ctx)
{
	if (!cpu_has_mips_2_3_4_5_r)
		emit(ctx, nop);
}

/* ALU immediate operation (64-bit) */
static void emit_alu_i64(struct jit_context *ctx,
			 const u8 dst[], s32 imm, u8 op)
{
	u8 src = MIPS_R_T6;

	/*
	 * ADD/SUB with all but the max negative imm can be handled by
	 * inverting the operation and the imm value, saving one insn.
	 */
	if (imm > S32_MIN && imm < 0)
		switch (op) {
		case BPF_ADD:
			op = BPF_SUB;
			imm = -imm;
			break;
		case BPF_SUB:
			op = BPF_ADD;
			imm = -imm;
			break;
		}

	/* Move immediate to temporary register */
	emit_mov_i(ctx, src, imm);

	switch (op) {
	/* dst = dst + imm */
	case BPF_ADD:
		emit(ctx, addu, lo(dst), lo(dst), src);
		emit(ctx, sltu, MIPS_R_T9, lo(dst), src);
		emit(ctx, addu, hi(dst), hi(dst), MIPS_R_T9);
		if (imm < 0)
			emit(ctx, addiu, hi(dst), hi(dst), -1);
		break;
	/* dst = dst - imm */
	case BPF_SUB:
		emit(ctx, sltu, MIPS_R_T9, lo(dst), src);
		emit(ctx, subu, lo(dst), lo(dst), src);
		emit(ctx, subu, hi(dst), hi(dst), MIPS_R_T9);
		if (imm < 0)
			emit(ctx, addiu, hi(dst), hi(dst), 1);
		break;
	/* dst = dst | imm */
	case BPF_OR:
		emit(ctx, or, lo(dst), lo(dst), src);
		if (imm < 0)
			emit(ctx, addiu, hi(dst), MIPS_R_ZERO, -1);
		break;
	/* dst = dst & imm */
	case BPF_AND:
		emit(ctx, and, lo(dst), lo(dst), src);
		if (imm >= 0)
			emit(ctx, move, hi(dst), MIPS_R_ZERO);
		break;
	/* dst = dst ^ imm */
	case BPF_XOR:
		emit(ctx, xor, lo(dst), lo(dst), src);
		if (imm < 0) {
			emit(ctx, subu, hi(dst), MIPS_R_ZERO, hi(dst));
			emit(ctx, addiu, hi(dst), hi(dst), -1);
		}
		break;
	}
	clobber_reg64(ctx, dst);
}

/* ALU register operation (64-bit) */
static void emit_alu_r64(struct jit_context *ctx,
			 const u8 dst[], const u8 src[], u8 op)
{
	switch (BPF_OP(op)) {
	/* dst = dst + src */
	case BPF_ADD:
		if (src == dst) {
			emit(ctx, srl, MIPS_R_T9, lo(dst), 31);
			emit(ctx, addu, lo(dst), lo(dst), lo(dst));
		} else {
			emit(ctx, addu, lo(dst), lo(dst), lo(src));
			emit(ctx, sltu, MIPS_R_T9, lo(dst), lo(src));
		}
		emit(ctx, addu, hi(dst), hi(dst), hi(src));
		emit(ctx, addu, hi(dst), hi(dst), MIPS_R_T9);
		break;
	/* dst = dst - src */
	case BPF_SUB:
		emit(ctx, sltu, MIPS_R_T9, lo(dst), lo(src));
		emit(ctx, subu, lo(dst), lo(dst), lo(src));
		emit(ctx, subu, hi(dst), hi(dst), hi(src));
		emit(ctx, subu, hi(dst), hi(dst), MIPS_R_T9);
		break;
	/* dst = dst | src */
	case BPF_OR:
		emit(ctx, or, lo(dst), lo(dst), lo(src));
		emit(ctx, or, hi(dst), hi(dst), hi(src));
		break;
	/* dst = dst & src */
	case BPF_AND:
		emit(ctx, and, lo(dst), lo(dst), lo(src));
		emit(ctx, and, hi(dst), hi(dst), hi(src));
		break;
	/* dst = dst ^ src */
	case BPF_XOR:
		emit(ctx, xor, lo(dst), lo(dst), lo(src));
		emit(ctx, xor, hi(dst), hi(dst), hi(src));
		break;
	}
	clobber_reg64(ctx, dst);
}

/* ALU invert (64-bit) */
static void emit_neg_i64(struct jit_context *ctx, const u8 dst[])
{
	emit(ctx, sltu, MIPS_R_T9, MIPS_R_ZERO, lo(dst));
	emit(ctx, subu, lo(dst), MIPS_R_ZERO, lo(dst));
	emit(ctx, subu, hi(dst), MIPS_R_ZERO, hi(dst));
	emit(ctx, subu, hi(dst), hi(dst), MIPS_R_T9);

	clobber_reg64(ctx, dst);
}

/* ALU shift immediate (64-bit) */
static void emit_shift_i64(struct jit_context *ctx,
			   const u8 dst[], u32 imm, u8 op)
{
	switch (BPF_OP(op)) {
	/* dst = dst << imm */
	case BPF_LSH:
		if (imm < 32) {
			emit(ctx, srl, MIPS_R_T9, lo(dst), 32 - imm);
			emit(ctx, sll, lo(dst), lo(dst), imm);
			emit(ctx, sll, hi(dst), hi(dst), imm);
			emit(ctx, or, hi(dst), hi(dst), MIPS_R_T9);
		} else {
			emit(ctx, sll, hi(dst), lo(dst), imm - 32);
			emit(ctx, move, lo(dst), MIPS_R_ZERO);
		}
		break;
	/* dst = dst >> imm */
	case BPF_RSH:
		if (imm < 32) {
			emit(ctx, sll, MIPS_R_T9, hi(dst), 32 - imm);
			emit(ctx, srl, lo(dst), lo(dst), imm);
			emit(ctx, srl, hi(dst), hi(dst), imm);
			emit(ctx, or, lo(dst), lo(dst), MIPS_R_T9);
		} else {
			emit(ctx, srl, lo(dst), hi(dst), imm - 32);
			emit(ctx, move, hi(dst), MIPS_R_ZERO);
		}
		break;
	/* dst = dst >> imm (arithmetic) */
	case BPF_ARSH:
		if (imm < 32) {
			emit(ctx, sll, MIPS_R_T9, hi(dst), 32 - imm);
			emit(ctx, srl, lo(dst), lo(dst), imm);
			emit(ctx, sra, hi(dst), hi(dst), imm);
			emit(ctx, or, lo(dst), lo(dst), MIPS_R_T9);
		} else {
			emit(ctx, sra, lo(dst), hi(dst), imm - 32);
			emit(ctx, sra, hi(dst), hi(dst), 31);
		}
		break;
	}
	clobber_reg64(ctx, dst);
}

/* ALU shift register (64-bit) */
static void emit_shift_r64(struct jit_context *ctx,
			   const u8 dst[], u8 src, u8 op)
{
	u8 t1 = MIPS_R_T8;
	u8 t2 = MIPS_R_T9;

	emit(ctx, andi, t1, src, 32);              /* t1 = src & 32          */
	emit(ctx, beqz, t1, 16);                   /* PC += 16 if t1 == 0    */
	emit(ctx, nor, t2, src, MIPS_R_ZERO);      /* t2 = ~src (delay slot) */

	switch (BPF_OP(op)) {
	/* dst = dst << src */
	case BPF_LSH:
		/* Next: shift >= 32 */
		emit(ctx, sllv, hi(dst), lo(dst), src);    /* dh = dl << src */
		emit(ctx, move, lo(dst), MIPS_R_ZERO);     /* dl = 0         */
		emit(ctx, b, 20);                          /* PC += 20       */
		/* +16: shift < 32 */
		emit(ctx, srl, t1, lo(dst), 1);            /* t1 = dl >> 1   */
		emit(ctx, srlv, t1, t1, t2);               /* t1 = t1 >> t2  */
		emit(ctx, sllv, lo(dst), lo(dst), src);    /* dl = dl << src */
		emit(ctx, sllv, hi(dst), hi(dst), src);    /* dh = dh << src */
		emit(ctx, or, hi(dst), hi(dst), t1);       /* dh = dh | t1   */
		break;
	/* dst = dst >> src */
	case BPF_RSH:
		/* Next: shift >= 32 */
		emit(ctx, srlv, lo(dst), hi(dst), src);    /* dl = dh >> src */
		emit(ctx, move, hi(dst), MIPS_R_ZERO);     /* dh = 0         */
		emit(ctx, b, 20);                          /* PC += 20       */
		/* +16: shift < 32 */
		emit(ctx, sll, t1, hi(dst), 1);            /* t1 = dl << 1   */
		emit(ctx, sllv, t1, t1, t2);               /* t1 = t1 << t2  */
		emit(ctx, srlv, lo(dst), lo(dst), src);    /* dl = dl >> src */
		emit(ctx, srlv, hi(dst), hi(dst), src);    /* dh = dh >> src */
		emit(ctx, or, lo(dst), lo(dst), t1);       /* dl = dl | t1   */
		break;
	/* dst = dst >> src (arithmetic) */
	case BPF_ARSH:
		/* Next: shift >= 32 */
		emit(ctx, srav, lo(dst), hi(dst), src);   /* dl = dh >>a src */
		emit(ctx, sra, hi(dst), hi(dst), 31);     /* dh = dh >>a 31  */
		emit(ctx, b, 20);                         /* PC += 20        */
		/* +16: shift < 32 */
		emit(ctx, sll, t1, hi(dst), 1);           /* t1 = dl << 1    */
		emit(ctx, sllv, t1, t1, t2);              /* t1 = t1 << t2   */
		emit(ctx, srlv, lo(dst), lo(dst), src);   /* dl = dl >>a src */
		emit(ctx, srav, hi(dst), hi(dst), src);   /* dh = dh >> src  */
		emit(ctx, or, lo(dst), lo(dst), t1);      /* dl = dl | t1    */
		break;
	}

	/* +20: Done */
	clobber_reg64(ctx, dst);
}

/* ALU mul immediate (64x32-bit) */
static void emit_mul_i64(struct jit_context *ctx, const u8 dst[], s32 imm)
{
	u8 src = MIPS_R_T6;
	u8 tmp = MIPS_R_T9;

	switch (imm) {
	/* dst = dst * 1 is a no-op */
	case 1:
		break;
	/* dst = dst * -1 */
	case -1:
		emit_neg_i64(ctx, dst);
		break;
	case 0:
		emit_mov_r(ctx, lo(dst), MIPS_R_ZERO);
		emit_mov_r(ctx, hi(dst), MIPS_R_ZERO);
		break;
	/* Full 64x32 multiply */
	default:
		/* hi(dst) = hi(dst) * src(imm) */
		emit_mov_i(ctx, src, imm);
		if (cpu_has_mips32r1 || cpu_has_mips32r6) {
			emit(ctx, mul, hi(dst), hi(dst), src);
		} else {
			emit(ctx, multu, hi(dst), src);
			emit(ctx, mflo, hi(dst));
		}

		/* hi(dst) = hi(dst) - lo(dst) */
		if (imm < 0)
			emit(ctx, subu, hi(dst), hi(dst), lo(dst));

		/* tmp = lo(dst) * src(imm) >> 32 */
		/* lo(dst) = lo(dst) * src(imm) */
		if (cpu_has_mips32r6) {
			emit(ctx, muhu, tmp, lo(dst), src);
			emit(ctx, mulu, lo(dst), lo(dst), src);
		} else {
			emit(ctx, multu, lo(dst), src);
			emit(ctx, mflo, lo(dst));
			emit(ctx, mfhi, tmp);
		}

		/* hi(dst) += tmp */
		emit(ctx, addu, hi(dst), hi(dst), tmp);
		clobber_reg64(ctx, dst);
		break;
	}
}

/* ALU mul register (64x64-bit) */
static void emit_mul_r64(struct jit_context *ctx,
			 const u8 dst[], const u8 src[])
{
	u8 acc = MIPS_R_T8;
	u8 tmp = MIPS_R_T9;

	/* acc = hi(dst) * lo(src) */
	if (cpu_has_mips32r1 || cpu_has_mips32r6) {
		emit(ctx, mul, acc, hi(dst), lo(src));
	} else {
		emit(ctx, multu, hi(dst), lo(src));
		emit(ctx, mflo, acc);
	}

	/* tmp = lo(dst) * hi(src) */
	if (cpu_has_mips32r1 || cpu_has_mips32r6) {
		emit(ctx, mul, tmp, lo(dst), hi(src));
	} else {
		emit(ctx, multu, lo(dst), hi(src));
		emit(ctx, mflo, tmp);
	}

	/* acc += tmp */
	emit(ctx, addu, acc, acc, tmp);

	/* tmp = lo(dst) * lo(src) >> 32 */
	/* lo(dst) = lo(dst) * lo(src) */
	if (cpu_has_mips32r6) {
		emit(ctx, muhu, tmp, lo(dst), lo(src));
		emit(ctx, mulu, lo(dst), lo(dst), lo(src));
	} else {
		emit(ctx, multu, lo(dst), lo(src));
		emit(ctx, mflo, lo(dst));
		emit(ctx, mfhi, tmp);
	}

	/* hi(dst) = acc + tmp */
	emit(ctx, addu, hi(dst), acc, tmp);
	clobber_reg64(ctx, dst);
}

/* Helper function for 64-bit modulo */
static u64 jit_mod64(u64 a, u64 b)
{
	u64 rem;

	div64_u64_rem(a, b, &rem);
	return rem;
}

/* ALU div/mod register (64-bit) */
static void emit_divmod_r64(struct jit_context *ctx,
			    const u8 dst[], const u8 src[], u8 op)
{
	const u8 *r0 = bpf2mips32[BPF_REG_0]; /* Mapped to v0-v1 */
	const u8 *r1 = bpf2mips32[BPF_REG_1]; /* Mapped to a0-a1 */
	const u8 *r2 = bpf2mips32[BPF_REG_2]; /* Mapped to a2-a3 */
	int exclude, k;
	u32 addr = 0;

	/* Push caller-saved registers on stack */
	push_regs(ctx, ctx->clobbered & JIT_CALLER_REGS,
		  0, JIT_RESERVED_STACK);

	/* Put 64-bit arguments 1 and 2 in registers a0-a3 */
	for (k = 0; k < 2; k++) {
		emit(ctx, move, MIPS_R_T9, src[k]);
		emit(ctx, move, r1[k], dst[k]);
		emit(ctx, move, r2[k], MIPS_R_T9);
	}

	/* Emit function call */
	switch (BPF_OP(op)) {
	/* dst = dst / src */
	case BPF_DIV:
		addr = (u32)&div64_u64;
		break;
	/* dst = dst % src */
	case BPF_MOD:
		addr = (u32)&jit_mod64;
		break;
	}
	emit_mov_i(ctx, MIPS_R_T9, addr);
	emit(ctx, jalr, MIPS_R_RA, MIPS_R_T9);
	emit(ctx, nop); /* Delay slot */

	/* Store the 64-bit result in dst */
	emit(ctx, move, dst[0], r0[0]);
	emit(ctx, move, dst[1], r0[1]);

	/* Restore caller-saved registers, excluding the computed result */
	exclude = BIT(lo(dst)) | BIT(hi(dst));
	pop_regs(ctx, ctx->clobbered & JIT_CALLER_REGS,
		 exclude, JIT_RESERVED_STACK);
	emit_load_delay(ctx);

	clobber_reg64(ctx, dst);
	clobber_reg(ctx, MIPS_R_V0);
	clobber_reg(ctx, MIPS_R_V1);
	clobber_reg(ctx, MIPS_R_RA);
}

/* Swap bytes in a register word */
static void emit_swap8_r(struct jit_context *ctx, u8 dst, u8 src, u8 mask)
{
	u8 tmp = MIPS_R_T9;

	emit(ctx, and, tmp, src, mask); /* tmp = src & 0x00ff00ff */
	emit(ctx, sll, tmp, tmp, 8);    /* tmp = tmp << 8         */
	emit(ctx, srl, dst, src, 8);    /* dst = src >> 8         */
	emit(ctx, and, dst, dst, mask); /* dst = dst & 0x00ff00ff */
	emit(ctx, or,  dst, dst, tmp);  /* dst = dst | tmp        */
}

/* Swap half words in a register word */
static void emit_swap16_r(struct jit_context *ctx, u8 dst, u8 src)
{
	u8 tmp = MIPS_R_T9;

	emit(ctx, sll, tmp, src, 16);  /* tmp = src << 16 */
	emit(ctx, srl, dst, src, 16);  /* dst = src >> 16 */
	emit(ctx, or,  dst, dst, tmp); /* dst = dst | tmp */
}

/* Swap bytes and truncate a register double word, word or half word */
static void emit_bswap_r64(struct jit_context *ctx, const u8 dst[], u32 width)
{
	u8 tmp = MIPS_R_T8;

	switch (width) {
	/* Swap bytes in a double word */
	case 64:
		if (cpu_has_mips32r2 || cpu_has_mips32r6) {
			emit(ctx, rotr, tmp, hi(dst), 16);
			emit(ctx, rotr, hi(dst), lo(dst), 16);
			emit(ctx, wsbh, lo(dst), tmp);
			emit(ctx, wsbh, hi(dst), hi(dst));
		} else {
			emit_swap16_r(ctx, tmp, lo(dst));
			emit_swap16_r(ctx, lo(dst), hi(dst));
			emit(ctx, move, hi(dst), tmp);

			emit(ctx, lui, tmp, 0xff);      /* tmp = 0x00ff0000 */
			emit(ctx, ori, tmp, tmp, 0xff); /* tmp = 0x00ff00ff */
			emit_swap8_r(ctx, lo(dst), lo(dst), tmp);
			emit_swap8_r(ctx, hi(dst), hi(dst), tmp);
		}
		break;
	/* Swap bytes in a word */
	/* Swap bytes in a half word */
	case 32:
	case 16:
		emit_bswap_r(ctx, lo(dst), width);
		emit(ctx, move, hi(dst), MIPS_R_ZERO);
		break;
	}
	clobber_reg64(ctx, dst);
}

/* Truncate a register double word, word or half word */
static void emit_trunc_r64(struct jit_context *ctx, const u8 dst[], u32 width)
{
	switch (width) {
	case 64:
		break;
	/* Zero-extend a word */
	case 32:
		emit(ctx, move, hi(dst), MIPS_R_ZERO);
		clobber_reg(ctx, hi(dst));
		break;
	/* Zero-extend a half word */
	case 16:
		emit(ctx, move, hi(dst), MIPS_R_ZERO);
		emit(ctx, andi, lo(dst), lo(dst), 0xffff);
		clobber_reg64(ctx, dst);
		break;
	}
}

/* Load operation: dst = *(size*)(src + off) */
static void emit_ldx(struct jit_context *ctx,
		     const u8 dst[], u8 src, s16 off, u8 size)
{
	switch (size) {
	/* Load a byte */
	case BPF_B:
		emit(ctx, lbu, lo(dst), off, src);
		emit(ctx, move, hi(dst), MIPS_R_ZERO);
		break;
	/* Load a half word */
	case BPF_H:
		emit(ctx, lhu, lo(dst), off, src);
		emit(ctx, move, hi(dst), MIPS_R_ZERO);
		break;
	/* Load a word */
	case BPF_W:
		emit(ctx, lw, lo(dst), off, src);
		emit(ctx, move, hi(dst), MIPS_R_ZERO);
		break;
	/* Load a double word */
	case BPF_DW:
		if (dst[1] == src) {
			emit(ctx, lw, dst[0], off + 4, src);
			emit(ctx, lw, dst[1], off, src);
		} else {
			emit(ctx, lw, dst[1], off, src);
			emit(ctx, lw, dst[0], off + 4, src);
		}
		emit_load_delay(ctx);
		break;
	}
	clobber_reg64(ctx, dst);
}

/* Store operation: *(size *)(dst + off) = src */
static void emit_stx(struct jit_context *ctx,
		     const u8 dst, const u8 src[], s16 off, u8 size)
{
	switch (size) {
	/* Store a byte */
	case BPF_B:
		emit(ctx, sb, lo(src), off, dst);
		break;
	/* Store a half word */
	case BPF_H:
		emit(ctx, sh, lo(src), off, dst);
		break;
	/* Store a word */
	case BPF_W:
		emit(ctx, sw, lo(src), off, dst);
		break;
	/* Store a double word */
	case BPF_DW:
		emit(ctx, sw, src[1], off, dst);
		emit(ctx, sw, src[0], off + 4, dst);
		break;
	}
}

/* Atomic read-modify-write (32-bit, non-ll/sc fallback) */
static void emit_atomic_r32(struct jit_context *ctx,
			    u8 dst, u8 src, s16 off, u8 code)
{
	u32 exclude = 0;
	u32 addr = 0;

	/* Push caller-saved registers on stack */
	push_regs(ctx, ctx->clobbered & JIT_CALLER_REGS,
		  0, JIT_RESERVED_STACK);
	/*
	 * Argument 1: dst+off if xchg, otherwise src, passed in register a0
	 * Argument 2: src if xchg, othersize dst+off, passed in register a1
	 */
	emit(ctx, move, MIPS_R_T9, dst);
	if (code == BPF_XCHG) {
		emit(ctx, move, MIPS_R_A1, src);
		emit(ctx, addiu, MIPS_R_A0, MIPS_R_T9, off);
	} else {
		emit(ctx, move, MIPS_R_A0, src);
		emit(ctx, addiu, MIPS_R_A1, MIPS_R_T9, off);
	}

	/* Emit function call */
	switch (code) {
	case BPF_ADD:
		addr = (u32)&atomic_add;
		break;
	case BPF_ADD | BPF_FETCH:
		addr = (u32)&atomic_fetch_add;
		break;
	case BPF_SUB:
		addr = (u32)&atomic_sub;
		break;
	case BPF_SUB | BPF_FETCH:
		addr = (u32)&atomic_fetch_sub;
		break;
	case BPF_OR:
		addr = (u32)&atomic_or;
		break;
	case BPF_OR | BPF_FETCH:
		addr = (u32)&atomic_fetch_or;
		break;
	case BPF_AND:
		addr = (u32)&atomic_and;
		break;
	case BPF_AND | BPF_FETCH:
		addr = (u32)&atomic_fetch_and;
		break;
	case BPF_XOR:
		addr = (u32)&atomic_xor;
		break;
	case BPF_XOR | BPF_FETCH:
		addr = (u32)&atomic_fetch_xor;
		break;
	case BPF_XCHG:
		addr = (u32)&atomic_xchg;
		break;
	}
	emit_mov_i(ctx, MIPS_R_T9, addr);
	emit(ctx, jalr, MIPS_R_RA, MIPS_R_T9);
	emit(ctx, nop); /* Delay slot */

	/* Update src register with old value, if specified */
	if (code & BPF_FETCH) {
		emit(ctx, move, src, MIPS_R_V0);
		exclude = BIT(src);
		clobber_reg(ctx, src);
	}

	/* Restore caller-saved registers, except any fetched value */
	pop_regs(ctx, ctx->clobbered & JIT_CALLER_REGS,
		 exclude, JIT_RESERVED_STACK);
	emit_load_delay(ctx);
	clobber_reg(ctx, MIPS_R_RA);
}

/* Helper function for 64-bit atomic exchange */
static s64 jit_xchg64(s64 a, atomic64_t *v)
{
	return atomic64_xchg(v, a);
}

/* Atomic read-modify-write (64-bit) */
static void emit_atomic_r64(struct jit_context *ctx,
			    u8 dst, const u8 src[], s16 off, u8 code)
{
	const u8 *r0 = bpf2mips32[BPF_REG_0]; /* Mapped to v0-v1 */
	const u8 *r1 = bpf2mips32[BPF_REG_1]; /* Mapped to a0-a1 */
	u32 exclude = 0;
	u32 addr = 0;

	/* Push caller-saved registers on stack */
	push_regs(ctx, ctx->clobbered & JIT_CALLER_REGS,
		  0, JIT_RESERVED_STACK);
	/*
	 * Argument 1: 64-bit src, passed in registers a0-a1
	 * Argument 2: 32-bit dst+off, passed in register a2
	 */
	emit(ctx, move, MIPS_R_T9, dst);
	emit(ctx, move, r1[0], src[0]);
	emit(ctx, move, r1[1], src[1]);
	emit(ctx, addiu, MIPS_R_A2, MIPS_R_T9, off);

	/* Emit function call */
	switch (code) {
	case BPF_ADD:
		addr = (u32)&atomic64_add;
		break;
	case BPF_ADD | BPF_FETCH:
		addr = (u32)&atomic64_fetch_add;
		break;
	case BPF_SUB:
		addr = (u32)&atomic64_sub;
		break;
	case BPF_SUB | BPF_FETCH:
		addr = (u32)&atomic64_fetch_sub;
		break;
	case BPF_OR:
		addr = (u32)&atomic64_or;
		break;
	case BPF_OR | BPF_FETCH:
		addr = (u32)&atomic64_fetch_or;
		break;
	case BPF_AND:
		addr = (u32)&atomic64_and;
		break;
	case BPF_AND | BPF_FETCH:
		addr = (u32)&atomic64_fetch_and;
		break;
	case BPF_XOR:
		addr = (u32)&atomic64_xor;
		break;
	case BPF_XOR | BPF_FETCH:
		addr = (u32)&atomic64_fetch_xor;
		break;
	case BPF_XCHG:
		addr = (u32)&jit_xchg64;
		break;
	}
	emit_mov_i(ctx, MIPS_R_T9, addr);
	emit(ctx, jalr, MIPS_R_RA, MIPS_R_T9);
	emit(ctx, nop); /* Delay slot */

	/* Update src register with old value, if specified */
	if (code & BPF_FETCH) {
		emit(ctx, move, lo(src), lo(r0));
		emit(ctx, move, hi(src), hi(r0));
		exclude = BIT(src[0]) | BIT(src[1]);
		clobber_reg64(ctx, src);
	}

	/* Restore caller-saved registers, except any fetched value */
	pop_regs(ctx, ctx->clobbered & JIT_CALLER_REGS,
		 exclude, JIT_RESERVED_STACK);
	emit_load_delay(ctx);
	clobber_reg(ctx, MIPS_R_RA);
}

/* Atomic compare-and-exchange (32-bit, non-ll/sc fallback) */
static void emit_cmpxchg_r32(struct jit_context *ctx, u8 dst, u8 src, s16 off)
{
	const u8 *r0 = bpf2mips32[BPF_REG_0];

	/* Push caller-saved registers on stack */
	push_regs(ctx, ctx->clobbered & JIT_CALLER_REGS,
		  JIT_RETURN_REGS, JIT_RESERVED_STACK + 2 * sizeof(u32));
	/*
	 * Argument 1: 32-bit dst+off, passed in register a0
	 * Argument 2: 32-bit r0, passed in register a1
	 * Argument 3: 32-bit src, passed in register a2
	 */
	emit(ctx, addiu, MIPS_R_T9, dst, off);
	emit(ctx, move, MIPS_R_T8, src);
	emit(ctx, move, MIPS_R_A1, lo(r0));
	emit(ctx, move, MIPS_R_A0, MIPS_R_T9);
	emit(ctx, move, MIPS_R_A2, MIPS_R_T8);

	/* Emit function call */
	emit_mov_i(ctx, MIPS_R_T9, (u32)&atomic_cmpxchg);
	emit(ctx, jalr, MIPS_R_RA, MIPS_R_T9);
	emit(ctx, nop); /* Delay slot */

#ifdef __BIG_ENDIAN
	emit(ctx, move, lo(r0), MIPS_R_V0);
#endif
	/* Restore caller-saved registers, except the return value */
	pop_regs(ctx, ctx->clobbered & JIT_CALLER_REGS,
		 JIT_RETURN_REGS, JIT_RESERVED_STACK + 2 * sizeof(u32));
	emit_load_delay(ctx);
	clobber_reg(ctx, MIPS_R_V0);
	clobber_reg(ctx, MIPS_R_V1);
	clobber_reg(ctx, MIPS_R_RA);
}

/* Atomic compare-and-exchange (64-bit) */
static void emit_cmpxchg_r64(struct jit_context *ctx,
			     u8 dst, const u8 src[], s16 off)
{
	const u8 *r0 = bpf2mips32[BPF_REG_0];
	const u8 *r2 = bpf2mips32[BPF_REG_2];

	/* Push caller-saved registers on stack */
	push_regs(ctx, ctx->clobbered & JIT_CALLER_REGS,
		  JIT_RETURN_REGS, JIT_RESERVED_STACK + 2 * sizeof(u32));
	/*
	 * Argument 1: 32-bit dst+off, passed in register a0 (a1 unused)
	 * Argument 2: 64-bit r0, passed in registers a2-a3
	 * Argument 3: 64-bit src, passed on stack
	 */
	push_regs(ctx, BIT(src[0]) | BIT(src[1]), 0, JIT_RESERVED_STACK);
	emit(ctx, addiu, MIPS_R_T9, dst, off);
	emit(ctx, move, r2[0], r0[0]);
	emit(ctx, move, r2[1], r0[1]);
	emit(ctx, move, MIPS_R_A0, MIPS_R_T9);

	/* Emit function call */
	emit_mov_i(ctx, MIPS_R_T9, (u32)&atomic64_cmpxchg);
	emit(ctx, jalr, MIPS_R_RA, MIPS_R_T9);
	emit(ctx, nop); /* Delay slot */

	/* Restore caller-saved registers, except the return value */
	pop_regs(ctx, ctx->clobbered & JIT_CALLER_REGS,
		 JIT_RETURN_REGS, JIT_RESERVED_STACK + 2 * sizeof(u32));
	emit_load_delay(ctx);
	clobber_reg(ctx, MIPS_R_V0);
	clobber_reg(ctx, MIPS_R_V1);
	clobber_reg(ctx, MIPS_R_RA);
}

/*
 * Conditional movz or an emulated equivalent.
 * Note that the rs register may be modified.
 */
static void emit_movz_r(struct jit_context *ctx, u8 rd, u8 rs, u8 rt)
{
	if (cpu_has_mips_2) {
		emit(ctx, movz, rd, rs, rt);           /* rd = rt ? rd : rs  */
	} else if (cpu_has_mips32r6) {
		if (rs != MIPS_R_ZERO)
			emit(ctx, seleqz, rs, rs, rt); /* rs = 0 if rt == 0  */
		emit(ctx, selnez, rd, rd, rt);         /* rd = 0 if rt != 0  */
		if (rs != MIPS_R_ZERO)
			emit(ctx, or, rd, rd, rs);     /* rd = rd | rs       */
	} else {
		emit(ctx, bnez, rt, 8);                /* PC += 8 if rd != 0 */
		emit(ctx, nop);                        /* +0: delay slot     */
		emit(ctx, or, rd, rs, MIPS_R_ZERO);    /* +4: rd = rs        */
	}
	clobber_reg(ctx, rd);
	clobber_reg(ctx, rs);
}

/*
 * Conditional movn or an emulated equivalent.
 * Note that the rs register may be modified.
 */
static void emit_movn_r(struct jit_context *ctx, u8 rd, u8 rs, u8 rt)
{
	if (cpu_has_mips_2) {
		emit(ctx, movn, rd, rs, rt);           /* rd = rt ? rs : rd  */
	} else if (cpu_has_mips32r6) {
		if (rs != MIPS_R_ZERO)
			emit(ctx, selnez, rs, rs, rt); /* rs = 0 if rt == 0  */
		emit(ctx, seleqz, rd, rd, rt);         /* rd = 0 if rt != 0  */
		if (rs != MIPS_R_ZERO)
			emit(ctx, or, rd, rd, rs);     /* rd = rd | rs       */
	} else {
		emit(ctx, beqz, rt, 8);                /* PC += 8 if rd == 0 */
		emit(ctx, nop);                        /* +0: delay slot     */
		emit(ctx, or, rd, rs, MIPS_R_ZERO);    /* +4: rd = rs        */
	}
	clobber_reg(ctx, rd);
	clobber_reg(ctx, rs);
}

/* Emulation of 64-bit sltiu rd, rs, imm, where imm may be S32_MAX + 1 */
static void emit_sltiu_r64(struct jit_context *ctx, u8 rd,
			   const u8 rs[], s64 imm)
{
	u8 tmp = MIPS_R_T9;

	if (imm < 0) {
		emit_mov_i(ctx, rd, imm);                 /* rd = imm        */
		emit(ctx, sltu, rd, lo(rs), rd);          /* rd = rsl < rd   */
		emit(ctx, sltiu, tmp, hi(rs), -1);        /* tmp = rsh < ~0U */
		emit(ctx, or, rd, rd, tmp);               /* rd = rd | tmp   */
	} else { /* imm >= 0 */
		if (imm > 0x7fff) {
			emit_mov_i(ctx, rd, (s32)imm);     /* rd = imm       */
			emit(ctx, sltu, rd, lo(rs), rd);   /* rd = rsl < rd  */
		} else {
			emit(ctx, sltiu, rd, lo(rs), imm); /* rd = rsl < imm */
		}
		emit_movn_r(ctx, rd, MIPS_R_ZERO, hi(rs)); /* rd = 0 if rsh  */
	}
}

/* Emulation of 64-bit sltu rd, rs, rt */
static void emit_sltu_r64(struct jit_context *ctx, u8 rd,
			  const u8 rs[], const u8 rt[])
{
	u8 tmp = MIPS_R_T9;

	emit(ctx, sltu, rd, lo(rs), lo(rt));           /* rd = rsl < rtl     */
	emit(ctx, subu, tmp, hi(rs), hi(rt));          /* tmp = rsh - rth    */
	emit_movn_r(ctx, rd, MIPS_R_ZERO, tmp);        /* rd = 0 if tmp != 0 */
	emit(ctx, sltu, tmp, hi(rs), hi(rt));          /* tmp = rsh < rth    */
	emit(ctx, or, rd, rd, tmp);                    /* rd = rd | tmp      */
}

/* Emulation of 64-bit slti rd, rs, imm, where imm may be S32_MAX + 1 */
static void emit_slti_r64(struct jit_context *ctx, u8 rd,
			  const u8 rs[], s64 imm)
{
	u8 t1 = MIPS_R_T8;
	u8 t2 = MIPS_R_T9;
	u8 cmp;

	/*
	 * if ((rs < 0) ^ (imm < 0)) t1 = imm >u rsl
	 * else                      t1 = rsl <u imm
	 */
	emit_mov_i(ctx, rd, (s32)imm);
	emit(ctx, sltu, t1, lo(rs), rd);               /* t1 = rsl <u imm   */
	emit(ctx, sltu, t2, rd, lo(rs));               /* t2 = imm <u rsl   */
	emit(ctx, srl, rd, hi(rs), 31);                /* rd = rsh >> 31    */
	if (imm < 0)
		emit_movz_r(ctx, t1, t2, rd);          /* t1 = rd ? t1 : t2 */
	else
		emit_movn_r(ctx, t1, t2, rd);          /* t1 = rd ? t2 : t1 */
	/*
	 * if ((imm < 0 && rsh != 0xffffffff) ||
	 *     (imm >= 0 && rsh != 0))
	 *      t1 = 0
	 */
	if (imm < 0) {
		emit(ctx, addiu, rd, hi(rs), 1);       /* rd = rsh + 1 */
		cmp = rd;
	} else { /* imm >= 0 */
		cmp = hi(rs);
	}
	emit_movn_r(ctx, t1, MIPS_R_ZERO, cmp);        /* t1 = 0 if cmp != 0 */

	/*
	 * if (imm < 0) rd = rsh < -1
	 * else         rd = rsh != 0
	 * rd = rd | t1
	 */
	emit(ctx, slti, rd, hi(rs), imm < 0 ? -1 : 0); /* rd = rsh < hi(imm) */
	emit(ctx, or, rd, rd, t1);                     /* rd = rd | t1       */
}

/* Emulation of 64-bit(slt rd, rs, rt) */
static void emit_slt_r64(struct jit_context *ctx, u8 rd,
			 const u8 rs[], const u8 rt[])
{
	u8 t1 = MIPS_R_T7;
	u8 t2 = MIPS_R_T8;
	u8 t3 = MIPS_R_T9;

	/*
	 * if ((rs < 0) ^ (rt < 0)) t1 = rtl <u rsl
	 * else                     t1 = rsl <u rtl
	 * if (rsh == rth)          t1 = 0
	 */
	emit(ctx, sltu, t1, lo(rs), lo(rt));           /* t1 = rsl <u rtl   */
	emit(ctx, sltu, t2, lo(rt), lo(rs));           /* t2 = rtl <u rsl   */
	emit(ctx, xor, t3, hi(rs), hi(rt));            /* t3 = rlh ^ rth    */
	emit(ctx, srl, rd, t3, 31);                    /* rd = t3 >> 31     */
	emit_movn_r(ctx, t1, t2, rd);                  /* t1 = rd ? t2 : t1 */
	emit_movn_r(ctx, t1, MIPS_R_ZERO, t3);         /* t1 = 0 if t3 != 0 */

	/* rd = (rsh < rth) | t1 */
	emit(ctx, slt, rd, hi(rs), hi(rt));            /* rd = rsh <s rth   */
	emit(ctx, or, rd, rd, t1);                     /* rd = rd | t1      */
}

/* Jump immediate (64-bit) */
static void emit_jmp_i64(struct jit_context *ctx,
			 const u8 dst[], s32 imm, s32 off, u8 op)
{
	u8 tmp = MIPS_R_T6;

	switch (op) {
	/* No-op, used internally for branch optimization */
	case JIT_JNOP:
		break;
	/* PC += off if dst == imm */
	/* PC += off if dst != imm */
	case BPF_JEQ:
	case BPF_JNE:
		if (imm >= -0x7fff && imm <= 0x8000) {
			emit(ctx, addiu, tmp, lo(dst), -imm);
		} else if ((u32)imm <= 0xffff) {
			emit(ctx, xori, tmp, lo(dst), imm);
		} else {       /* Register fallback */
			emit_mov_i(ctx, tmp, imm);
			emit(ctx, xor, tmp, lo(dst), tmp);
		}
		if (imm < 0) { /* Compare sign extension */
			emit(ctx, addu, MIPS_R_T9, hi(dst), 1);
			emit(ctx, or, tmp, tmp, MIPS_R_T9);
		} else {       /* Compare zero extension */
			emit(ctx, or, tmp, tmp, hi(dst));
		}
		if (op == BPF_JEQ)
			emit(ctx, beqz, tmp, off);
		else   /* BPF_JNE */
			emit(ctx, bnez, tmp, off);
		break;
	/* PC += off if dst & imm */
	/* PC += off if (dst & imm) == 0 (not in BPF, used for long jumps) */
	case BPF_JSET:
	case JIT_JNSET:
		if ((u32)imm <= 0xffff) {
			emit(ctx, andi, tmp, lo(dst), imm);
		} else {     /* Register fallback */
			emit_mov_i(ctx, tmp, imm);
			emit(ctx, and, tmp, lo(dst), tmp);
		}
		if (imm < 0) /* Sign-extension pulls in high word */
			emit(ctx, or, tmp, tmp, hi(dst));
		if (op == BPF_JSET)
			emit(ctx, bnez, tmp, off);
		else   /* JIT_JNSET */
			emit(ctx, beqz, tmp, off);
		break;
	/* PC += off if dst > imm */
	case BPF_JGT:
		emit_sltiu_r64(ctx, tmp, dst, (s64)imm + 1);
		emit(ctx, beqz, tmp, off);
		break;
	/* PC += off if dst >= imm */
	case BPF_JGE:
		emit_sltiu_r64(ctx, tmp, dst, imm);
		emit(ctx, beqz, tmp, off);
		break;
	/* PC += off if dst < imm */
	case BPF_JLT:
		emit_sltiu_r64(ctx, tmp, dst, imm);
		emit(ctx, bnez, tmp, off);
		break;
	/* PC += off if dst <= imm */
	case BPF_JLE:
		emit_sltiu_r64(ctx, tmp, dst, (s64)imm + 1);
		emit(ctx, bnez, tmp, off);
		break;
	/* PC += off if dst > imm (signed) */
	case BPF_JSGT:
		emit_slti_r64(ctx, tmp, dst, (s64)imm + 1);
		emit(ctx, beqz, tmp, off);
		break;
	/* PC += off if dst >= imm (signed) */
	case BPF_JSGE:
		emit_slti_r64(ctx, tmp, dst, imm);
		emit(ctx, beqz, tmp, off);
		break;
	/* PC += off if dst < imm (signed) */
	case BPF_JSLT:
		emit_slti_r64(ctx, tmp, dst, imm);
		emit(ctx, bnez, tmp, off);
		break;
	/* PC += off if dst <= imm (signed) */
	case BPF_JSLE:
		emit_slti_r64(ctx, tmp, dst, (s64)imm + 1);
		emit(ctx, bnez, tmp, off);
		break;
	}
}

/* Jump register (64-bit) */
static void emit_jmp_r64(struct jit_context *ctx,
			 const u8 dst[], const u8 src[], s32 off, u8 op)
{
	u8 t1 = MIPS_R_T6;
	u8 t2 = MIPS_R_T7;

	switch (op) {
	/* No-op, used internally for branch optimization */
	case JIT_JNOP:
		break;
	/* PC += off if dst == src */
	/* PC += off if dst != src */
	case BPF_JEQ:
	case BPF_JNE:
		emit(ctx, subu, t1, lo(dst), lo(src));
		emit(ctx, subu, t2, hi(dst), hi(src));
		emit(ctx, or, t1, t1, t2);
		if (op == BPF_JEQ)
			emit(ctx, beqz, t1, off);
		else   /* BPF_JNE */
			emit(ctx, bnez, t1, off);
		break;
	/* PC += off if dst & src */
	/* PC += off if (dst & imm) == 0 (not in BPF, used for long jumps) */
	case BPF_JSET:
	case JIT_JNSET:
		emit(ctx, and, t1, lo(dst), lo(src));
		emit(ctx, and, t2, hi(dst), hi(src));
		emit(ctx, or, t1, t1, t2);
		if (op == BPF_JSET)
			emit(ctx, bnez, t1, off);
		else   /* JIT_JNSET */
			emit(ctx, beqz, t1, off);
		break;
	/* PC += off if dst > src */
	case BPF_JGT:
		emit_sltu_r64(ctx, t1, src, dst);
		emit(ctx, bnez, t1, off);
		break;
	/* PC += off if dst >= src */
	case BPF_JGE:
		emit_sltu_r64(ctx, t1, dst, src);
		emit(ctx, beqz, t1, off);
		break;
	/* PC += off if dst < src */
	case BPF_JLT:
		emit_sltu_r64(ctx, t1, dst, src);
		emit(ctx, bnez, t1, off);
		break;
	/* PC += off if dst <= src */
	case BPF_JLE:
		emit_sltu_r64(ctx, t1, src, dst);
		emit(ctx, beqz, t1, off);
		break;
	/* PC += off if dst > src (signed) */
	case BPF_JSGT:
		emit_slt_r64(ctx, t1, src, dst);
		emit(ctx, bnez, t1, off);
		break;
	/* PC += off if dst >= src (signed) */
	case BPF_JSGE:
		emit_slt_r64(ctx, t1, dst, src);
		emit(ctx, beqz, t1, off);
		break;
	/* PC += off if dst < src (signed) */
	case BPF_JSLT:
		emit_slt_r64(ctx, t1, dst, src);
		emit(ctx, bnez, t1, off);
		break;
	/* PC += off if dst <= src (signed) */
	case BPF_JSLE:
		emit_slt_r64(ctx, t1, src, dst);
		emit(ctx, beqz, t1, off);
		break;
	}
}

/* Function call */
static int emit_call(struct jit_context *ctx, const struct bpf_insn *insn)
{
	bool fixed;
	u64 addr;

	/* Decode the call address */
	if (bpf_jit_get_func_addr(ctx->program, insn, false,
				  &addr, &fixed) < 0)
		return -1;
	if (!fixed)
		return -1;

	/* Push stack arguments */
	push_regs(ctx, JIT_STACK_REGS, 0, JIT_RESERVED_STACK);

	/* Emit function call */
	emit_mov_i(ctx, MIPS_R_T9, addr);
	emit(ctx, jalr, MIPS_R_RA, MIPS_R_T9);
	emit(ctx, nop); /* Delay slot */

	clobber_reg(ctx, MIPS_R_RA);
	clobber_reg(ctx, MIPS_R_V0);
	clobber_reg(ctx, MIPS_R_V1);
	return 0;
}

/* Function tail call */
static int emit_tail_call(struct jit_context *ctx)
{
	u8 ary = lo(bpf2mips32[BPF_REG_2]);
	u8 ind = lo(bpf2mips32[BPF_REG_3]);
	u8 t1 = MIPS_R_T8;
	u8 t2 = MIPS_R_T9;
	int off;

	/*
	 * Tail call:
	 * eBPF R1   - function argument (context ptr), passed in a0-a1
	 * eBPF R2   - ptr to object with array of function entry points
	 * eBPF R3   - array index of function to be called
	 * stack[sz] - remaining tail call count, initialized in prologue
	 */

	/* if (ind >= ary->map.max_entries) goto out */
	off = offsetof(struct bpf_array, map.max_entries);
	if (off > 0x7fff)
		return -1;
	emit(ctx, lw, t1, off, ary);             /* t1 = ary->map.max_entries*/
	emit_load_delay(ctx);                    /* Load delay slot          */
	emit(ctx, sltu, t1, ind, t1);            /* t1 = ind < t1            */
	emit(ctx, beqz, t1, get_offset(ctx, 1)); /* PC += off(1) if t1 == 0  */
						 /* (next insn delay slot)   */
	/* if (TCC-- <= 0) goto out */
	emit(ctx, lw, t2, ctx->stack_size, MIPS_R_SP);  /* t2 = *(SP + size) */
	emit_load_delay(ctx);                     /* Load delay slot         */
	emit(ctx, blez, t2, get_offset(ctx, 1));  /* PC += off(1) if t2 <= 0 */
	emit(ctx, addiu, t2, t2, -1);             /* t2-- (delay slot)       */
	emit(ctx, sw, t2, ctx->stack_size, MIPS_R_SP);  /* *(SP + size) = t2 */

	/* prog = ary->ptrs[ind] */
	off = offsetof(struct bpf_array, ptrs);
	if (off > 0x7fff)
		return -1;
	emit(ctx, sll, t1, ind, 2);               /* t1 = ind << 2           */
	emit(ctx, addu, t1, t1, ary);             /* t1 += ary               */
	emit(ctx, lw, t2, off, t1);               /* t2 = *(t1 + off)        */
	emit_load_delay(ctx);                     /* Load delay slot         */

	/* if (prog == 0) goto out */
	emit(ctx, beqz, t2, get_offset(ctx, 1));  /* PC += off(1) if t2 == 0 */
	emit(ctx, nop);                           /* Delay slot              */

	/* func = prog->bpf_func + 8 (prologue skip offset) */
	off = offsetof(struct bpf_prog, bpf_func);
	if (off > 0x7fff)
		return -1;
	emit(ctx, lw, t1, off, t2);                /* t1 = *(t2 + off)       */
	emit_load_delay(ctx);                      /* Load delay slot        */
	emit(ctx, addiu, t1, t1, JIT_TCALL_SKIP);  /* t1 += skip (8 or 12)   */

	/* goto func */
	build_epilogue(ctx, t1);
	return 0;
}

/*
 * Stack frame layout for a JITed program (stack grows down).
 *
 * Higher address  : Caller's stack frame       :
 *                 :----------------------------:
 *                 : 64-bit eBPF args r3-r5     :
 *                 :----------------------------:
 *                 : Reserved / tail call count :
 *                 +============================+  <--- MIPS sp before call
 *                 | Callee-saved registers,    |
 *                 | including RA and FP        |
 *                 +----------------------------+  <--- eBPF FP (MIPS zero,fp)
 *                 | Local eBPF variables       |
 *                 | allocated by program       |
 *                 +----------------------------+
 *                 | Reserved for caller-saved  |
 *                 | registers                  |
 *                 +----------------------------+
 *                 | Reserved for 64-bit eBPF   |
 *                 | args r3-r5 & args passed   |
 *                 | on stack in kernel calls   |
 * Lower address   +============================+  <--- MIPS sp
 */

/* Build program prologue to set up the stack and registers */
void build_prologue(struct jit_context *ctx)
{
	const u8 *r1 = bpf2mips32[BPF_REG_1];
	const u8 *fp = bpf2mips32[BPF_REG_FP];
	int stack, saved, locals, reserved;

	/*
	 * The first two instructions initialize TCC in the reserved (for us)
	 * 16-byte area in the parent's stack frame. On a tail call, the
	 * calling function jumps into the prologue after these instructions.
	 */
	emit(ctx, ori, MIPS_R_T9, MIPS_R_ZERO, min(MAX_TAIL_CALL_CNT, 0xffff));
	emit(ctx, sw, MIPS_R_T9, 0, MIPS_R_SP);

	/*
	 * Register eBPF R1 contains the 32-bit context pointer argument.
	 * A 32-bit argument is always passed in MIPS register a0, regardless
	 * of CPU endianness. Initialize R1 accordingly and zero-extend.
	 */
#ifdef __BIG_ENDIAN
	emit(ctx, move, lo(r1), MIPS_R_A0);
#endif

	/* === Entry-point for tail calls === */

	/* Zero-extend the 32-bit argument */
	emit(ctx, move, hi(r1), MIPS_R_ZERO);

	/* If the eBPF frame pointer was accessed it must be saved */
	if (ctx->accessed & BIT(BPF_REG_FP))
		clobber_reg64(ctx, fp);

	/* Compute the stack space needed for callee-saved registers */
	saved = hweight32(ctx->clobbered & JIT_CALLEE_REGS) * sizeof(u32);
	saved = ALIGN(saved, MIPS_STACK_ALIGNMENT);

	/* Stack space used by eBPF program local data */
	locals = ALIGN(ctx->program->aux->stack_depth, MIPS_STACK_ALIGNMENT);

	/*
	 * If we are emitting function calls, reserve extra stack space for
	 * caller-saved registers and function arguments passed on the stack.
	 * The required space is computed automatically during resource
	 * usage discovery (pass 1).
	 */
	reserved = ctx->stack_used;

	/* Allocate the stack frame */
	stack = ALIGN(saved + locals + reserved, MIPS_STACK_ALIGNMENT);
	emit(ctx, addiu, MIPS_R_SP, MIPS_R_SP, -stack);

	/* Store callee-saved registers on stack */
	push_regs(ctx, ctx->clobbered & JIT_CALLEE_REGS, 0, stack - saved);

	/* Initialize the eBPF frame pointer if accessed */
	if (ctx->accessed & BIT(BPF_REG_FP))
		emit(ctx, addiu, lo(fp), MIPS_R_SP, stack - saved);

	ctx->saved_size = saved;
	ctx->stack_size = stack;
}

/* Build the program epilogue to restore the stack and registers */
void build_epilogue(struct jit_context *ctx, int dest_reg)
{
	/* Restore callee-saved registers from stack */
	pop_regs(ctx, ctx->clobbered & JIT_CALLEE_REGS, 0,
		 ctx->stack_size - ctx->saved_size);
	/*
	 * A 32-bit return value is always passed in MIPS register v0,
	 * but on big-endian targets the low part of R0 is mapped to v1.
	 */
#ifdef __BIG_ENDIAN
	emit(ctx, move, MIPS_R_V0, MIPS_R_V1);
#endif

	/* Jump to the return address and adjust the stack pointer */
	emit(ctx, jr, dest_reg);
	emit(ctx, addiu, MIPS_R_SP, MIPS_R_SP, ctx->stack_size);
}

/* Build one eBPF instruction */
int build_insn(const struct bpf_insn *insn, struct jit_context *ctx)
{
	const u8 *dst = bpf2mips32[insn->dst_reg];
	const u8 *src = bpf2mips32[insn->src_reg];
	const u8 *res = bpf2mips32[BPF_REG_0];
	const u8 *tmp = bpf2mips32[JIT_REG_TMP];
	u8 code = insn->code;
	s16 off = insn->off;
	s32 imm = insn->imm;
	s32 val, rel;
	u8 alu, jmp;

	switch (code) {
	/* ALU operations */
	/* dst = imm */
	case BPF_ALU | BPF_MOV | BPF_K:
		emit_mov_i(ctx, lo(dst), imm);
		emit_zext_ver(ctx, dst);
		break;
	/* dst = src */
	case BPF_ALU | BPF_MOV | BPF_X:
		if (imm == 1) {
			/* Special mov32 for zext */
			emit_mov_i(ctx, hi(dst), 0);
		} else {
			emit_mov_r(ctx, lo(dst), lo(src));
			emit_zext_ver(ctx, dst);
		}
		break;
	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
		emit_alu_i(ctx, lo(dst), 0, BPF_NEG);
		emit_zext_ver(ctx, dst);
		break;
	/* dst = dst & imm */
	/* dst = dst | imm */
	/* dst = dst ^ imm */
	/* dst = dst << imm */
	/* dst = dst >> imm */
	/* dst = dst >> imm (arithmetic) */
	/* dst = dst + imm */
	/* dst = dst - imm */
	/* dst = dst * imm */
	/* dst = dst / imm */
	/* dst = dst % imm */
	case BPF_ALU | BPF_OR | BPF_K:
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU | BPF_LSH | BPF_K:
	case BPF_ALU | BPF_RSH | BPF_K:
	case BPF_ALU | BPF_ARSH | BPF_K:
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU | BPF_MOD | BPF_K:
		if (!valid_alu_i(BPF_OP(code), imm)) {
			emit_mov_i(ctx, MIPS_R_T6, imm);
			emit_alu_r(ctx, lo(dst), MIPS_R_T6, BPF_OP(code));
		} else if (rewrite_alu_i(BPF_OP(code), imm, &alu, &val)) {
			emit_alu_i(ctx, lo(dst), val, alu);
		}
		emit_zext_ver(ctx, dst);
		break;
	/* dst = dst & src */
	/* dst = dst | src */
	/* dst = dst ^ src */
	/* dst = dst << src */
	/* dst = dst >> src */
	/* dst = dst >> src (arithmetic) */
	/* dst = dst + src */
	/* dst = dst - src */
	/* dst = dst * src */
	/* dst = dst / src */
	/* dst = dst % src */
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU | BPF_XOR | BPF_X:
	case BPF_ALU | BPF_LSH | BPF_X:
	case BPF_ALU | BPF_RSH | BPF_X:
	case BPF_ALU | BPF_ARSH | BPF_X:
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU | BPF_MUL | BPF_X:
	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU | BPF_MOD | BPF_X:
		emit_alu_r(ctx, lo(dst), lo(src), BPF_OP(code));
		emit_zext_ver(ctx, dst);
		break;
	/* dst = imm (64-bit) */
	case BPF_ALU64 | BPF_MOV | BPF_K:
		emit_mov_se_i64(ctx, dst, imm);
		break;
	/* dst = src (64-bit) */
	case BPF_ALU64 | BPF_MOV | BPF_X:
		emit_mov_r(ctx, lo(dst), lo(src));
		emit_mov_r(ctx, hi(dst), hi(src));
		break;
	/* dst = -dst (64-bit) */
	case BPF_ALU64 | BPF_NEG:
		emit_neg_i64(ctx, dst);
		break;
	/* dst = dst & imm (64-bit) */
	case BPF_ALU64 | BPF_AND | BPF_K:
		emit_alu_i64(ctx, dst, imm, BPF_OP(code));
		break;
	/* dst = dst | imm (64-bit) */
	/* dst = dst ^ imm (64-bit) */
	/* dst = dst + imm (64-bit) */
	/* dst = dst - imm (64-bit) */
	case BPF_ALU64 | BPF_OR | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
		if (imm)
			emit_alu_i64(ctx, dst, imm, BPF_OP(code));
		break;
	/* dst = dst << imm (64-bit) */
	/* dst = dst >> imm (64-bit) */
	/* dst = dst >> imm (64-bit, arithmetic) */
	case BPF_ALU64 | BPF_LSH | BPF_K:
	case BPF_ALU64 | BPF_RSH | BPF_K:
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		if (imm)
			emit_shift_i64(ctx, dst, imm, BPF_OP(code));
		break;
	/* dst = dst * imm (64-bit) */
	case BPF_ALU64 | BPF_MUL | BPF_K:
		emit_mul_i64(ctx, dst, imm);
		break;
	/* dst = dst / imm (64-bit) */
	/* dst = dst % imm (64-bit) */
	case BPF_ALU64 | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		/*
		 * Sign-extend the immediate value into a temporary register,
		 * and then do the operation on this register.
		 */
		emit_mov_se_i64(ctx, tmp, imm);
		emit_divmod_r64(ctx, dst, tmp, BPF_OP(code));
		break;
	/* dst = dst & src (64-bit) */
	/* dst = dst | src (64-bit) */
	/* dst = dst ^ src (64-bit) */
	/* dst = dst + src (64-bit) */
	/* dst = dst - src (64-bit) */
	case BPF_ALU64 | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
		emit_alu_r64(ctx, dst, src, BPF_OP(code));
		break;
	/* dst = dst << src (64-bit) */
	/* dst = dst >> src (64-bit) */
	/* dst = dst >> src (64-bit, arithmetic) */
	case BPF_ALU64 | BPF_LSH | BPF_X:
	case BPF_ALU64 | BPF_RSH | BPF_X:
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		emit_shift_r64(ctx, dst, lo(src), BPF_OP(code));
		break;
	/* dst = dst * src (64-bit) */
	case BPF_ALU64 | BPF_MUL | BPF_X:
		emit_mul_r64(ctx, dst, src);
		break;
	/* dst = dst / src (64-bit) */
	/* dst = dst % src (64-bit) */
	case BPF_ALU64 | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
		emit_divmod_r64(ctx, dst, src, BPF_OP(code));
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
		emit_mov_i(ctx, lo(dst), imm);
		emit_mov_i(ctx, hi(dst), insn[1].imm);
		return 1;
	/* LDX: dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_DW:
		emit_ldx(ctx, dst, lo(src), off, BPF_SIZE(code));
		break;
	/* ST: *(size *)(dst + off) = imm */
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_B:
	case BPF_ST | BPF_MEM | BPF_DW:
		switch (BPF_SIZE(code)) {
		case BPF_DW:
			/* Sign-extend immediate value into temporary reg */
			emit_mov_se_i64(ctx, tmp, imm);
			break;
		case BPF_W:
		case BPF_H:
		case BPF_B:
			emit_mov_i(ctx, lo(tmp), imm);
			break;
		}
		emit_stx(ctx, lo(dst), tmp, off, BPF_SIZE(code));
		break;
	/* STX: *(size *)(dst + off) = src */
	case BPF_STX | BPF_MEM | BPF_W:
	case BPF_STX | BPF_MEM | BPF_H:
	case BPF_STX | BPF_MEM | BPF_B:
	case BPF_STX | BPF_MEM | BPF_DW:
		emit_stx(ctx, lo(dst), src, off, BPF_SIZE(code));
		break;
	/* Speculation barrier */
	case BPF_ST | BPF_NOSPEC:
		break;
	/* Atomics */
	case BPF_STX | BPF_ATOMIC | BPF_W:
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
			if (cpu_has_llsc)
				emit_atomic_r(ctx, lo(dst), lo(src), off, imm);
			else /* Non-ll/sc fallback */
				emit_atomic_r32(ctx, lo(dst), lo(src),
						off, imm);
			if (imm & BPF_FETCH)
				emit_zext_ver(ctx, src);
			break;
		case BPF_CMPXCHG:
			if (cpu_has_llsc)
				emit_cmpxchg_r(ctx, lo(dst), lo(src),
					       lo(res), off);
			else /* Non-ll/sc fallback */
				emit_cmpxchg_r32(ctx, lo(dst), lo(src), off);
			/* Result zero-extension inserted by verifier */
			break;
		default:
			goto notyet;
		}
		break;
	/* Atomics (64-bit) */
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
			emit_atomic_r64(ctx, lo(dst), src, off, imm);
			break;
		case BPF_CMPXCHG:
			emit_cmpxchg_r64(ctx, lo(dst), src, off);
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
		emit_jmp_r(ctx, lo(dst), lo(src), rel, jmp);
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
		if (valid_jmp_i(jmp, imm)) {
			emit_jmp_i(ctx, lo(dst), imm, rel, jmp);
		} else {
			/* Move large immediate to register */
			emit_mov_i(ctx, MIPS_R_T6, imm);
			emit_jmp_r(ctx, lo(dst), MIPS_R_T6, rel, jmp);
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
		emit_jmp_r64(ctx, dst, src, rel, jmp);
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
		emit_jmp_i64(ctx, dst, imm, rel, jmp);
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
