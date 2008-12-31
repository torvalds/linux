/* clear_page.S: UltraSparc optimized clear page.
 *
 * Copyright (C) 1996, 1998, 1999, 2000, 2004 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997 Jakub Jelinek (jakub@redhat.com)
 */

#include <asm/visasm.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/spitfire.h>
#include <asm/head.h>

	/* What we used to do was lock a TLB entry into a specific
	 * TLB slot, clear the page with interrupts disabled, then
	 * restore the original TLB entry.  This was great for
	 * disturbing the TLB as little as possible, but it meant
	 * we had to keep interrupts disabled for a long time.
	 *
	 * Now, we simply use the normal TLB loading mechanism,
	 * and this makes the cpu choose a slot all by itself.
	 * Then we do a normal TLB flush on exit.  We need only
	 * disable preemption during the clear.
	 */

	.text

	.globl		_clear_page
_clear_page:		/* %o0=dest */
	ba,pt		%xcc, clear_page_common
	 clr		%o4

	/* This thing is pretty important, it shows up
	 * on the profiles via do_anonymous_page().
	 */
	.align		32
	.globl		clear_user_page
clear_user_page:	/* %o0=dest, %o1=vaddr */
	lduw		[%g6 + TI_PRE_COUNT], %o2
	sethi		%uhi(PAGE_OFFSET), %g2
	sethi		%hi(PAGE_SIZE), %o4

	sllx		%g2, 32, %g2
	sethi		%hi(PAGE_KERNEL_LOCKED), %g3

	ldx		[%g3 + %lo(PAGE_KERNEL_LOCKED)], %g3
	sub		%o0, %g2, %g1		! paddr

	and		%o1, %o4, %o0		! vaddr D-cache alias bit

	or		%g1, %g3, %g1		! TTE data
	sethi		%hi(TLBTEMP_BASE), %o3

	add		%o2, 1, %o4
	add		%o0, %o3, %o0		! TTE vaddr

	/* Disable preemption.  */
	mov		TLB_TAG_ACCESS, %g3
	stw		%o4, [%g6 + TI_PRE_COUNT]

	/* Load TLB entry.  */
	rdpr		%pstate, %o4
	wrpr		%o4, PSTATE_IE, %pstate
	stxa		%o0, [%g3] ASI_DMMU
	stxa		%g1, [%g0] ASI_DTLB_DATA_IN
	sethi		%hi(KERNBASE), %g1
	flush		%g1
	wrpr		%o4, 0x0, %pstate

	mov		1, %o4

clear_page_common:
	VISEntryHalf
	membar		#StoreLoad | #StoreStore | #LoadStore
	fzero		%f0
	sethi		%hi(PAGE_SIZE/64), %o1
	mov		%o0, %g1		! remember vaddr for tlbflush
	fzero		%f2
	or		%o1, %lo(PAGE_SIZE/64), %o1
	faddd		%f0, %f2, %f4
	fmuld		%f0, %f2, %f6
	faddd		%f0, %f2, %f8
	fmuld		%f0, %f2, %f10

	faddd		%f0, %f2, %f12
	fmuld		%f0, %f2, %f14
1:	stda		%f0, [%o0 + %g0] ASI_BLK_P
	subcc		%o1, 1, %o1
	bne,pt		%icc, 1b
	 add		%o0, 0x40, %o0
	membar		#Sync
	VISExitHalf

	brz,pn		%o4, out
	 nop

	stxa		%g0, [%g1] ASI_DMMU_DEMAP
	membar		#Sync
	stw		%o2, [%g6 + TI_PRE_COUNT]

out:	retl
	 nop

