/*
 * Copy to/from userspace with optional address space checking.
 *
 * Copyright 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/processor.h>
#include <asm/asm.h>

	.text
	.align	1
	.global	strnlen_user
	.type	strnlen_user, "function"
strnlen_user:
	branch_if_kernel r8, __strnlen_user
	sub	r8, r11, 1
	add	r8, r12
	retcs	0
	brmi	adjust_length	/* do a closer inspection */

	.global	__strnlen_user
	.type	__strnlen_user, "function"
__strnlen_user:
	mov	r10, r12

10:	ld.ub	r8, r12++
	cp.w	r8, 0
	breq	2f
	sub	r11, 1
	brne	10b

	sub	r12, -1
2:	sub	r12, r10
	retal	r12


	.type	adjust_length, "function"
adjust_length:
	cp.w	r12, 0		/* addr must always be < TASK_SIZE */
	retmi	0

	pushm	lr
	lddpc	lr, _task_size
	sub	r11, lr, r12
	mov	r9, r11
	rcall	__strnlen_user
	cp.w	r12, r9
	brgt	1f
	popm	pc
1:	popm	pc, r12=0

	.align	2
_task_size:
	.long	TASK_SIZE

	.section .fixup, "ax"
	.align	1
19:	retal	0

	.section __ex_table, "a"
	.align	2
	.long	10b, 19b
