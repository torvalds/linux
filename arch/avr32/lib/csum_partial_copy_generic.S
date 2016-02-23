/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/errno.h>
#include <asm/asm.h>

	/*
	 * unsigned int csum_partial_copy_generic(const char *src, char *dst, int len
	 *					  int sum, int *src_err_ptr,
	 *					  int *dst_err_ptr)
	 *
	 * Copy src to dst while checksumming, otherwise like csum_partial.
	 */

	.macro ld_src size, reg, ptr
9999:	ld.\size \reg, \ptr
	.section __ex_table, "a"
	.long	9999b, fixup_ld_src
	.previous
	.endm

	.macro st_dst size, ptr, reg
9999:	st.\size \ptr, \reg
	.section __ex_table, "a"
	.long	9999b, fixup_st_dst
	.previous
	.endm

	.text
	.global	csum_partial_copy_generic
	.type	csum_partial_copy_generic,"function"
	.align	1
csum_partial_copy_generic:
	pushm	r4-r7,lr

	/* The inner loop */
1:	sub	r10, 4
	brlt	5f
2:	ld_src	w, r5, r12++
	st_dst	w, r11++, r5
	add	r9, r5
	acr	r9
	sub	r10, 4
	brge	2b

	/* return if we had a whole number of words */
5:	sub	r10, -4
	brne	7f

6:	mov	r12, r9
	popm	r4-r7,pc

	/* handle additional bytes at the tail */
7:	mov	r5, 0
	mov	r4, 32
8:	ld_src	ub, r6, r12++
	st_dst	b, r11++, r6
	lsl	r5, 8
	sub	r4, 8
	bfins	r5, r6, 0, 8
	sub	r10, 1
	brne	8b

	lsl	r5, r5, r4
	add	r9, r5
	acr	r9
	rjmp	6b

	/* Exception handler */
	.section .fixup,"ax"
	.align	1
fixup_ld_src:
	mov	r9, -EFAULT
	cp.w	r8, 0
	breq	1f
	st.w	r8[0], r9

1:	/*
	 * TODO: zero the complete destination - computing the rest
	 * is too much work
	 */

	mov	r9, 0
	rjmp	6b

fixup_st_dst:
	mov	r9, -EFAULT
	lddsp	r8, sp[20]
	cp.w	r8, 0
	breq	1f
	st.w	r8[0], r9
1:	mov	r9, 0
	rjmp	6b

	.previous
