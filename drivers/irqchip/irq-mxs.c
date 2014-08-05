/*
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/stmp_device.h>
#include <asm/exception.h>

#include "irqchip.h"

#define HW_ICOLL_VECTOR				0x0000
#define HW_ICOLL_LEVELACK			0x0010
#define HW_ICOLL_CTRL				0x0020
#define HW_ICOLL_STAT_OFFSET			0x0070
#define HW_ICOLL_INTERRUPTn_SET(n)		(0x0124 + (n) * 0x10)
#define HW_ICOLL_INTERRUPTn_CLR(n)		(0x0128 + (n) * 0x10)
#define BM_ICOLL_INTERRUPTn_ENABLE		0x00000004
#define BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0	0x1

#define ICOLL_NUM_IRQS		128

static void __iomem *icoll_base;
static struct irq_domain *icoll_domain;

static void icoll_ack_irq(struct irq_data *d)
{
	/*
	 * The Interrupt Collector is able to prioritize irqs.
	 * Currently only level 0 is used. So acking can use
	 * BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0 unconditionally.
	 */
	__raw_writel(BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0,
			icoll_base + HW_ICOLL_LEVELACK);
}

static void icoll_mask_irq(struct irq_data *d)
{
	__raw_writel(BM_ICOLL_INTERRUPTn_ENABLE,
			icoll_base + HW_ICOLL_INTERRUPTn_CLR(d->hwirq));
}

static void icoll_unmask_irq(struct irq_data *d)
{
	__raw_writel(BM_ICOLL_INTERRUPTn_ENABLE,
			icoll_base + HW_ICOLL_INTERRUPTn_SET(d->hwirq));
}

static struct irq_chip mxs_icoll_chip = {
	.irq_ack = icoll_ack_irq,
	.irq_mask = icoll_mask_irq,
	.irq_unmask = icoll_unmask_irq,
};

asmlinkage void __exception_irq_entry icoll_handle_irq(struct pt_regs *regs)
{
	u32 irqnr;

	irqnr = __raw_readl(icoll_base + HW_ICOLL_STAT_OFFSET);
	__raw_writel(irqnr, icoll_base + HW_ICOLL_VECTOR);
	irqnr = irq_find_mapping(icoll_domain, irqnr);
	handle_IRQ(irqnr, regs);
}

static int icoll_irq_domain_map(struct irq_domain *d, unsigned int virq,
				irq_hw_number_t hw)
{
	irq_set_chip_and_handler(virq, &mxs_icoll_chip, handle_level_irq);
	set_irq_flags(virq, IRQF_VALID);

	return 0;
}

static struct irq_domain_ops icoll_irq_domain_ops = {
	.map = icoll_irq_domain_map,
	.xlate = irq_domain_xlate_onecell,
};

static int __init icoll_of_init(struct device_node *np,
			  struct device_node *interrupt_parent)
{
	icoll_base = of_iomap(np, 0);
	WARN_ON(!icoll_base);

	/*
	 * Interrupt Collector reset, which initializes the priority
	 * for each irq to level 0.
	 */
	stmp_reset_block(icoll_base + HW_ICOLL_CTRL);

	icoll_domain = irq_domain_add_linear(np, ICOLL_NUM_IRQS,
					     &icoll_irq_domain_ops, NULL);
	return icoll_domain ? 0 : -ENODEV;
}
IRQCHIP_DECLARE(mxs, "fsl,icoll", icoll_of_init);
