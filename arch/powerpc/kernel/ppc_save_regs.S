/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 1996 Paul Mackerras.
 *
 * NOTE: assert(sizeof(buf) > 23 * sizeof(long))
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/asm-compat.h>

/*
 * Grab the register values as they are now.
 * This won't do a particularly good job because we really
 * want our caller's caller's registers, and our caller has
 * already executed its prologue.
 * ToDo: We could reach back into the caller's save area to do
 * a better job of representing the caller's state (note that
 * that will be different for 32-bit and 64-bit, because of the
 * different ABIs, though).
 */
_GLOBAL(ppc_save_regs)
	/* This allows stack frame accessor macros and offsets to be used */
	subi	r3,r3,STACK_INT_FRAME_REGS
	PPC_STL	r0,GPR0(r3)
#ifdef CONFIG_PPC32
	stmw	r2,GPR2(r3)
#else
	SAVE_GPRS(2, 31, r3)
	lbz	r0,PACAIRQSOFTMASK(r13)
	PPC_STL	r0,SOFTE(r3)
#endif
	/* go up one stack frame for SP */
	PPC_LL	r4,0(r1)
	PPC_STL	r4,GPR1(r3)
	/* get caller's LR */
	PPC_LL	r0,LRSAVE(r4)
	PPC_STL	r0,_LINK(r3)
	mflr	r0
	PPC_STL	r0,_NIP(r3)
	mfmsr	r0
	PPC_STL	r0,_MSR(r3)
	mfctr	r0
	PPC_STL	r0,_CTR(r3)
	mfxer	r0
	PPC_STL	r0,_XER(r3)
	mfcr	r0
	PPC_STL	r0,_CCR(r3)
	li	r0,0
	PPC_STL	r0,_TRAP(r3)
	PPC_STL	r0,ORIG_GPR3(r3)
	blr
