/*
 *  linux/arch/arm/mach-pxa/irq.c
 *
 *  Generic PXA IRQ handling
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/exception.h>

#include <mach/hardware.h>
#include <mach/irqs.h>

#include "generic.h"

#define ICIP			(0x000)
#define ICMR			(0x004)
#define ICLR			(0x008)
#define ICFR			(0x00c)
#define ICPR			(0x010)
#define ICCR			(0x014)
#define ICHP			(0x018)
#define IPR(i)			(((i) < 32) ? (0x01c + ((i) << 2)) :		\
				((i) < 64) ? (0x0b0 + (((i) - 32) << 2)) :	\
				      (0x144 + (((i) - 64) << 2)))
#define ICHP_VAL_IRQ		(1 << 31)
#define ICHP_IRQ(i)		(((i) >> 16) & 0x7fff)
#define IPR_VALID		(1 << 31)

#define MAX_INTERNAL_IRQS	128

/*
 * This is for peripheral IRQs internal to the PXA chip.
 */

static void __iomem *pxa_irq_base;
static int pxa_internal_irq_nr;
static bool cpu_has_ipr;
static struct irq_domain *pxa_irq_domain;

static inline void __iomem *irq_base(int i)
{
	static unsigned long phys_base_offset[] = {
		0x0,
		0x9c,
		0x130,
	};

	return pxa_irq_base + phys_base_offset[i];
}

void pxa_mask_irq(struct irq_data *d)
{
	void __iomem *base = irq_data_get_irq_chip_data(d);
	irq_hw_number_t irq = irqd_to_hwirq(d);
	uint32_t icmr = __raw_readl(base + ICMR);

	icmr &= ~BIT(irq & 0x1f);
	__raw_writel(icmr, base + ICMR);
}

void pxa_unmask_irq(struct irq_data *d)
{
	void __iomem *base = irq_data_get_irq_chip_data(d);
	irq_hw_number_t irq = irqd_to_hwirq(d);
	uint32_t icmr = __raw_readl(base + ICMR);

	icmr |= BIT(irq & 0x1f);
	__raw_writel(icmr, base + ICMR);
}

static struct irq_chip pxa_internal_irq_chip = {
	.name		= "SC",
	.irq_ack	= pxa_mask_irq,
	.irq_mask	= pxa_mask_irq,
	.irq_unmask	= pxa_unmask_irq,
};

asmlinkage void __exception_irq_entry icip_handle_irq(struct pt_regs *regs)
{
	uint32_t icip, icmr, mask;

	do {
		icip = __raw_readl(pxa_irq_base + ICIP);
		icmr = __raw_readl(pxa_irq_base + ICMR);
		mask = icip & icmr;

		if (mask == 0)
			break;

		handle_IRQ(PXA_IRQ(fls(mask) - 1), regs);
	} while (1);
}

asmlinkage void __exception_irq_entry ichp_handle_irq(struct pt_regs *regs)
{
	uint32_t ichp;

	do {
		__asm__ __volatile__("mrc p6, 0, %0, c5, c0, 0\n": "=r"(ichp));

		if ((ichp & ICHP_VAL_IRQ) == 0)
			break;

		handle_IRQ(PXA_IRQ(ICHP_IRQ(ichp)), regs);
	} while (1);
}

static int pxa_irq_map(struct irq_domain *h, unsigned int virq,
		       irq_hw_number_t hw)
{
	void __iomem *base = irq_base(hw / 32);

	/* initialize interrupt priority */
	if (cpu_has_ipr)
		__raw_writel(hw | IPR_VALID, pxa_irq_base + IPR(hw));

	irq_set_chip_and_handler(virq, &pxa_internal_irq_chip,
				 handle_level_irq);
	irq_set_chip_data(virq, base);

	return 0;
}

static const struct irq_domain_ops pxa_irq_ops = {
	.map    = pxa_irq_map,
	.xlate  = irq_domain_xlate_onecell,
};

