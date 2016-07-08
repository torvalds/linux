	/* These get patched into the trap table at boot time
	 * once we know we have a cheetah processor.
	 */
	.globl		cheetah_fecc_trap_vector
	.type		cheetah_fecc_trap_vector,#function
cheetah_fecc_trap_vector:
	membar		#Sync
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1
	andn		%g1, DCU_DC | DCU_IC, %g1
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG
	membar		#Sync
	sethi		%hi(cheetah_fast_ecc), %g2
	jmpl		%g2 + %lo(cheetah_fast_ecc), %g0
	 mov		0, %g1
	.size		cheetah_fecc_trap_vector,.-cheetah_fecc_trap_vector

	.globl		cheetah_fecc_trap_vector_tl1
	.type		cheetah_fecc_trap_vector_tl1,#function
cheetah_fecc_trap_vector_tl1:
	membar		#Sync
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1
	andn		%g1, DCU_DC | DCU_IC, %g1
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG
	membar		#Sync
	sethi		%hi(cheetah_fast_ecc), %g2
	jmpl		%g2 + %lo(cheetah_fast_ecc), %g0
	 mov		1, %g1
	.size		cheetah_fecc_trap_vector_tl1,.-cheetah_fecc_trap_vector_tl1

	.globl	cheetah_cee_trap_vector
	.type	cheetah_cee_trap_vector,#function
cheetah_cee_trap_vector:
	membar		#Sync
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1
	andn		%g1, DCU_IC, %g1
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG
	membar		#Sync
	sethi		%hi(cheetah_cee), %g2
	jmpl		%g2 + %lo(cheetah_cee), %g0
	 mov		0, %g1
	.size		cheetah_cee_trap_vector,.-cheetah_cee_trap_vector

	.globl		cheetah_cee_trap_vector_tl1
	.type		cheetah_cee_trap_vector_tl1,#function
cheetah_cee_trap_vector_tl1:
	membar		#Sync
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1
	andn		%g1, DCU_IC, %g1
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG
	membar		#Sync
	sethi		%hi(cheetah_cee), %g2
	jmpl		%g2 + %lo(cheetah_cee), %g0
	 mov		1, %g1
	.size		cheetah_cee_trap_vector_tl1,.-cheetah_cee_trap_vector_tl1

	.globl	cheetah_deferred_trap_vector
	.type	cheetah_deferred_trap_vector,#function
cheetah_deferred_trap_vector:
	membar		#Sync
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1;
	andn		%g1, DCU_DC | DCU_IC, %g1;
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG;
	membar		#Sync;
	sethi		%hi(cheetah_deferred_trap), %g2
	jmpl		%g2 + %lo(cheetah_deferred_trap), %g0
	 mov		0, %g1
	.size		cheetah_deferred_trap_vector,.-cheetah_deferred_trap_vector

	.globl		cheetah_deferred_trap_vector_tl1
	.type		cheetah_deferred_trap_vector_tl1,#function
cheetah_deferred_trap_vector_tl1:
	membar		#Sync;
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1;
	andn		%g1, DCU_DC | DCU_IC, %g1;
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG;
	membar		#Sync;
	sethi		%hi(cheetah_deferred_trap), %g2
	jmpl		%g2 + %lo(cheetah_deferred_trap), %g0
	 mov		1, %g1
	.size		cheetah_deferred_trap_vector_tl1,.-cheetah_deferred_trap_vector_tl1

	/* Cheetah+ specific traps. These are for the new I/D cache parity
	 * error traps.  The first argument to cheetah_plus_parity_handler
	 * is encoded as follows:
	 *
	 * Bit0:	0=dcache,1=icache
	 * Bit1:	0=recoverable,1=unrecoverable
	 */
	.globl		cheetah_plus_dcpe_trap_vector
	.type		cheetah_plus_dcpe_trap_vector,#function
