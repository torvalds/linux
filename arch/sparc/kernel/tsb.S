/* tsb.S: Sparc64 TSB table handling.
 *
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */


#include <asm/tsb.h>
#include <asm/hypervisor.h>
#include <asm/page.h>
#include <asm/cpudata.h>
#include <asm/mmu.h>

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
	ba,pt		%xcc, tsb_miss_page_table_walk
	 ldxa		[%g4] ASI_DMMU, %g4

tsb_miss_itlb:
	mov		TLB_TAG_ACCESS, %g4
	ba,pt		%xcc, tsb_miss_page_table_walk
	 ldxa		[%g4] ASI_IMMU, %g4

	/* At this point we have:
	 * %g1 --	PAGE_SIZE TSB entry address
	 * %g3 --	FAULT_CODE_{D,I}TLB
	 * %g4 --	missing virtual address
	 * %g6 --	TAG TARGET (vaddr >> 22)
	 */
tsb_miss_page_table_walk:
	TRAP_LOAD_TRAP_BLOCK(%g7, %g5)

	/* Before committing to a full page table walk,
	 * check the huge page TSB.
	 */
#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)

661:	ldx		[%g7 + TRAP_PER_CPU_TSB_HUGE], %g5
	nop
	.section	.sun4v_2insn_patch, "ax"
	.word		661b
	mov		SCRATCHPAD_UTSBREG2, %g5
	ldxa		[%g5] ASI_SCRATCHPAD, %g5
	.previous

	cmp		%g5, -1
	be,pt		%xcc, 80f
	 nop

	/* We need an aligned pair of registers containing 2 values
	 * which can be easily rematerialized.  %g6 and %g7 foot the
	 * bill just nicely.  We'll save %g6 away into %g2 for the
	 * huge page TSB TAG comparison.
	 *
	 * Perform a huge page TSB lookup.
	 */
	mov		%g6, %g2
	and		%g5, 0x7, %g6
	mov		512, %g7
	andn		%g5, 0x7, %g5
	sllx		%g7, %g6, %g7
	srlx		%g4, HPAGE_SHIFT, %g6
	sub		%g7, 1, %g7
	and		%g6, %g7, %g6
	sllx		%g6, 4, %g6
	add		%g5, %g6, %g5

	TSB_LOAD_QUAD(%g5, %g6)
	cmp		%g6, %g2
	be,a,pt		%xcc, tsb_tlb_reload
	 mov		%g7, %g5

	/* No match, remember the huge page TSB entry address,
	 * and restore %g6 and %g7.
	 */
	TRAP_LOAD_TRAP_BLOCK(%g7, %g6)
	srlx		%g4, 22, %g6
80:	stx		%g5, [%g7 + TRAP_PER_CPU_TSB_HUGE_TEMP]

#endif

	ldx		[%g7 + TRAP_PER_CPU_PGD_PADDR], %g7

	/* At this point we have:
	 * %g1 --	TSB entry address
	 * %g3 --	FAULT_CODE_{D,I}TLB
	 * %g4 --	missing virtual address
	 * %g6 --	TAG TARGET (vaddr >> 22)
	 * %g7 --	page table physical address
	 *
	 * We know that both the base PAGE_SIZE TSB and the HPAGE_SIZE
	 * TSB both lack a matching entry.
	 */
tsb_miss_page_table_walk_sun4v_fastpath:
	USER_PGTABLE_WALK_TL1(%g4, %g7, %g5, %g2, tsb_do_fault)

	/* Valid PTE is now in %g5.  */

#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)
661:	sethi		%uhi(_PAGE_SZALL_4U), %g7
	sllx		%g7, 32, %g7
	.section	.sun4v_2insn_patch, "ax"
	.word		661b
	mov		_PAGE_SZALL_4V, %g7
	nop
	.previous

	and		%g5, %g7, %g2

661:	sethi		%uhi(_PAGE_SZHUGE_4U), %g7
	sllx		%g7, 32, %g7
	.section	.sun4v_2insn_patch, "ax"
	.word		661b
	mov		_PAGE_SZHUGE_4V, %g7
	nop
	.previous

	cmp		%g2, %g7
	bne,pt		%xcc, 60f
	 nop

	/* It is a huge page, use huge page TSB entry address we
	 * calculated above.
	 */
	TRAP_LOAD_TRAP_BLOCK(%g7, %g2)
	ldx		[%g7 + TRAP_PER_CPU_TSB_HUGE_TEMP], %g2
	cmp		%g2, -1
	movne		%xcc, %g2, %g1
