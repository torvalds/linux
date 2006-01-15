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

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/hardware/vic.h>

static void __iomem *vic_base;

static void vic_mask_irq(unsigned int irq)
{
	irq -= IRQ_VIC_START;
	writel(1 << irq, vic_base + VIC_INT_ENABLE_CLEAR);
}

static void vic_unmask_irq(unsigned int irq)
{
	irq -= IRQ_VIC_START;
	writel(1 << irq, vic_base + VIC_INT_ENABLE);
}

static struct irqchip vic_chip = {
	.ack	= vic_mask_irq,
	.mask	= vic_mask_irq,
	.unmask	= vic_unmask_irq,
};

void __init vic_init(void __iomem *base, u32 vic_sources)
{
	unsigned int i;

	vic_base = base;

	/* Disable all interrupts initially. */

	writel(0, vic_base + VIC_INT_SELECT);
	writel(0, vic_base + VIC_INT_ENABLE);
	writel(~0, vic_base + VIC_INT_ENABLE_CLEAR);
	writel(0, vic_base + VIC_IRQ_STATUS);
	writel(0, vic_base + VIC_ITCR);
	writel(~0, vic_base + VIC_INT_SOFT_CLEAR);

	/*
	 * Make sure we clear all existing interrupts
	 */
	writel(0, vic_base + VIC_VECT_ADDR);
	for (i = 0; i < 19; i++) {
		unsigned int value;

		value = readl(vic_base + VIC_VECT_ADDR);
		writel(value, vic_base + VIC_VECT_ADDR);
	}

	for (i = 0; i < 16; i++) {
		void __iomem *reg = vic_base + VIC_VECT_CNTL0 + (i * 4);
		writel(VIC_VECT_CNTL_ENABLE | i, reg);
	}

	writel(32, vic_base + VIC_DEF_VECT_ADDR);

	for (i = 0; i < 32; i++) {
		unsigned int irq = IRQ_VIC_START + i;

		set_irq_chip(irq, &vic_chip);

		if (vic_sources & (1 << i)) {
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
		}
	}
}
