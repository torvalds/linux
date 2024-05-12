// SPDX-License-Identifier: GPL-2.0-only
/*
 * Just-In-Time compiler for eBPF bytecode on MIPS.
 * Implementation of JIT functions common to 32-bit and 64-bit CPUs.
 *
 * Copyright (c) 2021 Anyfi Networks AB.
 * Author: Johan Almbladh <johan.almbladh@gmail.com>
 *
 * Based on code and ideas from
 * Copyright (c) 2017 Cavium, Inc.
 * Copyright (c) 2017 Shubham Bansal <illusionist.neo@gmail.com>
 * Copyright (c) 2011 Mircea Gherzan <mgherzan@gmail.com>
 */

/*
 * Code overview
 * =============
 *
 * - bpf_jit_comp.h
 *   Common definitions and utilities.
 *
 * - bpf_jit_comp.c
 *   Implementation of JIT top-level logic and exported JIT API functions.
 *   Implementation of internal operations shared by 32-bit and 64-bit code.
 *   JMP and ALU JIT control code, register control code, shared ALU and
 *   JMP/JMP32 JIT operations.
 *
 * - bpf_jit_comp32.c
 *   Implementation of functions to JIT prologue, epilogue and a single eBPF
 *   instruction for 32-bit MIPS CPUs. The functions use shared operations
 *   where possible, and implement the rest for 32-bit MIPS such as ALU64
 *   operations.
 *
 * - bpf_jit_comp64.c
 *   Ditto, for 64-bit MIPS CPUs.
 *
 * Zero and sign extension
 * ========================
 * 32-bit MIPS instructions on 64-bit MIPS registers use sign extension,
 * but the eBPF instruction set mandates zero extension. We let the verifier
 * insert explicit zero-extensions after 32-bit ALU operations, both for
 * 32-bit and 64-bit MIPS JITs. Conditional JMP32 operations on 64-bit MIPs
 * are JITed with sign extensions inserted when so expected.
 *
 * ALU operations
 * ==============
 * ALU operations on 32/64-bit MIPS and ALU64 operations on 64-bit MIPS are
 * JITed in the following steps. ALU64 operations on 32-bit MIPS are more
 * complicated and therefore only processed by special implementations in
 * step (3).
 *
 * 1) valid_alu_i:
 *    Determine if an immediate operation can be emitted as such, or if
 *    we must fall back to the register version.
 *
 * 2) rewrite_alu_i:
 *    Convert BPF operation and immediate value to a canonical form for
 *    JITing. In some degenerate cases this form may be a no-op.
 *
 * 3) emit_alu_{i,i64,r,64}:
 *    Emit instructions for an ALU or ALU64 immediate or register operation.
 *
 * JMP operations
 * ==============
 * JMP and JMP32 operations require an JIT instruction offset table for
 * translating the jump offset. This table is computed by dry-running the
 * JIT without actually emitting anything. However, the computed PC-relative
 * offset may overflow the 18-bit offset field width of the native MIPS
 * branch instruction. In such cases, the long jump is converted into the
 * following sequence.
 *
 *    <branch> !<cond> +2    Inverted PC-relative branch
 *    nop                    Delay slot
 *    j <offset>             Unconditional absolute long jump
 *    nop                    Delay slot
 *
 * Since this converted sequence alters the offset table, all offsets must
 * be re-calculated. This may in turn trigger new branch conversions, so
 * the process is repeated until no further changes are made. Normally it
 * completes in 1-2 iterations. If JIT_MAX_ITERATIONS should reached, we
 * fall back to converting every remaining jump operation. The branch
 * conversion is independent of how the JMP or JMP32 condition is JITed.
 *
 * JMP32 and JMP operations are JITed as follows.
 *
 * 1) setup_jmp_{i,r}:
 *    Convert jump conditional and offset into a form that can be JITed.
 *    This form may be a no-op, a canonical form, or an inverted PC-relative
 *    jump if branch conversion is necessary.
 *
 * 2) valid_jmp_i:
 *    Determine if an immediate operations can be emitted as such, or if
 *    we must fall back to the register version. Applies to JMP32 for 32-bit
 *    MIPS, and both JMP and JMP32 for 64-bit MIPS.
 *
 * 3) emit_jmp_{i,i64,r,r64}:
 *    Emit instructions for an JMP or JMP32 immediate or register operation.
 *
 * 4) finish_jmp_{i,r}:
 *    Emit any instructions needed to finish the jump. This includes a nop
 *    for the delay slot if a branch was emitted, and a long absolute jump
 *    if the branch was converted.
 */

