	/* The registers for cross calls will be:
	 *
	 * DATA 0: [low 32-bits]  Address of function to call, jmp to this
	 *         [high 32-bits] MMU Context Argument 0, place in %g5
	 * DATA 1: Address Argument 1, place in %g1
	 * DATA 2: Address Argument 2, place in %g7
	 *
	 * With this method we can do most of the cross-call tlb/cache
	 * flushing very quickly.
	 */
	.align		32
	.globl		do_ivec
	.type		do_ivec,#function
do_ivec:
	mov		0x40, %g3
	ldxa		[%g3 + %g0] ASI_INTR_R, %g3
	sethi		%hi(KERNBASE), %g4
	cmp		%g3, %g4
	bgeu,pn		%xcc, do_ivec_xcall
	 srlx		%g3, 32, %g5
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync

	sethi		%hi(ivector_table_pa), %g2
	ldx		[%g2 + %lo(ivector_table_pa)], %g2
	sllx		%g3, 4, %g3
	add		%g2, %g3, %g3

	TRAP_LOAD_IRQ_WORK_PA(%g6, %g1)

	ldx		[%g6], %g5
	stxa		%g5, [%g3] ASI_PHYS_USE_EC
	stx		%g3, [%g6]
	wr		%g0, 1 << PIL_DEVICE_IRQ, %set_softint
	retry
do_ivec_xcall:
	mov		0x50, %g1
	ldxa		[%g1 + %g0] ASI_INTR_R, %g1
	srl		%g3, 0, %g3

	mov		0x60, %g7
	ldxa		[%g7 + %g0] ASI_INTR_R, %g7
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync
	ba,pt		%xcc, 1f
	 nop

	.align		32
1:	jmpl		%g3, %g0
	 nop
	.size		do_ivec,.-do_ivec
