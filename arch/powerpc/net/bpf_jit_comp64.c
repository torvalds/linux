// SPDX-License-Identifier: GPL-2.0-only
/*
 * bpf_jit_comp64.c: eBPF JIT compiler
 *
 * Copyright 2016 Naveen N. Rao <naveen.n.rao@linux.vnet.ibm.com>
 *		  IBM Corporation
 *
 * Based on the powerpc classic BPF JIT compiler by Matt Evans
 */
#include <linux/moduleloader.h>
#include <asm/cacheflush.h>
#include <asm/asm-compat.h>
#include <linux/netdevice.h>
#include <linux/filter.h>
#include <linux/if_vlan.h>
#include <asm/kprobes.h>
#include <linux/bpf.h>
#include <asm/security_features.h>

#include "bpf_jit.h"

/*
 * Stack layout:
 * Ensure the top half (upto local_tmp_var) stays consistent
 * with our redzone usage.
 *
 *		[	prev sp		] <-------------
 *		[   nv gpr save area	] 5*8		|
 *		[    tail_call_cnt	] 8		|
 *		[    local_tmp_var	] 16		|
 * fp (r31) -->	[   ebpf stack space	] upto 512	|
 *		[     frame header	] 32/112	|
 * sp (r1) --->	[    stack pointer	] --------------
 */

/* for gpr non volatile registers BPG_REG_6 to 10 */
#define BPF_PPC_STACK_SAVE	(5*8)
/* for bpf JIT code internal usage */
#define BPF_PPC_STACK_LOCALS	24
/* stack frame excluding BPF stack, ensure this is quadword aligned */
#define BPF_PPC_STACKFRAME	(STACK_FRAME_MIN_SIZE + \
				 BPF_PPC_STACK_LOCALS + BPF_PPC_STACK_SAVE)

/* BPF register usage */
#define TMP_REG_1	(MAX_BPF_JIT_REG + 0)
#define TMP_REG_2	(MAX_BPF_JIT_REG + 1)

/* BPF to ppc register mappings */
void bpf_jit_init_reg_mapping(struct codegen_context *ctx)
{
	/* function return value */
	ctx->b2p[BPF_REG_0] = _R8;
	/* function arguments */
	ctx->b2p[BPF_REG_1] = _R3;
	ctx->b2p[BPF_REG_2] = _R4;
	ctx->b2p[BPF_REG_3] = _R5;
	ctx->b2p[BPF_REG_4] = _R6;
	ctx->b2p[BPF_REG_5] = _R7;
	/* non volatile registers */
	ctx->b2p[BPF_REG_6] = _R27;
	ctx->b2p[BPF_REG_7] = _R28;
	ctx->b2p[BPF_REG_8] = _R29;
	ctx->b2p[BPF_REG_9] = _R30;
	/* frame pointer aka BPF_REG_10 */
	ctx->b2p[BPF_REG_FP] = _R31;
	/* eBPF jit internal registers */
	ctx->b2p[BPF_REG_AX] = _R12;
	ctx->b2p[TMP_REG_1] = _R9;
	ctx->b2p[TMP_REG_2] = _R10;
}

/* PPC NVR range -- update this if we ever use NVRs below r27 */
#define BPF_PPC_NVR_MIN		_R27

static inline bool bpf_has_stack_frame(struct codegen_context *ctx)
{
	/*
	 * We only need a stack frame if:
	 * - we call other functions (kernel helpers), or
	 * - the bpf program uses its stack area
	 * The latter condition is deduced from the usage of BPF_REG_FP
	 */
	return ctx->seen & SEEN_FUNC || bpf_is_seen_register(ctx, bpf_to_ppc(BPF_REG_FP));
}

/*
 * When not setting up our own stackframe, the redzone usage is:
 *
 *		[	prev sp		] <-------------
 *		[	  ...       	] 		|
 * sp (r1) --->	[    stack pointer	] --------------
 *		[   nv gpr save area	] 5*8
 *		[    tail_call_cnt	] 8
 *		[    local_tmp_var	] 16
 *		[   unused red zone	] 208 bytes protected
 */
static int bpf_jit_stack_local(struct codegen_context *ctx)
{
	if (bpf_has_stack_frame(ctx))
		return STACK_FRAME_MIN_SIZE + ctx->stack_size;
	else
		return -(BPF_PPC_STACK_SAVE + 24);
}

