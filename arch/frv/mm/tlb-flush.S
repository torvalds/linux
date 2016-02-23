/* tlb-flush.S: TLB flushing routines
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/spr-regs.h>

.macro DEBUG ch
#	sethi.p		%hi(0xfeff9c00),gr4
#	setlo		%lo(0xfeff9c00),gr4
#	setlos		#\ch,gr5
#	stbi		gr5,@(gr4,#0)
#	membar
.endm

	.section	.rodata

	# sizes corresponding to TPXR.LMAX
	.balign		1
__tlb_lmax_sizes:
	.byte		0, 64, 0, 0
	.byte		0, 0, 0, 0
	.byte		0, 0, 0, 0
	.byte		0, 0, 0, 0

	.section	.text
	.balign		4

###############################################################################
#
# flush everything
# - void __flush_tlb_all(void)
#
###############################################################################
	.globl		__flush_tlb_all
	.type		__flush_tlb_all,@function
__flush_tlb_all:
	DEBUG		'A'

	# kill cached PGE value
	setlos		#0xffffffff,gr4
	movgs		gr4,scr0
	movgs		gr4,scr1

	# kill AMPR-cached TLB values
	movgs		gr0,iamlr1
	movgs		gr0,iampr1
	movgs		gr0,damlr1
	movgs		gr0,dampr1

	# find out how many lines there are
	movsg		tpxr,gr5
	sethi.p		%hi(__tlb_lmax_sizes),gr4
	srli		gr5,#TPXR_LMAX_SHIFT,gr5
	setlo.p		%lo(__tlb_lmax_sizes),gr4
	andi		gr5,#TPXR_LMAX_SMASK,gr5
	ldub		@(gr4,gr5),gr4

	# now, we assume that the TLB line step is page size in size
	setlos.p	#PAGE_SIZE,gr5
	setlos		#0,gr6
1:
	tlbpr		gr6,gr0,#6,#0
	subicc.p	gr4,#1,gr4,icc0
	add		gr6,gr5,gr6
	bne		icc0,#2,1b

	DEBUG		'B'
	bralr

	.size		__flush_tlb_all, .-__flush_tlb_all

###############################################################################
#
# flush everything to do with one context
# - void __flush_tlb_mm(unsigned long contextid [GR8])
#
###############################################################################
	.globl		__flush_tlb_mm
	.type		__flush_tlb_mm,@function
__flush_tlb_mm:
	DEBUG		'M'

	# kill cached PGE value
	setlos		#0xffffffff,gr4
	movgs		gr4,scr0
	movgs		gr4,scr1

	# specify the context we want to flush
	movgs		gr8,tplr

	# find out how many lines there are
	movsg		tpxr,gr5
	sethi.p		%hi(__tlb_lmax_sizes),gr4
	srli		gr5,#TPXR_LMAX_SHIFT,gr5
	setlo.p		%lo(__tlb_lmax_sizes),gr4
	andi		gr5,#TPXR_LMAX_SMASK,gr5
	ldub		@(gr4,gr5),gr4

	# now, we assume that the TLB line step is page size in size
	setlos.p	#PAGE_SIZE,gr5
	setlos		#0,gr6
0:
	tlbpr		gr6,gr0,#5,#0
	subicc.p	gr4,#1,gr4,icc0
	add		gr6,gr5,gr6
	bne		icc0,#2,0b

	DEBUG		'N'
	bralr

	.size		__flush_tlb_mm, .-__flush_tlb_mm

###############################################################################
#
# flush a range of addresses from the TLB
# - void __flush_tlb_page(unsigned long contextid [GR8],
#			  unsigned long start [GR9])
#
###############################################################################
	.globl		__flush_tlb_page
	.type		__flush_tlb_page,@function
__flush_tlb_page:
	# kill cached PGE value
	setlos		#0xffffffff,gr4
	movgs		gr4,scr0
	movgs		gr4,scr1

	# specify the context we want to flush
	movgs		gr8,tplr

	# zap the matching TLB line and AMR values
	setlos		#~(PAGE_SIZE-1),gr5
	and		gr9,gr5,gr9
	tlbpr		gr9,gr0,#5,#0

	bralr

	.size		__flush_tlb_page, .-__flush_tlb_page

###############################################################################
#
# flush a range of addresses from the TLB
# - void __flush_tlb_range(unsigned long contextid [GR8],
#			   unsigned long start [GR9],
#			   unsigned long end [GR10])
#
###############################################################################
	.globl		__flush_tlb_range
	.type		__flush_tlb_range,@function
__flush_tlb_range:
	# kill cached PGE value
	setlos		#0xffffffff,gr4
	movgs		gr4,scr0
	movgs		gr4,scr1

	# specify the context we want to flush
	movgs		gr8,tplr

	# round the start down to beginning of TLB line and end up to beginning of next TLB line
	setlos.p	#~(PAGE_SIZE-1),gr5
	setlos		#PAGE_SIZE,gr6
	subi.p		gr10,#1,gr10
	and		gr9,gr5,gr9
	and		gr10,gr5,gr10
2:
	tlbpr		gr9,gr0,#5,#0
	subcc.p		gr9,gr10,gr0,icc0
	add		gr9,gr6,gr9
	bne		icc0,#0,2b		; most likely a 1-page flush

	bralr

	.size		__flush_tlb_range, .-__flush_tlb_range
