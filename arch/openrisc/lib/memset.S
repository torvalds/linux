/*
 * OpenRISC memset.S
 *
 * Hand-optimized assembler version of memset for OpenRISC.
 * Algorithm inspired by several other arch-specific memset routines
 * in the kernel tree
 *
 * Copyright (C) 2015 Olof Kindgren <olof.kindgren@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

	.global memset
	.type	memset, @function
memset:
	/* arguments:
	 * r3 = *s
	 * r4 = c
	 * r5 = n
	 * r13, r15, r17, r19 used as temp regs
	*/

	/* Exit if n == 0 */
	l.sfeqi		r5, 0
	l.bf		4f

	/* Truncate c to char */
	l.andi  	r13, r4, 0xff

	/* Skip word extension if c is 0 */
	l.sfeqi		r13, 0
	l.bf		1f
	/* Check for at least two whole words (8 bytes) */
	 l.sfleui	r5, 7

	/* Extend char c to 32-bit word cccc in r13 */
	l.slli		r15, r13, 16  // r13 = 000c, r15 = 0c00
	l.or		r13, r13, r15 // r13 = 0c0c, r15 = 0c00
	l.slli		r15, r13, 8   // r13 = 0c0c, r15 = c0c0
	l.or		r13, r13, r15 // r13 = cccc, r15 = c0c0

1:	l.addi		r19, r3, 0 // Set r19 = src
	/* Jump to byte copy loop if less than two words */
	l.bf		3f
	 l.or		r17, r5, r0 // Set r17 = n

	/* Mask out two LSBs to check alignment */
	l.andi		r15, r3, 0x3

	/* lsb == 00, jump to word copy loop */
	l.sfeqi		r15, 0
	l.bf		2f
	 l.addi		r19, r3, 0 // Set r19 = src

	/* lsb == 01,10 or 11 */
	l.sb		0(r3), r13   // *src = c
	l.addi		r17, r17, -1 // Decrease n

	l.sfeqi		r15, 3
	l.bf		2f
	 l.addi		r19, r3, 1  // src += 1

	/* lsb == 01 or 10 */
	l.sb		1(r3), r13   // *(src+1) = c
	l.addi		r17, r17, -1 // Decrease n

	l.sfeqi		r15, 2
	l.bf		2f
	 l.addi		r19, r3, 2  // src += 2

	/* lsb == 01 */
	l.sb		2(r3), r13   // *(src+2) = c
	l.addi		r17, r17, -1 // Decrease n
	l.addi		r19, r3, 3   // src += 3

	/* Word copy loop */
2:	l.sw		0(r19), r13  // *src = cccc
	l.addi		r17, r17, -4 // Decrease n
	l.sfgeui	r17, 4
	l.bf		2b
	 l.addi		r19, r19, 4  // Increase src

	/* When n > 0, copy the remaining bytes, otherwise jump to exit */
	l.sfeqi		r17, 0
	l.bf		4f

	/* Byte copy loop */
3:	l.addi		r17, r17, -1 // Decrease n
	l.sb		0(r19), r13  // *src = cccc
	l.sfnei		r17, 0
	l.bf		3b
	 l.addi		r19, r19, 1  // Increase src

4:	l.jr		r9
	 l.ori		r11, r3, 0
