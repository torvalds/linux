/*
 * Just-In-Time compiler for BPF filters on 32bit ARM
 *
 * Copyright (c) 2011 Mircea Gherzan <mgherzan@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 */

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/filter.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/if_vlan.h>

#include <asm/cacheflush.h>
#include <asm/hwcap.h>
#include <asm/opcodes.h>

#include "bpf_jit_32.h"

/*
 * ABI:
 *
 * r0	scratch register
 * r4	BPF register A
 * r5	BPF register X
 * r6	pointer to the skb
 * r7	skb->data
 * r8	skb_headlen(skb)
 */

#define r_scratch	ARM_R0
/* r1-r3 are (also) used for the unaligned loads on the non-ARMv7 slowpath */
#define r_off		ARM_R1
#define r_A		ARM_R4
#define r_X		ARM_R5
#define r_skb		ARM_R6
#define r_skb_data	ARM_R7
#define r_skb_hl	ARM_R8

#define SCRATCH_SP_OFFSET	0
#define SCRATCH_OFF(k)		(SCRATCH_SP_OFFSET + 4 * (k))

#define SEEN_MEM		((1 << BPF_MEMWORDS) - 1)
#define SEEN_MEM_WORD(k)	(1 << (k))
#define SEEN_X			(1 << BPF_MEMWORDS)
#define SEEN_CALL		(1 << (BPF_MEMWORDS + 1))
#define SEEN_SKB		(1 << (BPF_MEMWORDS + 2))
#define SEEN_DATA		(1 << (BPF_MEMWORDS + 3))

#define FLAG_NEED_X_RESET	(1 << 0)
#define FLAG_IMM_OVERFLOW	(1 << 1)

struct jit_ctx {
	const struct bpf_prog *skf;
	unsigned idx;
	unsigned prologue_bytes;
	int ret0_fp_idx;
	u32 seen;
	u32 flags;
	u32 *offsets;
	u32 *target;
#if __LINUX_ARM_ARCH__ < 7
	u16 epilogue_bytes;
	u16 imm_count;
	u32 *imms;
#endif
};

int bpf_jit_enable __read_mostly;

static inline int call_neg_helper(struct sk_buff *skb, int offset, void *ret,
		      unsigned int size)
{
	void *ptr = bpf_internal_load_pointer_neg_helper(skb, offset, size);

	if (!ptr)
		return -EFAULT;
	memcpy(ret, ptr, size);
	return 0;
}

static u64 jit_get_skb_b(struct sk_buff *skb, int offset)
{
	u8 ret;
	int err;

	if (offset < 0)
		err = call_neg_helper(skb, offset, &ret, 1);
	else
		err = skb_copy_bits(skb, offset, &ret, 1);

	return (u64)err << 32 | ret;
}

static u64 jit_get_skb_h(struct sk_buff *skb, int offset)
{
	u16 ret;
	int err;

	if (offset < 0)
		err = call_neg_helper(skb, offset, &ret, 2);
	else
		err = skb_copy_bits(skb, offset, &ret, 2);

	return (u64)err << 32 | ntohs(ret);
}

static u64 jit_get_skb_w(struct sk_buff *skb, int offset)
{
	u32 ret;
	int err;

	if (offset < 0)
		err = call_neg_helper(skb, offset, &ret, 4);
	else
		err = skb_copy_bits(skb, offset, &ret, 4);

	return (u64)err << 32 | ntohl(ret);
}

/*
 * Wrappers which handle both OABI and EABI and assures Thumb2 interworking
 * (where the assembly routines like __aeabi_uidiv could cause problems).
 */
static u32 jit_udiv(u32 dividend, u32 divisor)
{
	return dividend / divisor;
}

static u32 jit_mod(u32 dividend, u32 divisor)
{
	return dividend % divisor;
}

static inline void _emit(int cond, u32 inst, struct jit_ctx *ctx)
{
	inst |= (cond << 28);
	inst = __opcode_to_mem_arm(inst);

	if (ctx->target != NULL)
		ctx->target[ctx->idx] = inst;

	ctx->idx++;
}

/*
 * Emit an instruction that will be executed unconditionally.
 */
static inline void emit(u32 inst, struct jit_ctx *ctx)
{
	_emit(ARM_COND_AL, inst, ctx);
}

static u16 saved_regs(struct jit_ctx *ctx)
{
	u16 ret = 0;

	if ((ctx->skf->len > 1) ||
	    (ctx->skf->insns[0].code == (BPF_RET | BPF_A)))
		ret |= 1 << r_A;

#ifdef CONFIG_FRAME_POINTER
	ret |= (1 << ARM_FP) | (1 << ARM_IP) | (1 << ARM_LR) | (1 << ARM_PC);
#else
	if (ctx->seen & SEEN_CALL)
		ret |= 1 << ARM_LR;
#endif
	if (ctx->seen & (SEEN_DATA | SEEN_SKB))
		ret |= 1 << r_skb;
	if (ctx->seen & SEEN_DATA)
		ret |= (1 << r_skb_data) | (1 << r_skb_hl);
	if (ctx->seen & SEEN_X)
		ret |= 1 << r_X;

	return ret;
}

