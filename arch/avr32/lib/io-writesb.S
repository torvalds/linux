/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

	.text
.Lnot_word_aligned:
1:	ld.ub	r8, r11++
	sub	r10, 1
	st.b	r12[0], r8
	reteq	r12
	tst	r11, r9
	brne	1b

	/* fall through */

	.global	__raw_writesb
	.type	__raw_writesb,@function
__raw_writesb:
	cp.w	r10, 0
	mov	r9, 3
	reteq	r12

	tst	r11, r9
	brne	.Lnot_word_aligned

	sub	r10, 4
	brlt	2f

1:	ld.w	r8, r11++
	bfextu	r9, r8, 24, 8
	st.b	r12[0], r9
	bfextu	r9, r8, 16, 8
	st.b	r12[0], r9
	bfextu	r9, r8, 8, 8
	st.b	r12[0], r9
	st.b	r12[0], r8
	sub	r10, 4
	brge	1b

2:	sub	r10, -4
	reteq	r12

3:	ld.ub	r8, r11++
	sub	r10, 1
	st.b	r12[0], r8
	brne	3b

	retal	r12
