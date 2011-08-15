/*
 * Interrupt handler for DaVinci boards.
 *
 * Copyright (C) 2006 Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/cputype.h>
#include <mach/common.h>
#include <asm/mach/irq.h>

#define FIQ_REG0_OFFSET		0x0000
#define FIQ_REG1_OFFSET		0x0004
#define IRQ_REG0_OFFSET		0x0008
#define IRQ_REG1_OFFSET		0x000C
#define IRQ_ENT_REG0_OFFSET	0x0018
#define IRQ_ENT_REG1_OFFSET	0x001C
#define IRQ_INCTL_REG_OFFSET	0x0020
#define IRQ_EABASE_REG_OFFSET	0x0024
#define IRQ_INTPRI0_REG_OFFSET	0x0030
#define IRQ_INTPRI7_REG_OFFSET	0x004C

static inline void davinci_irq_writel(unsigned long value, int offset)
{
	__raw_writel(value, davinci_intc_base + offset);
}

static __init void
davinci_alloc_gc(void __iomem *base, unsigned int irq_start, unsigned int num)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	gc = irq_alloc_generic_chip("AINTC", 1, irq_start, base, handle_edge_irq);
	if (!gc) {
		pr_err("%s: irq_alloc_generic_chip for IRQ %u failed\n",
		       __func__, irq_start);
		return;
	}

	ct = gc->chip_types;
	ct->chip.irq_ack = irq_gc_ack_set_bit;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;

	ct->regs.ack = IRQ_REG0_OFFSET;
	ct->regs.mask = IRQ_ENT_REG0_OFFSET;
	irq_setup_generic_chip(gc, IRQ_MSK(num), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST | IRQ_NOPROBE, 0);
}

/* ARM Interrupt Controller Initialization */
void __init davinci_irq_init(void)
{
	unsigned i, j;
	const u8 *davinci_def_priorities = davinci_soc_info.intc_irq_prios;

	davinci_intc_type = DAVINCI_INTC_TYPE_AINTC;
	davinci_intc_base = ioremap(davinci_soc_info.intc_base, SZ_4K);
	if (WARN_ON(!davinci_intc_base))
		return;

	/* Clear all interrupt requests */
	davinci_irq_writel(~0x0, FIQ_REG0_OFFSET);
	davinci_irq_writel(~0x0, FIQ_REG1_OFFSET);
	davinci_irq_writel(~0x0, IRQ_REG0_OFFSET);
	davinci_irq_writel(~0x0, IRQ_REG1_OFFSET);

	/* Disable all interrupts */
	davinci_irq_writel(0x0, IRQ_ENT_REG0_OFFSET);
	davinci_irq_writel(0x0, IRQ_ENT_REG1_OFFSET);

	/* Interrupts disabled immediately, IRQ entry reflects all */
	davinci_irq_writel(0x0, IRQ_INCTL_REG_OFFSET);

	/* we don't use the hardware vector table, just its entry addresses */
	davinci_irq_writel(0, IRQ_EABASE_REG_OFFSET);

	/* Clear all interrupt requests */
	davinci_irq_writel(~0x0, FIQ_REG0_OFFSET);
	davinci_irq_writel(~0x0, FIQ_REG1_OFFSET);
	davinci_irq_writel(~0x0, IRQ_REG0_OFFSET);
	davinci_irq_writel(~0x0, IRQ_REG1_OFFSET);

	for (i = IRQ_INTPRI0_REG_OFFSET; i <= IRQ_INTPRI7_REG_OFFSET; i += 4) {
		u32		pri;

		for (j = 0, pri = 0; j < 32; j += 4, davinci_def_priorities++)
			pri |= (*davinci_def_priorities & 0x07) << j;
		davinci_irq_writel(pri, i);
	}

	for (i = 0, j = 0; i < davinci_soc_info.intc_irq_num; i += 32, j += 0x04)
		davinci_alloc_gc(davinci_intc_base + j, i, 32);

	irq_set_handler(IRQ_TINT1_TINT34, handle_level_irq);
}