static inline int mem_words_used(struct jit_ctx *ctx)
{
	/* yes, we do waste some stack space IF there are "holes" in the set" */
	return fls(ctx->seen & SEEN_MEM);
}

static inline bool is_load_to_a(u16 inst)
{
	switch (inst) {
	case BPF_LD | BPF_W | BPF_LEN:
	case BPF_LD | BPF_W | BPF_ABS:
	case BPF_LD | BPF_H | BPF_ABS:
	case BPF_LD | BPF_B | BPF_ABS:
		return true;
	default:
		return false;
	}
}

static void jit_fill_hole(void *area, unsigned int size)
{
	u32 *ptr;
	/* We are guaranteed to have aligned memory. */
	for (ptr = area; size >= sizeof(u32); size -= sizeof(u32))
		*ptr++ = __opcode_to_mem_arm(ARM_INST_UDF);
}

static void build_prologue(struct jit_ctx *ctx)
{
	u16 reg_set = saved_regs(ctx);
	u16 first_inst = ctx->skf->insns[0].code;
	u16 off;

#ifdef CONFIG_FRAME_POINTER
	emit(ARM_MOV_R(ARM_IP, ARM_SP), ctx);
	emit(ARM_PUSH(reg_set), ctx);
	emit(ARM_SUB_I(ARM_FP, ARM_IP, 4), ctx);
#else
	if (reg_set)
		emit(ARM_PUSH(reg_set), ctx);
#endif

	if (ctx->seen & (SEEN_DATA | SEEN_SKB))
		emit(ARM_MOV_R(r_skb, ARM_R0), ctx);

	if (ctx->seen & SEEN_DATA) {
		off = offsetof(struct sk_buff, data);
		emit(ARM_LDR_I(r_skb_data, r_skb, off), ctx);
		/* headlen = len - data_len */
		off = offsetof(struct sk_buff, len);
		emit(ARM_LDR_I(r_skb_hl, r_skb, off), ctx);
		off = offsetof(struct sk_buff, data_len);
		emit(ARM_LDR_I(r_scratch, r_skb, off), ctx);
		emit(ARM_SUB_R(r_skb_hl, r_skb_hl, r_scratch), ctx);
	}

	if (ctx->flags & FLAG_NEED_X_RESET)
		emit(ARM_MOV_I(r_X, 0), ctx);

	/* do not leak kernel data to userspace */
	if ((first_inst != (BPF_RET | BPF_K)) && !(is_load_to_a(first_inst)))
		emit(ARM_MOV_I(r_A, 0), ctx);

	/* stack space for the BPF_MEM words */
	if (ctx->seen & SEEN_MEM)
		emit(ARM_SUB_I(ARM_SP, ARM_SP, mem_words_used(ctx) * 4), ctx);
}

static void build_epilogue(struct jit_ctx *ctx)
{
	u16 reg_set = saved_regs(ctx);

	if (ctx->seen & SEEN_MEM)
		emit(ARM_ADD_I(ARM_SP, ARM_SP, mem_words_used(ctx) * 4), ctx);

	reg_set &= ~(1 << ARM_LR);

#ifdef CONFIG_FRAME_POINTER
	/* the first instruction of the prologue was: mov ip, sp */
	reg_set &= ~(1 << ARM_IP);
	reg_set |= (1 << ARM_SP);
	emit(ARM_LDM(ARM_SP, reg_set), ctx);
#else
	if (reg_set) {
		if (ctx->seen & SEEN_CALL)
			reg_set |= 1 << ARM_PC;
		emit(ARM_POP(reg_set), ctx);
	}

	if (!(ctx->seen & SEEN_CALL))
		emit(ARM_BX(ARM_LR), ctx);
#endif
}

static int16_t imm8m(u32 x)
{
	u32 rot;

	for (rot = 0; rot < 16; rot++)
		if ((x & ~ror32(0xff, 2 * rot)) == 0)
			return rol32(x, 2 * rot) | (rot << 8);

	return -1;
}

#if __LINUX_ARM_ARCH__ < 7

static u16 imm_offset(u32 k, struct jit_ctx *ctx)
{
	unsigned i = 0, offset;
	u16 imm;

	/* on the "fake" run we just count them (duplicates included) */
	if (ctx->target == NULL) {
		ctx->imm_count++;
		return 0;
	}

	while ((i < ctx->imm_count) && ctx->imms[i]) {
		if (ctx->imms[i] == k)
			break;
		i++;
	}

	if (ctx->imms[i] == 0)
		ctx->imms[i] = k;

	/* constants go just after the epilogue */
	offset =  ctx->offsets[ctx->skf->len];
	offset += ctx->prologue_bytes;
	offset += ctx->epilogue_bytes;
	offset += i * 4;

	ctx->target[offset / 4] = k;

	/* PC in ARM mode == address of the instruction + 8 */
	imm = offset - (8 + ctx->idx * 4);

	if (imm & ~0xfff) {
		/*
		 * literal pool is too far, signal it into flags. we
		 * can only detect it on the second pass unfortunately.
		 */
		ctx->flags |= FLAG_IMM_OVERFLOW;
		return 0;
	}

	return imm;
}

#endif /* __LINUX_ARM_ARCH__ */

/*
 * Move an immediate that's not an imm8m to a core register.
 */
