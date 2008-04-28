	/* We need to carefully read the error status, ACK the errors,
	 * prevent recursive traps, and pass the information on to C
	 * code for logging.
	 *
	 * We pass the AFAR in as-is, and we encode the status
	 * information as described in asm-sparc64/sfafsr.h
	 */
	.type		__spitfire_access_error,#function
__spitfire_access_error:
	/* Disable ESTATE error reporting so that we do not take
	 * recursive traps and RED state the processor.
	 */
	stxa		%g0, [%g0] ASI_ESTATE_ERROR_EN
	membar		#Sync

	mov		UDBE_UE, %g1
	ldxa		[%g0] ASI_AFSR, %g4	! Get AFSR

	/* __spitfire_cee_trap branches here with AFSR in %g4 and
	 * UDBE_CE in %g1.  It only clears ESTATE_ERR_CE in the ESTATE
	 * Error Enable register.
	 */
__spitfire_cee_trap_continue:
	ldxa		[%g0] ASI_AFAR, %g5	! Get AFAR

	rdpr		%tt, %g3
	and		%g3, 0x1ff, %g3		! Paranoia
	sllx		%g3, SFSTAT_TRAP_TYPE_SHIFT, %g3
	or		%g4, %g3, %g4
	rdpr		%tl, %g3
	cmp		%g3, 1
	mov		1, %g3
	bleu		%xcc, 1f
	 sllx		%g3, SFSTAT_TL_GT_ONE_SHIFT, %g3

	or		%g4, %g3, %g4

	/* Read in the UDB error register state, clearing the sticky
	 * error bits as-needed.  We only clear them if the UE bit is
	 * set.  Likewise, __spitfire_cee_trap below will only do so
	 * if the CE bit is set.
	 *
	 * NOTE: UltraSparc-I/II have high and low UDB error
	 *       registers, corresponding to the two UDB units
	 *       present on those chips.  UltraSparc-IIi only
	 *       has a single UDB, called "SDB" in the manual.
	 *       For IIi the upper UDB register always reads
	 *       as zero so for our purposes things will just
	 *       work with the checks below.
	 */
1:	ldxa		[%g0] ASI_UDBH_ERROR_R, %g3
	and		%g3, 0x3ff, %g7		! Paranoia
	sllx		%g7, SFSTAT_UDBH_SHIFT, %g7
	or		%g4, %g7, %g4
	andcc		%g3, %g1, %g3		! UDBE_UE or UDBE_CE
	be,pn		%xcc, 1f
	 nop
	stxa		%g3, [%g0] ASI_UDB_ERROR_W
	membar		#Sync

1:	mov		0x18, %g3
	ldxa		[%g3] ASI_UDBL_ERROR_R, %g3
	and		%g3, 0x3ff, %g7		! Paranoia
	sllx		%g7, SFSTAT_UDBL_SHIFT, %g7
	or		%g4, %g7, %g4
	andcc		%g3, %g1, %g3		! UDBE_UE or UDBE_CE
	be,pn		%xcc, 1f
	 nop
	mov		0x18, %g7
	stxa		%g3, [%g7] ASI_UDB_ERROR_W
	membar		#Sync

1:	/* Ok, now that we've latched the error state, clear the
	 * sticky bits in the AFSR.
	 */
	stxa		%g4, [%g0] ASI_AFSR
	membar		#Sync

	rdpr		%tl, %g2
	cmp		%g2, 1
	rdpr		%pil, %g2
	bleu,pt		%xcc, 1f
	 wrpr		%g0, 15, %pil

	ba,pt		%xcc, etraptl1
	 rd		%pc, %g7

	ba,pt		%xcc, 2f
	 nop

1:	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7

2:
#ifdef CONFIG_TRACE_IRQFLAGS
	call	trace_hardirqs_off
	 nop
