/*
 * arch/sh/kernel/cpu/sh3/swsusp.S
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/sys.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/page.h>

#define k0	r0
#define k1	r1
#define k2	r2
#define k3	r3
#define k4	r4

! swsusp_arch_resume()
! - copy restore_pblist pages
! - restore registers from swsusp_arch_regs_cpu0

ENTRY(swsusp_arch_resume)
	mov.l	1f, r15
	mov.l	2f, r4
	mov.l	@r4, r4

swsusp_copy_loop:
	mov	r4, r0
	cmp/eq	#0, r0
	bt	swsusp_restore_regs

	mov.l	@(PBE_ADDRESS, r4), r2
	mov.l	@(PBE_ORIG_ADDRESS, r4), r5

	mov	#(PAGE_SIZE >> 10), r3
	shll8	r3
	shlr2	r3 /* PAGE_SIZE / 16 */
swsusp_copy_page:
	dt	r3
	mov.l	@r2+,r1   /*  16n+0 */
	mov.l	r1,@r5
	add	#4,r5
	mov.l	@r2+,r1	  /*  16n+4 */
	mov.l	r1,@r5
	add	#4,r5
	mov.l	@r2+,r1   /*  16n+8 */
	mov.l	r1,@r5
	add	#4,r5
	mov.l	@r2+,r1   /*  16n+12 */
	mov.l	r1,@r5
	bf/s	swsusp_copy_page
	 add	#4,r5

	bra	swsusp_copy_loop
	 mov.l	@(PBE_NEXT, r4), r4

swsusp_restore_regs:
	! BL=0: R7->R0 is bank0
	mov.l	3f, r8
	mov.l	4f, r5
	jsr	@r5
	 nop

	! BL=1: R7->R0 is bank1
	lds	k2, pr
	ldc	k3, ssr

	mov.l	@r15+, r0
	mov.l	@r15+, r1
	mov.l	@r15+, r2
	mov.l	@r15+, r3
	mov.l	@r15+, r4
	mov.l	@r15+, r5
	mov.l	@r15+, r6
	mov.l	@r15+, r7

	rte
	 nop
	! BL=0: R7->R0 is bank0

	.align	2
1:	.long	swsusp_arch_regs_cpu0
2:	.long	restore_pblist
3:	.long	0x20000000 ! RB=1
4:	.long	restore_regs

! swsusp_arch_suspend()
! - prepare pc for resume, return from function without swsusp_save on resume
! - save registers in swsusp_arch_regs_cpu0
! - call swsusp_save write suspend image

ENTRY(swsusp_arch_suspend)
	sts	pr, r0		! save pr in r0
	mov	r15, r2		! save sp in r2
	mov	r8, r5		! save r8 in r5
	stc	sr, r1
	ldc	r1, ssr		! save sr in ssr
	mov.l	1f, r1
	ldc	r1, spc		! setup pc value for resuming
	mov.l	5f, r15		! use swsusp_arch_regs_cpu0 as stack
	mov.l	6f, r3
	add	r3, r15		! save from top of structure

	! BL=0: R7->R0 is bank0
	mov.l	2f, r3		! get new SR value for bank1
	mov	#0, r4
	mov.l	7f, r1
	jsr	@r1		! switch to bank1 and save bank1 r7->r0
	 not	r4, r4

	! BL=1: R7->R0 is bank1
	stc	r2_bank, k0	! fetch old sp from r2_bank0
	mov.l	3f, k4		! SR bits to clear in k4
	mov.l	8f, k1
	jsr	@k1		! switch to bank0 and save all regs
	 stc	r0_bank, k3	! fetch old pr from r0_bank0

	! BL=0: R7->R0 is bank0
	mov	r2, r15		! restore old sp
	mov	r5, r8		! restore old r8
	stc	ssr, r1
	ldc	r1, sr		! restore old sr
	lds	r0, pr		! restore old pr
	mov.l	4f, r0
	jmp	@r0
	 nop

swsusp_call_save:
	mov	r2, r15		! restore old sp
	mov	r5, r8		! restore old r8
	lds	r0, pr		! restore old pr
	rts
	 mov	#0, r0

	.align	2
1:	.long	swsusp_call_save
2:	.long	0x20000000 ! RB=1
3:	.long	0xdfffffff ! RB=0
4:	.long	swsusp_save
5:	.long	swsusp_arch_regs_cpu0
6:	.long	SWSUSP_ARCH_REGS_SIZE
7:	.long	save_low_regs
8:	.long	save_regs
