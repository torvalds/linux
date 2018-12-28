/* SPDX-License-Identifier: GPL-2.0+ WITH GCC-exception-2.0

   Copyright (C) 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005
   Free Software Foundation, Inc.
*/

!! libgcc routines for the Renesas / SuperH SH CPUs.
!! Contributed by Steve Chamberlain.
!! sac@cygnus.com

	.balign 4
	.global	__udivsi3
	.type	__udivsi3, @function
div8:
	div1 r5,r4
div7:
	div1 r5,r4; div1 r5,r4; div1 r5,r4
	div1 r5,r4; div1 r5,r4; div1 r5,r4; rts; div1 r5,r4

divx4:
	div1 r5,r4; rotcl r0
	div1 r5,r4; rotcl r0
	div1 r5,r4; rotcl r0
	rts; div1 r5,r4

__udivsi3:
	sts.l pr,@-r15
	extu.w r5,r0
	cmp/eq r5,r0
	bf/s large_divisor
	div0u
	swap.w r4,r0
	shlr16 r4
	bsr div8
	shll16 r5
	bsr div7
	div1 r5,r4
	xtrct r4,r0
	xtrct r0,r4
	bsr div8
	swap.w r4,r4
	bsr div7
	div1 r5,r4
	lds.l @r15+,pr
	xtrct r4,r0
	swap.w r0,r0
	rotcl r0
	rts
	shlr16 r5

large_divisor:
	mov #0,r0
	xtrct r4,r0
	xtrct r0,r4
	bsr divx4
	rotcl r0
	bsr divx4
	rotcl r0
	bsr divx4
	rotcl r0
	bsr divx4
	rotcl r0
	lds.l @r15+,pr
	rts
	rotcl r0
