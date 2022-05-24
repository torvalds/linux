// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/irq/irq-nvic.c
 *
 * Copyright (C) 2008 ARM Limited, All Rights Reserved.
 * Copyright (C) 2013 Pengutronix
 *
 * Support for the Nested Vectored Interrupt Controller found on the
 * ARMv7-M CPUs (Cortex-M3/M4)
 */
#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>

#include <asm/v7m.h>
#include <asm/exception.h>

#define NVIC_ISER		0x000
#define NVIC_ICER		0x080
#define NVIC_IPR		0x400

#define NVIC_MAX_BANKS		16
/*
 * Each bank handles 32 irqs. Only the 16th (= last) bank handles only
 * 16 irqs.
 */
#define NVIC_MAX_IRQ		((NVIC_MAX_BANKS - 1) * 32 + 16)

static struct irq_domain *nvic_irq_domain;

asmlinkage void __exception_irq_entry
nvic_handle_irq(irq_hw_number_t hwirq, struct pt_regs *regs)
{
	unsigned int irq = irq_linear_revmap(nvic_irq_domain, hwirq);

	handle_IRQ(irq, regs);
}

static int nvic_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *arg)
{
	int i, ret;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = arg;

	ret = irq_domain_translate_onecell(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++)
		irq_map_generic_chip(domain, virq + i, hwirq + i);

	return 0;
}

static const struct irq_domain_ops nvic_irq_domain_ops = {
	.translate = irq_domain_translate_onecell,
	.alloc = nvic_irq_domain_alloc,
	.free = irq_domain_free_irqs_top,
};

static int __init nvic_of_init(struct device_node *node,
			       struct device_node *parent)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	unsigned int irqs, i, ret, numbanks;
	void __iomem *nvic_base;

	numbanks = (readl_relaxed(V7M_SCS_ICTR) &
		    V7M_SCS_ICTR_INTLINESNUM_MASK) + 1;

	nvic_base = of_iomap(node, 0);
	if (!nvic_base) {
		pr_warn("unable to map nvic registers\n");
		return -ENOMEM;
	}

	irqs = numbanks * 32;
	if (irqs > NVIC_MAX_IRQ)
		irqs = NVIC_MAX_IRQ;

	nvic_irq_domain =
		irq_domain_add_linear(node, irqs, &nvic_irq_domain_ops, NULL);

	if (!nvic_irq_domain) {
		pr_warn("Failed to allocate irq domain\n");
		iounmap(nvic_base);
		return -ENOMEM;
	}

	ret = irq_alloc_domain_generic_chips(nvic_irq_domain, 32, 1,
					     "nvic_irq", handle_fasteoi_irq,
					     clr, 0, IRQ_GC_INIT_MASK_CACHE);
	if (ret) {
		pr_warn("Failed to allocate irq chips\n");
		irq_domain_remove(nvic_irq_domain);
		iounmap(nvic_base);
		return ret;
	}

	for (i = 0; i < numbanks; ++i) {
		struct irq_chip_generic *gc;

		gc = irq_get_domain_generic_chip(nvic_irq_domain, 32 * i);
		gc->reg_base = nvic_base + 4 * i;
		gc->chip_types[0].regs.enable = NVIC_ISER;
		gc->chip_types[0].regs.disable = NVIC_ICER;
		gc->chip_types[0].chip.irq_mask = irq_gc_mask_disable_reg;
		gc->chip_types[0].chip.irq_unmask = irq_gc_unmask_enable_reg;
		/* This is a no-op as end of interrupt is signaled by the
		 * exception return sequence.
		 */
		gc->chip_types[0].chip.irq_eoi = irq_gc_noop;

		/* disable interrupts */
		writel_relaxed(~0, gc->reg_base + NVIC_ICER);
	}

	/* Set priority on all interrupts */
	for (i = 0; i < irqs; i += 4)
		writel_relaxed(0, nvic_base + NVIC_IPR + i);

	return 0;
}
IRQCHIP_DECLARE(armv7m_nvic, "arm,armv7m-nvic", nvic_of_init);
