/*
 * Copy to/from userspace with optional address space checking.
 *
 * Copyright 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/errno.h>

#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/asm.h>

	/*
	 * long strncpy_from_user(char *dst, const char *src, long count)
	 *
	 * On success, returns the length of the string, not including
	 * the terminating NUL.
	 *
	 * If the string is longer than count, returns count
	 *
	 * If userspace access fails, returns -EFAULT
	 */
	.text
	.align	1
	.global	strncpy_from_user
	.type	strncpy_from_user, "function"
strncpy_from_user:
	mov	r9, -EFAULT
	branch_if_kernel r8, __strncpy_from_user
	ret_if_privileged r8, r11, r10, r9

	.global	__strncpy_from_user
	.type	__strncpy_from_user, "function"
__strncpy_from_user:
	cp.w	r10, 0
	reteq	0

	mov	r9, r10

1:	ld.ub	r8, r11++
	st.b	r12++, r8
	cp.w	r8, 0
	breq	2f
	sub	r9, 1
	brne	1b

2:	sub	r10, r9
	retal	r10

	.section .fixup, "ax"
	.align	1
3:	mov	r12, -EFAULT
	retal	r12

	.section __ex_table, "a"
	.align	2
	.long	1b, 3b