#include <linux/limits.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/slab.h>
#include <asm/bitops.h>
#include <asm/cacheflush.h>
#include <asm/cpu-features.h>
#include <asm/isa-rev.h>
#include <asm/uasm.h>

#include "bpf_jit_comp.h"

/* Convenience macros for descriptor access */
#define CONVERTED(desc)	((desc) & JIT_DESC_CONVERT)
#define INDEX(desc)	((desc) & ~JIT_DESC_CONVERT)

/*
 * Push registers on the stack, starting at a given depth from the stack
 * pointer and increasing. The next depth to be written is returned.
 */
int push_regs(struct jit_context *ctx, u32 mask, u32 excl, int depth)
{
	int reg;

	for (reg = 0; reg < BITS_PER_BYTE * sizeof(mask); reg++)
		if (mask & BIT(reg)) {
			if ((excl & BIT(reg)) == 0) {
				if (sizeof(long) == 4)
					emit(ctx, sw, reg, depth, MIPS_R_SP);
				else /* sizeof(long) == 8 */
					emit(ctx, sd, reg, depth, MIPS_R_SP);
			}
			depth += sizeof(long);
		}

	ctx->stack_used = max((int)ctx->stack_used, depth);
	return depth;
}

/*
 * Pop registers from the stack, starting at a given depth from the stack
 * pointer and increasing. The next depth to be read is returned.
 */
int pop_regs(struct jit_context *ctx, u32 mask, u32 excl, int depth)
{
	int reg;

	for (reg = 0; reg < BITS_PER_BYTE * sizeof(mask); reg++)
		if (mask & BIT(reg)) {
			if ((excl & BIT(reg)) == 0) {
				if (sizeof(long) == 4)
					emit(ctx, lw, reg, depth, MIPS_R_SP);
				else /* sizeof(long) == 8 */
					emit(ctx, ld, reg, depth, MIPS_R_SP);
			}
			depth += sizeof(long);
		}

	return depth;
}

/* Compute the 28-bit jump target address from a BPF program location */
int get_target(struct jit_context *ctx, u32 loc)
{
	u32 index = INDEX(ctx->descriptors[loc]);
	unsigned long pc = (unsigned long)&ctx->target[ctx->jit_index];
	unsigned long addr = (unsigned long)&ctx->target[index];

	if (!ctx->target)
		return 0;

	if ((addr ^ pc) & ~MIPS_JMP_MASK)
		return -1;

	return addr & MIPS_JMP_MASK;
}

/* Compute the PC-relative offset to relative BPF program offset */
int get_offset(const struct jit_context *ctx, int off)
{
	return (INDEX(ctx->descriptors[ctx->bpf_index + off]) -
		ctx->jit_index - 1) * sizeof(u32);
}

/* dst = imm (register width) */
void emit_mov_i(struct jit_context *ctx, u8 dst, s32 imm)
{
	if (imm >= -0x8000 && imm <= 0x7fff) {
		emit(ctx, addiu, dst, MIPS_R_ZERO, imm);
	} else {
		emit(ctx, lui, dst, (s16)((u32)imm >> 16));
		emit(ctx, ori, dst, dst, (u16)(imm & 0xffff));
	}
	clobber_reg(ctx, dst);
}

/* dst = src (register width) */
void emit_mov_r(struct jit_context *ctx, u8 dst, u8 src)
{
	emit(ctx, ori, dst, src, 0);
	clobber_reg(ctx, dst);
}

/* Validate ALU immediate range */
bool valid_alu_i(u8 op, s32 imm)
{
	switch (BPF_OP(op)) {
	case BPF_NEG:
	case BPF_LSH:
	case BPF_RSH:
	case BPF_ARSH:
		/* All legal eBPF values are valid */
		return true;
	case BPF_ADD:
		if (IS_ENABLED(CONFIG_CPU_DADDI_WORKAROUNDS))
			return false;
		/* imm must be 16 bits */
		return imm >= -0x8000 && imm <= 0x7fff;
	case BPF_SUB:
		if (IS_ENABLED(CONFIG_CPU_DADDI_WORKAROUNDS))
			return false;
		/* -imm must be 16 bits */
		return imm >= -0x7fff && imm <= 0x8000;
	case BPF_AND:
	case BPF_OR:
	case BPF_XOR:
		/* imm must be 16 bits unsigned */
		return imm >= 0 && imm <= 0xffff;
	case BPF_MUL:
		/* imm must be zero or a positive power of two */
		return imm == 0 || (imm > 0 && is_power_of_2(imm));
	case BPF_DIV:
	case BPF_MOD:
		/* imm must be an 17-bit power of two */
		return (u32)imm <= 0x10000 && is_power_of_2((u32)imm);
	}
	return false;
}

