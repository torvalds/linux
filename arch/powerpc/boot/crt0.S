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

#include "ppc_asm.h"

	.text
	/* a procedure descriptor used when booting this as a COFF file */
_zimage_start_opd:
	.long	_zimage_start, 0, 0, 0

	.globl	_zimage_start
_zimage_start:
	/* Work out the offset between the address we were linked at
	   and the address where we're running. */
	bl	1f
1:	mflr	r0
	lis	r9,1b@ha
	addi	r9,r9,1b@l
	subf.	r0,r9,r0
	beq	3f		/* if running at same address as linked */

	/* The .got2 section contains a list of addresses, so add
	   the address offset onto each entry. */
	lis	r9,__got2_start@ha
	addi	r9,r9,__got2_start@l
	lis	r8,__got2_end@ha
	addi	r8,r8,__got2_end@l
	subf.	r8,r9,r8
	beq	3f
	srwi.	r8,r8,2
	mtctr	r8
	add	r9,r0,r9
2:	lwz	r8,0(r9)
	add	r8,r8,r0
	stw	r8,0(r9)
	addi	r9,r9,4
	bdnz	2b

	/* Do a cache flush for our text, in case OF didn't */
3:	lis	r9,_start@ha
	addi	r9,r9,_start@l
	add	r9,r0,r9
	lis	r8,_etext@ha
	addi	r8,r8,_etext@l
	add	r8,r0,r8
4:	dcbf	r0,r9
	icbi	r0,r9
	addi	r9,r9,0x20
	cmplw	cr0,r9,r8
	blt	4b
	sync
	isync

	mr	r6,r1
	b	start

