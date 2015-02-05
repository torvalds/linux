/*
 * BPF JIT compiler for ARM64
 *
 * Copyright (C) 2014 Zi Shen Lim <zlim.lnx@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "bpf_jit: " fmt

#include <linux/filter.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/cacheflush.h>
#include <asm/debug-monitors.h>

#include "bpf_jit.h"

int bpf_jit_enable __read_mostly;

#define TMP_REG_1 (MAX_BPF_REG + 0)
#define TMP_REG_2 (MAX_BPF_REG + 1)

/* Map BPF registers to A64 registers */
static const int bpf2a64[] = {
	/* return value from in-kernel function, and exit value from eBPF */
	[BPF_REG_0] = A64_R(7),
	/* arguments from eBPF program to in-kernel function */
	[BPF_REG_1] = A64_R(0),
	[BPF_REG_2] = A64_R(1),
	[BPF_REG_3] = A64_R(2),
	[BPF_REG_4] = A64_R(3),
	[BPF_REG_5] = A64_R(4),
	/* callee saved registers that in-kernel function will preserve */
	[BPF_REG_6] = A64_R(19),
	[BPF_REG_7] = A64_R(20),
	[BPF_REG_8] = A64_R(21),
	[BPF_REG_9] = A64_R(22),
	/* read-only frame pointer to access stack */
	[BPF_REG_FP] = A64_FP,
	/* temporary register for internal BPF JIT */
	[TMP_REG_1] = A64_R(23),
	[TMP_REG_2] = A64_R(24),
};

struct jit_ctx {
	const struct bpf_prog *prog;
	int idx;
	int tmp_used;
	int epilogue_offset;
	int *offset;
	u32 *image;
};

static inline void emit(const u32 insn, struct jit_ctx *ctx)
{
	if (ctx->image != NULL)
		ctx->image[ctx->idx] = cpu_to_le32(insn);

	ctx->idx++;
}

static inline void emit_a64_mov_i64(const int reg, const u64 val,
				    struct jit_ctx *ctx)
{
	u64 tmp = val;
	int shift = 0;

	emit(A64_MOVZ(1, reg, tmp & 0xffff, shift), ctx);
	tmp >>= 16;
	shift += 16;
	while (tmp) {
		if (tmp & 0xffff)
			emit(A64_MOVK(1, reg, tmp & 0xffff, shift), ctx);
		tmp >>= 16;
		shift += 16;
	}
}

static inline void emit_a64_mov_i(const int is64, const int reg,
				  const s32 val, struct jit_ctx *ctx)
{
	u16 hi = val >> 16;
	u16 lo = val & 0xffff;

	if (hi & 0x8000) {
		if (hi == 0xffff) {
			emit(A64_MOVN(is64, reg, (u16)~lo, 0), ctx);
		} else {
			emit(A64_MOVN(is64, reg, (u16)~hi, 16), ctx);
			emit(A64_MOVK(is64, reg, lo, 0), ctx);
		}
	} else {
		emit(A64_MOVZ(is64, reg, lo, 0), ctx);
		if (hi)
			emit(A64_MOVK(is64, reg, hi, 16), ctx);
	}
}

static inline int bpf2a64_offset(int bpf_to, int bpf_from,
				 const struct jit_ctx *ctx)
{
	int to = ctx->offset[bpf_to + 1];
	/* -1 to account for the Branch instruction */
	int from = ctx->offset[bpf_from + 1] - 1;

	return to - from;
}

static void jit_fill_hole(void *area, unsigned int size)
{
	u32 *ptr;
	/* We are guaranteed to have aligned memory. */
	for (ptr = area; size >= sizeof(u32); size -= sizeof(u32))
		*ptr++ = cpu_to_le32(AARCH64_BREAK_FAULT);
}

static inline int epilogue_offset(const struct jit_ctx *ctx)
{
	int to = ctx->epilogue_offset;
	int from = ctx->idx;

	return to - from;
}

/* Stack must be multiples of 16B */
#define STACK_ALIGN(sz) (((sz) + 15) & ~15)

