/* sun4v_tlb_miss.S: Sun4v TLB miss handlers.
 *
 * Copyright (C) 2006 <davem@davemloft.net>
 */

	.text
	.align	32

	/* Load ITLB fault information into VADDR and CTX, using BASE.  */
#define LOAD_ITLB_INFO(BASE, VADDR, CTX) \
	ldx	[BASE + HV_FAULT_I_ADDR_OFFSET], VADDR; \
	ldx	[BASE + HV_FAULT_I_CTX_OFFSET], CTX;

	/* Load DTLB fault information into VADDR and CTX, using BASE.  */
#define LOAD_DTLB_INFO(BASE, VADDR, CTX) \
	ldx	[BASE + HV_FAULT_D_ADDR_OFFSET], VADDR; \
	ldx	[BASE + HV_FAULT_D_CTX_OFFSET], CTX;

	/* DEST = (VADDR >> 22)
	 *
	 * Branch to ZERO_CTX_LABEL if context is zero.
	 */
#define	COMPUTE_TAG_TARGET(DEST, VADDR, CTX, ZERO_CTX_LABEL) \
	srlx	VADDR, 22, DEST; \
	brz,pn	CTX, ZERO_CTX_LABEL; \
	 nop;

	/* Create TSB pointer.  This is something like:
	 *
	 * index_mask = (512 << (tsb_reg & 0x7UL)) - 1UL;
	 * tsb_base = tsb_reg & ~0x7UL;
	 * tsb_index = ((vaddr >> HASH_SHIFT) & tsb_mask);
	 * tsb_ptr = tsb_base + (tsb_index * 16);
	 */
#define COMPUTE_TSB_PTR(TSB_PTR, VADDR, HASH_SHIFT, TMP1, TMP2) \
	and	TSB_PTR, 0x7, TMP1;			\
	mov	512, TMP2;				\
	andn	TSB_PTR, 0x7, TSB_PTR;			\
	sllx	TMP2, TMP1, TMP2;			\
	srlx	VADDR, HASH_SHIFT, TMP1;		\
	sub	TMP2, 1, TMP2;				\
	and	TMP1, TMP2, TMP1;			\
	sllx	TMP1, 4, TMP1;				\
	add	TSB_PTR, TMP1, TSB_PTR;

sun4v_itlb_miss:
	/* Load MMU Miss base into %g2.  */
	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	
	/* Load UTSB reg into %g1.  */
	mov	SCRATCHPAD_UTSBREG1, %g1
	ldxa	[%g1] ASI_SCRATCHPAD, %g1

	LOAD_ITLB_INFO(%g2, %g4, %g5)
	COMPUTE_TAG_TARGET(%g6, %g4, %g5, kvmap_itlb_4v)
	COMPUTE_TSB_PTR(%g1, %g4, PAGE_SHIFT, %g3, %g7)

	/* Load TSB tag/pte into %g2/%g3 and compare the tag.  */
	ldda	[%g1] ASI_QUAD_LDD_PHYS_4V, %g2
	cmp	%g2, %g6
	bne,a,pn %xcc, tsb_miss_page_table_walk
	 mov	FAULT_CODE_ITLB, %g3
	andcc	%g3, _PAGE_EXEC_4V, %g0
	be,a,pn	%xcc, tsb_do_fault
	 mov	FAULT_CODE_ITLB, %g3

	/* We have a valid entry, make hypervisor call to load
	 * I-TLB and return from trap.
	 *
	 * %g3:	PTE
	 * %g4:	vaddr
	 */
sun4v_itlb_load:
	ldxa	[%g0] ASI_SCRATCHPAD, %g6
	mov	%o0, %g1		! save %o0
	mov	%o1, %g2		! save %o1
	mov	%o2, %g5		! save %o2
	mov	%o3, %g7		! save %o3
	mov	%g4, %o0		! vaddr
	ldx	[%g6 + HV_FAULT_I_CTX_OFFSET], %o1	! ctx
	mov	%g3, %o2		! PTE
	mov	HV_MMU_IMMU, %o3	! flags
	ta	HV_MMU_MAP_ADDR_TRAP
	brnz,pn	%o0, sun4v_itlb_error
	 mov	%g2, %o1		! restore %o1
	mov	%g1, %o0		! restore %o0
	mov	%g5, %o2		! restore %o2
	mov	%g7, %o3		! restore %o3

	retry

