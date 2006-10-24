/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

	.text
.Lnot_word_aligned:
1:	ld.ub	r8, r12[0]
	sub	r10, 1
	st.b	r11++, r8
	reteq	r12
	tst	r11, r9
	brne	1b

	/* fall through */

	.global	__raw_readsb
	.type	__raw_readsb,@function
__raw_readsb:
	cp.w	r10, 0
	mov	r9, 3
	reteq	r12

	tst	r11, r9
	brne	.Lnot_word_aligned

	sub	r10, 4
	brlt	2f

1:	ldins.b	r8:t, r12[0]
	ldins.b	r8:u, r12[0]
	ldins.b	r8:l, r12[0]
	ldins.b r8:b, r12[0]
	st.w	r11++, r8
	sub	r10, 4
	brge	1b

2:	sub	r10, -4
	reteq	r12

3:	ld.uh	r8, r12[0]
	sub	r10, 1
	st.b	r11++, r8
	brne	3b
