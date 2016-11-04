/* arch/sparc64/kernel/ktlb.S: Kernel mapping TLB miss handling.
 *
 * Copyright (C) 1995, 1997, 2005, 2008 David S. Miller <davem@davemloft.net>
 * Copyright (C) 1996 Eddie C. Dost        (ecd@brainaid.de)
 * Copyright (C) 1996 Miguel de Icaza      (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996,98,99 Jakub Jelinek  (jj@sunsite.mff.cuni.cz)
 */

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tsb.h>

	.text
	.align		32

kvmap_itlb:
	/* g6: TAG TARGET */
	mov		TLB_TAG_ACCESS, %g4
	ldxa		[%g4] ASI_IMMU, %g4

	/* The kernel executes in context zero, therefore we do not
	 * need to clear the context ID bits out of %g4 here.
	 */

	/* sun4v_itlb_miss branches here with the missing virtual
	 * address already loaded into %g4
	 */
kvmap_itlb_4v:

	/* Catch kernel NULL pointer calls.  */
	sethi		%hi(PAGE_SIZE), %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_itlb_longpath
	 nop

	KERN_TSB_LOOKUP_TL1(%g4, %g6, %g5, %g1, %g2, %g3, kvmap_itlb_load)

kvmap_itlb_tsb_miss:
	sethi		%hi(LOW_OBP_ADDRESS), %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_itlb_vmalloc_addr
	 mov		0x1, %g5
	sllx		%g5, 32, %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_itlb_obp
	 nop

kvmap_itlb_vmalloc_addr:
	KERN_PGTABLE_WALK(%g4, %g5, %g2, kvmap_itlb_longpath)

	TSB_LOCK_TAG(%g1, %g2, %g7)
	TSB_WRITE(%g1, %g5, %g6)

	/* fallthrough to TLB load */

kvmap_itlb_load:

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

kvmap_itlb_longpath:

661:	rdpr	%pstate, %g5
	wrpr	%g5, PSTATE_AG | PSTATE_MG, %pstate
	.section .sun4v_2insn_patch, "ax"
	.word	661b
	SET_GL(1)
	nop
	.previous

	rdpr	%tpc, %g5
	ba,pt	%xcc, sparc64_realfault_common
	 mov	FAULT_CODE_ITLB, %g4

kvmap_itlb_obp:
	OBP_TRANS_LOOKUP(%g4, %g5, %g2, %g3, kvmap_itlb_longpath)

	TSB_LOCK_TAG(%g1, %g2, %g7)

	TSB_WRITE(%g1, %g5, %g6)

	ba,pt		%xcc, kvmap_itlb_load
	 nop

kvmap_dtlb_obp:
	OBP_TRANS_LOOKUP(%g4, %g5, %g2, %g3, kvmap_dtlb_longpath)

	TSB_LOCK_TAG(%g1, %g2, %g7)

	TSB_WRITE(%g1, %g5, %g6)

	ba,pt		%xcc, kvmap_dtlb_load
	 nop

kvmap_linear_early:
	sethi		%hi(kern_linear_pte_xor), %g7
	ldx		[%g7 + %lo(kern_linear_pte_xor)], %g2
	ba,pt		%xcc, kvmap_dtlb_tsb4m_load
	 xor		%g2, %g4, %g5

	.align		32
kvmap_dtlb_tsb4m_load:
	TSB_LOCK_TAG(%g1, %g2, %g7)
	TSB_WRITE(%g1, %g5, %g6)
	ba,pt		%xcc, kvmap_dtlb_load
	 nop

kvmap_dtlb:
	/* %g6: TAG TARGET */
	mov		TLB_TAG_ACCESS, %g4
	ldxa		[%g4] ASI_DMMU, %g4

	/* The kernel executes in context zero, therefore we do not
	 * need to clear the context ID bits out of %g4 here.
	 */

	/* sun4v_dtlb_miss branches here with the missing virtual
	 * address already loaded into %g4
	 */
kvmap_dtlb_4v:
	brgez,pn	%g4, kvmap_dtlb_nonlinear
	 nop

