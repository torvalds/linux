/*
 * Just-In-Time compiler for BPF filters on MIPS
 *
 * Copyright (c) 2014 Imagination Technologies Ltd.
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 */

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/filter.h>
#include <linux/if_vlan.h>
#include <linux/moduleloader.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <asm/asm.h>
#include <asm/bitops.h>
#include <asm/cacheflush.h>
#include <asm/cpu-features.h>
#include <asm/uasm.h>

#include "bpf_jit.h"

/* ABI
 * r_skb_hl	SKB header length
 * r_data	SKB data pointer
 * r_off	Offset
 * r_A		BPF register A
 * r_X		BPF register X
 * r_skb	*skb
 * r_M		*scratch memory
 * r_skb_len	SKB length
 *
 * On entry (*bpf_func)(*skb, *filter)
 * a0 = MIPS_R_A0 = skb;
 * a1 = MIPS_R_A1 = filter;
 *
 * Stack
 * ...
 * M[15]
 * M[14]
 * M[13]
 * ...
 * M[0] <-- r_M
 * saved reg k-1
 * saved reg k-2
 * ...
 * saved reg 0 <-- r_sp
 * <no argument area>
 *
 *                     Packet layout
 *
 * <--------------------- len ------------------------>
 * <--skb-len(r_skb_hl)-->< ----- skb->data_len ------>
 * ----------------------------------------------------
 * |                  skb->data                       |
 * ----------------------------------------------------
 */

#define ptr typeof(unsigned long)

#define SCRATCH_OFF(k)		(4 * (k))

/* JIT flags */
#define SEEN_CALL		(1 << BPF_MEMWORDS)
#define SEEN_SREG_SFT		(BPF_MEMWORDS + 1)
#define SEEN_SREG_BASE		(1 << SEEN_SREG_SFT)
#define SEEN_SREG(x)		(SEEN_SREG_BASE << (x))
#define SEEN_OFF		SEEN_SREG(2)
#define SEEN_A			SEEN_SREG(3)
#define SEEN_X			SEEN_SREG(4)
#define SEEN_SKB		SEEN_SREG(5)
#define SEEN_MEM		SEEN_SREG(6)
/* SEEN_SK_DATA also implies skb_hl an skb_len */
#define SEEN_SKB_DATA		(SEEN_SREG(7) | SEEN_SREG(1) | SEEN_SREG(0))

/* Arguments used by JIT */
#define ARGS_USED_BY_JIT	2 /* only applicable to 64-bit */

#define SBIT(x)			(1 << (x)) /* Signed version of BIT() */

/**
 * struct jit_ctx - JIT context
 * @skf:		The sk_filter
 * @prologue_bytes:	Number of bytes for prologue
 * @idx:		Instruction index
 * @flags:		JIT flags
 * @offsets:		Instruction offsets
 * @target:		Memory location for the compiled filter
 */
struct jit_ctx {
	const struct bpf_prog *skf;
	unsigned int prologue_bytes;
	u32 idx;
	u32 flags;
	u32 *offsets;
	u32 *target;
};


static inline int optimize_div(u32 *k)
{
	/* power of 2 divides can be implemented with right shift */
	if (!(*k & (*k-1))) {
		*k = ilog2(*k);
		return 1;
	}

	return 0;
}

static inline void emit_jit_reg_move(ptr dst, ptr src, struct jit_ctx *ctx);

/* Simply emit the instruction if the JIT memory space has been allocated */
#define emit_instr(ctx, func, ...)			\
do {							\
	if ((ctx)->target != NULL) {			\
		u32 *p = &(ctx)->target[ctx->idx];	\
		uasm_i_##func(&p, ##__VA_ARGS__);	\
	}						\
	(ctx)->idx++;					\
} while (0)

/*
 * Similar to emit_instr but it must be used when we need to emit
 * 32-bit or 64-bit instructions
 */
#define emit_long_instr(ctx, func, ...)			\
do {							\
	if ((ctx)->target != NULL) {			\
		u32 *p = &(ctx)->target[ctx->idx];	\
		UASM_i_##func(&p, ##__VA_ARGS__);	\
	}						\
	(ctx)->idx++;					\
} while (0)

/* Determine if immediate is within the 16-bit signed range */
static inline bool is_range16(s32 imm)
{
	return !(imm >= SBIT(15) || imm < -SBIT(15));
}

static inline void emit_addu(unsigned int dst, unsigned int src1,
			     unsigned int src2, struct jit_ctx *ctx)
{
	emit_instr(ctx, addu, dst, src1, src2);
}

static inline void emit_nop(struct jit_ctx *ctx)
{
	emit_instr(ctx, nop);
}

/* Load a u32 immediate to a register */
static inline void emit_load_imm(unsigned int dst, u32 imm, struct jit_ctx *ctx)
{
	if (ctx->target != NULL) {
		/* addiu can only handle s16 */
		if (!is_range16(imm)) {
			u32 *p = &ctx->target[ctx->idx];
			uasm_i_lui(&p, r_tmp_imm, (s32)imm >> 16);
			p = &ctx->target[ctx->idx + 1];
			uasm_i_ori(&p, dst, r_tmp_imm, imm & 0xffff);
		} else {
			u32 *p = &ctx->target[ctx->idx];
			uasm_i_addiu(&p, dst, r_zero, imm);
		}
	}
	ctx->idx++;

	if (!is_range16(imm))
		ctx->idx++;
}

static inline void emit_or(unsigned int dst, unsigned int src1,
			   unsigned int src2, struct jit_ctx *ctx)
{
	emit_instr(ctx, or, dst, src1, src2);
}

static inline void emit_ori(unsigned int dst, unsigned src, u32 imm,
			    struct jit_ctx *ctx)
{
	if (imm >= BIT(16)) {
		emit_load_imm(r_tmp, imm, ctx);
		emit_or(dst, src, r_tmp, ctx);
	} else {
		emit_instr(ctx, ori, dst, src, imm);
	}
}

