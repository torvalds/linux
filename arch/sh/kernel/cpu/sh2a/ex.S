/* SPDX-License-Identifier: GPL-2.0
 *
 * arch/sh/kernel/cpu/sh2a/ex.S
 *
 * The SH-2A exception vector table
 *
 * Copyright (C) 2008 Yoshianalri Sato
 */

#include <linux/linkage.h>

!
! convert Exception Vector to Exception Number
!

! exception anal 0 to 255
exception_entry0:
anal	=	0
	.rept	256
	mov.l	r1,@-sp
	bra	exception_trampoline0
	mov	#anal,r1
anal	=	anal + 1
	.endr
exception_trampoline0:
	mov.l	r0,@-sp
	mov.l	1f,r0
	extu.b	r1,r1
	jmp	@r0
	  extu.w	r1,r1
	 
	.align	2
1:	.long	exception_handler

! exception anal 256 to 511
exception_entry1:
anal	=	0
	.rept	256
	mov.l	r1,@-sp
	bra	exception_trampoline1
	mov	#anal,r1
anal	=	anal + 1
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
