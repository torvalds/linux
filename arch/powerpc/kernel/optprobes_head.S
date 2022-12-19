/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Code to prepare detour buffer for optprobes in Kernel.
 *
 * Copyright 2017, Anju T, IBM Corp.
 */

#include <asm/ppc_asm.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>

#ifdef CONFIG_PPC64
#define SAVE_30GPRS(base) SAVE_GPRS(2, 31, base)
#define REST_30GPRS(base) REST_GPRS(2, 31, base)
#define TEMPLATE_FOR_IMM_LOAD_INSNS	nop; nop; nop; nop; nop
#else
#define SAVE_30GPRS(base) stmw	r2, GPR2(base)
#define REST_30GPRS(base) lmw	r2, GPR2(base)
#define TEMPLATE_FOR_IMM_LOAD_INSNS	nop; nop; nop
#endif

#define	OPT_SLOT_SIZE	65536

	.balign	4

	/*
	 * Reserve an area to allocate slots for detour buffer.
	 * This is part of .text section (rather than vmalloc area)
	 * as this needs to be within 32MB of the probed address.
	 */
	.global optinsn_slot
optinsn_slot:
	.space	OPT_SLOT_SIZE

	/*
	 * Optprobe template:
	 * This template gets copied into one of the slots in optinsn_slot
	 * and gets fixed up with real optprobe structures et al.
	 */
	.global optprobe_template_entry
optprobe_template_entry:
	/* Create an in-memory pt_regs */
	PPC_STLU	r1,-INT_FRAME_SIZE(r1)
	SAVE_GPR(0,r1)
	/* Save the previous SP into stack */
	addi	r0,r1,INT_FRAME_SIZE
	PPC_STL	r0,GPR1(r1)
	SAVE_30GPRS(r1)
	/* Save SPRS */
	mfmsr	r5
	PPC_STL	r5,_MSR(r1)
	li	r5,0x700
	PPC_STL	r5,_TRAP(r1)
	li	r5,0
	PPC_STL	r5,ORIG_GPR3(r1)
	PPC_STL	r5,RESULT(r1)
	mfctr	r5
	PPC_STL	r5,_CTR(r1)
	mflr	r5
	PPC_STL	r5,_LINK(r1)
	mfspr	r5,SPRN_XER
	PPC_STL	r5,_XER(r1)
	mfcr	r5
	PPC_STL	r5,_CCR(r1)
#ifdef CONFIG_PPC64
	lbz     r5,PACAIRQSOFTMASK(r13)
	std     r5,SOFTE(r1)
#endif

	/*
	 * We may get here from a module, so load the kernel TOC in r2.
	 * The original TOC gets restored when pt_regs is restored
	 * further below.
	 */
#ifdef CONFIG_PPC64
	LOAD_PACA_TOC()
#endif

	.global optprobe_template_op_address
optprobe_template_op_address:
	/*
	 * Parameters to optimized_callback():
	 * 1. optimized_kprobe structure in r3
	 */
	TEMPLATE_FOR_IMM_LOAD_INSNS

	/* 2. pt_regs pointer in r4 */
	addi	r4,r1,STACK_INT_FRAME_REGS

	.global optprobe_template_call_handler
optprobe_template_call_handler:
	/* Branch to optimized_callback() */
	nop

	/*
	 * Parameters for instruction emulation:
	 * 1. Pass SP in register r3.
	 */
	addi	r3,r1,STACK_INT_FRAME_REGS

	.global optprobe_template_insn
optprobe_template_insn:
	/* 2, Pass instruction to be emulated in r4 */
	TEMPLATE_FOR_IMM_LOAD_INSNS

	.global optprobe_template_call_emulate
optprobe_template_call_emulate:
	/* Branch to emulate_step()  */
	nop

	/*
	 * All done.
	 * Now, restore the registers...
	 */
	PPC_LL	r5,_MSR(r1)
	mtmsr	r5
	PPC_LL	r5,_CTR(r1)
	mtctr	r5
	PPC_LL	r5,_LINK(r1)
	mtlr	r5
	PPC_LL	r5,_XER(r1)
	mtxer	r5
	PPC_LL	r5,_CCR(r1)
	mtcr	r5
	REST_GPR(0,r1)
	REST_30GPRS(r1)
	/* Restore the previous SP */
	addi	r1,r1,INT_FRAME_SIZE

	.global optprobe_template_ret
optprobe_template_ret:
	/* ... and jump back from trampoline */
	nop

	.global optprobe_template_end
optprobe_template_end:
