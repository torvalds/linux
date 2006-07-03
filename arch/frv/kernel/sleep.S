/* sleep.S: power saving mode entry
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
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/errno.h>
#include <asm/cache.h>
#include <asm/spr-regs.h>

#define __addr_MASK	0xfeff9820	/* interrupt controller mask */

#define __addr_FR55X_DRCN	0xfeff0218      /* Address of DRCN register */
#define FR55X_DSTS_OFFSET	-4		/* Offset from DRCN to DSTS */
#define FR55X_SDRAMC_DSTS_SSI	0x00000002	/* indicates that the SDRAM is in self-refresh mode */

#define __addr_FR4XX_DRCN	0xfe000430      /* Address of DRCN register */
#define FR4XX_DSTS_OFFSET	-8		/* Offset from DRCN to DSTS */
#define FR4XX_SDRAMC_DSTS_SSI	0x00000001	/* indicates that the SDRAM is in self-refresh mode */

#define SDRAMC_DRCN_SR	0x00000001	/* transition SDRAM into self-refresh mode */

	.section	.bss
	.balign		8
	.globl		__sleep_save_area
__sleep_save_area:
	.space		16


	.text
	.balign		4

.macro li v r
	sethi.p		%hi(\v),\r
	setlo		%lo(\v),\r
.endm

#ifdef CONFIG_PM
###############################################################################
#
# CPU suspension routine
# - void frv_cpu_suspend(unsigned long pdm_mode)
#
###############################################################################
	.globl		frv_cpu_suspend
        .type		frv_cpu_suspend,@function
