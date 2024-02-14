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

!
! __ashrsi3
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
! __ashrsi3_r0
!
! Entry:
!
! r4: Value to shift
! r0: Shifts
!
! Exit:
!
! r0: Result
!
! Destroys:
!
! (none)

	.global	__ashrsi3
	.global	__ashrsi3_r0
	
	.align	2
__ashrsi3:
	mov	r5,r0
	.align	2
__ashrsi3_r0:
	and	#31,r0
	mov.l	r4,@-r15
	mov	r0,r4
	mova	ashrsi3_table,r0
	mov.b	@(r0,r4),r4
	add	r4,r0
	jmp	@r0
	mov.l	@r15+,r0

	.align	2
ashrsi3_table:
	.byte		ashrsi3_0-ashrsi3_table
	.byte		ashrsi3_1-ashrsi3_table
	.byte		ashrsi3_2-ashrsi3_table
	.byte		ashrsi3_3-ashrsi3_table
	.byte		ashrsi3_4-ashrsi3_table
	.byte		ashrsi3_5-ashrsi3_table
	.byte		ashrsi3_6-ashrsi3_table
	.byte		ashrsi3_7-ashrsi3_table
	.byte		ashrsi3_8-ashrsi3_table
	.byte		ashrsi3_9-ashrsi3_table
	.byte		ashrsi3_10-ashrsi3_table
	.byte		ashrsi3_11-ashrsi3_table
	.byte		ashrsi3_12-ashrsi3_table
	.byte		ashrsi3_13-ashrsi3_table
	.byte		ashrsi3_14-ashrsi3_table
	.byte		ashrsi3_15-ashrsi3_table
	.byte		ashrsi3_16-ashrsi3_table
	.byte		ashrsi3_17-ashrsi3_table
	.byte		ashrsi3_18-ashrsi3_table
	.byte		ashrsi3_19-ashrsi3_table
	.byte		ashrsi3_20-ashrsi3_table
	.byte		ashrsi3_21-ashrsi3_table
	.byte		ashrsi3_22-ashrsi3_table
	.byte		ashrsi3_23-ashrsi3_table
	.byte		ashrsi3_24-ashrsi3_table
	.byte		ashrsi3_25-ashrsi3_table
	.byte		ashrsi3_26-ashrsi3_table
	.byte		ashrsi3_27-ashrsi3_table
	.byte		ashrsi3_28-ashrsi3_table
	.byte		ashrsi3_29-ashrsi3_table
	.byte		ashrsi3_30-ashrsi3_table
	.byte		ashrsi3_31-ashrsi3_table

ashrsi3_31:
	rotcl	r0
	rts
	subc	r0,r0

ashrsi3_30:
	shar	r0
ashrsi3_29:
	shar	r0
ashrsi3_28:
	shar	r0
ashrsi3_27:
	shar	r0
ashrsi3_26:
	shar	r0
ashrsi3_25:
	shar	r0
ashrsi3_24:
	shlr16	r0
	shlr8	r0
	rts
	exts.b	r0,r0

ashrsi3_23:
	shar	r0
ashrsi3_22:
	shar	r0
ashrsi3_21:
	shar	r0
ashrsi3_20:
	shar	r0
ashrsi3_19:
	shar	r0
ashrsi3_18:
	shar	r0
ashrsi3_17:
	shar	r0
ashrsi3_16:
	shlr16	r0
	rts
	exts.w	r0,r0

ashrsi3_15:
	shar	r0
ashrsi3_14:
	shar	r0
ashrsi3_13:
	shar	r0
ashrsi3_12:
	shar	r0
ashrsi3_11:
	shar	r0
ashrsi3_10:
	shar	r0
ashrsi3_9:
	shar	r0
ashrsi3_8:
	shar	r0
ashrsi3_7:
	shar	r0
ashrsi3_6:
	shar	r0
ashrsi3_5:
	shar	r0
ashrsi3_4:
	shar	r0
ashrsi3_3:
	shar	r0
ashrsi3_2:
	shar	r0
ashrsi3_1:
	rts
	shar	r0

ashrsi3_0:
	rts
	nop
