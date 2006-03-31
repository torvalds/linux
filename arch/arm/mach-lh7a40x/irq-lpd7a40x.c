/* arch/arm/mach-lh7a40x/irq-lpd7a40x.c
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *  Copyright (C) 2004 Logic Product Development
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/arch/irqs.h>

#include "common.h"

static void lh7a40x_ack_cpld_irq (u32 irq)
{
	/* CPLD doesn't have ack capability */
}

static void lh7a40x_mask_cpld_irq (u32 irq)
{
	switch (irq) {
	case IRQ_LPD7A40X_ETH_INT:
		CPLD_INTERRUPTS = CPLD_INTERRUPTS | 0x4;
		break;
	case IRQ_LPD7A400_TS:
		CPLD_INTERRUPTS = CPLD_INTERRUPTS | 0x8;
		break;
	}
}

static void lh7a40x_unmask_cpld_irq (u32 irq)
{
	switch (irq) {
	case IRQ_LPD7A40X_ETH_INT:
		CPLD_INTERRUPTS = CPLD_INTERRUPTS & ~ 0x4;
		break;
	case IRQ_LPD7A400_TS:
		CPLD_INTERRUPTS = CPLD_INTERRUPTS & ~ 0x8;
		break;
	}
}

static struct irqchip lh7a40x_cpld_chip = {
	.ack	= lh7a40x_ack_cpld_irq,
	.mask	= lh7a40x_mask_cpld_irq,
	.unmask	= lh7a40x_unmask_cpld_irq,
};

static void lh7a40x_cpld_handler (unsigned int irq, struct irqdesc *desc,
				  struct pt_regs *regs)
{
	unsigned int mask = CPLD_INTERRUPTS;

	desc->chip->ack (irq);

	if ((mask & 0x1) == 0)	/* WLAN */
		IRQ_DISPATCH (IRQ_LPD7A40X_ETH_INT);

	if ((mask & 0x2) == 0)	/* Touch */
		IRQ_DISPATCH (IRQ_LPD7A400_TS);

	desc->chip->unmask (irq); /* Level-triggered need this */
}


  /* IRQ initialization */

void __init lh7a40x_init_board_irq (void)
{
	int irq;

		/* Rev A (v2.8): PF0, PF1, PF2, and PF3 are available IRQs.
		                 PF7 supports the CPLD.
		   Rev B (v3.4): PF0, PF1, and PF2 are available IRQs.
		                 PF3 supports the CPLD.
		   (Some) LPD7A404 prerelease boards report a version
		   number of 0x16, but we force an override since the
		   hardware is of the newer variety.
		*/

	unsigned char cpld_version = CPLD_REVISION;
	int pinCPLD;

#if defined CONFIG_MACH_LPD7A404
	cpld_version = 0x34;	/* Override, for now */
#endif
	pinCPLD = (cpld_version == 0x28) ? 7 : 3;

		/* First, configure user controlled GPIOF interrupts  */

	GPIO_PFDD	&= ~0x0f; /* PF0-3 are inputs */
	GPIO_INTTYPE1	&= ~0x0f; /* PF0-3 are level triggered */
	GPIO_INTTYPE2	&= ~0x0f; /* PF0-3 are active low */
	barrier ();
	GPIO_GPIOFINTEN |=  0x0f; /* Enable PF0, PF1, PF2, and PF3 IRQs */

		/* Then, configure CPLD interrupt */

	CPLD_INTERRUPTS	=   0x0c; /* Disable all CPLD interrupts */
	GPIO_PFDD	&= ~(1 << pinCPLD); /* Make input */
	GPIO_INTTYPE1	|=  (1 << pinCPLD); /* Edge triggered */
	GPIO_INTTYPE2	&= ~(1 << pinCPLD); /* Active low */
	barrier ();
	GPIO_GPIOFINTEN |=  (1 << pinCPLD); /* Enable */

		/* Cascade CPLD interrupts */

	for (irq = IRQ_BOARD_START;
	     irq < IRQ_BOARD_START + NR_IRQ_BOARD; ++irq) {
		set_irq_chip (irq, &lh7a40x_cpld_chip);
		set_irq_handler (irq, do_edge_IRQ);
		set_irq_flags (irq, IRQF_VALID);
	}

	set_irq_chained_handler ((cpld_version == 0x28)
				 ? IRQ_CPLD_V28
				 : IRQ_CPLD_V34,
				 lh7a40x_cpld_handler);
}
