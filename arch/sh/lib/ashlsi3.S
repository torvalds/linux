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
! __ashlsi3
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
	.global	__ashlsi3
	
	.align	2
__ashlsi3:
	mov	#31,r0
	and	r0,r5
	mova	ashlsi3_table,r0
	mov.b	@(r0,r5),r5
#ifdef __sh1__
	add	r5,r0
	jmp	@r0
#else
	braf	r5
#endif
	mov	r4,r0

	.align	2
ashlsi3_table:
	.byte		ashlsi3_0-ashlsi3_table
	.byte		ashlsi3_1-ashlsi3_table
	.byte		ashlsi3_2-ashlsi3_table
	.byte		ashlsi3_3-ashlsi3_table
	.byte		ashlsi3_4-ashlsi3_table
	.byte		ashlsi3_5-ashlsi3_table
	.byte		ashlsi3_6-ashlsi3_table
	.byte		ashlsi3_7-ashlsi3_table
	.byte		ashlsi3_8-ashlsi3_table
	.byte		ashlsi3_9-ashlsi3_table
	.byte		ashlsi3_10-ashlsi3_table
	.byte		ashlsi3_11-ashlsi3_table
	.byte		ashlsi3_12-ashlsi3_table
	.byte		ashlsi3_13-ashlsi3_table
	.byte		ashlsi3_14-ashlsi3_table
	.byte		ashlsi3_15-ashlsi3_table
	.byte		ashlsi3_16-ashlsi3_table
	.byte		ashlsi3_17-ashlsi3_table
	.byte		ashlsi3_18-ashlsi3_table
	.byte		ashlsi3_19-ashlsi3_table
	.byte		ashlsi3_20-ashlsi3_table
	.byte		ashlsi3_21-ashlsi3_table
	.byte		ashlsi3_22-ashlsi3_table
	.byte		ashlsi3_23-ashlsi3_table
	.byte		ashlsi3_24-ashlsi3_table
	.byte		ashlsi3_25-ashlsi3_table
	.byte		ashlsi3_26-ashlsi3_table
	.byte		ashlsi3_27-ashlsi3_table
	.byte		ashlsi3_28-ashlsi3_table
	.byte		ashlsi3_29-ashlsi3_table
	.byte		ashlsi3_30-ashlsi3_table
	.byte		ashlsi3_31-ashlsi3_table

ashlsi3_6:
	shll2	r0
ashlsi3_4:
	shll2	r0
ashlsi3_2:
	rts
	shll2	r0

ashlsi3_7:
	shll2	r0
ashlsi3_5:
	shll2	r0
ashlsi3_3:
	shll2	r0
ashlsi3_1:
	rts
	shll	r0

ashlsi3_14:
	shll2	r0
ashlsi3_12:
	shll2	r0
ashlsi3_10:
	shll2	r0
ashlsi3_8:
	rts
	shll8	r0

ashlsi3_15:
	shll2	r0
ashlsi3_13:
	shll2	r0
ashlsi3_11:
	shll2	r0
ashlsi3_9:
	shll8	r0
	rts
	shll	r0

ashlsi3_22:
	shll2	r0
ashlsi3_20:
	shll2	r0
ashlsi3_18:
	shll2	r0
ashlsi3_16:
	rts
	shll16	r0

ashlsi3_23:
	shll2	r0
ashlsi3_21:
	shll2	r0
ashlsi3_19:
	shll2	r0
ashlsi3_17:
	shll16	r0
	rts
	shll	r0

ashlsi3_30:
	shll2	r0
ashlsi3_28:
	shll2	r0
ashlsi3_26:
	shll2	r0
ashlsi3_24:
	shll16	r0
	rts
	shll8	r0

ashlsi3_31:
	shll2	r0
ashlsi3_29:
	shll2	r0
ashlsi3_27:
	shll2	r0
ashlsi3_25:
	shll16	r0
	shll8	r0
	rts
	shll	r0

ashlsi3_0:
	rts
	nop