static inline void emit_mov_i_no8m(int rd, u32 val, struct jit_ctx *ctx)
{
#if __LINUX_ARM_ARCH__ < 7
	emit(ARM_LDR_I(rd, ARM_PC, imm_offset(val, ctx)), ctx);
#else
	emit(ARM_MOVW(rd, val & 0xffff), ctx);
	if (val > 0xffff)
		emit(ARM_MOVT(rd, val >> 16), ctx);
#endif
}

static inline void emit_mov_i(int rd, u32 val, struct jit_ctx *ctx)
{
	int imm12 = imm8m(val);

	if (imm12 >= 0)
		emit(ARM_MOV_I(rd, imm12), ctx);
	else
		emit_mov_i_no8m(rd, val, ctx);
}

#if __LINUX_ARM_ARCH__ < 6

static void emit_load_be32(u8 cond, u8 r_res, u8 r_addr, struct jit_ctx *ctx)
{
	_emit(cond, ARM_LDRB_I(ARM_R3, r_addr, 1), ctx);
	_emit(cond, ARM_LDRB_I(ARM_R1, r_addr, 0), ctx);
	_emit(cond, ARM_LDRB_I(ARM_R2, r_addr, 3), ctx);
	_emit(cond, ARM_LSL_I(ARM_R3, ARM_R3, 16), ctx);
	_emit(cond, ARM_LDRB_I(ARM_R0, r_addr, 2), ctx);
	_emit(cond, ARM_ORR_S(ARM_R3, ARM_R3, ARM_R1, SRTYPE_LSL, 24), ctx);
	_emit(cond, ARM_ORR_R(ARM_R3, ARM_R3, ARM_R2), ctx);
	_emit(cond, ARM_ORR_S(r_res, ARM_R3, ARM_R0, SRTYPE_LSL, 8), ctx);
}

static void emit_load_be16(u8 cond, u8 r_res, u8 r_addr, struct jit_ctx *ctx)
{
	_emit(cond, ARM_LDRB_I(ARM_R1, r_addr, 0), ctx);
	_emit(cond, ARM_LDRB_I(ARM_R2, r_addr, 1), ctx);
	_emit(cond, ARM_ORR_S(r_res, ARM_R2, ARM_R1, SRTYPE_LSL, 8), ctx);
}

static inline void emit_swap16(u8 r_dst, u8 r_src, struct jit_ctx *ctx)
{
	/* r_dst = (r_src << 8) | (r_src >> 8) */
	emit(ARM_LSL_I(ARM_R1, r_src, 8), ctx);
	emit(ARM_ORR_S(r_dst, ARM_R1, r_src, SRTYPE_LSR, 8), ctx);

	/*
	 * we need to mask out the bits set in r_dst[23:16] due to
	 * the first shift instruction.
	 *
	 * note that 0x8ff is the encoded immediate 0x00ff0000.
	 */
	emit(ARM_BIC_I(r_dst, r_dst, 0x8ff), ctx);
}

#else  /* ARMv6+ */

static void emit_load_be32(u8 cond, u8 r_res, u8 r_addr, struct jit_ctx *ctx)
{
	_emit(cond, ARM_LDR_I(r_res, r_addr, 0), ctx);
#ifdef __LITTLE_ENDIAN
	_emit(cond, ARM_REV(r_res, r_res), ctx);
#endif
}

static void emit_load_be16(u8 cond, u8 r_res, u8 r_addr, struct jit_ctx *ctx)
{
	_emit(cond, ARM_LDRH_I(r_res, r_addr, 0), ctx);
#ifdef __LITTLE_ENDIAN
	_emit(cond, ARM_REV16(r_res, r_res), ctx);
#endif
}

static inline void emit_swap16(u8 r_dst __maybe_unused,
			       u8 r_src __maybe_unused,
			       struct jit_ctx *ctx __maybe_unused)
{
#ifdef __LITTLE_ENDIAN
	emit(ARM_REV16(r_dst, r_src), ctx);
#endif
}

#endif /* __LINUX_ARM_ARCH__ < 6 */


/* Compute the immediate value for a PC-relative branch. */
static inline u32 b_imm(unsigned tgt, struct jit_ctx *ctx)
{
	u32 imm;

	if (ctx->target == NULL)
		return 0;
	/*
	 * BPF allows only forward jumps and the offset of the target is
	 * still the one computed during the first pass.
	 */
	imm  = ctx->offsets[tgt] + ctx->prologue_bytes - (ctx->idx * 4 + 8);

	return imm >> 2;
}

