/*
 * interrupt controller support for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/irqdomain.h>
#include <linux/syscore_ops.h>
#include <asm/mach/irq.h>
#include <asm/exception.h>
#include "irqchip.h"

#define SIRFSOC_INT_RISC_MASK0          0x0018
#define SIRFSOC_INT_RISC_MASK1          0x001C
#define SIRFSOC_INT_RISC_LEVEL0         0x0020
#define SIRFSOC_INT_RISC_LEVEL1         0x0024
#define SIRFSOC_INIT_IRQ_ID		0x0038

#define SIRFSOC_NUM_IRQS		64

static struct irq_domain *sirfsoc_irqdomain;

static __init void
sirfsoc_alloc_gc(void __iomem *base, unsigned int irq_start, unsigned int num)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	int ret;
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	unsigned int set = IRQ_LEVEL;

	ret = irq_alloc_domain_generic_chips(sirfsoc_irqdomain, num, 1, "irq_sirfsoc",
		handle_level_irq, clr, set, IRQ_GC_INIT_MASK_CACHE);

	gc = irq_get_domain_generic_chip(sirfsoc_irqdomain, irq_start);
	gc->reg_base = base;
	ct = gc->chip_types;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	ct->regs.mask = SIRFSOC_INT_RISC_MASK0;
}

static asmlinkage void __exception_irq_entry sirfsoc_handle_irq(struct pt_regs *regs)
{
	void __iomem *base = sirfsoc_irqdomain->host_data;
	u32 irqstat, irqnr;

	irqstat = readl_relaxed(base + SIRFSOC_INIT_IRQ_ID);
	irqnr = irq_find_mapping(sirfsoc_irqdomain, irqstat & 0xff);

	handle_IRQ(irqnr, regs);
}

static int __init sirfsoc_irq_init(struct device_node *np, struct device_node *parent)
{
	void __iomem *base = of_iomap(np, 0);
	if (!base)
		panic("unable to map intc cpu registers\n");

	sirfsoc_irqdomain = irq_domain_add_linear(np, SIRFSOC_NUM_IRQS,
		&irq_generic_chip_ops, base);

	sirfsoc_alloc_gc(base, 0, 32);
	sirfsoc_alloc_gc(base + 4, 32, SIRFSOC_NUM_IRQS - 32);

	writel_relaxed(0, base + SIRFSOC_INT_RISC_LEVEL0);
	writel_relaxed(0, base + SIRFSOC_INT_RISC_LEVEL1);

	writel_relaxed(0, base + SIRFSOC_INT_RISC_MASK0);
	writel_relaxed(0, base + SIRFSOC_INT_RISC_MASK1);

	set_handle_irq(sirfsoc_handle_irq);

	return 0;
}
IRQCHIP_DECLARE(sirfsoc_intc, "sirf,prima2-intc", sirfsoc_irq_init);

struct sirfsoc_irq_status {
	u32 mask0;
	u32 mask1;
	u32 level0;
	u32 level1;
};

static struct sirfsoc_irq_status sirfsoc_irq_st;

static int sirfsoc_irq_suspend(void)
{
	void __iomem *base = sirfsoc_irqdomain->host_data;

	sirfsoc_irq_st.mask0 = readl_relaxed(base + SIRFSOC_INT_RISC_MASK0);
	sirfsoc_irq_st.mask1 = readl_relaxed(base + SIRFSOC_INT_RISC_MASK1);
	sirfsoc_irq_st.level0 = readl_relaxed(base + SIRFSOC_INT_RISC_LEVEL0);
	sirfsoc_irq_st.level1 = readl_relaxed(base + SIRFSOC_INT_RISC_LEVEL1);

	return 0;
}

static void sirfsoc_irq_resume(void)
{
	void __iomem *base = sirfsoc_irqdomain->host_data;

	writel_relaxed(sirfsoc_irq_st.mask0, base + SIRFSOC_INT_RISC_MASK0);
	writel_relaxed(sirfsoc_irq_st.mask1, base + SIRFSOC_INT_RISC_MASK1);
	writel_relaxed(sirfsoc_irq_st.level0, base + SIRFSOC_INT_RISC_LEVEL0);
	writel_relaxed(sirfsoc_irq_st.level1, base + SIRFSOC_INT_RISC_LEVEL1);
}

static struct syscore_ops sirfsoc_irq_syscore_ops = {
	.suspend	= sirfsoc_irq_suspend,
	.resume		= sirfsoc_irq_resume,
};

static int __init sirfsoc_irq_pm_init(void)
{
	if (!sirfsoc_irqdomain)
		return 0;

	register_syscore_ops(&sirfsoc_irq_syscore_ops);
	return 0;
}
device_initcall(sirfsoc_irq_pm_init);
