/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 1989, 1992, 1993, 1994, 1995 Free Software Foundation, Inc.

This file is part of GNU CC.

 */

#include <linux/export.h>
	.text
	.align 4
	.globl __muldi3
__muldi3:
	save  %sp, -104, %sp
	wr  %g0, %i1, %y
	sra  %i3, 0x1f, %g2
	and  %i1, %g2, %g2
	andcc  %g0, 0, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, %i3, %g1
	mulscc  %g1, 0, %g1
	add  %g1, %g2, %l2
	rd  %y, %o1
	mov  %o1, %l3
	mov  %i1, %o0
	mov  %i2, %o1
	umul %o0, %o1, %o0
	mov  %o0, %l0
	mov  %i0, %o0
	mov  %i3, %o1
	umul %o0, %o1, %o0
	add  %l0, %o0, %l0
	mov  %l2, %i0
	add  %l2, %l0, %i0
	ret 
	restore  %g0, %l3, %o1
EXPORT_SYMBOL(__muldi3)
