/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * NOTE: this code runs in 32 bit mode and is packaged as ELF32.
 */

#include <asm/ppc_asm.h>

	.text
	.globl	_start
_start:
	lis	r9,_start@h
	lis	r8,_etext@ha
	addi	r8,r8,_etext@l
1:	dcbf	r0,r9
	icbi	r0,r9
	addi	r9,r9,0x20
	cmplwi	0,r9,8
	blt	1b
	sync
	isync

	## Clear out the BSS as per ANSI C requirements

	lis	r7,_end@ha
	addi    r7,r7,_end@l		# r7 = &_end 
	lis	r8,__bss_start@ha	# 
	addi    r8,r8,__bss_start@l	# r8 = &_bss_start

	## Determine how large an area, in number of words, to clear

	subf	r7,r8,r7		# r7 = &_end - &_bss_start + 1 
	addi	r7,r7,3			# r7 += 3 
	srwi.	r7,r7,2			# r7 = size in words.
	beq	3f			# If the size is zero, don't bother
	addi	r8,r8,-4		# r8 -= 4 
	mtctr	r7			# SPRN_CTR = number of words to clear
	li	r0,0			# r0 = 0
2:	stwu	r0,4(r8)		# Clear out a word
	bdnz	2b			# Keep clearing until done
3:
	b	start

