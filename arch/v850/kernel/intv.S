/*
 * arch/v850/kernel/intv.S -- Interrupt vectors
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <asm/clinkage.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/entry.h>

#ifdef CONFIG_V850E_HIGHRES_TIMER
#include <asm/highres_timer.h>
#endif

/* Jump to an interrupt/trap handler.  These handlers (defined in entry.S)
   expect the stack-pointer to be saved in ENTRY_SP, so we use sp to do an
   indirect jump (which avoids problems when the handler is more than a signed
   22-bit offset away).  */
#define JUMP_TO_HANDLER(name, sp_save_loc)				      \
	st.w	sp, sp_save_loc;					      \
	mov	hilo(name), sp;						      \
	jmp	[sp]


	/* Reset vector.  */
	.section	.intv.reset, "ax"
	.org	0x0
	mov	hilo(C_SYMBOL_NAME(start)), r1;
	jmp	[r1]


	/* Generic interrupt vectors.  */
	.section	.intv.common, "ax"
	.balign	0x10
	JUMP_TO_HANDLER (nmi, NMI_ENTRY_SP)	// 0x10 - NMI0
	.balign	0x10
	JUMP_TO_HANDLER (nmi, NMI_ENTRY_SP)	// 0x20 - NMI1
	.balign	0x10
	JUMP_TO_HANDLER (nmi, NMI_ENTRY_SP)	// 0x30 - NMI2
	
	.balign	0x10
	JUMP_TO_HANDLER (trap, ENTRY_SP)	// 0x40 - TRAP0n
	.balign	0x10
	JUMP_TO_HANDLER (trap, ENTRY_SP)	// 0x50 - TRAP1n

	.balign	0x10
	JUMP_TO_HANDLER (dbtrap, ENTRY_SP)	// 0x60 - Illegal op / DBTRAP insn


	/* Hardware interrupt vectors.  */
	.section	.intv.mach, "ax"
	.org	0x0

#if defined (CONFIG_V850E_HIGHRES_TIMER) && defined (IRQ_INTCMD)

	/* Interrupts before the highres timer interrupt.  */
	.rept	IRQ_INTCMD (HIGHRES_TIMER_TIMER_D_UNIT)
	.balign	0x10
	JUMP_TO_HANDLER (irq, ENTRY_SP)
	.endr

	/* The highres timer interrupt.  */
	.balign	0x10
	JUMP_TO_HANDLER (C_SYMBOL_NAME (highres_timer_slow_tick_irq), ENTRY_SP)

	/* Interrupts after the highres timer interrupt.  */
	.rept	NUM_CPU_IRQS - IRQ_INTCMD (HIGHRES_TIMER_TIMER_D_UNIT) - 1
	.balign	0x10
	JUMP_TO_HANDLER (irq, ENTRY_SP)
	.endr

#else /* No highres timer */

	.rept	NUM_CPU_IRQS
	.balign	0x10
	JUMP_TO_HANDLER (irq, ENTRY_SP)
	.endr

#endif /* Highres timer */
