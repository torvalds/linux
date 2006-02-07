/* sun4v_tlb_miss.S: Sun4v TLB miss handlers.
 *
 * Copyright (C) 2006 <davem@davemloft.net>
 */

	.text
	.align	32

sun4v_itlb_miss:
	/* Load CPU ID into %g3.  */
	mov	SCRATCHPAD_CPUID, %g1
	ldxa	[%g1] ASI_SCRATCHPAD, %g3
	
	/* Load UTSB reg into %g1.  */
	ldxa	[%g1 + %g1] ASI_SCRATCHPAD, %g1

	/* Load &trap_block[smp_processor_id()] into %g2.  */
	sethi	%hi(trap_block), %g2
	or	%g2, %lo(trap_block), %g2
	sllx	%g3, TRAP_BLOCK_SZ_SHIFT, %g3
	add	%g2, %g3, %g2

	/* Create a TAG TARGET, "(vaddr>>22) | (ctx << 48)", in %g6.
	 * Branch if kernel TLB miss.  The kernel TSB and user TSB miss
	 * code wants the missing virtual address in %g4, so that value
	 * cannot be modified through the entirety of this handler.
	 */
	ldx	[%g2 + TRAP_PER_CPU_FAULT_INFO + HV_FAULT_I_ADDR_OFFSET], %g4
	ldx	[%g2 + TRAP_PER_CPU_FAULT_INFO + HV_FAULT_I_CTX_OFFSET], %g5
	srlx	%g4, 22, %g3
	sllx	%g5, 48, %g6
	or	%g6, %g3, %g6
	brz,pn	%g5, kvmap_itlb_4v
	 nop

	/* Create TSB pointer.  This is something like:
	 *
	 * index_mask = (512 << (tsb_reg & 0x7UL)) - 1UL;
	 * tsb_base = tsb_reg & ~0x7UL;
	 */
	and	%g1, 0x7, %g3
	andn	%g1, 0x7, %g1
	mov	512, %g7
	sllx	%g7, %g3, %g7
	sub	%g7, 1, %g7

	/* TSB index mask is in %g7, tsb base is in %g1.  Compute
	 * the TSB entry pointer into %g1:
	 *
	 * tsb_index = ((vaddr >> PAGE_SHIFT) & tsb_mask);
	 * tsb_ptr = tsb_base + (tsb_index * 16);
	 */
	srlx	%g4, PAGE_SHIFT, %g3
	and	%g3, %g7, %g3
	sllx	%g3, 4, %g3
	add	%g1, %g3, %g1

	/* Load TSB tag/pte into %g2/%g3 and compare the tag.  */
	ldda	[%g1] ASI_QUAD_LDD_PHYS, %g2
	cmp	%g2, %g6
	sethi	%hi(_PAGE_EXEC), %g7
	bne,a,pn %xcc, tsb_miss_page_table_walk
	 mov	FAULT_CODE_ITLB, %g3
	andcc	%g3, %g7, %g0
	be,a,pn	%xcc, tsb_do_fault
	 mov	FAULT_CODE_ITLB, %g3

	/* We have a valid entry, make hypervisor call to load
	 * I-TLB and return from trap.
	 *
	 * %g3:	PTE
	 * %g4:	vaddr
	 * %g6:	TAG TARGET (only "CTX << 48" part matters)
	 */
sun4v_itlb_load:
	mov	%o0, %g1		! save %o0
	mov	%o1, %g2		! save %o1
	mov	%o2, %g5		! save %o2
	mov	%o3, %g7		! save %o3
	mov	%g4, %o0		! vaddr
	srlx	%g6, 48, %o1		! ctx
	mov	%g3, %o2		! PTE
	mov	HV_MMU_IMMU, %o3	! flags
	ta	HV_MMU_MAP_ADDR_TRAP
	mov	%g1, %o0		! restore %o0
	mov	%g2, %o1		! restore %o1
	mov	%g5, %o2		! restore %o2
	mov	%g7, %o3		! restore %o3

	retry

