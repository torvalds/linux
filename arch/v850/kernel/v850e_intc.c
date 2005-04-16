/*
 * arch/v850/kernel/v850e_intc.c -- V850E interrupt controller (INTC)
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/v850e_intc.h>

static void irq_nop (unsigned irq) { }

static unsigned v850e_intc_irq_startup (unsigned irq)
{
	v850e_intc_clear_pending_irq (irq);
	v850e_intc_enable_irq (irq);
	return 0;
}

static void v850e_intc_end_irq (unsigned irq)
{
	unsigned long psw, temp;

	/* Clear the highest-level bit in the In-service priority register
	   (ISPR), to allow this interrupt (or another of the same or
	   lesser priority) to happen again.

	   The `reti' instruction normally does this automatically when the
	   PSW bits EP and NP are zero, but we can't always rely on reti
	   being used consistently to return after an interrupt (another
	   process can be scheduled, for instance, which can delay the
	   associated reti for a long time, or this process may be being
	   single-stepped, which uses the `dbret' instruction to return
	   from the kernel).

	   We also set the PSW EP bit, which prevents reti from also
	   trying to modify the ISPR itself.  */

	/* Get PSW and disable interrupts.  */
	asm volatile ("stsr psw, %0; di" : "=r" (psw));
	/* We don't want to do anything for NMIs (they don't use the ISPR).  */
	if (! (psw & 0xC0)) {
		/* Transition to `trap' state, so that an eventual real
		   reti instruction won't modify the ISPR.  */
		psw |= 0x40;
		/* Fake an interrupt return, which automatically clears the
		   appropriate bit in the ISPR.  */
		asm volatile ("mov hilo(1f), %0;"
			      "ldsr %0, eipc; ldsr %1, eipsw;"
			      "reti;"
			      "1:"
			      : "=&r" (temp) : "r" (psw));
	}
}

/* Initialize HW_IRQ_TYPES for INTC-controlled irqs described in array
   INITS (which is terminated by an entry with the name field == 0).  */
void __init v850e_intc_init_irq_types (struct v850e_intc_irq_init *inits,
				       struct hw_interrupt_type *hw_irq_types)
{
	struct v850e_intc_irq_init *init;
	for (init = inits; init->name; init++) {
		unsigned i;
		struct hw_interrupt_type *hwit = hw_irq_types++;

		hwit->typename = init->name;

		hwit->startup  = v850e_intc_irq_startup;
		hwit->shutdown = v850e_intc_disable_irq;
		hwit->enable   = v850e_intc_enable_irq;
		hwit->disable  = v850e_intc_disable_irq;
		hwit->ack      = irq_nop;
		hwit->end      = v850e_intc_end_irq;
		
		/* Initialize kernel IRQ infrastructure for this interrupt.  */
		init_irq_handlers(init->base, init->num, init->interval, hwit);

		/* Set the interrupt priorities.  */
		for (i = 0; i < init->num; i++) {
			unsigned irq = init->base + i * init->interval;

			/* If the interrupt is currently enabled (all
			   interrupts are initially disabled), then
			   assume whoever enabled it has set things up
			   properly, and avoid messing with it.  */
			if (! v850e_intc_irq_enabled (irq))
				/* This write also (1) disables the
				   interrupt, and (2) clears any pending
				   interrupts.  */
				V850E_INTC_IC (irq)
					= (V850E_INTC_IC_PR (init->priority)
					   | V850E_INTC_IC_MK);
		}
	}
}