sun4v_dtlb_miss:
	/* Load MMU Miss base into %g2.  */
	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	
	/* Load UTSB reg into %g1.  */
	mov	SCRATCHPAD_UTSBREG1, %g1
	ldxa	[%g1] ASI_SCRATCHPAD, %g1

	LOAD_DTLB_INFO(%g2, %g4, %g5)
	COMPUTE_TAG_TARGET(%g6, %g4, %g5, kvmap_dtlb_4v)
	COMPUTE_TSB_PTR(%g1, %g4, PAGE_SHIFT, %g3, %g7)

	/* Load TSB tag/pte into %g2/%g3 and compare the tag.  */
	ldda	[%g1] ASI_QUAD_LDD_PHYS_4V, %g2
	cmp	%g2, %g6
	bne,a,pn %xcc, tsb_miss_page_table_walk
	 mov	FAULT_CODE_DTLB, %g3

	/* We have a valid entry, make hypervisor call to load
	 * D-TLB and return from trap.
	 *
	 * %g3:	PTE
	 * %g4:	vaddr
	 */
sun4v_dtlb_load:
	ldxa	[%g0] ASI_SCRATCHPAD, %g6
	mov	%o0, %g1		! save %o0
	mov	%o1, %g2		! save %o1
	mov	%o2, %g5		! save %o2
	mov	%o3, %g7		! save %o3
	mov	%g4, %o0		! vaddr
	ldx	[%g6 + HV_FAULT_D_CTX_OFFSET], %o1	! ctx
	mov	%g3, %o2		! PTE
	mov	HV_MMU_DMMU, %o3	! flags
	ta	HV_MMU_MAP_ADDR_TRAP
	brnz,pn	%o0, sun4v_dtlb_error
	 mov	%g2, %o1		! restore %o1
	mov	%g1, %o0		! restore %o0
	mov	%g5, %o2		! restore %o2
	mov	%g7, %o3		! restore %o3

	retry

sun4v_dtlb_prot:
	SET_GL(1)

	/* Load MMU Miss base into %g5.  */
	ldxa	[%g0] ASI_SCRATCHPAD, %g5
	
	ldx	[%g5 + HV_FAULT_D_ADDR_OFFSET], %g5
	rdpr	%tl, %g1
	cmp	%g1, 1
	bgu,pn	%xcc, winfix_trampoline
	 mov	FAULT_CODE_DTLB | FAULT_CODE_WRITE, %g4
	ba,pt	%xcc, sparc64_realfault_common
	 nop

	/* Called from trap table:
	 * %g4:	vaddr
	 * %g5:	context
	 * %g6: TAG TARGET
	 */
sun4v_itsb_miss:
	mov	SCRATCHPAD_UTSBREG1, %g1
	ldxa	[%g1] ASI_SCRATCHPAD, %g1
	brz,pn	%g5, kvmap_itlb_4v
	 mov	FAULT_CODE_ITLB, %g3
	ba,a,pt	%xcc, sun4v_tsb_miss_common

	/* Called from trap table:
	 * %g4:	vaddr
	 * %g5:	context
	 * %g6: TAG TARGET
	 */
sun4v_dtsb_miss:
	mov	SCRATCHPAD_UTSBREG1, %g1
	ldxa	[%g1] ASI_SCRATCHPAD, %g1
	brz,pn	%g5, kvmap_dtlb_4v
	 mov	FAULT_CODE_DTLB, %g3

	/* fallthrough */

sun4v_tsb_miss_common:
	COMPUTE_TSB_PTR(%g1, %g4, PAGE_SHIFT, %g5, %g7)

	sub	%g2, TRAP_PER_CPU_FAULT_INFO, %g2

#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)
	mov	SCRATCHPAD_UTSBREG2, %g5
	ldxa	[%g5] ASI_SCRATCHPAD, %g5
	cmp	%g5, -1
	be,pt	%xcc, 80f
	 nop
	COMPUTE_TSB_PTR(%g5, %g4, REAL_HPAGE_SHIFT, %g2, %g7)

	/* That clobbered %g2, reload it.  */
	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	sub	%g2, TRAP_PER_CPU_FAULT_INFO, %g2

80:	stx	%g5, [%g2 + TRAP_PER_CPU_TSB_HUGE_TEMP]
#endif

	ba,pt	%xcc, tsb_miss_page_table_walk_sun4v_fastpath
	 ldx	[%g2 + TRAP_PER_CPU_PGD_PADDR], %g7

