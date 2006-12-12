/*
 * Copyright (c) 2006 Andriy Skulysh <askulsyh@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/linkage.h>
#include <asm/cpu/mmu_context.h>

#define k0	r0
#define k1	r1
#define k2	r2
#define k3	r3
#define k4	r4

/*
 * Kernel mode register usage:
 *	k0	scratch
 *	k1	scratch
 *	k2	scratch (Exception code)
 *	k3	scratch (Return address)
 *	k4	scratch
 *	k5	reserved
 *	k6	Global Interrupt Mask (0--15 << 4)
 *	k7	CURRENT_THREAD_INFO (pointer to current thread info)
 */

ENTRY(wakeup_start)
! clear STBY bit
	mov	#-126, k2
   	and	#127, k0
	mov.b	k0, @k2
! enable refresh
	mov.l	5f, k1
	mov.w	6f, k0
  	mov.w	k0, @k1
! jump to handler
	mov.l	2f, k2
	mov.l	3f, k3
	mov.l	@k2, k2

	mov.l	4f, k1
	jmp	@k1
	nop

	.align	2
1:	.long	EXPEVT
2:	.long	INTEVT
3:	.long	ret_from_irq
4:	.long	handle_exception
5:	.long	0xffffff68
6:	.word	0x0524

ENTRY(wakeup_end)
	nop
