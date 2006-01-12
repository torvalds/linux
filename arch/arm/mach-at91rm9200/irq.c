/*
 * linux/arch/arm/mach-at91rm9200/irq.c
 *
 *  Copyright (C) 2004 SAN People
 *  Copyright (C) 2004 ATMEL
 *  Copyright (C) Rick Bronson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include "generic.h"

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int at91rm9200_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,	/* Advanced Interrupt Controller */
	7,	/* System Peripheral */
	0,	/* Parallel IO Controller A */
	0,	/* Parallel IO Controller B */
	0,	/* Parallel IO Controller C */
	0,	/* Parallel IO Controller D */
	6,	/* USART 0 */
	6,	/* USART 1 */
	6,	/* USART 2 */
	6,	/* USART 3 */
	0,	/* Multimedia Card Interface */
	4,	/* USB Device Port */
	0,	/* Two-Wire Interface */
	6,	/* Serial Peripheral Interface */
	5,	/* Serial Synchronous Controller */
	5,	/* Serial Synchronous Controller */
	5,	/* Serial Synchronous Controller */
	0,	/* Timer Counter 0 */
	0,	/* Timer Counter 1 */
	0,	/* Timer Counter 2 */
	0,	/* Timer Counter 3 */
	0,	/* Timer Counter 4 */
	0,	/* Timer Counter 5 */
	3,	/* USB Host port */
	3,	/* Ethernet MAC */
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
	0,	/* Advanced Interrupt Controller */
	0	/* Advanced Interrupt Controller */
};


static void at91rm9200_mask_irq(unsigned int irq)
{
	/* Disable interrupt on AIC */
	at91_sys_write(AT91_AIC_IDCR, 1 << irq);
}

static void at91rm9200_unmask_irq(unsigned int irq)
{
	/* Enable interrupt on AIC */
	at91_sys_write(AT91_AIC_IECR, 1 << irq);
}

static int at91rm9200_irq_type(unsigned irq, unsigned type)
{
	unsigned int smr, srctype;

	/* change triggering only for FIQ and external IRQ0..IRQ6 */
	if ((irq < AT91_ID_IRQ0) && (irq != AT91_ID_FIQ))
		return -EINVAL;

	switch (type) {
	case IRQT_HIGH:
		srctype = AT91_AIC_SRCTYPE_HIGH;
		break;
	case IRQT_RISING:
		srctype = AT91_AIC_SRCTYPE_RISING;
		break;
	case IRQT_LOW:
		srctype = AT91_AIC_SRCTYPE_LOW;
		break;
	case IRQT_FALLING:
		srctype = AT91_AIC_SRCTYPE_FALLING;
		break;
	default:
		return -EINVAL;
	}

	smr = at91_sys_read(AT91_AIC_SMR(irq)) & ~AT91_AIC_SRCTYPE;
	at91_sys_write(AT91_AIC_SMR(irq), smr | srctype);
	return 0;
}

static struct irqchip at91rm9200_irq_chip = {
	.ack		= at91rm9200_mask_irq,
	.mask		= at91rm9200_mask_irq,
	.unmask		= at91rm9200_unmask_irq,
	.set_type	= at91rm9200_irq_type,
};

/*
 * Initialize the AIC interrupt controller.
 */
void __init at91rm9200_init_irq(unsigned int priority[NR_AIC_IRQS])
{
	unsigned int i;

	/* No priority list specified for this board -> use defaults */
	if (priority == NULL)
		priority = at91rm9200_default_irq_priority;

	/*
	 * The IVR is used by macro get_irqnr_and_base to read and verify.
	 * The irq number is NR_AIC_IRQS when a spurious interrupt has occurred.
	 */
	for (i = 0; i < NR_AIC_IRQS; i++) {
		/* Put irq number in Source Vector Register: */
		at91_sys_write(AT91_AIC_SVR(i), i);
		/* Store the Source Mode Register as defined in table above */
		at91_sys_write(AT91_AIC_SMR(i), AT91_AIC_SRCTYPE_LOW | priority[i]);

		set_irq_chip(i, &at91rm9200_irq_chip);
		set_irq_handler(i, do_level_IRQ);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);

		/* Perform 8 End Of Interrupt Command to make sure AIC will not Lock out nIRQ */
		if (i < 8)
			at91_sys_write(AT91_AIC_EOICR, 0);
	}

	/*
	 * Spurious Interrupt ID in Spurious Vector Register is NR_AIC_IRQS
	 * When there is no current interrupt, the IRQ Vector Register reads the value stored in AIC_SPU
	 */
	at91_sys_write(AT91_AIC_SPU, NR_AIC_IRQS);

	/* No debugging in AIC: Debug (Protect) Control Register */
	at91_sys_write(AT91_AIC_DCR, 0);

	/* Disable and clear all interrupts initially */
	at91_sys_write(AT91_AIC_IDCR, 0xFFFFFFFF);
	at91_sys_write(AT91_AIC_ICCR, 0xFFFFFFFF);
}
