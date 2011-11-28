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
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
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

/*
 * long strncpy_from_user(char *dst, const char *src, long count)
 *
 *
 */
	.global	__strncpy_from_user
__strncpy_from_user:
	l.addi  r1,r1,-16
	l.sw    0(r1),r6
	l.sw    4(r1),r5
	l.sw    8(r1),r4
	l.sw    12(r1),r3

	l.addi  r11,r5,0
2:  	l.sfeq  r5,r0
	l.bf    1f
	l.addi  r5,r5,-1
8:    	l.lbz   r6,0(r4)
	l.sfeq  r6,r0
	l.bf    1f
9:    	l.sb    0(r3),r6
	l.addi  r3,r3,1
	l.j     2b
	l.addi  r4,r4,1
1:
	l.lwz   r6,0(r1)
	l.addi  r5,r5,1
	l.sub   r11,r11,r5              // r11 holds the return value

	l.lwz   r6,0(r1)
	l.lwz   r5,4(r1)
	l.lwz   r4,8(r1)
	l.lwz   r3,12(r1)
	l.jr    r9
	l.addi  r1,r1,16

	.section .fixup, "ax"
99:
		l.movhi r11,hi(-EFAULT)
		l.ori   r11,r11,lo(-EFAULT)

		l.lwz   r6,0(r1)
		l.lwz   r5,4(r1)
		l.lwz   r4,8(r1)
		l.lwz   r3,12(r1)
		l.jr	r9
		l.addi  r1,r1,16
	.previous

	.section __ex_table, "a"
		.long 8b, 99b		// read fault
	.previous

/*
 * extern int __strnlen_user(const char *str, long len, unsigned long top);
 *
 *
 * RTRN: - length of a string including NUL termination character
 *       - on page fault 0
 */

	.global __strnlen_user
__strnlen_user:
	l.addi  r1,r1,-8
	l.sw    0(r1),r6
	l.sw    4(r1),r3

	l.addi  r11,r0,0
2:  	l.sfeq  r11,r4
	l.bf    1f
	l.addi  r11,r11,1
8:    	l.lbz   r6,0(r3)
	l.sfeq  r6,r0
	l.bf    1f
	l.sfgeu r3,r5                  // are we over the top ?
	l.bf    99f
	l.j     2b
	l.addi  r3,r3,1

1:
	l.lwz   r6,0(r1)
	l.lwz	r3,4(r1)
	l.jr    r9
	l.addi  r1,r1,8

	.section .fixup, "ax"
99:
		l.addi  r11,r0,0

		l.lwz   r6,0(r1)
		l.lwz	r3,4(r1)
		l.jr	r9
		l.addi  r1,r1,8
	.previous

	.section __ex_table, "a"
		.long 8b, 99b		// read fault
	.previous
