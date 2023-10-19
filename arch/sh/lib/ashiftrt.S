/* SPDX-License-Identifier: GPL-2.0+ WITH GCC-exception-2.0

   Copyright (C) 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006
   Free Software Foundation, Inc.
*/

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
