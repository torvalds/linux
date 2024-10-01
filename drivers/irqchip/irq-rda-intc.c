// SPDX-License-Identifier: GPL-2.0+
/*
 * RDA8810PL SoC irqchip driver
 *
 * Copyright RDA Microelectronics Company Limited
 * Copyright (c) 2017 Andreas Färber
 * Copyright (c) 2018 Manivannan Sadhasivam
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>

#include <asm/exception.h>

#define RDA_INTC_FINALSTATUS	0x00
#define RDA_INTC_MASK_SET	0x08
#define RDA_INTC_MASK_CLR	0x0c

#define RDA_IRQ_MASK_ALL	0xFFFFFFFF

#define RDA_NR_IRQS 32

static void __iomem *rda_intc_base;
static struct irq_domain *rda_irq_domain;

static void rda_intc_mask_irq(struct irq_data *d)
{
	writel_relaxed(BIT(d->hwirq), rda_intc_base + RDA_INTC_MASK_CLR);
}

static void rda_intc_unmask_irq(struct irq_data *d)
{
	writel_relaxed(BIT(d->hwirq), rda_intc_base + RDA_INTC_MASK_SET);
}

static int rda_intc_set_type(struct irq_data *data, unsigned int flow_type)
{
	/* Hardware supports only level triggered interrupts */
	if ((flow_type & (IRQF_TRIGGER_HIGH | IRQF_TRIGGER_LOW)) == flow_type)
		return 0;

	return -EINVAL;
}

static void __exception_irq_entry rda_handle_irq(struct pt_regs *regs)
{
	u32 stat = readl_relaxed(rda_intc_base + RDA_INTC_FINALSTATUS);
	u32 hwirq;

	while (stat) {
		hwirq = __fls(stat);
		generic_handle_domain_irq(rda_irq_domain, hwirq);
		stat &= ~BIT(hwirq);
	}
}

static struct irq_chip rda_irq_chip = {
	.name		= "rda-intc",
	.irq_mask	= rda_intc_mask_irq,
	.irq_unmask	= rda_intc_unmask_irq,
	.irq_set_type	= rda_intc_set_type,
};

static int rda_irq_map(struct irq_domain *d,
		       unsigned int virq, irq_hw_number_t hw)
{
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &rda_irq_chip, handle_level_irq);
	irq_set_chip_data(virq, d->host_data);
	irq_set_probe(virq);

	return 0;
}

static const struct irq_domain_ops rda_irq_domain_ops = {
	.map = rda_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static int __init rda8810_intc_init(struct device_node *node,
				    struct device_node *parent)
{
	rda_intc_base = of_io_request_and_map(node, 0, "rda-intc");
	if (IS_ERR(rda_intc_base))
		return PTR_ERR(rda_intc_base);

	/* Mask all interrupt sources */
	writel_relaxed(RDA_IRQ_MASK_ALL, rda_intc_base + RDA_INTC_MASK_CLR);

	rda_irq_domain = irq_domain_create_linear(&node->fwnode, RDA_NR_IRQS,
						  &rda_irq_domain_ops,
						  rda_intc_base);
	if (!rda_irq_domain) {
		iounmap(rda_intc_base);
		return -ENOMEM;
	}

	set_handle_irq(rda_handle_irq);

	return 0;
}

IRQCHIP_DECLARE(rda_intc, "rda,8810pl-intc", rda8810_intc_init);
