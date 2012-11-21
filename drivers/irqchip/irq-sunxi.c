/*
 * Allwinner A1X SoCs IRQ chip driver.
 *
 * Copyright (C) 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * Based on code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <linux/irqchip/sunxi.h>

#define SUNXI_IRQ_VECTOR_REG		0x00
#define SUNXI_IRQ_PROTECTION_REG	0x08
#define SUNXI_IRQ_NMI_CTRL_REG		0x0c
#define SUNXI_IRQ_PENDING_REG(x)	(0x10 + 0x4 * x)
#define SUNXI_IRQ_FIQ_PENDING_REG(x)	(0x20 + 0x4 * x)
#define SUNXI_IRQ_ENABLE_REG(x)		(0x40 + 0x4 * x)
#define SUNXI_IRQ_MASK_REG(x)		(0x50 + 0x4 * x)

static void __iomem *sunxi_irq_base;
static struct irq_domain *sunxi_irq_domain;

void sunxi_irq_ack(struct irq_data *irqd)
{
	unsigned int irq = irqd_to_hwirq(irqd);
	unsigned int irq_off = irq % 32;
	int reg = irq / 32;
	u32 val;

	val = readl(sunxi_irq_base + SUNXI_IRQ_PENDING_REG(reg));
	writel(val | (1 << irq_off),
	       sunxi_irq_base + SUNXI_IRQ_PENDING_REG(reg));
}

static void sunxi_irq_mask(struct irq_data *irqd)
{
	unsigned int irq = irqd_to_hwirq(irqd);
	unsigned int irq_off = irq % 32;
	int reg = irq / 32;
	u32 val;

	val = readl(sunxi_irq_base + SUNXI_IRQ_ENABLE_REG(reg));
	writel(val & ~(1 << irq_off),
	       sunxi_irq_base + SUNXI_IRQ_ENABLE_REG(reg));
}

static void sunxi_irq_unmask(struct irq_data *irqd)
{
	unsigned int irq = irqd_to_hwirq(irqd);
	unsigned int irq_off = irq % 32;
	int reg = irq / 32;
	u32 val;

	val = readl(sunxi_irq_base + SUNXI_IRQ_ENABLE_REG(reg));
	writel(val | (1 << irq_off),
	       sunxi_irq_base + SUNXI_IRQ_ENABLE_REG(reg));
}

static struct irq_chip sunxi_irq_chip = {
	.name		= "sunxi_irq",
	.irq_ack	= sunxi_irq_ack,
	.irq_mask	= sunxi_irq_mask,
	.irq_unmask	= sunxi_irq_unmask,
};

static int sunxi_irq_map(struct irq_domain *d, unsigned int virq,
			 irq_hw_number_t hw)
{
	irq_set_chip_and_handler(virq, &sunxi_irq_chip,
				 handle_level_irq);
	set_irq_flags(virq, IRQF_VALID | IRQF_PROBE);

	return 0;
}

static struct irq_domain_ops sunxi_irq_ops = {
	.map = sunxi_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static int __init sunxi_of_init(struct device_node *node,
				struct device_node *parent)
{
	sunxi_irq_base = of_iomap(node, 0);
	if (!sunxi_irq_base)
		panic("%s: unable to map IC registers\n",
			node->full_name);

	/* Disable all interrupts */
	writel(0, sunxi_irq_base + SUNXI_IRQ_ENABLE_REG(0));
	writel(0, sunxi_irq_base + SUNXI_IRQ_ENABLE_REG(1));
	writel(0, sunxi_irq_base + SUNXI_IRQ_ENABLE_REG(2));

	/* Mask all the interrupts */
	writel(0, sunxi_irq_base + SUNXI_IRQ_MASK_REG(0));
	writel(0, sunxi_irq_base + SUNXI_IRQ_MASK_REG(1));
	writel(0, sunxi_irq_base + SUNXI_IRQ_MASK_REG(2));

	/* Clear all the pending interrupts */
	writel(0xffffffff, sunxi_irq_base + SUNXI_IRQ_PENDING_REG(0));
	writel(0xffffffff, sunxi_irq_base + SUNXI_IRQ_PENDING_REG(1));
	writel(0xffffffff, sunxi_irq_base + SUNXI_IRQ_PENDING_REG(2));

	/* Enable protection mode */
	writel(0x01, sunxi_irq_base + SUNXI_IRQ_PROTECTION_REG);

	/* Configure the external interrupt source type */
	writel(0x00, sunxi_irq_base + SUNXI_IRQ_NMI_CTRL_REG);

	sunxi_irq_domain = irq_domain_add_linear(node, 3 * 32,
						 &sunxi_irq_ops, NULL);
	if (!sunxi_irq_domain)
		panic("%s: unable to create IRQ domain\n", node->full_name);

	return 0;
}

static struct of_device_id sunxi_irq_dt_ids[] __initconst = {
	{ .compatible = "allwinner,sunxi-ic", .data = sunxi_of_init }
};

void __init sunxi_init_irq(void)
{
	of_irq_init(sunxi_irq_dt_ids);
}

asmlinkage void __exception_irq_entry sunxi_handle_irq(struct pt_regs *regs)
{
	u32 irq, hwirq;

	hwirq = readl(sunxi_irq_base + SUNXI_IRQ_VECTOR_REG) >> 2;
	while (hwirq != 0) {
		irq = irq_find_mapping(sunxi_irq_domain, hwirq);
		handle_IRQ(irq, regs);
		hwirq = readl(sunxi_irq_base + SUNXI_IRQ_VECTOR_REG) >> 2;
	}
}
