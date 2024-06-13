// SPDX-License-Identifier: GPL-2.0
/*
 * linux/arch/sh/boards/se/7724/irq.c
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 *
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Based on  linux/arch/sh/boards/se/7722/irq.c
 * Copyright (C) 2007  Nobuhiro Iwamatsu
 *
 * Hitachi UL SolutionEngine 7724 Support.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/topology.h>
#include <linux/io.h>
#include <linux/err.h>
#include <mach-se/mach/se7724.h>

struct fpga_irq {
	unsigned long  sraddr;
	unsigned long  mraddr;
	unsigned short mask;
	unsigned int   base;
};

static unsigned int fpga2irq(unsigned int irq)
{
	if (irq >= IRQ0_BASE &&
	    irq <= IRQ0_END)
		return IRQ0_IRQ;
	else if (irq >= IRQ1_BASE &&
		 irq <= IRQ1_END)
		return IRQ1_IRQ;
	else
		return IRQ2_IRQ;
}

static struct fpga_irq get_fpga_irq(unsigned int irq)
{
	struct fpga_irq set;

	switch (irq) {
	case IRQ0_IRQ:
		set.sraddr = IRQ0_SR;
		set.mraddr = IRQ0_MR;
		set.mask   = IRQ0_MASK;
		set.base   = IRQ0_BASE;
		break;
	case IRQ1_IRQ:
		set.sraddr = IRQ1_SR;
		set.mraddr = IRQ1_MR;
		set.mask   = IRQ1_MASK;
		set.base   = IRQ1_BASE;
		break;
	default:
		set.sraddr = IRQ2_SR;
		set.mraddr = IRQ2_MR;
		set.mask   = IRQ2_MASK;
		set.base   = IRQ2_BASE;
		break;
	}

	return set;
}

static void disable_se7724_irq(struct irq_data *data)
{
	unsigned int irq = data->irq;
	struct fpga_irq set = get_fpga_irq(fpga2irq(irq));
	unsigned int bit = irq - set.base;
	__raw_writew(__raw_readw(set.mraddr) | 0x0001 << bit, set.mraddr);
}

static void enable_se7724_irq(struct irq_data *data)
{
	unsigned int irq = data->irq;
	struct fpga_irq set = get_fpga_irq(fpga2irq(irq));
	unsigned int bit = irq - set.base;
	__raw_writew(__raw_readw(set.mraddr) & ~(0x0001 << bit), set.mraddr);
}

static struct irq_chip se7724_irq_chip __read_mostly = {
	.name		= "SE7724-FPGA",
	.irq_mask	= disable_se7724_irq,
	.irq_unmask	= enable_se7724_irq,
};

static void se7724_irq_demux(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct fpga_irq set = get_fpga_irq(irq);
	unsigned short intv = __raw_readw(set.sraddr);
	unsigned int ext_irq = set.base;

	intv &= set.mask;

	for (; intv; intv >>= 1, ext_irq++) {
		if (!(intv & 1))
			continue;

		generic_handle_irq(ext_irq);
	}
}

/*
 * Initialize IRQ setting
 */
void __init init_se7724_IRQ(void)
{
	int irq_base, i;

	__raw_writew(0xffff, IRQ0_MR);  /* mask all */
	__raw_writew(0xffff, IRQ1_MR);  /* mask all */
	__raw_writew(0xffff, IRQ2_MR);  /* mask all */
	__raw_writew(0x0000, IRQ0_SR);  /* clear irq */
	__raw_writew(0x0000, IRQ1_SR);  /* clear irq */
	__raw_writew(0x0000, IRQ2_SR);  /* clear irq */
	__raw_writew(0x002a, IRQ_MODE); /* set irq type */

	irq_base = irq_alloc_descs(SE7724_FPGA_IRQ_BASE, SE7724_FPGA_IRQ_BASE,
				   SE7724_FPGA_IRQ_NR, numa_node_id());
	if (IS_ERR_VALUE(irq_base)) {
		pr_err("%s: failed hooking irqs for FPGA\n", __func__);
		return;
	}

	for (i = 0; i < SE7724_FPGA_IRQ_NR; i++)
		irq_set_chip_and_handler_name(irq_base + i, &se7724_irq_chip,
					      handle_level_irq, "level");

	irq_set_chained_handler(IRQ0_IRQ, se7724_irq_demux);
	irq_set_irq_type(IRQ0_IRQ, IRQ_TYPE_LEVEL_LOW);

	irq_set_chained_handler(IRQ1_IRQ, se7724_irq_demux);
	irq_set_irq_type(IRQ1_IRQ, IRQ_TYPE_LEVEL_LOW);

	irq_set_chained_handler(IRQ2_IRQ, se7724_irq_demux);
	irq_set_irq_type(IRQ2_IRQ, IRQ_TYPE_LEVEL_LOW);
}
