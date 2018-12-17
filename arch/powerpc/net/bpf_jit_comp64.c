/*
 * bpf_jit_comp64.c: eBPF JIT compiler
 *
 * Copyright 2016 Naveen N. Rao <naveen.n.rao@linux.vnet.ibm.com>
 *		  IBM Corporation
 *
 * Based on the powerpc classic BPF JIT compiler by Matt Evans
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#include <linux/moduleloader.h>
#include <asm/cacheflush.h>
#include <asm/asm-compat.h>
#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/if_vlan.h>
#include <asm/kprobes.h>
#include <linux/bpf.h>

#include "bpf_jit64.h"

static void bpf_jit_fill_ill_insns(void *area, unsigned int size)
{
	memset32(area, BREAKPOINT_INSTRUCTION, size/4);
}

static inline void bpf_flush_icache(void *start, void *end)
{
	smp_wmb();
	flush_icache_range((unsigned long)start, (unsigned long)end);
}

static inline bool bpf_is_seen_register(struct codegen_context *ctx, int i)
{
	return (ctx->seen & (1 << (31 - b2p[i])));
}

static inline void bpf_set_seen_register(struct codegen_context *ctx, int i)
{
	ctx->seen |= (1 << (31 - b2p[i]));
}

static inline bool bpf_has_stack_frame(struct codegen_context *ctx)
{
	/*
	 * We only need a stack frame if:
	 * - we call other functions (kernel helpers), or
	 * - the bpf program uses its stack area
	 * The latter condition is deduced from the usage of BPF_REG_FP
	 */
	return ctx->seen & SEEN_FUNC || bpf_is_seen_register(ctx, BPF_REG_FP);
}

/*
 * When not setting up our own stackframe, the redzone usage is:
 *
 *		[	prev sp		] <-------------
 *		[	  ...       	] 		|
 * sp (r1) --->	[    stack pointer	] --------------
 *		[   nv gpr save area	] 6*8
 *		[    tail_call_cnt	] 8
 *		[    local_tmp_var	] 8
 *		[   unused red zone	] 208 bytes protected
 */
static int bpf_jit_stack_local(struct codegen_context *ctx)
{
	if (bpf_has_stack_frame(ctx))
		return STACK_FRAME_MIN_SIZE + ctx->stack_size;
	else
		return -(BPF_PPC_STACK_SAVE + 16);
}

static int bpf_jit_stack_tailcallcnt(struct codegen_context *ctx)
{
	return bpf_jit_stack_local(ctx) + 8;
}

static int bpf_jit_stack_offsetof(struct codegen_context *ctx, int reg)
{
	if (reg >= BPF_PPC_NVR_MIN && reg < 32)
		return (bpf_has_stack_frame(ctx) ?
			(BPF_PPC_STACKFRAME + ctx->stack_size) : 0)
				- (8 * (32 - reg));

	pr_err("BPF JIT is asking about unknown registers");
	BUG();
}

static void bpf_jit_build_prologue(u32 *image, struct codegen_context *ctx)
{
	int i;

	/*
	 * Initialize tail_call_cnt if we do tail calls.
	 * Otherwise, put in NOPs so that it can be skipped when we are
	 * invoked through a tail call.
	 */
	if (ctx->seen & SEEN_TAILCALL) {
		PPC_LI(b2p[TMP_REG_1], 0);
		/* this goes in the redzone */
		PPC_BPF_STL(b2p[TMP_REG_1], 1, -(BPF_PPC_STACK_SAVE + 8));
	} else {
		PPC_NOP();
		PPC_NOP();
	}

#define BPF_TAILCALL_PROLOGUE_SIZE	8

	if (bpf_has_stack_frame(ctx)) {
		/*
		 * We need a stack frame, but we don't necessarily need to
		 * save/restore LR unless we call other functions
		 */
		if (ctx->seen & SEEN_FUNC) {
			EMIT(PPC_INST_MFLR | __PPC_RT(R0));
			PPC_BPF_STL(0, 1, PPC_LR_STKOFF);
		}

		PPC_BPF_STLU(1, 1, -(BPF_PPC_STACKFRAME + ctx->stack_size));
	}

	/*
	 * Back up non-volatile regs -- BPF registers 6-10
	 * If we haven't created our own stack frame, we save these
	 * in the protected zone below the previous stack frame
	 */
	for (i = BPF_REG_6; i <= BPF_REG_10; i++)
		if (bpf_is_seen_register(ctx, i))
			PPC_BPF_STL(b2p[i], 1, bpf_jit_stack_offsetof(ctx, b2p[i]));

	/* Setup frame pointer to point to the bpf stack area */
	if (bpf_is_seen_register(ctx, BPF_REG_FP))
		PPC_ADDI(b2p[BPF_REG_FP], 1,
				STACK_FRAME_MIN_SIZE + ctx->stack_size);
}

static void bpf_jit_emit_common_epilogue(u32 *image, struct codegen_context *ctx)
{
	int i;

	/* Restore NVRs */
	for (i = BPF_REG_6; i <= BPF_REG_10; i++)
		if (bpf_is_seen_register(ctx, i))
			PPC_BPF_LL(b2p[i], 1, bpf_jit_stack_offsetof(ctx, b2p[i]));

	/* Tear down our stack frame */
	if (bpf_has_stack_frame(ctx)) {
		PPC_ADDI(1, 1, BPF_PPC_STACKFRAME + ctx->stack_size);
		if (ctx->seen & SEEN_FUNC) {
			PPC_BPF_LL(0, 1, PPC_LR_STKOFF);
			PPC_MTLR(0);
		}
	}
}

static void bpf_jit_build_epilogue(u32 *image, struct codegen_context *ctx)
{
	bpf_jit_emit_common_epilogue(image, ctx);

	/* Move result to r3 */
	PPC_MR(3, b2p[BPF_REG_0]);

	PPC_BLR();
}

static void bpf_jit_emit_func_call_hlp(u32 *image, struct codegen_context *ctx,
				       u64 func)
{
#ifdef PPC64_ELF_ABI_v1
	/* func points to the function descriptor */
	PPC_LI64(b2p[TMP_REG_2], func);
	/* Load actual entry point from function descriptor */
	PPC_BPF_LL(b2p[TMP_REG_1], b2p[TMP_REG_2], 0);
	/* ... and move it to LR */
	PPC_MTLR(b2p[TMP_REG_1]);
	/*
	 * Load TOC from function descriptor at offset 8.
	 * We can clobber r2 since we get called through a
	 * function pointer (so caller will save/restore r2)
	 * and since we don't use a TOC ourself.
	 */
	PPC_BPF_LL(2, b2p[TMP_REG_2], 8);
#else
	/* We can clobber r12 */
	PPC_FUNC_ADDR(12, func);
	PPC_MTLR(12);
#endif
	PPC_BLRL();
}

static void bpf_jit_emit_func_call_rel(u32 *image, struct codegen_context *ctx,
				       u64 func)
{
	unsigned int i, ctx_idx = ctx->idx;

	/* Load function address into r12 */
	PPC_LI64(12, func);

	/* For bpf-to-bpf function calls, the callee's address is unknown
	 * until the last extra pass. As seen above, we use PPC_LI64() to
	 * load the callee's address, but this may optimize the number of
	 * instructions required based on the nature of the address.
	 *
	 * Since we don't want the number of instructions emitted to change,
	 * we pad the optimized PPC_LI64() call with NOPs to guarantee that
	 * we always have a five-instruction sequence, which is the maximum
	 * that PPC_LI64() can emit.
	 */
	for (i = ctx->idx - ctx_idx; i < 5; i++)
		PPC_NOP();

#ifdef PPC64_ELF_ABI_v1
	/*
	 * Load TOC from function descriptor at offset 8.
	 * We can clobber r2 since we get called through a
	 * function pointer (so caller will save/restore r2)
	 * and since we don't use a TOC ourself.
	 */
	PPC_BPF_LL(2, 12, 8);
	/* Load actual entry point from function descriptor */
	PPC_BPF_LL(12, 12, 0);
#endif

	PPC_MTLR(12);
	PPC_BLRL();
}

static void bpf_jit_emit_tail_call(u32 *image, struct codegen_context *ctx, u32 out)
{
	/*
	 * By now, the eBPF program has already setup parameters in r3, r4 and r5
	 * r3/BPF_REG_1 - pointer to ctx -- passed as is to the next bpf program
	 * r4/BPF_REG_2 - pointer to bpf_array
	 * r5/BPF_REG_3 - index in bpf_array
	 */
	int b2p_bpf_array = b2p[BPF_REG_2];
	int b2p_index = b2p[BPF_REG_3];

	/*
	 * if (index >= array->map.max_entries)
	 *   goto out;
	 */
	PPC_LWZ(b2p[TMP_REG_1], b2p_bpf_array, offsetof(struct bpf_array, map.max_entries));
	PPC_RLWINM(b2p_index, b2p_index, 0, 0, 31);
	PPC_CMPLW(b2p_index, b2p[TMP_REG_1]);
	PPC_BCC(COND_GE, out);

	/*
	 * if (tail_call_cnt > MAX_TAIL_CALL_CNT)
	 *   goto out;
	 */
	PPC_LD(b2p[TMP_REG_1], 1, bpf_jit_stack_tailcallcnt(ctx));
	PPC_CMPLWI(b2p[TMP_REG_1], MAX_TAIL_CALL_CNT);
	PPC_BCC(COND_GT, out);

	/*
	 * tail_call_cnt++;
	 */
	PPC_ADDI(b2p[TMP_REG_1], b2p[TMP_REG_1], 1);
	PPC_BPF_STL(b2p[TMP_REG_1], 1, bpf_jit_stack_tailcallcnt(ctx));

	/* prog = array->ptrs[index]; */
	PPC_MULI(b2p[TMP_REG_1], b2p_index, 8);
	PPC_ADD(b2p[TMP_REG_1], b2p[TMP_REG_1], b2p_bpf_array);
	PPC_LD(b2p[TMP_REG_1], b2p[TMP_REG_1], offsetof(struct bpf_array, ptrs));

	/*
	 * if (prog == NULL)
	 *   goto out;
	 */
	PPC_CMPLDI(b2p[TMP_REG_1], 0);
	PPC_BCC(COND_EQ, out);

	/* goto *(prog->bpf_func + prologue_size); */
	PPC_LD(b2p[TMP_REG_1], b2p[TMP_REG_1], offsetof(struct bpf_prog, bpf_func));
#ifdef PPC64_ELF_ABI_v1
	/* skip past the function descriptor */
	PPC_ADDI(b2p[TMP_REG_1], b2p[TMP_REG_1],
			FUNCTION_DESCR_SIZE + BPF_TAILCALL_PROLOGUE_SIZE);
#else
	PPC_ADDI(b2p[TMP_REG_1], b2p[TMP_REG_1], BPF_TAILCALL_PROLOGUE_SIZE);
#endif
	PPC_MTCTR(b2p[TMP_REG_1]);

	/* tear down stack, restore NVRs, ... */
	bpf_jit_emit_common_epilogue(image, ctx);

	PPC_BCTR();
	/* out: */
}