frv_cpu_suspend:

	#----------------------------------------------------
	# save hsr0, psr, isr, and lr for resume code
	#----------------------------------------------------
	li		__sleep_save_area,gr11

	movsg		hsr0,gr4
	movsg		psr,gr5
	movsg		isr,gr6
	movsg		lr,gr7
	stdi		gr4,@(gr11,#0)
	stdi		gr6,@(gr11,#8)

	# store the return address from sleep in GR14, and its complement in GR13 as a check
	li		__ramboot_resume,gr14
#ifdef CONFIG_MMU
	# Resume via RAMBOOT# will turn MMU off, so bootloader needs a physical address.
	sethi.p		%hi(__page_offset),gr13
	setlo		%lo(__page_offset),gr13
	sub		gr14,gr13,gr14
#endif
	not		gr14,gr13

	#----------------------------------------------------
	# preload and lock into icache that code which may have to run
	# when dram is in self-refresh state.
	#----------------------------------------------------
	movsg		hsr0, gr3
	li		HSR0_ICE,gr4
	or		gr3,gr4,gr3
	movgs		gr3,hsr0
	or		gr3,gr8,gr7	// add the sleep bits for later

	li		#__icache_lock_start,gr3
	li		#__icache_lock_end,gr4
1:	icpl		gr3,gr0,#1
	addi		gr3,#L1_CACHE_BYTES,gr3
	cmp		gr4,gr3,icc0
	bhi		icc0,#0,1b

	# disable exceptions
	movsg		psr,gr8
	andi.p		gr8,#~PSR_PIL,gr8
	andi		gr8,~PSR_ET,gr8
	movgs		gr8,psr
	ori		gr8,#PSR_ET,gr8

	srli		gr8,#28,gr4
	subicc		gr4,#3,gr0,icc0
	beq		icc0,#0,1f
	# FR4xx
	li		__addr_FR4XX_DRCN,gr4
	li		FR4XX_SDRAMC_DSTS_SSI,gr5
	li		FR4XX_DSTS_OFFSET,gr6
	bra		__icache_lock_start
1:
	# FR5xx
	li		__addr_FR55X_DRCN,gr4
	li		FR55X_SDRAMC_DSTS_SSI,gr5
	li		FR55X_DSTS_OFFSET,gr6
	bra		__icache_lock_start

	.size		frv_cpu_suspend, .-frv_cpu_suspend

#
# the final part of the sleep sequence...
# - we want it to be be cacheline aligned so we can lock it into the icache easily
#  On entry:	gr7 holds desired hsr0 sleep value
#               gr8 holds desired psr sleep value
#
	.balign		L1_CACHE_BYTES
        .type		__icache_lock_start,@function
__icache_lock_start:

	#----------------------------------------------------
	# put SDRAM in self-refresh mode
	#----------------------------------------------------

	# Flush all data in the cache using the DCEF instruction.
	dcef		@(gr0,gr0),#1

	# Stop DMAC transfer

	# Execute dummy load from SDRAM
	ldi		@(gr11,#0),gr11

	# put the SDRAM into self-refresh mode
	ld              @(gr4,gr0),gr11
	ori		gr11,#SDRAMC_DRCN_SR,gr11
	st		gr11,@(gr4,gr0)
	membar

	# wait for SDRAM to reach self-refresh mode
1:	ld		@(gr4,gr6),gr11
	andcc		gr11,gr5,gr11,icc0
	beq		icc0,#0,1b

	#  Set the GPIO register so that the IRQ[3:0] pins become valid, as required.
	#  Set the clock mode (CLKC register) as required.
	#     - At this time, also set the CLKC register P0 bit.

	# Set the HSR0 register PDM field.
	movgs		gr7,hsr0

	# Execute NOP 32 times.
	.rept		32
	nop
	.endr

#if 0 // Fujitsu recommend to skip this and will update docs.
	#      Release the interrupt mask setting of the MASK register of the
	#      interrupt controller if necessary.
	sti		gr10,@(gr9,#0)
	membar
#endif

	# Set the PSR register ET bit to 1 to enable interrupts.
	movgs		gr8,psr

	###################################################
	# this is only reached if waking up via interrupt
	###################################################

	# Execute NOP 32 times.
	.rept		32
	nop
	.endr

	#----------------------------------------------------
	# wake SDRAM from self-refresh mode
	#----------------------------------------------------
	ld              @(gr4,gr0),gr11
	andi		gr11,#~SDRAMC_DRCN_SR,gr11
	st		gr11,@(gr4,gr0)
	membar
2:
	ld		@(gr4,gr6),gr11	// Wait for it to come back...
	andcc		gr11,gr5,gr0,icc0
	bne		icc0,0,2b

	# wait for the SDRAM to stabilise
	li		0x0100000,gr3
3:	subicc		gr3,#1,gr3,icc0
	bne		icc0,#0,3b

	# now that DRAM is back, this is the end of the code which gets
	# locked in icache.
__icache_lock_end:
	.size		__icache_lock_start, .-__icache_lock_start

	# Fall-through to the RAMBOOT# wakeup path

###############################################################################
#
#  resume from suspend re-entry point reached via RAMBOOT# and bootloader
#
###############################################################################
__ramboot_resume:

	#----------------------------------------------------
	# restore hsr0, psr, isr, and leave saved lr in gr7
	#----------------------------------------------------
	li		__sleep_save_area,gr11
#ifdef CONFIG_MMU
	movsg		hsr0,gr4
	sethi.p		%hi(HSR0_EXMMU),gr3
	setlo		%lo(HSR0_EXMMU),gr3
	andcc		gr3,gr4,gr0,icc0
	bne		icc0,#0,2f

	# need to use physical address
	sethi.p		%hi(__page_offset),gr3
	setlo		%lo(__page_offset),gr3
	sub		gr11,gr3,gr11

	# flush all tlb entries
	setlos		#64,gr4
	setlos.p	#PAGE_SIZE,gr5
	setlos		#0,gr6
1:
	tlbpr		gr6,gr0,#6,#0
	subicc.p	gr4,#1,gr4,icc0
	add		gr6,gr5,gr6
	bne		icc0,#2,1b

	# need a temporary mapping for the current physical address we are
	# using between time MMU is enabled and jump to virtual address is
	# made.
	sethi.p		%hi(0x00000000),gr4
	setlo		%lo(0x00000000),gr4		; physical address
	setlos		#xAMPRx_L|xAMPRx_M|xAMPRx_SS_256Mb|xAMPRx_S_KERNEL|xAMPRx_V,gr5
	or		gr4,gr5,gr5

	movsg		cxnr,gr13
	or		gr4,gr13,gr4

	movgs		gr4,iamlr1			; mapped from real address 0
	movgs		gr5,iampr1			; cached kernel memory at 0x00000000
2:
#endif

	lddi		@(gr11,#0),gr4 ; hsr0, psr
	lddi		@(gr11,#8),gr6 ; isr, lr
	movgs		gr4,hsr0
	bar

#ifdef CONFIG_MMU
	sethi.p		%hi(1f),gr11
	setlo		%lo(1f),gr11
	jmpl		@(gr11,gr0)
1:
	movgs		gr0,iampr1 	; get rid of temporary mapping
#endif
	movgs		gr5,psr
	movgs		gr6,isr

	#----------------------------------------------------
	# unlock the icache which was locked before going to sleep
	#----------------------------------------------------
	li		__icache_lock_start,gr3
	li		__icache_lock_end,gr4
1:	icul		gr3
	addi		gr3,#L1_CACHE_BYTES,gr3
	cmp		gr4,gr3,icc0
	bhi		icc0,#0,1b

	#----------------------------------------------------
	# back to business as usual
	#----------------------------------------------------
	jmpl		@(gr7,gr0)		;

#endif /* CONFIG_PM */

###############################################################################
#
# CPU core sleep mode routine
#
###############################################################################
	.globl		frv_cpu_core_sleep
        .type		frv_cpu_core_sleep,@function
frv_cpu_core_sleep:

	# Preload into icache.
	li		#__core_sleep_icache_lock_start,gr3
	li		#__core_sleep_icache_lock_end,gr4

1:	icpl		gr3,gr0,#1
	addi		gr3,#L1_CACHE_BYTES,gr3
	cmp		gr4,gr3,icc0
	bhi		icc0,#0,1b

	bra	__core_sleep_icache_lock_start

	.balign L1_CACHE_BYTES
__core_sleep_icache_lock_start:

	# (1) Set the PSR register ET bit to 0 to disable interrupts.
	movsg		psr,gr8
	andi.p		gr8,#~(PSR_PIL),gr8
	andi		gr8,#~(PSR_ET),gr4
	movgs		gr4,psr

#if 0 // Fujitsu recommend to skip this and will update docs.
	# (2) Set '1' to all bits in the MASK register of the interrupt
	#     controller and mask interrupts.
	sethi.p		%hi(__addr_MASK),gr9
	setlo		%lo(__addr_MASK),gr9
	sethi.p		%hi(0xffff0000),gr4
	setlo		%lo(0xffff0000),gr4
	ldi		@(gr9,#0),gr10
	sti		gr4,@(gr9,#0)
#endif
	# (3) Flush all data in the cache using the DCEF instruction.
	dcef		@(gr0,gr0),#1

	# (4) Execute the memory barrier instruction
	membar

	# (5) Set the GPIO register so that the IRQ[3:0] pins become valid, as required.
	# (6) Set the clock mode (CLKC register) as required.
	#     - At this time, also set the CLKC register P0 bit.
	# (7) Set the HSR0 register PDM field to  001 .
	movsg		hsr0,gr4
	ori		gr4,HSR0_PDM_CORE_SLEEP,gr4
	movgs		gr4,hsr0

	# (8) Execute NOP 32 times.
	.rept		32
	nop
	.endr

#if 0 // Fujitsu recommend to skip this and will update docs.
	# (9) Release the interrupt mask setting of the MASK register of the
	#     interrupt controller if necessary.
	sti		gr10,@(gr9,#0)
	membar
#endif

	# (10) Set the PSR register ET bit to 1 to enable interrupts.
	movgs		gr8,psr

__core_sleep_icache_lock_end:

	# Unlock from icache
	li	__core_sleep_icache_lock_start,gr3
	li	__core_sleep_icache_lock_end,gr4
1:	icul		gr3
	addi		gr3,#L1_CACHE_BYTES,gr3
	cmp		gr4,gr3,icc0
	bhi		icc0,#0,1b

	bralr

	.size		frv_cpu_core_sleep, .-frv_cpu_core_sleep
