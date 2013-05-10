/* Copyright (C) 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006
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

!! ashiftrt_r4_x, ___ashrsi3, ___ashlsi3, ___lshrsi3 routines
!! recoded in assembly by Toshiyasu Morita
!! tm@netcom.com

/* SH2 optimizations for ___ashrsi3, ___ashlsi3, ___lshrsi3 and
   ELF local label prefixes by J"orn Rennecke
   amylaar@cygnus.com  */

	/* r0: rn r1: qn */ /* r0: n1 r4: n0 r5: d r6: d1 */ /* r2: __m */
	/* n1 < d, but n1 might be larger than d1.  */
	.global __udiv_qrnnd_16
	.balign 8
__udiv_qrnnd_16:
	div0u
	cmp/hi r6,r0
	bt .Lots
	.rept 16
	div1 r6,r0 
	.endr
	extu.w r0,r1
	bt 0f
	add r6,r0
0:	rotcl r1
	mulu.w r1,r5
	xtrct r4,r0
	swap.w r0,r0
	sts macl,r2
	cmp/hs r2,r0
	sub r2,r0
	bt 0f
	addc r5,r0
	add #-1,r1
	bt 0f
1:	add #-1,r1
	rts
	add r5,r0
	.balign 8
.Lots:
	sub r5,r0
	swap.w r4,r1
	xtrct r0,r1
	clrt
	mov r1,r0
	addc r5,r0
	mov #-1,r1
	bf/s 1b
	 shlr16 r1
0:	rts
	 nop
