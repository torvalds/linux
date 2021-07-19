// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/irq.h>

#define INTC_IRQS		64

#define CK_INTC_ICR		0x00
#define CK_INTC_PEN31_00	0x14
#define CK_INTC_PEN63_32	0x2c
#define CK_INTC_NEN31_00	0x10
#define CK_INTC_NEN63_32	0x28
#define CK_INTC_SOURCE		0x40
#define CK_INTC_DUAL_BASE	0x100

#define GX_INTC_PEN31_00	0x00
#define GX_INTC_PEN63_32	0x04
#define GX_INTC_NEN31_00	0x40
#define GX_INTC_NEN63_32	0x44
#define GX_INTC_NMASK31_00	0x50
#define GX_INTC_NMASK63_32	0x54
#define GX_INTC_SOURCE		0x60

static void __iomem *reg_base;
static struct irq_domain *root_domain;

static int nr_irq = INTC_IRQS;

/*
 * When controller support pulse signal, the PEN_reg will hold on signal
 * without software trigger.
 *
 * So, to support pulse signal we need to clear IFR_reg and the address of
 * IFR_offset is NEN_offset - 8.
 */
static void irq_ck_mask_set_bit(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	unsigned long ifr = ct->regs.mask - 8;
	u32 mask = d->mask;

	irq_gc_lock(gc);
	*ct->mask_cache |= mask;
	irq_reg_writel(gc, *ct->mask_cache, ct->regs.mask);
	irq_reg_writel(gc, irq_reg_readl(gc, ifr) & ~mask, ifr);
	irq_gc_unlock(gc);
}

static void __init ck_set_gc(struct device_node *node, void __iomem *reg_base,
			     u32 mask_reg, u32 irq_base)
{
	struct irq_chip_generic *gc;

	gc = irq_get_domain_generic_chip(root_domain, irq_base);
	gc->reg_base = reg_base;
	gc->chip_types[0].regs.mask = mask_reg;
	gc->chip_types[0].chip.irq_mask = irq_gc_mask_clr_bit;
	gc->chip_types[0].chip.irq_unmask = irq_gc_mask_set_bit;

	if (of_find_property(node, "csky,support-pulse-signal", NULL))
		gc->chip_types[0].chip.irq_unmask = irq_ck_mask_set_bit;
}

static inline u32 build_channel_val(u32 idx, u32 magic)
{
	u32 res;

	/*
	 * Set the same index for each channel
	 */
	res = idx | (idx << 8) | (idx << 16) | (idx << 24);

	/*
	 * Set the channel magic number in descending order.
	 * The magic is 0x00010203 for ck-intc
	 * The magic is 0x03020100 for gx6605s-intc
	 */
	return res | magic;
}

static inline void setup_irq_channel(u32 magic, void __iomem *reg_addr)
{
	u32 i;

	/* Setup 64 channel slots */
	for (i = 0; i < INTC_IRQS; i += 4)
		writel(build_channel_val(i, magic), reg_addr + i);
}

static int __init
ck_intc_init_comm(struct device_node *node, struct device_node *parent)
{
	int ret;

	if (parent) {
		pr_err("C-SKY Intc not a root irq controller\n");
		return -EINVAL;
	}

	reg_base = of_iomap(node, 0);
	if (!reg_base) {
		pr_err("C-SKY Intc unable to map: %p.\n", node);
		return -EINVAL;
	}

	root_domain = irq_domain_add_linear(node, nr_irq,
					    &irq_generic_chip_ops, NULL);
	if (!root_domain) {
		pr_err("C-SKY Intc irq_domain_add failed.\n");
		return -ENOMEM;
	}

	ret = irq_alloc_domain_generic_chips(root_domain, 32, 1,
			"csky_intc", handle_level_irq,
			IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN, 0, 0);
	if (ret) {
		pr_err("C-SKY Intc irq_alloc_gc failed.\n");
		return -ENOMEM;
	}

	return 0;
}

static inline bool handle_irq_perbit(struct pt_regs *regs, u32 hwirq,
				     u32 irq_base)
{
	if (hwirq == 0)
		return 0;

	handle_domain_irq(root_domain, irq_base + __fls(hwirq), regs);

	return 1;
}

