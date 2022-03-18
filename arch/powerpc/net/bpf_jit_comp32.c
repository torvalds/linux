// SPDX-License-Identifier: GPL-2.0-only
/*
 * eBPF JIT compiler for PPC32
 *
 * Copyright 2020 Christophe Leroy <christophe.leroy@csgroup.eu>
 *		  CS GROUP France
 *
 * Based on PPC64 eBPF JIT compiler by Naveen N. Rao
 */
#include <linux/moduleloader.h>
#include <asm/cacheflush.h>
#include <asm/asm-compat.h>
#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/if_vlan.h>
#include <asm/kprobes.h>
#include <linux/bpf.h>

#include "bpf_jit.h"

/*
 * Stack layout:
 *
 *		[	prev sp		] <-------------
 *		[   nv gpr save area	] 16 * 4	|
 * fp (r31) -->	[   ebpf stack space	] upto 512	|
 *		[     frame header	] 16		|
 * sp (r1) --->	[    stack pointer	] --------------
 */

/* for gpr non volatile registers r17 to r31 (14) + tail call */
#define BPF_PPC_STACK_SAVE	(15 * 4 + 4)
/* stack frame, ensure this is quadword aligned */
#define BPF_PPC_STACKFRAME(ctx)	(STACK_FRAME_MIN_SIZE + BPF_PPC_STACK_SAVE + (ctx)->stack_size)

/* BPF register usage */
#define TMP_REG	(MAX_BPF_JIT_REG + 0)

/* BPF to ppc register mappings */
const int b2p[MAX_BPF_JIT_REG + 1] = {
	/* function return value */
	[BPF_REG_0] = 12,
	/* function arguments */
	[BPF_REG_1] = 4,
	[BPF_REG_2] = 6,
	[BPF_REG_3] = 8,
	[BPF_REG_4] = 10,
	[BPF_REG_5] = 22,
	/* non volatile registers */
	[BPF_REG_6] = 24,
	[BPF_REG_7] = 26,
	[BPF_REG_8] = 28,
	[BPF_REG_9] = 30,
	/* frame pointer aka BPF_REG_10 */
	[BPF_REG_FP] = 18,
	/* eBPF jit internal registers */
	[BPF_REG_AX] = 20,
	[TMP_REG] = 31,		/* 32 bits */
};

static int bpf_to_ppc(struct codegen_context *ctx, int reg)
{
	return ctx->b2p[reg];
}

/* PPC NVR range -- update this if we ever use NVRs below r17 */
#define BPF_PPC_NVR_MIN		17
#define BPF_PPC_TC		16

static int bpf_jit_stack_offsetof(struct codegen_context *ctx, int reg)
{
	if ((reg >= BPF_PPC_NVR_MIN && reg < 32) || reg == BPF_PPC_TC)
		return BPF_PPC_STACKFRAME(ctx) - 4 * (32 - reg);

	WARN(true, "BPF JIT is asking about unknown registers, will crash the stack");
	/* Use the hole we have left for alignment */
	return BPF_PPC_STACKFRAME(ctx) - 4;
}

void bpf_jit_realloc_regs(struct codegen_context *ctx)
{
	if (ctx->seen & SEEN_FUNC)
		return;

	while (ctx->seen & SEEN_NVREG_MASK &&
	      (ctx->seen & SEEN_VREG_MASK) != SEEN_VREG_MASK) {
		int old = 32 - fls(ctx->seen & (SEEN_NVREG_MASK & 0xaaaaaaab));
		int new = 32 - fls(~ctx->seen & (SEEN_VREG_MASK & 0xaaaaaaaa));
		int i;

		for (i = BPF_REG_0; i <= TMP_REG; i++) {
			if (ctx->b2p[i] != old)
				continue;
			ctx->b2p[i] = new;
			bpf_set_seen_register(ctx, new);
			bpf_clear_seen_register(ctx, old);
			if (i != TMP_REG) {
				bpf_set_seen_register(ctx, new - 1);
				bpf_clear_seen_register(ctx, old - 1);
			}
			break;
		}
	}
}

void bpf_jit_build_prologue(u32 *image, struct codegen_context *ctx)
{
	int i;

	/* First arg comes in as a 32 bits pointer. */
	EMIT(PPC_RAW_MR(bpf_to_ppc(ctx, BPF_REG_1), _R3));
	EMIT(PPC_RAW_LI(bpf_to_ppc(ctx, BPF_REG_1) - 1, 0));
	EMIT(PPC_RAW_STWU(_R1, _R1, -BPF_PPC_STACKFRAME(ctx)));

	/*
	 * Initialize tail_call_cnt in stack frame if we do tail calls.
	 * Otherwise, put in NOPs so that it can be skipped when we are
	 * invoked through a tail call.
	 */
	if (ctx->seen & SEEN_TAILCALL)
		EMIT(PPC_RAW_STW(bpf_to_ppc(ctx, BPF_REG_1) - 1, _R1,
				 bpf_jit_stack_offsetof(ctx, BPF_PPC_TC)));
	else
		EMIT(PPC_RAW_NOP());

#define BPF_TAILCALL_PROLOGUE_SIZE	16

	/*
	 * We need a stack frame, but we don't necessarily need to
	 * save/restore LR unless we call other functions
	 */
	if (ctx->seen & SEEN_FUNC)
		EMIT(PPC_RAW_MFLR(_R0));

	/*
	 * Back up non-volatile regs -- registers r18-r31
	 */
	for (i = BPF_PPC_NVR_MIN; i <= 31; i++)
		if (bpf_is_seen_register(ctx, i))
			EMIT(PPC_RAW_STW(i, _R1, bpf_jit_stack_offsetof(ctx, i)));

	/* If needed retrieve arguments 9 and 10, ie 5th 64 bits arg.*/
	if (bpf_is_seen_register(ctx, bpf_to_ppc(ctx, BPF_REG_5))) {
		EMIT(PPC_RAW_LWZ(bpf_to_ppc(ctx, BPF_REG_5) - 1, _R1, BPF_PPC_STACKFRAME(ctx)) + 8);
		EMIT(PPC_RAW_LWZ(bpf_to_ppc(ctx, BPF_REG_5), _R1, BPF_PPC_STACKFRAME(ctx)) + 12);
	}

	/* Setup frame pointer to point to the bpf stack area */
	if (bpf_is_seen_register(ctx, bpf_to_ppc(ctx, BPF_REG_FP))) {
		EMIT(PPC_RAW_LI(bpf_to_ppc(ctx, BPF_REG_FP) - 1, 0));
		EMIT(PPC_RAW_ADDI(bpf_to_ppc(ctx, BPF_REG_FP), _R1,
				  STACK_FRAME_MIN_SIZE + ctx->stack_size));
	}

	if (ctx->seen & SEEN_FUNC)
		EMIT(PPC_RAW_STW(_R0, _R1, BPF_PPC_STACKFRAME(ctx) + PPC_LR_STKOFF));
}

static void bpf_jit_emit_common_epilogue(u32 *image, struct codegen_context *ctx)
{
	int i;

	/* Restore NVRs */
	for (i = BPF_PPC_NVR_MIN; i <= 31; i++)
		if (bpf_is_seen_register(ctx, i))
			EMIT(PPC_RAW_LWZ(i, _R1, bpf_jit_stack_offsetof(ctx, i)));
}