sun4v_itlb_error:
	sethi	%hi(sun4v_err_itlb_vaddr), %g1
	stx	%g4, [%g1 + %lo(sun4v_err_itlb_vaddr)]
	sethi	%hi(sun4v_err_itlb_ctx), %g1
	ldxa	[%g0] ASI_SCRATCHPAD, %g6
	ldx	[%g6 + HV_FAULT_I_CTX_OFFSET], %o1
	stx	%o1, [%g1 + %lo(sun4v_err_itlb_ctx)]
	sethi	%hi(sun4v_err_itlb_pte), %g1
	stx	%g3, [%g1 + %lo(sun4v_err_itlb_pte)]
	sethi	%hi(sun4v_err_itlb_error), %g1
	stx	%o0, [%g1 + %lo(sun4v_err_itlb_error)]

	rdpr	%tl, %g4
	cmp	%g4, 1
	ble,pt	%icc, 1f
	 sethi	%hi(2f), %g7
	ba,pt	%xcc, etraptl1
	 or	%g7, %lo(2f), %g7

1:	ba,pt	%xcc, etrap
2:	 or	%g7, %lo(2b), %g7
	mov	%l4, %o1
	call	sun4v_itlb_error_report
	 add	%sp, PTREGS_OFF, %o0

	/* NOTREACHED */

sun4v_dtlb_error:
	sethi	%hi(sun4v_err_dtlb_vaddr), %g1
	stx	%g4, [%g1 + %lo(sun4v_err_dtlb_vaddr)]
	sethi	%hi(sun4v_err_dtlb_ctx), %g1
	ldxa	[%g0] ASI_SCRATCHPAD, %g6
	ldx	[%g6 + HV_FAULT_D_CTX_OFFSET], %o1
	stx	%o1, [%g1 + %lo(sun4v_err_dtlb_ctx)]
	sethi	%hi(sun4v_err_dtlb_pte), %g1
	stx	%g3, [%g1 + %lo(sun4v_err_dtlb_pte)]
	sethi	%hi(sun4v_err_dtlb_error), %g1
	stx	%o0, [%g1 + %lo(sun4v_err_dtlb_error)]

	rdpr	%tl, %g4
	cmp	%g4, 1
	ble,pt	%icc, 1f
	 sethi	%hi(2f), %g7
	ba,pt	%xcc, etraptl1
	 or	%g7, %lo(2f), %g7

1:	ba,pt	%xcc, etrap
2:	 or	%g7, %lo(2b), %g7
	mov	%l4, %o1
	call	sun4v_dtlb_error_report
	 add	%sp, PTREGS_OFF, %o0

	/* NOTREACHED */

	/* Instruction Access Exception, tl0. */
sun4v_iacc:
	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	ldx	[%g2 + HV_FAULT_I_TYPE_OFFSET], %g3
	ldx	[%g2 + HV_FAULT_I_ADDR_OFFSET], %g4
	ldx	[%g2 + HV_FAULT_I_CTX_OFFSET], %g5
	sllx	%g3, 16, %g3
	or	%g5, %g3, %g5
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	mov	%l4, %o1
	mov	%l5, %o2
	call	sun4v_insn_access_exception
	 add	%sp, PTREGS_OFF, %o0
	ba,a,pt	%xcc, rtrap

	/* Instruction Access Exception, tl1. */
sun4v_iacc_tl1:
	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	ldx	[%g2 + HV_FAULT_I_TYPE_OFFSET], %g3
	ldx	[%g2 + HV_FAULT_I_ADDR_OFFSET], %g4
	ldx	[%g2 + HV_FAULT_I_CTX_OFFSET], %g5
	sllx	%g3, 16, %g3
	or	%g5, %g3, %g5
	ba,pt	%xcc, etraptl1
	 rd	%pc, %g7
	mov	%l4, %o1
	mov	%l5, %o2
	call	sun4v_insn_access_exception_tl1
	 add	%sp, PTREGS_OFF, %o0
	ba,a,pt	%xcc, rtrap

	/* Data Access Exception, tl0. */
sun4v_dacc:
	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	ldx	[%g2 + HV_FAULT_D_TYPE_OFFSET], %g3
	ldx	[%g2 + HV_FAULT_D_ADDR_OFFSET], %g4
	ldx	[%g2 + HV_FAULT_D_CTX_OFFSET], %g5
	sllx	%g3, 16, %g3
	or	%g5, %g3, %g5
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	mov	%l4, %o1
	mov	%l5, %o2
	call	sun4v_data_access_exception
	 add	%sp, PTREGS_OFF, %o0
	ba,a,pt	%xcc, rtrap

	/* Data Access Exception, tl1. */
