// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * interrupt controller support for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/syscore_ops.h>
#include <asm/mach/irq.h>
#include <asm/exception.h>

#define SIRFSOC_INT_RISC_MASK0		0x0018
#define SIRFSOC_INT_RISC_MASK1		0x001C
#define SIRFSOC_INT_RISC_LEVEL0		0x0020
#define SIRFSOC_INT_RISC_LEVEL1		0x0024
#define SIRFSOC_INIT_IRQ_ID		0x0038
#define SIRFSOC_INT_BASE_OFFSET		0x0004

#define SIRFSOC_NUM_IRQS		64
#define SIRFSOC_NUM_BANKS		(SIRFSOC_NUM_IRQS / 32)

static struct irq_domain *sirfsoc_irqdomain;

static void __iomem *sirfsoc_irq_get_regbase(void)
{
	return (void __iomem __force *)sirfsoc_irqdomain->host_data;
}

static __init void sirfsoc_alloc_gc(void __iomem *base)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	unsigned int set = IRQ_LEVEL;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	int i;

	irq_alloc_domain_generic_chips(sirfsoc_irqdomain, 32, 1, "irq_sirfsoc",
				       handle_level_irq, clr, set,
				       IRQ_GC_INIT_MASK_CACHE);

	for (i = 0; i < SIRFSOC_NUM_BANKS; i++) {
		gc = irq_get_domain_generic_chip(sirfsoc_irqdomain, i * 32);
		gc->reg_base = base + i * SIRFSOC_INT_BASE_OFFSET;
		ct = gc->chip_types;
		ct->chip.irq_mask = irq_gc_mask_clr_bit;
		ct->chip.irq_unmask = irq_gc_mask_set_bit;
		ct->regs.mask = SIRFSOC_INT_RISC_MASK0;
	}
}

static void __exception_irq_entry sirfsoc_handle_irq(struct pt_regs *regs)
{
	void __iomem *base = sirfsoc_irq_get_regbase();
	u32 irqstat;

	irqstat = readl_relaxed(base + SIRFSOC_INIT_IRQ_ID);
	handle_domain_irq(sirfsoc_irqdomain, irqstat & 0xff, regs);
}

static int __init sirfsoc_irq_init(struct device_node *np,
	struct device_node *parent)
{
	void __iomem *base = of_iomap(np, 0);
	if (!base)
		panic("unable to map intc cpu registers\n");

	sirfsoc_irqdomain = irq_domain_add_linear(np, SIRFSOC_NUM_IRQS,
						  &irq_generic_chip_ops, base);
	sirfsoc_alloc_gc(base);

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
	void __iomem *base = sirfsoc_irq_get_regbase();

	sirfsoc_irq_st.mask0 = readl_relaxed(base + SIRFSOC_INT_RISC_MASK0);
	sirfsoc_irq_st.mask1 = readl_relaxed(base + SIRFSOC_INT_RISC_MASK1);
	sirfsoc_irq_st.level0 = readl_relaxed(base + SIRFSOC_INT_RISC_LEVEL0);
	sirfsoc_irq_st.level1 = readl_relaxed(base + SIRFSOC_INT_RISC_LEVEL1);

	return 0;
}

static void sirfsoc_irq_resume(void)
{
	void __iomem *base = sirfsoc_irq_get_regbase();

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