sun4v_dtlb_miss:
	/* Load CPU ID into %g3.  */
	mov	SCRATCHPAD_CPUID, %g1
	ldxa	[%g1] ASI_SCRATCHPAD, %g3
	
	/* Load UTSB reg into %g1.  */
	ldxa	[%g1 + %g1] ASI_SCRATCHPAD, %g1

	/* Load &trap_block[smp_processor_id()] into %g2.  */
	sethi	%hi(trap_block), %g2
	or	%g2, %lo(trap_block), %g2
	sllx	%g3, TRAP_BLOCK_SZ_SHIFT, %g3
	add	%g2, %g3, %g2

	/* Create a TAG TARGET, "(vaddr>>22) | (ctx << 48)", in %g6.
	 * Branch if kernel TLB miss.  The kernel TSB and user TSB miss
	 * code wants the missing virtual address in %g4, so that value
	 * cannot be modified through the entirety of this handler.
	 */
	ldx	[%g2 + TRAP_PER_CPU_FAULT_INFO + HV_FAULT_D_ADDR_OFFSET], %g4
	ldx	[%g2 + TRAP_PER_CPU_FAULT_INFO + HV_FAULT_D_CTX_OFFSET], %g5
	srlx	%g4, 22, %g3
	sllx	%g5, 48, %g6
	or	%g6, %g3, %g6
	brz,pn	%g5, kvmap_dtlb_4v
	 nop

	/* Create TSB pointer.  This is something like:
	 *
	 * index_mask = (512 << (tsb_reg & 0x7UL)) - 1UL;
	 * tsb_base = tsb_reg & ~0x7UL;
	 */
	and	%g1, 0x7, %g3
	andn	%g1, 0x7, %g1
	mov	512, %g7
	sllx	%g7, %g3, %g7
	sub	%g7, 1, %g7

	/* TSB index mask is in %g7, tsb base is in %g1.  Compute
	 * the TSB entry pointer into %g1:
	 *
	 * tsb_index = ((vaddr >> PAGE_SHIFT) & tsb_mask);
	 * tsb_ptr = tsb_base + (tsb_index * 16);
	 */
	srlx	%g4, PAGE_SHIFT, %g3
	and	%g3, %g7, %g3
	sllx	%g3, 4, %g3
	add	%g1, %g3, %g1

	/* Load TSB tag/pte into %g2/%g3 and compare the tag.  */
	ldda	[%g1] ASI_QUAD_LDD_PHYS, %g2
	cmp	%g2, %g6
	bne,a,pn %xcc, tsb_miss_page_table_walk
	 mov	FAULT_CODE_ITLB, %g3

	/* We have a valid entry, make hypervisor call to load
	 * D-TLB and return from trap.
	 *
	 * %g3:	PTE
	 * %g4:	vaddr
	 * %g6:	TAG TARGET (only "CTX << 48" part matters)
	 */
sun4v_dtlb_load:
	mov	%o0, %g1		! save %o0
	mov	%o1, %g2		! save %o1
	mov	%o2, %g5		! save %o2
	mov	%o3, %g7		! save %o3
	mov	%g4, %o0		! vaddr
	srlx	%g6, 48, %o1		! ctx
	mov	%g3, %o2		! PTE
	mov	HV_MMU_DMMU, %o3	! flags
	ta	HV_MMU_MAP_ADDR_TRAP
	mov	%g1, %o0		! restore %o0
	mov	%g2, %o1		! restore %o1
	mov	%g5, %o2		! restore %o2
	mov	%g7, %o3		! restore %o3

	retry

sun4v_dtlb_prot:
	/* Load CPU ID into %g3.  */
	mov	SCRATCHPAD_CPUID, %g1
	ldxa	[%g1] ASI_SCRATCHPAD, %g3
	
	/* Load &trap_block[smp_processor_id()] into %g2.  */
	sethi	%hi(trap_block), %g2
	or	%g2, %lo(trap_block), %g2
	sllx	%g3, TRAP_BLOCK_SZ_SHIFT, %g3
	add	%g2, %g3, %g2

	ldx	[%g2 + TRAP_PER_CPU_FAULT_INFO + HV_FAULT_D_ADDR_OFFSET], %g5
	rdpr	%tl, %g1
	cmp	%g1, 1
	bgu,pn		%xcc, winfix_trampoline
	 nop
	ba,pt		%xcc, sparc64_realfault_common
	 mov		FAULT_CODE_DTLB | FAULT_CODE_WRITE, %g4

#define BRANCH_ALWAYS	0x10680000
#define NOP		0x01000000
#define SUN4V_DO_PATCH(OLD, NEW)	\
	sethi	%hi(NEW), %g1; \
	or	%g1, %lo(NEW), %g1; \
	sethi	%hi(OLD), %g2; \
	or	%g2, %lo(OLD), %g2; \
	sub	%g1, %g2, %g1; \
	sethi	%hi(BRANCH_ALWAYS), %g3; \
	srl	%g1, 2, %g1; \
	or	%g3, %lo(BRANCH_ALWAYS), %g3; \
	or	%g3, %g1, %g3; \
	stw	%g3, [%g2]; \
	sethi	%hi(NOP), %g3; \
	or	%g3, %lo(NOP), %g3; \
	stw	%g3, [%g2 + 0x4]; \
	flush	%g2;

	.globl	sun4v_patch_tlb_handlers
	.type	sun4v_patch_tlb_handlers,#function
sun4v_patch_tlb_handlers:
	SUN4V_DO_PATCH(tl0_iamiss, sun4v_itlb_miss)
	SUN4V_DO_PATCH(tl1_iamiss, sun4v_itlb_miss)
	SUN4V_DO_PATCH(tl0_damiss, sun4v_dtlb_miss)
	SUN4V_DO_PATCH(tl1_damiss, sun4v_dtlb_miss)
	SUN4V_DO_PATCH(tl0_daprot, sun4v_dtlb_prot)
	SUN4V_DO_PATCH(tl1_daprot, sun4v_dtlb_prot)
	retl
	 nop
	.size	sun4v_patch_tlb_handlers,.-sun4v_patch_tlb_handlers