static void build_prologue(struct jit_ctx *ctx)
{
	const u8 r6 = bpf2a64[BPF_REG_6];
	const u8 r7 = bpf2a64[BPF_REG_7];
	const u8 r8 = bpf2a64[BPF_REG_8];
	const u8 r9 = bpf2a64[BPF_REG_9];
	const u8 fp = bpf2a64[BPF_REG_FP];
	const u8 ra = bpf2a64[BPF_REG_A];
	const u8 rx = bpf2a64[BPF_REG_X];
	const u8 tmp1 = bpf2a64[TMP_REG_1];
	const u8 tmp2 = bpf2a64[TMP_REG_2];
	int stack_size = MAX_BPF_STACK;

	stack_size += 4; /* extra for skb_copy_bits buffer */
	stack_size = STACK_ALIGN(stack_size);

	/* Save callee-saved register */
	emit(A64_PUSH(r6, r7, A64_SP), ctx);
	emit(A64_PUSH(r8, r9, A64_SP), ctx);
	if (ctx->tmp_used)
		emit(A64_PUSH(tmp1, tmp2, A64_SP), ctx);

	/* Set up BPF stack */
	emit(A64_SUB_I(1, A64_SP, A64_SP, stack_size), ctx);

	/* Set up frame pointer */
	emit(A64_MOV(1, fp, A64_SP), ctx);

	/* Clear registers A and X */
	emit_a64_mov_i64(ra, 0, ctx);
	emit_a64_mov_i64(rx, 0, ctx);
}

static void build_epilogue(struct jit_ctx *ctx)
{
	const u8 r0 = bpf2a64[BPF_REG_0];
	const u8 r6 = bpf2a64[BPF_REG_6];
	const u8 r7 = bpf2a64[BPF_REG_7];
	const u8 r8 = bpf2a64[BPF_REG_8];
	const u8 r9 = bpf2a64[BPF_REG_9];
	const u8 fp = bpf2a64[BPF_REG_FP];
	const u8 tmp1 = bpf2a64[TMP_REG_1];
	const u8 tmp2 = bpf2a64[TMP_REG_2];
	int stack_size = MAX_BPF_STACK;

	stack_size += 4; /* extra for skb_copy_bits buffer */
	stack_size = STACK_ALIGN(stack_size);

	/* We're done with BPF stack */
	emit(A64_ADD_I(1, A64_SP, A64_SP, stack_size), ctx);

	/* Restore callee-saved register */
	if (ctx->tmp_used)
		emit(A64_POP(tmp1, tmp2, A64_SP), ctx);
	emit(A64_POP(r8, r9, A64_SP), ctx);
	emit(A64_POP(r6, r7, A64_SP), ctx);

	/* Restore frame pointer */
	emit(A64_MOV(1, fp, A64_SP), ctx);

	/* Set return value */
	emit(A64_MOV(1, A64_R(0), r0), ctx);

	emit(A64_RET(A64_LR), ctx);
}

/* JITs an eBPF instruction.
 * Returns:
 * 0  - successfully JITed an 8-byte eBPF instruction.
 * >0 - successfully JITed a 16-byte eBPF instruction.
 * <0 - failed to JIT.
 */
