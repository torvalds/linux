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

	b	start

