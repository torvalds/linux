/* SPDX-License-Identifier: GPL-2.0
 *
 * arch/sh/kernel/cpu/sh4a/sleep-sh_mobile.S
 *
 * Sleep mode and Standby modes support for SuperH Mobile
 *
 *  Copyright (C) 2009 Magnus Damm
 */

#include <linux/sys.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/suspend.h>

/*
 * Kernel mode register usage, see entry.S:
 *	k0	scratch
 *	k1	scratch
 */
#define k0	r0
#define k1	r1

/* manage self-refresh and enter standby mode. must be self-contained.
 * this code will be copied to on-chip memory and executed from there.
 */
	.balign 4
ENTRY(sh_mobile_sleep_enter_start)

	/* save mode flags */
	mov.l	r4, @(SH_SLEEP_MODE, r5)

	/* save original vbr */
	stc	vbr, r0
	mov.l	r0, @(SH_SLEEP_VBR, r5)

	/* point vbr to our on-chip memory page */
	ldc	r5, vbr

	/* save return address */
	sts	pr, r0
	mov.l	r0, @(SH_SLEEP_SPC, r5)

	/* save sr */
	stc	sr, r0
	mov.l	r0, @(SH_SLEEP_SR, r5)

	/* save general purpose registers to stack if needed */
	mov.l	@(SH_SLEEP_MODE, r5), r0
	tst	#SUSP_SH_REGS, r0
	bt	skip_regs_save

	sts.l	pr, @-r15
	mov.l	r14, @-r15
	mov.l	r13, @-r15
	mov.l	r12, @-r15
	mov.l	r11, @-r15
	mov.l	r10, @-r15
	mov.l	r9, @-r15
	mov.l	r8, @-r15

	/* make sure bank0 is selected, save low registers */
	mov.l	rb_bit, r9
	not	r9, r9
	bsr	set_sr
	 mov	#0, r10

	bsr	save_low_regs
	 nop

	/* switch to bank 1, save low registers */
	mov.l	rb_bit, r10
	bsr	set_sr
	 mov	#-1, r9

	bsr	save_low_regs
	 nop

	/* switch back to bank 0 */
	mov.l	rb_bit, r9
	not	r9, r9
	bsr	set_sr
	 mov	#0, r10

skip_regs_save:

	/* save sp, also set to internal ram */
	mov.l	r15, @(SH_SLEEP_SP, r5)
	mov	r5, r15

	/* save stbcr */
	bsr     save_register
	 mov    #SH_SLEEP_REG_STBCR, r0

	/* save mmu and cache context if needed */
	mov.l	@(SH_SLEEP_MODE, r5), r0
	tst	#SUSP_SH_MMU, r0
	bt	skip_mmu_save_disable

	/* save mmu state */
	bsr	save_register
	 mov	#SH_SLEEP_REG_PTEH, r0

	bsr	save_register
	 mov	#SH_SLEEP_REG_PTEL, r0

	bsr	save_register
	 mov	#SH_SLEEP_REG_TTB, r0

	bsr	save_register
	 mov	#SH_SLEEP_REG_TEA, r0

	bsr	save_register
	 mov	#SH_SLEEP_REG_MMUCR, r0

	bsr	save_register
	 mov	#SH_SLEEP_REG_PTEA, r0

	bsr	save_register
	 mov	#SH_SLEEP_REG_PASCR, r0

	bsr	save_register
	 mov	#SH_SLEEP_REG_IRMCR, r0

	/* invalidate TLBs and disable the MMU */
	bsr	get_register
	 mov	#SH_SLEEP_REG_MMUCR, r0
	mov	#4, r1
	mov.l	r1, @r0
	icbi	@r0

	/* save cache registers and disable caches */
	bsr	save_register
	 mov	#SH_SLEEP_REG_CCR, r0

	bsr	save_register
	 mov	#SH_SLEEP_REG_RAMCR, r0

	bsr	get_register
	 mov	#SH_SLEEP_REG_CCR, r0
	mov	#0, r1
	mov.l	r1, @r0
	icbi	@r0

skip_mmu_save_disable:
	/* call self-refresh entering code if needed */
	mov.l	@(SH_SLEEP_MODE, r5), r0
	tst	#SUSP_SH_SF, r0
	bt	skip_set_sf

	mov.l	@(SH_SLEEP_SF_PRE, r5), r0
	jsr	@r0
	 nop

skip_set_sf:
	mov.l	@(SH_SLEEP_MODE, r5), r0
	tst	#SUSP_SH_STANDBY, r0
	bt	test_rstandby

	/* set mode to "software standby mode" */
	bra	do_sleep
	 mov	#0x80, r1

test_rstandby:
	tst	#SUSP_SH_RSTANDBY, r0
	bt	test_ustandby

	/* setup BAR register */
	bsr	get_register
	 mov	#SH_SLEEP_REG_BAR, r0
	mov.l	@(SH_SLEEP_RESUME, r5), r1
	mov.l	r1, @r0

	/* set mode to "r-standby mode" */
	bra	do_sleep
	 mov	#0x20, r1

test_ustandby:
	tst	#SUSP_SH_USTANDBY, r0
	bt	force_sleep

	/* set mode to "u-standby mode" */
	bra	do_sleep
	 mov	#0x10, r1

force_sleep:

	/* set mode to "sleep mode" */
	mov	#0x00, r1

do_sleep:
	/* setup and enter selected standby mode */
	bsr     get_register
	 mov    #SH_SLEEP_REG_STBCR, r0
	mov.l	r1, @r0
again:
	sleep
	bra	again
	 nop

