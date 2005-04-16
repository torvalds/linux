/*
 * arch/v850/kernel/teg.c -- NB85E-TEG cpu chip
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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bootmem.h>
#include <linux/irq.h>

#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/machdep.h>
#include <asm/v850e_timer_d.h>

#include "mach.h"

void __init mach_sched_init (struct irqaction *timer_action)
{
	/* Select timer interrupt instead of external pin.  */
	TEG_ISS |= 0x1;
	/* Start hardware timer.  */
	v850e_timer_d_configure (0, HZ);
	/* Install timer interrupt handler.  */
	setup_irq (IRQ_INTCMD(0), timer_action);
}

static struct v850e_intc_irq_init irq_inits[] = {
	{ "IRQ", 0,		NUM_CPU_IRQS,	1, 7 },
	{ "CMD", IRQ_INTCMD(0),	IRQ_INTCMD_NUM,	1, 5 },
	{ "SER", IRQ_INTSER(0),	IRQ_INTSER_NUM,	1, 3 },
	{ "SR",	 IRQ_INTSR(0),	IRQ_INTSR_NUM,	1, 4 },
	{ "ST",	 IRQ_INTST(0),	IRQ_INTST_NUM,	1, 5 },
	{ 0 }
};
#define NUM_IRQ_INITS ((sizeof irq_inits / sizeof irq_inits[0]) - 1)

static struct hw_interrupt_type hw_itypes[NUM_IRQ_INITS];

/* Initialize MA chip interrupts.  */
void __init teg_init_irqs (void)
{
	v850e_intc_init_irq_types (irq_inits, hw_itypes);
}

/* Called before configuring an on-chip UART.  */
void teg_uart_pre_configure (unsigned chan, unsigned cflags, unsigned baud)
{
	/* Enable UART I/O pins instead of external interrupt pins, and
	   UART interrupts instead of external pin interrupts.  */
	TEG_ISS |= 0x4E;
}
