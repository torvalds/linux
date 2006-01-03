/*
 * Copyright (C) 1996 Paul Mackerras.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * NOTE: assert(sizeof(buf) > 23 * sizeof(long))
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>

_GLOBAL(xmon_setjmp)
	mflr	r0
	PPC_STL	r0,0(r3)
	PPC_STL	r1,SZL(r3)
	PPC_STL	r2,2*SZL(r3)
	mfcr	r0
	PPC_STL	r0,3*SZL(r3)
	PPC_STL	r13,4*SZL(r3)
	PPC_STL	r14,5*SZL(r3)
	PPC_STL	r15,6*SZL(r3)
	PPC_STL	r16,7*SZL(r3)
	PPC_STL	r17,8*SZL(r3)
	PPC_STL	r18,9*SZL(r3)
	PPC_STL	r19,10*SZL(r3)
	PPC_STL	r20,11*SZL(r3)
	PPC_STL	r21,12*SZL(r3)
	PPC_STL	r22,13*SZL(r3)
	PPC_STL	r23,14*SZL(r3)
	PPC_STL	r24,15*SZL(r3)
	PPC_STL	r25,16*SZL(r3)
	PPC_STL	r26,17*SZL(r3)
	PPC_STL	r27,18*SZL(r3)
	PPC_STL	r28,19*SZL(r3)
	PPC_STL	r29,20*SZL(r3)
	PPC_STL	r30,21*SZL(r3)
	PPC_STL	r31,22*SZL(r3)
	li	r3,0
	blr

_GLOBAL(xmon_longjmp)
	PPC_LCMPI r4,0
	bne	1f
	li	r4,1
1:	PPC_LL	r13,4*SZL(r3)
	PPC_LL	r14,5*SZL(r3)
	PPC_LL	r15,6*SZL(r3)
	PPC_LL	r16,7*SZL(r3)
	PPC_LL	r17,8*SZL(r3)
	PPC_LL	r18,9*SZL(r3)
	PPC_LL	r19,10*SZL(r3)
	PPC_LL	r20,11*SZL(r3)
	PPC_LL	r21,12*SZL(r3)
	PPC_LL	r22,13*SZL(r3)
	PPC_LL	r23,14*SZL(r3)
	PPC_LL	r24,15*SZL(r3)
	PPC_LL	r25,16*SZL(r3)
	PPC_LL	r26,17*SZL(r3)
	PPC_LL	r27,18*SZL(r3)
	PPC_LL	r28,19*SZL(r3)
	PPC_LL	r29,20*SZL(r3)
	PPC_LL	r30,21*SZL(r3)
	PPC_LL	r31,22*SZL(r3)
	PPC_LL	r0,3*SZL(r3)
	mtcrf	0x38,r0
	PPC_LL	r0,0(r3)
	PPC_LL	r1,SZL(r3)
	PPC_LL	r2,2*SZL(r3)
	mtlr	r0
	mr	r3,r4
	blr

/*
 * Grab the register values as they are now.
 * This won't do a particularily good job because we really
 * want our caller's caller's registers, and our caller has
 * already executed its prologue.
 * ToDo: We could reach back into the caller's save area to do
 * a better job of representing the caller's state (note that
 * that will be different for 32-bit and 64-bit, because of the
 * different ABIs, though).
 */
_GLOBAL(xmon_save_regs)
	PPC_STL	r0,0*SZL(r3)
	PPC_STL	r2,2*SZL(r3)
	PPC_STL	r3,3*SZL(r3)
	PPC_STL	r4,4*SZL(r3)
	PPC_STL	r5,5*SZL(r3)
	PPC_STL	r6,6*SZL(r3)
	PPC_STL	r7,7*SZL(r3)
	PPC_STL	r8,8*SZL(r3)
	PPC_STL	r9,9*SZL(r3)
	PPC_STL	r10,10*SZL(r3)
	PPC_STL	r11,11*SZL(r3)
	PPC_STL	r12,12*SZL(r3)
	PPC_STL	r13,13*SZL(r3)
	PPC_STL	r14,14*SZL(r3)
	PPC_STL	r15,15*SZL(r3)
	PPC_STL	r16,16*SZL(r3)
	PPC_STL	r17,17*SZL(r3)
	PPC_STL	r18,18*SZL(r3)
	PPC_STL	r19,19*SZL(r3)
	PPC_STL	r20,20*SZL(r3)
	PPC_STL	r21,21*SZL(r3)
	PPC_STL	r22,22*SZL(r3)
	PPC_STL	r23,23*SZL(r3)
	PPC_STL	r24,24*SZL(r3)
	PPC_STL	r25,25*SZL(r3)
	PPC_STL	r26,26*SZL(r3)
	PPC_STL	r27,27*SZL(r3)
	PPC_STL	r28,28*SZL(r3)
	PPC_STL	r29,29*SZL(r3)
	PPC_STL	r30,30*SZL(r3)
	PPC_STL	r31,31*SZL(r3)
	/* go up one stack frame for SP */
	PPC_LL	r4,0(r1)
	PPC_STL	r4,1*SZL(r3)
	/* get caller's LR */
	PPC_LL	r0,LRSAVE(r4)
	PPC_STL	r0,_NIP-STACK_FRAME_OVERHEAD(r3)
	PPC_STL	r0,_LINK-STACK_FRAME_OVERHEAD(r3)
	mfmsr	r0
	PPC_STL	r0,_MSR-STACK_FRAME_OVERHEAD(r3)
	mfctr	r0
	PPC_STL	r0,_CTR-STACK_FRAME_OVERHEAD(r3)
	mfxer	r0
	PPC_STL	r0,_XER-STACK_FRAME_OVERHEAD(r3)
	mfcr	r0
	PPC_STL	r0,_CCR-STACK_FRAME_OVERHEAD(r3)
	li	r0,0
	PPC_STL	r0,_TRAP-STACK_FRAME_OVERHEAD(r3)
	blr
