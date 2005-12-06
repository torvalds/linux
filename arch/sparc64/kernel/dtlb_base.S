/* $Id: dtlb_base.S,v 1.17 2001/10/11 22:33:52 davem Exp $
 * dtlb_base.S:	Front end to DTLB miss replacement strategy.
 *              This is included directly into the trap table.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997,1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#include <asm/pgtable.h>
#include <asm/mmu.h>

/* %g1	TLB_SFSR	(%g1 + %g1 == TLB_TAG_ACCESS)
 * %g2	(KERN_HIGHBITS | KERN_LOWBITS)
 * %g3  VPTE base	(0xfffffffe00000000)	Spitfire/Blackbird (44-bit VA space)
 *			(0xffe0000000000000)	Cheetah		   (64-bit VA space)
 * %g7	__pa(current->mm->pgd)
 *
 * The VPTE base value is completely magic, but note that
 * few places in the kernel other than these TLB miss
 * handlers know anything about the VPTE mechanism or
 * how it works (see VPTE_SIZE, TASK_SIZE and PTRS_PER_PGD).
 * Consider the 44-bit VADDR Ultra-I/II case as an example:
 *
 * VA[0 :  (1<<43)] produce VPTE index [%g3                        :   0]
 * VA[0 : -(1<<43)] produce VPTE index [%g3-(1<<(43-PAGE_SHIFT+3)) : %g3]
 *
 * For Cheetah's 64-bit VADDR space this is:
 *
 * VA[0 :  (1<<63)] produce VPTE index [%g3                        :   0]
 * VA[0 : -(1<<63)] produce VPTE index [%g3-(1<<(63-PAGE_SHIFT+3)) : %g3]
 *
 * If you're paying attention you'll notice that this means half of
 * the VPTE table is above %g3 and half is below, low VA addresses
 * map progressively upwards from %g3, and high VA addresses map
 * progressively upwards towards %g3.  This trick was needed to make
 * the same 8 instruction handler work both for Spitfire/Blackbird's
 * peculiar VA space hole configuration and the full 64-bit VA space
 * one of Cheetah at the same time.
 */

/* Ways we can get here:
 *
 * 1) Nucleus loads and stores to/from PA-->VA direct mappings.
 * 2) Nucleus loads and stores to/from vmalloc() areas.
 * 3) User loads and stores.
 * 4) User space accesses by nucleus at tl0
 */

#if PAGE_SHIFT == 13
/*
 * To compute vpte offset, we need to do ((addr >> 13) << 3),
 * which can be optimized to (addr >> 10) if bits 10/11/12 can
 * be guaranteed to be 0 ... mmu_context.h does guarantee this
 * by only using 10 bits in the hwcontext value.
 */
#define CREATE_VPTE_OFFSET1(r1, r2) nop
#define CREATE_VPTE_OFFSET2(r1, r2) \
				srax	r1, 10, r2
#else
#define CREATE_VPTE_OFFSET1(r1, r2) \
				srax	r1, PAGE_SHIFT, r2
#define CREATE_VPTE_OFFSET2(r1, r2) \
				sllx	r2, 3, r2
#endif

/* DTLB ** ICACHE line 1: Quick user TLB misses		*/
	mov		TLB_SFSR, %g1
	ldxa		[%g1 + %g1] ASI_DMMU, %g4	! Get TAG_ACCESS
	andcc		%g4, TAG_CONTEXT_BITS, %g0	! From Nucleus?
from_tl1_trap:
	rdpr		%tl, %g5			! For TL==3 test
	CREATE_VPTE_OFFSET1(%g4, %g6)			! Create VPTE offset
	be,pn		%xcc, kvmap			! Yep, special processing
	 CREATE_VPTE_OFFSET2(%g4, %g6)			! Create VPTE offset
	cmp		%g5, 4				! Last trap level?

/* DTLB ** ICACHE line 2: User finish + quick kernel TLB misses	*/
	be,pn		%xcc, longpath			! Yep, cannot risk VPTE miss
	 nop						! delay slot
	ldxa		[%g3 + %g6] ASI_S, %g5		! Load VPTE
1:	brgez,pn	%g5, longpath			! Invalid, branch out
	 nop						! Delay-slot
9:	stxa		%g5, [%g0] ASI_DTLB_DATA_IN	! Reload TLB
	retry						! Trap return
	nop

/* DTLB ** ICACHE line 3: winfixups+real_faults		*/
longpath:
	rdpr		%pstate, %g5			! Move into alternate globals
	wrpr		%g5, PSTATE_AG|PSTATE_MG, %pstate
	rdpr		%tl, %g4			! See where we came from.
	cmp		%g4, 1				! Is etrap/rtrap window fault?
	mov		TLB_TAG_ACCESS, %g4		! Prepare for fault processing
	ldxa		[%g4] ASI_DMMU, %g5		! Load faulting VA page
	be,pt		%xcc, sparc64_realfault_common	! Jump to normal fault handling
	 mov		FAULT_CODE_DTLB, %g4		! It was read from DTLB

/* DTLB ** ICACHE line 4: Unused...	*/
	ba,a,pt		%xcc, winfix_trampoline		! Call window fixup code
	nop
	nop
	nop
	nop
	nop
	nop
	nop

#undef CREATE_VPTE_OFFSET1
#undef CREATE_VPTE_OFFSET2