static inline void emit_daddiu(unsigned int dst, unsigned int src,
			       int imm, struct jit_ctx *ctx)
{
	/*
	 * Only used for stack, so the imm is relatively small
	 * and it fits in 15-bits
	 */
	emit_instr(ctx, daddiu, dst, src, imm);
}

static inline void emit_addiu(unsigned int dst, unsigned int src,
			      u32 imm, struct jit_ctx *ctx)
{
	if (!is_range16(imm)) {
		emit_load_imm(r_tmp, imm, ctx);
		emit_addu(dst, r_tmp, src, ctx);
	} else {
		emit_instr(ctx, addiu, dst, src, imm);
	}
}

static inline void emit_and(unsigned int dst, unsigned int src1,
			    unsigned int src2, struct jit_ctx *ctx)
{
	emit_instr(ctx, and, dst, src1, src2);
}

static inline void emit_andi(unsigned int dst, unsigned int src,
			     u32 imm, struct jit_ctx *ctx)
{
	/* If imm does not fit in u16 then load it to register */
	if (imm >= BIT(16)) {
		emit_load_imm(r_tmp, imm, ctx);
		emit_and(dst, src, r_tmp, ctx);
	} else {
		emit_instr(ctx, andi, dst, src, imm);
	}
}

static inline void emit_xor(unsigned int dst, unsigned int src1,
			    unsigned int src2, struct jit_ctx *ctx)
{
	emit_instr(ctx, xor, dst, src1, src2);
}

static inline void emit_xori(ptr dst, ptr src, u32 imm, struct jit_ctx *ctx)
{
	/* If imm does not fit in u16 then load it to register */
	if (imm >= BIT(16)) {
		emit_load_imm(r_tmp, imm, ctx);
		emit_xor(dst, src, r_tmp, ctx);
	} else {
		emit_instr(ctx, xori, dst, src, imm);
	}
}

static inline void emit_stack_offset(int offset, struct jit_ctx *ctx)
{
	emit_long_instr(ctx, ADDIU, r_sp, r_sp, offset);
}

static inline void emit_subu(unsigned int dst, unsigned int src1,
			     unsigned int src2, struct jit_ctx *ctx)
{
	emit_instr(ctx, subu, dst, src1, src2);
}

static inline void emit_neg(unsigned int reg, struct jit_ctx *ctx)
{
	emit_subu(reg, r_zero, reg, ctx);
}

static inline void emit_sllv(unsigned int dst, unsigned int src,
			     unsigned int sa, struct jit_ctx *ctx)
{
	emit_instr(ctx, sllv, dst, src, sa);
}

static inline void emit_sll(unsigned int dst, unsigned int src,
			    unsigned int sa, struct jit_ctx *ctx)
{
	/* sa is 5-bits long */
	if (sa >= BIT(5))
		/* Shifting >= 32 results in zero */
		emit_jit_reg_move(dst, r_zero, ctx);
	else
		emit_instr(ctx, sll, dst, src, sa);
}

static inline void emit_srlv(unsigned int dst, unsigned int src,
			     unsigned int sa, struct jit_ctx *ctx)
{
	emit_instr(ctx, srlv, dst, src, sa);
}

static inline void emit_srl(unsigned int dst, unsigned int src,
			    unsigned int sa, struct jit_ctx *ctx)
{
	/* sa is 5-bits long */
	if (sa >= BIT(5))
		/* Shifting >= 32 results in zero */
		emit_jit_reg_move(dst, r_zero, ctx);
	else
		emit_instr(ctx, srl, dst, src, sa);
}

static inline void emit_slt(unsigned int dst, unsigned int src1,
			    unsigned int src2, struct jit_ctx *ctx)
{
	emit_instr(ctx, slt, dst, src1, src2);
}

static inline void emit_sltu(unsigned int dst, unsigned int src1,
			     unsigned int src2, struct jit_ctx *ctx)
{
	emit_instr(ctx, sltu, dst, src1, src2);
}

static inline void emit_sltiu(unsigned dst, unsigned int src,
			      unsigned int imm, struct jit_ctx *ctx)
{
	/* 16 bit immediate */
	if (!is_range16((s32)imm)) {
		emit_load_imm(r_tmp, imm, ctx);
		emit_sltu(dst, src, r_tmp, ctx);
	} else {
		emit_instr(ctx, sltiu, dst, src, imm);
	}

}

/* Store register on the stack */
static inline void emit_store_stack_reg(ptr reg, ptr base,
					unsigned int offset,
					struct jit_ctx *ctx)
{
	emit_long_instr(ctx, SW, reg, offset, base);
}

static inline void emit_store(ptr reg, ptr base, unsigned int offset,
			      struct jit_ctx *ctx)
{
	emit_instr(ctx, sw, reg, offset, base);
}

static inline void emit_load_stack_reg(ptr reg, ptr base,
				       unsigned int offset,
				       struct jit_ctx *ctx)
{
	emit_long_instr(ctx, LW, reg, offset, base);
}

static inline void emit_load(unsigned int reg, unsigned int base,
			     unsigned int offset, struct jit_ctx *ctx)
{
	emit_instr(ctx, lw, reg, offset, base);
}

static inline void emit_load_byte(unsigned int reg, unsigned int base,
				  unsigned int offset, struct jit_ctx *ctx)
{
	emit_instr(ctx, lb, reg, offset, base);
}

static inline void emit_half_load(unsigned int reg, unsigned int base,
				  unsigned int offset, struct jit_ctx *ctx)
{
	emit_instr(ctx, lh, reg, offset, base);
}

static inline void emit_half_load_unsigned(unsigned int reg, unsigned int base,
					   unsigned int offset, struct jit_ctx *ctx)
{
	emit_instr(ctx, lhu, reg, offset, base);
}

static inline void emit_mul(unsigned int dst, unsigned int src1,
			    unsigned int src2, struct jit_ctx *ctx)
{
	emit_instr(ctx, mul, dst, src1, src2);
}

