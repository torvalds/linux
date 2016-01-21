/*
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2014 Oleksij Rempel <linux@rempel-privat.de>
 *	Add Alphascale ASM9260 support.
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
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/stmp_device.h>
#include <asm/exception.h>

#include "alphascale_asm9260-icoll.h"

/*
 * this device provide 4 offsets for each register:
 * 0x0 - plain read write mode
 * 0x4 - set mode, OR logic.
 * 0x8 - clr mode, XOR logic.
 * 0xc - togle mode.
 */
#define SET_REG 4
#define CLR_REG 8

#define HW_ICOLL_VECTOR				0x0000
#define HW_ICOLL_LEVELACK			0x0010
#define HW_ICOLL_CTRL				0x0020
#define HW_ICOLL_STAT_OFFSET			0x0070
#define HW_ICOLL_INTERRUPT0			0x0120
#define HW_ICOLL_INTERRUPTn(n)			((n) * 0x10)
#define BM_ICOLL_INTR_ENABLE			BIT(2)
#define BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0	0x1

#define ICOLL_NUM_IRQS		128

enum icoll_type {
	ICOLL,
	ASM9260_ICOLL,
};

struct icoll_priv {
	void __iomem *vector;
	void __iomem *levelack;
	void __iomem *ctrl;
	void __iomem *stat;
	void __iomem *intr;
	void __iomem *clear;
	enum icoll_type type;
};

static struct icoll_priv icoll_priv;
static struct irq_domain *icoll_domain;

/* calculate bit offset depending on number of intterupt per register */
static u32 icoll_intr_bitshift(struct irq_data *d, u32 bit)
{
	/*
	 * mask lower part of hwirq to convert it
	 * in 0, 1, 2 or 3 and then multiply it by 8 (or shift by 3)
	 */
	return bit << ((d->hwirq & 3) << 3);
}

/* calculate mem offset depending on number of intterupt per register */
static void __iomem *icoll_intr_reg(struct irq_data *d)
{
	/* offset = hwirq / intr_per_reg * 0x10 */
	return icoll_priv.intr + ((d->hwirq >> 2) * 0x10);
}

static void icoll_ack_irq(struct irq_data *d)
{
	/*
	 * The Interrupt Collector is able to prioritize irqs.
	 * Currently only level 0 is used. So acking can use
	 * BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0 unconditionally.
	 */
	__raw_writel(BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0,
			icoll_priv.levelack);
}

static void icoll_mask_irq(struct irq_data *d)
{
	__raw_writel(BM_ICOLL_INTR_ENABLE,
			icoll_priv.intr + CLR_REG + HW_ICOLL_INTERRUPTn(d->hwirq));
}

static void icoll_unmask_irq(struct irq_data *d)
{
	__raw_writel(BM_ICOLL_INTR_ENABLE,
			icoll_priv.intr + SET_REG + HW_ICOLL_INTERRUPTn(d->hwirq));
}

static void asm9260_mask_irq(struct irq_data *d)
{
	__raw_writel(icoll_intr_bitshift(d, BM_ICOLL_INTR_ENABLE),
			icoll_intr_reg(d) + CLR_REG);
}

static void asm9260_unmask_irq(struct irq_data *d)
{
	__raw_writel(ASM9260_BM_CLEAR_BIT(d->hwirq),
		     icoll_priv.clear +
		     ASM9260_HW_ICOLL_CLEARn(d->hwirq));

	__raw_writel(icoll_intr_bitshift(d, BM_ICOLL_INTR_ENABLE),
			icoll_intr_reg(d) + SET_REG);
}

static struct irq_chip mxs_icoll_chip = {
	.irq_ack = icoll_ack_irq,
	.irq_mask = icoll_mask_irq,
	.irq_unmask = icoll_unmask_irq,
};

static struct irq_chip asm9260_icoll_chip = {
	.irq_ack = icoll_ack_irq,
	.irq_mask = asm9260_mask_irq,
	.irq_unmask = asm9260_unmask_irq,
};

