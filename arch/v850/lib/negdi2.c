/*
 * arch/v850/lib/negdi2.c -- 64-bit negation
 *
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

typedef		 int DItype	__attribute__ ((mode (DI)));

DItype __negdi2 (DItype x)
{
	__asm__ __volatile__
		("not	r6, r10;"
		 "add	1, r10;"
		 "setf	c, r6;"
		 "not	r7, r11;"
		 "add	r6, r11"
		 ::: "r6", "r7", "r10", "r11");
}
