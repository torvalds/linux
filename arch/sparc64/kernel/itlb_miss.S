/* ITLB ** ICACHE line 1: Context 0 check and TSB load	*/
	ldxa	[%g0] ASI_IMMU_TSB_8KB_PTR, %g1	! Get TSB 8K pointer
	ldxa	[%g0] ASI_IMMU, %g6		! Get TAG TARGET
	srlx	%g6, 48, %g5			! Get context
	brz,pn	%g5, kvmap_itlb			! Context 0 processing
	 nop					! Delay slot (fill me)
	ldda	[%g1] ASI_NUCLEUS_QUAD_LDD, %g4	! Load TSB entry
	cmp	%g4, %g6			! Compare TAG
	sethi	%hi(_PAGE_EXEC), %g4		! Setup exec check

/* ITLB ** ICACHE line 2: TSB compare and TLB load	*/
	bne,pn	%xcc, tsb_miss_itlb		! Miss
	 mov	FAULT_CODE_ITLB, %g3
	andcc	%g5, %g4, %g0			! Executable?
	be,pn	%xcc, tsb_do_fault
	 nop					! Delay slot, fill me
	stxa	%g5, [%g0] ASI_ITLB_DATA_IN	! Load TLB
	retry					! Trap done
	nop

/* ITLB ** ICACHE line 3: 				*/
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

/* ITLB ** ICACHE line 4: 				*/
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
