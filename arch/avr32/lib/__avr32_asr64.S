/*
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

	/*
	 * DWtype __avr32_asr64(DWtype u, word_type b)
	 */
	.text
	.global	__avr32_asr64
	.type	__avr32_asr64,@function
__avr32_asr64:
	cp.w	r12, 0
	reteq	r12

	rsub	r9, r12, 32
	brle	1f

	lsl	r8, r11, r9
	lsr	r10, r10, r12
	asr	r11, r11, r12
	or	r10, r8
	retal	r12

1:	neg	r9
	asr	r10, r11, r9
	asr	r11, 31
	retal	r12
