/* Copyright (C) 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005
   Free Software Foundation, Inc.

This file is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

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