/* Rewrite ALU immediate operation */
bool rewrite_alu_i(u8 op, s32 imm, u8 *alu, s32 *val)
{
	bool act = true;

	switch (BPF_OP(op)) {
	case BPF_LSH:
	case BPF_RSH:
	case BPF_ARSH:
	case BPF_ADD:
	case BPF_SUB:
	case BPF_OR:
	case BPF_XOR:
		/* imm == 0 is a no-op */
		act = imm != 0;
		break;
	case BPF_MUL:
		if (imm == 1) {
			/* dst * 1 is a no-op */
			act = false;
		} else if (imm == 0) {
			/* dst * 0 is dst & 0 */
			op = BPF_AND;
		} else {
			/* dst * (1 << n) is dst << n */
			op = BPF_LSH;
			imm = ilog2(abs(imm));
		}
		break;
	case BPF_DIV:
		if (imm == 1) {
			/* dst / 1 is a no-op */
			act = false;
		} else {
			/* dst / (1 << n) is dst >> n */
			op = BPF_RSH;
			imm = ilog2(imm);
		}
		break;
	case BPF_MOD:
		/* dst % (1 << n) is dst & ((1 << n) - 1) */
		op = BPF_AND;
		imm--;
		break;
	}

	*alu = op;
	*val = imm;
	return act;
}

/* ALU immediate operation (32-bit) */
void emit_alu_i(struct jit_context *ctx, u8 dst, s32 imm, u8 op)
{
	switch (BPF_OP(op)) {
	/* dst = -dst */
	case BPF_NEG:
		emit(ctx, subu, dst, MIPS_R_ZERO, dst);
		break;
	/* dst = dst & imm */
	case BPF_AND:
		emit(ctx, andi, dst, dst, (u16)imm);
		break;
	/* dst = dst | imm */
	case BPF_OR:
		emit(ctx, ori, dst, dst, (u16)imm);
		break;
	/* dst = dst ^ imm */
	case BPF_XOR:
		emit(ctx, xori, dst, dst, (u16)imm);
		break;
	/* dst = dst << imm */
	case BPF_LSH:
		emit(ctx, sll, dst, dst, imm);
		break;
	/* dst = dst >> imm */
	case BPF_RSH:
		emit(ctx, srl, dst, dst, imm);
		break;
	/* dst = dst >> imm (arithmetic) */
	case BPF_ARSH:
		emit(ctx, sra, dst, dst, imm);
		break;
	/* dst = dst + imm */
	case BPF_ADD:
		emit(ctx, addiu, dst, dst, imm);
		break;
	/* dst = dst - imm */
	case BPF_SUB:
		emit(ctx, addiu, dst, dst, -imm);
		break;
	}
	clobber_reg(ctx, dst);
}

/* ALU register operation (32-bit) */
void emit_alu_r(struct jit_context *ctx, u8 dst, u8 src, u8 op)
{
	switch (BPF_OP(op)) {
	/* dst = dst & src */
	case BPF_AND:
		emit(ctx, and, dst, dst, src);
		break;
	/* dst = dst | src */
	case BPF_OR:
		emit(ctx, or, dst, dst, src);
		break;
	/* dst = dst ^ src */
	case BPF_XOR:
		emit(ctx, xor, dst, dst, src);
		break;
	/* dst = dst << src */
	case BPF_LSH:
		emit(ctx, sllv, dst, dst, src);
		break;
	/* dst = dst >> src */
	case BPF_RSH:
		emit(ctx, srlv, dst, dst, src);
		break;
	/* dst = dst >> src (arithmetic) */
	case BPF_ARSH:
		emit(ctx, srav, dst, dst, src);
		break;
	/* dst = dst + src */
	case BPF_ADD:
		emit(ctx, addu, dst, dst, src);
		break;
	/* dst = dst - src */
	case BPF_SUB:
		emit(ctx, subu, dst, dst, src);
		break;
	/* dst = dst * src */
	case BPF_MUL:
		if (cpu_has_mips32r1 || cpu_has_mips32r6) {
			emit(ctx, mul, dst, dst, src);
		} else {
			emit(ctx, multu, dst, src);
			emit(ctx, mflo, dst);
		}
		break;
	/* dst = dst / src */
	case BPF_DIV:
		if (cpu_has_mips32r6) {
			emit(ctx, divu_r6, dst, dst, src);
		} else {
			emit(ctx, divu, dst, src);
			emit(ctx, mflo, dst);
		}
		break;
	/* dst = dst % src */
	case BPF_MOD:
		if (cpu_has_mips32r6) {
			emit(ctx, modu, dst, dst, src);
		} else {
			emit(ctx, divu, dst, src);
			emit(ctx, mfhi, dst);
		}
		break;
	}
	clobber_reg(ctx, dst);
}