void bpf_jit_build_epilogue(u32 *image, struct codegen_context *ctx)
{
	EMIT(PPC_RAW_MR(_R3, bpf_to_ppc(ctx, BPF_REG_0)));

	bpf_jit_emit_common_epilogue(image, ctx);

	/* Tear down our stack frame */

	if (ctx->seen & SEEN_FUNC)
		EMIT(PPC_RAW_LWZ(_R0, _R1, BPF_PPC_STACKFRAME(ctx) + PPC_LR_STKOFF));

	EMIT(PPC_RAW_ADDI(_R1, _R1, BPF_PPC_STACKFRAME(ctx)));

	if (ctx->seen & SEEN_FUNC)
		EMIT(PPC_RAW_MTLR(_R0));

	EMIT(PPC_RAW_BLR());
}

void bpf_jit_emit_func_call_rel(u32 *image, struct codegen_context *ctx, u64 func)
{
	s32 rel = (s32)func - (s32)(image + ctx->idx);

	if (image && rel < 0x2000000 && rel >= -0x2000000) {
		PPC_BL_ABS(func);
		EMIT(PPC_RAW_NOP());
		EMIT(PPC_RAW_NOP());
		EMIT(PPC_RAW_NOP());
	} else {
		/* Load function address into r0 */
		EMIT(PPC_RAW_LIS(_R0, IMM_H(func)));
		EMIT(PPC_RAW_ORI(_R0, _R0, IMM_L(func)));
		EMIT(PPC_RAW_MTCTR(_R0));
		EMIT(PPC_RAW_BCTRL());
	}
}

static int bpf_jit_emit_tail_call(u32 *image, struct codegen_context *ctx, u32 out)
{
	/*
	 * By now, the eBPF program has already setup parameters in r3-r6
	 * r3-r4/BPF_REG_1 - pointer to ctx -- passed as is to the next bpf program
	 * r5-r6/BPF_REG_2 - pointer to bpf_array
	 * r7-r8/BPF_REG_3 - index in bpf_array
	 */
	int b2p_bpf_array = bpf_to_ppc(ctx, BPF_REG_2);
	int b2p_index = bpf_to_ppc(ctx, BPF_REG_3);

	/*
	 * if (index >= array->map.max_entries)
	 *   goto out;
	 */
	EMIT(PPC_RAW_LWZ(_R0, b2p_bpf_array, offsetof(struct bpf_array, map.max_entries)));
	EMIT(PPC_RAW_CMPLW(b2p_index, _R0));
	EMIT(PPC_RAW_LWZ(_R0, _R1, bpf_jit_stack_offsetof(ctx, BPF_PPC_TC)));
	PPC_BCC(COND_GE, out);

	/*
	 * if (tail_call_cnt >= MAX_TAIL_CALL_CNT)
	 *   goto out;
	 */
	EMIT(PPC_RAW_CMPLWI(_R0, MAX_TAIL_CALL_CNT));
	/* tail_call_cnt++; */
	EMIT(PPC_RAW_ADDIC(_R0, _R0, 1));
	PPC_BCC(COND_GE, out);

	/* prog = array->ptrs[index]; */
	EMIT(PPC_RAW_RLWINM(_R3, b2p_index, 2, 0, 29));
	EMIT(PPC_RAW_ADD(_R3, _R3, b2p_bpf_array));
	EMIT(PPC_RAW_LWZ(_R3, _R3, offsetof(struct bpf_array, ptrs)));
	EMIT(PPC_RAW_STW(_R0, _R1, bpf_jit_stack_offsetof(ctx, BPF_PPC_TC)));

	/*
	 * if (prog == NULL)
	 *   goto out;
	 */
	EMIT(PPC_RAW_CMPLWI(_R3, 0));
	PPC_BCC(COND_EQ, out);

	/* goto *(prog->bpf_func + prologue_size); */
	EMIT(PPC_RAW_LWZ(_R3, _R3, offsetof(struct bpf_prog, bpf_func)));

	if (ctx->seen & SEEN_FUNC)
		EMIT(PPC_RAW_LWZ(_R0, _R1, BPF_PPC_STACKFRAME(ctx) + PPC_LR_STKOFF));

	EMIT(PPC_RAW_ADDIC(_R3, _R3, BPF_TAILCALL_PROLOGUE_SIZE));

	if (ctx->seen & SEEN_FUNC)
		EMIT(PPC_RAW_MTLR(_R0));

	EMIT(PPC_RAW_MTCTR(_R3));

	EMIT(PPC_RAW_MR(_R3, bpf_to_ppc(ctx, BPF_REG_1)));

	/* tear restore NVRs, ... */
	bpf_jit_emit_common_epilogue(image, ctx);

	EMIT(PPC_RAW_BCTR());

	/* out: */
	return 0;
}