static inline void emit_div(unsigned int dst, unsigned int src,
			    struct jit_ctx *ctx)
{
	if (ctx->target != NULL) {
		u32 *p = &ctx->target[ctx->idx];
		uasm_i_divu(&p, dst, src);
		p = &ctx->target[ctx->idx + 1];
		uasm_i_mflo(&p, dst);
	}
	ctx->idx += 2; /* 2 insts */
}

static inline void emit_mod(unsigned int dst, unsigned int src,
			    struct jit_ctx *ctx)
{
	if (ctx->target != NULL) {
		u32 *p = &ctx->target[ctx->idx];
		uasm_i_divu(&p, dst, src);
		p = &ctx->target[ctx->idx + 1];
		uasm_i_mfhi(&p, dst);
	}
	ctx->idx += 2; /* 2 insts */
}

static inline void emit_dsll(unsigned int dst, unsigned int src,
			     unsigned int sa, struct jit_ctx *ctx)
{
	emit_instr(ctx, dsll, dst, src, sa);
}

static inline void emit_dsrl32(unsigned int dst, unsigned int src,
			       unsigned int sa, struct jit_ctx *ctx)
{
	emit_instr(ctx, dsrl32, dst, src, sa);
}

static inline void emit_wsbh(unsigned int dst, unsigned int src,
			     struct jit_ctx *ctx)
{
	emit_instr(ctx, wsbh, dst, src);
}

/* load pointer to register */
static inline void emit_load_ptr(unsigned int dst, unsigned int src,
				     int imm, struct jit_ctx *ctx)
{
	/* src contains the base addr of the 32/64-pointer */
	emit_long_instr(ctx, LW, dst, imm, src);
}

/* load a function pointer to register */
static inline void emit_load_func(unsigned int reg, ptr imm,
				  struct jit_ctx *ctx)
{
	if (IS_ENABLED(CONFIG_64BIT)) {
		/* At this point imm is always 64-bit */
		emit_load_imm(r_tmp, (u64)imm >> 32, ctx);
		emit_dsll(r_tmp_imm, r_tmp, 16, ctx); /* left shift by 16 */
		emit_ori(r_tmp, r_tmp_imm, (imm >> 16) & 0xffff, ctx);
		emit_dsll(r_tmp_imm, r_tmp, 16, ctx); /* left shift by 16 */
		emit_ori(reg, r_tmp_imm, imm & 0xffff, ctx);
	} else {
		emit_load_imm(reg, imm, ctx);
	}
}

/* Move to real MIPS register */
static inline void emit_reg_move(ptr dst, ptr src, struct jit_ctx *ctx)
{
	emit_long_instr(ctx, ADDU, dst, src, r_zero);
}

/* Move to JIT (32-bit) register */
static inline void emit_jit_reg_move(ptr dst, ptr src, struct jit_ctx *ctx)
{
	emit_addu(dst, src, r_zero, ctx);
}

/* Compute the immediate value for PC-relative branches. */
static inline u32 b_imm(unsigned int tgt, struct jit_ctx *ctx)
{
	if (ctx->target == NULL)
		return 0;

	/*
	 * We want a pc-relative branch. We only do forward branches
	 * so tgt is always after pc. tgt is the instruction offset
	 * we want to jump to.

	 * Branch on MIPS:
	 * I: target_offset <- sign_extend(offset)
	 * I+1: PC += target_offset (delay slot)
	 *
	 * ctx->idx currently points to the branch instruction
	 * but the offset is added to the delay slot so we need
	 * to subtract 4.
	 */
	return ctx->offsets[tgt] -
		(ctx->idx * 4 - ctx->prologue_bytes) - 4;
}

static inline void emit_bcond(int cond, unsigned int reg1, unsigned int reg2,
			     unsigned int imm, struct jit_ctx *ctx)
{
	if (ctx->target != NULL) {
		u32 *p = &ctx->target[ctx->idx];

		switch (cond) {
		case MIPS_COND_EQ:
			uasm_i_beq(&p, reg1, reg2, imm);
			break;
		case MIPS_COND_NE:
			uasm_i_bne(&p, reg1, reg2, imm);
			break;
		case MIPS_COND_ALL:
			uasm_i_b(&p, imm);
			break;
		default:
			pr_warn("%s: Unhandled branch conditional: %d\n",
				__func__, cond);
		}
	}
	ctx->idx++;
}

static inline void emit_b(unsigned int imm, struct jit_ctx *ctx)
{
	emit_bcond(MIPS_COND_ALL, r_zero, r_zero, imm, ctx);
}

static inline void emit_jalr(unsigned int link, unsigned int reg,
			     struct jit_ctx *ctx)
{
	emit_instr(ctx, jalr, link, reg);
}

static inline void emit_jr(unsigned int reg, struct jit_ctx *ctx)
{
	emit_instr(ctx, jr, reg);
}

static inline u16 align_sp(unsigned int num)
{
	/* Double word alignment for 32-bit, quadword for 64-bit */
	unsigned int align = IS_ENABLED(CONFIG_64BIT) ? 16 : 8;
	num = (num + (align - 1)) & -align;
	return num;
}

static void save_bpf_jit_regs(struct jit_ctx *ctx, unsigned offset)
{
	int i = 0, real_off = 0;
	u32 sflags, tmp_flags;

	/* Adjust the stack pointer */
	if (offset)
		emit_stack_offset(-align_sp(offset), ctx);

	tmp_flags = sflags = ctx->flags >> SEEN_SREG_SFT;
	/* sflags is essentially a bitmap */
	while (tmp_flags) {
		if ((sflags >> i) & 0x1) {
			emit_store_stack_reg(MIPS_R_S0 + i, r_sp, real_off,
					     ctx);
			real_off += SZREG;
		}
		i++;
		tmp_flags >>= 1;
	}

	/* save return address */
	if (ctx->flags & SEEN_CALL) {
		emit_store_stack_reg(r_ra, r_sp, real_off, ctx);
		real_off += SZREG;
	}

	/* Setup r_M leaving the alignment gap if necessary */
	if (ctx->flags & SEEN_MEM) {
		if (real_off % (SZREG * 2))
			real_off += SZREG;
		emit_long_instr(ctx, ADDIU, r_M, r_sp, real_off);
	}
}