#define OP_IMM3(op, r1, r2, imm_val, ctx)				\
	do {								\
		imm12 = imm8m(imm_val);					\
		if (imm12 < 0) {					\
			emit_mov_i_no8m(r_scratch, imm_val, ctx);	\
			emit(op ## _R((r1), (r2), r_scratch), ctx);	\
		} else {						\
			emit(op ## _I((r1), (r2), imm12), ctx);		\
		}							\
	} while (0)

static inline void emit_err_ret(u8 cond, struct jit_ctx *ctx)
{
	if (ctx->ret0_fp_idx >= 0) {
		_emit(cond, ARM_B(b_imm(ctx->ret0_fp_idx, ctx)), ctx);
		/* NOP to keep the size constant between passes */
		emit(ARM_MOV_R(ARM_R0, ARM_R0), ctx);
	} else {
		_emit(cond, ARM_MOV_I(ARM_R0, 0), ctx);
		_emit(cond, ARM_B(b_imm(ctx->skf->len, ctx)), ctx);
	}
}

static inline void emit_blx_r(u8 tgt_reg, struct jit_ctx *ctx)
{
#if __LINUX_ARM_ARCH__ < 5
	emit(ARM_MOV_R(ARM_LR, ARM_PC), ctx);

	if (elf_hwcap & HWCAP_THUMB)
		emit(ARM_BX(tgt_reg), ctx);
	else
		emit(ARM_MOV_R(ARM_PC, tgt_reg), ctx);
#else
	emit(ARM_BLX_R(tgt_reg), ctx);
#endif
}

static inline void emit_udivmod(u8 rd, u8 rm, u8 rn, struct jit_ctx *ctx,
				int bpf_op)
{
#if __LINUX_ARM_ARCH__ == 7
	if (elf_hwcap & HWCAP_IDIVA) {
		if (bpf_op == BPF_DIV)
			emit(ARM_UDIV(rd, rm, rn), ctx);
		else {
			emit(ARM_UDIV(ARM_R3, rm, rn), ctx);
			emit(ARM_MLS(rd, rn, ARM_R3, rm), ctx);
		}
		return;
	}
#endif

	/*
	 * For BPF_ALU | BPF_DIV | BPF_K instructions, rm is ARM_R4
	 * (r_A) and rn is ARM_R0 (r_scratch) so load rn first into
	 * ARM_R1 to avoid accidentally overwriting ARM_R0 with rm
	 * before using it as a source for ARM_R1.
	 *
	 * For BPF_ALU | BPF_DIV | BPF_X rm is ARM_R4 (r_A) and rn is
	 * ARM_R5 (r_X) so there is no particular register overlap
	 * issues.
	 */
	if (rn != ARM_R1)
		emit(ARM_MOV_R(ARM_R1, rn), ctx);
	if (rm != ARM_R0)
		emit(ARM_MOV_R(ARM_R0, rm), ctx);

	ctx->seen |= SEEN_CALL;
	emit_mov_i(ARM_R3, bpf_op == BPF_DIV ? (u32)jit_udiv : (u32)jit_mod,
		   ctx);
	emit_blx_r(ARM_R3, ctx);

	if (rd != ARM_R0)
		emit(ARM_MOV_R(rd, ARM_R0), ctx);
}

static inline void update_on_xread(struct jit_ctx *ctx)
{
	if (!(ctx->seen & SEEN_X))
		ctx->flags |= FLAG_NEED_X_RESET;

	ctx->seen |= SEEN_X;
}

static int build_body(struct jit_ctx *ctx)
{
	void *load_func[] = {jit_get_skb_b, jit_get_skb_h, jit_get_skb_w};
	const struct bpf_prog *prog = ctx->skf;
	const struct sock_filter *inst;
	unsigned i, load_order, off, condt;
	int imm12;
	u32 k;

	for (i = 0; i < prog->len; i++) {
		u16 code;

		inst = &(prog->insns[i]);
		/* K as an immediate value operand */
		k = inst->k;
		code = bpf_anc_helper(inst);

		/* compute offsets only in the fake pass */
		if (ctx->target == NULL)
			ctx->offsets[i] = ctx->idx * 4;

		switch (code) {
		case BPF_LD | BPF_IMM:
			emit_mov_i(r_A, k, ctx);
			break;
		case BPF_LD | BPF_W | BPF_LEN:
			ctx->seen |= SEEN_SKB;
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, len) != 4);
			emit(ARM_LDR_I(r_A, r_skb,
				       offsetof(struct sk_buff, len)), ctx);
			break;
		case BPF_LD | BPF_MEM:
			/* A = scratch[k] */
			ctx->seen |= SEEN_MEM_WORD(k);
			emit(ARM_LDR_I(r_A, ARM_SP, SCRATCH_OFF(k)), ctx);
			break;
		case BPF_LD | BPF_W | BPF_ABS:
			load_order = 2;
			goto load;
		case BPF_LD | BPF_H | BPF_ABS:
			load_order = 1;
			goto load;
		case BPF_LD | BPF_B | BPF_ABS:
			load_order = 0;
load:
			emit_mov_i(r_off, k, ctx);
load_common:
			ctx->seen |= SEEN_DATA | SEEN_CALL;

			if (load_order > 0) {
				emit(ARM_SUB_I(r_scratch, r_skb_hl,
					       1 << load_order), ctx);
				emit(ARM_CMP_R(r_scratch, r_off), ctx);
				condt = ARM_COND_GE;
			} else {
				emit(ARM_CMP_R(r_skb_hl, r_off), ctx);
				condt = ARM_COND_HI;
			}

			/*
			 * test for negative offset, only if we are
			 * currently scheduled to take the fast
			 * path. this will update the flags so that
			 * the slowpath instruction are ignored if the
			 * offset is negative.
			 *
			 * for loard_order == 0 the HI condition will
			 * make loads at offset 0 take the slow path too.
			 */
			_emit(condt, ARM_CMP_I(r_off, 0), ctx);

			_emit(condt, ARM_ADD_R(r_scratch, r_off, r_skb_data),
			      ctx);

			if (load_order == 0)
				_emit(condt, ARM_LDRB_I(r_A, r_scratch, 0),
				      ctx);
			else if (load_order == 1)
				emit_load_be16(condt, r_A, r_scratch, ctx);
			else if (load_order == 2)
				emit_load_be32(condt, r_A, r_scratch, ctx);

			_emit(condt, ARM_B(b_imm(i + 1, ctx)), ctx);

			/* the slowpath */
			emit_mov_i(ARM_R3, (u32)load_func[load_order], ctx);
			emit(ARM_MOV_R(ARM_R0, r_skb), ctx);
			/* the offset is already in R1 */
			emit_blx_r(ARM_R3, ctx);
			/* check the result of skb_copy_bits */
			emit(ARM_CMP_I(ARM_R1, 0), ctx);
			emit_err_ret(ARM_COND_NE, ctx);
			emit(ARM_MOV_R(r_A, ARM_R0), ctx);
			break;
		case BPF_LD | BPF_W | BPF_IND:
			load_order = 2;
			goto load_ind;
		case BPF_LD | BPF_H | BPF_IND:
			load_order = 1;
			goto load_ind;
		case BPF_LD | BPF_B | BPF_IND:
			load_order = 0;
load_ind:
			update_on_xread(ctx);
			OP_IMM3(ARM_ADD, r_off, r_X, k, ctx);
			goto load_common;
		case BPF_LDX | BPF_IMM:
			ctx->seen |= SEEN_X;
			emit_mov_i(r_X, k, ctx);
			break;
		case BPF_LDX | BPF_W | BPF_LEN:
			ctx->seen |= SEEN_X | SEEN_SKB;
			emit(ARM_LDR_I(r_X, r_skb,
				       offsetof(struct sk_buff, len)), ctx);
			break;
		case BPF_LDX | BPF_MEM:
			ctx->seen |= SEEN_X | SEEN_MEM_WORD(k);
			emit(ARM_LDR_I(r_X, ARM_SP, SCRATCH_OFF(k)), ctx);
			break;
		case BPF_LDX | BPF_B | BPF_MSH:
			/* x = ((*(frame + k)) & 0xf) << 2; */
			ctx->seen |= SEEN_X | SEEN_DATA | SEEN_CALL;
			/* the interpreter should deal with the negative K */
			if ((int)k < 0)
				return -1;
			/* offset in r1: we might have to take the slow path */
			emit_mov_i(r_off, k, ctx);
			emit(ARM_CMP_R(r_skb_hl, r_off), ctx);

			/* load in r0: common with the slowpath */
			_emit(ARM_COND_HI, ARM_LDRB_R(ARM_R0, r_skb_data,
						      ARM_R1), ctx);
			/*
			 * emit_mov_i() might generate one or two instructions,
			 * the same holds for emit_blx_r()
			 */
			_emit(ARM_COND_HI, ARM_B(b_imm(i + 1, ctx) - 2), ctx);

			emit(ARM_MOV_R(ARM_R0, r_skb), ctx);
			/* r_off is r1 */
			emit_mov_i(ARM_R3, (u32)jit_get_skb_b, ctx);
			emit_blx_r(ARM_R3, ctx);
			/* check the return value of skb_copy_bits */
			emit(ARM_CMP_I(ARM_R1, 0), ctx);
			emit_err_ret(ARM_COND_NE, ctx);

			emit(ARM_AND_I(r_X, ARM_R0, 0x00f), ctx);
			emit(ARM_LSL_I(r_X, r_X, 2), ctx);
			break;
		case BPF_ST:
			ctx->seen |= SEEN_MEM_WORD(k);
			emit(ARM_STR_I(r_A, ARM_SP, SCRATCH_OFF(k)), ctx);
			break;
		case BPF_STX:
			update_on_xread(ctx);
			ctx->seen |= SEEN_MEM_WORD(k);
			emit(ARM_STR_I(r_X, ARM_SP, SCRATCH_OFF(k)), ctx);
			break;
		case BPF_ALU | BPF_ADD | BPF_K:
			/* A += K */
			OP_IMM3(ARM_ADD, r_A, r_A, k, ctx);
			break;
		case BPF_ALU | BPF_ADD | BPF_X:
			update_on_xread(ctx);
			emit(ARM_ADD_R(r_A, r_A, r_X), ctx);
			break;
		case BPF_ALU | BPF_SUB | BPF_K:
			/* A -= K */
			OP_IMM3(ARM_SUB, r_A, r_A, k, ctx);
			break;
		case BPF_ALU | BPF_SUB | BPF_X:
			update_on_xread(ctx);
			emit(ARM_SUB_R(r_A, r_A, r_X), ctx);
			break;
		case BPF_ALU | BPF_MUL | BPF_K:
			/* A *= K */
			emit_mov_i(r_scratch, k, ctx);
			emit(ARM_MUL(r_A, r_A, r_scratch), ctx);
			break;
		case BPF_ALU | BPF_MUL | BPF_X:
			update_on_xread(ctx);
			emit(ARM_MUL(r_A, r_A, r_X), ctx);
			break;
		case BPF_ALU | BPF_DIV | BPF_K:
			if (k == 1)
				break;
			emit_mov_i(r_scratch, k, ctx);
			emit_udivmod(r_A, r_A, r_scratch, ctx, BPF_DIV);
			break;
		case BPF_ALU | BPF_DIV | BPF_X:
			update_on_xread(ctx);
			emit(ARM_CMP_I(r_X, 0), ctx);
			emit_err_ret(ARM_COND_EQ, ctx);
			emit_udivmod(r_A, r_A, r_X, ctx, BPF_DIV);
			break;
		case BPF_ALU | BPF_MOD | BPF_K:
			if (k == 1) {
				emit_mov_i(r_A, 0, ctx);
				break;
			}
			emit_mov_i(r_scratch, k, ctx);
			emit_udivmod(r_A, r_A, r_scratch, ctx, BPF_MOD);
			break;
		case BPF_ALU | BPF_MOD | BPF_X:
			update_on_xread(ctx);
			emit(ARM_CMP_I(r_X, 0), ctx);
			emit_err_ret(ARM_COND_EQ, ctx);
			emit_udivmod(r_A, r_A, r_X, ctx, BPF_MOD);
			break;
		case BPF_ALU | BPF_OR | BPF_K:
			/* A |= K */
			OP_IMM3(ARM_ORR, r_A, r_A, k, ctx);
			break;
		case BPF_ALU | BPF_OR | BPF_X:
			update_on_xread(ctx);
			emit(ARM_ORR_R(r_A, r_A, r_X), ctx);
			break;
		case BPF_ALU | BPF_XOR | BPF_K:
			/* A ^= K; */
			OP_IMM3(ARM_EOR, r_A, r_A, k, ctx);
			break;
		case BPF_ANC | SKF_AD_ALU_XOR_X:
		case BPF_ALU | BPF_XOR | BPF_X:
			/* A ^= X */
			update_on_xread(ctx);
			emit(ARM_EOR_R(r_A, r_A, r_X), ctx);
			break;
		case BPF_ALU | BPF_AND | BPF_K:
			/* A &= K */
			OP_IMM3(ARM_AND, r_A, r_A, k, ctx);
			break;
		case BPF_ALU | BPF_AND | BPF_X:
			update_on_xread(ctx);
			emit(ARM_AND_R(r_A, r_A, r_X), ctx);
			break;
		case BPF_ALU | BPF_LSH | BPF_K:
			if (unlikely(k > 31))
				return -1;
			emit(ARM_LSL_I(r_A, r_A, k), ctx);
			break;
		case BPF_ALU | BPF_LSH | BPF_X:
			update_on_xread(ctx);
			emit(ARM_LSL_R(r_A, r_A, r_X), ctx);
			break;
		case BPF_ALU | BPF_RSH | BPF_K:
			if (unlikely(k > 31))
				return -1;
			emit(ARM_LSR_I(r_A, r_A, k), ctx);
			break;
		case BPF_ALU | BPF_RSH | BPF_X:
			update_on_xread(ctx);
			emit(ARM_LSR_R(r_A, r_A, r_X), ctx);
			break;
		case BPF_ALU | BPF_NEG:
			/* A = -A */
			emit(ARM_RSB_I(r_A, r_A, 0), ctx);
			break;
		case BPF_JMP | BPF_JA:
			/* pc += K */
			emit(ARM_B(b_imm(i + k + 1, ctx)), ctx);
			break;
		case BPF_JMP | BPF_JEQ | BPF_K:
			/* pc += (A == K) ? pc->jt : pc->jf */
			condt  = ARM_COND_EQ;
			goto cmp_imm;
		case BPF_JMP | BPF_JGT | BPF_K:
			/* pc += (A > K) ? pc->jt : pc->jf */
			condt  = ARM_COND_HI;
			goto cmp_imm;
		case BPF_JMP | BPF_JGE | BPF_K:
			/* pc += (A >= K) ? pc->jt : pc->jf */
			condt  = ARM_COND_HS;
cmp_imm:
			imm12 = imm8m(k);
			if (imm12 < 0) {
				emit_mov_i_no8m(r_scratch, k, ctx);
				emit(ARM_CMP_R(r_A, r_scratch), ctx);
			} else {
				emit(ARM_CMP_I(r_A, imm12), ctx);
			}
cond_jump:
			if (inst->jt)
				_emit(condt, ARM_B(b_imm(i + inst->jt + 1,
						   ctx)), ctx);
			if (inst->jf)
				_emit(condt ^ 1, ARM_B(b_imm(i + inst->jf + 1,
							     ctx)), ctx);
			break;
		case BPF_JMP | BPF_JEQ | BPF_X:
			/* pc += (A == X) ? pc->jt : pc->jf */
			condt   = ARM_COND_EQ;
			goto cmp_x;
		case BPF_JMP | BPF_JGT | BPF_X:
			/* pc += (A > X) ? pc->jt : pc->jf */
			condt   = ARM_COND_HI;
			goto cmp_x;
		case BPF_JMP | BPF_JGE | BPF_X:
			/* pc += (A >= X) ? pc->jt : pc->jf */
			condt   = ARM_COND_CS;
cmp_x:
			update_on_xread(ctx);
			emit(ARM_CMP_R(r_A, r_X), ctx);
			goto cond_jump;
		case BPF_JMP | BPF_JSET | BPF_K:
			/* pc += (A & K) ? pc->jt : pc->jf */
			condt  = ARM_COND_NE;
			/* not set iff all zeroes iff Z==1 iff EQ */

			imm12 = imm8m(k);
			if (imm12 < 0) {
				emit_mov_i_no8m(r_scratch, k, ctx);
				emit(ARM_TST_R(r_A, r_scratch), ctx);
			} else {
				emit(ARM_TST_I(r_A, imm12), ctx);
			}
			goto cond_jump;
		case BPF_JMP | BPF_JSET | BPF_X:
			/* pc += (A & X) ? pc->jt : pc->jf */
			update_on_xread(ctx);
			condt  = ARM_COND_NE;
			emit(ARM_TST_R(r_A, r_X), ctx);
			goto cond_jump;
		case BPF_RET | BPF_A:
			emit(ARM_MOV_R(ARM_R0, r_A), ctx);
			goto b_epilogue;
		case BPF_RET | BPF_K:
			if ((k == 0) && (ctx->ret0_fp_idx < 0))
				ctx->ret0_fp_idx = i;
			emit_mov_i(ARM_R0, k, ctx);
b_epilogue:
			if (i != ctx->skf->len - 1)
				emit(ARM_B(b_imm(prog->len, ctx)), ctx);
			break;
		case BPF_MISC | BPF_TAX:
			/* X = A */
			ctx->seen |= SEEN_X;
			emit(ARM_MOV_R(r_X, r_A), ctx);
			break;
		case BPF_MISC | BPF_TXA:
			/* A = X */
			update_on_xread(ctx);
			emit(ARM_MOV_R(r_A, r_X), ctx);
			break;
		case BPF_ANC | SKF_AD_PROTOCOL:
			/* A = ntohs(skb->protocol) */
			ctx->seen |= SEEN_SKB;
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff,
						  protocol) != 2);
			off = offsetof(struct sk_buff, protocol);
			emit(ARM_LDRH_I(r_scratch, r_skb, off), ctx);
			emit_swap16(r_A, r_scratch, ctx);
			break;
		case BPF_ANC | SKF_AD_CPU:
			/* r_scratch = current_thread_info() */
			OP_IMM3(ARM_BIC, r_scratch, ARM_SP, THREAD_SIZE - 1, ctx);
			/* A = current_thread_info()->cpu */
			BUILD_BUG_ON(FIELD_SIZEOF(struct thread_info, cpu) != 4);
			off = offsetof(struct thread_info, cpu);
			emit(ARM_LDR_I(r_A, r_scratch, off), ctx);
			break;
		case BPF_ANC | SKF_AD_IFINDEX:
		case BPF_ANC | SKF_AD_HATYPE:
			/* A = skb->dev->ifindex */
			/* A = skb->dev->type */
			ctx->seen |= SEEN_SKB;
			off = offsetof(struct sk_buff, dev);
			emit(ARM_LDR_I(r_scratch, r_skb, off), ctx);

			emit(ARM_CMP_I(r_scratch, 0), ctx);
			emit_err_ret(ARM_COND_EQ, ctx);

			BUILD_BUG_ON(FIELD_SIZEOF(struct net_device,
						  ifindex) != 4);
			BUILD_BUG_ON(FIELD_SIZEOF(struct net_device,
						  type) != 2);

			if (code == (BPF_ANC | SKF_AD_IFINDEX)) {
				off = offsetof(struct net_device, ifindex);
				emit(ARM_LDR_I(r_A, r_scratch, off), ctx);
			} else {
				/*
				 * offset of field "type" in "struct
				 * net_device" is above what can be
				 * used in the ldrh rd, [rn, #imm]
				 * instruction, so load the offset in
				 * a register and use ldrh rd, [rn, rm]
				 */
				off = offsetof(struct net_device, type);
				emit_mov_i(ARM_R3, off, ctx);
				emit(ARM_LDRH_R(r_A, r_scratch, ARM_R3), ctx);
			}
			break;
		case BPF_ANC | SKF_AD_MARK:
			ctx->seen |= SEEN_SKB;
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, mark) != 4);
			off = offsetof(struct sk_buff, mark);
			emit(ARM_LDR_I(r_A, r_skb, off), ctx);
			break;
		case BPF_ANC | SKF_AD_RXHASH:
			ctx->seen |= SEEN_SKB;
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, hash) != 4);
			off = offsetof(struct sk_buff, hash);
			emit(ARM_LDR_I(r_A, r_skb, off), ctx);
			break;
		case BPF_ANC | SKF_AD_VLAN_TAG:
		case BPF_ANC | SKF_AD_VLAN_TAG_PRESENT:
			ctx->seen |= SEEN_SKB;
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, vlan_tci) != 2);
			off = offsetof(struct sk_buff, vlan_tci);
			emit(ARM_LDRH_I(r_A, r_skb, off), ctx);
			if (code == (BPF_ANC | SKF_AD_VLAN_TAG))
				OP_IMM3(ARM_AND, r_A, r_A, ~VLAN_TAG_PRESENT, ctx);
			else {
				OP_IMM3(ARM_LSR, r_A, r_A, 12, ctx);
				OP_IMM3(ARM_AND, r_A, r_A, 0x1, ctx);
			}
			break;
		case BPF_ANC | SKF_AD_PKTTYPE:
			ctx->seen |= SEEN_SKB;
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff,
						  __pkt_type_offset[0]) != 1);
			off = PKT_TYPE_OFFSET();
			emit(ARM_LDRB_I(r_A, r_skb, off), ctx);
			emit(ARM_AND_I(r_A, r_A, PKT_TYPE_MAX), ctx);
