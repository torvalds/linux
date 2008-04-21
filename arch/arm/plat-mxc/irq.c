/*
 *  Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/arch/common.h>

/* Disable interrupt number "irq" in the AVIC */
static void mxc_mask_irq(unsigned int irq)
{
	__raw_writel(irq, AVIC_INTDISNUM);
}

/* Enable interrupt number "irq" in the AVIC */
static void mxc_unmask_irq(unsigned int irq)
{
	__raw_writel(irq, AVIC_INTENNUM);
}

static struct irq_chip mxc_avic_chip = {
	.mask_ack = mxc_mask_irq,
	.mask = mxc_mask_irq,
	.unmask = mxc_unmask_irq,
};

/*
 * This function initializes the AVIC hardware and disables all the
 * interrupts. It registers the interrupt enable and disable functions
 * to the kernel for each interrupt source.
 */
void __init mxc_init_irq(void)
{
	int i;
	u32 reg;

	/* put the AVIC into the reset value with
	 * all interrupts disabled
	 */
	__raw_writel(0, AVIC_INTCNTL);
	__raw_writel(0x1f, AVIC_NIMASK);

	/* disable all interrupts */
	__raw_writel(0, AVIC_INTENABLEH);
	__raw_writel(0, AVIC_INTENABLEL);

	/* all IRQ no FIQ */
	__raw_writel(0, AVIC_INTTYPEH);
	__raw_writel(0, AVIC_INTTYPEL);
	for (i = 0; i < MXC_MAX_INT_LINES; i++) {
		set_irq_chip(i, &mxc_avic_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	/* Set WDOG2's interrupt the highest priority level (bit 28-31) */
	reg = __raw_readl(AVIC_NIPRIORITY6);
	reg |= (0xF << 28);
	__raw_writel(reg, AVIC_NIPRIORITY6);

	printk(KERN_INFO "MXC IRQ initialized\n");
}
