/*
 * arch/sh/kernel/cpu/sh2/ex.S
 *
 * The SH-2 exception vector table
 *
 * Copyright (C) 2005 Yoshinori Sato
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/linkage.h>

!
! convert Exception Vector to Exception Number
!
exception_entry:	
no	=	0
	.rept	256
	mov.l	r0,@-sp
	mov	#no,r0
	bra	exception_trampoline
	and	#0xff,r0
no	=	no + 1
	.endr
exception_trampoline:
	mov.l	r1,@-sp
	mov.l	$exception_handler,r1
	jmp	@r1

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
	.long	exception_entry + vector * 8
vector	=	vector + 1
	.endr