60:
#endif

	/* At this point we have:
	 * %g1 --	TSB entry address
	 * %g3 --	FAULT_CODE_{D,I}TLB
	 * %g5 --	valid PTE
	 * %g6 --	TAG TARGET (vaddr >> 22)
	 */
tsb_reload:
	TSB_LOCK_TAG(%g1, %g2, %g7)
	TSB_WRITE(%g1, %g5, %g6)

	/* Finally, load TLB and return from trap.  */
tsb_tlb_reload:
	cmp		%g3, FAULT_CODE_DTLB
	bne,pn		%xcc, tsb_itlb_load
	 nop

tsb_dtlb_load:

661:	stxa		%g5, [%g0] ASI_DTLB_DATA_IN
	retry
	.section	.sun4v_2insn_patch, "ax"
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
	/* Executable bit must be set.  */
661:	sethi		%hi(_PAGE_EXEC_4U), %g4
	andcc		%g5, %g4, %g0
	.section	.sun4v_2insn_patch, "ax"
	.word		661b
	andcc		%g5, _PAGE_EXEC_4V, %g0
	nop
	.previous

	be,pn		%xcc, tsb_do_fault
	 nop

661:	stxa		%g5, [%g0] ASI_ITLB_DATA_IN
	retry
	.section	.sun4v_2insn_patch, "ax"
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
	.section	.sun4v_2insn_patch, "ax"
	.word		661b
	SET_GL(1)
	ldxa		[%g0] ASI_SCRATCHPAD, %g4
	.previous

	bne,pn		%xcc, tsb_do_itlb_fault
	 nop

tsb_do_dtlb_fault:
	rdpr	%tl, %g3
	cmp	%g3, 1

661:	mov	TLB_TAG_ACCESS, %g4
	ldxa	[%g4] ASI_DMMU, %g5
	.section .sun4v_2insn_patch, "ax"
	.word	661b
	ldx	[%g4 + HV_FAULT_D_ADDR_OFFSET], %g5
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
	ba,pt	%xcc, rtrap			! Restore cpu state
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
	.size	__tsb_insert, .-__tsb_insert

	/* Flush the given TSB entry if it has the matching
	 * tag.
	 *
	 * %o0: TSB entry pointer (virt or phys address)
	 * %o1:	tag
	 */
	.align	32
	.globl	tsb_flush
	.type	tsb_flush,#function
tsb_flush:
	sethi	%hi(TSB_TAG_LOCK_HIGH), %g2
1:	TSB_LOAD_TAG(%o0, %g1)
	srlx	%g1, 32, %o3
	andcc	%o3, %g2, %g0
	bne,pn	%icc, 1b
	 nop
	cmp	%g1, %o1
	mov	1, %o3
	bne,pt	%xcc, 2f
	 sllx	%o3, TSB_TAG_INVALID_BIT, %o3
	TSB_CAS_TAG(%o0, %g1, %o3)
	cmp	%g1, %o3
	bne,pn	%xcc, 1b
	 nop
2:	retl
	 nop
	.size	tsb_flush, .-tsb_flush

	/* Reload MMU related context switch state at
	 * schedule() time.
	 *
	 * %o0: page table physical address
	 * %o1:	TSB base config pointer
	 * %o2:	TSB huge config pointer, or NULL if none
	 * %o3:	Hypervisor TSB descriptor physical address
	 *
	 * We have to run this whole thing with interrupts
	 * disabled so that the current cpu doesn't change
	 * due to preemption.
	 */
	.align	32
	.globl	__tsb_context_switch
	.type	__tsb_context_switch,#function
__tsb_context_switch:
	rdpr	%pstate, %g1
	wrpr	%g1, PSTATE_IE, %pstate

	TRAP_LOAD_TRAP_BLOCK(%g2, %g3)

	stx	%o0, [%g2 + TRAP_PER_CPU_PGD_PADDR]

	ldx	[%o1 + TSB_CONFIG_REG_VAL], %o0
	brz,pt	%o2, 1f
	 mov	-1, %g3

	ldx	[%o2 + TSB_CONFIG_REG_VAL], %g3