#ifdef __BIG_ENDIAN_BITFIELD
			emit(ARM_LSR_I(r_A, r_A, 5), ctx);
#endif
			break;
		case BPF_ANC | SKF_AD_QUEUE:
			ctx->seen |= SEEN_SKB;
			BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff,
						  queue_mapping) != 2);
			BUILD_BUG_ON(offsetof(struct sk_buff,
					      queue_mapping) > 0xff);
			off = offsetof(struct sk_buff, queue_mapping);
			emit(ARM_LDRH_I(r_A, r_skb, off), ctx);
			break;
		case BPF_ANC | SKF_AD_PAY_OFFSET:
			ctx->seen |= SEEN_SKB | SEEN_CALL;

			emit(ARM_MOV_R(ARM_R0, r_skb), ctx);
			emit_mov_i(ARM_R3, (unsigned int)skb_get_poff, ctx);
			emit_blx_r(ARM_R3, ctx);
			emit(ARM_MOV_R(r_A, ARM_R0), ctx);
			break;
		case BPF_LDX | BPF_W | BPF_ABS:
			/*
			 * load a 32bit word from struct seccomp_data.
			 * seccomp_check_filter() will already have checked
			 * that k is 32bit aligned and lies within the
			 * struct seccomp_data.
			 */
			ctx->seen |= SEEN_SKB;
			emit(ARM_LDR_I(r_A, r_skb, k), ctx);
			break;
		default:
			return -1;
		}

		if (ctx->flags & FLAG_IMM_OVERFLOW)
			/*
			 * this instruction generated an overflow when
			 * trying to access the literal pool, so
			 * delegate this filter to the kernel interpreter.
			 */
			return -1;
	}

	/* compute offsets only during the first pass */
	if (ctx->target == NULL)
		ctx->offsets[i] = ctx->idx * 4;

	return 0;
}


