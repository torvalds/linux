// SPDX-License-Identifier: GPL-2.0-or-later
//
// Copyright (C) 2006, 2019 Texas Instruments.
//
// Interrupt handler for DaVinci boards.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/irq-davinci-aintc.h>
#include <linux/io.h>
#include <linux/irqdomain.h>

#include <asm/exception.h>

#define DAVINCI_AINTC_FIQ_REG0		0x00
#define DAVINCI_AINTC_FIQ_REG1		0x04
#define DAVINCI_AINTC_IRQ_REG0		0x08
#define DAVINCI_AINTC_IRQ_REG1		0x0c
#define DAVINCI_AINTC_IRQ_IRQENTRY	0x14
#define DAVINCI_AINTC_IRQ_ENT_REG0	0x18
#define DAVINCI_AINTC_IRQ_ENT_REG1	0x1c
#define DAVINCI_AINTC_IRQ_INCTL_REG	0x20
#define DAVINCI_AINTC_IRQ_EABASE_REG	0x24
#define DAVINCI_AINTC_IRQ_INTPRI0_REG	0x30
#define DAVINCI_AINTC_IRQ_INTPRI7_REG	0x4c

static void __iomem *davinci_aintc_base;
static struct irq_domain *davinci_aintc_irq_domain;

static inline void davinci_aintc_writel(unsigned long value, int offset)
{
	writel_relaxed(value, davinci_aintc_base + offset);
}

static inline unsigned long davinci_aintc_readl(int offset)
{
	return readl_relaxed(davinci_aintc_base + offset);
}

static __init void
davinci_aintc_setup_gc(void __iomem *base,
		       unsigned int irq_start, unsigned int num)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	gc = irq_get_domain_generic_chip(davinci_aintc_irq_domain, irq_start);
	gc->reg_base = base;
	gc->irq_base = irq_start;

	ct = gc->chip_types;
	ct->chip.irq_ack = irq_gc_ack_set_bit;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;

	ct->regs.ack = DAVINCI_AINTC_IRQ_REG0;
	ct->regs.mask = DAVINCI_AINTC_IRQ_ENT_REG0;
	irq_setup_generic_chip(gc, IRQ_MSK(num), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST | IRQ_NOPROBE, 0);
}

static asmlinkage void __exception_irq_entry
davinci_aintc_handle_irq(struct pt_regs *regs)
{
	int irqnr = davinci_aintc_readl(DAVINCI_AINTC_IRQ_IRQENTRY);

	/*
	 * Use the formula for entry vector index generation from section
	 * 8.3.3 of the manual.
	 */
	irqnr >>= 2;
	irqnr -= 1;

	handle_domain_irq(davinci_aintc_irq_domain, irqnr, regs);
}

/* ARM Interrupt Controller Initialization */
void __init davinci_aintc_init(const struct davinci_aintc_config *config)
{
	unsigned int irq_off, reg_off, prio, shift;
	void __iomem *req;
	int ret, irq_base;
	const u8 *prios;

	req = request_mem_region(config->reg.start,
				 resource_size(&config->reg),
				 "davinci-cp-intc");
	if (!req) {
		pr_err("%s: register range busy\n", __func__);
		return;
	}

	davinci_aintc_base = ioremap(config->reg.start,
				     resource_size(&config->reg));
	if (!davinci_aintc_base) {
		pr_err("%s: unable to ioremap register range\n", __func__);
		return;
	}

	/* Clear all interrupt requests */
	davinci_aintc_writel(~0x0, DAVINCI_AINTC_FIQ_REG0);
	davinci_aintc_writel(~0x0, DAVINCI_AINTC_FIQ_REG1);
	davinci_aintc_writel(~0x0, DAVINCI_AINTC_IRQ_REG0);
	davinci_aintc_writel(~0x0, DAVINCI_AINTC_IRQ_REG1);

	/* Disable all interrupts */
	davinci_aintc_writel(0x0, DAVINCI_AINTC_IRQ_ENT_REG0);
	davinci_aintc_writel(0x0, DAVINCI_AINTC_IRQ_ENT_REG1);

	/* Interrupts disabled immediately, IRQ entry reflects all */
	davinci_aintc_writel(0x0, DAVINCI_AINTC_IRQ_INCTL_REG);

	/* we don't use the hardware vector table, just its entry addresses */
	davinci_aintc_writel(0, DAVINCI_AINTC_IRQ_EABASE_REG);

	/* Clear all interrupt requests */
	davinci_aintc_writel(~0x0, DAVINCI_AINTC_FIQ_REG0);
	davinci_aintc_writel(~0x0, DAVINCI_AINTC_FIQ_REG1);
	davinci_aintc_writel(~0x0, DAVINCI_AINTC_IRQ_REG0);
	davinci_aintc_writel(~0x0, DAVINCI_AINTC_IRQ_REG1);

	prios = config->prios;
	for (reg_off = DAVINCI_AINTC_IRQ_INTPRI0_REG;
	     reg_off <= DAVINCI_AINTC_IRQ_INTPRI7_REG; reg_off += 4) {
		for (shift = 0, prio = 0; shift < 32; shift += 4, prios++)
			prio |= (*prios & 0x07) << shift;
		davinci_aintc_writel(prio, reg_off);
	}

	irq_base = irq_alloc_descs(-1, 0, config->num_irqs, 0);
	if (irq_base < 0) {
		pr_err("%s: unable to allocate interrupt descriptors: %d\n",
		       __func__, irq_base);
		return;
	}

	davinci_aintc_irq_domain = irq_domain_add_legacy(NULL,
						config->num_irqs, irq_base, 0,
						&irq_domain_simple_ops, NULL);
	if (!davinci_aintc_irq_domain) {
		pr_err("%s: unable to create interrupt domain\n", __func__);
		return;
	}

	ret = irq_alloc_domain_generic_chips(davinci_aintc_irq_domain, 32, 1,
					     "AINTC", handle_edge_irq,
					     IRQ_NOREQUEST | IRQ_NOPROBE, 0, 0);
	if (ret) {
		pr_err("%s: unable to allocate generic irq chips for domain\n",
		       __func__);
		return;
	}

	for (irq_off = 0, reg_off = 0;
	     irq_off < config->num_irqs;
	     irq_off += 32, reg_off += 0x04)
		davinci_aintc_setup_gc(davinci_aintc_base + reg_off,
				       irq_base + irq_off, 32);

	set_handle_irq(davinci_aintc_handle_irq);
}