1:	stx	%g3, [%g2 + TRAP_PER_CPU_TSB_HUGE]

	sethi	%hi(tlb_type), %g2
	lduw	[%g2 + %lo(tlb_type)], %g2
	cmp	%g2, 3
	bne,pt	%icc, 50f
	 nop

	/* Hypervisor TSB switch. */
	mov	SCRATCHPAD_UTSBREG1, %o5
	stxa	%o0, [%o5] ASI_SCRATCHPAD
	mov	SCRATCHPAD_UTSBREG2, %o5
	stxa	%g3, [%o5] ASI_SCRATCHPAD

	mov	2, %o0
	cmp	%g3, -1
	move	%xcc, 1, %o0

	mov	HV_FAST_MMU_TSB_CTXNON0, %o5
	mov	%o3, %o1
	ta	HV_FAST_TRAP

	/* Finish up.  */
	ba,pt	%xcc, 9f
	 nop

	/* SUN4U TSB switch.  */
50:	mov	TSB_REG, %o5
	stxa	%o0, [%o5] ASI_DMMU
	membar	#Sync
	stxa	%o0, [%o5] ASI_IMMU
	membar	#Sync

2:	ldx	[%o1 + TSB_CONFIG_MAP_VADDR], %o4
	brz	%o4, 9f
	 ldx	[%o1 + TSB_CONFIG_MAP_PTE], %o5

	sethi	%hi(sparc64_highest_unlocked_tlb_ent), %g2
	mov	TLB_TAG_ACCESS, %g3
	lduw	[%g2 + %lo(sparc64_highest_unlocked_tlb_ent)], %g2
	stxa	%o4, [%g3] ASI_DMMU
	membar	#Sync
	sllx	%g2, 3, %g2
	stxa	%o5, [%g2] ASI_DTLB_DATA_ACCESS
	membar	#Sync

	brz,pt	%o2, 9f
	 nop

	ldx	[%o2 + TSB_CONFIG_MAP_VADDR], %o4
	ldx	[%o2 + TSB_CONFIG_MAP_PTE], %o5
	mov	TLB_TAG_ACCESS, %g3
	stxa	%o4, [%g3] ASI_DMMU
	membar	#Sync
	sub	%g2, (1 << 3), %g2
	stxa	%o5, [%g2] ASI_DTLB_DATA_ACCESS
	membar	#Sync

9:
	wrpr	%g1, %pstate

	retl
	 nop
	.size	__tsb_context_switch, .-__tsb_context_switch

#define TSB_PASS_BITS	((1 << TSB_TAG_LOCK_BIT) | \
			 (1 << TSB_TAG_INVALID_BIT))

	.align	32
	.globl	copy_tsb
	.type	copy_tsb,#function
copy_tsb:		/* %o0=old_tsb_base, %o1=old_tsb_size
			 * %o2=new_tsb_base, %o3=new_tsb_size
			 */
	sethi		%uhi(TSB_PASS_BITS), %g7
	srlx		%o3, 4, %o3
	add		%o0, %o1, %g1	/* end of old tsb */
	sllx		%g7, 32, %g7
	sub		%o3, 1, %o3	/* %o3 == new tsb hash mask */

661:	prefetcha	[%o0] ASI_N, #one_read
	.section	.tsb_phys_patch, "ax"
	.word		661b
	prefetcha	[%o0] ASI_PHYS_USE_EC, #one_read
	.previous

90:	andcc		%o0, (64 - 1), %g0
	bne		1f
	 add		%o0, 64, %o5

661:	prefetcha	[%o5] ASI_N, #one_read
	.section	.tsb_phys_patch, "ax"
	.word		661b
	prefetcha	[%o5] ASI_PHYS_USE_EC, #one_read
	.previous

