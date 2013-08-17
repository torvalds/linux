/* Copyright (C) 1989, 1992, 1993, 1994, 1995 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

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
	call  .umul
	mov  %i2, %o1
	mov  %o0, %l0
	mov  %i0, %o0
	call  .umul
	mov  %i3, %o1
	add  %l0, %o0, %l0
	mov  %l2, %i0
	add  %l2, %l0, %i0
	ret 
	restore  %g0, %l3, %o1