void bpf_jit_compile(struct bpf_prog *fp)
{
	struct bpf_binary_header *header;
	struct jit_ctx ctx;
	unsigned tmp_idx;
	unsigned alloc_size;
	u8 *target_ptr;

	if (!bpf_jit_enable)
		return;

	memset(&ctx, 0, sizeof(ctx));
	ctx.skf		= fp;
	ctx.ret0_fp_idx = -1;

	ctx.offsets = kzalloc(4 * (ctx.skf->len + 1), GFP_KERNEL);
	if (ctx.offsets == NULL)
		return;

	/* fake pass to fill in the ctx->seen */
	if (unlikely(build_body(&ctx)))
		goto out;

	tmp_idx = ctx.idx;
	build_prologue(&ctx);
	ctx.prologue_bytes = (ctx.idx - tmp_idx) * 4;

#if __LINUX_ARM_ARCH__ < 7
	tmp_idx = ctx.idx;
	build_epilogue(&ctx);
	ctx.epilogue_bytes = (ctx.idx - tmp_idx) * 4;

	ctx.idx += ctx.imm_count;
	if (ctx.imm_count) {
		ctx.imms = kzalloc(4 * ctx.imm_count, GFP_KERNEL);
		if (ctx.imms == NULL)
			goto out;
	}
#else
	/* there's nothing after the epilogue on ARMv7 */
	build_epilogue(&ctx);
#endif
	alloc_size = 4 * ctx.idx;
	header = bpf_jit_binary_alloc(alloc_size, &target_ptr,
				      4, jit_fill_hole);
	if (header == NULL)
		goto out;

	ctx.target = (u32 *) target_ptr;
	ctx.idx = 0;

	build_prologue(&ctx);
	if (build_body(&ctx) < 0) {
#if __LINUX_ARM_ARCH__ < 7
		if (ctx.imm_count)
			kfree(ctx.imms);
#endif
		bpf_jit_binary_free(header);
		goto out;
	}
	build_epilogue(&ctx);

	flush_icache_range((u32)ctx.target, (u32)(ctx.target + ctx.idx));

#if __LINUX_ARM_ARCH__ < 7
	if (ctx.imm_count)
		kfree(ctx.imms);
#endif

	if (bpf_jit_enable > 1)
		/* there are 2 passes here */
		bpf_jit_dump(fp->len, alloc_size, 2, ctx.target);

	set_memory_ro((unsigned long)header, header->pages);
	fp->bpf_func = (void *)ctx.target;
	fp->jited = 1;
out:
	kfree(ctx.offsets);
	return;
}

void bpf_jit_free(struct bpf_prog *fp)
{
	unsigned long addr = (unsigned long)fp->bpf_func & PAGE_MASK;
	struct bpf_binary_header *header = (void *)addr;

	if (!fp->jited)
		goto free_filter;

	set_memory_rw(addr, header->pages);
	bpf_jit_binary_free(header);

free_filter:
	bpf_prog_unlock_free(fp);
}