asmlinkage void __exception_irq_entry icoll_handle_irq(struct pt_regs *regs)
{
	u32 irqnr;

	irqnr = __raw_readl(icoll_priv.stat);
	__raw_writel(irqnr, icoll_priv.vector);
	handle_domain_irq(icoll_domain, irqnr, regs);
}

static int icoll_irq_domain_map(struct irq_domain *d, unsigned int virq,
				irq_hw_number_t hw)
{
	struct irq_chip *chip;

	if (icoll_priv.type == ICOLL)
		chip = &mxs_icoll_chip;
	else
		chip = &asm9260_icoll_chip;

	irq_set_chip_and_handler(virq, chip, handle_level_irq);

	return 0;
}

static const struct irq_domain_ops icoll_irq_domain_ops = {
	.map = icoll_irq_domain_map,
	.xlate = irq_domain_xlate_onecell,
};

static void __init icoll_add_domain(struct device_node *np,
			  int num)
{
	icoll_domain = irq_domain_add_linear(np, num,
					     &icoll_irq_domain_ops, NULL);

	if (!icoll_domain)
		panic("%s: unable to create irq domain", np->full_name);
}

static void __iomem * __init icoll_init_iobase(struct device_node *np)
{
	void __iomem *icoll_base;

	icoll_base = of_io_request_and_map(np, 0, np->name);
	if (!icoll_base)
		panic("%s: unable to map resource", np->full_name);
	return icoll_base;
}

static int __init icoll_of_init(struct device_node *np,
			  struct device_node *interrupt_parent)
{
	void __iomem *icoll_base;

	icoll_priv.type = ICOLL;

	icoll_base		= icoll_init_iobase(np);
	icoll_priv.vector	= icoll_base + HW_ICOLL_VECTOR;
	icoll_priv.levelack	= icoll_base + HW_ICOLL_LEVELACK;
	icoll_priv.ctrl		= icoll_base + HW_ICOLL_CTRL;
	icoll_priv.stat		= icoll_base + HW_ICOLL_STAT_OFFSET;
	icoll_priv.intr		= icoll_base + HW_ICOLL_INTERRUPT0;
	icoll_priv.clear	= NULL;

	/*
	 * Interrupt Collector reset, which initializes the priority
	 * for each irq to level 0.
	 */
	stmp_reset_block(icoll_priv.ctrl);

	icoll_add_domain(np, ICOLL_NUM_IRQS);

	return 0;
}
IRQCHIP_DECLARE(mxs, "fsl,icoll", icoll_of_init);

static int __init asm9260_of_init(struct device_node *np,
			  struct device_node *interrupt_parent)
{
	void __iomem *icoll_base;
	int i;

	icoll_priv.type = ASM9260_ICOLL;

	icoll_base = icoll_init_iobase(np);
	icoll_priv.vector	= icoll_base + ASM9260_HW_ICOLL_VECTOR;
	icoll_priv.levelack	= icoll_base + ASM9260_HW_ICOLL_LEVELACK;
	icoll_priv.ctrl		= icoll_base + ASM9260_HW_ICOLL_CTRL;
	icoll_priv.stat		= icoll_base + ASM9260_HW_ICOLL_STAT_OFFSET;
	icoll_priv.intr		= icoll_base + ASM9260_HW_ICOLL_INTERRUPT0;
	icoll_priv.clear	= icoll_base + ASM9260_HW_ICOLL_CLEAR0;

	writel_relaxed(ASM9260_BM_CTRL_IRQ_ENABLE,
			icoll_priv.ctrl);
	/*
	 * ASM9260 don't provide reset bit. So, we need to set level 0
	 * manually.
	 */
	for (i = 0; i < 16 * 0x10; i += 0x10)
		writel(0, icoll_priv.intr + i);

	icoll_add_domain(np, ASM9260_NUM_IRQS);

	return 0;
}
IRQCHIP_DECLARE(asm9260, "alphascale,asm9260-icoll", asm9260_of_init);