cheetah_plus_dcpe_trap_vector:
	membar		#Sync
	sethi		%hi(do_cheetah_plus_data_parity), %g7
	jmpl		%g7 + %lo(do_cheetah_plus_data_parity), %g0
	 nop
	nop
	nop
	nop
	nop
	.size		cheetah_plus_dcpe_trap_vector,.-cheetah_plus_dcpe_trap_vector

	.type		do_cheetah_plus_data_parity,#function
do_cheetah_plus_data_parity:
	rdpr		%pil, %g2
	wrpr		%g0, PIL_NORMAL_MAX, %pil
	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7
#ifdef CONFIG_TRACE_IRQFLAGS
	call		trace_hardirqs_off
	 nop
#endif
	mov		0x0, %o0
	call		cheetah_plus_parity_error
	 add		%sp, PTREGS_OFF, %o1
	ba,a,pt		%xcc, rtrap_irq
	.size		do_cheetah_plus_data_parity,.-do_cheetah_plus_data_parity

	.globl		cheetah_plus_dcpe_trap_vector_tl1
	.type		cheetah_plus_dcpe_trap_vector_tl1,#function
cheetah_plus_dcpe_trap_vector_tl1:
	membar		#Sync
	wrpr		PSTATE_IG | PSTATE_PEF | PSTATE_PRIV, %pstate
	sethi		%hi(do_dcpe_tl1), %g3
	jmpl		%g3 + %lo(do_dcpe_tl1), %g0
	 nop
	nop
	nop
	nop
	.size		cheetah_plus_dcpe_trap_vector_tl1,.-cheetah_plus_dcpe_trap_vector_tl1

	.globl		cheetah_plus_icpe_trap_vector
	.type		cheetah_plus_icpe_trap_vector,#function
cheetah_plus_icpe_trap_vector:
	membar		#Sync
	sethi		%hi(do_cheetah_plus_insn_parity), %g7
	jmpl		%g7 + %lo(do_cheetah_plus_insn_parity), %g0
	 nop
	nop
	nop
	nop
	nop
	.size		cheetah_plus_icpe_trap_vector,.-cheetah_plus_icpe_trap_vector

	.type		do_cheetah_plus_insn_parity,#function
do_cheetah_plus_insn_parity:
	rdpr		%pil, %g2
	wrpr		%g0, PIL_NORMAL_MAX, %pil
	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7
#ifdef CONFIG_TRACE_IRQFLAGS
	call		trace_hardirqs_off
	 nop
#endif
	mov		0x1, %o0
	call		cheetah_plus_parity_error
	 add		%sp, PTREGS_OFF, %o1
	ba,a,pt		%xcc, rtrap_irq
	.size		do_cheetah_plus_insn_parity,.-do_cheetah_plus_insn_parity

	.globl		cheetah_plus_icpe_trap_vector_tl1
	.type		cheetah_plus_icpe_trap_vector_tl1,#function
cheetah_plus_icpe_trap_vector_tl1:
	membar		#Sync
	wrpr		PSTATE_IG | PSTATE_PEF | PSTATE_PRIV, %pstate
	sethi		%hi(do_icpe_tl1), %g3
	jmpl		%g3 + %lo(do_icpe_tl1), %g0
	 nop
	nop
	nop
	nop
	.size		cheetah_plus_icpe_trap_vector_tl1,.-cheetah_plus_icpe_trap_vector_tl1

	/* If we take one of these traps when tl >= 1, then we
	 * jump to interrupt globals.  If some trap level above us
	 * was also using interrupt globals, we cannot recover.
	 * We may use all interrupt global registers except %g6.
	 */
	.globl		do_dcpe_tl1
	.type		do_dcpe_tl1,#function
do_dcpe_tl1:
	rdpr		%tl, %g1		! Save original trap level
	mov		1, %g2			! Setup TSTATE checking loop
	sethi		%hi(TSTATE_IG), %g3	! TSTATE mask bit
