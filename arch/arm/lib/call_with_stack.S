/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/lib/call_with_stack.S
 *
 * Copyright (C) 2011 ARM Ltd.
 * Written by Will Deacon <will.deacon@arm.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/unwind.h>

/*
 * void call_with_stack(void (*fn)(void *), void *arg, void *sp)
 *
 * Change the stack to that pointed at by sp, then invoke fn(arg) with
 * the new stack.
 *
 * The sequence below follows the APCS frame convention for frame pointer
 * unwinding, and implements the unwinder annotations needed by the EABI
 * unwinder.
 */

ENTRY(call_with_stack)
#if defined(CONFIG_UNWINDER_FRAME_POINTER) && defined(CONFIG_CC_IS_GCC)
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
#else
UNWIND( .fnstart		)
UNWIND( .save	{fpreg, lr}	)
	push	{fpreg, lr}
UNWIND( .setfp	fpreg, sp	)
	mov	fpreg, sp
#endif
	mov	sp, r2
	mov	r2, r0
	mov	r0, r1

	bl_r	r2

#if defined(CONFIG_UNWINDER_FRAME_POINTER) && defined(CONFIG_CC_IS_GCC)
	ldmdb	fp, {fp, sp, pc}
#else
	mov	sp, fpreg
	pop	{fpreg, pc}
UNWIND( .fnend			)
#endif
ENDPROC(call_with_stack)
