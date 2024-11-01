/* SPDX-License-Identifier: GPL-2.0 */
/* DTLB ** ICACHE line 1: Context 0 check and TSB load	*/
	ldxa	[%g0] ASI_DMMU_TSB_8KB_PTR, %g1	! Get TSB 8K pointer
	ldxa	[%g0] ASI_DMMU, %g6		! Get TAG TARGET
	srlx	%g6, 48, %g5			! Get context
	sllx	%g6, 22, %g6			! Zero out context
	brz,pn	%g5, kvmap_dtlb			! Context 0 processing
	 srlx	%g6, 22, %g6			! Delay slot
	TSB_LOAD_QUAD(%g1, %g4)			! Load TSB entry
	cmp	%g4, %g6			! Compare TAG

/* DTLB ** ICACHE line 2: TSB compare and TLB load	*/
	bne,pn	%xcc, tsb_miss_dtlb		! Miss
	 mov	FAULT_CODE_DTLB, %g3
	stxa	%g5, [%g0] ASI_DTLB_DATA_IN	! Load TLB
	retry					! Trap done
	nop
	nop
	nop
	nop

/* DTLB ** ICACHE line 3:				*/
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop

/* DTLB ** ICACHE line 4: 				*/
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