save_register:
	add	#SH_SLEEP_BASE_ADDR, r0
	mov.l	@(r0, r5), r1
	add	#-SH_SLEEP_BASE_ADDR, r0
	mov.l	@r1, r1
	add	#SH_SLEEP_BASE_DATA, r0
	mov.l	r1, @(r0, r5)
	add	#-SH_SLEEP_BASE_DATA, r0
	rts
	 nop

get_register:
	add	#SH_SLEEP_BASE_ADDR, r0
	mov.l	@(r0, r5), r0
	rts
	 nop

set_sr:
	stc	sr, r8
	and	r9, r8
	or	r10, r8
	ldc	r8, sr
	rts
	 nop

save_low_regs:
	mov.l	r7, @-r15
	mov.l	r6, @-r15
	mov.l	r5, @-r15
	mov.l	r4, @-r15
	mov.l	r3, @-r15
	mov.l	r2, @-r15
	mov.l	r1, @-r15
	rts
	 mov.l	r0, @-r15

	.balign 4
rb_bit:	.long	0x20000000 ! RB=1

ENTRY(sh_mobile_sleep_enter_end)

	.balign 4
ENTRY(sh_mobile_sleep_resume_start)

	/* figure out start address */
	bsr	0f
	 nop
0:
	sts	pr, k1
	mov.l	1f, k0
	and	k0, k1

	/* store pointer to data area in VBR */
	ldc	k1, vbr

	/* setup sr with saved sr */
	mov.l	@(SH_SLEEP_SR, k1), k0
	ldc	k0, sr

	/* now: user register set! */
	stc	vbr, r5

	/* setup spc with return address to c code */
	mov.l	@(SH_SLEEP_SPC, r5), r0
	ldc	r0, spc

	/* restore vbr */
	mov.l	@(SH_SLEEP_VBR, r5), r0
	ldc	r0, vbr

	/* setup ssr with saved sr */
	mov.l	@(SH_SLEEP_SR, r5), r0
	ldc	r0, ssr

	/* restore sp */
	mov.l   @(SH_SLEEP_SP, r5), r15

	/* restore sleep mode register */
	bsr     restore_register
	 mov    #SH_SLEEP_REG_STBCR, r0

	/* call self-refresh resume code if needed */
	mov.l	@(SH_SLEEP_MODE, r5), r0
	tst	#SUSP_SH_SF, r0
	bt	skip_restore_sf

	mov.l	@(SH_SLEEP_SF_POST, r5), r0
	jsr	@r0
	 nop

skip_restore_sf:
	/* restore mmu and cache state if needed */
	mov.l	@(SH_SLEEP_MODE, r5), r0
	tst	#SUSP_SH_MMU, r0
	bt	skip_restore_mmu

	/* restore mmu state */
	bsr	restore_register
	 mov	#SH_SLEEP_REG_PTEH, r0

	bsr	restore_register
	 mov	#SH_SLEEP_REG_PTEL, r0

	bsr	restore_register
	 mov	#SH_SLEEP_REG_TTB, r0

	bsr	restore_register
	 mov	#SH_SLEEP_REG_TEA, r0

	bsr	restore_register
	 mov	#SH_SLEEP_REG_PTEA, r0

	bsr	restore_register
	 mov	#SH_SLEEP_REG_PASCR, r0

	bsr	restore_register
	 mov	#SH_SLEEP_REG_IRMCR, r0

	bsr	restore_register
	 mov	#SH_SLEEP_REG_MMUCR, r0
	icbi	@r0

	/* restore cache settings */
	bsr	restore_register
	 mov	#SH_SLEEP_REG_RAMCR, r0
	icbi	@r0

	bsr	restore_register
	 mov	#SH_SLEEP_REG_CCR, r0
	icbi	@r0

skip_restore_mmu:

	/* restore general purpose registers if needed */
	mov.l	@(SH_SLEEP_MODE, r5), r0
	tst	#SUSP_SH_REGS, r0
	bt	skip_restore_regs

	/* switch to bank 1, restore low registers */
	mov.l	_rb_bit, r10
	bsr	_set_sr
	 mov	#-1, r9

	bsr	restore_low_regs
	 nop

	/* switch to bank0, restore low registers */
	mov.l	_rb_bit, r9
	not	r9, r9
	bsr	_set_sr
	 mov	#0, r10

	bsr	restore_low_regs
	 nop

	/* restore the rest of the registers */
	mov.l	@r15+, r8
	mov.l	@r15+, r9
	mov.l	@r15+, r10
	mov.l	@r15+, r11
	mov.l	@r15+, r12
	mov.l	@r15+, r13
	mov.l	@r15+, r14
	lds.l	@r15+, pr

skip_restore_regs:
	rte
	 nop

restore_register:
	add	#SH_SLEEP_BASE_DATA, r0
	mov.l	@(r0, r5), r1
	add	#-SH_SLEEP_BASE_DATA, r0
	add	#SH_SLEEP_BASE_ADDR, r0
	mov.l	@(r0, r5), r0
	mov.l	r1, @r0
	rts
	 nop

_set_sr:
	stc	sr, r8
	and	r9, r8
	or	r10, r8
	ldc	r8, sr
	rts
	 nop

restore_low_regs:
	mov.l	@r15+, r0
	mov.l	@r15+, r1
	mov.l	@r15+, r2
	mov.l	@r15+, r3
	mov.l	@r15+, r4
	mov.l	@r15+, r5
	mov.l	@r15+, r6
	rts
	 mov.l	@r15+, r7

	.balign 4
_rb_bit:	.long	0x20000000 ! RB=1
1:	.long	~0x7ff
ENTRY(sh_mobile_sleep_resume_end)
