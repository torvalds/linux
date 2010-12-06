/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <mach/common.h>
#include <asm/mach/irq.h>
#include <mach/hardware.h>

#include "irq-common.h"

#define AVIC_INTCNTL		0x00	/* int control reg */
#define AVIC_NIMASK		0x04	/* int mask reg */
#define AVIC_INTENNUM		0x08	/* int enable number reg */
#define AVIC_INTDISNUM		0x0C	/* int disable number reg */
#define AVIC_INTENABLEH		0x10	/* int enable reg high */
#define AVIC_INTENABLEL		0x14	/* int enable reg low */
#define AVIC_INTTYPEH		0x18	/* int type reg high */
#define AVIC_INTTYPEL		0x1C	/* int type reg low */
#define AVIC_NIPRIORITY(x)	(0x20 + 4 * (7 - (x))) /* int priority */
#define AVIC_NIVECSR		0x40	/* norm int vector/status */
#define AVIC_FIVECSR		0x44	/* fast int vector/status */
#define AVIC_INTSRCH		0x48	/* int source reg high */
#define AVIC_INTSRCL		0x4C	/* int source reg low */
#define AVIC_INTFRCH		0x50	/* int force reg high */
#define AVIC_INTFRCL		0x54	/* int force reg low */
#define AVIC_NIPNDH		0x58	/* norm int pending high */
#define AVIC_NIPNDL		0x5C	/* norm int pending low */
#define AVIC_FIPNDH		0x60	/* fast int pending high */
#define AVIC_FIPNDL		0x64	/* fast int pending low */

void __iomem *avic_base;

#ifdef CONFIG_MXC_IRQ_PRIOR
static int avic_irq_set_priority(unsigned char irq, unsigned char prio)
{
	unsigned int temp;
	unsigned int mask = 0x0F << irq % 8 * 4;

	if (irq >= MXC_INTERNAL_IRQS)
		return -EINVAL;;

	temp = __raw_readl(avic_base + AVIC_NIPRIORITY(irq / 8));
	temp &= ~mask;
	temp |= prio & mask;

	__raw_writel(temp, avic_base + AVIC_NIPRIORITY(irq / 8));

	return 0;
}
#endif

#ifdef CONFIG_FIQ
static int avic_set_irq_fiq(unsigned int irq, unsigned int type)
{
	unsigned int irqt;

	if (irq >= MXC_INTERNAL_IRQS)
		return -EINVAL;

	if (irq < MXC_INTERNAL_IRQS / 2) {
		irqt = __raw_readl(avic_base + AVIC_INTTYPEL) & ~(1 << irq);
		__raw_writel(irqt | (!!type << irq), avic_base + AVIC_INTTYPEL);
	} else {
		irq -= MXC_INTERNAL_IRQS / 2;
		irqt = __raw_readl(avic_base + AVIC_INTTYPEH) & ~(1 << irq);
		__raw_writel(irqt | (!!type << irq), avic_base + AVIC_INTTYPEH);
	}

	return 0;
}
#endif /* CONFIG_FIQ */

/* Disable interrupt number "irq" in the AVIC */
static void mxc_mask_irq(unsigned int irq)
{
	__raw_writel(irq, avic_base + AVIC_INTDISNUM);
}

/* Enable interrupt number "irq" in the AVIC */
static void mxc_unmask_irq(unsigned int irq)
{
	__raw_writel(irq, avic_base + AVIC_INTENNUM);
}

static struct mxc_irq_chip mxc_avic_chip = {
	.base = {
		.ack = mxc_mask_irq,
		.mask = mxc_mask_irq,
		.unmask = mxc_unmask_irq,
	},
#ifdef CONFIG_MXC_IRQ_PRIOR
	.set_priority = avic_irq_set_priority,
#endif
#ifdef CONFIG_FIQ
	.set_irq_fiq = avic_set_irq_fiq,
#endif
};

/*
 * This function initializes the AVIC hardware and disables all the
 * interrupts. It registers the interrupt enable and disable functions
 * to the kernel for each interrupt source.
 */
void __init mxc_init_irq(void __iomem *irqbase)
{
	int i;

	avic_base = irqbase;

	/* put the AVIC into the reset value with
	 * all interrupts disabled
	 */
	__raw_writel(0, avic_base + AVIC_INTCNTL);
	__raw_writel(0x1f, avic_base + AVIC_NIMASK);

	/* disable all interrupts */
	__raw_writel(0, avic_base + AVIC_INTENABLEH);
	__raw_writel(0, avic_base + AVIC_INTENABLEL);

	/* all IRQ no FIQ */
	__raw_writel(0, avic_base + AVIC_INTTYPEH);
	__raw_writel(0, avic_base + AVIC_INTTYPEL);
	for (i = 0; i < MXC_INTERNAL_IRQS; i++) {
		set_irq_chip(i, &mxc_avic_chip.base);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	/* Set default priority value (0) for all IRQ's */
	for (i = 0; i < 8; i++)
		__raw_writel(0, avic_base + AVIC_NIPRIORITY(i));

#ifdef CONFIG_FIQ
	/* Initialize FIQ */
	init_FIQ();
#endif

	printk(KERN_INFO "MXC IRQ initialized\n");
}

