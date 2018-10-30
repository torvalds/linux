/* SPDX-License-Identifier: GPL-2.0 */
/* ITLB ** ICACHE line 1: Context 0 check and TSB load	*/
	ldxa	[%g0] ASI_IMMU_TSB_8KB_PTR, %g1	! Get TSB 8K pointer
	ldxa	[%g0] ASI_IMMU, %g6		! Get TAG TARGET
	srlx	%g6, 48, %g5			! Get context
	sllx	%g6, 22, %g6			! Zero out context
	brz,pn	%g5, kvmap_itlb			! Context 0 processing
	 srlx	%g6, 22, %g6			! Delay slot
	TSB_LOAD_QUAD(%g1, %g4)			! Load TSB entry
	cmp	%g4, %g6			! Compare TAG

/* ITLB ** ICACHE line 2: TSB compare and TLB load	*/
	bne,pn	%xcc, tsb_miss_itlb		! Miss
	 mov	FAULT_CODE_ITLB, %g3
	sethi	%hi(_PAGE_EXEC_4U), %g4
	andcc	%g5, %g4, %g0			! Executable?
	be,pn	%xcc, tsb_do_fault
	 nop					! Delay slot, fill me
	stxa	%g5, [%g0] ASI_ITLB_DATA_IN	! Load TLB
	retry					! Trap done

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
