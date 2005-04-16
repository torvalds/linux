/* head-uc-fr451.S: FR451 uc-linux specific bits of initialisation
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
# set the protection map with the I/DAMPR registers
#
#	ENTRY:			EXIT:
# GR25	SDRAM size		[saved]
# GR26	&__head_reference	[saved]
# GR30	LED address		[saved]
#
###############################################################################
	.globl		__head_fr451_set_protection
__head_fr451_set_protection:
	movsg		lr,gr27

	movgs		gr0,dampr10
	movgs		gr0,damlr10
	movgs		gr0,dampr9
	movgs		gr0,damlr9
	movgs		gr0,dampr8
	movgs		gr0,damlr8

	# set the I/O region protection registers for FR401/3/5
	sethi.p		%hi(__region_IO),gr5
	setlo		%lo(__region_IO),gr5
	sethi.p		%hi(0x1fffffff),gr7
	setlo		%lo(0x1fffffff),gr7
	ori		gr5,#xAMPRx_SS_512Mb|xAMPRx_S_KERNEL|xAMPRx_C|xAMPRx_V,gr5
	movgs		gr5,dampr11			; General I/O tile
	movgs		gr7,damlr11

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

	sethi.p		%hi(0xffffc000),gr11
	setlo		%lo(0xffffc000),gr11

	# GR8 = base of uncovered RAM
	# GR9 = top of uncovered RAM
	# GR11 = xAMLR mask
	LEDS		0x3317
	call		__head_split_region
	movgs		gr4,iampr7
	movgs		gr6,iamlr7
	movgs		gr5,dampr7
	movgs		gr7,damlr7

	LEDS		0x3316
	call		__head_split_region
	movgs		gr4,iampr6
	movgs		gr6,iamlr6
	movgs		gr5,dampr6
	movgs		gr7,damlr6

	LEDS		0x3315
	call		__head_split_region
	movgs		gr4,iampr5
	movgs		gr6,iamlr5
	movgs		gr5,dampr5
	movgs		gr7,damlr5

	LEDS		0x3314
	call		__head_split_region
	movgs		gr4,iampr4
	movgs		gr6,iamlr4
	movgs		gr5,dampr4
	movgs		gr7,damlr4

	LEDS		0x3313
	call		__head_split_region
	movgs		gr4,iampr3
	movgs		gr6,iamlr3
	movgs		gr5,dampr3
	movgs		gr7,damlr3

	LEDS		0x3312
	call		__head_split_region
	movgs		gr4,iampr2
	movgs		gr6,iamlr2
	movgs		gr5,dampr2
	movgs		gr7,damlr2

	LEDS		0x3311
	call		__head_split_region
	movgs		gr4,iampr1
	movgs		gr6,iamlr1
	movgs		gr5,dampr1
	movgs		gr7,damlr1

	# cover kernel core image with kernel-only segment
	LEDS		0x3310
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

	# start in TLB context 0 with no page tables
	movgs		gr0,cxnr
	movgs		gr0,ttbr

	# the FR451 also has an extra trap base register
	movsg		tbr,gr4
	movgs		gr4,btbr

	# turn on the timers as appropriate
	movgs		gr0,timerh
	movgs		gr0,timerl
	movgs		gr0,timerd
	movsg		hsr0,gr4
	sethi.p		%hi(HSR0_ETMI),gr5
	setlo		%lo(HSR0_ETMI),gr5
	or		gr4,gr5,gr4
	movgs		gr4,hsr0

	LEDS		0x3300
	jmpl		@(gr27,gr0)
