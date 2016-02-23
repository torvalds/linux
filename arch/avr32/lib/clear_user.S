/*
 * Copyright 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/asm.h>

	.text
	.align	1
	.global	clear_user
	.type	clear_user, "function"
clear_user:
	branch_if_kernel r8, __clear_user
	ret_if_privileged r8, r12, r11, r11

	.global	__clear_user
	.type	__clear_user, "function"
__clear_user:
	mov	r9, r12
	mov	r8, 0
	andl	r9, 3, COH
	brne	5f

1:	sub	r11, 4
	brlt	2f

10:	st.w	r12++, r8
	sub	r11, 4
	brge	10b

2:	sub	r11, -4
	reteq	0

	/* Unaligned count or address */
	bld	r11, 1
	brcc	12f
11:	st.h	r12++, r8
	sub	r11, 2
	reteq	0
12:	st.b	r12++, r8
	retal	0

	/* Unaligned address */
5:	cp.w	r11, 4
	brlt	2b

	lsl	r9, 2
	add	pc, pc, r9
13:	st.b	r12++, r8
	sub	r11, 1
14:	st.b	r12++, r8
	sub	r11, 1
15:	st.b	r12++, r8
	sub	r11, 1
	rjmp	1b

	.size	clear_user, . - clear_user
	.size	__clear_user, . - __clear_user

	.section .fixup, "ax"
	.align	1
18:	sub	r11, -4
19:	retal	r11

	.section __ex_table, "a"
	.align	2
	.long	10b, 18b
	.long	11b, 19b
	.long	12b, 19b
	.long	13b, 19b
	.long	14b, 19b
	.long	15b, 19b
