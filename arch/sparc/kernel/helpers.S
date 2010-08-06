	.align	32
	.globl	__flushw_user
	.type	__flushw_user,#function
__flushw_user:
	rdpr	%otherwin, %g1
	brz,pn	%g1, 2f
	 clr	%g2
1:	save	%sp, -128, %sp
	rdpr	%otherwin, %g1
	brnz,pt	%g1, 1b
	 add	%g2, 1, %g2
1:	sub	%g2, 1, %g2
	brnz,pt	%g2, 1b
	 restore %g0, %g0, %g0
2:	retl
	 nop
	.size	__flushw_user,.-__flushw_user

	/* Flush %fp and %i7 to the stack for all register
	 * windows active inside of the cpu.  This allows
	 * show_stack_trace() to avoid using an expensive
	 * 'flushw'.
	 */
	.globl		stack_trace_flush
	.type		stack_trace_flush,#function
stack_trace_flush:
	rdpr		%pstate, %o0
	wrpr		%o0, PSTATE_IE, %pstate

	rdpr		%cwp, %g1
	rdpr		%canrestore, %g2
	sub		%g1, 1, %g3

1:	brz,pn		%g2, 2f
	 sub		%g2, 1, %g2
	wrpr		%g3, %cwp
	stx		%fp, [%sp + STACK_BIAS + RW_V9_I6]
	stx		%i7, [%sp + STACK_BIAS + RW_V9_I7]
	ba,pt		%xcc, 1b
	 sub		%g3, 1, %g3

2:	wrpr		%g1, %cwp
	wrpr		%o0, %pstate

	retl
	 nop
	.size		stack_trace_flush,.-stack_trace_flush

#ifdef CONFIG_PERF_EVENTS
	.globl		perf_arch_fetch_caller_regs
	.type		perf_arch_fetch_caller_regs,#function
perf_arch_fetch_caller_regs:
	/* We always read the %pstate into %o5 since we will use
	 * that to construct a fake %tstate to store into the regs.
	 */
	rdpr		%pstate, %o5
	brz,pn		%o2, 50f
	 mov		%o2, %g7

	/* Turn off interrupts while we walk around the register
	 * window by hand.
	 */
	wrpr		%o5, PSTATE_IE, %pstate

	/* The %canrestore tells us how many register windows are
	 * still live in the chip above us, past that we have to
	 * walk the frame as saved on the stack.   We stash away
	 * the %cwp in %g1 so we can return back to the original
	 * register window.
	 */
	rdpr		%cwp, %g1
	rdpr		%canrestore, %g2
	sub		%g1, 1, %g3

	/* We have the skip count in %g7, if it hits zero then
	 * %fp/%i7 are the registers we need.  Otherwise if our
	 * %canrestore count maintained in %g2 hits zero we have
	 * to start traversing the stack.
	 */
10:	brz,pn		%g2, 4f
	 sub		%g2, 1, %g2
	wrpr		%g3, %cwp
	subcc		%g7, 1, %g7
	bne,pt		%xcc, 10b
	 sub		%g3, 1, %g3

	/* We found the values we need in the cpu's register
	 * windows.
	 */
	mov		%fp, %g3
	ba,pt		%xcc, 3f
	 mov		%i7, %g2

50:	mov		%fp, %g3
	ba,pt		%xcc, 2f
	 mov		%i7, %g2

	/* We hit the end of the valid register windows in the
	 * cpu, start traversing the stack frame.
	 */
4:	mov		%fp, %g3

20:	ldx		[%g3 + STACK_BIAS + RW_V9_I7], %g2
	subcc		%g7, 1, %g7
	bne,pn		%xcc, 20b
	 ldx		[%g3 + STACK_BIAS + RW_V9_I6], %g3

	/* Restore the current register window position and
	 * re-enable interrupts.
	 */
3:	wrpr		%g1, %cwp
	wrpr		%o5, %pstate

2:	stx		%g3, [%o0 + PT_V9_FP]
	sllx		%o5, 8, %o5
	stx		%o5, [%o0 + PT_V9_TSTATE]
	stx		%g2, [%o0 + PT_V9_TPC]
	add		%g2, 4, %g2
	retl
	 stx		%g2, [%o0 + PT_V9_TNPC]
	.size		perf_arch_fetch_caller_regs,.-perf_arch_fetch_caller_regs
#endif /* CONFIG_PERF_EVENTS */

#ifdef CONFIG_SMP
	.globl		hard_smp_processor_id
	.type		hard_smp_processor_id,#function
hard_smp_processor_id:
#endif
	.globl		real_hard_smp_processor_id
	.type		real_hard_smp_processor_id,#function
real_hard_smp_processor_id:
	__GET_CPUID(%o0)
	retl
	 nop
#ifdef CONFIG_SMP
	.size		hard_smp_processor_id,.-hard_smp_processor_id
#endif
	.size		real_hard_smp_processor_id,.-real_hard_smp_processor_id
