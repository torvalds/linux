/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

	/*
	 * void *memcpy(void *to, const void *from, unsigned long n)
	 *
	 * This implementation does word-aligned loads in the main loop,
	 * possibly sacrificing alignment of stores.
	 *
	 * Hopefully, in most cases, both "to" and "from" will be
	 * word-aligned to begin with.
	 */
	.text
	.global	memcpy
	.type	memcpy, @function
memcpy:
	mov	r9, r11
	andl	r9, 3, COH
	brne	1f

	/* At this point, "from" is word-aligned */
2:	mov	r9, r12
5:	sub	r10, 4
	brlt	4f

3:	ld.w	r8, r11++
	sub	r10, 4
	st.w	r12++, r8
	brge	3b

4:	neg	r10
	reteq	r9

	/* Handle unaligned count */
	lsl	r10, 2
	add	pc, pc, r10
	ld.ub	r8, r11++
	st.b	r12++, r8
	ld.ub	r8, r11++
	st.b	r12++, r8
	ld.ub	r8, r11++
	st.b	r12++, r8
	retal	r9

	/* Handle unaligned "from" pointer */
1:	sub	r10, 4
	movlt	r9, r12
	brlt	4b
	add	r10, r9
	lsl	r9, 2
	add	pc, pc, r9
	ld.ub	r8, r11++
	st.b	r12++, r8
	ld.ub	r8, r11++
	st.b	r12++, r8
	ld.ub	r8, r11++
	st.b	r12++, r8
	mov	r8, r12
	add	pc, pc, r9
	sub	r8, 1
	nop
	sub	r8, 1
	nop
	sub	r8, 1
	nop
	mov	r9, r8
	rjmp	5b
