/* tsb.S: Sparc64 TSB table handling.
 *
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */

#include <asm/tsb.h>

	.text
	.align	32

	/* Invoked from TLB miss handler, we are in the
	 * MMU global registers and they are setup like
	 * this:
	 *
	 * %g1: TSB entry pointer
	 * %g2:	available temporary
	 * %g3:	FAULT_CODE_{D,I}TLB
	 * %g4:	available temporary
	 * %g5:	available temporary
	 * %g6: TAG TARGET
	 * %g7:	available temporary, will be loaded by us with
	 *      the physical address base of the linux page
	 *      tables for the current address space
	 */
tsb_miss_dtlb:
	mov		TLB_TAG_ACCESS, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ba,pt		%xcc, tsb_miss_page_table_walk
	 nop

tsb_miss_itlb:
	mov		TLB_TAG_ACCESS, %g4
	ldxa		[%g4] ASI_IMMU, %g4
	ba,pt		%xcc, tsb_miss_page_table_walk
	 nop

	/* The sun4v TLB miss handlers jump directly here instead
	 * of tsb_miss_{d,i}tlb with the missing virtual address
	 * already loaded into %g4.
	 */
tsb_miss_page_table_walk:
	TRAP_LOAD_PGD_PHYS(%g7, %g5)

	USER_PGTABLE_WALK_TL1(%g4, %g7, %g5, %g2, tsb_do_fault)

tsb_reload:
	TSB_LOCK_TAG(%g1, %g2, %g7)

	/* Load and check PTE.  */
	ldxa		[%g5] ASI_PHYS_USE_EC, %g5
	brgez,a,pn	%g5, tsb_do_fault
	 TSB_STORE(%g1, %g0)

	/* If it is larger than the base page size, don't
	 * bother putting it into the TSB.
	 */
	srlx		%g5, 32, %g2
	sethi		%hi(_PAGE_ALL_SZ_BITS >> 32), %g7
	and		%g2, %g7, %g2
	sethi		%hi(_PAGE_SZBITS >> 32), %g7
	cmp		%g2, %g7
	bne,a,pn	%xcc, tsb_tlb_reload
	 TSB_STORE(%g1, %g0)

	TSB_WRITE(%g1, %g5, %g6)

	/* Finally, load TLB and return from trap.  */
tsb_tlb_reload:
	cmp		%g3, FAULT_CODE_DTLB
	bne,pn		%xcc, tsb_itlb_load
	 nop

tsb_dtlb_load:

661:	stxa		%g5, [%g0] ASI_DTLB_DATA_IN
	retry
	.section	.gl_2insn_patch, "ax"
	.word		661b
	nop
	nop
	.previous

	/* For sun4v the ASI_DTLB_DATA_IN store and the retry
	 * instruction get nop'd out and we get here to branch
	 * to the sun4v tlb load code.  The registers are setup
	 * as follows:
	 *
	 * %g4: vaddr
	 * %g5: PTE
	 * %g6:	TAG
	 *
	 * The sun4v TLB load wants the PTE in %g3 so we fix that
	 * up here.
	 */
	ba,pt		%xcc, sun4v_dtlb_load
	 mov		%g5, %g3

tsb_itlb_load:

661:	stxa		%g5, [%g0] ASI_ITLB_DATA_IN
	retry
	.section	.gl_2insn_patch, "ax"
	.word		661b
	nop
	nop
	.previous

	/* For sun4v the ASI_ITLB_DATA_IN store and the retry
	 * instruction get nop'd out and we get here to branch
	 * to the sun4v tlb load code.  The registers are setup
	 * as follows:
	 *
	 * %g4: vaddr
	 * %g5: PTE
	 * %g6:	TAG
	 *
	 * The sun4v TLB load wants the PTE in %g3 so we fix that
	 * up here.
	 */
	ba,pt		%xcc, sun4v_itlb_load
	 mov		%g5, %g3

	/* No valid entry in the page tables, do full fault
	 * processing.
	 */

	.globl		tsb_do_fault
tsb_do_fault:
	cmp		%g3, FAULT_CODE_DTLB

661:	rdpr		%pstate, %g5
	wrpr		%g5, PSTATE_AG | PSTATE_MG, %pstate
	.section	.gl_2insn_patch, "ax"
	.word		661b
	nop
	nop
	.previous

	bne,pn		%xcc, tsb_do_itlb_fault
	 nop

tsb_do_dtlb_fault:
	rdpr	%tl, %g3
	cmp	%g3, 1

661:	mov	TLB_TAG_ACCESS, %g4
	ldxa	[%g4] ASI_DMMU, %g5
	.section .gl_2insn_patch, "ax"
	.word	661b
	mov	%g4, %g5
	nop
	.previous

	be,pt	%xcc, sparc64_realfault_common
	 mov	FAULT_CODE_DTLB, %g4
	ba,pt	%xcc, winfix_trampoline
	 nop

