/*
 * linux/arch/arm/mach-omap2/irq.c
 *
 * Interrupt handler for OMAP2 boards.
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <asm/exception.h>
#include <asm/mach/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "soc.h"
#include "iomap.h"
#include "common.h"

/* selected INTC register offsets */

#define INTC_REVISION		0x0000
#define INTC_SYSCONFIG		0x0010
#define INTC_SYSSTATUS		0x0014
#define INTC_SIR		0x0040
#define INTC_CONTROL		0x0048
#define INTC_PROTECTION		0x004C
#define INTC_IDLE		0x0050
#define INTC_THRESHOLD		0x0068
#define INTC_MIR0		0x0084
#define INTC_MIR_CLEAR0		0x0088
#define INTC_MIR_SET0		0x008c
#define INTC_PENDING_IRQ0	0x0098
#define INTC_PENDING_IRQ1	0x00b8
#define INTC_PENDING_IRQ2	0x00d8
#define INTC_PENDING_IRQ3	0x00f8
#define INTC_ILR0		0x0100

#define ACTIVEIRQ_MASK		0x7f	/* omap2/3 active interrupt bits */
#define INTCPS_NR_ILR_REGS	128
#define INTCPS_NR_MIR_REGS	3

/*
 * OMAP2 has a number of different interrupt controllers, each interrupt
 * controller is identified as its own "bank". Register definitions are
 * fairly consistent for each bank, but not all registers are implemented
 * for each bank.. when in doubt, consult the TRM.
 */

static struct irq_domain *domain;
static void __iomem *omap_irq_base;
static int omap_nr_irqs = 96;

/* Structure to save interrupt controller context */
struct omap_intc_regs {
	u32 sysconfig;
	u32 protection;
	u32 idle;
	u32 threshold;
	u32 ilr[INTCPS_NR_ILR_REGS];
	u32 mir[INTCPS_NR_MIR_REGS];
};

/* INTC bank register get/set */
static void intc_writel(u32 reg, u32 val)
{
	writel_relaxed(val, omap_irq_base + reg);
}

static u32 intc_readl(u32 reg)
{
	return readl_relaxed(omap_irq_base + reg);
}

/* XXX: FIQ and additional INTC support (only MPU at the moment) */
static void omap_ack_irq(struct irq_data *d)
{
	intc_writel(INTC_CONTROL, 0x1);
}

static void omap_mask_ack_irq(struct irq_data *d)
{
	irq_gc_mask_disable_reg(d);
	omap_ack_irq(d);
}

static void __init omap_irq_soft_reset(void)
{
	unsigned long tmp;

	tmp = intc_readl(INTC_REVISION) & 0xff;

	pr_info("IRQ: Found an INTC at 0x%p (revision %ld.%ld) with %d interrupts\n",
		omap_irq_base, tmp >> 4, tmp & 0xf, omap_nr_irqs);

	tmp = intc_readl(INTC_SYSCONFIG);
	tmp |= 1 << 1;	/* soft reset */
	intc_writel(INTC_SYSCONFIG, tmp);

	while (!(intc_readl(INTC_SYSSTATUS) & 0x1))
		/* Wait for reset to complete */;

	/* Enable autoidle */
	intc_writel(INTC_SYSCONFIG, 1 << 0);
}

int omap_irq_pending(void)
{
	int irq;

	for (irq = 0; irq < omap_nr_irqs; irq += 32)
		if (intc_readl(INTC_PENDING_IRQ0 +
					((irq >> 5) << 5)))
			return 1;
	return 0;
}

static __init void
omap_alloc_gc(void __iomem *base, unsigned int irq_start, unsigned int num)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	gc = irq_alloc_generic_chip("INTC", 1, irq_start, base,
					handle_level_irq);
	ct = gc->chip_types;
	ct->chip.irq_ack = omap_mask_ack_irq;
	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
	ct->chip.flags |= IRQCHIP_SKIP_SET_WAKE;

	ct->regs.enable = INTC_MIR_CLEAR0;
	ct->regs.disable = INTC_MIR_SET0;
	irq_setup_generic_chip(gc, IRQ_MSK(num), IRQ_GC_INIT_MASK_CACHE,
				IRQ_NOREQUEST | IRQ_NOPROBE, 0);
}

static void __init omap_init_irq(u32 base, int nr_irqs,
				 struct device_node *node)
{
	int j, irq_base;

	omap_irq_base = ioremap(base, SZ_4K);
	if (WARN_ON(!omap_irq_base))
		return;

	omap_nr_irqs = nr_irqs;

	irq_base = irq_alloc_descs(-1, 0, nr_irqs, 0);
	if (irq_base < 0) {
		pr_warn("Couldn't allocate IRQ numbers\n");
		irq_base = 0;
	}

	domain = irq_domain_add_legacy(node, nr_irqs, irq_base, 0,
			&irq_domain_simple_ops, NULL);

	omap_irq_soft_reset();