1:	wrpr		%g2, %tl		! Set trap level to check
	rdpr		%tstate, %g4		! Read TSTATE for this level
	andcc		%g4, %g3, %g0		! Interrupt globals in use?
	bne,a,pn	%xcc, do_dcpe_tl1_fatal	! Yep, irrecoverable
	 wrpr		%g1, %tl		! Restore original trap level
	add		%g2, 1, %g2		! Next trap level
	cmp		%g2, %g1		! Hit them all yet?
	ble,pt		%icc, 1b		! Not yet
	 nop
	wrpr		%g1, %tl		! Restore original trap level
do_dcpe_tl1_nonfatal:	/* Ok we may use interrupt globals safely. */
	sethi		%hi(dcache_parity_tl1_occurred), %g2
	lduw		[%g2 + %lo(dcache_parity_tl1_occurred)], %g1
	add		%g1, 1, %g1
	stw		%g1, [%g2 + %lo(dcache_parity_tl1_occurred)]
	/* Reset D-cache parity */
	sethi		%hi(1 << 16), %g1	! D-cache size
	mov		(1 << 5), %g2		! D-cache line size
	sub		%g1, %g2, %g1		! Move down 1 cacheline
1:	srl		%g1, 14, %g3		! Compute UTAG
	membar		#Sync
	stxa		%g3, [%g1] ASI_DCACHE_UTAG
	membar		#Sync
	sub		%g2, 8, %g3		! 64-bit data word within line
2:	membar		#Sync
	stxa		%g0, [%g1 + %g3] ASI_DCACHE_DATA
	membar		#Sync
	subcc		%g3, 8, %g3		! Next 64-bit data word
	bge,pt		%icc, 2b
	 nop
	subcc		%g1, %g2, %g1		! Next cacheline
	bge,pt		%icc, 1b
	 nop
	ba,a,pt		%xcc, dcpe_icpe_tl1_common

do_dcpe_tl1_fatal:
	sethi		%hi(1f), %g7
	ba,pt		%xcc, etraptl1
1:	or		%g7, %lo(1b), %g7
	mov		0x2, %o0
	call		cheetah_plus_parity_error
	 add		%sp, PTREGS_OFF, %o1
	ba,a,pt		%xcc, rtrap
	.size		do_dcpe_tl1,.-do_dcpe_tl1

	.globl		do_icpe_tl1
	.type		do_icpe_tl1,#function
do_icpe_tl1:
	rdpr		%tl, %g1		! Save original trap level
	mov		1, %g2			! Setup TSTATE checking loop
	sethi		%hi(TSTATE_IG), %g3	! TSTATE mask bit
1:	wrpr		%g2, %tl		! Set trap level to check
	rdpr		%tstate, %g4		! Read TSTATE for this level
	andcc		%g4, %g3, %g0		! Interrupt globals in use?
	bne,a,pn	%xcc, do_icpe_tl1_fatal	! Yep, irrecoverable
	 wrpr		%g1, %tl		! Restore original trap level
	add		%g2, 1, %g2		! Next trap level
	cmp		%g2, %g1		! Hit them all yet?
	ble,pt		%icc, 1b		! Not yet
	 nop
	wrpr		%g1, %tl		! Restore original trap level
do_icpe_tl1_nonfatal:	/* Ok we may use interrupt globals safely. */
	sethi		%hi(icache_parity_tl1_occurred), %g2
	lduw		[%g2 + %lo(icache_parity_tl1_occurred)], %g1
	add		%g1, 1, %g1
	stw		%g1, [%g2 + %lo(icache_parity_tl1_occurred)]
	/* Flush I-cache */
	sethi		%hi(1 << 15), %g1	! I-cache size
	mov		(1 << 5), %g2		! I-cache line size
	sub		%g1, %g2, %g1
1:	or		%g1, (2 << 3), %g3
	stxa		%g0, [%g3] ASI_IC_TAG
	membar		#Sync
	subcc		%g1, %g2, %g1
	bge,pt		%icc, 1b
	 nop
	ba,a,pt		%xcc, dcpe_icpe_tl1_common

do_icpe_tl1_fatal:
	sethi		%hi(1f), %g7
	ba,pt		%xcc, etraptl1
