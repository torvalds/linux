/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * BPF JIT compiler for LoongArch
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */
#include <linux/bpf.h>
#include <linux/filter.h>
#include <asm/cacheflush.h>
#include <asm/inst.h>

struct jit_ctx {
	const struct bpf_prog *prog;
	unsigned int idx;
	unsigned int flags;
	unsigned int epilogue_offset;
	u32 *offset;
	union loongarch_instruction *image;
	u32 stack_size;
};

struct jit_data {
	struct bpf_binary_header *header;
	u8 *image;
	struct jit_ctx ctx;
};

#define emit_insn(ctx, func, ...)						\
do {										\
	if (ctx->image != NULL) {						\
		union loongarch_instruction *insn = &ctx->image[ctx->idx];	\
		emit_##func(insn, ##__VA_ARGS__);				\
	}									\
	ctx->idx++;								\
} while (0)

#define is_signed_imm12(val)	signed_imm_check(val, 12)
#define is_signed_imm14(val)	signed_imm_check(val, 14)
#define is_signed_imm16(val)	signed_imm_check(val, 16)
#define is_signed_imm26(val)	signed_imm_check(val, 26)
#define is_signed_imm32(val)	signed_imm_check(val, 32)
#define is_signed_imm52(val)	signed_imm_check(val, 52)
#define is_unsigned_imm12(val)	unsigned_imm_check(val, 12)

static inline int bpf2la_offset(int bpf_insn, int off, const struct jit_ctx *ctx)
{
	/* BPF JMP offset is relative to the next instruction */
	bpf_insn++;
	/*
	 * Whereas LoongArch branch instructions encode the offset
	 * from the branch itself, so we must subtract 1 from the
	 * instruction offset.
	 */
	return (ctx->offset[bpf_insn + off] - (ctx->offset[bpf_insn] - 1));
}

static inline int epilogue_offset(const struct jit_ctx *ctx)
{
	int from = ctx->idx;
	int to = ctx->epilogue_offset;

	return (to - from);
}

/* Zero-extend 32 bits into 64 bits */
static inline void emit_zext_32(struct jit_ctx *ctx, enum loongarch_gpr reg, bool is32)
{
	if (!is32)
		return;

	emit_insn(ctx, lu32id, reg, 0);
}

/* Signed-extend 32 bits into 64 bits */
static inline void emit_sext_32(struct jit_ctx *ctx, enum loongarch_gpr reg, bool is32)
{
	if (!is32)
		return;

	emit_insn(ctx, addiw, reg, reg, 0);
}

static inline void move_addr(struct jit_ctx *ctx, enum loongarch_gpr rd, u64 addr)
{
	u64 imm_11_0, imm_31_12, imm_51_32, imm_63_52;

	/* lu12iw rd, imm_31_12 */
	imm_31_12 = (addr >> 12) & 0xfffff;
	emit_insn(ctx, lu12iw, rd, imm_31_12);

	/* ori rd, rd, imm_11_0 */
	imm_11_0 = addr & 0xfff;
	emit_insn(ctx, ori, rd, rd, imm_11_0);

	/* lu32id rd, imm_51_32 */
	imm_51_32 = (addr >> 32) & 0xfffff;
	emit_insn(ctx, lu32id, rd, imm_51_32);

	/* lu52id rd, rd, imm_63_52 */
	imm_63_52 = (addr >> 52) & 0xfff;
	emit_insn(ctx, lu52id, rd, rd, imm_63_52);
}

static inline void move_imm(struct jit_ctx *ctx, enum loongarch_gpr rd, long imm, bool is32)
{
	long imm_11_0, imm_31_12, imm_51_32, imm_63_52, imm_51_0, imm_51_31;

	/* or rd, $zero, $zero */
	if (imm == 0) {
		emit_insn(ctx, or, rd, LOONGARCH_GPR_ZERO, LOONGARCH_GPR_ZERO);
		return;
	}

	/* addiw rd, $zero, imm_11_0 */
	if (is_signed_imm12(imm)) {
		emit_insn(ctx, addiw, rd, LOONGARCH_GPR_ZERO, imm);
		goto zext;
	}

	/* ori rd, $zero, imm_11_0 */
	if (is_unsigned_imm12(imm)) {
		emit_insn(ctx, ori, rd, LOONGARCH_GPR_ZERO, imm);
		goto zext;
	}

	/* lu52id rd, $zero, imm_63_52 */
	imm_63_52 = (imm >> 52) & 0xfff;
	imm_51_0 = imm & 0xfffffffffffff;
	if (imm_63_52 != 0 && imm_51_0 == 0) {
		emit_insn(ctx, lu52id, rd, LOONGARCH_GPR_ZERO, imm_63_52);
		return;
	}

	/* lu12iw rd, imm_31_12 */
	imm_31_12 = (imm >> 12) & 0xfffff;
	emit_insn(ctx, lu12iw, rd, imm_31_12);

	/* ori rd, rd, imm_11_0 */
	imm_11_0 = imm & 0xfff;
	if (imm_11_0 != 0)
		emit_insn(ctx, ori, rd, rd, imm_11_0);

	if (!is_signed_imm32(imm)) {
		if (imm_51_0 != 0) {
			/*
			 * If bit[51:31] is all 0 or all 1,
			 * it means bit[51:32] is sign extended by lu12iw,
			 * no need to call lu32id to do a new filled operation.
			 */
			imm_51_31 = (imm >> 31) & 0x1fffff;
			if (imm_51_31 != 0 && imm_51_31 != 0x1fffff) {
				/* lu32id rd, imm_51_32 */
				imm_51_32 = (imm >> 32) & 0xfffff;
				emit_insn(ctx, lu32id, rd, imm_51_32);
			}
		}

		/* lu52id rd, rd, imm_63_52 */
		if (!is_signed_imm52(imm))
			emit_insn(ctx, lu52id, rd, rd, imm_63_52);
	}

zext:
	emit_zext_32(ctx, rd, is32);
}

