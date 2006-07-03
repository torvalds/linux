/* cmode.S: clock mode management
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Woodhouse (dwmw2@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/ptrace.h>
#include <asm/errno.h>
#include <asm/cache.h>
#include <asm/spr-regs.h>

#define __addr_MASK	0xfeff9820	/* interrupt controller mask */

#define __addr_SDRAMC	0xfe000400	/* SDRAM controller regs */
#define SDRAMC_DSTS	0x28		/* SDRAM status */
#define SDRAMC_DSTS_SSI	0x00000001	/* indicates that the SDRAM is in self-refresh mode */
#define SDRAMC_DRCN	0x30		/* SDRAM refresh control */
#define SDRAMC_DRCN_SR	0x00000001	/* transition SDRAM into self-refresh mode */
#define __addr_CLKC	0xfeff9a00
#define CLKC_SWCMODE	0x00000008
#define __addr_LEDS	0xe1200004

.macro li v r
	sethi.p		%hi(\v),\r
	setlo		%lo(\v),\r
.endm

	.text
	.balign		4


###############################################################################
#
# Change CMODE
# - void frv_change_cmode(int cmode)
#
###############################################################################
	.globl		frv_change_cmode
        .type		frv_change_cmode,@function

.macro	LEDS v
#ifdef DEBUG_CMODE
	setlos	#~\v,gr10
	sti	gr10,@(gr11,#0)
	membar
#endif
.endm

frv_change_cmode:
	movsg		lr,gr9
#ifdef DEBUG_CMODE
	li		__addr_LEDS,gr11
#endif
	dcef		@(gr0,gr0),#1

	# Shift argument left by 24 bits to fit in SWCMODE register later.
	slli		gr8,#24,gr8

	# (1) Set '0' in the PSR.ET bit, and prohibit interrupts.
	movsg		psr,gr14
	andi		gr14,#~PSR_ET,gr3
	movgs		gr3,psr

#if 0 // Fujitsu recommend to skip this and will update docs.
	# (2) Set '0' to all bits of the MASK register of the interrupt
	#     controller, and mask interrupts.
	li		__addr_MASK,gr12
	ldi		@(gr12,#0),gr13
	li		0xffff0000,gr4
	sti		gr4,@(gr12,#0)
#endif

	# (3) Stop the transfer function of DMAC. Stop all the bus masters
	#     to access SDRAM and the internal resources.

	# (already done by caller)

	# (4) Preload a series of following instructions to the instruction
	#     cache.
	li		#__cmode_icache_lock_start,gr3
	li		#__cmode_icache_lock_end,gr4

1:	icpl		gr3,gr0,#1
	addi		gr3,#L1_CACHE_BYTES,gr3
	cmp		gr4,gr3,icc0
	bhi		icc0,#0,1b

	# Set up addresses in regs for later steps.
	setlos		SDRAMC_DRCN_SR,gr3
	li		__addr_SDRAMC,gr4
	li		__addr_CLKC,gr5
	ldi		@(gr5,#0),gr6
	li		#0x80000000,gr7
	or		gr6,gr7,gr6

	bra		__cmode_icache_lock_start

	.balign	L1_CACHE_BYTES
__cmode_icache_lock_start:

	# (5) Flush the content of all caches by the DCEF instruction.
	dcef		@(gr0,gr0),#1

	# (6) Execute loading the dummy for SDRAM.
	ldi		@(gr9,#0),gr0

	# (7) Set '1' to the DRCN.SR bit, and change SDRAM to the
	#     self-refresh mode. Execute the dummy load to all memory
	#     devices set to cacheable on the external bus side in parallel
	#     with this.
	sti		gr3,@(gr4,#SDRAMC_DRCN)

	# (8) Execute memory barrier instruction (MEMBAR).
	membar

	# (9) Read the DSTS register repeatedly until '1' stands in the
	#     DSTS.SSI field.
1:	ldi		@(gr4,#SDRAMC_DSTS),gr3
	andicc		gr3,#SDRAMC_DSTS_SSI,gr3,icc0
	beq		icc0,#0,1b

	# (10) Execute memory barrier instruction (MEMBAR).
	membar

#if 1
	# (11) Set the value of CMODE that you want to change to
	#      SWCMODE.SWCM[3:0].
	sti		gr8,@(gr5,#CLKC_SWCMODE)

	# (12) Set '1' to the CLKC.SWEN bit. In that case, do not change
	#      fields other than SWEN of the CLKC register.
	sti		gr6,@(gr5,#0)
#endif
	# (13) Execute the instruction just after the memory barrier
	# instruction that executes the self-loop 256 times. (Meanwhile,
	# the CMODE switch is done.)
	membar
	setlos		#256,gr7
2:	subicc		gr7,#1,gr7,icc0
	bne		icc0,#2,2b

	LEDS	0x36

	# (14) Release the self-refresh of SDRAM.
	sti		gr0,@(gr4,#SDRAMC_DRCN)

	# Wait for it...
3:	ldi		@(gr4,#SDRAMC_DSTS),gr3
	andicc		gr3,#SDRAMC_DSTS_SSI,gr3,icc0
	bne		icc0,#2,3b

#if 0
	li		0x0100000,gr10
4:	subicc		gr10,#1,gr10,icc0

	bne		icc0,#0,4b
#endif

__cmode_icache_lock_end:

	li		#__cmode_icache_lock_start,gr3
	li		#__cmode_icache_lock_end,gr4

4:	icul		gr3
	addi		gr3,#L1_CACHE_BYTES,gr3
	cmp		gr4,gr3,icc0
	bhi		icc0,#0,4b

#if 0 // Fujitsu recommend to skip this and will update docs.
	# (15) Release the interrupt mask setting of the MASK register of
	# the interrupt controller if necessary.
	sti		gr13,@(gr12,#0)
#endif
	# (16) Set  1' in the PSR.ET bit, and permit interrupt.
	movgs		gr14,psr

	bralr

	.size		frv_change_cmode, .-frv_change_cmode
