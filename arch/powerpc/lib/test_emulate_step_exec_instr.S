/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Non-emulated single-stepping support (currently limited to basic integer
 * computations) used to validate the instruction emulation infrastructure.
 *
 * Copyright (C) 2019 IBM Corporation
 */

#include <asm/asm-offsets.h>
#include <asm/ppc_asm.h>
#include <asm/code-patching-asm.h>
#include <linux/errno.h>

/* int exec_instr(struct pt_regs *regs) */
_GLOBAL(exec_instr)

	/*
	 * Stack frame layout (INT_FRAME_SIZE bytes)
	 *   In-memory pt_regs	(SP + STACK_INT_FRAME_REGS)
	 *   Scratch space	(SP + 8)
	 *   Back chain		(SP + 0)
	 */

	/*
	 * Allocate a new stack frame with enough space to hold the register
	 * states in an in-memory pt_regs and also create the back chain to
	 * the caller's stack frame.
	 */
	stdu	r1, -INT_FRAME_SIZE(r1)

	/*
	 * Save non-volatile GPRs on stack. This includes TOC pointer (GPR2)
	 * and local variables (GPR14 to GPR31). The register for the pt_regs
	 * parameter (GPR3) is saved additionally to ensure that the resulting
	 * register state can still be saved even if GPR3 gets overwritten
	 * when loading the initial register state for the test instruction.
	 * The stack pointer (GPR1) and the thread pointer (GPR13) are not
	 * saved as these should not be modified anyway.
	 */
	SAVE_GPRS(2, 3, r1)
	SAVE_NVGPRS(r1)

	/*
	 * Save LR on stack to ensure that the return address is available
	 * even if it gets overwritten by the test instruction.
	 */
	mflr	r0
	std	r0, _LINK(r1)

	/*
	 * Save CR on stack. For simplicity, the entire register is saved
	 * even though only fields 2 to 4 are non-volatile.
	 */
	mfcr	r0
	std	r0, _CCR(r1)

	/*
	 * Load register state for the test instruction without touching the
	 * critical non-volatile registers. The register state is passed as a
	 * pointer to a pt_regs instance.
	 */
	subi	r31, r3, GPR0

	/* Load LR from pt_regs */
	ld	r0, _LINK(r31)
	mtlr	r0

	/* Load CR from pt_regs */
	ld	r0, _CCR(r31)
	mtcr	r0

	/* Load XER from pt_regs */
	ld	r0, _XER(r31)
	mtxer	r0

	/* Load GPRs from pt_regs */
	REST_GPR(0, r31)
	REST_GPRS(2, 12, r31)
	REST_NVGPRS(r31)

	/* Placeholder for the test instruction */
	.balign 64
1:	nop
	nop
	patch_site 1b patch__exec_instr

	/*
	 * Since GPR3 is overwritten, temporarily restore it back to its
	 * original state, i.e. the pointer to pt_regs, to ensure that the
	 * resulting register state can be saved. Before doing this, a copy
	 * of it is created in the scratch space which is used later on to
	 * save it to pt_regs.
	 */
	std	r3, 8(r1)
	REST_GPR(3, r1)

	/* Save resulting GPR state to pt_regs */
	subi	r3, r3, GPR0
	SAVE_GPR(0, r3)
	SAVE_GPR(2, r3)
	SAVE_GPRS(4, 12, r3)
	SAVE_NVGPRS(r3)

	/* Save resulting LR to pt_regs */
	mflr	r0
	std	r0, _LINK(r3)

	/* Save resulting CR to pt_regs */
	mfcr	r0
	std	r0, _CCR(r3)

	/* Save resulting XER to pt_regs */
	mfxer	r0
	std	r0, _XER(r3)

	/* Restore resulting GPR3 from scratch space and save it to pt_regs */
	ld	r0, 8(r1)
	std	r0, GPR3(r3)

	/* Set return value to denote execution success */
	li	r3, 0

	/* Continue */
	b	3f

	/* Set return value to denote execution failure */
2:	li	r3, -EFAULT

	/* Restore the non-volatile GPRs from stack */
3:	REST_GPR(2, r1)
	REST_NVGPRS(r1)

	/* Restore LR from stack to be able to return */
	ld	r0, _LINK(r1)
	mtlr	r0

	/* Restore CR from stack */
	ld	r0, _CCR(r1)
	mtcr	r0

	/* Tear down stack frame */
	addi	r1, r1, INT_FRAME_SIZE

	/* Return */
	blr

	/* Setup exception table */
	EX_TABLE(1b, 2b)

_ASM_NOKPROBE_SYMBOL(exec_instr)