/* Assemble the body code between the prologue & epilogue */
static int bpf_jit_build_body(struct bpf_prog *fp, u32 *image,
			      struct codegen_context *ctx,
			      u32 *addrs, bool extra_pass)
{
	const struct bpf_insn *insn = fp->insnsi;
	int flen = fp->len;
	int i, ret;

	/* Start of epilogue code - will only be valid 2nd pass onwards */
	u32 exit_addr = addrs[flen];

	for (i = 0; i < flen; i++) {
		u32 code = insn[i].code;
		u32 dst_reg = b2p[insn[i].dst_reg];
		u32 src_reg = b2p[insn[i].src_reg];
		s16 off = insn[i].off;
		s32 imm = insn[i].imm;
		bool func_addr_fixed;
		u64 func_addr;
		u64 imm64;
		u32 true_cond;
		u32 tmp_idx;

		/*
		 * addrs[] maps a BPF bytecode address into a real offset from
		 * the start of the body code.
		 */
		addrs[i] = ctx->idx * 4;

		/*
		 * As an optimization, we note down which non-volatile registers
		 * are used so that we can only save/restore those in our
		 * prologue and epilogue. We do this here regardless of whether
		 * the actual BPF instruction uses src/dst registers or not
		 * (for instance, BPF_CALL does not use them). The expectation
		 * is that those instructions will have src_reg/dst_reg set to
		 * 0. Even otherwise, we just lose some prologue/epilogue
		 * optimization but everything else should work without
		 * any issues.
		 */
		if (dst_reg >= BPF_PPC_NVR_MIN && dst_reg < 32)
			bpf_set_seen_register(ctx, insn[i].dst_reg);
		if (src_reg >= BPF_PPC_NVR_MIN && src_reg < 32)
			bpf_set_seen_register(ctx, insn[i].src_reg);

		switch (code) {
		/*
		 * Arithmetic operations: ADD/SUB/MUL/DIV/MOD/NEG
		 */
		case BPF_ALU | BPF_ADD | BPF_X: /* (u32) dst += (u32) src */
		case BPF_ALU64 | BPF_ADD | BPF_X: /* dst += src */
			PPC_ADD(dst_reg, dst_reg, src_reg);
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_SUB | BPF_X: /* (u32) dst -= (u32) src */
		case BPF_ALU64 | BPF_SUB | BPF_X: /* dst -= src */
			PPC_SUB(dst_reg, dst_reg, src_reg);
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_ADD | BPF_K: /* (u32) dst += (u32) imm */
		case BPF_ALU | BPF_SUB | BPF_K: /* (u32) dst -= (u32) imm */
		case BPF_ALU64 | BPF_ADD | BPF_K: /* dst += imm */
		case BPF_ALU64 | BPF_SUB | BPF_K: /* dst -= imm */
			if (BPF_OP(code) == BPF_SUB)
				imm = -imm;
			if (imm) {
				if (imm >= -32768 && imm < 32768)
					PPC_ADDI(dst_reg, dst_reg, IMM_L(imm));
				else {
					PPC_LI32(b2p[TMP_REG_1], imm);
					PPC_ADD(dst_reg, dst_reg, b2p[TMP_REG_1]);
				}
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_MUL | BPF_X: /* (u32) dst *= (u32) src */
		case BPF_ALU64 | BPF_MUL | BPF_X: /* dst *= src */
			if (BPF_CLASS(code) == BPF_ALU)
				PPC_MULW(dst_reg, dst_reg, src_reg);
			else
				PPC_MULD(dst_reg, dst_reg, src_reg);
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_MUL | BPF_K: /* (u32) dst *= (u32) imm */
		case BPF_ALU64 | BPF_MUL | BPF_K: /* dst *= imm */
			if (imm >= -32768 && imm < 32768)
				PPC_MULI(dst_reg, dst_reg, IMM_L(imm));
			else {
				PPC_LI32(b2p[TMP_REG_1], imm);
				if (BPF_CLASS(code) == BPF_ALU)
					PPC_MULW(dst_reg, dst_reg,
							b2p[TMP_REG_1]);
				else
					PPC_MULD(dst_reg, dst_reg,
							b2p[TMP_REG_1]);
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_DIV | BPF_X: /* (u32) dst /= (u32) src */
		case BPF_ALU | BPF_MOD | BPF_X: /* (u32) dst %= (u32) src */
			if (BPF_OP(code) == BPF_MOD) {
				PPC_DIVWU(b2p[TMP_REG_1], dst_reg, src_reg);
				PPC_MULW(b2p[TMP_REG_1], src_reg,
						b2p[TMP_REG_1]);
				PPC_SUB(dst_reg, dst_reg, b2p[TMP_REG_1]);
			} else
				PPC_DIVWU(dst_reg, dst_reg, src_reg);
			goto bpf_alu32_trunc;
		case BPF_ALU64 | BPF_DIV | BPF_X: /* dst /= src */
		case BPF_ALU64 | BPF_MOD | BPF_X: /* dst %= src */
			if (BPF_OP(code) == BPF_MOD) {
				PPC_DIVD(b2p[TMP_REG_1], dst_reg, src_reg);
				PPC_MULD(b2p[TMP_REG_1], src_reg,
						b2p[TMP_REG_1]);
				PPC_SUB(dst_reg, dst_reg, b2p[TMP_REG_1]);
			} else
				PPC_DIVD(dst_reg, dst_reg, src_reg);
			break;
		case BPF_ALU | BPF_MOD | BPF_K: /* (u32) dst %= (u32) imm */
		case BPF_ALU | BPF_DIV | BPF_K: /* (u32) dst /= (u32) imm */
		case BPF_ALU64 | BPF_MOD | BPF_K: /* dst %= imm */
		case BPF_ALU64 | BPF_DIV | BPF_K: /* dst /= imm */
			if (imm == 0)
				return -EINVAL;
			else if (imm == 1)
				goto bpf_alu32_trunc;

			PPC_LI32(b2p[TMP_REG_1], imm);
			switch (BPF_CLASS(code)) {
			case BPF_ALU:
				if (BPF_OP(code) == BPF_MOD) {
					PPC_DIVWU(b2p[TMP_REG_2], dst_reg,
							b2p[TMP_REG_1]);
					PPC_MULW(b2p[TMP_REG_1],
							b2p[TMP_REG_1],
							b2p[TMP_REG_2]);
					PPC_SUB(dst_reg, dst_reg,
							b2p[TMP_REG_1]);
				} else
					PPC_DIVWU(dst_reg, dst_reg,
							b2p[TMP_REG_1]);
				break;
			case BPF_ALU64:
				if (BPF_OP(code) == BPF_MOD) {
					PPC_DIVD(b2p[TMP_REG_2], dst_reg,
							b2p[TMP_REG_1]);
					PPC_MULD(b2p[TMP_REG_1],
							b2p[TMP_REG_1],
							b2p[TMP_REG_2]);
					PPC_SUB(dst_reg, dst_reg,
							b2p[TMP_REG_1]);
				} else
					PPC_DIVD(dst_reg, dst_reg,
							b2p[TMP_REG_1]);
				break;
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_NEG: /* (u32) dst = -dst */
		case BPF_ALU64 | BPF_NEG: /* dst = -dst */
			PPC_NEG(dst_reg, dst_reg);
			goto bpf_alu32_trunc;

		/*
		 * Logical operations: AND/OR/XOR/[A]LSH/[A]RSH
		 */
		case BPF_ALU | BPF_AND | BPF_X: /* (u32) dst = dst & src */
		case BPF_ALU64 | BPF_AND | BPF_X: /* dst = dst & src */
			PPC_AND(dst_reg, dst_reg, src_reg);
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_AND | BPF_K: /* (u32) dst = dst & imm */
		case BPF_ALU64 | BPF_AND | BPF_K: /* dst = dst & imm */
			if (!IMM_H(imm))
				PPC_ANDI(dst_reg, dst_reg, IMM_L(imm));
			else {
				/* Sign-extended */
				PPC_LI32(b2p[TMP_REG_1], imm);
				PPC_AND(dst_reg, dst_reg, b2p[TMP_REG_1]);
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_OR | BPF_X: /* dst = (u32) dst | (u32) src */
		case BPF_ALU64 | BPF_OR | BPF_X: /* dst = dst | src */
			PPC_OR(dst_reg, dst_reg, src_reg);
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_OR | BPF_K:/* dst = (u32) dst | (u32) imm */
		case BPF_ALU64 | BPF_OR | BPF_K:/* dst = dst | imm */
			if (imm < 0 && BPF_CLASS(code) == BPF_ALU64) {
				/* Sign-extended */
				PPC_LI32(b2p[TMP_REG_1], imm);
				PPC_OR(dst_reg, dst_reg, b2p[TMP_REG_1]);
			} else {
				if (IMM_L(imm))
					PPC_ORI(dst_reg, dst_reg, IMM_L(imm));
				if (IMM_H(imm))
					PPC_ORIS(dst_reg, dst_reg, IMM_H(imm));
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_XOR | BPF_X: /* (u32) dst ^= src */
		case BPF_ALU64 | BPF_XOR | BPF_X: /* dst ^= src */
			PPC_XOR(dst_reg, dst_reg, src_reg);
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_XOR | BPF_K: /* (u32) dst ^= (u32) imm */
		case BPF_ALU64 | BPF_XOR | BPF_K: /* dst ^= imm */
			if (imm < 0 && BPF_CLASS(code) == BPF_ALU64) {
				/* Sign-extended */
				PPC_LI32(b2p[TMP_REG_1], imm);
				PPC_XOR(dst_reg, dst_reg, b2p[TMP_REG_1]);
			} else {
				if (IMM_L(imm))
					PPC_XORI(dst_reg, dst_reg, IMM_L(imm));
				if (IMM_H(imm))
					PPC_XORIS(dst_reg, dst_reg, IMM_H(imm));
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_LSH | BPF_X: /* (u32) dst <<= (u32) src */
			/* slw clears top 32 bits */
			PPC_SLW(dst_reg, dst_reg, src_reg);
			break;
		case BPF_ALU64 | BPF_LSH | BPF_X: /* dst <<= src; */
			PPC_SLD(dst_reg, dst_reg, src_reg);
			break;
		case BPF_ALU | BPF_LSH | BPF_K: /* (u32) dst <<== (u32) imm */
			/* with imm 0, we still need to clear top 32 bits */
			PPC_SLWI(dst_reg, dst_reg, imm);
			break;
		case BPF_ALU64 | BPF_LSH | BPF_K: /* dst <<== imm */
			if (imm != 0)
				PPC_SLDI(dst_reg, dst_reg, imm);
			break;
		case BPF_ALU | BPF_RSH | BPF_X: /* (u32) dst >>= (u32) src */
			PPC_SRW(dst_reg, dst_reg, src_reg);
			break;
		case BPF_ALU64 | BPF_RSH | BPF_X: /* dst >>= src */
			PPC_SRD(dst_reg, dst_reg, src_reg);
			break;
		case BPF_ALU | BPF_RSH | BPF_K: /* (u32) dst >>= (u32) imm */
			PPC_SRWI(dst_reg, dst_reg, imm);
			break;
		case BPF_ALU64 | BPF_RSH | BPF_K: /* dst >>= imm */
			if (imm != 0)
				PPC_SRDI(dst_reg, dst_reg, imm);
			break;
		case BPF_ALU64 | BPF_ARSH | BPF_X: /* (s64) dst >>= src */
			PPC_SRAD(dst_reg, dst_reg, src_reg);
			break;
		case BPF_ALU64 | BPF_ARSH | BPF_K: /* (s64) dst >>= imm */
			if (imm != 0)
				PPC_SRADI(dst_reg, dst_reg, imm);
			break;

		/*
		 * MOV
		 */
		case BPF_ALU | BPF_MOV | BPF_X: /* (u32) dst = src */
		case BPF_ALU64 | BPF_MOV | BPF_X: /* dst = src */
			PPC_MR(dst_reg, src_reg);
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_MOV | BPF_K: /* (u32) dst = imm */
		case BPF_ALU64 | BPF_MOV | BPF_K: /* dst = (s64) imm */
			PPC_LI32(dst_reg, imm);
			if (imm < 0)
				goto bpf_alu32_trunc;
			break;

bpf_alu32_trunc:
		/* Truncate to 32-bits */
		if (BPF_CLASS(code) == BPF_ALU)
			PPC_RLWINM(dst_reg, dst_reg, 0, 0, 31);
		break;

		/*
		 * BPF_FROM_BE/LE
		 */
		case BPF_ALU | BPF_END | BPF_FROM_LE:
		case BPF_ALU | BPF_END | BPF_FROM_BE:
#ifdef __BIG_ENDIAN__
			if (BPF_SRC(code) == BPF_FROM_BE)
				goto emit_clear;
#else /* !__BIG_ENDIAN__ */
			if (BPF_SRC(code) == BPF_FROM_LE)
				goto emit_clear;
#endif
			switch (imm) {
			case 16:
				/* Rotate 8 bits left & mask with 0x0000ff00 */
				PPC_RLWINM(b2p[TMP_REG_1], dst_reg, 8, 16, 23);
				/* Rotate 8 bits right & insert LSB to reg */
				PPC_RLWIMI(b2p[TMP_REG_1], dst_reg, 24, 24, 31);
				/* Move result back to dst_reg */
				PPC_MR(dst_reg, b2p[TMP_REG_1]);
				break;
			case 32:
				/*
				 * Rotate word left by 8 bits:
				 * 2 bytes are already in their final position
				 * -- byte 2 and 4 (of bytes 1, 2, 3 and 4)
				 */
				PPC_RLWINM(b2p[TMP_REG_1], dst_reg, 8, 0, 31);
				/* Rotate 24 bits and insert byte 1 */
				PPC_RLWIMI(b2p[TMP_REG_1], dst_reg, 24, 0, 7);
				/* Rotate 24 bits and insert byte 3 */
				PPC_RLWIMI(b2p[TMP_REG_1], dst_reg, 24, 16, 23);
				PPC_MR(dst_reg, b2p[TMP_REG_1]);
				break;
			case 64:
				/*
				 * Way easier and faster(?) to store the value
				 * into stack and then use ldbrx
				 *
				 * ctx->seen will be reliable in pass2, but
				 * the instructions generated will remain the
				 * same across all passes
				 */
				PPC_STD(dst_reg, 1, bpf_jit_stack_local(ctx));
				PPC_ADDI(b2p[TMP_REG_1], 1, bpf_jit_stack_local(ctx));
				PPC_LDBRX(dst_reg, 0, b2p[TMP_REG_1]);
				break;
			}
			break;

emit_clear:
			switch (imm) {
			case 16:
				/* zero-extend 16 bits into 64 bits */
				PPC_RLDICL(dst_reg, dst_reg, 0, 48);
				break;
			case 32:
				/* zero-extend 32 bits into 64 bits */
				PPC_RLDICL(dst_reg, dst_reg, 0, 32);
				break;
			case 64:
				/* nop */
				break;
			}
			break;

		/*
		 * BPF_ST(X)
		 */
		case BPF_STX | BPF_MEM | BPF_B: /* *(u8 *)(dst + off) = src */
		case BPF_ST | BPF_MEM | BPF_B: /* *(u8 *)(dst + off) = imm */
			if (BPF_CLASS(code) == BPF_ST) {
				PPC_LI(b2p[TMP_REG_1], imm);
				src_reg = b2p[TMP_REG_1];
			}
			PPC_STB(src_reg, dst_reg, off);
			break;
		case BPF_STX | BPF_MEM | BPF_H: /* (u16 *)(dst + off) = src */
		case BPF_ST | BPF_MEM | BPF_H: /* (u16 *)(dst + off) = imm */
			if (BPF_CLASS(code) == BPF_ST) {
				PPC_LI(b2p[TMP_REG_1], imm);
				src_reg = b2p[TMP_REG_1];
			}
			PPC_STH(src_reg, dst_reg, off);
			break;
		case BPF_STX | BPF_MEM | BPF_W: /* *(u32 *)(dst + off) = src */
		case BPF_ST | BPF_MEM | BPF_W: /* *(u32 *)(dst + off) = imm */
			if (BPF_CLASS(code) == BPF_ST) {
				PPC_LI32(b2p[TMP_REG_1], imm);
				src_reg = b2p[TMP_REG_1];
			}
			PPC_STW(src_reg, dst_reg, off);
			break;
		case BPF_STX | BPF_MEM | BPF_DW: /* (u64 *)(dst + off) = src */
		case BPF_ST | BPF_MEM | BPF_DW: /* *(u64 *)(dst + off) = imm */
			if (BPF_CLASS(code) == BPF_ST) {
				PPC_LI32(b2p[TMP_REG_1], imm);
				src_reg = b2p[TMP_REG_1];
			}
			PPC_STD(src_reg, dst_reg, off);
			break;

		/*
		 * BPF_STX XADD (atomic_add)
		 */
		/* *(u32 *)(dst + off) += src */
		case BPF_STX | BPF_XADD | BPF_W:
			/* Get EA into TMP_REG_1 */
			PPC_ADDI(b2p[TMP_REG_1], dst_reg, off);
			tmp_idx = ctx->idx * 4;
			/* load value from memory into TMP_REG_2 */
			PPC_BPF_LWARX(b2p[TMP_REG_2], 0, b2p[TMP_REG_1], 0);
			/* add value from src_reg into this */
			PPC_ADD(b2p[TMP_REG_2], b2p[TMP_REG_2], src_reg);
			/* store result back */
			PPC_BPF_STWCX(b2p[TMP_REG_2], 0, b2p[TMP_REG_1]);
			/* we're done if this succeeded */
			PPC_BCC_SHORT(COND_NE, tmp_idx);
			break;
		/* *(u64 *)(dst + off) += src */
		case BPF_STX | BPF_XADD | BPF_DW:
			PPC_ADDI(b2p[TMP_REG_1], dst_reg, off);
			tmp_idx = ctx->idx * 4;
			PPC_BPF_LDARX(b2p[TMP_REG_2], 0, b2p[TMP_REG_1], 0);
			PPC_ADD(b2p[TMP_REG_2], b2p[TMP_REG_2], src_reg);
			PPC_BPF_STDCX(b2p[TMP_REG_2], 0, b2p[TMP_REG_1]);
			PPC_BCC_SHORT(COND_NE, tmp_idx);
			break;

		/*
		 * BPF_LDX
		 */
		/* dst = *(u8 *)(ul) (src + off) */
		case BPF_LDX | BPF_MEM | BPF_B:
			PPC_LBZ(dst_reg, src_reg, off);
			break;
		/* dst = *(u16 *)(ul) (src + off) */
		case BPF_LDX | BPF_MEM | BPF_H:
			PPC_LHZ(dst_reg, src_reg, off);
			break;
		/* dst = *(u32 *)(ul) (src + off) */
		case BPF_LDX | BPF_MEM | BPF_W:
			PPC_LWZ(dst_reg, src_reg, off);
			break;
		/* dst = *(u64 *)(ul) (src + off) */
		case BPF_LDX | BPF_MEM | BPF_DW:
			PPC_LD(dst_reg, src_reg, off);
			break;

		/*
		 * Doubleword load
		 * 16 byte instruction that uses two 'struct bpf_insn'
		 */
		case BPF_LD | BPF_IMM | BPF_DW: /* dst = (u64) imm */
			imm64 = ((u64)(u32) insn[i].imm) |
				    (((u64)(u32) insn[i+1].imm) << 32);
			/* Adjust for two bpf instructions */
			addrs[++i] = ctx->idx * 4;
			PPC_LI64(dst_reg, imm64);
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

			ret = bpf_jit_get_func_addr(fp, &insn[i], extra_pass,
						    &func_addr, &func_addr_fixed);
			if (ret < 0)
				return ret;

			if (func_addr_fixed)
				bpf_jit_emit_func_call_hlp(image, ctx, func_addr);
			else
				bpf_jit_emit_func_call_rel(image, ctx, func_addr);
			/* move return value from r3 to BPF_REG_0 */
			PPC_MR(b2p[BPF_REG_0], 3);
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
			true_cond = COND_GT;
			goto cond_branch;
		case BPF_JMP | BPF_JLT | BPF_K:
		case BPF_JMP | BPF_JLT | BPF_X:
		case BPF_JMP | BPF_JSLT | BPF_K:
		case BPF_JMP | BPF_JSLT | BPF_X:
			true_cond = COND_LT;
			goto cond_branch;
		case BPF_JMP | BPF_JGE | BPF_K:
		case BPF_JMP | BPF_JGE | BPF_X:
		case BPF_JMP | BPF_JSGE | BPF_K:
		case BPF_JMP | BPF_JSGE | BPF_X:
			true_cond = COND_GE;
			goto cond_branch;
		case BPF_JMP | BPF_JLE | BPF_K:
		case BPF_JMP | BPF_JLE | BPF_X:
		case BPF_JMP | BPF_JSLE | BPF_K:
		case BPF_JMP | BPF_JSLE | BPF_X:
			true_cond = COND_LE;
			goto cond_branch;
		case BPF_JMP | BPF_JEQ | BPF_K:
		case BPF_JMP | BPF_JEQ | BPF_X:
			true_cond = COND_EQ;
			goto cond_branch;
		case BPF_JMP | BPF_JNE | BPF_K:
		case BPF_JMP | BPF_JNE | BPF_X:
			true_cond = COND_NE;
			goto cond_branch;
		case BPF_JMP | BPF_JSET | BPF_K:
		case BPF_JMP | BPF_JSET | BPF_X:
			true_cond = COND_NE;
			/* Fall through */

cond_branch:
			switch (code) {
			case BPF_JMP | BPF_JGT | BPF_X:
			case BPF_JMP | BPF_JLT | BPF_X:
			case BPF_JMP | BPF_JGE | BPF_X:
			case BPF_JMP | BPF_JLE | BPF_X:
			case BPF_JMP | BPF_JEQ | BPF_X:
			case BPF_JMP | BPF_JNE | BPF_X:
				/* unsigned comparison */
				PPC_CMPLD(dst_reg, src_reg);
				break;
			case BPF_JMP | BPF_JSGT | BPF_X:
			case BPF_JMP | BPF_JSLT | BPF_X:
			case BPF_JMP | BPF_JSGE | BPF_X:
			case BPF_JMP | BPF_JSLE | BPF_X:
				/* signed comparison */
				PPC_CMPD(dst_reg, src_reg);
				break;
			case BPF_JMP | BPF_JSET | BPF_X:
				PPC_AND_DOT(b2p[TMP_REG_1], dst_reg, src_reg);
				break;
			case BPF_JMP | BPF_JNE | BPF_K:
			case BPF_JMP | BPF_JEQ | BPF_K:
			case BPF_JMP | BPF_JGT | BPF_K:
			case BPF_JMP | BPF_JLT | BPF_K:
			case BPF_JMP | BPF_JGE | BPF_K:
			case BPF_JMP | BPF_JLE | BPF_K:
				/*
				 * Need sign-extended load, so only positive
				 * values can be used as imm in cmpldi
				 */
				if (imm >= 0 && imm < 32768)
					PPC_CMPLDI(dst_reg, imm);
				else {
					/* sign-extending load */
					PPC_LI32(b2p[TMP_REG_1], imm);
					/* ... but unsigned comparison */
					PPC_CMPLD(dst_reg, b2p[TMP_REG_1]);
				}
				break;
			case BPF_JMP | BPF_JSGT | BPF_K:
			case BPF_JMP | BPF_JSLT | BPF_K:
			case BPF_JMP | BPF_JSGE | BPF_K:
			case BPF_JMP | BPF_JSLE | BPF_K:
				/*
				 * signed comparison, so any 16-bit value
				 * can be used in cmpdi
				 */
				if (imm >= -32768 && imm < 32768)
					PPC_CMPDI(dst_reg, imm);
				else {
					PPC_LI32(b2p[TMP_REG_1], imm);
					PPC_CMPD(dst_reg, b2p[TMP_REG_1]);
				}
				break;
			case BPF_JMP | BPF_JSET | BPF_K:
				/* andi does not sign-extend the immediate */
				if (imm >= 0 && imm < 32768)
					/* PPC_ANDI is _only/always_ dot-form */
					PPC_ANDI(b2p[TMP_REG_1], dst_reg, imm);
				else {
					PPC_LI32(b2p[TMP_REG_1], imm);
					PPC_AND_DOT(b2p[TMP_REG_1], dst_reg,
						    b2p[TMP_REG_1]);
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
			bpf_jit_emit_tail_call(image, ctx, addrs[i + 1]);
			break;

		default:
			/*
			 * The filter contains something cruel & unusual.
			 * We don't handle it, but also there shouldn't be
			 * anything missing from our list.
			 */
			pr_err_ratelimited("eBPF filter opcode %04x (@%d) unsupported\n",
					code, i);
			return -ENOTSUPP;
		}
	}

	/* Set end-of-body-code address for exit. */
	addrs[i] = ctx->idx * 4;

	return 0;
}

struct powerpc64_jit_data {
	struct bpf_binary_header *header;
	u32 *addrs;
	u8 *image;
	u32 proglen;
	struct codegen_context ctx;
};

struct bpf_prog *bpf_int_jit_compile(struct bpf_prog *fp)
{
	u32 proglen;
	u32 alloclen;
	u8 *image = NULL;
	u32 *code_base;
	u32 *addrs;
	struct powerpc64_jit_data *jit_data;
	struct codegen_context cgctx;
	int pass;
	int flen;
	struct bpf_binary_header *bpf_hdr;
	struct bpf_prog *org_fp = fp;
	struct bpf_prog *tmp_fp;
	bool bpf_blinded = false;
	bool extra_pass = false;

	if (!fp->jit_requested)
		return org_fp;

	tmp_fp = bpf_jit_blind_constants(org_fp);
	if (IS_ERR(tmp_fp))
		return org_fp;

	if (tmp_fp != org_fp) {
		bpf_blinded = true;
		fp = tmp_fp;
	}

	jit_data = fp->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc(sizeof(*jit_data), GFP_KERNEL);
		if (!jit_data) {
			fp = org_fp;
			goto out;
		}
		fp->aux->jit_data = jit_data;
	}

	flen = fp->len;
	addrs = jit_data->addrs;
	if (addrs) {
		cgctx = jit_data->ctx;
		image = jit_data->image;
		bpf_hdr = jit_data->header;
		proglen = jit_data->proglen;
		alloclen = proglen + FUNCTION_DESCR_SIZE;
		extra_pass = true;
		goto skip_init_ctx;
	}

	addrs = kcalloc(flen + 1, sizeof(*addrs), GFP_KERNEL);
	if (addrs == NULL) {
		fp = org_fp;
		goto out_addrs;
	}

	memset(&cgctx, 0, sizeof(struct codegen_context));

	/* Make sure that the stack is quadword aligned. */
	cgctx.stack_size = round_up(fp->aux->stack_depth, 16);

	/* Scouting faux-generate pass 0 */
	if (bpf_jit_build_body(fp, 0, &cgctx, addrs, false)) {
		/* We hit something illegal or unsupported. */
		fp = org_fp;
		goto out_addrs;
	}

	/*
	 * Pretend to build prologue, given the features we've seen.  This will
	 * update ctgtx.idx as it pretends to output instructions, then we can
	 * calculate total size from idx.
	 */
	bpf_jit_build_prologue(0, &cgctx);
	bpf_jit_build_epilogue(0, &cgctx);

	proglen = cgctx.idx * 4;
	alloclen = proglen + FUNCTION_DESCR_SIZE;

	bpf_hdr = bpf_jit_binary_alloc(alloclen, &image, 4,
			bpf_jit_fill_ill_insns);
	if (!bpf_hdr) {
		fp = org_fp;
		goto out_addrs;
	}

skip_init_ctx:
	code_base = (u32 *)(image + FUNCTION_DESCR_SIZE);

	/* Code generation passes 1-2 */
	for (pass = 1; pass < 3; pass++) {
		/* Now build the prologue, body code & epilogue for real. */
		cgctx.idx = 0;
		bpf_jit_build_prologue(code_base, &cgctx);
		bpf_jit_build_body(fp, code_base, &cgctx, addrs, extra_pass);
		bpf_jit_build_epilogue(code_base, &cgctx);

		if (bpf_jit_enable > 1)
			pr_info("Pass %d: shrink = %d, seen = 0x%x\n", pass,
				proglen - (cgctx.idx * 4), cgctx.seen);
	}

	if (bpf_jit_enable > 1)
		/*
		 * Note that we output the base address of the code_base
		 * rather than image, since opcodes are in code_base.
		 */
		bpf_jit_dump(flen, proglen, pass, code_base);

#ifdef PPC64_ELF_ABI_v1
	/* Function descriptor nastiness: Address + TOC */
	((u64 *)image)[0] = (u64)code_base;
	((u64 *)image)[1] = local_paca->kernel_toc;
#endif

	fp->bpf_func = (void *)image;
	fp->jited = 1;
	fp->jited_len = alloclen;

	bpf_flush_icache(bpf_hdr, (u8 *)bpf_hdr + (bpf_hdr->pages * PAGE_SIZE));
	if (!fp->is_func || extra_pass) {
out_addrs:
		kfree(addrs);
		kfree(jit_data);
		fp->aux->jit_data = NULL;
	} else {
		jit_data->addrs = addrs;
		jit_data->ctx = cgctx;
		jit_data->proglen = proglen;
		jit_data->image = image;
		jit_data->header = bpf_hdr;
	}

out:
	if (bpf_blinded)
		bpf_jit_prog_release_other(fp, fp == org_fp ? tmp_fp : org_fp);

	return fp;
}

/* Overriding bpf_jit_free() as we don't set images read-only. */
void bpf_jit_free(struct bpf_prog *fp)
{
	unsigned long addr = (unsigned long)fp->bpf_func & PAGE_MASK;
	struct bpf_binary_header *bpf_hdr = (void *)addr;

	if (fp->jited)
		bpf_jit_binary_free(bpf_hdr);

	bpf_prog_unlock_free(fp);
}