/* Atomic read-modify-write (32-bit) */
void emit_atomic_r(struct jit_context *ctx, u8 dst, u8 src, s16 off, u8 code)
{
	LLSC_sync(ctx);
	emit(ctx, ll, MIPS_R_T9, off, dst);
	switch (code) {
	case BPF_ADD:
	case BPF_ADD | BPF_FETCH:
		emit(ctx, addu, MIPS_R_T8, MIPS_R_T9, src);
		break;
	case BPF_AND:
	case BPF_AND | BPF_FETCH:
		emit(ctx, and, MIPS_R_T8, MIPS_R_T9, src);
		break;
	case BPF_OR:
	case BPF_OR | BPF_FETCH:
		emit(ctx, or, MIPS_R_T8, MIPS_R_T9, src);
		break;
	case BPF_XOR:
	case BPF_XOR | BPF_FETCH:
		emit(ctx, xor, MIPS_R_T8, MIPS_R_T9, src);
		break;
	case BPF_XCHG:
		emit(ctx, move, MIPS_R_T8, src);
		break;
	}
	emit(ctx, sc, MIPS_R_T8, off, dst);
	emit(ctx, LLSC_beqz, MIPS_R_T8, -16 - LLSC_offset);
	emit(ctx, nop); /* Delay slot */

	if (code & BPF_FETCH) {
		emit(ctx, move, src, MIPS_R_T9);
		clobber_reg(ctx, src);
	}
}

/* Atomic compare-and-exchange (32-bit) */
void emit_cmpxchg_r(struct jit_context *ctx, u8 dst, u8 src, u8 res, s16 off)
{
	LLSC_sync(ctx);
	emit(ctx, ll, MIPS_R_T9, off, dst);
	emit(ctx, bne, MIPS_R_T9, res, 12);
	emit(ctx, move, MIPS_R_T8, src);     /* Delay slot */
	emit(ctx, sc, MIPS_R_T8, off, dst);
	emit(ctx, LLSC_beqz, MIPS_R_T8, -20 - LLSC_offset);
	emit(ctx, move, res, MIPS_R_T9);     /* Delay slot */
	clobber_reg(ctx, res);
}

/* Swap bytes and truncate a register word or half word */
void emit_bswap_r(struct jit_context *ctx, u8 dst, u32 width)
{
	u8 tmp = MIPS_R_T8;
	u8 msk = MIPS_R_T9;

	switch (width) {
	/* Swap bytes in a word */
	case 32:
		if (cpu_has_mips32r2 || cpu_has_mips32r6) {
			emit(ctx, wsbh, dst, dst);
			emit(ctx, rotr, dst, dst, 16);
		} else {
			emit(ctx, sll, tmp, dst, 16);    /* tmp  = dst << 16 */
			emit(ctx, srl, dst, dst, 16);    /* dst = dst >> 16  */
			emit(ctx, or, dst, dst, tmp);    /* dst = dst | tmp  */

			emit(ctx, lui, msk, 0xff);       /* msk = 0x00ff0000 */
			emit(ctx, ori, msk, msk, 0xff);  /* msk = msk | 0xff */

			emit(ctx, and, tmp, dst, msk);   /* tmp = dst & msk  */
			emit(ctx, sll, tmp, tmp, 8);     /* tmp = tmp << 8   */
			emit(ctx, srl, dst, dst, 8);     /* dst = dst >> 8   */
			emit(ctx, and, dst, dst, msk);   /* dst = dst & msk  */
			emit(ctx, or, dst, dst, tmp);    /* reg = dst | tmp  */
		}
		break;
	/* Swap bytes in a half word */
	case 16:
		if (cpu_has_mips32r2 || cpu_has_mips32r6) {
			emit(ctx, wsbh, dst, dst);
			emit(ctx, andi, dst, dst, 0xffff);
		} else {
			emit(ctx, andi, tmp, dst, 0xff00); /* t = d & 0xff00 */
			emit(ctx, srl, tmp, tmp, 8);       /* t = t >> 8     */
			emit(ctx, andi, dst, dst, 0x00ff); /* d = d & 0x00ff */
			emit(ctx, sll, dst, dst, 8);       /* d = d << 8     */
			emit(ctx, or,  dst, dst, tmp);     /* d = d | t      */
		}
		break;
	}
	clobber_reg(ctx, dst);
}

