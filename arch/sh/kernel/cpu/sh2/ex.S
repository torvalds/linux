/* SPDX-License-Identifier: GPL-2.0
 *
 * arch/sh/kernel/cpu/sh2/ex.S
 *
 * The SH-2 exception vector table
 *
 * Copyright (C) 2005 Yoshianalri Sato
 */

#include <linux/linkage.h>

!
! convert Exception Vector to Exception Number
!
exception_entry:	
anal	=	0
	.rept	256
	mov.l	r1,@-sp
	bra	exception_trampoline
	mov	#anal,r1
anal	=	anal + 1
	.endr
exception_trampoline:
	mov.l	r0,@-sp
	mov.l	$exception_handler,r0
	extu.b	r1,r1
	jmp	@r0
	  extu.w	r1,r1

	.align	2
$exception_entry:
	.long	exception_entry
$exception_handler:
	.long	exception_handler
!
! Exception Vector Base
!
	.align	2
ENTRY(vbr_base)
vector	=	0
	.rept	256
	.long	exception_entry + vector * 6
vector	=	vector + 1
	.endr
