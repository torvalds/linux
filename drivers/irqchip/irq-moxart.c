/*
 * MOXA ART SoCs IRQ chip driver.
 *
 * Copyright (C) 2013 Jonas Jensen
 *
 * Jonas Jensen <jonas.jensen@gmail.com>
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
#include <linux/irqdomain.h>

#include <asm/exception.h>

#define IRQ_SOURCE_REG		0
#define IRQ_MASK_REG		0x04
#define IRQ_CLEAR_REG		0x08
#define IRQ_MODE_REG		0x0c
#define IRQ_LEVEL_REG		0x10
#define IRQ_STATUS_REG		0x14

#define FIQ_SOURCE_REG		0x20
#define FIQ_MASK_REG		0x24
#define FIQ_CLEAR_REG		0x28
#define FIQ_MODE_REG		0x2c
#define FIQ_LEVEL_REG		0x30
#define FIQ_STATUS_REG		0x34


struct moxart_irq_data {
	void __iomem *base;
	struct irq_domain *domain;
	unsigned int interrupt_mask;
};

static struct moxart_irq_data intc;

static void __exception_irq_entry handle_irq(struct pt_regs *regs)
{
	u32 irqstat;
	int hwirq;

	irqstat = readl(intc.base + IRQ_STATUS_REG);

	while (irqstat) {
		hwirq = ffs(irqstat) - 1;
		handle_IRQ(irq_linear_revmap(intc.domain, hwirq), regs);
		irqstat &= ~(1 << hwirq);
	}
}

static int __init moxart_of_intc_init(struct device_node *node,
				      struct device_node *parent)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	int ret;
	struct irq_chip_generic *gc;

	intc.base = of_iomap(node, 0);
	if (!intc.base) {
		pr_err("%s: unable to map IC registers\n",
		       node->full_name);
		return -EINVAL;
	}

	intc.domain = irq_domain_add_linear(node, 32, &irq_generic_chip_ops,
					    intc.base);
	if (!intc.domain) {
		pr_err("%s: unable to create IRQ domain\n", node->full_name);
		return -EINVAL;
	}

	ret = irq_alloc_domain_generic_chips(intc.domain, 32, 1,
					     "MOXARTINTC", handle_edge_irq,
					     clr, 0, IRQ_GC_INIT_MASK_CACHE);
	if (ret) {
		pr_err("%s: could not allocate generic chip\n",
		       node->full_name);
		irq_domain_remove(intc.domain);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "interrupt-mask",
				   &intc.interrupt_mask);
	if (ret)
		pr_err("%s: could not read interrupt-mask DT property\n",
		       node->full_name);

	gc = irq_get_domain_generic_chip(intc.domain, 0);

	gc->reg_base = intc.base;
	gc->chip_types[0].regs.mask = IRQ_MASK_REG;
	gc->chip_types[0].regs.ack = IRQ_CLEAR_REG;
	gc->chip_types[0].chip.irq_ack = irq_gc_ack_set_bit;
	gc->chip_types[0].chip.irq_mask = irq_gc_mask_clr_bit;
	gc->chip_types[0].chip.irq_unmask = irq_gc_mask_set_bit;

	writel(0, intc.base + IRQ_MASK_REG);
	writel(0xffffffff, intc.base + IRQ_CLEAR_REG);

	writel(intc.interrupt_mask, intc.base + IRQ_MODE_REG);
	writel(intc.interrupt_mask, intc.base + IRQ_LEVEL_REG);

	set_handle_irq(handle_irq);

	return 0;
}
IRQCHIP_DECLARE(moxa_moxart_ic, "moxa,moxart-ic", moxart_of_intc_init);
