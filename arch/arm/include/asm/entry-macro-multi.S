#include <asm/assembler.h>

/*
 * Interrupt handling.  Preserves r7, r8, r9
 */
	.macro	arch_irq_handler_default
	get_irqnr_preamble r5, lr
1:	get_irqnr_and_base r0, r6, r5, lr
	movne	r1, sp
	@
	@ routine called with r0 = irq number, r1 = struct pt_regs *
	@
	adrne	lr, BSYM(1b)
	bne	asm_do_IRQ

#ifdef CONFIG_SMP
	/*
	 * XXX
	 *
	 * this macro assumes that irqstat (r6) and base (r5) are
	 * preserved from get_irqnr_and_base above
	 */
	ALT_SMP(test_for_ipi r0, r6, r5, lr)
	ALT_UP_B(9997f)
	movne	r1, sp
	adrne	lr, BSYM(1b)
	bne	do_IPI

#ifdef CONFIG_LOCAL_TIMERS
	test_for_ltirq r0, r6, r5, lr
	movne	r0, sp
	adrne	lr, BSYM(1b)
	bne	do_local_timer
#endif
#endif
9997:
	.endm

	.macro	arch_irq_handler, symbol_name
	.align	5
	.global \symbol_name
\symbol_name:
	mov	r4, lr
	arch_irq_handler_default
	mov     pc, r4
	.endm
