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
}