1:	TSB_LOAD_QUAD(%o0, %g2)		/* %g2/%g3 == TSB entry */
	andcc		%g2, %g7, %g0	/* LOCK or INVALID set? */
	bne,pn		%xcc, 80f	/* Skip it */
	 sllx		%g2, 22, %o4	/* TAG --> VADDR */

	/* This can definitely be computed faster... */
	srlx		%o0, 4, %o5	/* Build index */
	and		%o5, 511, %o5	/* Mask index */
	sllx		%o5, PAGE_SHIFT, %o5 /* Put into vaddr position */
	or		%o4, %o5, %o4	/* Full VADDR. */
	srlx		%o4, PAGE_SHIFT, %o4 /* Shift down to create index */
	and		%o4, %o3, %o4	/* Mask with new_tsb_nents-1 */
	sllx		%o4, 4, %o4	/* Shift back up into tsb ent offset */
	TSB_STORE(%o2 + %o4, %g2)	/* Store TAG */
	add		%o4, 0x8, %o4	/* Advance to TTE */
	TSB_STORE(%o2 + %o4, %g3)	/* Store TTE */

80:	add		%o0, 16, %o0
	cmp		%o0, %g1
	bne,pt		%xcc, 90b
	 nop

	retl
	 nop
	.size		copy_tsb, .-copy_tsb

	/* Set the invalid bit in all TSB entries.  */
	.align		32
	.globl		tsb_init
	.type		tsb_init,#function
tsb_init:		/* %o0 = TSB vaddr, %o1 = size in bytes */
	prefetch	[%o0 + 0x000], #n_writes
	mov		1, %g1
	prefetch	[%o0 + 0x040], #n_writes
	sllx		%g1, TSB_TAG_INVALID_BIT, %g1
	prefetch	[%o0 + 0x080], #n_writes
1:	prefetch	[%o0 + 0x0c0], #n_writes
	stx		%g1, [%o0 + 0x00]
	stx		%g1, [%o0 + 0x10]
	stx		%g1, [%o0 + 0x20]
	stx		%g1, [%o0 + 0x30]
	prefetch	[%o0 + 0x100], #n_writes
	stx		%g1, [%o0 + 0x40]
	stx		%g1, [%o0 + 0x50]
	stx		%g1, [%o0 + 0x60]
	stx		%g1, [%o0 + 0x70]
	prefetch	[%o0 + 0x140], #n_writes
	stx		%g1, [%o0 + 0x80]
	stx		%g1, [%o0 + 0x90]
	stx		%g1, [%o0 + 0xa0]
	stx		%g1, [%o0 + 0xb0]
	prefetch	[%o0 + 0x180], #n_writes
	stx		%g1, [%o0 + 0xc0]
	stx		%g1, [%o0 + 0xd0]
	stx		%g1, [%o0 + 0xe0]
	stx		%g1, [%o0 + 0xf0]
	subcc		%o1, 0x100, %o1
	bne,pt		%xcc, 1b
	 add		%o0, 0x100, %o0
	retl
	 nop
	nop
	nop
	.size		tsb_init, .-tsb_init

	.globl		NGtsb_init
	.type		NGtsb_init,#function
NGtsb_init:
	rd		%asi, %g2
	mov		1, %g1
	wr		%g0, ASI_BLK_INIT_QUAD_LDD_P, %asi
	sllx		%g1, TSB_TAG_INVALID_BIT, %g1
1:	stxa		%g1, [%o0 + 0x00] %asi
	stxa		%g1, [%o0 + 0x10] %asi
	stxa		%g1, [%o0 + 0x20] %asi
	stxa		%g1, [%o0 + 0x30] %asi
	stxa		%g1, [%o0 + 0x40] %asi
	stxa		%g1, [%o0 + 0x50] %asi
	stxa		%g1, [%o0 + 0x60] %asi
	stxa		%g1, [%o0 + 0x70] %asi
	stxa		%g1, [%o0 + 0x80] %asi
	stxa		%g1, [%o0 + 0x90] %asi
	stxa		%g1, [%o0 + 0xa0] %asi
	stxa		%g1, [%o0 + 0xb0] %asi
	stxa		%g1, [%o0 + 0xc0] %asi
	stxa		%g1, [%o0 + 0xd0] %asi
	stxa		%g1, [%o0 + 0xe0] %asi
	stxa		%g1, [%o0 + 0xf0] %asi
	subcc		%o1, 0x100, %o1
	bne,pt		%xcc, 1b
	 add		%o0, 0x100, %o0
	membar		#Sync
	retl
	 wr		%g2, 0x0, %asi
	.size		NGtsb_init, .-NGtsb_init
