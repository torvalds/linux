/* Support file for -mfix-vr4120.
   Copyright (C) 2002, 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* This file contains functions which implement divsi3 and modsi3 for
   -mfix-vr4120.  div and ddiv do not give the correct result when one
   of the operands is negative.  */

	.set	nomips16

#define DIV								\
	xor	$3,$4,$5	/* t = x ^ y */ ;			\
	li	$2,0x80000000;						\
	.set	noreorder;						\
	bgez	$4,1f		/* x >= 0 */; 				\
	and	$3,$3,$2	/* t = (x ^ y) & 0x80000000 in delay slot */ ;\
	.set	reorder;						\
	subu	$4,$0,$4	/* x = -x */ ;				\
1:; 									\
	.set	noreorder;						\
	bgez	$5,2f		/* y >= 0 */ ;				\
	nop;								\
	subu	$5,$0,$5	/* y = -y */ ;				\
	.set	reorder;						\
2:;									\
	divu	$0,$4,$5;	/* we use divu because of INT_MIN */	\
	.set	noreorder;						\
	bne	$5,$0,3f;						\
	nop;								\
	break	7		/* division on zero y */ ;		\
3:;									\
	.set	reorder;						\
	mflo	$2		/* r = x / y */ ;			\
	.set	noreorder;						\
	beq	$3,$0,4f	/* t == 0 */ ;				\
	nop;								\
	subu	$2,$0,$2	/* r = -r */ ;				\
	.set	reorder;						\
4:

	.globl	__vr4120_divsi3
	.ent	__vr4120_divsi3
__vr4120_divsi3:
	DIV
	j	$31
	.end	__vr4120_divsi3

	.globl	__vr4120_modsi3
	.ent	__vr4120_modsi3
__vr4120_modsi3:
	move	$6,$4		# x1 = x
	move	$7,$5		# y1 = y
	DIV
	mult	$2,$7		# r = r * y1
	mflo	$2
	.set	noreorder
	j	$31
	subu	$2,$6,$2	# r = x1 - r  in delay slot
	.end	__vr4120_modsi3
