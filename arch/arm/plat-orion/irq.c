/*
 * arch/arm/plat-orion/irq.c
 *
 * Marvell Orion SoC IRQ handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/exception.h>
#include <plat/irq.h>
#include <plat/orion-gpio.h>
#include <mach/bridge-regs.h>

#ifdef CONFIG_MULTI_IRQ_HANDLER
/*
 * Compiling with both non-DT and DT support enabled, will
 * break asm irq handler used by non-DT boards. Therefore,
 * we provide a C-style irq handler even for non-DT boards,
 * if MULTI_IRQ_HANDLER is set.
 *
 * Notes:
 * - this is prepared for Kirkwood and Dove only, update
 *   accordingly if you add Orion5x or MV78x00.
 * - Orion5x uses different macro names and has only one
 *   set of CAUSE/MASK registers.
 * - MV78x00 uses the same macro names but has a third
 *   set of CAUSE/MASK registers.
 *
 */

static void __iomem *orion_irq_base = IRQ_VIRT_BASE;

asmlinkage void
__exception_irq_entry orion_legacy_handle_irq(struct pt_regs *regs)
{
	u32 stat;

	stat = readl_relaxed(orion_irq_base + IRQ_CAUSE_LOW_OFF);
	stat &= readl_relaxed(orion_irq_base + IRQ_MASK_LOW_OFF);
	if (stat) {
		unsigned int hwirq = __fls(stat);
		handle_IRQ(hwirq, regs);
		return;
	}
	stat = readl_relaxed(orion_irq_base + IRQ_CAUSE_HIGH_OFF);
	stat &= readl_relaxed(orion_irq_base + IRQ_MASK_HIGH_OFF);
	if (stat) {
		unsigned int hwirq = 32 + __fls(stat);
		handle_IRQ(hwirq, regs);
		return;
	}
}
#endif

void __init orion_irq_init(unsigned int irq_start, void __iomem *maskaddr)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	/*
	 * Mask all interrupts initially.
	 */
	writel(0, maskaddr);

	gc = irq_alloc_generic_chip("orion_irq", 1, irq_start, maskaddr,
				    handle_level_irq);
	ct = gc->chip_types;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	irq_setup_generic_chip(gc, IRQ_MSK(32), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST, IRQ_LEVEL | IRQ_NOPROBE);

#ifdef CONFIG_MULTI_IRQ_HANDLER
	set_handle_irq(orion_legacy_handle_irq);
#endif
}

#ifdef CONFIG_OF
static int __init orion_add_irq_domain(struct device_node *np,
				       struct device_node *interrupt_parent)
{
	int i = 0;
	void __iomem *base;

	do {
		base = of_iomap(np, i);
		if (base) {
			orion_irq_init(i * 32, base + 0x04);
			i++;
		}
	} while (base);

	irq_domain_add_legacy(np, i * 32, 0, 0,
			      &irq_domain_simple_ops, NULL);
	return 0;
}

static const struct of_device_id orion_irq_match[] = {
	{ .compatible = "marvell,orion-intc",
	  .data = orion_add_irq_domain, },
	{},
};

void __init orion_dt_init_irq(void)
{
	of_irq_init(orion_irq_match);
}
#endif