/* Validate jump immediate range */
bool valid_jmp_i(u8 op, s32 imm)
{
	switch (op) {
	case JIT_JNOP:
		/* Immediate value not used */
		return true;
	case BPF_JEQ:
	case BPF_JNE:
		/* No immediate operation */
		return false;
	case BPF_JSET:
	case JIT_JNSET:
		/* imm must be 16 bits unsigned */
		return imm >= 0 && imm <= 0xffff;
	case BPF_JGE:
	case BPF_JLT:
	case BPF_JSGE:
	case BPF_JSLT:
		/* imm must be 16 bits */
		return imm >= -0x8000 && imm <= 0x7fff;
	case BPF_JGT:
	case BPF_JLE:
	case BPF_JSGT:
	case BPF_JSLE:
		/* imm + 1 must be 16 bits */
		return imm >= -0x8001 && imm <= 0x7ffe;
	}
	return false;
}

/* Invert a conditional jump operation */
static u8 invert_jmp(u8 op)
{
	switch (op) {
	case BPF_JA: return JIT_JNOP;
	case BPF_JEQ: return BPF_JNE;
	case BPF_JNE: return BPF_JEQ;
	case BPF_JSET: return JIT_JNSET;
	case BPF_JGT: return BPF_JLE;
	case BPF_JGE: return BPF_JLT;
	case BPF_JLT: return BPF_JGE;
	case BPF_JLE: return BPF_JGT;
	case BPF_JSGT: return BPF_JSLE;
	case BPF_JSGE: return BPF_JSLT;
	case BPF_JSLT: return BPF_JSGE;
	case BPF_JSLE: return BPF_JSGT;
	}
	return 0;
}

/* Prepare a PC-relative jump operation */
static void setup_jmp(struct jit_context *ctx, u8 bpf_op,
		      s16 bpf_off, u8 *jit_op, s32 *jit_off)
{
	u32 *descp = &ctx->descriptors[ctx->bpf_index];
	int op = bpf_op;
	int offset = 0;

	/* Do not compute offsets on the first pass */
	if (INDEX(*descp) == 0)
		goto done;

	/* Skip jumps never taken */
	if (bpf_op == JIT_JNOP)
		goto done;

	/* Convert jumps always taken */
	if (bpf_op == BPF_JA)
		*descp |= JIT_DESC_CONVERT;

	/*
	 * Current ctx->jit_index points to the start of the branch preamble.
	 * Since the preamble differs among different branch conditionals,
	 * the current index cannot be used to compute the branch offset.
	 * Instead, we use the offset table value for the next instruction,
	 * which gives the index immediately after the branch delay slot.
	 */
	if (!CONVERTED(*descp)) {
		int target = ctx->bpf_index + bpf_off + 1;
		int origin = ctx->bpf_index + 1;

		offset = (INDEX(ctx->descriptors[target]) -
			  INDEX(ctx->descriptors[origin]) + 1) * sizeof(u32);
	}

	/*
	 * The PC-relative branch offset field on MIPS is 18 bits signed,
	 * so if the computed offset is larger than this we generate a an
	 * absolute jump that we skip with an inverted conditional branch.
	 */
	if (CONVERTED(*descp) || offset < -0x20000 || offset > 0x1ffff) {
		offset = 3 * sizeof(u32);
		op = invert_jmp(bpf_op);
		ctx->changes += !CONVERTED(*descp);
		*descp |= JIT_DESC_CONVERT;
	}

done:
	*jit_off = offset;
	*jit_op = op;
}

/* Prepare a PC-relative jump operation with immediate conditional */
void setup_jmp_i(struct jit_context *ctx, s32 imm, u8 width,
		 u8 bpf_op, s16 bpf_off, u8 *jit_op, s32 *jit_off)
{
	bool always = false;
	bool never = false;

	switch (bpf_op) {
	case BPF_JEQ:
	case BPF_JNE:
		break;
	case BPF_JSET:
	case BPF_JLT:
		never = imm == 0;
		break;
	case BPF_JGE:
		always = imm == 0;
		break;
	case BPF_JGT:
		never = (u32)imm == U32_MAX;
		break;
	case BPF_JLE:
		always = (u32)imm == U32_MAX;
		break;
	case BPF_JSGT:
		never = imm == S32_MAX && width == 32;
		break;
	case BPF_JSGE:
		always = imm == S32_MIN && width == 32;
		break;
	case BPF_JSLT:
		never = imm == S32_MIN && width == 32;
		break;
	case BPF_JSLE:
		always = imm == S32_MAX && width == 32;
		break;
	}

	if (never)
		bpf_op = JIT_JNOP;
	if (always)
		bpf_op = BPF_JA;
	setup_jmp(ctx, bpf_op, bpf_off, jit_op, jit_off);
}

