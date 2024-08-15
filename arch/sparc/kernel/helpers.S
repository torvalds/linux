/* SPDX-License-Identifier: GPL-2.0-only */
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
EXPORT_SYMBOL(__flushw_user)

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
EXPORT_SYMBOL_GPL(real_hard_smp_processor_id)
