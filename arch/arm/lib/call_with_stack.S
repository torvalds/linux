/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/lib/call_with_stack.S
 *
 * Copyright (C) 2011 ARM Ltd.
 * Written by Will Deacon <will.deacon@arm.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * void call_with_stack(void (*fn)(void *), void *arg, void *sp)
 *
 * Change the stack to that pointed at by sp, then invoke fn(arg) with
 * the new stack.
 */
ENTRY(call_with_stack)
	str	sp, [r2, #-4]!
	str	lr, [r2, #-4]!

	mov	sp, r2
	mov	r2, r0
	mov	r0, r1

	badr	lr, 1f
	ret	r2

1:	ldr	lr, [sp]
	ldr	sp, [sp, #4]
	ret	lr
ENDPROC(call_with_stack)