/* Prepare a PC-relative jump operation with register conditional */
void setup_jmp_r(struct jit_context *ctx, bool same_reg,
		 u8 bpf_op, s16 bpf_off, u8 *jit_op, s32 *jit_off)
{
	switch (bpf_op) {
	case BPF_JSET:
		break;
	case BPF_JEQ:
	case BPF_JGE:
	case BPF_JLE:
	case BPF_JSGE:
	case BPF_JSLE:
		if (same_reg)
			bpf_op = BPF_JA;
		break;
	case BPF_JNE:
	case BPF_JLT:
	case BPF_JGT:
	case BPF_JSGT:
	case BPF_JSLT:
		if (same_reg)
			bpf_op = JIT_JNOP;
		break;
	}
	setup_jmp(ctx, bpf_op, bpf_off, jit_op, jit_off);
}

/* Finish a PC-relative jump operation */
int finish_jmp(struct jit_context *ctx, u8 jit_op, s16 bpf_off)
{
	/* Emit conditional branch delay slot */
	if (jit_op != JIT_JNOP)
		emit(ctx, nop);
	/*
	 * Emit an absolute long jump with delay slot,
	 * if the PC-relative branch was converted.
	 */
	if (CONVERTED(ctx->descriptors[ctx->bpf_index])) {
		int target = get_target(ctx, ctx->bpf_index + bpf_off + 1);

		if (target < 0)
			return -1;
		emit(ctx, j, target);
		emit(ctx, nop);
	}
	return 0;
}

/* Jump immediate (32-bit) */
void emit_jmp_i(struct jit_context *ctx, u8 dst, s32 imm, s32 off, u8 op)
{
	switch (op) {
	/* No-op, used internally for branch optimization */
	case JIT_JNOP:
		break;
	/* PC += off if dst & imm */
	case BPF_JSET:
		emit(ctx, andi, MIPS_R_T9, dst, (u16)imm);
		emit(ctx, bnez, MIPS_R_T9, off);
		break;
	/* PC += off if (dst & imm) == 0 (not in BPF, used for long jumps) */
	case JIT_JNSET:
		emit(ctx, andi, MIPS_R_T9, dst, (u16)imm);
		emit(ctx, beqz, MIPS_R_T9, off);
		break;
	/* PC += off if dst > imm */
	case BPF_JGT:
		emit(ctx, sltiu, MIPS_R_T9, dst, imm + 1);
		emit(ctx, beqz, MIPS_R_T9, off);
		break;
	/* PC += off if dst >= imm */
	case BPF_JGE:
		emit(ctx, sltiu, MIPS_R_T9, dst, imm);
		emit(ctx, beqz, MIPS_R_T9, off);
		break;
	/* PC += off if dst < imm */
	case BPF_JLT:
		emit(ctx, sltiu, MIPS_R_T9, dst, imm);
		emit(ctx, bnez, MIPS_R_T9, off);
		break;
	/* PC += off if dst <= imm */
	case BPF_JLE:
		emit(ctx, sltiu, MIPS_R_T9, dst, imm + 1);
		emit(ctx, bnez, MIPS_R_T9, off);
		break;
	/* PC += off if dst > imm (signed) */
	case BPF_JSGT:
		emit(ctx, slti, MIPS_R_T9, dst, imm + 1);
		emit(ctx, beqz, MIPS_R_T9, off);
		break;
	/* PC += off if dst >= imm (signed) */
	case BPF_JSGE:
		emit(ctx, slti, MIPS_R_T9, dst, imm);
		emit(ctx, beqz, MIPS_R_T9, off);
		break;
	/* PC += off if dst < imm (signed) */
	case BPF_JSLT:
		emit(ctx, slti, MIPS_R_T9, dst, imm);
		emit(ctx, bnez, MIPS_R_T9, off);
		break;
	/* PC += off if dst <= imm (signed) */
	case BPF_JSLE:
		emit(ctx, slti, MIPS_R_T9, dst, imm + 1);
		emit(ctx, bnez, MIPS_R_T9, off);
		break;
	}
}