sun4v_dacc_tl1:
	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	ldx	[%g2 + HV_FAULT_D_TYPE_OFFSET], %g3
	ldx	[%g2 + HV_FAULT_D_ADDR_OFFSET], %g4
	ldx	[%g2 + HV_FAULT_D_CTX_OFFSET], %g5
	sllx	%g3, 16, %g3
	or	%g5, %g3, %g5
	ba,pt	%xcc, etraptl1
	 rd	%pc, %g7
	mov	%l4, %o1
	mov	%l5, %o2
	call	sun4v_data_access_exception_tl1
	 add	%sp, PTREGS_OFF, %o0
	ba,a,pt	%xcc, rtrap

	/* Memory Address Unaligned.  */
sun4v_mna:
	/* Window fixup? */
	rdpr	%tl, %g2
	cmp	%g2, 1
	ble,pt	%icc, 1f
	 nop

	SET_GL(1)
	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	ldx	[%g2 + HV_FAULT_D_ADDR_OFFSET], %g5
	mov	HV_FAULT_TYPE_UNALIGNED, %g3
	ldx	[%g2 + HV_FAULT_D_CTX_OFFSET], %g4
	sllx	%g3, 16, %g3
	or	%g4, %g3, %g4
	ba,pt	%xcc, winfix_mna
	 rdpr	%tpc, %g3
	/* not reached */

1:	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	mov	HV_FAULT_TYPE_UNALIGNED, %g3
	ldx	[%g2 + HV_FAULT_D_ADDR_OFFSET], %g4
	ldx	[%g2 + HV_FAULT_D_CTX_OFFSET], %g5
	sllx	%g3, 16, %g3
	or	%g5, %g3, %g5

	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	mov	%l4, %o1
	mov	%l5, %o2
	call	sun4v_do_mna
	 add	%sp, PTREGS_OFF, %o0
	ba,a,pt	%xcc, rtrap

	/* Privileged Action.  */
sun4v_privact:
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	call	do_privact
	 add	%sp, PTREGS_OFF, %o0
	ba,a,pt	%xcc, rtrap

	/* Unaligned ldd float, tl0. */
sun4v_lddfmna:
	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	ldx	[%g2 + HV_FAULT_D_TYPE_OFFSET], %g3
	ldx	[%g2 + HV_FAULT_D_ADDR_OFFSET], %g4
	ldx	[%g2 + HV_FAULT_D_CTX_OFFSET], %g5
	sllx	%g3, 16, %g3
	or	%g5, %g3, %g5
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	mov	%l4, %o1
	mov	%l5, %o2
	call	handle_lddfmna
	 add	%sp, PTREGS_OFF, %o0
	ba,a,pt	%xcc, rtrap

	/* Unaligned std float, tl0. */
sun4v_stdfmna:
	ldxa	[%g0] ASI_SCRATCHPAD, %g2
	ldx	[%g2 + HV_FAULT_D_TYPE_OFFSET], %g3
	ldx	[%g2 + HV_FAULT_D_ADDR_OFFSET], %g4
	ldx	[%g2 + HV_FAULT_D_CTX_OFFSET], %g5
	sllx	%g3, 16, %g3
	or	%g5, %g3, %g5
	ba,pt	%xcc, etrap
	 rd	%pc, %g7
	mov	%l4, %o1
	mov	%l5, %o2
	call	handle_stdfmna
	 add	%sp, PTREGS_OFF, %o0
	ba,a,pt	%xcc, rtrap

#define BRANCH_ALWAYS	0x10680000
#define NOP		0x01000000
#define SUN4V_DO_PATCH(OLD, NEW)	\
	sethi	%hi(NEW), %g1; \
	or	%g1, %lo(NEW), %g1; \
	sethi	%hi(OLD), %g2; \
	or	%g2, %lo(OLD), %g2; \
	sub	%g1, %g2, %g1; \
	sethi	%hi(BRANCH_ALWAYS), %g3; \
	sll	%g1, 11, %g1; \
	srl	%g1, 11 + 2, %g1; \
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
	SUN4V_DO_PATCH(tl0_iax, sun4v_iacc)
	SUN4V_DO_PATCH(tl1_iax, sun4v_iacc_tl1)
	SUN4V_DO_PATCH(tl0_dax, sun4v_dacc)
	SUN4V_DO_PATCH(tl1_dax, sun4v_dacc_tl1)
	SUN4V_DO_PATCH(tl0_mna, sun4v_mna)
	SUN4V_DO_PATCH(tl1_mna, sun4v_mna)
	SUN4V_DO_PATCH(tl0_lddfmna, sun4v_lddfmna)
	SUN4V_DO_PATCH(tl0_stdfmna, sun4v_stdfmna)
	SUN4V_DO_PATCH(tl0_privact, sun4v_privact)
	retl
	 nop
	.size	sun4v_patch_tlb_handlers,.-sun4v_patch_tlb_handlers
