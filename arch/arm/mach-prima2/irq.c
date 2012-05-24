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
#include <mach/hardware.h>
#include <asm/mach/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/irqdomain.h>
#include <linux/syscore_ops.h>

#define SIRFSOC_INT_RISC_MASK0          0x0018
#define SIRFSOC_INT_RISC_MASK1          0x001C
#define SIRFSOC_INT_RISC_LEVEL0         0x0020
#define SIRFSOC_INT_RISC_LEVEL1         0x0024

void __iomem *sirfsoc_intc_base;

static __init void
sirfsoc_alloc_gc(void __iomem *base, unsigned int irq_start, unsigned int num)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	gc = irq_alloc_generic_chip("SIRFINTC", 1, irq_start, base, handle_level_irq);
	ct = gc->chip_types;

	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	ct->regs.mask = SIRFSOC_INT_RISC_MASK0;

	irq_setup_generic_chip(gc, IRQ_MSK(num), IRQ_GC_INIT_MASK_CACHE, IRQ_NOREQUEST, 0);
}

static __init void sirfsoc_irq_init(void)
{
	sirfsoc_alloc_gc(sirfsoc_intc_base, 0, 32);
	sirfsoc_alloc_gc(sirfsoc_intc_base + 4, 32, SIRFSOC_INTENAL_IRQ_END - 32);

	writel_relaxed(0, sirfsoc_intc_base + SIRFSOC_INT_RISC_LEVEL0);
	writel_relaxed(0, sirfsoc_intc_base + SIRFSOC_INT_RISC_LEVEL1);

	writel_relaxed(0, sirfsoc_intc_base + SIRFSOC_INT_RISC_MASK0);
	writel_relaxed(0, sirfsoc_intc_base + SIRFSOC_INT_RISC_MASK1);
}

static struct of_device_id intc_ids[]  = {
	{ .compatible = "sirf,prima2-intc" },
	{},
};

void __init sirfsoc_of_irq_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, intc_ids);
	if (!np)
		panic("unable to find compatible intc node in dtb\n");

	sirfsoc_intc_base = of_iomap(np, 0);
	if (!sirfsoc_intc_base)
		panic("unable to map intc cpu registers\n");

	irq_domain_add_legacy(np, 32, 0, 0, &irq_domain_simple_ops, NULL);

	of_node_put(np);

	sirfsoc_irq_init();
}

struct sirfsoc_irq_status {
	u32 mask0;
	u32 mask1;
	u32 level0;
	u32 level1;
};

static struct sirfsoc_irq_status sirfsoc_irq_st;

static int sirfsoc_irq_suspend(void)
{
	sirfsoc_irq_st.mask0 = readl_relaxed(sirfsoc_intc_base + SIRFSOC_INT_RISC_MASK0);
	sirfsoc_irq_st.mask1 = readl_relaxed(sirfsoc_intc_base + SIRFSOC_INT_RISC_MASK1);
	sirfsoc_irq_st.level0 = readl_relaxed(sirfsoc_intc_base + SIRFSOC_INT_RISC_LEVEL0);
	sirfsoc_irq_st.level1 = readl_relaxed(sirfsoc_intc_base + SIRFSOC_INT_RISC_LEVEL1);

	return 0;
}

static void sirfsoc_irq_resume(void)
{
	writel_relaxed(sirfsoc_irq_st.mask0, sirfsoc_intc_base + SIRFSOC_INT_RISC_MASK0);
	writel_relaxed(sirfsoc_irq_st.mask1, sirfsoc_intc_base + SIRFSOC_INT_RISC_MASK1);
	writel_relaxed(sirfsoc_irq_st.level0, sirfsoc_intc_base + SIRFSOC_INT_RISC_LEVEL0);
	writel_relaxed(sirfsoc_irq_st.level1, sirfsoc_intc_base + SIRFSOC_INT_RISC_LEVEL1);
}

static struct syscore_ops sirfsoc_irq_syscore_ops = {
	.suspend	= sirfsoc_irq_suspend,
	.resume		= sirfsoc_irq_resume,
};

static int __init sirfsoc_irq_pm_init(void)
{
	register_syscore_ops(&sirfsoc_irq_syscore_ops);
	return 0;
}
device_initcall(sirfsoc_irq_pm_init);