static int bpf_jit_stack_tailcallcnt(struct codegen_context *ctx)
{
	return bpf_jit_stack_local(ctx) + 16;
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

void bpf_jit_realloc_regs(struct codegen_context *ctx)
{
}

void bpf_jit_build_prologue(u32 *image, struct codegen_context *ctx)
{
	int i;

	if (__is_defined(PPC64_ELF_ABI_v2))
		EMIT(PPC_RAW_LD(_R2, _R13, offsetof(struct paca_struct, kernel_toc)));

	/*
	 * Initialize tail_call_cnt if we do tail calls.
	 * Otherwise, put in NOPs so that it can be skipped when we are
	 * invoked through a tail call.
	 */
	if (ctx->seen & SEEN_TAILCALL) {
		EMIT(PPC_RAW_LI(bpf_to_ppc(TMP_REG_1), 0));
		/* this goes in the redzone */
		EMIT(PPC_RAW_STD(bpf_to_ppc(TMP_REG_1), _R1, -(BPF_PPC_STACK_SAVE + 8)));
	} else {
		EMIT(PPC_RAW_NOP());
		EMIT(PPC_RAW_NOP());
	}

	if (bpf_has_stack_frame(ctx)) {
		/*
		 * We need a stack frame, but we don't necessarily need to
		 * save/restore LR unless we call other functions
		 */
		if (ctx->seen & SEEN_FUNC) {
			EMIT(PPC_RAW_MFLR(_R0));
			EMIT(PPC_RAW_STD(_R0, _R1, PPC_LR_STKOFF));
		}

		EMIT(PPC_RAW_STDU(_R1, _R1, -(BPF_PPC_STACKFRAME + ctx->stack_size)));
	}

	/*
	 * Back up non-volatile regs -- BPF registers 6-10
	 * If we haven't created our own stack frame, we save these
	 * in the protected zone below the previous stack frame
	 */
	for (i = BPF_REG_6; i <= BPF_REG_10; i++)
		if (bpf_is_seen_register(ctx, bpf_to_ppc(i)))
			EMIT(PPC_RAW_STD(bpf_to_ppc(i), _R1, bpf_jit_stack_offsetof(ctx, bpf_to_ppc(i))));

	/* Setup frame pointer to point to the bpf stack area */
	if (bpf_is_seen_register(ctx, bpf_to_ppc(BPF_REG_FP)))
		EMIT(PPC_RAW_ADDI(bpf_to_ppc(BPF_REG_FP), _R1,
				STACK_FRAME_MIN_SIZE + ctx->stack_size));
}

static void bpf_jit_emit_common_epilogue(u32 *image, struct codegen_context *ctx)
{
	int i;

	/* Restore NVRs */
	for (i = BPF_REG_6; i <= BPF_REG_10; i++)
		if (bpf_is_seen_register(ctx, bpf_to_ppc(i)))
			EMIT(PPC_RAW_LD(bpf_to_ppc(i), _R1, bpf_jit_stack_offsetof(ctx, bpf_to_ppc(i))));

	/* Tear down our stack frame */
	if (bpf_has_stack_frame(ctx)) {
		EMIT(PPC_RAW_ADDI(_R1, _R1, BPF_PPC_STACKFRAME + ctx->stack_size));
		if (ctx->seen & SEEN_FUNC) {
			EMIT(PPC_RAW_LD(_R0, _R1, PPC_LR_STKOFF));
			EMIT(PPC_RAW_MTLR(_R0));
		}
	}
}

void bpf_jit_build_epilogue(u32 *image, struct codegen_context *ctx)
{
	bpf_jit_emit_common_epilogue(image, ctx);

	/* Move result to r3 */
	EMIT(PPC_RAW_MR(_R3, bpf_to_ppc(BPF_REG_0)));

	EMIT(PPC_RAW_BLR());
}

static int bpf_jit_emit_func_call_hlp(u32 *image, struct codegen_context *ctx, u64 func)
{
	unsigned long func_addr = func ? ppc_function_entry((void *)func) : 0;
	long reladdr;

	if (WARN_ON_ONCE(!core_kernel_text(func_addr)))
		return -EINVAL;

	reladdr = func_addr - kernel_toc_addr();
	if (reladdr > 0x7FFFFFFF || reladdr < -(0x80000000L)) {
		pr_err("eBPF: address of %ps out of range of kernel_toc.\n", (void *)func);
		return -ERANGE;
	}

	EMIT(PPC_RAW_ADDIS(_R12, _R2, PPC_HA(reladdr)));
	EMIT(PPC_RAW_ADDI(_R12, _R12, PPC_LO(reladdr)));
	EMIT(PPC_RAW_MTCTR(_R12));
	EMIT(PPC_RAW_BCTRL());

	return 0;
}

int bpf_jit_emit_func_call_rel(u32 *image, struct codegen_context *ctx, u64 func)
{
	unsigned int i, ctx_idx = ctx->idx;

	if (WARN_ON_ONCE(func && is_module_text_address(func)))
		return -EINVAL;

	/* skip past descriptor if elf v1 */
	func += FUNCTION_DESCR_SIZE;

	/* Load function address into r12 */
	PPC_LI64(_R12, func);

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
		EMIT(PPC_RAW_NOP());

	EMIT(PPC_RAW_MTCTR(_R12));
	EMIT(PPC_RAW_BCTRL());

	return 0;
}

static int bpf_jit_emit_tail_call(u32 *image, struct codegen_context *ctx, u32 out)
{
	/*
	 * By now, the eBPF program has already setup parameters in r3, r4 and r5
	 * r3/BPF_REG_1 - pointer to ctx -- passed as is to the next bpf program
	 * r4/BPF_REG_2 - pointer to bpf_array
	 * r5/BPF_REG_3 - index in bpf_array
	 */
	int b2p_bpf_array = bpf_to_ppc(BPF_REG_2);
	int b2p_index = bpf_to_ppc(BPF_REG_3);
	int bpf_tailcall_prologue_size = 8;

	if (__is_defined(PPC64_ELF_ABI_v2))
		bpf_tailcall_prologue_size += 4; /* skip past the toc load */

	/*
	 * if (index >= array->map.max_entries)
	 *   goto out;
	 */
	EMIT(PPC_RAW_LWZ(bpf_to_ppc(TMP_REG_1), b2p_bpf_array, offsetof(struct bpf_array, map.max_entries)));
	EMIT(PPC_RAW_RLWINM(b2p_index, b2p_index, 0, 0, 31));
	EMIT(PPC_RAW_CMPLW(b2p_index, bpf_to_ppc(TMP_REG_1)));
	PPC_BCC_SHORT(COND_GE, out);

	/*
	 * if (tail_call_cnt >= MAX_TAIL_CALL_CNT)
	 *   goto out;
	 */
	EMIT(PPC_RAW_LD(bpf_to_ppc(TMP_REG_1), _R1, bpf_jit_stack_tailcallcnt(ctx)));
	EMIT(PPC_RAW_CMPLWI(bpf_to_ppc(TMP_REG_1), MAX_TAIL_CALL_CNT));
	PPC_BCC_SHORT(COND_GE, out);

	/*
	 * tail_call_cnt++;
	 */
	EMIT(PPC_RAW_ADDI(bpf_to_ppc(TMP_REG_1), bpf_to_ppc(TMP_REG_1), 1));
	EMIT(PPC_RAW_STD(bpf_to_ppc(TMP_REG_1), _R1, bpf_jit_stack_tailcallcnt(ctx)));

	/* prog = array->ptrs[index]; */
	EMIT(PPC_RAW_MULI(bpf_to_ppc(TMP_REG_1), b2p_index, 8));
	EMIT(PPC_RAW_ADD(bpf_to_ppc(TMP_REG_1), bpf_to_ppc(TMP_REG_1), b2p_bpf_array));
	EMIT(PPC_RAW_LD(bpf_to_ppc(TMP_REG_1), bpf_to_ppc(TMP_REG_1), offsetof(struct bpf_array, ptrs)));

	/*
	 * if (prog == NULL)
	 *   goto out;
	 */
	EMIT(PPC_RAW_CMPLDI(bpf_to_ppc(TMP_REG_1), 0));
	PPC_BCC_SHORT(COND_EQ, out);

	/* goto *(prog->bpf_func + prologue_size); */
	EMIT(PPC_RAW_LD(bpf_to_ppc(TMP_REG_1), bpf_to_ppc(TMP_REG_1), offsetof(struct bpf_prog, bpf_func)));
	EMIT(PPC_RAW_ADDI(bpf_to_ppc(TMP_REG_1), bpf_to_ppc(TMP_REG_1),
			FUNCTION_DESCR_SIZE + bpf_tailcall_prologue_size));
	EMIT(PPC_RAW_MTCTR(bpf_to_ppc(TMP_REG_1)));

	/* tear down stack, restore NVRs, ... */
	bpf_jit_emit_common_epilogue(image, ctx);

	EMIT(PPC_RAW_BCTR());

	/* out: */
	return 0;
}

/*
 * We spill into the redzone always, even if the bpf program has its own stackframe.
 * Offsets hardcoded based on BPF_PPC_STACK_SAVE -- see bpf_jit_stack_local()
 */
void bpf_stf_barrier(void);

asm (
"		.global bpf_stf_barrier		;"
"	bpf_stf_barrier:			;"
"		std	21,-64(1)		;"
"		std	22,-56(1)		;"
"		sync				;"
"		ld	21,-64(1)		;"
"		ld	22,-56(1)		;"
"		ori	31,31,0			;"
"		.rept 14			;"
"		b	1f			;"
"	1:					;"
"		.endr				;"
"		blr				;"
);

/* Assemble the body code between the prologue & epilogue */
int bpf_jit_build_body(struct bpf_prog *fp, u32 *image, struct codegen_context *ctx,
		       u32 *addrs, int pass)
{
	enum stf_barrier_type stf_barrier = stf_barrier_type_get();
	const struct bpf_insn *insn = fp->insnsi;
	int flen = fp->len;
	int i, ret;

	/* Start of epilogue code - will only be valid 2nd pass onwards */
	u32 exit_addr = addrs[flen];

	for (i = 0; i < flen; i++) {
		u32 code = insn[i].code;
		u32 dst_reg = bpf_to_ppc(insn[i].dst_reg);
		u32 src_reg = bpf_to_ppc(insn[i].src_reg);
		u32 size = BPF_SIZE(code);
		u32 tmp1_reg = bpf_to_ppc(TMP_REG_1);
		u32 tmp2_reg = bpf_to_ppc(TMP_REG_2);
		s16 off = insn[i].off;
		s32 imm = insn[i].imm;
		bool func_addr_fixed;
		u64 func_addr;
		u64 imm64;
		u32 true_cond;
		u32 tmp_idx;
		int j;

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
			bpf_set_seen_register(ctx, dst_reg);
		if (src_reg >= BPF_PPC_NVR_MIN && src_reg < 32)
			bpf_set_seen_register(ctx, src_reg);

		switch (code) {
		/*
		 * Arithmetic operations: ADD/SUB/MUL/DIV/MOD/NEG
		 */
		case BPF_ALU | BPF_ADD | BPF_X: /* (u32) dst += (u32) src */
		case BPF_ALU64 | BPF_ADD | BPF_X: /* dst += src */
			EMIT(PPC_RAW_ADD(dst_reg, dst_reg, src_reg));
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_SUB | BPF_X: /* (u32) dst -= (u32) src */
		case BPF_ALU64 | BPF_SUB | BPF_X: /* dst -= src */
			EMIT(PPC_RAW_SUB(dst_reg, dst_reg, src_reg));
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_ADD | BPF_K: /* (u32) dst += (u32) imm */
		case BPF_ALU64 | BPF_ADD | BPF_K: /* dst += imm */
			if (!imm) {
				goto bpf_alu32_trunc;
			} else if (imm >= -32768 && imm < 32768) {
				EMIT(PPC_RAW_ADDI(dst_reg, dst_reg, IMM_L(imm)));
			} else {
				PPC_LI32(tmp1_reg, imm);
				EMIT(PPC_RAW_ADD(dst_reg, dst_reg, tmp1_reg));
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_SUB | BPF_K: /* (u32) dst -= (u32) imm */
		case BPF_ALU64 | BPF_SUB | BPF_K: /* dst -= imm */
			if (!imm) {
				goto bpf_alu32_trunc;
			} else if (imm > -32768 && imm <= 32768) {
				EMIT(PPC_RAW_ADDI(dst_reg, dst_reg, IMM_L(-imm)));
			} else {
				PPC_LI32(tmp1_reg, imm);
				EMIT(PPC_RAW_SUB(dst_reg, dst_reg, tmp1_reg));
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_MUL | BPF_X: /* (u32) dst *= (u32) src */
		case BPF_ALU64 | BPF_MUL | BPF_X: /* dst *= src */
			if (BPF_CLASS(code) == BPF_ALU)
				EMIT(PPC_RAW_MULW(dst_reg, dst_reg, src_reg));
			else
				EMIT(PPC_RAW_MULD(dst_reg, dst_reg, src_reg));
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_MUL | BPF_K: /* (u32) dst *= (u32) imm */
		case BPF_ALU64 | BPF_MUL | BPF_K: /* dst *= imm */
			if (imm >= -32768 && imm < 32768)
				EMIT(PPC_RAW_MULI(dst_reg, dst_reg, IMM_L(imm)));
			else {
				PPC_LI32(tmp1_reg, imm);
				if (BPF_CLASS(code) == BPF_ALU)
					EMIT(PPC_RAW_MULW(dst_reg, dst_reg, tmp1_reg));
				else
					EMIT(PPC_RAW_MULD(dst_reg, dst_reg, tmp1_reg));
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_DIV | BPF_X: /* (u32) dst /= (u32) src */
		case BPF_ALU | BPF_MOD | BPF_X: /* (u32) dst %= (u32) src */
			if (BPF_OP(code) == BPF_MOD) {
				EMIT(PPC_RAW_DIVWU(tmp1_reg, dst_reg, src_reg));
				EMIT(PPC_RAW_MULW(tmp1_reg, src_reg, tmp1_reg));
				EMIT(PPC_RAW_SUB(dst_reg, dst_reg, tmp1_reg));
			} else
				EMIT(PPC_RAW_DIVWU(dst_reg, dst_reg, src_reg));
			goto bpf_alu32_trunc;
		case BPF_ALU64 | BPF_DIV | BPF_X: /* dst /= src */
		case BPF_ALU64 | BPF_MOD | BPF_X: /* dst %= src */
			if (BPF_OP(code) == BPF_MOD) {
				EMIT(PPC_RAW_DIVDU(tmp1_reg, dst_reg, src_reg));
				EMIT(PPC_RAW_MULD(tmp1_reg, src_reg, tmp1_reg));
				EMIT(PPC_RAW_SUB(dst_reg, dst_reg, tmp1_reg));
			} else
				EMIT(PPC_RAW_DIVDU(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU | BPF_MOD | BPF_K: /* (u32) dst %= (u32) imm */
		case BPF_ALU | BPF_DIV | BPF_K: /* (u32) dst /= (u32) imm */
		case BPF_ALU64 | BPF_MOD | BPF_K: /* dst %= imm */
		case BPF_ALU64 | BPF_DIV | BPF_K: /* dst /= imm */
			if (imm == 0)
				return -EINVAL;
			if (imm == 1) {
				if (BPF_OP(code) == BPF_DIV) {
					goto bpf_alu32_trunc;
				} else {
					EMIT(PPC_RAW_LI(dst_reg, 0));
					break;
				}
			}

			PPC_LI32(tmp1_reg, imm);
			switch (BPF_CLASS(code)) {
			case BPF_ALU:
				if (BPF_OP(code) == BPF_MOD) {
					EMIT(PPC_RAW_DIVWU(tmp2_reg, dst_reg, tmp1_reg));
					EMIT(PPC_RAW_MULW(tmp1_reg, tmp1_reg, tmp2_reg));
					EMIT(PPC_RAW_SUB(dst_reg, dst_reg, tmp1_reg));
				} else
					EMIT(PPC_RAW_DIVWU(dst_reg, dst_reg, tmp1_reg));
				break;
			case BPF_ALU64:
				if (BPF_OP(code) == BPF_MOD) {
					EMIT(PPC_RAW_DIVDU(tmp2_reg, dst_reg, tmp1_reg));
					EMIT(PPC_RAW_MULD(tmp1_reg, tmp1_reg, tmp2_reg));
					EMIT(PPC_RAW_SUB(dst_reg, dst_reg, tmp1_reg));
				} else
					EMIT(PPC_RAW_DIVDU(dst_reg, dst_reg, tmp1_reg));
				break;
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_NEG: /* (u32) dst = -dst */
		case BPF_ALU64 | BPF_NEG: /* dst = -dst */
			EMIT(PPC_RAW_NEG(dst_reg, dst_reg));
			goto bpf_alu32_trunc;

		/*
		 * Logical operations: AND/OR/XOR/[A]LSH/[A]RSH
		 */
		case BPF_ALU | BPF_AND | BPF_X: /* (u32) dst = dst & src */
		case BPF_ALU64 | BPF_AND | BPF_X: /* dst = dst & src */
			EMIT(PPC_RAW_AND(dst_reg, dst_reg, src_reg));
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_AND | BPF_K: /* (u32) dst = dst & imm */
		case BPF_ALU64 | BPF_AND | BPF_K: /* dst = dst & imm */
			if (!IMM_H(imm))
				EMIT(PPC_RAW_ANDI(dst_reg, dst_reg, IMM_L(imm)));
			else {
				/* Sign-extended */
				PPC_LI32(tmp1_reg, imm);
				EMIT(PPC_RAW_AND(dst_reg, dst_reg, tmp1_reg));
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_OR | BPF_X: /* dst = (u32) dst | (u32) src */
		case BPF_ALU64 | BPF_OR | BPF_X: /* dst = dst | src */
			EMIT(PPC_RAW_OR(dst_reg, dst_reg, src_reg));
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_OR | BPF_K:/* dst = (u32) dst | (u32) imm */
		case BPF_ALU64 | BPF_OR | BPF_K:/* dst = dst | imm */
			if (imm < 0 && BPF_CLASS(code) == BPF_ALU64) {
				/* Sign-extended */
				PPC_LI32(tmp1_reg, imm);
				EMIT(PPC_RAW_OR(dst_reg, dst_reg, tmp1_reg));
			} else {
				if (IMM_L(imm))
					EMIT(PPC_RAW_ORI(dst_reg, dst_reg, IMM_L(imm)));
				if (IMM_H(imm))
					EMIT(PPC_RAW_ORIS(dst_reg, dst_reg, IMM_H(imm)));
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_XOR | BPF_X: /* (u32) dst ^= src */
		case BPF_ALU64 | BPF_XOR | BPF_X: /* dst ^= src */
			EMIT(PPC_RAW_XOR(dst_reg, dst_reg, src_reg));
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_XOR | BPF_K: /* (u32) dst ^= (u32) imm */
		case BPF_ALU64 | BPF_XOR | BPF_K: /* dst ^= imm */
			if (imm < 0 && BPF_CLASS(code) == BPF_ALU64) {
				/* Sign-extended */
				PPC_LI32(tmp1_reg, imm);
				EMIT(PPC_RAW_XOR(dst_reg, dst_reg, tmp1_reg));
			} else {
				if (IMM_L(imm))
					EMIT(PPC_RAW_XORI(dst_reg, dst_reg, IMM_L(imm)));
				if (IMM_H(imm))
					EMIT(PPC_RAW_XORIS(dst_reg, dst_reg, IMM_H(imm)));
			}
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_LSH | BPF_X: /* (u32) dst <<= (u32) src */
			/* slw clears top 32 bits */
			EMIT(PPC_RAW_SLW(dst_reg, dst_reg, src_reg));
			/* skip zero extension move, but set address map. */
			if (insn_is_zext(&insn[i + 1]))
				addrs[++i] = ctx->idx * 4;
			break;
		case BPF_ALU64 | BPF_LSH | BPF_X: /* dst <<= src; */
			EMIT(PPC_RAW_SLD(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU | BPF_LSH | BPF_K: /* (u32) dst <<== (u32) imm */
			/* with imm 0, we still need to clear top 32 bits */
			EMIT(PPC_RAW_SLWI(dst_reg, dst_reg, imm));
			if (insn_is_zext(&insn[i + 1]))
				addrs[++i] = ctx->idx * 4;
			break;
		case BPF_ALU64 | BPF_LSH | BPF_K: /* dst <<== imm */
			if (imm != 0)
				EMIT(PPC_RAW_SLDI(dst_reg, dst_reg, imm));
			break;
		case BPF_ALU | BPF_RSH | BPF_X: /* (u32) dst >>= (u32) src */
			EMIT(PPC_RAW_SRW(dst_reg, dst_reg, src_reg));
			if (insn_is_zext(&insn[i + 1]))
				addrs[++i] = ctx->idx * 4;
			break;
		case BPF_ALU64 | BPF_RSH | BPF_X: /* dst >>= src */
			EMIT(PPC_RAW_SRD(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU | BPF_RSH | BPF_K: /* (u32) dst >>= (u32) imm */
			EMIT(PPC_RAW_SRWI(dst_reg, dst_reg, imm));
			if (insn_is_zext(&insn[i + 1]))
				addrs[++i] = ctx->idx * 4;
			break;
		case BPF_ALU64 | BPF_RSH | BPF_K: /* dst >>= imm */
			if (imm != 0)
				EMIT(PPC_RAW_SRDI(dst_reg, dst_reg, imm));
			break;
		case BPF_ALU | BPF_ARSH | BPF_X: /* (s32) dst >>= src */
			EMIT(PPC_RAW_SRAW(dst_reg, dst_reg, src_reg));
			goto bpf_alu32_trunc;
		case BPF_ALU64 | BPF_ARSH | BPF_X: /* (s64) dst >>= src */
			EMIT(PPC_RAW_SRAD(dst_reg, dst_reg, src_reg));
			break;
		case BPF_ALU | BPF_ARSH | BPF_K: /* (s32) dst >>= imm */
			EMIT(PPC_RAW_SRAWI(dst_reg, dst_reg, imm));
			goto bpf_alu32_trunc;
		case BPF_ALU64 | BPF_ARSH | BPF_K: /* (s64) dst >>= imm */
			if (imm != 0)
				EMIT(PPC_RAW_SRADI(dst_reg, dst_reg, imm));
			break;

		/*
		 * MOV
		 */
		case BPF_ALU | BPF_MOV | BPF_X: /* (u32) dst = src */
		case BPF_ALU64 | BPF_MOV | BPF_X: /* dst = src */
			if (imm == 1) {
				/* special mov32 for zext */
				EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, 0, 0, 31));
				break;
			}
			EMIT(PPC_RAW_MR(dst_reg, src_reg));
			goto bpf_alu32_trunc;
		case BPF_ALU | BPF_MOV | BPF_K: /* (u32) dst = imm */
		case BPF_ALU64 | BPF_MOV | BPF_K: /* dst = (s64) imm */
			PPC_LI32(dst_reg, imm);
			if (imm < 0)
				goto bpf_alu32_trunc;
			else if (insn_is_zext(&insn[i + 1]))
				addrs[++i] = ctx->idx * 4;
			break;

bpf_alu32_trunc:
		/* Truncate to 32-bits */
		if (BPF_CLASS(code) == BPF_ALU && !fp->aux->verifier_zext)
			EMIT(PPC_RAW_RLWINM(dst_reg, dst_reg, 0, 0, 31));
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
				EMIT(PPC_RAW_RLWINM(tmp1_reg, dst_reg, 8, 16, 23));
				/* Rotate 8 bits right & insert LSB to reg */
				EMIT(PPC_RAW_RLWIMI(tmp1_reg, dst_reg, 24, 24, 31));
				/* Move result back to dst_reg */
				EMIT(PPC_RAW_MR(dst_reg, tmp1_reg));
				break;
			case 32:
				/*
				 * Rotate word left by 8 bits:
				 * 2 bytes are already in their final position
				 * -- byte 2 and 4 (of bytes 1, 2, 3 and 4)
				 */
				EMIT(PPC_RAW_RLWINM(tmp1_reg, dst_reg, 8, 0, 31));
				/* Rotate 24 bits and insert byte 1 */
				EMIT(PPC_RAW_RLWIMI(tmp1_reg, dst_reg, 24, 0, 7));
				/* Rotate 24 bits and insert byte 3 */
				EMIT(PPC_RAW_RLWIMI(tmp1_reg, dst_reg, 24, 16, 23));
				EMIT(PPC_RAW_MR(dst_reg, tmp1_reg));
				break;
			case 64:
				/* Store the value to stack and then use byte-reverse loads */
				EMIT(PPC_RAW_STD(dst_reg, _R1, bpf_jit_stack_local(ctx)));
				EMIT(PPC_RAW_ADDI(tmp1_reg, _R1, bpf_jit_stack_local(ctx)));
				if (cpu_has_feature(CPU_FTR_ARCH_206)) {
					EMIT(PPC_RAW_LDBRX(dst_reg, 0, tmp1_reg));
				} else {
					EMIT(PPC_RAW_LWBRX(dst_reg, 0, tmp1_reg));
					if (IS_ENABLED(CONFIG_CPU_LITTLE_ENDIAN))
						EMIT(PPC_RAW_SLDI(dst_reg, dst_reg, 32));
					EMIT(PPC_RAW_LI(tmp2_reg, 4));
					EMIT(PPC_RAW_LWBRX(tmp2_reg, tmp2_reg, tmp1_reg));
					if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
						EMIT(PPC_RAW_SLDI(tmp2_reg, tmp2_reg, 32));
					EMIT(PPC_RAW_OR(dst_reg, dst_reg, tmp2_reg));
				}
				break;
			}
			break;

emit_clear:
			switch (imm) {
			case 16:
				/* zero-extend 16 bits into 64 bits */
				EMIT(PPC_RAW_RLDICL(dst_reg, dst_reg, 0, 48));
				if (insn_is_zext(&insn[i + 1]))
					addrs[++i] = ctx->idx * 4;
				break;
			case 32:
				if (!fp->aux->verifier_zext)
					/* zero-extend 32 bits into 64 bits */
					EMIT(PPC_RAW_RLDICL(dst_reg, dst_reg, 0, 32));
				break;
			case 64:
				/* nop */
				break;
			}
			break;

		/*
		 * BPF_ST NOSPEC (speculation barrier)
		 */
		case BPF_ST | BPF_NOSPEC:
			if (!security_ftr_enabled(SEC_FTR_FAVOUR_SECURITY) ||
					!security_ftr_enabled(SEC_FTR_STF_BARRIER))
				break;

			switch (stf_barrier) {
			case STF_BARRIER_EIEIO:
				EMIT(PPC_RAW_EIEIO() | 0x02000000);
				break;
			case STF_BARRIER_SYNC_ORI:
				EMIT(PPC_RAW_SYNC());
				EMIT(PPC_RAW_LD(tmp1_reg, _R13, 0));
				EMIT(PPC_RAW_ORI(_R31, _R31, 0));
				break;
			case STF_BARRIER_FALLBACK:
				ctx->seen |= SEEN_FUNC;
				PPC_LI64(_R12, dereference_kernel_function_descriptor(bpf_stf_barrier));
				EMIT(PPC_RAW_MTCTR(_R12));
				EMIT(PPC_RAW_BCTRL());
				break;
			case STF_BARRIER_NONE:
				break;
			}
			break;

		/*
		 * BPF_ST(X)
		 */
		case BPF_STX | BPF_MEM | BPF_B: /* *(u8 *)(dst + off) = src */
		case BPF_ST | BPF_MEM | BPF_B: /* *(u8 *)(dst + off) = imm */
			if (BPF_CLASS(code) == BPF_ST) {
				EMIT(PPC_RAW_LI(tmp1_reg, imm));
				src_reg = tmp1_reg;
			}
			EMIT(PPC_RAW_STB(src_reg, dst_reg, off));
			break;
		case BPF_STX | BPF_MEM | BPF_H: /* (u16 *)(dst + off) = src */
		case BPF_ST | BPF_MEM | BPF_H: /* (u16 *)(dst + off) = imm */
			if (BPF_CLASS(code) == BPF_ST) {
				EMIT(PPC_RAW_LI(tmp1_reg, imm));
				src_reg = tmp1_reg;
			}
			EMIT(PPC_RAW_STH(src_reg, dst_reg, off));
			break;
		case BPF_STX | BPF_MEM | BPF_W: /* *(u32 *)(dst + off) = src */
		case BPF_ST | BPF_MEM | BPF_W: /* *(u32 *)(dst + off) = imm */
			if (BPF_CLASS(code) == BPF_ST) {
				PPC_LI32(tmp1_reg, imm);
				src_reg = tmp1_reg;
			}
			EMIT(PPC_RAW_STW(src_reg, dst_reg, off));
			break;
		case BPF_STX | BPF_MEM | BPF_DW: /* (u64 *)(dst + off) = src */
		case BPF_ST | BPF_MEM | BPF_DW: /* *(u64 *)(dst + off) = imm */
			if (BPF_CLASS(code) == BPF_ST) {
				PPC_LI32(tmp1_reg, imm);
				src_reg = tmp1_reg;
			}
			if (off % 4) {
				EMIT(PPC_RAW_LI(tmp2_reg, off));
				EMIT(PPC_RAW_STDX(src_reg, dst_reg, tmp2_reg));
			} else {
				EMIT(PPC_RAW_STD(src_reg, dst_reg, off));
			}
			break;

		/*
		 * BPF_STX ATOMIC (atomic ops)
		 */
		case BPF_STX | BPF_ATOMIC | BPF_W:
			if (imm != BPF_ADD) {
				pr_err_ratelimited(
					"eBPF filter atomic op code %02x (@%d) unsupported\n",
					code, i);
				return -ENOTSUPP;
			}

			/* *(u32 *)(dst + off) += src */

			/* Get EA into TMP_REG_1 */
			EMIT(PPC_RAW_ADDI(tmp1_reg, dst_reg, off));
			tmp_idx = ctx->idx * 4;
			/* load value from memory into TMP_REG_2 */
			EMIT(PPC_RAW_LWARX(tmp2_reg, 0, tmp1_reg, 0));
			/* add value from src_reg into this */
			EMIT(PPC_RAW_ADD(tmp2_reg, tmp2_reg, src_reg));
			/* store result back */
			EMIT(PPC_RAW_STWCX(tmp2_reg, 0, tmp1_reg));
			/* we're done if this succeeded */
			PPC_BCC_SHORT(COND_NE, tmp_idx);
			break;
		case BPF_STX | BPF_ATOMIC | BPF_DW:
			if (imm != BPF_ADD) {
				pr_err_ratelimited(
					"eBPF filter atomic op code %02x (@%d) unsupported\n",
					code, i);
				return -ENOTSUPP;
			}
			/* *(u64 *)(dst + off) += src */

			EMIT(PPC_RAW_ADDI(tmp1_reg, dst_reg, off));
			tmp_idx = ctx->idx * 4;
			EMIT(PPC_RAW_LDARX(tmp2_reg, 0, tmp1_reg, 0));
			EMIT(PPC_RAW_ADD(tmp2_reg, tmp2_reg, src_reg));
			EMIT(PPC_RAW_STDCX(tmp2_reg, 0, tmp1_reg));
			PPC_BCC_SHORT(COND_NE, tmp_idx);
			break;

		/*
		 * BPF_LDX
		 */
		/* dst = *(u8 *)(ul) (src + off) */
		case BPF_LDX | BPF_MEM | BPF_B:
		case BPF_LDX | BPF_PROBE_MEM | BPF_B:
		/* dst = *(u16 *)(ul) (src + off) */
		case BPF_LDX | BPF_MEM | BPF_H:
		case BPF_LDX | BPF_PROBE_MEM | BPF_H:
		/* dst = *(u32 *)(ul) (src + off) */
		case BPF_LDX | BPF_MEM | BPF_W:
		case BPF_LDX | BPF_PROBE_MEM | BPF_W:
		/* dst = *(u64 *)(ul) (src + off) */
		case BPF_LDX | BPF_MEM | BPF_DW:
		case BPF_LDX | BPF_PROBE_MEM | BPF_DW:
			/*
			 * As PTR_TO_BTF_ID that uses BPF_PROBE_MEM mode could either be a valid
			 * kernel pointer or NULL but not a userspace address, execute BPF_PROBE_MEM
			 * load only if addr is kernel address (see is_kernel_addr()), otherwise
			 * set dst_reg=0 and move on.
			 */
			if (BPF_MODE(code) == BPF_PROBE_MEM) {
				EMIT(PPC_RAW_ADDI(tmp1_reg, src_reg, off));
				if (IS_ENABLED(CONFIG_PPC_BOOK3E_64))
					PPC_LI64(tmp2_reg, 0x8000000000000000ul);
				else /* BOOK3S_64 */
					PPC_LI64(tmp2_reg, PAGE_OFFSET);
				EMIT(PPC_RAW_CMPLD(tmp1_reg, tmp2_reg));
				PPC_BCC_SHORT(COND_GT, (ctx->idx + 3) * 4);
				EMIT(PPC_RAW_LI(dst_reg, 0));
				/*
				 * Check if 'off' is word aligned for BPF_DW, because
				 * we might generate two instructions.
				 */
				if (BPF_SIZE(code) == BPF_DW && (off & 3))
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
				if (off % 4) {
					EMIT(PPC_RAW_LI(tmp1_reg, off));
					EMIT(PPC_RAW_LDX(dst_reg, src_reg, tmp1_reg));
				} else {
					EMIT(PPC_RAW_LD(dst_reg, src_reg, off));
				}
				break;
			}

			if (size != BPF_DW && insn_is_zext(&insn[i + 1]))
				addrs[++i] = ctx->idx * 4;

			if (BPF_MODE(code) == BPF_PROBE_MEM) {
				ret = bpf_add_extable_entry(fp, image, pass, ctx, ctx->idx - 1,
							    4, dst_reg);
				if (ret)
					return ret;
			}
			break;

		/*
		 * Doubleword load
		 * 16 byte instruction that uses two 'struct bpf_insn'
		 */
		case BPF_LD | BPF_IMM | BPF_DW: /* dst = (u64) imm */
			imm64 = ((u64)(u32) insn[i].imm) |
				    (((u64)(u32) insn[i+1].imm) << 32);
			tmp_idx = ctx->idx;
			PPC_LI64(dst_reg, imm64);
			/* padding to allow full 5 instructions for later patching */
			for (j = ctx->idx - tmp_idx; j < 5; j++)
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
			if (i != flen - 1) {
				ret = bpf_jit_emit_exit_insn(image, ctx, tmp1_reg, exit_addr);
				if (ret)
					return ret;
			}
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

			if (func_addr_fixed)
				ret = bpf_jit_emit_func_call_hlp(image, ctx, func_addr);
			else
				ret = bpf_jit_emit_func_call_rel(image, ctx, func_addr);

			if (ret)
				return ret;

			/* move return value from r3 to BPF_REG_0 */
			EMIT(PPC_RAW_MR(bpf_to_ppc(BPF_REG_0), _R3));
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
			/* Fall through */

cond_branch:
			switch (code) {
			case BPF_JMP | BPF_JGT | BPF_X:
			case BPF_JMP | BPF_JLT | BPF_X:
			case BPF_JMP | BPF_JGE | BPF_X:
			case BPF_JMP | BPF_JLE | BPF_X:
			case BPF_JMP | BPF_JEQ | BPF_X:
			case BPF_JMP | BPF_JNE | BPF_X:
			case BPF_JMP32 | BPF_JGT | BPF_X:
			case BPF_JMP32 | BPF_JLT | BPF_X:
			case BPF_JMP32 | BPF_JGE | BPF_X:
			case BPF_JMP32 | BPF_JLE | BPF_X:
			case BPF_JMP32 | BPF_JEQ | BPF_X:
			case BPF_JMP32 | BPF_JNE | BPF_X:
				/* unsigned comparison */
				if (BPF_CLASS(code) == BPF_JMP32)
					EMIT(PPC_RAW_CMPLW(dst_reg, src_reg));
				else
					EMIT(PPC_RAW_CMPLD(dst_reg, src_reg));
				break;
			case BPF_JMP | BPF_JSGT | BPF_X:
			case BPF_JMP | BPF_JSLT | BPF_X:
			case BPF_JMP | BPF_JSGE | BPF_X:
			case BPF_JMP | BPF_JSLE | BPF_X:
			case BPF_JMP32 | BPF_JSGT | BPF_X:
			case BPF_JMP32 | BPF_JSLT | BPF_X:
			case BPF_JMP32 | BPF_JSGE | BPF_X:
			case BPF_JMP32 | BPF_JSLE | BPF_X:
				/* signed comparison */
				if (BPF_CLASS(code) == BPF_JMP32)
					EMIT(PPC_RAW_CMPW(dst_reg, src_reg));
				else
					EMIT(PPC_RAW_CMPD(dst_reg, src_reg));
				break;
			case BPF_JMP | BPF_JSET | BPF_X:
			case BPF_JMP32 | BPF_JSET | BPF_X:
				if (BPF_CLASS(code) == BPF_JMP) {
					EMIT(PPC_RAW_AND_DOT(tmp1_reg, dst_reg, src_reg));
				} else {
					EMIT(PPC_RAW_AND(tmp1_reg, dst_reg, src_reg));
					EMIT(PPC_RAW_RLWINM_DOT(tmp1_reg, tmp1_reg, 0, 0, 31));
				}
				break;
			case BPF_JMP | BPF_JNE | BPF_K:
			case BPF_JMP | BPF_JEQ | BPF_K:
			case BPF_JMP | BPF_JGT | BPF_K:
			case BPF_JMP | BPF_JLT | BPF_K:
			case BPF_JMP | BPF_JGE | BPF_K:
			case BPF_JMP | BPF_JLE | BPF_K:
			case BPF_JMP32 | BPF_JNE | BPF_K:
			case BPF_JMP32 | BPF_JEQ | BPF_K:
			case BPF_JMP32 | BPF_JGT | BPF_K:
			case BPF_JMP32 | BPF_JLT | BPF_K:
			case BPF_JMP32 | BPF_JGE | BPF_K:
			case BPF_JMP32 | BPF_JLE | BPF_K:
			{
				bool is_jmp32 = BPF_CLASS(code) == BPF_JMP32;

				/*
				 * Need sign-extended load, so only positive
				 * values can be used as imm in cmpldi
				 */
				if (imm >= 0 && imm < 32768) {
					if (is_jmp32)
						EMIT(PPC_RAW_CMPLWI(dst_reg, imm));
					else
						EMIT(PPC_RAW_CMPLDI(dst_reg, imm));
				} else {
					/* sign-extending load */
					PPC_LI32(tmp1_reg, imm);
					/* ... but unsigned comparison */
					if (is_jmp32)
						EMIT(PPC_RAW_CMPLW(dst_reg, tmp1_reg));
					else
						EMIT(PPC_RAW_CMPLD(dst_reg, tmp1_reg));
				}
				break;
			}
			case BPF_JMP | BPF_JSGT | BPF_K:
			case BPF_JMP | BPF_JSLT | BPF_K:
			case BPF_JMP | BPF_JSGE | BPF_K:
			case BPF_JMP | BPF_JSLE | BPF_K:
			case BPF_JMP32 | BPF_JSGT | BPF_K:
			case BPF_JMP32 | BPF_JSLT | BPF_K:
			case BPF_JMP32 | BPF_JSGE | BPF_K:
			case BPF_JMP32 | BPF_JSLE | BPF_K:
			{
				bool is_jmp32 = BPF_CLASS(code) == BPF_JMP32;

				/*
				 * signed comparison, so any 16-bit value
				 * can be used in cmpdi
				 */
				if (imm >= -32768 && imm < 32768) {
					if (is_jmp32)
						EMIT(PPC_RAW_CMPWI(dst_reg, imm));
					else
						EMIT(PPC_RAW_CMPDI(dst_reg, imm));
				} else {
					PPC_LI32(tmp1_reg, imm);
					if (is_jmp32)
						EMIT(PPC_RAW_CMPW(dst_reg, tmp1_reg));
					else
						EMIT(PPC_RAW_CMPD(dst_reg, tmp1_reg));
				}
				break;
			}
			case BPF_JMP | BPF_JSET | BPF_K:
			case BPF_JMP32 | BPF_JSET | BPF_K:
				/* andi does not sign-extend the immediate */
				if (imm >= 0 && imm < 32768)
					/* PPC_ANDI is _only/always_ dot-form */
					EMIT(PPC_RAW_ANDI(tmp1_reg, dst_reg, imm));
				else {
					PPC_LI32(tmp1_reg, imm);
					if (BPF_CLASS(code) == BPF_JMP) {
						EMIT(PPC_RAW_AND_DOT(tmp1_reg, dst_reg,
								     tmp1_reg));
					} else {
						EMIT(PPC_RAW_AND(tmp1_reg, dst_reg, tmp1_reg));
						EMIT(PPC_RAW_RLWINM_DOT(tmp1_reg, tmp1_reg,
									0, 0, 31));
					}
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
			pr_err_ratelimited("eBPF filter opcode %04x (@%d) unsupported\n",
					code, i);
			return -ENOTSUPP;
		}
	}

	/* Set end-of-body-code address for exit. */
	addrs[i] = ctx->idx * 4;

	return 0;
}