static int build_insn(const struct bpf_insn *insn, struct jit_ctx *ctx)
{
	const u8 code = insn->code;
	const u8 dst = bpf2a64[insn->dst_reg];
	const u8 src = bpf2a64[insn->src_reg];
	const u8 tmp = bpf2a64[TMP_REG_1];
	const u8 tmp2 = bpf2a64[TMP_REG_2];
	const s16 off = insn->off;
	const s32 imm = insn->imm;
	const int i = insn - ctx->prog->insnsi;
	const bool is64 = BPF_CLASS(code) == BPF_ALU64;
	u8 jmp_cond;
	s32 jmp_offset;

	switch (code) {
	/* dst = src */
	case BPF_ALU | BPF_MOV | BPF_X:
	case BPF_ALU64 | BPF_MOV | BPF_X:
		emit(A64_MOV(is64, dst, src), ctx);
		break;
	/* dst = dst OP src */
	case BPF_ALU | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_ADD | BPF_X:
		emit(A64_ADD(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
		emit(A64_SUB(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_AND | BPF_X:
		emit(A64_AND(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_X:
	case BPF_ALU64 | BPF_OR | BPF_X:
		emit(A64_ORR(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
		emit(A64_EOR(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_X:
	case BPF_ALU64 | BPF_MUL | BPF_X:
		emit(A64_MUL(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_DIV | BPF_X:
		emit(A64_UDIV(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
		ctx->tmp_used = 1;
		emit(A64_UDIV(is64, tmp, dst, src), ctx);
		emit(A64_MUL(is64, tmp, tmp, src), ctx);
		emit(A64_SUB(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_LSH | BPF_X:
	case BPF_ALU64 | BPF_LSH | BPF_X:
		emit(A64_LSLV(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_X:
	case BPF_ALU64 | BPF_RSH | BPF_X:
		emit(A64_LSRV(is64, dst, dst, src), ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_X:
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		emit(A64_ASRV(is64, dst, dst, src), ctx);
		break;
	/* dst = -dst */
	case BPF_ALU | BPF_NEG:
	case BPF_ALU64 | BPF_NEG:
		emit(A64_NEG(is64, dst, dst), ctx);
		break;
	/* dst = BSWAP##imm(dst) */
	case BPF_ALU | BPF_END | BPF_FROM_LE:
	case BPF_ALU | BPF_END | BPF_FROM_BE:
#ifdef CONFIG_CPU_BIG_ENDIAN
		if (BPF_SRC(code) == BPF_FROM_BE)
			break;
#else /* !CONFIG_CPU_BIG_ENDIAN */
		if (BPF_SRC(code) == BPF_FROM_LE)
			break;
#endif
		switch (imm) {
		case 16:
			emit(A64_REV16(is64, dst, dst), ctx);
			break;
		case 32:
			emit(A64_REV32(is64, dst, dst), ctx);
			break;
		case 64:
			emit(A64_REV64(dst, dst), ctx);
			break;
		}
		break;
	/* dst = imm */
	case BPF_ALU | BPF_MOV | BPF_K:
	case BPF_ALU64 | BPF_MOV | BPF_K:
		emit_a64_mov_i(is64, dst, imm, ctx);
		break;
	/* dst = dst OP imm */
	case BPF_ALU | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_ADD | BPF_K:
		ctx->tmp_used = 1;
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_ADD(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_SUB | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
		ctx->tmp_used = 1;
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_SUB(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_AND | BPF_K:
	case BPF_ALU64 | BPF_AND | BPF_K:
		ctx->tmp_used = 1;
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_AND(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_OR | BPF_K:
	case BPF_ALU64 | BPF_OR | BPF_K:
		ctx->tmp_used = 1;
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_ORR(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_XOR | BPF_K:
	case BPF_ALU64 | BPF_XOR | BPF_K:
		ctx->tmp_used = 1;
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_EOR(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_MUL | BPF_K:
	case BPF_ALU64 | BPF_MUL | BPF_K:
		ctx->tmp_used = 1;
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_MUL(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_DIV | BPF_K:
		ctx->tmp_used = 1;
		emit_a64_mov_i(is64, tmp, imm, ctx);
		emit(A64_UDIV(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_MOD | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		ctx->tmp_used = 1;
		emit_a64_mov_i(is64, tmp2, imm, ctx);
		emit(A64_UDIV(is64, tmp, dst, tmp2), ctx);
		emit(A64_MUL(is64, tmp, tmp, tmp2), ctx);
		emit(A64_SUB(is64, dst, dst, tmp), ctx);
		break;
	case BPF_ALU | BPF_LSH | BPF_K:
	case BPF_ALU64 | BPF_LSH | BPF_K:
		emit(A64_LSL(is64, dst, dst, imm), ctx);
		break;
	case BPF_ALU | BPF_RSH | BPF_K:
	case BPF_ALU64 | BPF_RSH | BPF_K:
		emit(A64_LSR(is64, dst, dst, imm), ctx);
		break;
	case BPF_ALU | BPF_ARSH | BPF_K:
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		emit(A64_ASR(is64, dst, dst, imm), ctx);
		break;

#define check_imm(bits, imm) do {				\
	if ((((imm) > 0) && ((imm) >> (bits))) ||		\
	    (((imm) < 0) && (~(imm) >> (bits)))) {		\
		pr_info("[%2d] imm=%d(0x%x) out of range\n",	\
			i, imm, imm);				\
		return -EINVAL;					\
	}							\
} while (0)
#define check_imm19(imm) check_imm(19, imm)
#define check_imm26(imm) check_imm(26, imm)

	/* JUMP off */
	case BPF_JMP | BPF_JA:
		jmp_offset = bpf2a64_offset(i + off, i, ctx);
		check_imm26(jmp_offset);
		emit(A64_B(jmp_offset), ctx);
		break;
	/* IF (dst COND src) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_X:
	case BPF_JMP | BPF_JGT | BPF_X:
	case BPF_JMP | BPF_JGE | BPF_X:
	case BPF_JMP | BPF_JNE | BPF_X:
	case BPF_JMP | BPF_JSGT | BPF_X:
	case BPF_JMP | BPF_JSGE | BPF_X:
		emit(A64_CMP(1, dst, src), ctx);
emit_cond_jmp:
		jmp_offset = bpf2a64_offset(i + off, i, ctx);
		check_imm19(jmp_offset);
		switch (BPF_OP(code)) {
		case BPF_JEQ:
			jmp_cond = A64_COND_EQ;
			break;
		case BPF_JGT:
			jmp_cond = A64_COND_HI;
			break;
		case BPF_JGE:
			jmp_cond = A64_COND_CS;
			break;
		case BPF_JNE:
			jmp_cond = A64_COND_NE;
			break;
		case BPF_JSGT:
			jmp_cond = A64_COND_GT;
			break;
		case BPF_JSGE:
			jmp_cond = A64_COND_GE;
			break;
		default:
			return -EFAULT;
		}
		emit(A64_B_(jmp_cond, jmp_offset), ctx);
		break;
	case BPF_JMP | BPF_JSET | BPF_X:
		emit(A64_TST(1, dst, src), ctx);
		goto emit_cond_jmp;
	/* IF (dst COND imm) JUMP off */
	case BPF_JMP | BPF_JEQ | BPF_K:
	case BPF_JMP | BPF_JGT | BPF_K:
	case BPF_JMP | BPF_JGE | BPF_K:
	case BPF_JMP | BPF_JNE | BPF_K:
	case BPF_JMP | BPF_JSGT | BPF_K:
	case BPF_JMP | BPF_JSGE | BPF_K:
		ctx->tmp_used = 1;
		emit_a64_mov_i(1, tmp, imm, ctx);
		emit(A64_CMP(1, dst, tmp), ctx);
		goto emit_cond_jmp;
	case BPF_JMP | BPF_JSET | BPF_K:
		ctx->tmp_used = 1;
		emit_a64_mov_i(1, tmp, imm, ctx);
		emit(A64_TST(1, dst, tmp), ctx);
		goto emit_cond_jmp;
	/* function call */
	case BPF_JMP | BPF_CALL:
	{
		const u8 r0 = bpf2a64[BPF_REG_0];
		const u64 func = (u64)__bpf_call_base + imm;

		ctx->tmp_used = 1;
		emit_a64_mov_i64(tmp, func, ctx);
		emit(A64_PUSH(A64_FP, A64_LR, A64_SP), ctx);
		emit(A64_MOV(1, A64_FP, A64_SP), ctx);
		emit(A64_BLR(tmp), ctx);
		emit(A64_MOV(1, r0, A64_R(0)), ctx);
		emit(A64_POP(A64_FP, A64_LR, A64_SP), ctx);
		break;
	}
	/* function return */
	case BPF_JMP | BPF_EXIT:
		/* Optimization: when last instruction is EXIT,
		   simply fallthrough to epilogue. */
		if (i == ctx->prog->len - 1)
			break;
		jmp_offset = epilogue_offset(ctx);
		check_imm26(jmp_offset);
		emit(A64_B(jmp_offset), ctx);
		break;

	/* dst = imm64 */
	case BPF_LD | BPF_IMM | BPF_DW:
	{
		const struct bpf_insn insn1 = insn[1];
		u64 imm64;

		if (insn1.code != 0 || insn1.src_reg != 0 ||
		    insn1.dst_reg != 0 || insn1.off != 0) {
			/* Note: verifier in BPF core must catch invalid
			 * instructions.
			 */
			pr_err_once("Invalid BPF_LD_IMM64 instruction\n");
			return -EINVAL;
		}

		imm64 = (u64)insn1.imm << 32 | imm;
		emit_a64_mov_i64(dst, imm64, ctx);

		return 1;
	}

	/* LDX: dst = *(size *)(src + off) */
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_DW:
		ctx->tmp_used = 1;
		emit_a64_mov_i(1, tmp, off, ctx);
		switch (BPF_SIZE(code)) {
		case BPF_W:
			emit(A64_LDR32(dst, src, tmp), ctx);
			break;
		case BPF_H:
			emit(A64_LDRH(dst, src, tmp), ctx);
			break;
		case BPF_B:
			emit(A64_LDRB(dst, src, tmp), ctx);
			break;
		case BPF_DW:
			emit(A64_LDR64(dst, src, tmp), ctx);
			break;
		}
		break;

	/* ST: *(size *)(dst + off) = imm */
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_B:
	case BPF_ST | BPF_MEM | BPF_DW:
		goto notyet;

	/* STX: *(size *)(dst + off) = src */
	case BPF_STX | BPF_MEM | BPF_W:
	case BPF_STX | BPF_MEM | BPF_H:
	case BPF_STX | BPF_MEM | BPF_B:
	case BPF_STX | BPF_MEM | BPF_DW:
		ctx->tmp_used = 1;
		emit_a64_mov_i(1, tmp, off, ctx);
		switch (BPF_SIZE(code)) {
		case BPF_W:
			emit(A64_STR32(src, dst, tmp), ctx);
			break;
		case BPF_H:
			emit(A64_STRH(src, dst, tmp), ctx);
			break;
		case BPF_B:
			emit(A64_STRB(src, dst, tmp), ctx);
			break;
		case BPF_DW:
			emit(A64_STR64(src, dst, tmp), ctx);
			break;
		}
		break;
	/* STX XADD: lock *(u32 *)(dst + off) += src */
	case BPF_STX | BPF_XADD | BPF_W:
	/* STX XADD: lock *(u64 *)(dst + off) += src */
	case BPF_STX | BPF_XADD | BPF_DW:
		goto notyet;

	/* R0 = ntohx(*(size *)(((struct sk_buff *)R6)->data + imm)) */
	case BPF_LD | BPF_ABS | BPF_W:
	case BPF_LD | BPF_ABS | BPF_H:
	case BPF_LD | BPF_ABS | BPF_B:
	/* R0 = ntohx(*(size *)(((struct sk_buff *)R6)->data + src + imm)) */
	case BPF_LD | BPF_IND | BPF_W:
	case BPF_LD | BPF_IND | BPF_H:
	case BPF_LD | BPF_IND | BPF_B:
	{
		const u8 r0 = bpf2a64[BPF_REG_0]; /* r0 = return value */
		const u8 r6 = bpf2a64[BPF_REG_6]; /* r6 = pointer to sk_buff */
		const u8 fp = bpf2a64[BPF_REG_FP];
		const u8 r1 = bpf2a64[BPF_REG_1]; /* r1: struct sk_buff *skb */
		const u8 r2 = bpf2a64[BPF_REG_2]; /* r2: int k */
		const u8 r3 = bpf2a64[BPF_REG_3]; /* r3: unsigned int size */
		const u8 r4 = bpf2a64[BPF_REG_4]; /* r4: void *buffer */
		const u8 r5 = bpf2a64[BPF_REG_5]; /* r5: void *(*func)(...) */
		int size;

		emit(A64_MOV(1, r1, r6), ctx);
		emit_a64_mov_i(0, r2, imm, ctx);
		if (BPF_MODE(code) == BPF_IND)
			emit(A64_ADD(0, r2, r2, src), ctx);
		switch (BPF_SIZE(code)) {
		case BPF_W:
			size = 4;
			break;
		case BPF_H:
			size = 2;
			break;
		case BPF_B:
			size = 1;
			break;
		default:
			return -EINVAL;
		}
		emit_a64_mov_i64(r3, size, ctx);
		emit(A64_ADD_I(1, r4, fp, MAX_BPF_STACK), ctx);
		emit_a64_mov_i64(r5, (unsigned long)bpf_load_pointer, ctx);
		emit(A64_PUSH(A64_FP, A64_LR, A64_SP), ctx);
		emit(A64_MOV(1, A64_FP, A64_SP), ctx);
		emit(A64_BLR(r5), ctx);
		emit(A64_MOV(1, r0, A64_R(0)), ctx);
		emit(A64_POP(A64_FP, A64_LR, A64_SP), ctx);

		jmp_offset = epilogue_offset(ctx);
		check_imm19(jmp_offset);
		emit(A64_CBZ(1, r0, jmp_offset), ctx);
		emit(A64_MOV(1, r5, r0), ctx);
		switch (BPF_SIZE(code)) {
		case BPF_W:
			emit(A64_LDR32(r0, r5, A64_ZR), ctx);
#ifndef CONFIG_CPU_BIG_ENDIAN
			emit(A64_REV32(0, r0, r0), ctx);
#endif
			break;
		case BPF_H:
			emit(A64_LDRH(r0, r5, A64_ZR), ctx);
#ifndef CONFIG_CPU_BIG_ENDIAN
			emit(A64_REV16(0, r0, r0), ctx);
#endif
			break;
		case BPF_B:
			emit(A64_LDRB(r0, r5, A64_ZR), ctx);
			break;
		}
		break;
	}
notyet:
		pr_info_once("*** NOT YET: opcode %02x ***\n", code);
		return -EFAULT;

	default:
		pr_err_once("unknown opcode %02x\n", code);
		return -EINVAL;
	}

	return 0;
}

static int build_body(struct jit_ctx *ctx)
{
	const struct bpf_prog *prog = ctx->prog;
	int i;

	for (i = 0; i < prog->len; i++) {
		const struct bpf_insn *insn = &prog->insnsi[i];
		int ret;

		if (ctx->image == NULL)
			ctx->offset[i] = ctx->idx;

		ret = build_insn(insn, ctx);
		if (ret > 0) {
			i++;
			continue;
		}
		if (ret)
			return ret;
	}

	return 0;
}

static inline void bpf_flush_icache(void *start, void *end)
{
	flush_icache_range((unsigned long)start, (unsigned long)end);
}

void bpf_jit_compile(struct bpf_prog *prog)
{
	/* Nothing to do here. We support Internal BPF. */
}

void bpf_int_jit_compile(struct bpf_prog *prog)
{
	struct bpf_binary_header *header;
	struct jit_ctx ctx;
	int image_size;
	u8 *image_ptr;

	if (!bpf_jit_enable)
		return;

	if (!prog || !prog->len)
		return;

	memset(&ctx, 0, sizeof(ctx));
	ctx.prog = prog;

	ctx.offset = kcalloc(prog->len, sizeof(int), GFP_KERNEL);
	if (ctx.offset == NULL)
		return;

	/* 1. Initial fake pass to compute ctx->idx. */

	/* Fake pass to fill in ctx->offset and ctx->tmp_used. */
	if (build_body(&ctx))
		goto out;

	build_prologue(&ctx);

	ctx.epilogue_offset = ctx.idx;
	build_epilogue(&ctx);

	/* Now we know the actual image size. */
	image_size = sizeof(u32) * ctx.idx;
	header = bpf_jit_binary_alloc(image_size, &image_ptr,
				      sizeof(u32), jit_fill_hole);
	if (header == NULL)
		goto out;

	/* 2. Now, the actual pass. */

	ctx.image = (u32 *)image_ptr;
	ctx.idx = 0;

	build_prologue(&ctx);

	if (build_body(&ctx)) {
		bpf_jit_binary_free(header);
		goto out;
	}

	build_epilogue(&ctx);

	/* And we're done. */
	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, image_size, 2, ctx.image);

	bpf_flush_icache(ctx.image, ctx.image + ctx.idx);

	set_memory_ro((unsigned long)header, header->pages);
	prog->bpf_func = (void *)ctx.image;
	prog->jited = true;
out:
	kfree(ctx.offset);
}

void bpf_jit_free(struct bpf_prog *prog)
{
	unsigned long addr = (unsigned long)prog->bpf_func & PAGE_MASK;
	struct bpf_binary_header *header = (void *)addr;

	if (!prog->jited)
		goto free_filter;

	set_memory_rw(addr, header->pages);
	bpf_jit_binary_free(header);

free_filter:
	bpf_prog_unlock_free(prog);
}