	for (j = 0; j < omap_nr_irqs; j += 32)
		omap_alloc_gc(omap_irq_base + j, j + irq_base, 32);
}

void __init omap2_init_irq(void)
{
	omap_init_irq(OMAP24XX_IC_BASE, 96, NULL);
}

void __init omap3_init_irq(void)
{
	omap_init_irq(OMAP34XX_IC_BASE, 96, NULL);
}

void __init ti81xx_init_irq(void)
{
	omap_init_irq(OMAP34XX_IC_BASE, 128, NULL);
}

static inline void omap_intc_handle_irq(struct pt_regs *regs)
{
	u32 irqnr;
	int handled_irq = 0;

	do {
		irqnr = intc_readl(INTC_PENDING_IRQ0);
		if (irqnr)
			goto out;

		irqnr = intc_readl(INTC_PENDING_IRQ1);
		if (irqnr)
			goto out;

		irqnr = intc_readl(INTC_PENDING_IRQ2);
#if IS_ENABLED(CONFIG_SOC_TI81XX) || IS_ENABLED(CONFIG_SOC_AM33XX)
		if (irqnr)
			goto out;
		irqnr = intc_readl(INTC_PENDING_IRQ3);
#endif

out:
		if (!irqnr)
			break;

		irqnr = intc_readl(INTC_SIR);
		irqnr &= ACTIVEIRQ_MASK;

		if (irqnr) {
			irqnr = irq_find_mapping(domain, irqnr);
			handle_IRQ(irqnr, regs);
			handled_irq = 1;
		}
	} while (irqnr);

	/* If an irq is masked or deasserted while active, we will
	 * keep ending up here with no irq handled. So remove it from
	 * the INTC with an ack.*/
	if (!handled_irq)
		omap_ack_irq(NULL);
}

asmlinkage void __exception_irq_entry omap2_intc_handle_irq(struct pt_regs *regs)
{
	omap_intc_handle_irq(regs);
}

int __init intc_of_init(struct device_node *node,
			     struct device_node *parent)
{
	struct resource res;
	u32 nr_irq = 96;

	if (WARN_ON(!node))
		return -ENODEV;

	if (of_address_to_resource(node, 0, &res)) {
		WARN(1, "unable to get intc registers\n");
		return -EINVAL;
	}

	if (of_property_read_u32(node, "ti,intc-size", &nr_irq))
		pr_warn("unable to get intc-size, default to %d\n", nr_irq);

	omap_init_irq(res.start, nr_irq, of_node_get(node));

	return 0;
}

static const struct of_device_id irq_match[] __initconst = {
	{ .compatible = "ti,omap2-intc", .data = intc_of_init, },
	{ }
};

void __init omap_intc_of_init(void)
{
	of_irq_init(irq_match);
}

static struct omap_intc_regs intc_context;

void omap_intc_save_context(void)
{
	int i;

	intc_context.sysconfig =
		intc_readl(INTC_SYSCONFIG);
	intc_context.protection =
		intc_readl(INTC_PROTECTION);
	intc_context.idle =
		intc_readl(INTC_IDLE);
	intc_context.threshold =
		intc_readl(INTC_THRESHOLD);

	for (i = 0; i < omap_nr_irqs; i++)
		intc_context.ilr[i] =
			intc_readl((INTC_ILR0 + 0x4 * i));
	for (i = 0; i < INTCPS_NR_MIR_REGS; i++)
		intc_context.mir[i] =
			intc_readl(INTC_MIR0 + (0x20 * i));
}

void omap_intc_restore_context(void)
{
	int i;

	intc_writel(INTC_SYSCONFIG, intc_context.sysconfig);
	intc_writel(INTC_PROTECTION, intc_context.protection);
	intc_writel(INTC_IDLE, intc_context.idle);
	intc_writel(INTC_THRESHOLD, intc_context.threshold);

	for (i = 0; i < omap_nr_irqs; i++)
		intc_writel(INTC_ILR0 + 0x4 * i,
				intc_context.ilr[i]);

	for (i = 0; i < INTCPS_NR_MIR_REGS; i++)
		intc_writel(INTC_MIR0 + 0x20 * i,
			intc_context.mir[i]);
	/* MIRs are saved and restore with other PRCM registers */
}

void omap3_intc_suspend(void)
{
	/* A pending interrupt would prevent OMAP from entering suspend */
	omap_ack_irq(NULL);
}

void omap3_intc_prepare_idle(void)
{
	/*
	 * Disable autoidle as it can stall interrupt controller,
	 * cf. errata ID i540 for 3430 (all revisions up to 3.1.x)
	 */
	intc_writel(INTC_SYSCONFIG, 0);
}

void omap3_intc_resume_idle(void)
{
	/* Re-enable autoidle */
	intc_writel(INTC_SYSCONFIG, 1);
}

asmlinkage void __exception_irq_entry omap3_intc_handle_irq(struct pt_regs *regs)
{
	omap_intc_handle_irq(regs);
}
