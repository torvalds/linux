/* $Id: dtlb_prot.S,v 1.22 2001/04/11 23:40:32 davem Exp $
 * dtlb_prot.S: DTLB protection trap strategy.
 *              This is included directly into the trap table.
 *
 * Copyright (C) 1996,1998 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997,1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

/* Ways we can get here:
 *
 * [TL == 0] 1) User stores to readonly pages.
 * [TL == 0] 2) Nucleus stores to user readonly pages.
 * [TL >  0] 3) Nucleus stores to user readonly stack frame.
 */

/* PROT ** ICACHE line 1: User DTLB protection trap	*/
	mov		TLB_SFSR, %g1
	stxa		%g0, [%g1] ASI_DMMU		! Clear FaultValid bit
	membar		#Sync				! Synchronize stores
	rdpr		%pstate, %g5			! Move into alt-globals
	wrpr		%g5, PSTATE_AG|PSTATE_MG, %pstate
	rdpr		%tl, %g1			! Need a winfixup?
	cmp		%g1, 1				! Trap level >1?
	mov		TLB_TAG_ACCESS, %g4		! For reload of vaddr

/* PROT ** ICACHE line 2: More real fault processing */
	bgu,pn		%xcc, winfix_trampoline		! Yes, perform winfixup
	 ldxa		[%g4] ASI_DMMU, %g5		! Put tagaccess in %g5
	ba,pt		%xcc, sparc64_realfault_common	! Nope, normal fault
	 mov		FAULT_CODE_DTLB | FAULT_CODE_WRITE, %g4
	nop
	nop
	nop
	nop

/* PROT ** ICACHE line 3: Unused...	*/
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

/* PROT ** ICACHE line 4: Unused...	*/
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