static void restore_bpf_jit_regs(struct jit_ctx *ctx,
				 unsigned int offset)
{
	int i, real_off = 0;
	u32 sflags, tmp_flags;

	tmp_flags = sflags = ctx->flags >> SEEN_SREG_SFT;
	/* sflags is a bitmap */
	i = 0;
	while (tmp_flags) {
		if ((sflags >> i) & 0x1) {
			emit_load_stack_reg(MIPS_R_S0 + i, r_sp, real_off,
					    ctx);
			real_off += SZREG;
		}
		i++;
		tmp_flags >>= 1;
	}

	/* restore return address */
	if (ctx->flags & SEEN_CALL)
		emit_load_stack_reg(r_ra, r_sp, real_off, ctx);

	/* Restore the sp and discard the scrach memory */
	if (offset)
		emit_stack_offset(align_sp(offset), ctx);
}

static unsigned int get_stack_depth(struct jit_ctx *ctx)
{
	int sp_off = 0;


	/* How may s* regs do we need to preserved? */
	sp_off += hweight32(ctx->flags >> SEEN_SREG_SFT) * SZREG;

	if (ctx->flags & SEEN_MEM)
		sp_off += 4 * BPF_MEMWORDS; /* BPF_MEMWORDS are 32-bit */

	if (ctx->flags & SEEN_CALL)
		sp_off += SZREG; /* Space for our ra register */

	return sp_off;
}

static void build_prologue(struct jit_ctx *ctx)
{
	int sp_off;

	/* Calculate the total offset for the stack pointer */
	sp_off = get_stack_depth(ctx);
	save_bpf_jit_regs(ctx, sp_off);

	if (ctx->flags & SEEN_SKB)
		emit_reg_move(r_skb, MIPS_R_A0, ctx);

	if (ctx->flags & SEEN_SKB_DATA) {
		/* Load packet length */
		emit_load(r_skb_len, r_skb, offsetof(struct sk_buff, len),
			  ctx);
		emit_load(r_tmp, r_skb, offsetof(struct sk_buff, data_len),
			  ctx);
		/* Load the data pointer */
		emit_load_ptr(r_skb_data, r_skb,
			      offsetof(struct sk_buff, data), ctx);
		/* Load the header length */
		emit_subu(r_skb_hl, r_skb_len, r_tmp, ctx);
	}

	if (ctx->flags & SEEN_X)
		emit_jit_reg_move(r_X, r_zero, ctx);

	/*
	 * Do not leak kernel data to userspace, we only need to clear
	 * r_A if it is ever used.  In fact if it is never used, we
	 * will not save/restore it, so clearing it in this case would
	 * corrupt the state of the caller.
	 */
	if (bpf_needs_clear_a(&ctx->skf->insns[0]) &&
	    (ctx->flags & SEEN_A))
		emit_jit_reg_move(r_A, r_zero, ctx);
}

static void build_epilogue(struct jit_ctx *ctx)
{
	unsigned int sp_off;

	/* Calculate the total offset for the stack pointer */

	sp_off = get_stack_depth(ctx);
	restore_bpf_jit_regs(ctx, sp_off);

	/* Return */
	emit_jr(r_ra, ctx);
	emit_nop(ctx);
}