#ifdef CONFIG_DEBUG_PAGEALLOC
	/* Index through the base page size TSB even for linear
	 * mappings when using page allocation debugging.
	 */
	KERN_TSB_LOOKUP_TL1(%g4, %g6, %g5, %g1, %g2, %g3, kvmap_dtlb_load)
#else
	/* Correct TAG_TARGET is already in %g6, check 4mb TSB.  */
	KERN_TSB4M_LOOKUP_TL1(%g6, %g5, %g1, %g2, %g3, kvmap_dtlb_load)
#endif
	/* Linear mapping TSB lookup failed.  Fallthrough to kernel
	 * page table based lookup.
	 */
	.globl		kvmap_linear_patch
kvmap_linear_patch:
	ba,a,pt		%xcc, kvmap_linear_early

kvmap_dtlb_vmalloc_addr:
	KERN_PGTABLE_WALK(%g4, %g5, %g2, kvmap_dtlb_longpath)

	TSB_LOCK_TAG(%g1, %g2, %g7)
	TSB_WRITE(%g1, %g5, %g6)

	/* fallthrough to TLB load */

kvmap_dtlb_load:

661:	stxa		%g5, [%g0] ASI_DTLB_DATA_IN	! Reload TLB
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

#ifdef CONFIG_SPARSEMEM_VMEMMAP
kvmap_vmemmap:
	KERN_PGTABLE_WALK(%g4, %g5, %g2, kvmap_dtlb_longpath)
	ba,a,pt		%xcc, kvmap_dtlb_load
#endif

kvmap_dtlb_nonlinear:
	/* Catch kernel NULL pointer derefs.  */
	sethi		%hi(PAGE_SIZE), %g5
	cmp		%g4, %g5
	bleu,pn		%xcc, kvmap_dtlb_longpath
	 nop

#ifdef CONFIG_SPARSEMEM_VMEMMAP
	/* Do not use the TSB for vmemmap.  */
	sethi		%hi(VMEMMAP_BASE), %g5
	ldx		[%g5 + %lo(VMEMMAP_BASE)], %g5
	cmp		%g4,%g5
	bgeu,pn		%xcc, kvmap_vmemmap
	 nop
#endif

	KERN_TSB_LOOKUP_TL1(%g4, %g6, %g5, %g1, %g2, %g3, kvmap_dtlb_load)

kvmap_dtlb_tsbmiss:
	sethi		%hi(MODULES_VADDR), %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_dtlb_longpath
	 sethi		%hi(VMALLOC_END), %g5
	ldx		[%g5 + %lo(VMALLOC_END)], %g5
	cmp		%g4, %g5
	bgeu,pn		%xcc, kvmap_dtlb_longpath
	 nop

kvmap_check_obp:
	sethi		%hi(LOW_OBP_ADDRESS), %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_dtlb_vmalloc_addr
	 mov		0x1, %g5
	sllx		%g5, 32, %g5
	cmp		%g4, %g5
	blu,pn		%xcc, kvmap_dtlb_obp
	 nop
	ba,pt		%xcc, kvmap_dtlb_vmalloc_addr
	 nop

kvmap_dtlb_longpath:

661:	rdpr	%pstate, %g5
	wrpr	%g5, PSTATE_AG | PSTATE_MG, %pstate
	.section .sun4v_2insn_patch, "ax"
	.word	661b
	SET_GL(1)
	ldxa		[%g0] ASI_SCRATCHPAD, %g5
	.previous

	rdpr	%tl, %g3
	cmp	%g3, 1

661:	mov	TLB_TAG_ACCESS, %g4
	ldxa	[%g4] ASI_DMMU, %g5
	.section .sun4v_2insn_patch, "ax"
	.word	661b
	ldx	[%g5 + HV_FAULT_D_ADDR_OFFSET], %g5
	nop
	.previous

	/* The kernel executes in context zero, therefore we do not
	 * need to clear the context ID bits out of %g5 here.
	 */

	be,pt	%xcc, sparc64_realfault_common
	 mov	FAULT_CODE_DTLB, %g4
	ba,pt	%xcc, winfix_trampoline
	 nop