1:	or		%g7, %lo(1b), %g7
	mov		0x3, %o0
	call		cheetah_plus_parity_error
	 add		%sp, PTREGS_OFF, %o1
	ba,a,pt		%xcc, rtrap
	.size		do_icpe_tl1,.-do_icpe_tl1
	
	.type		dcpe_icpe_tl1_common,#function
dcpe_icpe_tl1_common:
	/* Flush D-cache, re-enable D/I caches in DCU and finally
	 * retry the trapping instruction.
	 */
	sethi		%hi(1 << 16), %g1	! D-cache size
	mov		(1 << 5), %g2		! D-cache line size
	sub		%g1, %g2, %g1
1:	stxa		%g0, [%g1] ASI_DCACHE_TAG
	membar		#Sync
	subcc		%g1, %g2, %g1
	bge,pt		%icc, 1b
	 nop
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1
	or		%g1, (DCU_DC | DCU_IC), %g1
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG
	membar		#Sync
	retry
	.size		dcpe_icpe_tl1_common,.-dcpe_icpe_tl1_common

	/* Capture I/D/E-cache state into per-cpu error scoreboard.
	 *
	 * %g1:		(TL>=0) ? 1 : 0
	 * %g2:		scratch
	 * %g3:		scratch
	 * %g4:		AFSR
	 * %g5:		AFAR
	 * %g6:		unused, will have current thread ptr after etrap
	 * %g7:		scratch
	 */
	.type		__cheetah_log_error,#function
__cheetah_log_error:
	/* Put "TL1" software bit into AFSR. */
	and		%g1, 0x1, %g1
	sllx		%g1, 63, %g2
	or		%g4, %g2, %g4

	/* Get log entry pointer for this cpu at this trap level. */
	BRANCH_IF_JALAPENO(g2,g3,50f)
	ldxa		[%g0] ASI_SAFARI_CONFIG, %g2
	srlx		%g2, 17, %g2
	ba,pt		%xcc, 60f
	 and		%g2, 0x3ff, %g2

50:	ldxa		[%g0] ASI_JBUS_CONFIG, %g2
	srlx		%g2, 17, %g2
	and		%g2, 0x1f, %g2

60:	sllx		%g2, 9, %g2
	sethi		%hi(cheetah_error_log), %g3
	ldx		[%g3 + %lo(cheetah_error_log)], %g3
	brz,pn		%g3, 80f
	 nop

	add		%g3, %g2, %g3
	sllx		%g1, 8, %g1
	add		%g3, %g1, %g1

	/* %g1 holds pointer to the top of the logging scoreboard */
	ldx		[%g1 + 0x0], %g7
	cmp		%g7, -1
	bne,pn		%xcc, 80f
	 nop

	stx		%g4, [%g1 + 0x0]
	stx		%g5, [%g1 + 0x8]
	add		%g1, 0x10, %g1

	/* %g1 now points to D-cache logging area */
	set		0x3ff8, %g2	/* DC_addr mask		*/
	and		%g5, %g2, %g2	/* DC_addr bits of AFAR	*/
	srlx		%g5, 12, %g3
	or		%g3, 1, %g3	/* PHYS tag + valid	*/

10:	ldxa		[%g2] ASI_DCACHE_TAG, %g7
	cmp		%g3, %g7	/* TAG match?		*/
	bne,pt		%xcc, 13f
	 nop

	/* Yep, what we want, capture state. */
	stx		%g2, [%g1 + 0x20]
	stx		%g7, [%g1 + 0x28]

	/* A membar Sync is required before and after utag access. */
	membar		#Sync
	ldxa		[%g2] ASI_DCACHE_UTAG, %g7
	membar		#Sync
	stx		%g7, [%g1 + 0x30]
	ldxa		[%g2] ASI_DCACHE_SNOOP_TAG, %g7
	stx		%g7, [%g1 + 0x38]
	clr		%g3