#define CHOOSE_LOAD_FUNC(K, func) \
	((int)K < 0 ? ((int)K >= SKF_LL_OFF ? func##_negative : func) : \
	 func##_positive)

static bool is_bad_offset(int b_off)
{
	return b_off > 0x1ffff || b_off < -0x20000;
}

static int build_body(struct jit_ctx *ctx)
{
	const struct bpf_prog *prog = ctx->skf;
	const struct sock_filter *inst;
	unsigned int i, off, condt;
	u32 k, b_off __maybe_unused;
	u8 (*sk_load_func)(unsigned long *skb, int offset);

	for (i = 0; i < prog->len; i++) {
		u16 code;

		inst = &(prog->insns[i]);
		pr_debug("%s: code->0x%02x, jt->0x%x, jf->0x%x, k->0x%x\n",
			 __func__, inst->code, inst->jt, inst->jf, inst->k);
		k = inst->k;
		code = bpf_anc_helper(inst);

		if (ctx->target == NULL)
			ctx->offsets[i] = ctx->idx * 4;

		switch (code) {
		case BPF_LD | BPF_IMM:
			/* A <- k ==> li r_A, k */
			ctx->flags |= SEEN_A;
			emit_load_imm(r_A, k, ctx);
			break;
		case BPF_LD | BPF_W | BPF_LEN:
			BUILD_BUG_ON(sizeof_field(struct sk_buff, len) != 4);
			/* A <- len ==> lw r_A, offset(skb) */
			ctx->flags |= SEEN_SKB | SEEN_A;
			off = offsetof(struct sk_buff, len);
			emit_load(r_A, r_skb, off, ctx);
			break;
		case BPF_LD | BPF_MEM:
			/* A <- M[k] ==> lw r_A, offset(M) */
			ctx->flags |= SEEN_MEM | SEEN_A;
			emit_load(r_A, r_M, SCRATCH_OFF(k), ctx);
			break;
		case BPF_LD | BPF_W | BPF_ABS:
			/* A <- P[k:4] */
			sk_load_func = CHOOSE_LOAD_FUNC(k, sk_load_word);
			goto load;
		case BPF_LD | BPF_H | BPF_ABS:
			/* A <- P[k:2] */
			sk_load_func = CHOOSE_LOAD_FUNC(k, sk_load_half);
			goto load;
		case BPF_LD | BPF_B | BPF_ABS:
			/* A <- P[k:1] */
			sk_load_func = CHOOSE_LOAD_FUNC(k, sk_load_byte);
load:
			emit_load_imm(r_off, k, ctx);
load_common:
			ctx->flags |= SEEN_CALL | SEEN_OFF |
				SEEN_SKB | SEEN_A | SEEN_SKB_DATA;

			emit_load_func(r_s0, (ptr)sk_load_func, ctx);
			emit_reg_move(MIPS_R_A0, r_skb, ctx);
			emit_jalr(MIPS_R_RA, r_s0, ctx);
			/* Load second argument to delay slot */
			emit_reg_move(MIPS_R_A1, r_off, ctx);
			/* Check the error value */
			emit_bcond(MIPS_COND_EQ, r_ret, 0, b_imm(i + 1, ctx),
				   ctx);
			/* Load return register on DS for failures */
			emit_reg_move(r_ret, r_zero, ctx);
			/* Return with error */
			b_off = b_imm(prog->len, ctx);
			if (is_bad_offset(b_off))
				return -E2BIG;
			emit_b(b_off, ctx);
			emit_nop(ctx);
			break;
		case BPF_LD | BPF_W | BPF_IND:
			/* A <- P[X + k:4] */
			sk_load_func = sk_load_word;
			goto load_ind;
		case BPF_LD | BPF_H | BPF_IND:
			/* A <- P[X + k:2] */
			sk_load_func = sk_load_half;
			goto load_ind;
		case BPF_LD | BPF_B | BPF_IND:
			/* A <- P[X + k:1] */
			sk_load_func = sk_load_byte;
load_ind:
			ctx->flags |= SEEN_OFF | SEEN_X;
			emit_addiu(r_off, r_X, k, ctx);
			goto load_common;
		case BPF_LDX | BPF_IMM:
			/* X <- k */
			ctx->flags |= SEEN_X;
			emit_load_imm(r_X, k, ctx);
			break;
		case BPF_LDX | BPF_MEM:
			/* X <- M[k] */
			ctx->flags |= SEEN_X | SEEN_MEM;
			emit_load(r_X, r_M, SCRATCH_OFF(k), ctx);
			break;
		case BPF_LDX | BPF_W | BPF_LEN:
			/* X <- len */
			ctx->flags |= SEEN_X | SEEN_SKB;
			off = offsetof(struct sk_buff, len);
			emit_load(r_X, r_skb, off, ctx);
			break;
		case BPF_LDX | BPF_B | BPF_MSH:
			/* X <- 4 * (P[k:1] & 0xf) */
			ctx->flags |= SEEN_X | SEEN_CALL | SEEN_SKB;
			/* Load offset to a1 */
			emit_load_func(r_s0, (ptr)sk_load_byte, ctx);
			/*
			 * This may emit two instructions so it may not fit
			 * in the delay slot. So use a0 in the delay slot.
			 */
			emit_load_imm(MIPS_R_A1, k, ctx);
			emit_jalr(MIPS_R_RA, r_s0, ctx);
			emit_reg_move(MIPS_R_A0, r_skb, ctx); /* delay slot */
			/* Check the error value */
			b_off = b_imm(prog->len, ctx);
			if (is_bad_offset(b_off))
				return -E2BIG;
			emit_bcond(MIPS_COND_NE, r_ret, 0, b_off, ctx);
			emit_reg_move(r_ret, r_zero, ctx);
			/* We are good */
			/* X <- P[1:K] & 0xf */
			emit_andi(r_X, r_A, 0xf, ctx);
			/* X << 2 */
			emit_b(b_imm(i + 1, ctx), ctx);
			emit_sll(r_X, r_X, 2, ctx); /* delay slot */
			break;
		case BPF_ST:
			/* M[k] <- A */
			ctx->flags |= SEEN_MEM | SEEN_A;
			emit_store(r_A, r_M, SCRATCH_OFF(k), ctx);
			break;
		case BPF_STX:
			/* M[k] <- X */
			ctx->flags |= SEEN_MEM | SEEN_X;
			emit_store(r_X, r_M, SCRATCH_OFF(k), ctx);
			break;
		case BPF_ALU | BPF_ADD | BPF_K:
			/* A += K */
			ctx->flags |= SEEN_A;
			emit_addiu(r_A, r_A, k, ctx);
			break;
		case BPF_ALU | BPF_ADD | BPF_X:
			/* A += X */
			ctx->flags |= SEEN_A | SEEN_X;
			emit_addu(r_A, r_A, r_X, ctx);
			break;
		case BPF_ALU | BPF_SUB | BPF_K:
			/* A -= K */
			ctx->flags |= SEEN_A;
			emit_addiu(r_A, r_A, -k, ctx);
			break;
		case BPF_ALU | BPF_SUB | BPF_X:
			/* A -= X */
			ctx->flags |= SEEN_A | SEEN_X;
			emit_subu(r_A, r_A, r_X, ctx);
			break;
		case BPF_ALU | BPF_MUL | BPF_K:
			/* A *= K */
			/* Load K to scratch register before MUL */
			ctx->flags |= SEEN_A;
			emit_load_imm(r_s0, k, ctx);
			emit_mul(r_A, r_A, r_s0, ctx);
			break;
		case BPF_ALU | BPF_MUL | BPF_X:
			/* A *= X */
			ctx->flags |= SEEN_A | SEEN_X;
			emit_mul(r_A, r_A, r_X, ctx);
			break;
		case BPF_ALU | BPF_DIV | BPF_K:
			/* A /= k */
			if (k == 1)
				break;
			if (optimize_div(&k)) {
				ctx->flags |= SEEN_A;
				emit_srl(r_A, r_A, k, ctx);
				break;
			}
			ctx->flags |= SEEN_A;
			emit_load_imm(r_s0, k, ctx);
			emit_div(r_A, r_s0, ctx);
			break;
		case BPF_ALU | BPF_MOD | BPF_K:
			/* A %= k */
			if (k == 1) {
				ctx->flags |= SEEN_A;
				emit_jit_reg_move(r_A, r_zero, ctx);
			} else {
				ctx->flags |= SEEN_A;
				emit_load_imm(r_s0, k, ctx);
				emit_mod(r_A, r_s0, ctx);
			}
			break;
		case BPF_ALU | BPF_DIV | BPF_X:
			/* A /= X */
			ctx->flags |= SEEN_X | SEEN_A;
			/* Check if r_X is zero */
			b_off = b_imm(prog->len, ctx);
			if (is_bad_offset(b_off))
				return -E2BIG;
			emit_bcond(MIPS_COND_EQ, r_X, r_zero, b_off, ctx);
			emit_load_imm(r_ret, 0, ctx); /* delay slot */
			emit_div(r_A, r_X, ctx);
			break;
		case BPF_ALU | BPF_MOD | BPF_X:
			/* A %= X */
			ctx->flags |= SEEN_X | SEEN_A;
			/* Check if r_X is zero */
			b_off = b_imm(prog->len, ctx);
			if (is_bad_offset(b_off))
				return -E2BIG;
			emit_bcond(MIPS_COND_EQ, r_X, r_zero, b_off, ctx);
			emit_load_imm(r_ret, 0, ctx); /* delay slot */
			emit_mod(r_A, r_X, ctx);
			break;
		case BPF_ALU | BPF_OR | BPF_K:
			/* A |= K */
			ctx->flags |= SEEN_A;
			emit_ori(r_A, r_A, k, ctx);
			break;
		case BPF_ALU | BPF_OR | BPF_X:
			/* A |= X */
			ctx->flags |= SEEN_A;
			emit_ori(r_A, r_A, r_X, ctx);
			break;
		case BPF_ALU | BPF_XOR | BPF_K:
			/* A ^= k */
			ctx->flags |= SEEN_A;
			emit_xori(r_A, r_A, k, ctx);
			break;
		case BPF_ANC | SKF_AD_ALU_XOR_X:
		case BPF_ALU | BPF_XOR | BPF_X:
			/* A ^= X */
			ctx->flags |= SEEN_A;
			emit_xor(r_A, r_A, r_X, ctx);
			break;
		case BPF_ALU | BPF_AND | BPF_K:
			/* A &= K */
			ctx->flags |= SEEN_A;
			emit_andi(r_A, r_A, k, ctx);
			break;
		case BPF_ALU | BPF_AND | BPF_X:
			/* A &= X */
			ctx->flags |= SEEN_A | SEEN_X;
			emit_and(r_A, r_A, r_X, ctx);
			break;
		case BPF_ALU | BPF_LSH | BPF_K:
			/* A <<= K */
			ctx->flags |= SEEN_A;
			emit_sll(r_A, r_A, k, ctx);
			break;
		case BPF_ALU | BPF_LSH | BPF_X:
			/* A <<= X */
			ctx->flags |= SEEN_A | SEEN_X;
			emit_sllv(r_A, r_A, r_X, ctx);
			break;
		case BPF_ALU | BPF_RSH | BPF_K:
			/* A >>= K */
			ctx->flags |= SEEN_A;
			emit_srl(r_A, r_A, k, ctx);
			break;
		case BPF_ALU | BPF_RSH | BPF_X:
			ctx->flags |= SEEN_A | SEEN_X;
			emit_srlv(r_A, r_A, r_X, ctx);
			break;
		case BPF_ALU | BPF_NEG:
			/* A = -A */
			ctx->flags |= SEEN_A;
			emit_neg(r_A, ctx);
			break;
		case BPF_JMP | BPF_JA:
			/* pc += K */
			b_off = b_imm(i + k + 1, ctx);
			if (is_bad_offset(b_off))
				return -E2BIG;
			emit_b(b_off, ctx);
			emit_nop(ctx);
			break;
		case BPF_JMP | BPF_JEQ | BPF_K:
			/* pc += ( A == K ) ? pc->jt : pc->jf */
			condt = MIPS_COND_EQ | MIPS_COND_K;
			goto jmp_cmp;
		case BPF_JMP | BPF_JEQ | BPF_X:
			ctx->flags |= SEEN_X;
			/* pc += ( A == X ) ? pc->jt : pc->jf */
			condt = MIPS_COND_EQ | MIPS_COND_X;
			goto jmp_cmp;
		case BPF_JMP | BPF_JGE | BPF_K:
			/* pc += ( A >= K ) ? pc->jt : pc->jf */
			condt = MIPS_COND_GE | MIPS_COND_K;
			goto jmp_cmp;
		case BPF_JMP | BPF_JGE | BPF_X:
			ctx->flags |= SEEN_X;
			/* pc += ( A >= X ) ? pc->jt : pc->jf */
			condt = MIPS_COND_GE | MIPS_COND_X;
			goto jmp_cmp;
		case BPF_JMP | BPF_JGT | BPF_K:
			/* pc += ( A > K ) ? pc->jt : pc->jf */
			condt = MIPS_COND_GT | MIPS_COND_K;
			goto jmp_cmp;
		case BPF_JMP | BPF_JGT | BPF_X:
			ctx->flags |= SEEN_X;
			/* pc += ( A > X ) ? pc->jt : pc->jf */
			condt = MIPS_COND_GT | MIPS_COND_X;
jmp_cmp:
			/* Greater or Equal */
			if ((condt & MIPS_COND_GE) ||
			    (condt & MIPS_COND_GT)) {
				if (condt & MIPS_COND_K) { /* K */
					ctx->flags |= SEEN_A;
					emit_sltiu(r_s0, r_A, k, ctx);
				} else { /* X */
					ctx->flags |= SEEN_A |
						SEEN_X;
					emit_sltu(r_s0, r_A, r_X, ctx);
				}
				/* A < (K|X) ? r_scrach = 1 */
				b_off = b_imm(i + inst->jf + 1, ctx);
				emit_bcond(MIPS_COND_NE, r_s0, r_zero, b_off,
					   ctx);
				emit_nop(ctx);
				/* A > (K|X) ? scratch = 0 */
				if (condt & MIPS_COND_GT) {
					/* Checking for equality */
					ctx->flags |= SEEN_A | SEEN_X;
					if (condt & MIPS_COND_K)
						emit_load_imm(r_s0, k, ctx);
					else
						emit_jit_reg_move(r_s0, r_X,
								  ctx);
					b_off = b_imm(i + inst->jf + 1, ctx);
					emit_bcond(MIPS_COND_EQ, r_A, r_s0,
						   b_off, ctx);
					emit_nop(ctx);
					/* Finally, A > K|X */
					b_off = b_imm(i + inst->jt + 1, ctx);
					emit_b(b_off, ctx);
					emit_nop(ctx);
				} else {
					/* A >= (K|X) so jump */
					b_off = b_imm(i + inst->jt + 1, ctx);
					emit_b(b_off, ctx);
					emit_nop(ctx);
				}
			} else {
				/* A == K|X */
				if (condt & MIPS_COND_K) { /* K */
					ctx->flags |= SEEN_A;
					emit_load_imm(r_s0, k, ctx);
					/* jump true */
					b_off = b_imm(i + inst->jt + 1, ctx);
					emit_bcond(MIPS_COND_EQ, r_A, r_s0,
						   b_off, ctx);
					emit_nop(ctx);
					/* jump false */
					b_off = b_imm(i + inst->jf + 1,
						      ctx);
					emit_bcond(MIPS_COND_NE, r_A, r_s0,
						   b_off, ctx);
					emit_nop(ctx);
				} else { /* X */
					/* jump true */
					ctx->flags |= SEEN_A | SEEN_X;
					b_off = b_imm(i + inst->jt + 1,
						      ctx);
					emit_bcond(MIPS_COND_EQ, r_A, r_X,
						   b_off, ctx);
					emit_nop(ctx);
					/* jump false */
					b_off = b_imm(i + inst->jf + 1, ctx);
					emit_bcond(MIPS_COND_NE, r_A, r_X,
						   b_off, ctx);
					emit_nop(ctx);
				}
			}
			break;
		case BPF_JMP | BPF_JSET | BPF_K:
			ctx->flags |= SEEN_A;
			/* pc += (A & K) ? pc -> jt : pc -> jf */
			emit_load_imm(r_s1, k, ctx);
			emit_and(r_s0, r_A, r_s1, ctx);
			/* jump true */
			b_off = b_imm(i + inst->jt + 1, ctx);
			emit_bcond(MIPS_COND_NE, r_s0, r_zero, b_off, ctx);
			emit_nop(ctx);
			/* jump false */
			b_off = b_imm(i + inst->jf + 1, ctx);
			emit_b(b_off, ctx);
			emit_nop(ctx);
			break;
		case BPF_JMP | BPF_JSET | BPF_X:
			ctx->flags |= SEEN_X | SEEN_A;
			/* pc += (A & X) ? pc -> jt : pc -> jf */
			emit_and(r_s0, r_A, r_X, ctx);
			/* jump true */
			b_off = b_imm(i + inst->jt + 1, ctx);
			emit_bcond(MIPS_COND_NE, r_s0, r_zero, b_off, ctx);
			emit_nop(ctx);
			/* jump false */
			b_off = b_imm(i + inst->jf + 1, ctx);
			emit_b(b_off, ctx);
			emit_nop(ctx);
			break;
		case BPF_RET | BPF_A:
			ctx->flags |= SEEN_A;
			if (i != prog->len - 1) {
				/*
				 * If this is not the last instruction
				 * then jump to the epilogue
				 */
				b_off = b_imm(prog->len, ctx);
				if (is_bad_offset(b_off))
					return -E2BIG;
				emit_b(b_off, ctx);
			}
			emit_reg_move(r_ret, r_A, ctx); /* delay slot */
			break;
		case BPF_RET | BPF_K:
			/*
			 * It can emit two instructions so it does not fit on
			 * the delay slot.
			 */
			emit_load_imm(r_ret, k, ctx);
			if (i != prog->len - 1) {
				/*
				 * If this is not the last instruction
				 * then jump to the epilogue
				 */
				b_off = b_imm(prog->len, ctx);
				if (is_bad_offset(b_off))
					return -E2BIG;
				emit_b(b_off, ctx);
				emit_nop(ctx);
			}
			break;
		case BPF_MISC | BPF_TAX:
			/* X = A */
			ctx->flags |= SEEN_X | SEEN_A;
			emit_jit_reg_move(r_X, r_A, ctx);
			break;
		case BPF_MISC | BPF_TXA:
			/* A = X */
			ctx->flags |= SEEN_A | SEEN_X;
			emit_jit_reg_move(r_A, r_X, ctx);
			break;
		/* AUX */
		case BPF_ANC | SKF_AD_PROTOCOL:
			/* A = ntohs(skb->protocol */
			ctx->flags |= SEEN_SKB | SEEN_OFF | SEEN_A;
			BUILD_BUG_ON(sizeof_field(struct sk_buff,
						  protocol) != 2);
			off = offsetof(struct sk_buff, protocol);
			emit_half_load(r_A, r_skb, off, ctx);
#ifdef CONFIG_CPU_LITTLE_ENDIAN
			/* This needs little endian fixup */
			if (cpu_has_wsbh) {
				/* R2 and later have the wsbh instruction */
				emit_wsbh(r_A, r_A, ctx);
			} else {
				/* Get first byte */
				emit_andi(r_tmp_imm, r_A, 0xff, ctx);
				/* Shift it */
				emit_sll(r_tmp, r_tmp_imm, 8, ctx);
				/* Get second byte */
				emit_srl(r_tmp_imm, r_A, 8, ctx);
				emit_andi(r_tmp_imm, r_tmp_imm, 0xff, ctx);
				/* Put everyting together in r_A */
				emit_or(r_A, r_tmp, r_tmp_imm, ctx);
			}
#endif
			break;
		case BPF_ANC | SKF_AD_CPU:
			ctx->flags |= SEEN_A | SEEN_OFF;
			/* A = current_thread_info()->cpu */
			BUILD_BUG_ON(sizeof_field(struct thread_info,
						  cpu) != 4);
			off = offsetof(struct thread_info, cpu);
			/* $28/gp points to the thread_info struct */
			emit_load(r_A, 28, off, ctx);
			break;
		case BPF_ANC | SKF_AD_IFINDEX:
			/* A = skb->dev->ifindex */
		case BPF_ANC | SKF_AD_HATYPE:
			/* A = skb->dev->type */
			ctx->flags |= SEEN_SKB | SEEN_A;
			off = offsetof(struct sk_buff, dev);
			/* Load *dev pointer */
			emit_load_ptr(r_s0, r_skb, off, ctx);
			/* error (0) in the delay slot */
			b_off = b_imm(prog->len, ctx);
			if (is_bad_offset(b_off))
				return -E2BIG;
			emit_bcond(MIPS_COND_EQ, r_s0, r_zero, b_off, ctx);
			emit_reg_move(r_ret, r_zero, ctx);
			if (code == (BPF_ANC | SKF_AD_IFINDEX)) {
				BUILD_BUG_ON(sizeof_field(struct net_device, ifindex) != 4);
				off = offsetof(struct net_device, ifindex);
				emit_load(r_A, r_s0, off, ctx);
			} else { /* (code == (BPF_ANC | SKF_AD_HATYPE) */
				BUILD_BUG_ON(sizeof_field(struct net_device, type) != 2);
				off = offsetof(struct net_device, type);
				emit_half_load_unsigned(r_A, r_s0, off, ctx);
			}
			break;
		case BPF_ANC | SKF_AD_MARK:
			ctx->flags |= SEEN_SKB | SEEN_A;
			BUILD_BUG_ON(sizeof_field(struct sk_buff, mark) != 4);
			off = offsetof(struct sk_buff, mark);
			emit_load(r_A, r_skb, off, ctx);
			break;
		case BPF_ANC | SKF_AD_RXHASH:
			ctx->flags |= SEEN_SKB | SEEN_A;
			BUILD_BUG_ON(sizeof_field(struct sk_buff, hash) != 4);
			off = offsetof(struct sk_buff, hash);
			emit_load(r_A, r_skb, off, ctx);
			break;
		case BPF_ANC | SKF_AD_VLAN_TAG:
			ctx->flags |= SEEN_SKB | SEEN_A;
			BUILD_BUG_ON(sizeof_field(struct sk_buff,
						  vlan_tci) != 2);
			off = offsetof(struct sk_buff, vlan_tci);
			emit_half_load_unsigned(r_A, r_skb, off, ctx);
			break;
		case BPF_ANC | SKF_AD_VLAN_TAG_PRESENT:
			ctx->flags |= SEEN_SKB | SEEN_A;
			emit_load_byte(r_A, r_skb, PKT_VLAN_PRESENT_OFFSET(), ctx);
			if (PKT_VLAN_PRESENT_BIT)
				emit_srl(r_A, r_A, PKT_VLAN_PRESENT_BIT, ctx);
			if (PKT_VLAN_PRESENT_BIT < 7)
				emit_andi(r_A, r_A, 1, ctx);
			break;
		case BPF_ANC | SKF_AD_PKTTYPE:
			ctx->flags |= SEEN_SKB;

			emit_load_byte(r_tmp, r_skb, PKT_TYPE_OFFSET(), ctx);
			/* Keep only the last 3 bits */
			emit_andi(r_A, r_tmp, PKT_TYPE_MAX, ctx);
#ifdef __BIG_ENDIAN_BITFIELD
			/* Get the actual packet type to the lower 3 bits */
			emit_srl(r_A, r_A, 5, ctx);
#endif
			break;
		case BPF_ANC | SKF_AD_QUEUE:
			ctx->flags |= SEEN_SKB | SEEN_A;
			BUILD_BUG_ON(sizeof_field(struct sk_buff,
						  queue_mapping) != 2);
			BUILD_BUG_ON(offsetof(struct sk_buff,
					      queue_mapping) > 0xff);
			off = offsetof(struct sk_buff, queue_mapping);
			emit_half_load_unsigned(r_A, r_skb, off, ctx);
			break;
		default:
			pr_debug("%s: Unhandled opcode: 0x%02x\n", __FILE__,
				 inst->code);
			return -1;
		}
	}

	/* compute offsets only during the first pass */
	if (ctx->target == NULL)
		ctx->offsets[i] = ctx->idx * 4;

	return 0;
}

void bpf_jit_compile(struct bpf_prog *fp)
{
	struct jit_ctx ctx;
	unsigned int alloc_size, tmp_idx;

	if (!bpf_jit_enable)
		return;

	memset(&ctx, 0, sizeof(ctx));

	ctx.offsets = kcalloc(fp->len + 1, sizeof(*ctx.offsets), GFP_KERNEL);
	if (ctx.offsets == NULL)
		return;

	ctx.skf = fp;

	if (build_body(&ctx))
		goto out;

	tmp_idx = ctx.idx;
	build_prologue(&ctx);
	ctx.prologue_bytes = (ctx.idx - tmp_idx) * 4;
	/* just to complete the ctx.idx count */
	build_epilogue(&ctx);

	alloc_size = 4 * ctx.idx;
	ctx.target = module_alloc(alloc_size);
	if (ctx.target == NULL)
		goto out;

	/* Clean it */
	memset(ctx.target, 0, alloc_size);

	ctx.idx = 0;

	/* Generate the actual JIT code */
	build_prologue(&ctx);
	if (build_body(&ctx)) {
		module_memfree(ctx.target);
		goto out;
	}
	build_epilogue(&ctx);

	/* Update the icache */
	flush_icache_range((ptr)ctx.target, (ptr)(ctx.target + ctx.idx));

	if (bpf_jit_enable > 1)
		/* Dump JIT code */
		bpf_jit_dump(fp->len, alloc_size, 2, ctx.target);

	fp->bpf_func = (void *)ctx.target;
	fp->jited = 1;

out:
	kfree(ctx.offsets);
}

void bpf_jit_free(struct bpf_prog *fp)
{
	if (fp->jited)
		module_memfree(fp->bpf_func);

	bpf_prog_unlock_free(fp);
}
