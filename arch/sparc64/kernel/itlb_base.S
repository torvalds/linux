/* $Id: itlb_base.S,v 1.12 2002/02/09 19:49:30 davem Exp $
 * itlb_base.S:	Front end to ITLB miss replacement strategy.
 *              This is included directly into the trap table.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997,1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#if PAGE_SHIFT == 13
/*
 * To compute vpte offset, we need to do ((addr >> 13) << 3),
 * which can be optimized to (addr >> 10) if bits 10/11/12 can
 * be guaranteed to be 0 ... mmu_context.h does guarantee this
 * by only using 10 bits in the hwcontext value.
 */
#define CREATE_VPTE_OFFSET1(r1, r2) \
				srax	r1, 10, r2
#define CREATE_VPTE_OFFSET2(r1, r2) nop
#else /* PAGE_SHIFT */
#define CREATE_VPTE_OFFSET1(r1, r2) \
				srax	r1, PAGE_SHIFT, r2
#define CREATE_VPTE_OFFSET2(r1, r2) \
				sllx	r2, 3, r2
#endif /* PAGE_SHIFT */


/* Ways we can get here:
 *
 * 1) Nucleus instruction misses from module code.
 * 2) All user instruction misses.
 *
 * All real page faults merge their code paths to the
 * sparc64_realfault_common label below.
 */

/* ITLB ** ICACHE line 1: Quick user TLB misses		*/
	mov		TLB_SFSR, %g1
	ldxa		[%g1 + %g1] ASI_IMMU, %g4	! Get TAG_ACCESS
	CREATE_VPTE_OFFSET1(%g4, %g6)			! Create VPTE offset
	CREATE_VPTE_OFFSET2(%g4, %g6)			! Create VPTE offset
	ldxa		[%g3 + %g6] ASI_P, %g5		! Load VPTE
1:	brgez,pn	%g5, 3f				! Not valid, branch out
	 sethi		%hi(_PAGE_EXEC), %g4		! Delay-slot
	andcc		%g5, %g4, %g0			! Executable?

/* ITLB ** ICACHE line 2: Real faults			*/
	be,pn		%xcc, 3f			! Nope, branch.
	 nop						! Delay-slot
2:	stxa		%g5, [%g0] ASI_ITLB_DATA_IN	! Load PTE into TLB
	retry						! Trap return
3:	rdpr		%pstate, %g4			! Move into alt-globals
	wrpr		%g4, PSTATE_AG|PSTATE_MG, %pstate
	rdpr		%tpc, %g5			! And load faulting VA
	mov		FAULT_CODE_ITLB, %g4		! It was read from ITLB

/* ITLB ** ICACHE line 3: Finish faults	*/
sparc64_realfault_common:				! Called by dtlb_miss
	stb		%g4, [%g6 + TI_FAULT_CODE]
	stx		%g5, [%g6 + TI_FAULT_ADDR]
	ba,pt		%xcc, etrap			! Save state
1:	 rd		%pc, %g7			! ...
	call		do_sparc64_fault		! Call fault handler
	 add		%sp, PTREGS_OFF, %o0! Compute pt_regs arg
	ba,pt		%xcc, rtrap_clr_l6		! Restore cpu state
	 nop

/* ITLB ** ICACHE line 4: Window fixups */
winfix_trampoline:
	rdpr		%tpc, %g3			! Prepare winfixup TNPC
	or		%g3, 0x7c, %g3			! Compute branch offset
	wrpr		%g3, %tnpc			! Write it into TNPC
	done						! Do it to it
	nop
	nop
	nop
	nop

#undef CREATE_VPTE_OFFSET1
#undef CREATE_VPTE_OFFSET2