12:	ldxa		[%g2 + %g3] ASI_DCACHE_DATA, %g7
	stx		%g7, [%g1]
	add		%g3, (1 << 5), %g3
	cmp		%g3, (4 << 5)
	bl,pt		%xcc, 12b
	 add		%g1, 0x8, %g1

	ba,pt		%xcc, 20f
	 add		%g1, 0x20, %g1

13:	sethi		%hi(1 << 14), %g7
	add		%g2, %g7, %g2
	srlx		%g2, 14, %g7
	cmp		%g7, 4
	bl,pt		%xcc, 10b
	 nop

	add		%g1, 0x40, %g1

	/* %g1 now points to I-cache logging area */
20:	set		0x1fe0, %g2	/* IC_addr mask		*/
	and		%g5, %g2, %g2	/* IC_addr bits of AFAR	*/
	sllx		%g2, 1, %g2	/* IC_addr[13:6]==VA[12:5] */
	srlx		%g5, (13 - 8), %g3 /* Make PTAG */
	andn		%g3, 0xff, %g3	/* Mask off undefined bits */

21:	ldxa		[%g2] ASI_IC_TAG, %g7
	andn		%g7, 0xff, %g7
	cmp		%g3, %g7
	bne,pt		%xcc, 23f
	 nop

	/* Yep, what we want, capture state. */
	stx		%g2, [%g1 + 0x40]
	stx		%g7, [%g1 + 0x48]
	add		%g2, (1 << 3), %g2
	ldxa		[%g2] ASI_IC_TAG, %g7
	add		%g2, (1 << 3), %g2
	stx		%g7, [%g1 + 0x50]
	ldxa		[%g2] ASI_IC_TAG, %g7
	add		%g2, (1 << 3), %g2
	stx		%g7, [%g1 + 0x60]
	ldxa		[%g2] ASI_IC_TAG, %g7
	stx		%g7, [%g1 + 0x68]
	sub		%g2, (3 << 3), %g2
	ldxa		[%g2] ASI_IC_STAG, %g7
	stx		%g7, [%g1 + 0x58]
	clr		%g3
	srlx		%g2, 2, %g2

22:	ldxa		[%g2 + %g3] ASI_IC_INSTR, %g7
	stx		%g7, [%g1]
	add		%g3, (1 << 3), %g3
	cmp		%g3, (8 << 3)
	bl,pt		%xcc, 22b
	 add		%g1, 0x8, %g1

	ba,pt		%xcc, 30f
	 add		%g1, 0x30, %g1

23:	sethi		%hi(1 << 14), %g7
	add		%g2, %g7, %g2
	srlx		%g2, 14, %g7
	cmp		%g7, 4
	bl,pt		%xcc, 21b
	 nop

	add		%g1, 0x70, %g1

	/* %g1 now points to E-cache logging area */
30:	andn		%g5, (32 - 1), %g2
	stx		%g2, [%g1 + 0x20]
	ldxa		[%g2] ASI_EC_TAG_DATA, %g7
	stx		%g7, [%g1 + 0x28]
	ldxa		[%g2] ASI_EC_R, %g0
	clr		%g3

31:	ldxa		[%g3] ASI_EC_DATA, %g7
	stx		%g7, [%g1 + %g3]
	add		%g3, 0x8, %g3
	cmp		%g3, 0x20

	bl,pt		%xcc, 31b
	 nop
80:
	rdpr		%tt, %g2
	cmp		%g2, 0x70
	be		c_fast_ecc
	 cmp		%g2, 0x63
	be		c_cee
	 nop
	ba,a,pt		%xcc, c_deferred
	.size		__cheetah_log_error,.-__cheetah_log_error

	/* Cheetah FECC trap handling, we get here from tl{0,1}_fecc
	 * in the trap table.  That code has done a memory barrier
	 * and has disabled both the I-cache and D-cache in the DCU
	 * control register.  The I-cache is disabled so that we may
	 * capture the corrupted cache line, and the D-cache is disabled
	 * because corrupt data may have been placed there and we don't
	 * want to reference it.
	 *
	 * %g1 is one if this trap occurred at %tl >= 1.
	 *
	 * Next, we turn off error reporting so that we don't recurse.
	 */
	.globl		cheetah_fast_ecc
	.type		cheetah_fast_ecc,#function
