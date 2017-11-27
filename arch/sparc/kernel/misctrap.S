/* SPDX-License-Identifier: GPL-2.0 */
#ifdef CONFIG_KGDB
	.globl		arch_kgdb_breakpoint
	.type		arch_kgdb_breakpoint,#function
arch_kgdb_breakpoint:
	ta		0x72
	retl
	 nop
	.size		arch_kgdb_breakpoint,.-arch_kgdb_breakpoint
#endif

	.type		__do_privact,#function
__do_privact:
	mov		TLB_SFSR, %g3
	stxa		%g0, [%g3] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	or		%g7, %lo(109b), %g7
	call		do_privact
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap
	.size		__do_privact,.-__do_privact

	.type		do_mna,#function
do_mna:
	rdpr		%tl, %g3
	cmp		%g3, 1

	/* Setup %g4/%g5 now as they are used in the
	 * winfixup code.
	 */
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ldxa		[%g3] ASI_DMMU, %g5
	stxa		%g0, [%g3] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	bgu,pn		%icc, winfix_mna
	 rdpr		%tpc, %g3

1:	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		mem_address_unaligned
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap
	.size		do_mna,.-do_mna

	.type		do_lddfmna,#function
do_lddfmna:
	sethi		%hi(109f), %g7
	mov		TLB_SFSR, %g4
	ldxa		[%g4] ASI_DMMU, %g5
	stxa		%g0, [%g4] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		handle_lddfmna
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap
	.size		do_lddfmna,.-do_lddfmna

	.type		do_stdfmna,#function
do_stdfmna:
	sethi		%hi(109f), %g7
	mov		TLB_SFSR, %g4
	ldxa		[%g4] ASI_DMMU, %g5
	stxa		%g0, [%g4] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		handle_stdfmna
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap
	 nop
	.size		do_stdfmna,.-do_stdfmna

	.type		breakpoint_trap,#function
breakpoint_trap:
	call		sparc_breakpoint
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 nop
	.size		breakpoint_trap,.-breakpoint_trap
