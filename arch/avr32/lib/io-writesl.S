/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

	.global	__raw_writesl
	.type	__raw_writesl,@function
__raw_writesl:
	cp.w	r10, 0
	reteq	r12

1:	ld.w	r8, r11++
	sub	r10, 1
	st.w	r12[0], r8
	brne	1b

	retal	r12