/* Jump register (32-bit) */
void emit_jmp_r(struct jit_context *ctx, u8 dst, u8 src, s32 off, u8 op)
{
	switch (op) {
	/* No-op, used internally for branch optimization */
	case JIT_JNOP:
		break;
	/* PC += off if dst == src */
	case BPF_JEQ:
		emit(ctx, beq, dst, src, off);
		break;
	/* PC += off if dst != src */
	case BPF_JNE:
		emit(ctx, bne, dst, src, off);
		break;
	/* PC += off if dst & src */
	case BPF_JSET:
		emit(ctx, and, MIPS_R_T9, dst, src);
		emit(ctx, bnez, MIPS_R_T9, off);
		break;
	/* PC += off if (dst & imm) == 0 (not in BPF, used for long jumps) */
	case JIT_JNSET:
		emit(ctx, and, MIPS_R_T9, dst, src);
		emit(ctx, beqz, MIPS_R_T9, off);
		break;
	/* PC += off if dst > src */
	case BPF_JGT:
		emit(ctx, sltu, MIPS_R_T9, src, dst);
		emit(ctx, bnez, MIPS_R_T9, off);
		break;
	/* PC += off if dst >= src */
	case BPF_JGE:
		emit(ctx, sltu, MIPS_R_T9, dst, src);
		emit(ctx, beqz, MIPS_R_T9, off);
		break;
	/* PC += off if dst < src */
	case BPF_JLT:
		emit(ctx, sltu, MIPS_R_T9, dst, src);
		emit(ctx, bnez, MIPS_R_T9, off);
		break;
	/* PC += off if dst <= src */
	case BPF_JLE:
		emit(ctx, sltu, MIPS_R_T9, src, dst);
		emit(ctx, beqz, MIPS_R_T9, off);
		break;
	/* PC += off if dst > src (signed) */
	case BPF_JSGT:
		emit(ctx, slt, MIPS_R_T9, src, dst);
		emit(ctx, bnez, MIPS_R_T9, off);
		break;
	/* PC += off if dst >= src (signed) */
	case BPF_JSGE:
		emit(ctx, slt, MIPS_R_T9, dst, src);
		emit(ctx, beqz, MIPS_R_T9, off);
		break;
	/* PC += off if dst < src (signed) */
	case BPF_JSLT:
		emit(ctx, slt, MIPS_R_T9, dst, src);
		emit(ctx, bnez, MIPS_R_T9, off);
		break;
	/* PC += off if dst <= src (signed) */
	case BPF_JSLE:
		emit(ctx, slt, MIPS_R_T9, src, dst);
		emit(ctx, beqz, MIPS_R_T9, off);
		break;
	}
}

/* Jump always */
int emit_ja(struct jit_context *ctx, s16 off)
{
	int target = get_target(ctx, ctx->bpf_index + off + 1);

	if (target < 0)
		return -1;
	emit(ctx, j, target);
	emit(ctx, nop);
	return 0;
}

/* Jump to epilogue */
int emit_exit(struct jit_context *ctx)
{
	int target = get_target(ctx, ctx->program->len);

	if (target < 0)
		return -1;
	emit(ctx, j, target);
	emit(ctx, nop);
	return 0;
}

/* Build the program body from eBPF bytecode */
static int build_body(struct jit_context *ctx)
{
	const struct bpf_prog *prog = ctx->program;
	unsigned int i;

	ctx->stack_used = 0;
	for (i = 0; i < prog->len; i++) {
		const struct bpf_insn *insn = &prog->insnsi[i];
		u32 *descp = &ctx->descriptors[i];
		int ret;

		access_reg(ctx, insn->src_reg);
		access_reg(ctx, insn->dst_reg);

		ctx->bpf_index = i;
		if (ctx->target == NULL) {
			ctx->changes += INDEX(*descp) != ctx->jit_index;
			*descp &= JIT_DESC_CONVERT;
			*descp |= ctx->jit_index;
		}

		ret = build_insn(insn, ctx);
		if (ret < 0)
			return ret;

		if (ret > 0) {
			i++;
			if (ctx->target == NULL)
				descp[1] = ctx->jit_index;
		}
	}

	/* Store the end offset, where the epilogue begins */
	ctx->descriptors[prog->len] = ctx->jit_index;
	return 0;
}

