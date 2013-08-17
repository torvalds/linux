/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

	/*
	 * unsigned int csum_partial(const unsigned char *buff,
	 *			     int len, unsigned int sum)
	 */
	.text
	.global	csum_partial
	.type	csum_partial,"function"
	.align	1
csum_partial:
	/* checksum complete words, aligned or not */
3:	sub	r11, 4
	brlt	5f
4:	ld.w	r9, r12++
	add	r10, r9
	acr	r10
	sub	r11, 4
	brge	4b

	/* return if we had a whole number of words */
5:	sub	r11, -4
	reteq	r10

	/* checksum any remaining bytes at the end */
	mov	r9, 0
	mov	r8, 0
	cp	r11, 2
	brlt	6f
	ld.uh	r9, r12++
	sub	r11, 2
	breq	7f
	lsl	r9, 16
6:	ld.ub	r8, r12++
	lsl	r8, 8
7:	or	r9, r8
	add	r10, r9
	acr	r10

	retal	r10
	.size	csum_partial, . - csum_partial
