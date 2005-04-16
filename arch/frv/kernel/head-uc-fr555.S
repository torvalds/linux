/* head-uc-fr555.S: FR555 uc-linux specific bits of initialisation
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
#include <asm/spr-regs.h>
#include <asm/mb86943a.h>
#include "head.inc"


#define __551_DARS0	0xfeff0100
#define __551_DARS1	0xfeff0104
#define __551_DARS2	0xfeff0108
#define __551_DARS3	0xfeff010c
#define __551_DAMK0	0xfeff0110
#define __551_DAMK1	0xfeff0114
#define __551_DAMK2	0xfeff0118
#define __551_DAMK3	0xfeff011c
#define __551_LCR	0xfeff1100
#define __551_LSBR	0xfeff1c00

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
# CC0	-			T if DARS0 is present
# CC1	-			T if DARS1 is present
# CC2	-			T if DARS2 is present
# CC3	-			T if DARS3 is present
#
###############################################################################
	.globl		__head_fr555_describe_sdram
__head_fr555_describe_sdram:
	sethi.p		%hi(__551_DARS0),gr14
	setlo		%lo(__551_DARS0),gr14
	setlos.p	#__551_DARS1-__551_DARS0,gr11
	setlos		#__551_DARS2-__551_DARS0,gr12
	setlos.p	#__551_DARS3-__551_DARS0,gr13
	setlos		#64,gr5			; cacheline size
	setlos		#20,gr15		; amount to shift addr by
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
	.globl		__head_fr555_set_busctl
__head_fr555_set_busctl:
	LEDS		0x100f
	sethi.p		%hi(__551_LSBR),gr10
	setlo		%lo(__551_LSBR),gr10
	sethi.p		%hi(__551_LCR),gr11
	setlo		%lo(__551_LCR),gr11

	# set the bus controller
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
	sethi.p		%hi(LED_ADDR - 0x20000000 +__region_CS2),gr30
	setlo		%lo(LED_ADDR - 0x20000000 +__region_CS2),gr30
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
	.globl		__head_fr555_survey_sdram
__head_fr555_survey_sdram:
	sethi.p		%hi(__551_DAMK0),gr11
	setlo		%lo(__551_DAMK0),gr11
	sethi.p		%hi(__551_DARS0),gr12
	setlo		%lo(__551_DARS0),gr12

	sethi.p		%hi(0xfff),gr17			; unused SDRAM AMK value
	setlo		%lo(0xfff),gr17
	setlos		#0,gr25

	ldi		@(gr11,#0x00),gr6		; DAMK0: bits 11:0 match addr 11:0
	subcc		gr6,gr17,gr0,icc0
	beq		icc0,#0,__head_no_DCS0
	ldi		@(gr12,#0x00),gr4		; DARS0
	add		gr25,gr6,gr25
	addi		gr25,#1,gr25
__head_no_DCS0:

	ldi		@(gr11,#0x04),gr6		; DAMK1: bits 11:0 match addr 11:0
	subcc		gr6,gr17,gr0,icc0
	beq		icc0,#0,__head_no_DCS1
	ldi		@(gr12,#0x04),gr4		; DARS1
	add		gr25,gr6,gr25
	addi		gr25,#1,gr25
__head_no_DCS1:

	ldi		@(gr11,#0x8),gr6		; DAMK2: bits 11:0 match addr 11:0
	subcc		gr6,gr17,gr0,icc0
	beq		icc0,#0,__head_no_DCS2
	ldi		@(gr12,#0x8),gr4		; DARS2
	add		gr25,gr6,gr25
	addi		gr25,#1,gr25
__head_no_DCS2:

	ldi		@(gr11,#0xc),gr6		; DAMK3: bits 11:0 match addr 11:0
	subcc		gr6,gr17,gr0,icc0
	beq		icc0,#0,__head_no_DCS3
	ldi		@(gr12,#0xc),gr4		; DARS3
	add		gr25,gr6,gr25
	addi		gr25,#1,gr25
__head_no_DCS3:

	slli		gr25,#20,gr25			; shift [11:0] -> [31:20]
	bralr

###############################################################################
#
# set the protection map with the I/DAMPR registers
#
#	ENTRY:			EXIT:
# GR25	SDRAM size		saved
# GR30	LED address		saved
#
###############################################################################
	.globl		__head_fr555_set_protection
__head_fr555_set_protection:
	movsg		lr,gr27

	sethi.p		%hi(0xfff00000),gr11
	setlo		%lo(0xfff00000),gr11

	# set the I/O region protection registers for FR555
	sethi.p		%hi(__region_IO),gr7
	setlo		%lo(__region_IO),gr7
	ori		gr7,#xAMPRx_SS_512Mb|xAMPRx_S_KERNEL|xAMPRx_C|xAMPRx_V,gr5
	movgs		gr0,iampr15
	movgs		gr0,iamlr15
	movgs		gr5,dampr15
	movgs		gr7,damlr15

	# need to tile the remaining IAMPR/DAMPR registers to cover as much of the RAM as possible
	# - start with the highest numbered registers
	sethi.p		%hi(__kernel_image_end),gr8
	setlo		%lo(__kernel_image_end),gr8
	sethi.p		%hi(32768),gr4			; allow for a maximal allocator bitmap
	setlo		%lo(32768),gr4
	add		gr8,gr4,gr8
	sethi.p		%hi(1024*2048-1),gr4		; round up to nearest 2MiB
	setlo		%lo(1024*2048-1),gr4
	add.p		gr8,gr4,gr8
	not		gr4,gr4
	and		gr8,gr4,gr8

	sethi.p		%hi(__page_offset),gr9
	setlo		%lo(__page_offset),gr9
	add		gr9,gr25,gr9

	# GR8 = base of uncovered RAM
	# GR9 = top of uncovered RAM
	# GR11 - mask for DAMLR/IAMLR regs
	#
	call		__head_split_region
	movgs		gr4,iampr14
	movgs		gr6,iamlr14
	movgs		gr5,dampr14
	movgs		gr7,damlr14
	call		__head_split_region
	movgs		gr4,iampr13
	movgs		gr6,iamlr13
	movgs		gr5,dampr13
	movgs		gr7,damlr13
	call		__head_split_region
	movgs		gr4,iampr12
	movgs		gr6,iamlr12
	movgs		gr5,dampr12
	movgs		gr7,damlr12
	call		__head_split_region
	movgs		gr4,iampr11
	movgs		gr6,iamlr11
	movgs		gr5,dampr11
	movgs		gr7,damlr11
	call		__head_split_region
	movgs		gr4,iampr10
	movgs		gr6,iamlr10
	movgs		gr5,dampr10
	movgs		gr7,damlr10
	call		__head_split_region
	movgs		gr4,iampr9
	movgs		gr6,iamlr9
	movgs		gr5,dampr9
	movgs		gr7,damlr9
	call		__head_split_region
	movgs		gr4,iampr8
	movgs		gr6,iamlr8
	movgs		gr5,dampr8
	movgs		gr7,damlr8

	call		__head_split_region
	movgs		gr4,iampr7
	movgs		gr6,iamlr7
	movgs		gr5,dampr7
	movgs		gr7,damlr7
	call		__head_split_region
	movgs		gr4,iampr6
	movgs		gr6,iamlr6
	movgs		gr5,dampr6
	movgs		gr7,damlr6
	call		__head_split_region
	movgs		gr4,iampr5
	movgs		gr6,iamlr5
	movgs		gr5,dampr5
	movgs		gr7,damlr5
	call		__head_split_region
	movgs		gr4,iampr4
	movgs		gr6,iamlr4
	movgs		gr5,dampr4
	movgs		gr7,damlr4
	call		__head_split_region
	movgs		gr4,iampr3
	movgs		gr6,iamlr3
	movgs		gr5,dampr3
	movgs		gr7,damlr3
	call		__head_split_region
	movgs		gr4,iampr2
	movgs		gr6,iamlr2
	movgs		gr5,dampr2
	movgs		gr7,damlr2
	call		__head_split_region
	movgs		gr4,iampr1
	movgs		gr6,iamlr1
	movgs		gr5,dampr1
	movgs		gr7,damlr1

	# cover kernel core image with kernel-only segment
	sethi.p		%hi(__page_offset),gr8
	setlo		%lo(__page_offset),gr8
	call		__head_split_region

#ifdef CONFIG_PROTECT_KERNEL
	ori.p		gr4,#xAMPRx_S_KERNEL,gr4
	ori		gr5,#xAMPRx_S_KERNEL,gr5
#endif

	movgs		gr4,iampr0
	movgs		gr6,iamlr0
	movgs		gr5,dampr0
	movgs		gr7,damlr0
	jmpl		@(gr27,gr0)
