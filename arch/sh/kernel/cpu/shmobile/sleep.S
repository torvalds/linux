/*
 * arch/sh/kernel/cpu/sh4a/sleep-sh_mobile.S
 *
 * Sleep mode and Standby modes support for SuperH Mobile
 *
 *  Copyright (C) 2009 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
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

	/* save sp */
	mov.l	r15, @(SH_SLEEP_SP, r5)

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

	.balign 4
1:	.long	~0x7ff
ENTRY(sh_mobile_sleep_resume_end)
