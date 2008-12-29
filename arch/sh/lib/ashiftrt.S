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

	.global	__ashiftrt_r4_0
	.global	__ashiftrt_r4_1
	.global	__ashiftrt_r4_2
	.global	__ashiftrt_r4_3
	.global	__ashiftrt_r4_4
	.global	__ashiftrt_r4_5
	.global	__ashiftrt_r4_6
	.global	__ashiftrt_r4_7
	.global	__ashiftrt_r4_8
	.global	__ashiftrt_r4_9
	.global	__ashiftrt_r4_10
	.global	__ashiftrt_r4_11
	.global	__ashiftrt_r4_12
	.global	__ashiftrt_r4_13
	.global	__ashiftrt_r4_14
	.global	__ashiftrt_r4_15
	.global	__ashiftrt_r4_16
	.global	__ashiftrt_r4_17
	.global	__ashiftrt_r4_18
	.global	__ashiftrt_r4_19
	.global	__ashiftrt_r4_20
	.global	__ashiftrt_r4_21
	.global	__ashiftrt_r4_22
	.global	__ashiftrt_r4_23
	.global	__ashiftrt_r4_24
	.global	__ashiftrt_r4_25
	.global	__ashiftrt_r4_26
	.global	__ashiftrt_r4_27
	.global	__ashiftrt_r4_28
	.global	__ashiftrt_r4_29
	.global	__ashiftrt_r4_30
	.global	__ashiftrt_r4_31
	.global	__ashiftrt_r4_32

	.align	1
__ashiftrt_r4_32:
__ashiftrt_r4_31:
	rotcl	r4
	rts
	subc	r4,r4
__ashiftrt_r4_30:
	shar	r4
__ashiftrt_r4_29:
	shar	r4
__ashiftrt_r4_28:
	shar	r4
__ashiftrt_r4_27:
	shar	r4
__ashiftrt_r4_26:
	shar	r4
__ashiftrt_r4_25:
	shar	r4
__ashiftrt_r4_24:
	shlr16	r4
	shlr8	r4
	rts
	exts.b	r4,r4
__ashiftrt_r4_23:
	shar	r4
__ashiftrt_r4_22:
	shar	r4
__ashiftrt_r4_21:
	shar	r4
__ashiftrt_r4_20:
	shar	r4
__ashiftrt_r4_19:
	shar	r4
__ashiftrt_r4_18:
	shar	r4
__ashiftrt_r4_17:
	shar	r4
__ashiftrt_r4_16:
	shlr16	r4
	rts
	exts.w	r4,r4
__ashiftrt_r4_15:
	shar	r4
__ashiftrt_r4_14:
	shar	r4
__ashiftrt_r4_13:
	shar	r4
__ashiftrt_r4_12:
	shar	r4
__ashiftrt_r4_11:
	shar	r4
__ashiftrt_r4_10:
	shar	r4
__ashiftrt_r4_9:
	shar	r4
__ashiftrt_r4_8:
	shar	r4
__ashiftrt_r4_7:
	shar	r4
__ashiftrt_r4_6:
	shar	r4
__ashiftrt_r4_5:
	shar	r4
__ashiftrt_r4_4:
	shar	r4
__ashiftrt_r4_3:
	shar	r4
__ashiftrt_r4_2:
	shar	r4
__ashiftrt_r4_1:
	rts
	shar	r4
__ashiftrt_r4_0:
	rts
	nop