cheetah_fast_ecc:
	ldxa		[%g0] ASI_ESTATE_ERROR_EN, %g2
	andn		%g2, ESTATE_ERROR_NCEEN | ESTATE_ERROR_CEEN, %g2
	stxa		%g2, [%g0] ASI_ESTATE_ERROR_EN
	membar		#Sync

	/* Fetch and clear AFSR/AFAR */
	ldxa		[%g0] ASI_AFSR, %g4
	ldxa		[%g0] ASI_AFAR, %g5
	stxa		%g4, [%g0] ASI_AFSR
	membar		#Sync

	ba,pt		%xcc, __cheetah_log_error
	 nop
	.size		cheetah_fast_ecc,.-cheetah_fast_ecc

	.type		c_fast_ecc,#function
c_fast_ecc:
	rdpr		%pil, %g2
	wrpr		%g0, PIL_NORMAL_MAX, %pil
	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7
#ifdef CONFIG_TRACE_IRQFLAGS
	call		trace_hardirqs_off
	 nop
#endif
	mov		%l4, %o1
	mov		%l5, %o2
	call		cheetah_fecc_handler
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap_irq
	.size		c_fast_ecc,.-c_fast_ecc

	/* Our caller has disabled I-cache and performed membar Sync. */
	.globl		cheetah_cee
	.type		cheetah_cee,#function
cheetah_cee:
	ldxa		[%g0] ASI_ESTATE_ERROR_EN, %g2
	andn		%g2, ESTATE_ERROR_CEEN, %g2
	stxa		%g2, [%g0] ASI_ESTATE_ERROR_EN
	membar		#Sync

	/* Fetch and clear AFSR/AFAR */
	ldxa		[%g0] ASI_AFSR, %g4
	ldxa		[%g0] ASI_AFAR, %g5
	stxa		%g4, [%g0] ASI_AFSR
	membar		#Sync

	ba,pt		%xcc, __cheetah_log_error
	 nop
	.size		cheetah_cee,.-cheetah_cee

	.type		c_cee,#function
c_cee:
	rdpr		%pil, %g2
	wrpr		%g0, PIL_NORMAL_MAX, %pil
	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7
#ifdef CONFIG_TRACE_IRQFLAGS
	call		trace_hardirqs_off
	 nop
#endif
	mov		%l4, %o1
	mov		%l5, %o2
	call		cheetah_cee_handler
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap_irq
	.size		c_cee,.-c_cee

	/* Our caller has disabled I-cache+D-cache and performed membar Sync. */
	.globl		cheetah_deferred_trap
	.type		cheetah_deferred_trap,#function
cheetah_deferred_trap:
	ldxa		[%g0] ASI_ESTATE_ERROR_EN, %g2
	andn		%g2, ESTATE_ERROR_NCEEN | ESTATE_ERROR_CEEN, %g2
	stxa		%g2, [%g0] ASI_ESTATE_ERROR_EN
	membar		#Sync

	/* Fetch and clear AFSR/AFAR */
	ldxa		[%g0] ASI_AFSR, %g4
	ldxa		[%g0] ASI_AFAR, %g5
	stxa		%g4, [%g0] ASI_AFSR
	membar		#Sync

	ba,pt		%xcc, __cheetah_log_error
	 nop
	.size		cheetah_deferred_trap,.-cheetah_deferred_trap

	.type		c_deferred,#function
c_deferred:
	rdpr		%pil, %g2
	wrpr		%g0, PIL_NORMAL_MAX, %pil
	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7
#ifdef CONFIG_TRACE_IRQFLAGS
	call		trace_hardirqs_off
	 nop
#endif
	mov		%l4, %o1
	mov		%l5, %o2
	call		cheetah_deferred_handler
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap_irq
	.size		c_deferred,.-c_deferred
