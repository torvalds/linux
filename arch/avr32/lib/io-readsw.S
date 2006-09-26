/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

.Lnot_word_aligned:
	/*
	 * Bad alignment will cause a hardware exception, which is as
	 * good as anything. No need for us to check for proper alignment.
	 */
	ld.uh	r8, r12[0]
	sub	r10, 1
	st.h	r11++, r8

	/* fall through */

	.global	__raw_readsw
	.type	__raw_readsw,@function
__raw_readsw:
	cp.w	r10, 0
	reteq	r12
	mov	r9, 3
	tst	r11, r9
	brne	.Lnot_word_aligned

	sub	r10, 2
	brlt	2f

1:	ldins.h	r8:t, r12[0]
	ldins.h	r8:b, r12[0]
	st.w	r11++, r8
	sub	r10, 2
	brge	1b

2:	sub	r10, -2
	reteq	r12

	ld.uh	r8, r12[0]
	st.h	r11++, r8
	retal	r12
