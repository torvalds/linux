/*
 * arch/sh/kernel/cpu/sh2a/ex.S
 *
 * The SH-2A exception vector table
 *
 * Copyright (C) 2008 Yoshinori Sato
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/linkage.h>

!
! convert Exception Vector to Exception Number
!

! exception no 0 to 255
exception_entry0:
no	=	0
	.rept	256
	mov.l	r1,@-sp
	bra	exception_trampoline0
	mov	#no,r1
no	=	no + 1
	.endr
exception_trampoline0:
	mov.l	r0,@-sp
	mov.l	1f,r0
	extu.b	r1,r1
	jmp	@r0
	  extu.w	r1,r1
	 
	.align	2
1:	.long	exception_handler

! exception no 256 to 511
exception_entry1:
no	=	0
	.rept	256
	mov.l	r1,@-sp
	bra	exception_trampoline1
	mov	#no,r1
no	=	no + 1
	.endr
exception_trampoline1:
	mov.l	r0,@-sp
	extu.b	r1,r1
	movi20	#0x100,r0
	add	r0,r1
	mov.l	1f,r0
	jmp	@r0
	  extu.w	r1,r1
	
	.align	2
1:	.long	exception_handler

	!
! Exception Vector Base
!
	.align	2
ENTRY(vbr_base)
vector	=	0
	.rept	256
	.long	exception_entry0 + vector * 6
vector	=	vector + 1
	.endr
vector	=	0
	.rept	256
	.long	exception_entry1 + vector * 6
vector	=	vector + 1
	.endr
