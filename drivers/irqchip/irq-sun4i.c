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
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/exception.h>
#include <asm/mach/irq.h>

#define SUN4I_IRQ_VECTOR_REG		0x00
#define SUN4I_IRQ_PROTECTION_REG	0x08
#define SUN4I_IRQ_NMI_CTRL_REG		0x0c
#define SUN4I_IRQ_PENDING_REG(x)	(0x10 + 0x4 * x)
#define SUN4I_IRQ_FIQ_PENDING_REG(x)	(0x20 + 0x4 * x)
#define SUN4I_IRQ_ENABLE_REG(x)		(0x40 + 0x4 * x)
#define SUN4I_IRQ_MASK_REG(x)		(0x50 + 0x4 * x)

static void __iomem *sun4i_irq_base;
static struct irq_domain *sun4i_irq_domain;

static void __exception_irq_entry sun4i_handle_irq(struct pt_regs *regs);

static void sun4i_irq_ack(struct irq_data *irqd)
{
	unsigned int irq = irqd_to_hwirq(irqd);

	if (irq != 0)
		return; /* Only IRQ 0 / the ENMI needs to be acked */

	writel(BIT(0), sun4i_irq_base + SUN4I_IRQ_PENDING_REG(0));
}

static void sun4i_irq_mask(struct irq_data *irqd)
{
	unsigned int irq = irqd_to_hwirq(irqd);
	unsigned int irq_off = irq % 32;
	int reg = irq / 32;
	u32 val;

	val = readl(sun4i_irq_base + SUN4I_IRQ_ENABLE_REG(reg));
	writel(val & ~(1 << irq_off),
	       sun4i_irq_base + SUN4I_IRQ_ENABLE_REG(reg));
}

static void sun4i_irq_unmask(struct irq_data *irqd)
{
	unsigned int irq = irqd_to_hwirq(irqd);
	unsigned int irq_off = irq % 32;
	int reg = irq / 32;
	u32 val;

	val = readl(sun4i_irq_base + SUN4I_IRQ_ENABLE_REG(reg));
	writel(val | (1 << irq_off),
	       sun4i_irq_base + SUN4I_IRQ_ENABLE_REG(reg));
}

static struct irq_chip sun4i_irq_chip = {
	.name		= "sun4i_irq",
	.irq_eoi	= sun4i_irq_ack,
	.irq_mask	= sun4i_irq_mask,
	.irq_unmask	= sun4i_irq_unmask,
	.flags		= IRQCHIP_EOI_THREADED | IRQCHIP_EOI_IF_HANDLED,
};

static int sun4i_irq_map(struct irq_domain *d, unsigned int virq,
			 irq_hw_number_t hw)
{
	irq_set_chip_and_handler(virq, &sun4i_irq_chip, handle_fasteoi_irq);
	set_irq_flags(virq, IRQF_VALID | IRQF_PROBE);

	return 0;
}

static const struct irq_domain_ops sun4i_irq_ops = {
	.map = sun4i_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static int __init sun4i_of_init(struct device_node *node,
				struct device_node *parent)
{
	sun4i_irq_base = of_iomap(node, 0);
	if (!sun4i_irq_base)
		panic("%s: unable to map IC registers\n",
			node->full_name);

	/* Disable all interrupts */
	writel(0, sun4i_irq_base + SUN4I_IRQ_ENABLE_REG(0));
	writel(0, sun4i_irq_base + SUN4I_IRQ_ENABLE_REG(1));
	writel(0, sun4i_irq_base + SUN4I_IRQ_ENABLE_REG(2));

	/* Unmask all the interrupts, ENABLE_REG(x) is used for masking */
	writel(0, sun4i_irq_base + SUN4I_IRQ_MASK_REG(0));
	writel(0, sun4i_irq_base + SUN4I_IRQ_MASK_REG(1));
	writel(0, sun4i_irq_base + SUN4I_IRQ_MASK_REG(2));

	/* Clear all the pending interrupts */
	writel(0xffffffff, sun4i_irq_base + SUN4I_IRQ_PENDING_REG(0));
	writel(0xffffffff, sun4i_irq_base + SUN4I_IRQ_PENDING_REG(1));
	writel(0xffffffff, sun4i_irq_base + SUN4I_IRQ_PENDING_REG(2));

	/* Enable protection mode */
	writel(0x01, sun4i_irq_base + SUN4I_IRQ_PROTECTION_REG);

	/* Configure the external interrupt source type */
	writel(0x00, sun4i_irq_base + SUN4I_IRQ_NMI_CTRL_REG);

	sun4i_irq_domain = irq_domain_add_linear(node, 3 * 32,
						 &sun4i_irq_ops, NULL);
	if (!sun4i_irq_domain)
		panic("%s: unable to create IRQ domain\n", node->full_name);

	set_handle_irq(sun4i_handle_irq);

	return 0;
}
IRQCHIP_DECLARE(allwinner_sun4i_ic, "allwinner,sun4i-a10-ic", sun4i_of_init);

static void __exception_irq_entry sun4i_handle_irq(struct pt_regs *regs)
{
	u32 hwirq;

	/*
	 * hwirq == 0 can mean one of 3 things:
	 * 1) no more irqs pending
	 * 2) irq 0 pending
	 * 3) spurious irq
	 * So if we immediately get a reading of 0, check the irq-pending reg
	 * to differentiate between 2 and 3. We only do this once to avoid
	 * the extra check in the common case of 1 hapening after having
	 * read the vector-reg once.
	 */
	hwirq = readl(sun4i_irq_base + SUN4I_IRQ_VECTOR_REG) >> 2;
	if (hwirq == 0 &&
		  !(readl(sun4i_irq_base + SUN4I_IRQ_PENDING_REG(0)) & BIT(0)))
		return;

	do {
		handle_domain_irq(sun4i_irq_domain, hwirq, regs);
		hwirq = readl(sun4i_irq_base + SUN4I_IRQ_VECTOR_REG) >> 2;
	} while (hwirq != 0);
}