/* Assemble the body code between the prologue & epilogue */
int bpf_jit_build_body(struct bpf_prog *fp, u32 *image, struct codegen_context *ctx,
		       u32 *addrs, int pass)
{
	const struct bpf_insn *insn = fp->insnsi;
	int flen = fp->len;
	int i, ret;

	/* Start of epilogue code - will only be valid 2nd pass onwards */
	u32 exit_addr = addrs[flen];

	for (i = 0; i < flen; i++) {
		u32 code = insn[i].code;
		u32 dst_reg = bpf_to_ppc(ctx, insn[i].dst_reg);
		u32 dst_reg_h = dst_reg - 1;
		u32 src_reg = bpf_to_ppc(ctx, insn[i].src_reg);
		u32 src_reg_h = src_reg - 1;
		u32 tmp_reg = bpf_to_ppc(ctx, TMP_REG);
		u32 size = BPF_SIZE(code);
		s16 off = insn[i].off;
		s32 imm = insn[i].imm;
		bool func_addr_fixed;
		u64 func_addr;
		u32 true_cond;
		u32 tmp_idx;
		int j;

		/*
		 * addrs[] maps a BPF bytecode address into a real offset from
		 * the start of the body code.
		 */
		addrs[i] = ctx->idx * 4;

		/*
		 * As an optimization, we note down which registers
		 * are used so that we can only save/restore those in our
		 * prologue and epilogue. We do this here regardless of whether
		 * the actual BPF instruction uses src/dst registers or not
		 * (for instance, BPF_CALL does not use them). The expectation
		 * is that those instructions will have src_reg/dst_reg set to
		 * 0. Even otherwise, we just lose some prologue/epilogue
		 * optimization but everything else should work without
		 * any issues.
		 */
		if (dst_reg >= 3 && dst_reg < 32) {
			bpf_set_seen_register(ctx, dst_reg);
			bpf_set_seen_register(ctx, dst_reg_h);
		}

		if (src_reg >= 3 && src_reg < 32) {
			bpf_set_seen_register(ctx, src_reg);
			bpf_set_seen_register(ctx, src_reg_h);
		}

		switch (code) {
		/*
		 * Arithmetic operations: ADD/SUB/MUL/DIV/MOD/NEG
		 */
		case BPF_ALU | BPF_ADD | BPF_X: /* (u32) dst += (u32) src */
			EMIT(PPC_RAW_ADD(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU64 | BPF_ADD | BPF_X: /* dst += src */
			EMIT(PPC_RAW_ADDC(dst_reg, dst_reg, src_reg));
			EMIT(PPC_RAW_ADDE(dst_reg_h, dst_reg_h, src_reg_h));
			break;
		case BPF_ALU | BPF_SUB | BPF_X: /* (u32) dst -= (u32) src */
			EMIT(PPC_RAW_SUB(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU64 | BPF_SUB | BPF_X: /* dst -= src */
			EMIT(PPC_RAW_SUBFC(dst_reg, src_reg, dst_reg));
			EMIT(PPC_RAW_SUBFE(dst_reg_h, src_reg_h, dst_reg_h));
			break;
		case BPF_ALU | BPF_SUB | BPF_K: /* (u32) dst -= (u32) imm */
			imm = -imm;
			fallthrough;
		case BPF_ALU | BPF_ADD | BPF_K: /* (u32) dst += (u32) imm */
			if (IMM_HA(imm) & 0xffff)
				EMIT(PPC_RAW_ADDIS(dst_reg, dst_reg, IMM_HA(imm)));
			if (IMM_L(imm))
				EMIT(PPC_RAW_ADDI(dst_reg, dst_reg, IMM_L(imm)));
			break;
		case BPF_ALU64 | BPF_SUB | BPF_K: /* dst -= imm */
			imm = -imm;
			fallthrough;
		case BPF_ALU64 | BPF_ADD | BPF_K: /* dst += imm */
			if (!imm)
				break;

			if (imm >= -32768 && imm < 32768) {
				EMIT(PPC_RAW_ADDIC(dst_reg, dst_reg, imm));
			} else {
				PPC_LI32(_R0, imm);
				EMIT(PPC_RAW_ADDC(dst_reg, dst_reg, _R0));
			}
			if (imm >= 0 || (BPF_OP(code) == BPF_SUB && imm == 0x80000000))
				EMIT(PPC_RAW_ADDZE(dst_reg_h, dst_reg_h));
			else
				EMIT(PPC_RAW_ADDME(dst_reg_h, dst_reg_h));
			break;
		case BPF_ALU64 | BPF_MUL | BPF_X: /* dst *= src */
			bpf_set_seen_register(ctx, tmp_reg);
			EMIT(PPC_RAW_MULW(_R0, dst_reg, src_reg_h));
			EMIT(PPC_RAW_MULW(dst_reg_h, dst_reg_h, src_reg));
			EMIT(PPC_RAW_MULHWU(tmp_reg, dst_reg, src_reg));
			EMIT(PPC_RAW_MULW(dst_reg, dst_reg, src_reg));
			EMIT(PPC_RAW_ADD(dst_reg_h, dst_reg_h, _R0));
			EMIT(PPC_RAW_ADD(dst_reg_h, dst_reg_h, tmp_reg));
			break;
		case BPF_ALU | BPF_MUL | BPF_X: /* (u32) dst *= (u32) src */
			EMIT(PPC_RAW_MULW(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU | BPF_MUL | BPF_K: /* (u32) dst *= (u32) imm */
			if (imm >= -32768 && imm < 32768) {
				EMIT(PPC_RAW_MULI(dst_reg, dst_reg, imm));
			} else {
				PPC_LI32(_R0, imm);
				EMIT(PPC_RAW_MULW(dst_reg, dst_reg, _R0));
			}
			break;
		case BPF_ALU64 | BPF_MUL | BPF_K: /* dst *= imm */
			if (!imm) {
				PPC_LI32(dst_reg, 0);
				PPC_LI32(dst_reg_h, 0);
				break;
			}
			if (imm == 1)
				break;
			if (imm == -1) {
				EMIT(PPC_RAW_SUBFIC(dst_reg, dst_reg, 0));
				EMIT(PPC_RAW_SUBFZE(dst_reg_h, dst_reg_h));
				break;
			}
			bpf_set_seen_register(ctx, tmp_reg);
			PPC_LI32(tmp_reg, imm);
			EMIT(PPC_RAW_MULW(dst_reg_h, dst_reg_h, tmp_reg));
			if (imm < 0)
				EMIT(PPC_RAW_SUB(dst_reg_h, dst_reg_h, dst_reg));
			EMIT(PPC_RAW_MULHWU(_R0, dst_reg, tmp_reg));
			EMIT(PPC_RAW_MULW(dst_reg, dst_reg, tmp_reg));
			EMIT(PPC_RAW_ADD(dst_reg_h, dst_reg_h, _R0));
			break;
		case BPF_ALU | BPF_DIV | BPF_X: /* (u32) dst /= (u32) src */
			EMIT(PPC_RAW_DIVWU(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU | BPF_MOD | BPF_X: /* (u32) dst %= (u32) src */
			EMIT(PPC_RAW_DIVWU(_R0, dst_reg, src_reg));
			EMIT(PPC_RAW_MULW(_R0, src_reg, _R0));
			EMIT(PPC_RAW_SUB(dst_reg, dst_reg, _R0));
			break;
		case BPF_ALU64 | BPF_DIV | BPF_X: /* dst /= src */
			return -EOPNOTSUPP;
		case BPF_ALU64 | BPF_MOD | BPF_X: /* dst %= src */
			return -EOPNOTSUPP;
		case BPF_ALU | BPF_DIV | BPF_K: /* (u32) dst /= (u32) imm */
			if (!imm)
				return -EINVAL;
			if (imm == 1)
				break;

			PPC_LI32(_R0, imm);
			EMIT(PPC_RAW_DIVWU(dst_reg, dst_reg, _R0));
			break;
		case BPF_ALU | BPF_MOD | BPF_K: /* (u32) dst %= (u32) imm */
			if (!imm)
				return -EINVAL;

			if (!is_power_of_2((u32)imm)) {
				bpf_set_seen_register(ctx, tmp_reg);
				PPC_LI32(tmp_reg, imm);
				EMIT(PPC_RAW_DIVWU(_R0, dst_reg, tmp_reg));
				EMIT(PPC_RAW_MULW(_R0, tmp_reg, _R0));
				EMIT(PPC_RAW_SUB(dst_reg, dst_reg, _R0));
				break;
			}
			if (imm == 1)
				EMIT(PPC_RAW_LI(dst_reg, 0));
			else
				EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, 0, 32 - ilog2((u32)imm), 31));

			break;
		case BPF_ALU64 | BPF_MOD | BPF_K: /* dst %= imm */
			if (!imm)
				return -EINVAL;
			if (imm < 0)
				imm = -imm;
			if (!is_power_of_2(imm))
				return -EOPNOTSUPP;
			if (imm == 1)
				EMIT(PPC_RAW_LI(dst_reg, 0));
			else
				EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, 0, 32 - ilog2(imm), 31));
			EMIT(PPC_RAW_LI(dst_reg_h, 0));
			break;
		case BPF_ALU64 | BPF_DIV | BPF_K: /* dst /= imm */
			if (!imm)
				return -EINVAL;
			if (!is_power_of_2(abs(imm)))
				return -EOPNOTSUPP;

			if (imm < 0) {
				EMIT(PPC_RAW_SUBFIC(dst_reg, dst_reg, 0));
				EMIT(PPC_RAW_SUBFZE(dst_reg_h, dst_reg_h));
				imm = -imm;
			}
			if (imm == 1)
				break;
			imm = ilog2(imm);
			EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, 32 - imm, imm, 31));
			EMIT(PPC_RAW_RLWIMI(dst_reg, dst_reg_h, 32 - imm, 0, imm - 1));
			EMIT(PPC_RAW_SRAWI(dst_reg_h, dst_reg_h, imm));
			break;
		case BPF_ALU | BPF_NEG: /* (u32) dst = -dst */
			EMIT(PPC_RAW_NEG(dst_reg, dst_reg));
			break;
		case BPF_ALU64 | BPF_NEG: /* dst = -dst */
			EMIT(PPC_RAW_SUBFIC(dst_reg, dst_reg, 0));
			EMIT(PPC_RAW_SUBFZE(dst_reg_h, dst_reg_h));
			break;

		/*
		 * Logical operations: AND/OR/XOR/[A]LSH/[A]RSH
		 */
		case BPF_ALU64 | BPF_AND | BPF_X: /* dst = dst & src */
			EMIT(PPC_RAW_AND(dst_reg, dst_reg, src_reg));
			EMIT(PPC_RAW_AND(dst_reg_h, dst_reg_h, src_reg_h));
			break;
		case BPF_ALU | BPF_AND | BPF_X: /* (u32) dst = dst & src */
			EMIT(PPC_RAW_AND(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU64 | BPF_AND | BPF_K: /* dst = dst & imm */
			if (imm >= 0)
				EMIT(PPC_RAW_LI(dst_reg_h, 0));
			fallthrough;
		case BPF_ALU | BPF_AND | BPF_K: /* (u32) dst = dst & imm */
			if (!IMM_H(imm)) {
				EMIT(PPC_RAW_ANDI(dst_reg, dst_reg, IMM_L(imm)));
			} else if (!IMM_L(imm)) {
				EMIT(PPC_RAW_ANDIS(dst_reg, dst_reg, IMM_H(imm)));
			} else if (imm == (((1 << fls(imm)) - 1) ^ ((1 << (ffs(i) - 1)) - 1))) {
				EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, 0,
						    32 - fls(imm), 32 - ffs(imm)));
			} else {
				PPC_LI32(_R0, imm);
				EMIT(PPC_RAW_AND(dst_reg, dst_reg, _R0));
			}
			break;
		case BPF_ALU64 | BPF_OR | BPF_X: /* dst = dst | src */
			EMIT(PPC_RAW_OR(dst_reg, dst_reg, src_reg));
			EMIT(PPC_RAW_OR(dst_reg_h, dst_reg_h, src_reg_h));
			break;
		case BPF_ALU | BPF_OR | BPF_X: /* dst = (u32) dst | (u32) src */
			EMIT(PPC_RAW_OR(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU64 | BPF_OR | BPF_K:/* dst = dst | imm */
			/* Sign-extended */
			if (imm < 0)
				EMIT(PPC_RAW_LI(dst_reg_h, -1));
			fallthrough;
		case BPF_ALU | BPF_OR | BPF_K:/* dst = (u32) dst | (u32) imm */
			if (IMM_L(imm))
				EMIT(PPC_RAW_ORI(dst_reg, dst_reg, IMM_L(imm)));
			if (IMM_H(imm))
				EMIT(PPC_RAW_ORIS(dst_reg, dst_reg, IMM_H(imm)));
			break;
		case BPF_ALU64 | BPF_XOR | BPF_X: /* dst ^= src */
			if (dst_reg == src_reg) {
				EMIT(PPC_RAW_LI(dst_reg, 0));
				EMIT(PPC_RAW_LI(dst_reg_h, 0));
			} else {
				EMIT(PPC_RAW_XOR(dst_reg, dst_reg, src_reg));
				EMIT(PPC_RAW_XOR(dst_reg_h, dst_reg_h, src_reg_h));
			}
			break;
		case BPF_ALU | BPF_XOR | BPF_X: /* (u32) dst ^= src */
			if (dst_reg == src_reg)
				EMIT(PPC_RAW_LI(dst_reg, 0));
			else
				EMIT(PPC_RAW_XOR(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU64 | BPF_XOR | BPF_K: /* dst ^= imm */
			if (imm < 0)
				EMIT(PPC_RAW_NOR(dst_reg_h, dst_reg_h, dst_reg_h));
			fallthrough;
		case BPF_ALU | BPF_XOR | BPF_K: /* (u32) dst ^= (u32) imm */
			if (IMM_L(imm))
				EMIT(PPC_RAW_XORI(dst_reg, dst_reg, IMM_L(imm)));
			if (IMM_H(imm))
				EMIT(PPC_RAW_XORIS(dst_reg, dst_reg, IMM_H(imm)));
			break;
		case BPF_ALU | BPF_LSH | BPF_X: /* (u32) dst <<= (u32) src */
			EMIT(PPC_RAW_SLW(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU64 | BPF_LSH | BPF_X: /* dst <<= src; */
			bpf_set_seen_register(ctx, tmp_reg);
			EMIT(PPC_RAW_SUBFIC(_R0, src_reg, 32));
			EMIT(PPC_RAW_SLW(dst_reg_h, dst_reg_h, src_reg));
			EMIT(PPC_RAW_ADDI(tmp_reg, src_reg, 32));
			EMIT(PPC_RAW_SRW(_R0, dst_reg, _R0));
			EMIT(PPC_RAW_SLW(tmp_reg, dst_reg, tmp_reg));
			EMIT(PPC_RAW_OR(dst_reg_h, dst_reg_h, _R0));
			EMIT(PPC_RAW_SLW(dst_reg, dst_reg, src_reg));
			EMIT(PPC_RAW_OR(dst_reg_h, dst_reg_h, tmp_reg));
			break;
		case BPF_ALU | BPF_LSH | BPF_K: /* (u32) dst <<= (u32) imm */
			if (!imm)
				break;
			EMIT(PPC_RAW_SLWI(dst_reg, dst_reg, imm));
			break;
		case BPF_ALU64 | BPF_LSH | BPF_K: /* dst <<= imm */
			if (imm < 0)
				return -EINVAL;
			if (!imm)
				break;
			if (imm < 32) {
				EMIT(PPC_RAW_RLWINM(dst_reg_h, dst_reg_h, imm, 0, 31 - imm));
				EMIT(PPC_RAW_RLWIMI(dst_reg_h, dst_reg, imm, 32 - imm, 31));
				EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, imm, 0, 31 - imm));
				break;
			}
			if (imm < 64)
				EMIT(PPC_RAW_RLWINM(dst_reg_h, dst_reg, imm, 0, 31 - imm));
			else
				EMIT(PPC_RAW_LI(dst_reg_h, 0));
			EMIT(PPC_RAW_LI(dst_reg, 0));
			break;
		case BPF_ALU | BPF_RSH | BPF_X: /* (u32) dst >>= (u32) src */
			EMIT(PPC_RAW_SRW(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU64 | BPF_RSH | BPF_X: /* dst >>= src */
			bpf_set_seen_register(ctx, tmp_reg);
			EMIT(PPC_RAW_SUBFIC(_R0, src_reg, 32));
			EMIT(PPC_RAW_SRW(dst_reg, dst_reg, src_reg));
			EMIT(PPC_RAW_ADDI(tmp_reg, src_reg, 32));
			EMIT(PPC_RAW_SLW(_R0, dst_reg_h, _R0));
			EMIT(PPC_RAW_SRW(tmp_reg, dst_reg_h, tmp_reg));
			EMIT(PPC_RAW_OR(dst_reg, dst_reg, _R0));
			EMIT(PPC_RAW_SRW(dst_reg_h, dst_reg_h, src_reg));
			EMIT(PPC_RAW_OR(dst_reg, dst_reg, tmp_reg));
			break;
		case BPF_ALU | BPF_RSH | BPF_K: /* (u32) dst >>= (u32) imm */
			if (!imm)
				break;
			EMIT(PPC_RAW_SRWI(dst_reg, dst_reg, imm));
			break;
		case BPF_ALU64 | BPF_RSH | BPF_K: /* dst >>= imm */
			if (imm < 0)
				return -EINVAL;
			if (!imm)
				break;
			if (imm < 32) {
				EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, 32 - imm, imm, 31));
				EMIT(PPC_RAW_RLWIMI(dst_reg, dst_reg_h, 32 - imm, 0, imm - 1));
				EMIT(PPC_RAW_RLWINM(dst_reg_h, dst_reg_h, 32 - imm, imm, 31));
				break;
			}
			if (imm < 64)
				EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg_h, 64 - imm, imm - 32, 31));
			else
				EMIT(PPC_RAW_LI(dst_reg, 0));
			EMIT(PPC_RAW_LI(dst_reg_h, 0));
			break;
		case BPF_ALU | BPF_ARSH | BPF_X: /* (s32) dst >>= src */
			EMIT(PPC_RAW_SRAW(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU64 | BPF_ARSH | BPF_X: /* (s64) dst >>= src */
			bpf_set_seen_register(ctx, tmp_reg);
			EMIT(PPC_RAW_SUBFIC(_R0, src_reg, 32));
			EMIT(PPC_RAW_SRW(dst_reg, dst_reg, src_reg));
			EMIT(PPC_RAW_SLW(_R0, dst_reg_h, _R0));
			EMIT(PPC_RAW_ADDI(tmp_reg, src_reg, 32));
			EMIT(PPC_RAW_OR(dst_reg, dst_reg, _R0));
			EMIT(PPC_RAW_RLWINM(_R0, tmp_reg, 0, 26, 26));
			EMIT(PPC_RAW_SRAW(tmp_reg, dst_reg_h, tmp_reg));
			EMIT(PPC_RAW_SRAW(dst_reg_h, dst_reg_h, src_reg));
			EMIT(PPC_RAW_SLW(tmp_reg, tmp_reg, _R0));
			EMIT(PPC_RAW_OR(dst_reg, dst_reg, tmp_reg));
			break;
		case BPF_ALU | BPF_ARSH | BPF_K: /* (s32) dst >>= imm */
			if (!imm)
				break;
			EMIT(PPC_RAW_SRAWI(dst_reg, dst_reg, imm));
			break;
		case BPF_ALU64 | BPF_ARSH | BPF_K: /* (s64) dst >>= imm */
			if (imm < 0)
				return -EINVAL;
			if (!imm)
				break;
			if (imm < 32) {
				EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, 32 - imm, imm, 31));
				EMIT(PPC_RAW_RLWIMI(dst_reg, dst_reg_h, 32 - imm, 0, imm - 1));
				EMIT(PPC_RAW_SRAWI(dst_reg_h, dst_reg_h, imm));
				break;
			}
			if (imm < 64)
				EMIT(PPC_RAW_SRAWI(dst_reg, dst_reg_h, imm - 32));
			else
				EMIT(PPC_RAW_SRAWI(dst_reg, dst_reg_h, 31));
			EMIT(PPC_RAW_SRAWI(dst_reg_h, dst_reg_h, 31));
			break;

		/*
		 * MOV
		 */
		case BPF_ALU64 | BPF_MOV | BPF_X: /* dst = src */
			if (dst_reg == src_reg)
				break;
			EMIT(PPC_RAW_MR(dst_reg, src_reg));
			EMIT(PPC_RAW_MR(dst_reg_h, src_reg_h));
			break;
		case BPF_ALU | BPF_MOV | BPF_X: /* (u32) dst = src */
			/* special mov32 for zext */
			if (imm == 1)
				EMIT(PPC_RAW_LI(dst_reg_h, 0));
			else if (dst_reg != src_reg)
				EMIT(PPC_RAW_MR(dst_reg, src_reg));
			break;
		case BPF_ALU64 | BPF_MOV | BPF_K: /* dst = (s64) imm */
			PPC_LI32(dst_reg, imm);
			PPC_EX32(dst_reg_h, imm);
			break;
		case BPF_ALU | BPF_MOV | BPF_K: /* (u32) dst = imm */
			PPC_LI32(dst_reg, imm);
			break;

		/*
		 * BPF_FROM_BE/LE
		 */
		case BPF_ALU | BPF_END | BPF_FROM_LE:
			switch (imm) {
			case 16:
				/* Copy 16 bits to upper part */
				EMIT(PPC_RAW_RLWIMI(dst_reg, dst_reg, 16, 0, 15));
				/* Rotate 8 bits right & mask */
				EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, 24, 16, 31));
				break;
			case 32:
				/*
				 * Rotate word left by 8 bits:
				 * 2 bytes are already in their final position
				 * -- byte 2 and 4 (of bytes 1, 2, 3 and 4)
				 */
				EMIT(PPC_RAW_RLWINM(_R0, dst_reg, 8, 0, 31));
				/* Rotate 24 bits and insert byte 1 */
				EMIT(PPC_RAW_RLWIMI(_R0, dst_reg, 24, 0, 7));
				/* Rotate 24 bits and insert byte 3 */
				EMIT(PPC_RAW_RLWIMI(_R0, dst_reg, 24, 16, 23));
				EMIT(PPC_RAW_MR(dst_reg, _R0));
				break;
			case 64:
				bpf_set_seen_register(ctx, tmp_reg);
				EMIT(PPC_RAW_RLWINM(tmp_reg, dst_reg, 8, 0, 31));
				EMIT(PPC_RAW_RLWINM(_R0, dst_reg_h, 8, 0, 31));
				/* Rotate 24 bits and insert byte 1 */
				EMIT(PPC_RAW_RLWIMI(tmp_reg, dst_reg, 24, 0, 7));
				EMIT(PPC_RAW_RLWIMI(_R0, dst_reg_h, 24, 0, 7));
				/* Rotate 24 bits and insert byte 3 */
				EMIT(PPC_RAW_RLWIMI(tmp_reg, dst_reg, 24, 16, 23));
				EMIT(PPC_RAW_RLWIMI(_R0, dst_reg_h, 24, 16, 23));
				EMIT(PPC_RAW_MR(dst_reg, _R0));
				EMIT(PPC_RAW_MR(dst_reg_h, tmp_reg));
				break;
			}
			break;
		case BPF_ALU | BPF_END | BPF_FROM_BE:
			switch (imm) {
			case 16:
				/* zero-extend 16 bits into 32 bits */
				EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, 0, 16, 31));
				break;
			case 32:
			case 64:
				/* nop */
				break;
			}
			break;

		/*
		 * BPF_ST NOSPEC (speculation barrier)
		 */
		case BPF_ST | BPF_NOSPEC:
			break;

		/*
		 * BPF_ST(X)
		 */
		case BPF_STX | BPF_MEM | BPF_B: /* *(u8 *)(dst + off) = src */
			EMIT(PPC_RAW_STB(src_reg, dst_reg, off));
			break;
		case BPF_ST | BPF_MEM | BPF_B: /* *(u8 *)(dst + off) = imm */
			PPC_LI32(_R0, imm);
			EMIT(PPC_RAW_STB(_R0, dst_reg, off));
			break;
		case BPF_STX | BPF_MEM | BPF_H: /* (u16 *)(dst + off) = src */
			EMIT(PPC_RAW_STH(src_reg, dst_reg, off));
			break;
		case BPF_ST | BPF_MEM | BPF_H: /* (u16 *)(dst + off) = imm */
			PPC_LI32(_R0, imm);
			EMIT(PPC_RAW_STH(_R0, dst_reg, off));
			break;
		case BPF_STX | BPF_MEM | BPF_W: /* *(u32 *)(dst + off) = src */
			EMIT(PPC_RAW_STW(src_reg, dst_reg, off));
			break;
		case BPF_ST | BPF_MEM | BPF_W: /* *(u32 *)(dst + off) = imm */
			PPC_LI32(_R0, imm);
			EMIT(PPC_RAW_STW(_R0, dst_reg, off));
			break;
		case BPF_STX | BPF_MEM | BPF_DW: /* (u64 *)(dst + off) = src */
			EMIT(PPC_RAW_STW(src_reg_h, dst_reg, off));
			EMIT(PPC_RAW_STW(src_reg, dst_reg, off + 4));
			break;
		case BPF_ST | BPF_MEM | BPF_DW: /* *(u64 *)(dst + off) = imm */
			PPC_LI32(_R0, imm);
			EMIT(PPC_RAW_STW(_R0, dst_reg, off + 4));
			PPC_EX32(_R0, imm);
			EMIT(PPC_RAW_STW(_R0, dst_reg, off));
			break;

		/*
		 * BPF_STX ATOMIC (atomic ops)
		 */
		case BPF_STX | BPF_ATOMIC | BPF_W:
			if (imm != BPF_ADD) {
				pr_err_ratelimited("eBPF filter atomic op code %02x (@%d) unsupported\n",
						   code, i);
				return -ENOTSUPP;
			}

			/* *(u32 *)(dst + off) += src */

			bpf_set_seen_register(ctx, tmp_reg);
			/* Get offset into TMP_REG */
			EMIT(PPC_RAW_LI(tmp_reg, off));
			/* load value from memory into r0 */
			EMIT(PPC_RAW_LWARX(_R0, tmp_reg, dst_reg, 0));
			/* add value from src_reg into this */
			EMIT(PPC_RAW_ADD(_R0, _R0, src_reg));
			/* store result back */
			EMIT(PPC_RAW_STWCX(_R0, tmp_reg, dst_reg));
			/* we're done if this succeeded */
			PPC_BCC_SHORT(COND_NE, (ctx->idx - 3) * 4);
			break;

		case BPF_STX | BPF_ATOMIC | BPF_DW: /* *(u64 *)(dst + off) += src */
			return -EOPNOTSUPP;

		/*
		 * BPF_LDX
		 */
		case BPF_LDX | BPF_MEM | BPF_B: /* dst = *(u8 *)(ul) (src + off) */
		case BPF_LDX | BPF_PROBE_MEM | BPF_B:
		case BPF_LDX | BPF_MEM | BPF_H: /* dst = *(u16 *)(ul) (src + off) */
		case BPF_LDX | BPF_PROBE_MEM | BPF_H:
		case BPF_LDX | BPF_MEM | BPF_W: /* dst = *(u32 *)(ul) (src + off) */
		case BPF_LDX | BPF_PROBE_MEM | BPF_W:
		case BPF_LDX | BPF_MEM | BPF_DW: /* dst = *(u64 *)(ul) (src + off) */
		case BPF_LDX | BPF_PROBE_MEM | BPF_DW:
			/*
			 * As PTR_TO_BTF_ID that uses BPF_PROBE_MEM mode could either be a valid
			 * kernel pointer or NULL but not a userspace address, execute BPF_PROBE_MEM
			 * load only if addr is kernel address (see is_kernel_addr()), otherwise
			 * set dst_reg=0 and move on.
			 */
			if (BPF_MODE(code) == BPF_PROBE_MEM) {
				PPC_LI32(_R0, TASK_SIZE - off);
				EMIT(PPC_RAW_CMPLW(src_reg, _R0));
				PPC_BCC(COND_GT, (ctx->idx + 5) * 4);
				EMIT(PPC_RAW_LI(dst_reg, 0));
				/*
				 * For BPF_DW case, "li reg_h,0" would be needed when
				 * !fp->aux->verifier_zext. Emit NOP otherwise.
				 *
				 * Note that "li reg_h,0" is emitted for BPF_B/H/W case,
				 * if necessary. So, jump there insted of emitting an
				 * additional "li reg_h,0" instruction.
				 */
				if (size == BPF_DW && !fp->aux->verifier_zext)
					EMIT(PPC_RAW_LI(dst_reg_h, 0));
				else
					EMIT(PPC_RAW_NOP());
				/*
				 * Need to jump two instructions instead of one for BPF_DW case
				 * as there are two load instructions for dst_reg_h & dst_reg
				 * respectively.
				 */
				if (size == BPF_DW)
					PPC_JMP((ctx->idx + 3) * 4);
				else
					PPC_JMP((ctx->idx + 2) * 4);
			}

			switch (size) {
			case BPF_B:
				EMIT(PPC_RAW_LBZ(dst_reg, src_reg, off));
				break;
			case BPF_H:
				EMIT(PPC_RAW_LHZ(dst_reg, src_reg, off));
				break;
			case BPF_W:
				EMIT(PPC_RAW_LWZ(dst_reg, src_reg, off));
				break;
			case BPF_DW:
				EMIT(PPC_RAW_LWZ(dst_reg_h, src_reg, off));
				EMIT(PPC_RAW_LWZ(dst_reg, src_reg, off + 4));
				break;
			}

			if (size != BPF_DW && !fp->aux->verifier_zext)
				EMIT(PPC_RAW_LI(dst_reg_h, 0));

			if (BPF_MODE(code) == BPF_PROBE_MEM) {
				int insn_idx = ctx->idx - 1;
				int jmp_off = 4;

				/*
				 * In case of BPF_DW, two lwz instructions are emitted, one
				 * for higher 32-bit and another for lower 32-bit. So, set
				 * ex->insn to the first of the two and jump over both
				 * instructions in fixup.
				 *
				 * Similarly, with !verifier_zext, two instructions are
				 * emitted for BPF_B/H/W case. So, set ex->insn to the
				 * instruction that could fault and skip over both
				 * instructions.
				 */
				if (size == BPF_DW || !fp->aux->verifier_zext) {
					insn_idx -= 1;
					jmp_off += 4;
				}

				ret = bpf_add_extable_entry(fp, image, pass, ctx, insn_idx,
							    jmp_off, dst_reg);
				if (ret)
					return ret;
			}
			break;

		/*
		 * Doubleword load
		 * 16 byte instruction that uses two 'struct bpf_insn'
		 */
		case BPF_LD | BPF_IMM | BPF_DW: /* dst = (u64) imm */
			tmp_idx = ctx->idx;
			PPC_LI32(dst_reg_h, (u32)insn[i + 1].imm);
			PPC_LI32(dst_reg, (u32)insn[i].imm);
			/* padding to allow full 4 instructions for later patching */
			for (j = ctx->idx - tmp_idx; j < 4; j++)
				EMIT(PPC_RAW_NOP());
			/* Adjust for two bpf instructions */
			addrs[++i] = ctx->idx * 4;
			break;

		/*
		 * Return/Exit
		 */
		case BPF_JMP | BPF_EXIT:
			/*
			 * If this isn't the very last instruction, branch to
			 * the epilogue. If we _are_ the last instruction,
			 * we'll just fall through to the epilogue.
			 */
			if (i != flen - 1)
				PPC_JMP(exit_addr);
			/* else fall through to the epilogue */
			break;

		/*
		 * Call kernel helper or bpf function
		 */
		case BPF_JMP | BPF_CALL:
			ctx->seen |= SEEN_FUNC;

			ret = bpf_jit_get_func_addr(fp, &insn[i], false,
						    &func_addr, &func_addr_fixed);
			if (ret < 0)
				return ret;

			if (bpf_is_seen_register(ctx, bpf_to_ppc(ctx, BPF_REG_5))) {
				EMIT(PPC_RAW_STW(bpf_to_ppc(ctx, BPF_REG_5) - 1, _R1, 8));
				EMIT(PPC_RAW_STW(bpf_to_ppc(ctx, BPF_REG_5), _R1, 12));
			}

			bpf_jit_emit_func_call_rel(image, ctx, func_addr);

			EMIT(PPC_RAW_MR(bpf_to_ppc(ctx, BPF_REG_0) - 1, _R3));
			EMIT(PPC_RAW_MR(bpf_to_ppc(ctx, BPF_REG_0), _R4));
			break;

		/*
		 * Jumps and branches
		 */
		case BPF_JMP | BPF_JA:
			PPC_JMP(addrs[i + 1 + off]);
			break;

		case BPF_JMP | BPF_JGT | BPF_K:
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JSGT | BPF_K:
		case BPF_JMP | BPF_JSGT | BPF_X:
		case BPF_JMP32 | BPF_JGT | BPF_K:
		case BPF_JMP32 | BPF_JGT | BPF_X:
		case BPF_JMP32 | BPF_JSGT | BPF_K:
		case BPF_JMP32 | BPF_JSGT | BPF_X:
			true_cond = COND_GT;
			goto cond_branch;
		case BPF_JMP | BPF_JLT | BPF_K:
		case BPF_JMP | BPF_JLT | BPF_X:
		case BPF_JMP | BPF_JSLT | BPF_K:
		case BPF_JMP | BPF_JSLT | BPF_X:
		case BPF_JMP32 | BPF_JLT | BPF_K:
		case BPF_JMP32 | BPF_JLT | BPF_X:
		case BPF_JMP32 | BPF_JSLT | BPF_K:
		case BPF_JMP32 | BPF_JSLT | BPF_X:
			true_cond = COND_LT;
			goto cond_branch;
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_X:
		case BPF_JMP | BPF_JSGE | BPF_K:
		case BPF_JMP | BPF_JSGE | BPF_X:
		case BPF_JMP32 | BPF_JGE | BPF_K:
		case BPF_JMP32 | BPF_JGE | BPF_X:
		case BPF_JMP32 | BPF_JSGE | BPF_K:
		case BPF_JMP32 | BPF_JSGE | BPF_X:
			true_cond = COND_GE;
			goto cond_branch;
		case BPF_JMP | BPF_JLE | BPF_K:
		case BPF_JMP | BPF_JLE | BPF_X:
		case BPF_JMP | BPF_JSLE | BPF_K:
		case BPF_JMP | BPF_JSLE | BPF_X:
		case BPF_JMP32 | BPF_JLE | BPF_K:
		case BPF_JMP32 | BPF_JLE | BPF_X:
		case BPF_JMP32 | BPF_JSLE | BPF_K:
		case BPF_JMP32 | BPF_JSLE | BPF_X:
			true_cond = COND_LE;
			goto cond_branch;
		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP32 | BPF_JEQ | BPF_K:
		case BPF_JMP32 | BPF_JEQ | BPF_X:
			true_cond = COND_EQ;
			goto cond_branch;
		case BPF_JMP | BPF_JNE | BPF_K:
		case BPF_JMP | BPF_JNE | BPF_X:
		case BPF_JMP32 | BPF_JNE | BPF_K:
		case BPF_JMP32 | BPF_JNE | BPF_X:
			true_cond = COND_NE;
			goto cond_branch;
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP | BPF_JSET | BPF_X:
		case BPF_JMP32 | BPF_JSET | BPF_K:
		case BPF_JMP32 | BPF_JSET | BPF_X:
			true_cond = COND_NE;
			/* fallthrough; */

cond_branch:
			switch (code) {
			case BPF_JMP | BPF_JGT | BPF_X:
			case BPF_JMP | BPF_JLT | BPF_X:
			case BPF_JMP | BPF_JGE | BPF_X:
			case BPF_JMP | BPF_JLE | BPF_X:
			case BPF_JMP | BPF_JEQ | BPF_X:
			case BPF_JMP | BPF_JNE | BPF_X:
				/* unsigned comparison */
				EMIT(PPC_RAW_CMPLW(dst_reg_h, src_reg_h));
				PPC_BCC_SHORT(COND_NE, (ctx->idx + 2) * 4);
				EMIT(PPC_RAW_CMPLW(dst_reg, src_reg));
				break;
			case BPF_JMP32 | BPF_JGT | BPF_X:
			case BPF_JMP32 | BPF_JLT | BPF_X:
			case BPF_JMP32 | BPF_JGE | BPF_X:
			case BPF_JMP32 | BPF_JLE | BPF_X:
			case BPF_JMP32 | BPF_JEQ | BPF_X:
			case BPF_JMP32 | BPF_JNE | BPF_X:
				/* unsigned comparison */
				EMIT(PPC_RAW_CMPLW(dst_reg, src_reg));
				break;
			case BPF_JMP | BPF_JSGT | BPF_X:
			case BPF_JMP | BPF_JSLT | BPF_X:
			case BPF_JMP | BPF_JSGE | BPF_X:
			case BPF_JMP | BPF_JSLE | BPF_X:
				/* signed comparison */
				EMIT(PPC_RAW_CMPW(dst_reg_h, src_reg_h));
				PPC_BCC_SHORT(COND_NE, (ctx->idx + 2) * 4);
				EMIT(PPC_RAW_CMPLW(dst_reg, src_reg));
				break;
			case BPF_JMP32 | BPF_JSGT | BPF_X:
			case BPF_JMP32 | BPF_JSLT | BPF_X:
			case BPF_JMP32 | BPF_JSGE | BPF_X:
			case BPF_JMP32 | BPF_JSLE | BPF_X:
				/* signed comparison */
				EMIT(PPC_RAW_CMPW(dst_reg, src_reg));
				break;
			case BPF_JMP | BPF_JSET | BPF_X:
				EMIT(PPC_RAW_AND_DOT(_R0, dst_reg_h, src_reg_h));
				PPC_BCC_SHORT(COND_NE, (ctx->idx + 2) * 4);
				EMIT(PPC_RAW_AND_DOT(_R0, dst_reg, src_reg));
				break;
			case BPF_JMP32 | BPF_JSET | BPF_X: {
				EMIT(PPC_RAW_AND_DOT(_R0, dst_reg, src_reg));
				break;
			case BPF_JMP | BPF_JNE | BPF_K:
			case BPF_JMP | BPF_JEQ | BPF_K:
			case BPF_JMP | BPF_JGT | BPF_K:
			case BPF_JMP | BPF_JLT | BPF_K:
			case BPF_JMP | BPF_JGE | BPF_K:
			case BPF_JMP | BPF_JLE | BPF_K:
				/*
				 * Need sign-extended load, so only positive
				 * values can be used as imm in cmplwi
				 */
				if (imm >= 0 && imm < 32768) {
					EMIT(PPC_RAW_CMPLWI(dst_reg_h, 0));
					PPC_BCC_SHORT(COND_NE, (ctx->idx + 2) * 4);
					EMIT(PPC_RAW_CMPLWI(dst_reg, imm));
				} else {
					/* sign-extending load ... but unsigned comparison */
					PPC_EX32(_R0, imm);
					EMIT(PPC_RAW_CMPLW(dst_reg_h, _R0));
					PPC_LI32(_R0, imm);
					PPC_BCC_SHORT(COND_NE, (ctx->idx + 2) * 4);
					EMIT(PPC_RAW_CMPLW(dst_reg, _R0));
				}
				break;
			case BPF_JMP32 | BPF_JNE | BPF_K:
			case BPF_JMP32 | BPF_JEQ | BPF_K:
			case BPF_JMP32 | BPF_JGT | BPF_K:
			case BPF_JMP32 | BPF_JLT | BPF_K:
			case BPF_JMP32 | BPF_JGE | BPF_K:
			case BPF_JMP32 | BPF_JLE | BPF_K:
				if (imm >= 0 && imm < 65536) {
					EMIT(PPC_RAW_CMPLWI(dst_reg, imm));
				} else {
					PPC_LI32(_R0, imm);
					EMIT(PPC_RAW_CMPLW(dst_reg, _R0));
				}
				break;
			}
			case BPF_JMP | BPF_JSGT | BPF_K:
			case BPF_JMP | BPF_JSLT | BPF_K:
			case BPF_JMP | BPF_JSGE | BPF_K:
			case BPF_JMP | BPF_JSLE | BPF_K:
				if (imm >= 0 && imm < 65536) {
					EMIT(PPC_RAW_CMPWI(dst_reg_h, imm < 0 ? -1 : 0));
					PPC_BCC_SHORT(COND_NE, (ctx->idx + 2) * 4);
					EMIT(PPC_RAW_CMPLWI(dst_reg, imm));
				} else {
					/* sign-extending load */
					EMIT(PPC_RAW_CMPWI(dst_reg_h, imm < 0 ? -1 : 0));
					PPC_LI32(_R0, imm);
					PPC_BCC_SHORT(COND_NE, (ctx->idx + 2) * 4);
					EMIT(PPC_RAW_CMPLW(dst_reg, _R0));
				}
				break;
			case BPF_JMP32 | BPF_JSGT | BPF_K:
			case BPF_JMP32 | BPF_JSLT | BPF_K:
			case BPF_JMP32 | BPF_JSGE | BPF_K:
			case BPF_JMP32 | BPF_JSLE | BPF_K:
				/*
				 * signed comparison, so any 16-bit value
				 * can be used in cmpwi
				 */
				if (imm >= -32768 && imm < 32768) {
					EMIT(PPC_RAW_CMPWI(dst_reg, imm));
				} else {
					/* sign-extending load */
					PPC_LI32(_R0, imm);
					EMIT(PPC_RAW_CMPW(dst_reg, _R0));
				}
				break;
			case BPF_JMP | BPF_JSET | BPF_K:
				/* andi does not sign-extend the immediate */
				if (imm >= 0 && imm < 32768) {
					/* PPC_ANDI is _only/always_ dot-form */
					EMIT(PPC_RAW_ANDI(_R0, dst_reg, imm));
				} else {
					PPC_LI32(_R0, imm);
					if (imm < 0) {
						EMIT(PPC_RAW_CMPWI(dst_reg_h, 0));
						PPC_BCC_SHORT(COND_NE, (ctx->idx + 2) * 4);
					}
					EMIT(PPC_RAW_AND_DOT(_R0, dst_reg, _R0));
				}
				break;
			case BPF_JMP32 | BPF_JSET | BPF_K:
				/* andi does not sign-extend the immediate */
				if (imm >= 0 && imm < 32768) {
					/* PPC_ANDI is _only/always_ dot-form */
					EMIT(PPC_RAW_ANDI(_R0, dst_reg, imm));
				} else {
					PPC_LI32(_R0, imm);
					EMIT(PPC_RAW_AND_DOT(_R0, dst_reg, _R0));
				}
				break;
			}
			PPC_BCC(true_cond, addrs[i + 1 + off]);
			break;

		/*
		 * Tail call
		 */
		case BPF_JMP | BPF_TAIL_CALL:
			ctx->seen |= SEEN_TAILCALL;
			ret = bpf_jit_emit_tail_call(image, ctx, addrs[i + 1]);
			if (ret < 0)
				return ret;
			break;

		default:
			/*
			 * The filter contains something cruel & unusual.
			 * We don't handle it, but also there shouldn't be
			 * anything missing from our list.
			 */
			pr_err_ratelimited("eBPF filter opcode %04x (@%d) unsupported\n", code, i);
			return -EOPNOTSUPP;
		}
		if (BPF_CLASS(code) == BPF_ALU && !fp->aux->verifier_zext &&
		    !insn_is_zext(&insn[i + 1]) && !(BPF_OP(code) == BPF_END && imm == 64))
			EMIT(PPC_RAW_LI(dst_reg_h, 0));
	}

	/* Set end-of-body-code address for exit. */
	addrs[i] = ctx->idx * 4;

	return 0;
}
