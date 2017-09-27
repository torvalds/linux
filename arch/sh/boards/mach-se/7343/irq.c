/*
 * Hitachi UL SolutionEngine 7343 FPGA IRQ Support.
 *
 * Copyright (C) 2008  Yoshihiro Shimoda
 * Copyright (C) 2012  Paul Mundt
 *
 * Based on linux/arch/sh/boards/se/7343/irq.c
 * Copyright (C) 2007  Nobuhiro Iwamatsu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define DRV_NAME "SE7343-FPGA"
#define pr_fmt(fmt) DRV_NAME ": " fmt

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <asm/sizes.h>
#include <mach-se/mach/se7343.h>

#define PA_CPLD_BASE_ADDR	0x11400000
#define PA_CPLD_ST_REG		0x08	/* CPLD Interrupt status register */
#define PA_CPLD_IMSK_REG	0x0a	/* CPLD Interrupt mask register */

static void __iomem *se7343_irq_regs;
struct irq_domain *se7343_irq_domain;

static void se7343_irq_demux(struct irq_desc *desc)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	unsigned long mask;
	int bit;

	chip->irq_mask_ack(data);

	mask = ioread16(se7343_irq_regs + PA_CPLD_ST_REG);

	for_each_set_bit(bit, &mask, SE7343_FPGA_IRQ_NR)
		generic_handle_irq(irq_linear_revmap(se7343_irq_domain, bit));

	chip->irq_unmask(data);
}

static void __init se7343_domain_init(void)
{
	int i;

	se7343_irq_domain = irq_domain_add_linear(NULL, SE7343_FPGA_IRQ_NR,
						  &irq_domain_simple_ops, NULL);
	if (unlikely(!se7343_irq_domain)) {
		printk("Failed to get IRQ domain\n");
		return;
	}

	for (i = 0; i < SE7343_FPGA_IRQ_NR; i++) {
		int irq = irq_create_mapping(se7343_irq_domain, i);

		if (unlikely(irq == 0)) {
			printk("Failed to allocate IRQ %d\n", i);
			return;
		}
	}
}

static void __init se7343_gc_init(void)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	unsigned int irq_base;

	irq_base = irq_linear_revmap(se7343_irq_domain, 0);

	gc = irq_alloc_generic_chip(DRV_NAME, 1, irq_base, se7343_irq_regs,
				    handle_level_irq);
	if (unlikely(!gc))
		return;

	ct = gc->chip_types;
	ct->chip.irq_mask = irq_gc_mask_set_bit;
	ct->chip.irq_unmask = irq_gc_mask_clr_bit;

	ct->regs.mask = PA_CPLD_IMSK_REG;

	irq_setup_generic_chip(gc, IRQ_MSK(SE7343_FPGA_IRQ_NR),
			       IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST | IRQ_NOPROBE, 0);

	irq_set_chained_handler(IRQ0_IRQ, se7343_irq_demux);
	irq_set_irq_type(IRQ0_IRQ, IRQ_TYPE_LEVEL_LOW);

	irq_set_chained_handler(IRQ1_IRQ, se7343_irq_demux);
	irq_set_irq_type(IRQ1_IRQ, IRQ_TYPE_LEVEL_LOW);

	irq_set_chained_handler(IRQ4_IRQ, se7343_irq_demux);
	irq_set_irq_type(IRQ4_IRQ, IRQ_TYPE_LEVEL_LOW);

	irq_set_chained_handler(IRQ5_IRQ, se7343_irq_demux);
	irq_set_irq_type(IRQ5_IRQ, IRQ_TYPE_LEVEL_LOW);
}

/*
 * Initialize IRQ setting
 */
void __init init_7343se_IRQ(void)
{
	se7343_irq_regs = ioremap(PA_CPLD_BASE_ADDR, SZ_16);
	if (unlikely(!se7343_irq_regs)) {
		pr_err("Failed to remap CPLD\n");
		return;
	}

	/*
	 * All FPGA IRQs disabled by default
	 */
	iowrite16(0, se7343_irq_regs + PA_CPLD_IMSK_REG);

	__raw_writew(0x2000, 0xb03fffec);	/* mrshpc irq enable */

	se7343_domain_init();
	se7343_gc_init();
}
