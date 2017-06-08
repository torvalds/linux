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

!
! __lshrsi3
!
! Entry:
!
! r4: Value to shift
! r5: Shifts
!
! Exit:
!
! r0: Result
!
! Destroys:
!
! (none)
!
! __lshrsi3_r0
!
! Entry:
!
! r0: Value to shift
! r5: Shifts
!
! Exit:
!
! r0: Result
!
! Destroys:
!
! (none)
!
	.global	__lshrsi3
	.global	__lshrsi3_r0
	
	.align	2
__lshrsi3:
	mov	r5,r0
	.align	2
__lshrsi3_r0:
	and	#31,r0
	mov.l	r4,@-r15
	mov	r0,r4
	mova	lshrsi3_table,r0
	mov.b	@(r0,r4),r4
	add	r4,r0
	jmp	@r0
	mov.l	@r15+,r0

	.align	2
lshrsi3_table:
	.byte		lshrsi3_0-lshrsi3_table
	.byte		lshrsi3_1-lshrsi3_table
	.byte		lshrsi3_2-lshrsi3_table
	.byte		lshrsi3_3-lshrsi3_table
	.byte		lshrsi3_4-lshrsi3_table
	.byte		lshrsi3_5-lshrsi3_table
	.byte		lshrsi3_6-lshrsi3_table
	.byte		lshrsi3_7-lshrsi3_table
	.byte		lshrsi3_8-lshrsi3_table
	.byte		lshrsi3_9-lshrsi3_table
	.byte		lshrsi3_10-lshrsi3_table
	.byte		lshrsi3_11-lshrsi3_table
	.byte		lshrsi3_12-lshrsi3_table
	.byte		lshrsi3_13-lshrsi3_table
	.byte		lshrsi3_14-lshrsi3_table
	.byte		lshrsi3_15-lshrsi3_table
	.byte		lshrsi3_16-lshrsi3_table
	.byte		lshrsi3_17-lshrsi3_table
	.byte		lshrsi3_18-lshrsi3_table
	.byte		lshrsi3_19-lshrsi3_table
	.byte		lshrsi3_20-lshrsi3_table
	.byte		lshrsi3_21-lshrsi3_table
	.byte		lshrsi3_22-lshrsi3_table
	.byte		lshrsi3_23-lshrsi3_table
	.byte		lshrsi3_24-lshrsi3_table
	.byte		lshrsi3_25-lshrsi3_table
	.byte		lshrsi3_26-lshrsi3_table
	.byte		lshrsi3_27-lshrsi3_table
	.byte		lshrsi3_28-lshrsi3_table
	.byte		lshrsi3_29-lshrsi3_table
	.byte		lshrsi3_30-lshrsi3_table
	.byte		lshrsi3_31-lshrsi3_table

lshrsi3_6:
	shlr2	r0
lshrsi3_4:
	shlr2	r0
lshrsi3_2:
	rts
	shlr2	r0

lshrsi3_7:
	shlr2	r0
lshrsi3_5:
	shlr2	r0
lshrsi3_3:
	shlr2	r0
lshrsi3_1:
	rts
	shlr	r0

lshrsi3_14:
	shlr2	r0
lshrsi3_12:
	shlr2	r0
lshrsi3_10:
	shlr2	r0
lshrsi3_8:
	rts
	shlr8	r0

lshrsi3_15:
	shlr2	r0
lshrsi3_13:
	shlr2	r0
lshrsi3_11:
	shlr2	r0
lshrsi3_9:
	shlr8	r0
	rts
	shlr	r0

lshrsi3_22:
	shlr2	r0
lshrsi3_20:
	shlr2	r0
lshrsi3_18:
	shlr2	r0
lshrsi3_16:
	rts
	shlr16	r0

lshrsi3_23:
	shlr2	r0
lshrsi3_21:
	shlr2	r0
lshrsi3_19:
	shlr2	r0
lshrsi3_17:
	shlr16	r0
	rts
	shlr	r0

lshrsi3_30:
	shlr2	r0
lshrsi3_28:
	shlr2	r0
lshrsi3_26:
	shlr2	r0
lshrsi3_24:
	shlr16	r0
	rts
	shlr8	r0

lshrsi3_31:
	shlr2	r0
lshrsi3_29:
	shlr2	r0
lshrsi3_27:
	shlr2	r0
lshrsi3_25:
	shlr16	r0
	shlr8	r0
	rts
	shlr	r0

lshrsi3_0:
	rts
	nop