static __init void
pxa_init_irq_common(struct device_node *node, int irq_nr,
		    int (*fn)(struct irq_data *, unsigned int))
{
	int n;

	pxa_internal_irq_nr = irq_nr;
	pxa_irq_domain = irq_domain_add_legacy(node, irq_nr,
					       PXA_IRQ(0), 0,
					       &pxa_irq_ops, NULL);
	if (!pxa_irq_domain)
		panic("Unable to add PXA IRQ domain\n");
	irq_set_default_host(pxa_irq_domain);

	for (n = 0; n < irq_nr; n += 32) {
		void __iomem *base = irq_base(n >> 5);

		__raw_writel(0, base + ICMR);	/* disable all IRQs */
		__raw_writel(0, base + ICLR);	/* all IRQs are IRQ, not FIQ */
	}
	/* only unmasked interrupts kick us out of idle */
	__raw_writel(1, irq_base(0) + ICCR);

	pxa_internal_irq_chip.irq_set_wake = fn;
}

void __init pxa_init_irq(int irq_nr, int (*fn)(struct irq_data *, unsigned int))
{
	BUG_ON(irq_nr > MAX_INTERNAL_IRQS);

	pxa_irq_base = io_p2v(0x40d00000);
	cpu_has_ipr = !cpu_is_pxa25x();
	pxa_init_irq_common(NULL, irq_nr, fn);
}

#ifdef CONFIG_PM
static unsigned long saved_icmr[MAX_INTERNAL_IRQS/32];
static unsigned long saved_ipr[MAX_INTERNAL_IRQS];

static int pxa_irq_suspend(void)
{
	int i;

	for (i = 0; i < DIV_ROUND_UP(pxa_internal_irq_nr, 32); i++) {
		void __iomem *base = irq_base(i);

		saved_icmr[i] = __raw_readl(base + ICMR);
		__raw_writel(0, base + ICMR);
	}

	if (cpu_has_ipr) {
		for (i = 0; i < pxa_internal_irq_nr; i++)
			saved_ipr[i] = __raw_readl(pxa_irq_base + IPR(i));
	}

	return 0;
}

static void pxa_irq_resume(void)
{
	int i;

	for (i = 0; i < DIV_ROUND_UP(pxa_internal_irq_nr, 32); i++) {
		void __iomem *base = irq_base(i);

		__raw_writel(saved_icmr[i], base + ICMR);
		__raw_writel(0, base + ICLR);
	}

	if (cpu_has_ipr)
		for (i = 0; i < pxa_internal_irq_nr; i++)
			__raw_writel(saved_ipr[i], pxa_irq_base + IPR(i));

	__raw_writel(1, pxa_irq_base + ICCR);
}
#else
#define pxa_irq_suspend		NULL
#define pxa_irq_resume		NULL
#endif

struct syscore_ops pxa_irq_syscore_ops = {
	.suspend	= pxa_irq_suspend,
	.resume		= pxa_irq_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id intc_ids[] __initconst = {
	{ .compatible = "marvell,pxa-intc", },
	{}
};

void __init pxa_dt_irq_init(int (*fn)(struct irq_data *, unsigned int))
{
	struct device_node *node;
	struct resource res;
	int ret;

	node = of_find_matching_node(NULL, intc_ids);
	if (!node) {
		pr_err("Failed to find interrupt controller in arch-pxa\n");
		return;
	}

	ret = of_property_read_u32(node, "marvell,intc-nr-irqs",
				   &pxa_internal_irq_nr);
	if (ret) {
		pr_err("Not found marvell,intc-nr-irqs property\n");
		return;
	}

	ret = of_address_to_resource(node, 0, &res);
	if (ret < 0) {
		pr_err("No registers defined for node\n");
		return;
	}
	pxa_irq_base = io_p2v(res.start);

	if (of_find_property(node, "marvell,intc-priority", NULL))
		cpu_has_ipr = 1;

	ret = irq_alloc_descs(-1, 0, pxa_internal_irq_nr, 0);
	if (ret < 0) {
		pr_err("Failed to allocate IRQ numbers\n");
		return;
	}

	pxa_init_irq_common(node, pxa_internal_irq_nr, fn);
}
#endif /* CONFIG_OF */
