/*
 *  linux/arch/arm/common/vic.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
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
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>

#include <asm/mach/irq.h>
#include <asm/hardware/vic.h>

static void vic_mask_irq(unsigned int irq)
{
	void __iomem *base = get_irq_chip_data(irq);
	irq &= 31;
	writel(1 << irq, base + VIC_INT_ENABLE_CLEAR);
}

static void vic_unmask_irq(unsigned int irq)
{
	void __iomem *base = get_irq_chip_data(irq);
	irq &= 31;
	writel(1 << irq, base + VIC_INT_ENABLE);
}

static struct irq_chip vic_chip = {
	.name	= "VIC",
	.ack	= vic_mask_irq,
	.mask	= vic_mask_irq,
	.unmask	= vic_unmask_irq,
};

/**
 * vic_init - initialise a vectored interrupt controller
 * @base: iomem base address
 * @irq_start: starting interrupt number, must be muliple of 32
 * @vic_sources: bitmask of interrupt sources to allow
 */
void __init vic_init(void __iomem *base, unsigned int irq_start,
		     u32 vic_sources)
{
	unsigned int i;

	/* Disable all interrupts initially. */

	writel(0, base + VIC_INT_SELECT);
	writel(0, base + VIC_INT_ENABLE);
	writel(~0, base + VIC_INT_ENABLE_CLEAR);
	writel(0, base + VIC_IRQ_STATUS);
	writel(0, base + VIC_ITCR);
	writel(~0, base + VIC_INT_SOFT_CLEAR);

	/*
	 * Make sure we clear all existing interrupts
	 */
	writel(0, base + VIC_VECT_ADDR);
	for (i = 0; i < 19; i++) {
		unsigned int value;

		value = readl(base + VIC_VECT_ADDR);
		writel(value, base + VIC_VECT_ADDR);
	}

	for (i = 0; i < 16; i++) {
		void __iomem *reg = base + VIC_VECT_CNTL0 + (i * 4);
		writel(VIC_VECT_CNTL_ENABLE | i, reg);
	}

	writel(32, base + VIC_DEF_VECT_ADDR);

	for (i = 0; i < 32; i++) {
		unsigned int irq = irq_start + i;

		set_irq_chip(irq, &vic_chip);
		set_irq_chip_data(irq, base);

		if (vic_sources & (1 << i)) {
			set_irq_handler(irq, handle_level_irq);
			set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
		}
	}
}
