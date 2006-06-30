/* tlb-miss.S: TLB miss handlers
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
#include <asm/pgtable.h>
#include <asm/highmem.h>
#include <asm/spr-regs.h>

	.section	.text
	.balign		4

	.globl		__entry_insn_mmu_miss
__entry_insn_mmu_miss:
	break
	nop

	.globl		__entry_insn_mmu_exception
__entry_insn_mmu_exception:
	break
	nop

	.globl		__entry_data_mmu_miss
__entry_data_mmu_miss:
	break
	nop

	.globl		__entry_data_mmu_exception
__entry_data_mmu_exception:
	break
	nop

###############################################################################
#
# handle a lookup failure of one sort or another in a kernel TLB handler
# On entry:
#   GR29 - faulting address
#   SCR2 - saved CCR
#
###############################################################################
	.type		__tlb_kernel_fault,@function
__tlb_kernel_fault:
	# see if we're supposed to re-enable single-step mode upon return
	sethi.p		%hi(__break_tlb_miss_return_break),gr30
	setlo		%lo(__break_tlb_miss_return_break),gr30
	movsg		pcsr,gr31

	subcc		gr31,gr30,gr0,icc0
	beq		icc0,#0,__tlb_kernel_fault_sstep

	movsg		scr2,gr30
	movgs		gr30,ccr
	movgs		gr29,scr2			/* save EAR0 value */
	sethi.p		%hi(__kernel_current_task),gr29
	setlo		%lo(__kernel_current_task),gr29
	ldi.p		@(gr29,#0),gr29			/* restore GR29 */

	bra		__entry_kernel_handle_mmu_fault

	# we've got to re-enable single-stepping
__tlb_kernel_fault_sstep:
	sethi.p		%hi(__break_tlb_miss_real_return_info),gr30
	setlo		%lo(__break_tlb_miss_real_return_info),gr30
	lddi		@(gr30,0),gr30
	movgs		gr30,pcsr
	movgs		gr31,psr

	movsg		scr2,gr30
	movgs		gr30,ccr
	movgs		gr29,scr2			/* save EAR0 value */
	sethi.p		%hi(__kernel_current_task),gr29
	setlo		%lo(__kernel_current_task),gr29
	ldi.p		@(gr29,#0),gr29			/* restore GR29 */
	bra		__entry_kernel_handle_mmu_fault_sstep

	.size		__tlb_kernel_fault, .-__tlb_kernel_fault

###############################################################################
#
# handle a lookup failure of one sort or another in a user TLB handler
# On entry:
#   GR28 - faulting address
#   SCR2 - saved CCR
#
###############################################################################
	.type		__tlb_user_fault,@function
__tlb_user_fault:
	# see if we're supposed to re-enable single-step mode upon return
	sethi.p		%hi(__break_tlb_miss_return_break),gr30
	setlo		%lo(__break_tlb_miss_return_break),gr30
	movsg		pcsr,gr31
	subcc		gr31,gr30,gr0,icc0
	beq		icc0,#0,__tlb_user_fault_sstep

	movsg		scr2,gr30
	movgs		gr30,ccr
	bra		__entry_uspace_handle_mmu_fault

	# we've got to re-enable single-stepping
__tlb_user_fault_sstep:
	sethi.p		%hi(__break_tlb_miss_real_return_info),gr30
	setlo		%lo(__break_tlb_miss_real_return_info),gr30
	lddi		@(gr30,0),gr30
	movgs		gr30,pcsr
	movgs		gr31,psr
	movsg		scr2,gr30
	movgs		gr30,ccr
	bra		__entry_uspace_handle_mmu_fault_sstep

	.size		__tlb_user_fault, .-__tlb_user_fault

###############################################################################
#
# Kernel instruction TLB miss handler
# On entry:
#   GR1   - kernel stack pointer
#   GR28  - saved exception frame pointer
#   GR29  - faulting address
#   GR31  - EAR0 ^ SCR0
#   SCR0  - base of virtual range covered by cached PGE from last ITLB miss (or 0xffffffff)
#   DAMR3 - mapped page directory
#   DAMR4 - mapped page table as matched by SCR0
#
###############################################################################
	.globl		__entry_kernel_insn_tlb_miss
	.type		__entry_kernel_insn_tlb_miss,@function
__entry_kernel_insn_tlb_miss:
#if 0
	sethi.p		%hi(0xe1200004),gr30
	setlo		%lo(0xe1200004),gr30
	st		gr0,@(gr30,gr0)
	sethi.p		%hi(0xffc00100),gr30
	setlo		%lo(0xffc00100),gr30
	sth		gr30,@(gr30,gr0)
	membar
#endif

	movsg		ccr,gr30			/* save CCR */
	movgs		gr30,scr2

	# see if the cached page table mapping is appropriate
	srlicc.p	gr31,#26,gr0,icc0
	setlos		0x3ffc,gr30
	srli.p		gr29,#12,gr31			/* use EAR0[25:14] as PTE index */
	bne		icc0,#0,__itlb_k_PTD_miss

__itlb_k_PTD_mapped:
	# access the PTD with EAR0[25:14]
	# - DAMLR4 points to the virtual address of the appropriate page table
	# - the PTD holds 4096 PTEs
	# - the PTD must be accessed uncached
	# - the PTE must be marked accessed if it was valid
	#
	and		gr31,gr30,gr31
	movsg		damlr4,gr30
	add		gr30,gr31,gr31
	ldi		@(gr31,#0),gr30			/* fetch the PTE */
	andicc		gr30,#_PAGE_PRESENT,gr0,icc0
	ori.p		gr30,#_PAGE_ACCESSED,gr30
	beq		icc0,#0,__tlb_kernel_fault	/* jump if PTE invalid */
	sti.p		gr30,@(gr31,#0)			/* update the PTE */
	andi		gr30,#~_PAGE_ACCESSED,gr30

	# we're using IAMR1 as an extra TLB entry
	# - punt the entry here (if valid) to the real TLB and then replace with the new PTE
	# - need to check DAMR1 lest we cause an multiple-DAT-hit exception
	# - IAMPR1 has no WP bit, and we mustn't lose WP information
	movsg		iampr1,gr31
	andicc		gr31,#xAMPRx_V,gr0,icc0
	setlos.p	0xfffff000,gr31
	beq		icc0,#0,__itlb_k_nopunt		/* punt not required */

	movsg		iamlr1,gr31
	movgs		gr31,tplr			/* set TPLR.CXN */
	tlbpr		gr31,gr0,#4,#0			/* delete matches from TLB, IAMR1, DAMR1 */

	movsg		dampr1,gr31
	ori		gr31,#xAMPRx_V,gr31		/* entry was invalidated by tlbpr #4 */
	movgs		gr31,tppr
	movsg		iamlr1,gr31			/* set TPLR.CXN */
	movgs		gr31,tplr
	tlbpr		gr31,gr0,#2,#0			/* save to the TLB */
	movsg		tpxr,gr31			/* check the TLB write error flag */
	andicc.p	gr31,#TPXR_E,gr0,icc0
	setlos		#0xfffff000,gr31
	bne		icc0,#0,__tlb_kernel_fault

__itlb_k_nopunt:

	# assemble the new TLB entry
	and		gr29,gr31,gr29
	movsg		cxnr,gr31
	or		gr29,gr31,gr29
	movgs		gr29,iamlr1			/* xAMLR = address | context number */
	movgs		gr30,iampr1
	movgs		gr29,damlr1
	movgs		gr30,dampr1

	# return, restoring registers
	movsg		scr2,gr30
	movgs		gr30,ccr
	sethi.p		%hi(__kernel_current_task),gr29
	setlo		%lo(__kernel_current_task),gr29
	ldi		@(gr29,#0),gr29
	rett		#0
	beq		icc0,#3,0			/* prevent icache prefetch */

	# the PTE we want wasn't in the PTD we have mapped, so we need to go looking for a more
	# appropriate page table and map that instead
	#   - access the PGD with EAR0[31:26]
	#   - DAMLR3 points to the virtual address of the page directory
	#   - the PGD holds 64 PGEs and each PGE/PME points to a set of page tables
__itlb_k_PTD_miss:
	srli		gr29,#26,gr31			/* calculate PGE offset */
	slli		gr31,#8,gr31			/* and clear bottom bits */

	movsg		damlr3,gr30
	ld		@(gr31,gr30),gr30		/* access the PGE */

	andicc.p	gr30,#_PAGE_PRESENT,gr0,icc0
	andicc		gr30,#xAMPRx_SS,gr0,icc1

	# map this PTD instead and record coverage address
	ori.p		gr30,#xAMPRx_L|xAMPRx_SS_16Kb|xAMPRx_S|xAMPRx_C|xAMPRx_V,gr30
	beq		icc0,#0,__tlb_kernel_fault	/* jump if PGE not present */
	slli.p		gr31,#18,gr31
	bne		icc1,#0,__itlb_k_bigpage
	movgs		gr30,dampr4
	movgs		gr31,scr0

	# we can now resume normal service
	setlos		0x3ffc,gr30
	srli.p		gr29,#12,gr31			/* use EAR0[25:14] as PTE index */
	bra		__itlb_k_PTD_mapped

__itlb_k_bigpage:
	break
	nop

	.size		__entry_kernel_insn_tlb_miss, .-__entry_kernel_insn_tlb_miss

###############################################################################
#
# Kernel data TLB miss handler
# On entry:
#   GR1   - kernel stack pointer
#   GR28  - saved exception frame pointer
#   GR29  - faulting address
#   GR31  - EAR0 ^ SCR1
#   SCR1  - base of virtual range covered by cached PGE from last DTLB miss (or 0xffffffff)
#   DAMR3 - mapped page directory
#   DAMR5 - mapped page table as matched by SCR1
#
###############################################################################
	.globl		__entry_kernel_data_tlb_miss
	.type		__entry_kernel_data_tlb_miss,@function
__entry_kernel_data_tlb_miss:
#if 0
	sethi.p		%hi(0xe1200004),gr30
	setlo		%lo(0xe1200004),gr30
	st		gr0,@(gr30,gr0)
	sethi.p		%hi(0xffc00100),gr30
	setlo		%lo(0xffc00100),gr30
	sth		gr30,@(gr30,gr0)
	membar
#endif

	movsg		ccr,gr30			/* save CCR */
	movgs		gr30,scr2

	# see if the cached page table mapping is appropriate
	srlicc.p	gr31,#26,gr0,icc0
	setlos		0x3ffc,gr30
	srli.p		gr29,#12,gr31			/* use EAR0[25:14] as PTE index */
	bne		icc0,#0,__dtlb_k_PTD_miss

__dtlb_k_PTD_mapped:
	# access the PTD with EAR0[25:14]
	# - DAMLR5 points to the virtual address of the appropriate page table
	# - the PTD holds 4096 PTEs
	# - the PTD must be accessed uncached
	# - the PTE must be marked accessed if it was valid
	#
	and		gr31,gr30,gr31
	movsg		damlr5,gr30
	add		gr30,gr31,gr31
	ldi		@(gr31,#0),gr30			/* fetch the PTE */
	andicc		gr30,#_PAGE_PRESENT,gr0,icc0
	ori.p		gr30,#_PAGE_ACCESSED,gr30
	beq		icc0,#0,__tlb_kernel_fault	/* jump if PTE invalid */
	sti.p		gr30,@(gr31,#0)			/* update the PTE */
	andi		gr30,#~_PAGE_ACCESSED,gr30

	# we're using DAMR1 as an extra TLB entry
	# - punt the entry here (if valid) to the real TLB and then replace with the new PTE
	# - need to check IAMR1 lest we cause an multiple-DAT-hit exception
	movsg		dampr1,gr31
	andicc		gr31,#xAMPRx_V,gr0,icc0
	setlos.p	0xfffff000,gr31
	beq		icc0,#0,__dtlb_k_nopunt		/* punt not required */

	movsg		damlr1,gr31
	movgs		gr31,tplr			/* set TPLR.CXN */
	tlbpr		gr31,gr0,#4,#0			/* delete matches from TLB, IAMR1, DAMR1 */

	movsg		dampr1,gr31
	ori		gr31,#xAMPRx_V,gr31		/* entry was invalidated by tlbpr #4 */
	movgs		gr31,tppr
	movsg		damlr1,gr31			/* set TPLR.CXN */
	movgs		gr31,tplr
	tlbpr		gr31,gr0,#2,#0			/* save to the TLB */
	movsg		tpxr,gr31			/* check the TLB write error flag */
	andicc.p	gr31,#TPXR_E,gr0,icc0
	setlos		#0xfffff000,gr31
	bne		icc0,#0,__tlb_kernel_fault

__dtlb_k_nopunt:

	# assemble the new TLB entry
	and		gr29,gr31,gr29
	movsg		cxnr,gr31
	or		gr29,gr31,gr29
	movgs		gr29,iamlr1			/* xAMLR = address | context number */
	movgs		gr30,iampr1
	movgs		gr29,damlr1
	movgs		gr30,dampr1

	# return, restoring registers
	movsg		scr2,gr30
	movgs		gr30,ccr
	sethi.p		%hi(__kernel_current_task),gr29
	setlo		%lo(__kernel_current_task),gr29
	ldi		@(gr29,#0),gr29
	rett		#0
	beq		icc0,#3,0			/* prevent icache prefetch */

	# the PTE we want wasn't in the PTD we have mapped, so we need to go looking for a more
	# appropriate page table and map that instead
	#   - access the PGD with EAR0[31:26]
	#   - DAMLR3 points to the virtual address of the page directory
	#   - the PGD holds 64 PGEs and each PGE/PME points to a set of page tables
__dtlb_k_PTD_miss:
	srli		gr29,#26,gr31			/* calculate PGE offset */
	slli		gr31,#8,gr31			/* and clear bottom bits */

	movsg		damlr3,gr30
	ld		@(gr31,gr30),gr30		/* access the PGE */

	andicc.p	gr30,#_PAGE_PRESENT,gr0,icc0
	andicc		gr30,#xAMPRx_SS,gr0,icc1

	# map this PTD instead and record coverage address
	ori.p		gr30,#xAMPRx_L|xAMPRx_SS_16Kb|xAMPRx_S|xAMPRx_C|xAMPRx_V,gr30
	beq		icc0,#0,__tlb_kernel_fault	/* jump if PGE not present */
	slli.p		gr31,#18,gr31
	bne		icc1,#0,__dtlb_k_bigpage
	movgs		gr30,dampr5
	movgs		gr31,scr1

	# we can now resume normal service
	setlos		0x3ffc,gr30
	srli.p		gr29,#12,gr31			/* use EAR0[25:14] as PTE index */
	bra		__dtlb_k_PTD_mapped

__dtlb_k_bigpage:
	break
	nop

	.size		__entry_kernel_data_tlb_miss, .-__entry_kernel_data_tlb_miss

###############################################################################
#
# Userspace instruction TLB miss handler (with PGE prediction)
# On entry:
#   GR28  - faulting address
#   GR31  - EAR0 ^ SCR0
#   SCR0  - base of virtual range covered by cached PGE from last ITLB miss (or 0xffffffff)
#   DAMR3 - mapped page directory
#   DAMR4 - mapped page table as matched by SCR0
#
###############################################################################
	.globl		__entry_user_insn_tlb_miss
	.type		__entry_user_insn_tlb_miss,@function
__entry_user_insn_tlb_miss:
#if 0
	sethi.p		%hi(0xe1200004),gr30
	setlo		%lo(0xe1200004),gr30
	st		gr0,@(gr30,gr0)
	sethi.p		%hi(0xffc00100),gr30
	setlo		%lo(0xffc00100),gr30
	sth		gr30,@(gr30,gr0)
	membar
#endif

	movsg		ccr,gr30			/* save CCR */
	movgs		gr30,scr2

	# see if the cached page table mapping is appropriate
	srlicc.p	gr31,#26,gr0,icc0
	setlos		0x3ffc,gr30
	srli.p		gr28,#12,gr31			/* use EAR0[25:14] as PTE index */
	bne		icc0,#0,__itlb_u_PTD_miss

__itlb_u_PTD_mapped:
	# access the PTD with EAR0[25:14]
	# - DAMLR4 points to the virtual address of the appropriate page table
	# - the PTD holds 4096 PTEs
	# - the PTD must be accessed uncached
	# - the PTE must be marked accessed if it was valid
	#
	and		gr31,gr30,gr31
	movsg		damlr4,gr30
	add		gr30,gr31,gr31
	ldi		@(gr31,#0),gr30			/* fetch the PTE */
	andicc		gr30,#_PAGE_PRESENT,gr0,icc0
	ori.p		gr30,#_PAGE_ACCESSED,gr30
	beq		icc0,#0,__tlb_user_fault	/* jump if PTE invalid */
	sti.p		gr30,@(gr31,#0)			/* update the PTE */
	andi		gr30,#~_PAGE_ACCESSED,gr30

	# we're using IAMR1/DAMR1 as an extra TLB entry
	# - punt the entry here (if valid) to the real TLB and then replace with the new PTE
	movsg		dampr1,gr31
	andicc		gr31,#xAMPRx_V,gr0,icc0
	setlos.p	0xfffff000,gr31
	beq		icc0,#0,__itlb_u_nopunt		/* punt not required */

	movsg		dampr1,gr31
	movgs		gr31,tppr
	movsg		damlr1,gr31			/* set TPLR.CXN */
	movgs		gr31,tplr
	tlbpr		gr31,gr0,#2,#0			/* save to the TLB */
	movsg		tpxr,gr31			/* check the TLB write error flag */
	andicc.p	gr31,#TPXR_E,gr0,icc0
	setlos		#0xfffff000,gr31
	bne		icc0,#0,__tlb_user_fault

__itlb_u_nopunt:

	# assemble the new TLB entry
	and		gr28,gr31,gr28
	movsg		cxnr,gr31
	or		gr28,gr31,gr28
	movgs		gr28,iamlr1			/* xAMLR = address | context number */
	movgs		gr30,iampr1
	movgs		gr28,damlr1
	movgs		gr30,dampr1

	# return, restoring registers
	movsg		scr2,gr30
	movgs		gr30,ccr
	rett		#0
	beq		icc0,#3,0			/* prevent icache prefetch */

	# the PTE we want wasn't in the PTD we have mapped, so we need to go looking for a more
	# appropriate page table and map that instead
	#   - access the PGD with EAR0[31:26]
	#   - DAMLR3 points to the virtual address of the page directory
	#   - the PGD holds 64 PGEs and each PGE/PME points to a set of page tables
__itlb_u_PTD_miss:
	srli		gr28,#26,gr31			/* calculate PGE offset */
	slli		gr31,#8,gr31			/* and clear bottom bits */

	movsg		damlr3,gr30
	ld		@(gr31,gr30),gr30		/* access the PGE */

	andicc.p	gr30,#_PAGE_PRESENT,gr0,icc0
	andicc		gr30,#xAMPRx_SS,gr0,icc1

	# map this PTD instead and record coverage address
	ori.p		gr30,#xAMPRx_L|xAMPRx_SS_16Kb|xAMPRx_S|xAMPRx_C|xAMPRx_V,gr30
	beq		icc0,#0,__tlb_user_fault	/* jump if PGE not present */
	slli.p		gr31,#18,gr31
	bne		icc1,#0,__itlb_u_bigpage
	movgs		gr30,dampr4
	movgs		gr31,scr0

	# we can now resume normal service
	setlos		0x3ffc,gr30
	srli.p		gr28,#12,gr31			/* use EAR0[25:14] as PTE index */
	bra		__itlb_u_PTD_mapped

__itlb_u_bigpage:
	break
	nop

	.size		__entry_user_insn_tlb_miss, .-__entry_user_insn_tlb_miss

###############################################################################
#
# Userspace data TLB miss handler
# On entry:
#   GR28  - faulting address
#   GR31  - EAR0 ^ SCR1
#   SCR1  - base of virtual range covered by cached PGE from last DTLB miss (or 0xffffffff)
#   DAMR3 - mapped page directory
#   DAMR5 - mapped page table as matched by SCR1
#
###############################################################################
	.globl		__entry_user_data_tlb_miss
	.type		__entry_user_data_tlb_miss,@function
__entry_user_data_tlb_miss:
#if 0
	sethi.p		%hi(0xe1200004),gr30
	setlo		%lo(0xe1200004),gr30
	st		gr0,@(gr30,gr0)
	sethi.p		%hi(0xffc00100),gr30
	setlo		%lo(0xffc00100),gr30
	sth		gr30,@(gr30,gr0)
	membar
#endif

	movsg		ccr,gr30			/* save CCR */
	movgs		gr30,scr2

	# see if the cached page table mapping is appropriate
	srlicc.p	gr31,#26,gr0,icc0
	setlos		0x3ffc,gr30
	srli.p		gr28,#12,gr31			/* use EAR0[25:14] as PTE index */
	bne		icc0,#0,__dtlb_u_PTD_miss

__dtlb_u_PTD_mapped:
	# access the PTD with EAR0[25:14]
	# - DAMLR5 points to the virtual address of the appropriate page table
	# - the PTD holds 4096 PTEs
	# - the PTD must be accessed uncached
	# - the PTE must be marked accessed if it was valid
	#
	and		gr31,gr30,gr31
	movsg		damlr5,gr30

__dtlb_u_using_iPTD:
	add		gr30,gr31,gr31
	ldi		@(gr31,#0),gr30			/* fetch the PTE */
	andicc		gr30,#_PAGE_PRESENT,gr0,icc0
	ori.p		gr30,#_PAGE_ACCESSED,gr30
	beq		icc0,#0,__tlb_user_fault	/* jump if PTE invalid */
	sti.p		gr30,@(gr31,#0)			/* update the PTE */
	andi		gr30,#~_PAGE_ACCESSED,gr30

	# we're using DAMR1 as an extra TLB entry
	# - punt the entry here (if valid) to the real TLB and then replace with the new PTE
	movsg		dampr1,gr31
	andicc		gr31,#xAMPRx_V,gr0,icc0
	setlos.p	0xfffff000,gr31
	beq		icc0,#0,__dtlb_u_nopunt		/* punt not required */

	movsg		dampr1,gr31
	movgs		gr31,tppr
	movsg		damlr1,gr31			/* set TPLR.CXN */
	movgs		gr31,tplr
	tlbpr		gr31,gr0,#2,#0			/* save to the TLB */
	movsg		tpxr,gr31			/* check the TLB write error flag */
	andicc.p	gr31,#TPXR_E,gr0,icc0
	setlos		#0xfffff000,gr31
	bne		icc0,#0,__tlb_user_fault

__dtlb_u_nopunt:

	# assemble the new TLB entry
	and		gr28,gr31,gr28
	movsg		cxnr,gr31
	or		gr28,gr31,gr28
	movgs		gr28,iamlr1			/* xAMLR = address | context number */
	movgs		gr30,iampr1
	movgs		gr28,damlr1
	movgs		gr30,dampr1

	# return, restoring registers
	movsg		scr2,gr30
	movgs		gr30,ccr
	rett		#0
	beq		icc0,#3,0			/* prevent icache prefetch */

	# the PTE we want wasn't in the PTD we have mapped, so we need to go looking for a more
	# appropriate page table and map that instead
	#   - first of all, check the insn PGE cache - we may well get a hit there
	#   - access the PGD with EAR0[31:26]
	#   - DAMLR3 points to the virtual address of the page directory
	#   - the PGD holds 64 PGEs and each PGE/PME points to a set of page tables
__dtlb_u_PTD_miss:
	movsg		scr0,gr31			/* consult the insn-PGE-cache key */
	xor		gr28,gr31,gr31
	srlicc		gr31,#26,gr0,icc0
	srli		gr28,#12,gr31			/* use EAR0[25:14] as PTE index */
	bne		icc0,#0,__dtlb_u_iPGE_miss

	# what we're looking for is covered by the insn-PGE-cache
	setlos		0x3ffc,gr30
	and		gr31,gr30,gr31
	movsg		damlr4,gr30
	bra		__dtlb_u_using_iPTD

__dtlb_u_iPGE_miss:
	srli		gr28,#26,gr31			/* calculate PGE offset */
	slli		gr31,#8,gr31			/* and clear bottom bits */

	movsg		damlr3,gr30
	ld		@(gr31,gr30),gr30		/* access the PGE */

	andicc.p	gr30,#_PAGE_PRESENT,gr0,icc0
	andicc		gr30,#xAMPRx_SS,gr0,icc1

	# map this PTD instead and record coverage address
	ori.p		gr30,#xAMPRx_L|xAMPRx_SS_16Kb|xAMPRx_S|xAMPRx_C|xAMPRx_V,gr30
	beq		icc0,#0,__tlb_user_fault	/* jump if PGE not present */
	slli.p		gr31,#18,gr31
	bne		icc1,#0,__dtlb_u_bigpage
	movgs		gr30,dampr5
	movgs		gr31,scr1

	# we can now resume normal service
	setlos		0x3ffc,gr30
	srli.p		gr28,#12,gr31			/* use EAR0[25:14] as PTE index */
	bra		__dtlb_u_PTD_mapped

__dtlb_u_bigpage:
	break
	nop

	.size		__entry_user_data_tlb_miss, .-__entry_user_data_tlb_miss
