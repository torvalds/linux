/* arch/arm/mach-lh7a40x/irq-kev7a400.c
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/mach/hardware.h>
#include <asm/mach/irqs.h>

#include "common.h"

  /* KEV7a400 CPLD IRQ handling */

static u16 CPLD_IRQ_mask;	/* Mask for CPLD IRQs, 1 == unmasked */

static void
lh7a400_ack_cpld_irq (u32 irq)
{
	CPLD_CL_INT = 1 << (irq - IRQ_KEV7A400_CPLD);
}

static void
lh7a400_mask_cpld_irq (u32 irq)
{
	CPLD_IRQ_mask &= ~(1 << (irq - IRQ_KEV7A400_CPLD));
	CPLD_WR_PB_INT_MASK = CPLD_IRQ_mask;
}

static void
lh7a400_unmask_cpld_irq (u32 irq)
{
	CPLD_IRQ_mask |= 1 << (irq - IRQ_KEV7A400_CPLD);
	CPLD_WR_PB_INT_MASK = CPLD_IRQ_mask;
}

static struct
irqchip lh7a400_cpld_chip = {
	.ack	= lh7a400_ack_cpld_irq,
	.mask	= lh7a400_mask_cpld_irq,
	.unmask	= lh7a400_unmask_cpld_irq,
};

static void
lh7a400_cpld_handler (unsigned int irq, struct irqdesc *desc,
		      struct pt_regs *regs)
{
	u32 mask = CPLD_LATCHED_INTS;
	irq = IRQ_KEV_7A400_CPLD;
	for (; mask; mask >>= 1, ++irq) {
		if (mask & 1)
			desc[irq].handle (irq, desc, regs);
	}
}

  /* IRQ initialization */

void __init
lh7a400_init_board_irq (void)
{
	int irq;

	for (irq = IRQ_KEV7A400_CPLD;
	     irq < IRQ_KEV7A400_CPLD + NR_IRQ_KEV7A400_CPLD; ++irq) {
		set_irq_chip (irq, &lh7a400_cpld_chip);
		set_irq_handler (irq, do_edge_IRQ);
		set_irq_flags (irq, IRQF_VALID);
	}
	set_irq_chained_handler (IRQ_CPLD, kev7a400_cpld_handler);

		/* Clear all CPLD interrupts */
	CPLD_CL_INT = 0xff; /* CPLD_INTR_MMC_CD | CPLD_INTR_ETH_INT; */

    /* *** FIXME CF enabled in ide-probe.c */

	GPIO_GPIOINTEN = 0;		/* Disable all GPIO interrupts */
	barrier();
	GPIO_INTTYPE1
		= (GPIO_INTR_PCC1_CD | GPIO_INTR_PCC1_CD); /* Edge trig. */
	GPIO_INTTYPE2 = 0;		/* Falling edge & low-level */
	GPIO_GPIOFEOI = 0xff;		/* Clear all GPIO interrupts */
	GPIO_GPIOINTEN = 0xff;		/* Enable all GPIO interrupts */

	init_FIQ();
}