#endif
	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_access_error
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 nop
	.size		__spitfire_access_error,.-__spitfire_access_error

	/* This is the trap handler entry point for ECC correctable
	 * errors.  They are corrected, but we listen for the trap so
	 * that the event can be logged.
	 *
	 * Disrupting errors are either:
	 * 1) single-bit ECC errors during UDB reads to system
	 *    memory
	 * 2) data parity errors during write-back events
	 *
	 * As far as I can make out from the manual, the CEE trap is
	 * only for correctable errors during memory read accesses by
	 * the front-end of the processor.
	 *
	 * The code below is only for trap level 1 CEE events, as it
	 * is the only situation where we can safely record and log.
	 * For trap level >1 we just clear the CE bit in the AFSR and
	 * return.
	 *
	 * This is just like __spiftire_access_error above, but it
	 * specifically handles correctable errors.  If an
	 * uncorrectable error is indicated in the AFSR we will branch
	 * directly above to __spitfire_access_error to handle it
	 * instead.  Uncorrectable therefore takes priority over
	 * correctable, and the error logging C code will notice this
	 * case by inspecting the trap type.
	 */
	.type		__spitfire_cee_trap,#function
__spitfire_cee_trap:
	ldxa		[%g0] ASI_AFSR, %g4	! Get AFSR
	mov		1, %g3
	sllx		%g3, SFAFSR_UE_SHIFT, %g3
	andcc		%g4, %g3, %g0		! Check for UE
	bne,pn		%xcc, __spitfire_access_error
	 nop

	/* Ok, in this case we only have a correctable error.
	 * Indicate we only wish to capture that state in register
	 * %g1, and we only disable CE error reporting unlike UE
	 * handling which disables all errors.
	 */
	ldxa		[%g0] ASI_ESTATE_ERROR_EN, %g3
	andn		%g3, ESTATE_ERR_CE, %g3
	stxa		%g3, [%g0] ASI_ESTATE_ERROR_EN
	membar		#Sync

	/* Preserve AFSR in %g4, indicate UDB state to capture in %g1 */
	ba,pt		%xcc, __spitfire_cee_trap_continue
	 mov		UDBE_CE, %g1
	.size		__spitfire_cee_trap,.-__spitfire_cee_trap

	.type		__spitfire_data_access_exception_tl1,#function
__spitfire_data_access_exception_tl1:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_DMMU	! Clear SFSR.FaultValid bit
	membar		#Sync
	rdpr		%tt, %g3
	cmp		%g3, 0x80		! first win spill/fill trap
	blu,pn		%xcc, 1f
	 cmp		%g3, 0xff		! last win spill/fill trap
	bgu,pn		%xcc, 1f
	 nop
	ba,pt		%xcc, winfix_dax
	 rdpr		%tpc, %g3
1:	sethi		%hi(109f), %g7
	ba,pt		%xcc, etraptl1
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_data_access_exception_tl1
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 nop
	.size		__spitfire_data_access_exception_tl1,.-__spitfire_data_access_exception_tl1

	.type		__spitfire_data_access_exception,#function
__spitfire_data_access_exception:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_DMMU	! Clear SFSR.FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_data_access_exception
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 nop
	.size		__spitfire_data_access_exception,.-__spitfire_data_access_exception

	.type		__spitfire_insn_access_exception_tl1,#function
__spitfire_insn_access_exception_tl1:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	ldxa		[%g3] ASI_IMMU, %g4	! Get SFSR
	rdpr		%tpc, %g5		! IMMU has no SFAR, use TPC
	stxa		%g0, [%g3] ASI_IMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etraptl1
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_insn_access_exception_tl1
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 nop
	.size		__spitfire_insn_access_exception_tl1,.-__spitfire_insn_access_exception_tl1

	.type		__spitfire_insn_access_exception,#function
__spitfire_insn_access_exception:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	ldxa		[%g3] ASI_IMMU, %g4	! Get SFSR
	rdpr		%tpc, %g5		! IMMU has no SFAR, use TPC
	stxa		%g0, [%g3] ASI_IMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_insn_access_exception
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 nop
	.size		__spitfire_insn_access_exception,.-__spitfire_insn_access_exception
