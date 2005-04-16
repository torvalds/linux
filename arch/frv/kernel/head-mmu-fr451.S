/* head-mmu-fr451.S: FR451 mmu-linux specific bits of initialisation
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/mem-layout.h>
#include <asm/spr-regs.h>
#include <asm/mb86943a.h>
#include "head.inc"


#define __400_DBR0	0xfe000e00
#define __400_DBR1	0xfe000e08
#define __400_DBR2	0xfe000e10
#define __400_DBR3	0xfe000e18
#define __400_DAM0	0xfe000f00
#define __400_DAM1	0xfe000f08
#define __400_DAM2	0xfe000f10
#define __400_DAM3	0xfe000f18
#define __400_LGCR	0xfe000010
#define __400_LCR	0xfe000100
#define __400_LSBR	0xfe000c00

	.section	.text.init,"ax"
	.balign		4

###############################################################################
#
# describe the position and layout of the SDRAM controller registers
#
#	ENTRY:			EXIT:
# GR5	-			cacheline size
# GR11	-			displacement of 2nd SDRAM addr reg from GR14
# GR12	-			displacement of 3rd SDRAM addr reg from GR14
# GR13	-			displacement of 4th SDRAM addr reg from GR14
# GR14	-			address of 1st SDRAM addr reg
# GR15	-			amount to shift address by to match SDRAM addr reg
# GR26	&__head_reference	[saved]
# GR30	LED address		[saved]
# CC0	-			T if DBR0 is present
# CC1	-			T if DBR1 is present
# CC2	-			T if DBR2 is present
# CC3	-			T if DBR3 is present
#
###############################################################################
	.globl		__head_fr451_describe_sdram
__head_fr451_describe_sdram:
	sethi.p		%hi(__400_DBR0),gr14
	setlo		%lo(__400_DBR0),gr14
	setlos.p	#__400_DBR1-__400_DBR0,gr11
	setlos		#__400_DBR2-__400_DBR0,gr12
	setlos.p	#__400_DBR3-__400_DBR0,gr13
	setlos		#32,gr5			; cacheline size
	setlos.p	#0,gr15			; amount to shift addr reg by
	setlos		#0x00ff,gr4
	movgs		gr4,cccr		; extant DARS/DAMK regs
	bralr

###############################################################################
#
# rearrange the bus controller registers
#
#	ENTRY:			EXIT:
# GR26	&__head_reference	[saved]
# GR30	LED address		revised LED address
#
###############################################################################
	.globl		__head_fr451_set_busctl
__head_fr451_set_busctl:
	sethi.p		%hi(__400_LGCR),gr4
	setlo		%lo(__400_LGCR),gr4
	sethi.p		%hi(__400_LSBR),gr10
	setlo		%lo(__400_LSBR),gr10
	sethi.p		%hi(__400_LCR),gr11
	setlo		%lo(__400_LCR),gr11

	# set the bus controller
	ldi		@(gr4,#0),gr5
	ori		gr5,#0xff,gr5		; make sure all chip-selects are enabled
	sti		gr5,@(gr4,#0)

	sethi.p		%hi(__region_CS1),gr4
	setlo		%lo(__region_CS1),gr4
	sethi.p		%hi(__region_CS1_M),gr5
	setlo		%lo(__region_CS1_M),gr5
	sethi.p		%hi(__region_CS1_C),gr6
	setlo		%lo(__region_CS1_C),gr6
	sti		gr4,@(gr10,#1*0x08)
	sti		gr5,@(gr10,#1*0x08+0x100)
	sti		gr6,@(gr11,#1*0x08)
	sethi.p		%hi(__region_CS2),gr4
	setlo		%lo(__region_CS2),gr4
	sethi.p		%hi(__region_CS2_M),gr5
	setlo		%lo(__region_CS2_M),gr5
	sethi.p		%hi(__region_CS2_C),gr6
	setlo		%lo(__region_CS2_C),gr6
	sti		gr4,@(gr10,#2*0x08)
	sti		gr5,@(gr10,#2*0x08+0x100)
	sti		gr6,@(gr11,#2*0x08)
	sethi.p		%hi(__region_CS3),gr4
	setlo		%lo(__region_CS3),gr4
	sethi.p		%hi(__region_CS3_M),gr5
	setlo		%lo(__region_CS3_M),gr5
	sethi.p		%hi(__region_CS3_C),gr6
	setlo		%lo(__region_CS3_C),gr6
	sti		gr4,@(gr10,#3*0x08)
	sti		gr5,@(gr10,#3*0x08+0x100)
	sti		gr6,@(gr11,#3*0x08)
	sethi.p		%hi(__region_CS4),gr4
	setlo		%lo(__region_CS4),gr4
	sethi.p		%hi(__region_CS4_M),gr5
	setlo		%lo(__region_CS4_M),gr5
	sethi.p		%hi(__region_CS4_C),gr6
	setlo		%lo(__region_CS4_C),gr6
	sti		gr4,@(gr10,#4*0x08)
	sti		gr5,@(gr10,#4*0x08+0x100)
	sti		gr6,@(gr11,#4*0x08)
	sethi.p		%hi(__region_CS5),gr4
	setlo		%lo(__region_CS5),gr4
	sethi.p		%hi(__region_CS5_M),gr5
	setlo		%lo(__region_CS5_M),gr5
	sethi.p		%hi(__region_CS5_C),gr6
	setlo		%lo(__region_CS5_C),gr6
	sti		gr4,@(gr10,#5*0x08)
	sti		gr5,@(gr10,#5*0x08+0x100)
	sti		gr6,@(gr11,#5*0x08)
	sethi.p		%hi(__region_CS6),gr4
	setlo		%lo(__region_CS6),gr4
	sethi.p		%hi(__region_CS6_M),gr5
	setlo		%lo(__region_CS6_M),gr5
	sethi.p		%hi(__region_CS6_C),gr6
	setlo		%lo(__region_CS6_C),gr6
	sti		gr4,@(gr10,#6*0x08)
	sti		gr5,@(gr10,#6*0x08+0x100)
	sti		gr6,@(gr11,#6*0x08)
	sethi.p		%hi(__region_CS7),gr4
	setlo		%lo(__region_CS7),gr4
	sethi.p		%hi(__region_CS7_M),gr5
	setlo		%lo(__region_CS7_M),gr5
	sethi.p		%hi(__region_CS7_C),gr6
	setlo		%lo(__region_CS7_C),gr6
	sti		gr4,@(gr10,#7*0x08)
	sti		gr5,@(gr10,#7*0x08+0x100)
	sti		gr6,@(gr11,#7*0x08)
	membar
	bar

	# adjust LED bank address
#ifdef CONFIG_MB93091_VDK
	sethi.p		%hi(__region_CS2 + 0x01200004),gr30
	setlo		%lo(__region_CS2 + 0x01200004),gr30
#endif
	bralr

###############################################################################
#
# determine the total SDRAM size
#
#	ENTRY:			EXIT:
# GR25	-			SDRAM size
# GR26	&__head_reference	[saved]
# GR30	LED address		[saved]
#
###############################################################################
	.globl		__head_fr451_survey_sdram
__head_fr451_survey_sdram:
	sethi.p		%hi(__400_DAM0),gr11
	setlo		%lo(__400_DAM0),gr11
	sethi.p		%hi(__400_DBR0),gr12
	setlo		%lo(__400_DBR0),gr12

	sethi.p		%hi(0xfe000000),gr17		; unused SDRAM DBR value
	setlo		%lo(0xfe000000),gr17
	setlos		#0,gr25

	ldi		@(gr12,#0x00),gr4		; DAR0
	subcc		gr4,gr17,gr0,icc0
	beq		icc0,#0,__head_no_DCS0
	ldi		@(gr11,#0x00),gr6		; DAM0: bits 31:20 match addr 31:20
	add		gr25,gr6,gr25
	addi		gr25,#1,gr25
__head_no_DCS0:

	ldi		@(gr12,#0x08),gr4		; DAR1
	subcc		gr4,gr17,gr0,icc0
	beq		icc0,#0,__head_no_DCS1
	ldi		@(gr11,#0x08),gr6		; DAM1: bits 31:20 match addr 31:20
	add		gr25,gr6,gr25
	addi		gr25,#1,gr25
__head_no_DCS1:

	ldi		@(gr12,#0x10),gr4		; DAR2
	subcc		gr4,gr17,gr0,icc0
	beq		icc0,#0,__head_no_DCS2
	ldi		@(gr11,#0x10),gr6		; DAM2: bits 31:20 match addr 31:20
	add		gr25,gr6,gr25
	addi		gr25,#1,gr25
__head_no_DCS2:

	ldi		@(gr12,#0x18),gr4		; DAR3
	subcc		gr4,gr17,gr0,icc0
	beq		icc0,#0,__head_no_DCS3
	ldi		@(gr11,#0x18),gr6		; DAM3: bits 31:20 match addr 31:20
	add		gr25,gr6,gr25
	addi		gr25,#1,gr25
__head_no_DCS3:
	bralr

###############################################################################
#
# set the protection map with the I/DAMPR registers
#
#	ENTRY:			EXIT:
# GR25	SDRAM size		[saved]
# GR26	&__head_reference	[saved]
# GR30	LED address		[saved]
#
#
# Using this map:
#	REGISTERS	ADDRESS RANGE		VIEW
#	===============	======================	===============================
#	IAMPR0/DAMPR0	0xC0000000-0xCFFFFFFF	Cached kernel RAM Window
#	DAMPR11		0xE0000000-0xFFFFFFFF	Uncached I/O
#
###############################################################################
	.globl		__head_fr451_set_protection
__head_fr451_set_protection:
	movsg		lr,gr27

	# set the I/O region protection registers for FR451 in MMU mode
#define PGPROT_IO	xAMPRx_L|xAMPRx_M|xAMPRx_S_KERNEL|xAMPRx_C|xAMPRx_V

	sethi.p		%hi(__region_IO),gr5
	setlo		%lo(__region_IO),gr5
	setlos		#PGPROT_IO|xAMPRx_SS_512Mb,gr4
	or		gr4,gr5,gr4
	movgs		gr5,damlr11			; General I/O tile
	movgs		gr4,dampr11

	# need to open a window onto at least part of the RAM for the kernel's use
	sethi.p		%hi(__sdram_base),gr8
	setlo		%lo(__sdram_base),gr8		; physical address
	sethi.p		%hi(__page_offset),gr9
	setlo		%lo(__page_offset),gr9		; virtual address

	setlos		#xAMPRx_L|xAMPRx_M|xAMPRx_SS_256Mb|xAMPRx_S_KERNEL|xAMPRx_V,gr11
	or		gr8,gr11,gr8

	movgs		gr9,iamlr0			; mapped from real address 0
	movgs		gr8,iampr0			; cached kernel memory at 0xC0000000
	movgs		gr9,damlr0
	movgs		gr8,dampr0

	# set a temporary mapping for the kernel running at address 0 until we've turned on the MMU
	sethi.p		%hi(__sdram_base),gr9
	setlo		%lo(__sdram_base),gr9		; virtual address

	and.p		gr4,gr11,gr4
	and		gr5,gr11,gr5
	or.p		gr4,gr11,gr4
	or		gr5,gr11,gr5

	movgs		gr9,iamlr1			; mapped from real address 0
	movgs		gr8,iampr1			; cached kernel memory at 0x00000000
	movgs		gr9,damlr1
	movgs		gr8,dampr1

	# we use DAMR2-10 for kmap_atomic(), cache flush and TLB management
	# since the DAMLR regs are not going to change, we can set them now
	# also set up IAMLR2 to the same as DAMLR5
	sethi.p		%hi(KMAP_ATOMIC_PRIMARY_FRAME),gr4
	setlo		%lo(KMAP_ATOMIC_PRIMARY_FRAME),gr4
	sethi.p		%hi(PAGE_SIZE),gr5
	setlo		%lo(PAGE_SIZE),gr5

	movgs		gr4,damlr2
	movgs		gr4,iamlr2
	add		gr4,gr5,gr4
	movgs		gr4,damlr3
	add		gr4,gr5,gr4
	movgs		gr4,damlr4
	add		gr4,gr5,gr4
	movgs		gr4,damlr5
	add		gr4,gr5,gr4
	movgs		gr4,damlr6
	add		gr4,gr5,gr4
	movgs		gr4,damlr7
	add		gr4,gr5,gr4
	movgs		gr4,damlr8
	add		gr4,gr5,gr4
	movgs		gr4,damlr9
	add		gr4,gr5,gr4
	movgs		gr4,damlr10

	movgs		gr0,dampr2
	movgs		gr0,dampr4
	movgs		gr0,dampr5
	movgs		gr0,dampr6
	movgs		gr0,dampr7
	movgs		gr0,dampr8
	movgs		gr0,dampr9
	movgs		gr0,dampr10

	movgs		gr0,iamlr3
	movgs		gr0,iamlr4
	movgs		gr0,iamlr5
	movgs		gr0,iamlr6
	movgs		gr0,iamlr7

	movgs		gr0,iampr2
	movgs		gr0,iampr3
	movgs		gr0,iampr4
	movgs		gr0,iampr5
	movgs		gr0,iampr6
	movgs		gr0,iampr7

	# start in TLB context 0 with the swapper's page tables
	movgs		gr0,cxnr

	sethi.p		%hi(swapper_pg_dir),gr4
	setlo		%lo(swapper_pg_dir),gr4
	sethi.p		%hi(__page_offset),gr5
	setlo		%lo(__page_offset),gr5
	sub		gr4,gr5,gr4
	movgs		gr4,ttbr
	setlos		#xAMPRx_L|xAMPRx_M|xAMPRx_SS_16Kb|xAMPRx_S|xAMPRx_C|xAMPRx_V,gr5
	or		gr4,gr5,gr4
	movgs		gr4,dampr3

	# the FR451 also has an extra trap base register
	movsg		tbr,gr4
	movgs		gr4,btbr

	LEDS		0x3300
	jmpl		@(gr27,gr0)

###############################################################################
#
# finish setting up the protection registers
#
###############################################################################
	.globl		__head_fr451_finalise_protection
__head_fr451_finalise_protection:
	# turn on the timers as appropriate
	movgs		gr0,timerh
	movgs		gr0,timerl
	movgs		gr0,timerd
	movsg		hsr0,gr4
	sethi.p		%hi(HSR0_ETMI),gr5
	setlo		%lo(HSR0_ETMI),gr5
	or		gr4,gr5,gr4
	movgs		gr4,hsr0

	# clear the TLB entry cache
	movgs		gr0,iamlr1
	movgs		gr0,iampr1
	movgs		gr0,damlr1
	movgs		gr0,dampr1

	# clear the PGE cache
	sethi.p		%hi(__flush_tlb_all),gr4
	setlo		%lo(__flush_tlb_all),gr4
	jmpl		@(gr4,gr0)