tsb_do_itlb_fault:
	rdpr	%tpc, %g5
	ba,pt	%xcc, sparc64_realfault_common
	 mov	FAULT_CODE_ITLB, %g4

	.globl	sparc64_realfault_common
sparc64_realfault_common:
	/* fault code in %g4, fault address in %g5, etrap will
	 * preserve these two values in %l4 and %l5 respectively
	 */
	ba,pt	%xcc, etrap			! Save trap state
1:	 rd	%pc, %g7			! ...
	stb	%l4, [%g6 + TI_FAULT_CODE]	! Save fault code
	stx	%l5, [%g6 + TI_FAULT_ADDR]	! Save fault address
	call	do_sparc64_fault		! Call fault handler
	 add	%sp, PTREGS_OFF, %o0		! Compute pt_regs arg
	ba,pt	%xcc, rtrap_clr_l6		! Restore cpu state
	 nop					! Delay slot (fill me)

winfix_trampoline:
	rdpr	%tpc, %g3			! Prepare winfixup TNPC
	or	%g3, 0x7c, %g3			! Compute branch offset
	wrpr	%g3, %tnpc			! Write it into TNPC
	done					! Trap return

	/* Insert an entry into the TSB.
	 *
	 * %o0: TSB entry pointer (virt or phys address)
	 * %o1: tag
	 * %o2:	pte
	 */
	.align	32
	.globl	__tsb_insert
__tsb_insert:
	rdpr	%pstate, %o5
	wrpr	%o5, PSTATE_IE, %pstate
	TSB_LOCK_TAG(%o0, %g2, %g3)
	TSB_WRITE(%o0, %o2, %o1)
	wrpr	%o5, %pstate
	retl
	 nop

	/* Flush the given TSB entry if it has the matching
	 * tag.
	 *
	 * %o0: TSB entry pointer (virt or phys address)
	 * %o1:	tag
	 */
	.align	32
	.globl	tsb_flush
tsb_flush:
	sethi	%hi(TSB_TAG_LOCK_HIGH), %g2
1:	TSB_LOAD_TAG(%o0, %g1)
	srlx	%g1, 32, %o3
	andcc	%o3, %g2, %g0
	bne,pn	%icc, 1b
	 membar	#LoadLoad
	cmp	%g1, %o1
	bne,pt	%xcc, 2f
	 clr	%o3
	TSB_CAS_TAG(%o0, %g1, %o3)
	cmp	%g1, %o3
	bne,pn	%xcc, 1b
	 nop
2:	retl
	 TSB_MEMBAR

	/* Reload MMU related context switch state at
	 * schedule() time.
	 *
	 * %o0: page table physical address
	 * %o1:	TSB register value
	 * %o2:	TSB virtual address
	 * %o3:	TSB mapping locked PTE
	 *
	 * We have to run this whole thing with interrupts
	 * disabled so that the current cpu doesn't change
	 * due to preemption.
	 */
	.align	32
	.globl	__tsb_context_switch
__tsb_context_switch:
	rdpr	%pstate, %o5
	wrpr	%o5, PSTATE_IE, %pstate

	ldub	[%g6 + TI_CPU], %g1
	sethi	%hi(trap_block), %g2
	sllx	%g1, TRAP_BLOCK_SZ_SHIFT, %g1
	or	%g2, %lo(trap_block), %g2
	add	%g2, %g1, %g2
	stx	%o0, [%g2 + TRAP_PER_CPU_PGD_PADDR]

661:	mov	TSB_REG, %g1
	stxa	%o1, [%g1] ASI_DMMU
	.section .gl_2insn_patch, "ax"
	.word	661b
	mov	SCRATCHPAD_UTSBREG1, %g1
	stxa	%o1, [%g1] ASI_SCRATCHPAD
	.previous

	membar	#Sync

661:	stxa	%o1, [%g1] ASI_IMMU
	membar	#Sync
	.section .gl_2insn_patch, "ax"
	.word	661b
	nop
	nop
	.previous

	brz	%o2, 9f
	 nop

	sethi	%hi(sparc64_highest_unlocked_tlb_ent), %o4
	mov	TLB_TAG_ACCESS, %g1
	lduw	[%o4 + %lo(sparc64_highest_unlocked_tlb_ent)], %g2
	stxa	%o2, [%g1] ASI_DMMU
	membar	#Sync
	sllx	%g2, 3, %g2
	stxa	%o3, [%g2] ASI_DTLB_DATA_ACCESS
	membar	#Sync
9:
	wrpr	%o5, %pstate

	retl
	 nop
