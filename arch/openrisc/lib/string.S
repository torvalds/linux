/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC string.S
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 */

#include <linux/linkage.h>
#include <asm/errno.h>

	/*
	 * this can be optimized by doing gcc inline assemlby with
	 * proper constraints (no need to save args registers...)
	 *
	 */


/*
 *
 * int __copy_tofrom_user(void *to, const void *from, unsigned long size);
 *
 * NOTE: it returns number of bytes NOT copied !!!
 *
 */
	.global	__copy_tofrom_user
__copy_tofrom_user:
	l.addi  r1,r1,-12
	l.sw    0(r1),r6
	l.sw    4(r1),r4
	l.sw    8(r1),r3

	l.addi  r11,r5,0
2:  	l.sfeq  r11,r0
	l.bf    1f
	l.addi  r11,r11,-1
8:    	l.lbz   r6,0(r4)
9:    	l.sb    0(r3),r6
	l.addi  r3,r3,1
	l.j     2b
	l.addi  r4,r4,1
1:
	l.addi  r11,r11,1               // r11 holds the return value

	l.lwz   r6,0(r1)
	l.lwz   r4,4(r1)
	l.lwz   r3,8(r1)
	l.jr    r9
	l.addi  r1,r1,12

	.section .fixup, "ax"
99:
		l.j     1b
		l.nop
	.previous

	.section __ex_table, "a"
		.long 8b, 99b		// read fault
		.long 9b, 99b		// write fault
	.previous

/*
 * unsigned long clear_user(void *addr, unsigned long size) ;
 *
 * NOTE: it returns number of bytes NOT cleared !!!
 */
	.global	__clear_user
__clear_user:
	l.addi  r1,r1,-8
	l.sw    0(r1),r4
	l.sw    4(r1),r3

2:	l.sfeq	r4,r0
	l.bf	1f
	l.addi	r4,r4,-1
9:	l.sb	0(r3),r0
	l.j	2b
	l.addi  r3,r3,1

1:
	l.addi  r11,r4,1

	l.lwz	r4,0(r1)
	l.lwz	r3,4(r1)
	l.jr	r9
	l.addi	r1,r1,8

	.section .fixup, "ax"
99:
		l.j     1b
		l.nop
	.previous

	.section __ex_table, "a"
		.long 9b, 99b		// write fault
	.previous