/* gx6605s 64 irqs interrupt controller */
static void gx_irq_handler(struct pt_regs *regs)
{
	bool ret;

retry:
	ret = handle_irq_perbit(regs,
			readl(reg_base + GX_INTC_PEN63_32), 32);
	if (ret)
		goto retry;

	ret = handle_irq_perbit(regs,
			readl(reg_base + GX_INTC_PEN31_00), 0);
	if (ret)
		goto retry;
}

static int __init
gx_intc_init(struct device_node *node, struct device_node *parent)
{
	int ret;

	ret = ck_intc_init_comm(node, parent);
	if (ret)
		return ret;

	/*
	 * Initial enable reg to disable all interrupts
	 */
	writel(0x0, reg_base + GX_INTC_NEN31_00);
	writel(0x0, reg_base + GX_INTC_NEN63_32);

	/*
	 * Initial mask reg with all unmasked, because we only use enable reg
	 */
	writel(0x0, reg_base + GX_INTC_NMASK31_00);
	writel(0x0, reg_base + GX_INTC_NMASK63_32);

	setup_irq_channel(0x03020100, reg_base + GX_INTC_SOURCE);

	ck_set_gc(node, reg_base, GX_INTC_NEN31_00, 0);
	ck_set_gc(node, reg_base, GX_INTC_NEN63_32, 32);

	set_handle_irq(gx_irq_handler);

	return 0;
}
IRQCHIP_DECLARE(csky_gx6605s_intc, "csky,gx6605s-intc", gx_intc_init);

/*
 * C-SKY simple 64 irqs interrupt controller, dual-together could support 128
 * irqs.
 */
static void ck_irq_handler(struct pt_regs *regs)
{
	bool ret;
	void __iomem *reg_pen_lo = reg_base + CK_INTC_PEN31_00;
	void __iomem *reg_pen_hi = reg_base + CK_INTC_PEN63_32;

retry:
	/* handle 0 - 63 irqs */
	ret = handle_irq_perbit(regs, readl(reg_pen_hi), 32);
	if (ret)
		goto retry;

	ret = handle_irq_perbit(regs, readl(reg_pen_lo), 0);
	if (ret)
		goto retry;

	if (nr_irq == INTC_IRQS)
		return;

	/* handle 64 - 127 irqs */
	ret = handle_irq_perbit(regs,
			readl(reg_pen_hi + CK_INTC_DUAL_BASE), 96);
	if (ret)
		goto retry;

	ret = handle_irq_perbit(regs,
			readl(reg_pen_lo + CK_INTC_DUAL_BASE), 64);
	if (ret)
		goto retry;
}

static int __init
ck_intc_init(struct device_node *node, struct device_node *parent)
{
	int ret;

	ret = ck_intc_init_comm(node, parent);
	if (ret)
		return ret;

	/* Initial enable reg to disable all interrupts */
	writel(0, reg_base + CK_INTC_NEN31_00);
	writel(0, reg_base + CK_INTC_NEN63_32);

	/* Enable irq intc */
	writel(BIT(31), reg_base + CK_INTC_ICR);

	ck_set_gc(node, reg_base, CK_INTC_NEN31_00, 0);
	ck_set_gc(node, reg_base, CK_INTC_NEN63_32, 32);

	setup_irq_channel(0x00010203, reg_base + CK_INTC_SOURCE);

	set_handle_irq(ck_irq_handler);

	return 0;
}
IRQCHIP_DECLARE(ck_intc, "csky,apb-intc", ck_intc_init);

static int __init
ck_dual_intc_init(struct device_node *node, struct device_node *parent)
{
	int ret;

	/* dual-apb-intc up to 128 irq sources*/
	nr_irq = INTC_IRQS * 2;

	ret = ck_intc_init(node, parent);
	if (ret)
		return ret;

	/* Initial enable reg to disable all interrupts */
	writel(0, reg_base + CK_INTC_NEN31_00 + CK_INTC_DUAL_BASE);
	writel(0, reg_base + CK_INTC_NEN63_32 + CK_INTC_DUAL_BASE);

	ck_set_gc(node, reg_base + CK_INTC_DUAL_BASE, CK_INTC_NEN31_00, 64);
	ck_set_gc(node, reg_base + CK_INTC_DUAL_BASE, CK_INTC_NEN63_32, 96);

	setup_irq_channel(0x00010203,
			  reg_base + CK_INTC_SOURCE + CK_INTC_DUAL_BASE);

	return 0;
}
IRQCHIP_DECLARE(ck_dual_intc, "csky,dual-apb-intc", ck_dual_intc_init);
