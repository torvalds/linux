/*
 * arch/v850/kernel/me2.c -- V850E/ME2 chip-specific support
 *
 *  Copyright (C) 2003  NEC Corporation
 *  Copyright (C) 2003  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

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
	/* Start hardware timer.  */
	v850e_timer_d_configure (0, HZ);
	/* Install timer interrupt handler.  */
	setup_irq (IRQ_INTCMD(0), timer_action);
}

static struct v850e_intc_irq_init irq_inits[] = {
	{ "IRQ",    0,                NUM_CPU_IRQS,      1, 7 },
	{ "INTP",   IRQ_INTP(0),      IRQ_INTP_NUM,      1, 5 },
	{ "CMD",    IRQ_INTCMD(0),    IRQ_INTCMD_NUM,    1, 3 },
	{ "UBTIRE", IRQ_INTUBTIRE(0), IRQ_INTUBTIRE_NUM, 5, 4 },
	{ "UBTIR",  IRQ_INTUBTIR(0),  IRQ_INTUBTIR_NUM,  5, 4 },
	{ "UBTIT",  IRQ_INTUBTIT(0),  IRQ_INTUBTIT_NUM,  5, 4 },
	{ "UBTIF",  IRQ_INTUBTIF(0),  IRQ_INTUBTIF_NUM,  5, 4 },
	{ "UBTITO", IRQ_INTUBTITO(0), IRQ_INTUBTITO_NUM, 5, 4 },
	{ 0 }
};
#define NUM_IRQ_INITS (ARRAY_SIZE(irq_inits) - 1)

static struct hw_interrupt_type hw_itypes[NUM_IRQ_INITS];

/* Initialize V850E/ME2 chip interrupts.  */
void __init me2_init_irqs (void)
{
	v850e_intc_init_irq_types (irq_inits, hw_itypes);
}

/* Called before configuring an on-chip UART.  */
void me2_uart_pre_configure (unsigned chan, unsigned cflags, unsigned baud)
{
	if (chan == 0) {
		/* Specify that the relevent pins on the chip should do
		   serial I/O, not direct I/O.  */
		ME2_PORT1_PMC |= 0xC;
		/* Specify that we're using the UART, not the CSI device. */
		ME2_PORT1_PFC |= 0xC;
	} else if (chan == 1) {
		/* Specify that the relevent pins on the chip should do
		   serial I/O, not direct I/O.  */
		ME2_PORT2_PMC |= 0x6;
		/* Specify that we're using the UART, not the CSI device. */
		ME2_PORT2_PFC |= 0x6;
	}
}