/* Set the branch conversion flag on all instructions */
static void set_convert_flag(struct jit_context *ctx, bool enable)
{
	const struct bpf_prog *prog = ctx->program;
	u32 flag = enable ? JIT_DESC_CONVERT : 0;
	unsigned int i;

	for (i = 0; i <= prog->len; i++)
		ctx->descriptors[i] = INDEX(ctx->descriptors[i]) | flag;
}

static void jit_fill_hole(void *area, unsigned int size)
{
	u32 *p;

	/* We are guaranteed to have aligned memory. */
	for (p = area; size >= sizeof(u32); size -= sizeof(u32))
		uasm_i_break(&p, BRK_BUG); /* Increments p */
}

bool bpf_jit_needs_zext(void)
{
	return true;
}

struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *prog)
{
	struct bpf_prog *tmp, *orig_prog = prog;
	struct bpf_binary_header *header = NULL;
	struct jit_context ctx;
	bool tmp_blinded = false;
	unsigned int tmp_idx;
	unsigned int image_size;
	u8 *image_ptr;
	int tries;

	/*
	 * If BPF JIT was not enabled then we must fall back to
	 * the interpreter.
	 */
	if (!prog->jit_requested)
		return orig_prog;
	/*
	 * If constant blinding was enabled and we failed during blinding
	 * then we must fall back to the interpreter. Otherwise, we save
	 * the new JITed code.
	 */
	tmp = bpf_jit_blind_constants(prog);
	if (IS_ERR(tmp))
		return orig_prog;
	if (tmp != prog) {
		tmp_blinded = true;
		prog = tmp;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.program = prog;

	/*
	 * Not able to allocate memory for descriptors[], then
	 * we must fall back to the interpreter
	 */
	ctx.descriptors = kcalloc(prog->len + 1, sizeof(*ctx.descriptors),
				  GFP_KERNEL);
	if (ctx.descriptors == NULL)
		goto out_err;

	/* First pass discovers used resources */
	if (build_body(&ctx) < 0)
		goto out_err;
	/*
	 * Second pass computes instruction offsets.
	 * If any PC-relative branches are out of range, a sequence of
	 * a PC-relative branch + a jump is generated, and we have to
	 * try again from the beginning to generate the new offsets.
	 * This is done until no additional conversions are necessary.
	 * The last two iterations are done with all branches being
	 * converted, to guarantee offset table convergence within a
	 * fixed number of iterations.
	 */
	ctx.jit_index = 0;
	build_prologue(&ctx);
	tmp_idx = ctx.jit_index;

	tries = JIT_MAX_ITERATIONS;
	do {
		ctx.jit_index = tmp_idx;
		ctx.changes = 0;
		if (tries == 2)
			set_convert_flag(&ctx, true);
		if (build_body(&ctx) < 0)
			goto out_err;
	} while (ctx.changes > 0 && --tries > 0);

	if (WARN_ONCE(ctx.changes > 0, "JIT offsets failed to converge"))
		goto out_err;

	build_epilogue(&ctx, MIPS_R_RA);

	/* Now we know the size of the structure to make */
	image_size = sizeof(u32) * ctx.jit_index;
	header = bpf_jit_binary_alloc(image_size, &image_ptr,
				      sizeof(u32), jit_fill_hole);
	/*
	 * Not able to allocate memory for the structure then
	 * we must fall back to the interpretation
	 */
	if (header == NULL)
		goto out_err;

	/* Actual pass to generate final JIT code */
	ctx.target = (u32 *)image_ptr;
	ctx.jit_index = 0;

	/*
	 * If building the JITed code fails somehow,
	 * we fall back to the interpretation.
	 */
	build_prologue(&ctx);
	if (build_body(&ctx) < 0)
		goto out_err;
	build_epilogue(&ctx, MIPS_R_RA);

	/* Populate line info meta data */
	set_convert_flag(&ctx, false);
	bpf_prog_fill_jited_linfo(prog, &ctx.descriptors[1]);

	/* Set as read-only exec and flush instruction cache */
	bpf_jit_binary_lock_ro(header);
	flush_icache_range((unsigned long)header,
			   (unsigned long)&ctx.target[ctx.jit_index]);

	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, image_size, 2, ctx.target);

	prog->bpf_func = (void *)ctx.target;
	prog->jited = 1;
	prog->jited_len = image_size;

out:
	if (tmp_blinded)
		bpf_jit_prog_release_other(prog, prog == orig_prog ?
					   tmp : orig_prog);
	kfree(ctx.descriptors);
	return prog;

out_err:
	prog = orig_prog;
	if (header)
		bpf_jit_binary_free(header);
	goto out;
}
