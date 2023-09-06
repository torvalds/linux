/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/assembler.h>

/*
 * Interrupt handling.  Preserves r7, r8, r9
 */
	.macro	arch_irq_handler_default
	get_irqnr_preamble r6, lr
1:	get_irqnr_and_base r0, r2, r6, lr
	movne	r1, sp
	@
	@ routine called with r0 = irq number, r1 = struct pt_regs *
	@
	badrne	lr, 1b
	bne	asm_do_IRQ
	.endm