static inline void move_reg(struct jit_ctx *ctx, enum loongarch_gpr rd,
			    enum loongarch_gpr rj)
{
	emit_insn(ctx, or, rd, rj, LOONGARCH_GPR_ZERO);
}

static inline int invert_jmp_cond(u8 cond)
{
	switch (cond) {
	case BPF_JEQ:
		return BPF_JNE;
	case BPF_JNE:
	case BPF_JSET:
		return BPF_JEQ;
	case BPF_JGT:
		return BPF_JLE;
	case BPF_JGE:
		return BPF_JLT;
	case BPF_JLT:
		return BPF_JGE;
	case BPF_JLE:
		return BPF_JGT;
	case BPF_JSGT:
		return BPF_JSLE;
	case BPF_JSGE:
		return BPF_JSLT;
	case BPF_JSLT:
		return BPF_JSGE;
	case BPF_JSLE:
		return BPF_JSGT;
	}
	return -1;
}

static inline void cond_jmp_offset(struct jit_ctx *ctx, u8 cond, enum loongarch_gpr rj,
				   enum loongarch_gpr rd, int jmp_offset)
{
	switch (cond) {
	case BPF_JEQ:
		/* PC += jmp_offset if rj == rd */
		emit_insn(ctx, beq, rj, rd, jmp_offset);
		return;
	case BPF_JNE:
	case BPF_JSET:
		/* PC += jmp_offset if rj != rd */
		emit_insn(ctx, bne, rj, rd, jmp_offset);
		return;
	case BPF_JGT:
		/* PC += jmp_offset if rj > rd (unsigned) */
		emit_insn(ctx, bltu, rd, rj, jmp_offset);
		return;
	case BPF_JLT:
		/* PC += jmp_offset if rj < rd (unsigned) */
		emit_insn(ctx, bltu, rj, rd, jmp_offset);
		return;
	case BPF_JGE:
		/* PC += jmp_offset if rj >= rd (unsigned) */
		emit_insn(ctx, bgeu, rj, rd, jmp_offset);
		return;
	case BPF_JLE:
		/* PC += jmp_offset if rj <= rd (unsigned) */
		emit_insn(ctx, bgeu, rd, rj, jmp_offset);
		return;
	case BPF_JSGT:
		/* PC += jmp_offset if rj > rd (signed) */
		emit_insn(ctx, blt, rd, rj, jmp_offset);
		return;
	case BPF_JSLT:
		/* PC += jmp_offset if rj < rd (signed) */
		emit_insn(ctx, blt, rj, rd, jmp_offset);
		return;
	case BPF_JSGE:
		/* PC += jmp_offset if rj >= rd (signed) */
		emit_insn(ctx, bge, rj, rd, jmp_offset);
		return;
	case BPF_JSLE:
		/* PC += jmp_offset if rj <= rd (signed) */
		emit_insn(ctx, bge, rd, rj, jmp_offset);
		return;
	}
}

static inline void cond_jmp_offs26(struct jit_ctx *ctx, u8 cond, enum loongarch_gpr rj,
				   enum loongarch_gpr rd, int jmp_offset)
{
	cond = invert_jmp_cond(cond);
	cond_jmp_offset(ctx, cond, rj, rd, 2);
	emit_insn(ctx, b, jmp_offset);
}

static inline void uncond_jmp_offs26(struct jit_ctx *ctx, int jmp_offset)
{
	emit_insn(ctx, b, jmp_offset);
}

static inline int emit_cond_jmp(struct jit_ctx *ctx, u8 cond, enum loongarch_gpr rj,
				enum loongarch_gpr rd, int jmp_offset)
{
	/*
	 * A large PC-relative jump offset may overflow the immediate field of
	 * the native conditional branch instruction, triggering a conversion
	 * to use an absolute jump instead, this jump sequence is particularly
	 * nasty. For now, use cond_jmp_offs26() directly to keep it simple.
	 * In the future, maybe we can add support for far branching, the branch
	 * relaxation requires more than two passes to converge, the code seems
	 * too complex to understand, not quite sure whether it is necessary and
	 * worth the extra pain. Anyway, just leave it as it is to enhance code
	 * readability now.
	 */
	if (is_signed_imm26(jmp_offset)) {
		cond_jmp_offs26(ctx, cond, rj, rd, jmp_offset);
		return 0;
	}

	return -EINVAL;
}

static inline int emit_uncond_jmp(struct jit_ctx *ctx, int jmp_offset)
{
	if (is_signed_imm26(jmp_offset)) {
		uncond_jmp_offs26(ctx, jmp_offset);
		return 0;
	}

	return -EINVAL;
}

static inline int emit_tailcall_jmp(struct jit_ctx *ctx, u8 cond, enum loongarch_gpr rj,
				    enum loongarch_gpr rd, int jmp_offset)
{
	if (is_signed_imm16(jmp_offset)) {
		cond_jmp_offset(ctx, cond, rj, rd, jmp_offset);
		return 0;
	}

	return -EINVAL;
}
