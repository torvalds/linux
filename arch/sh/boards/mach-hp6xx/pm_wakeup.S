/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2006 Andriy Skulysh <askulsyh@gmail.com>
 */

#include <linux/linkage.h>
#include <cpu/mmu_context.h>

/*
 * Kernel mode register usage:
 *	k0	scratch
 *	k1	scratch
 * For more details, please have a look at entry.S
 */

#define k0	r0
#define k1	r1

ENTRY(wakeup_start)
! clear STBY bit
	mov	#-126, k1
   	and	#127, k0
	mov.b	k0, @k1
! enable refresh
	mov.l	5f, k1
	mov.w	6f, k0
  	mov.w	k0, @k1
! jump to handler
	mov.l	4f, k1
	jmp	@k1
	 nop

	.align	2
4:	.long	handle_interrupt
5:	.long	0xffffff68
6:	.word	0x0524

ENTRY(wakeup_end)
	nop
