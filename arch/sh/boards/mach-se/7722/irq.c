/*
 * linux/arch/sh/boards/se/7722/irq.c
 *
 * Copyright (C) 2007  Nobuhiro Iwamatsu
 *
 * Hitachi UL SolutionEngine 7722 Support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <mach-se/mach/se7722.h>

unsigned int se7722_fpga_irq[SE7722_FPGA_IRQ_NR] = { 0, };

static void disable_se7722_irq(unsigned int irq)
{
	unsigned int bit = (unsigned int)get_irq_chip_data(irq);
	ctrl_outw(ctrl_inw(IRQ01_MASK) | 1 << bit, IRQ01_MASK);
}

static void enable_se7722_irq(unsigned int irq)
{
	unsigned int bit = (unsigned int)get_irq_chip_data(irq);
	ctrl_outw(ctrl_inw(IRQ01_MASK) & ~(1 << bit), IRQ01_MASK);
}

static struct irq_chip se7722_irq_chip __read_mostly = {
	.name           = "SE7722-FPGA",
	.mask           = disable_se7722_irq,
	.unmask         = enable_se7722_irq,
	.mask_ack       = disable_se7722_irq,
};

static void se7722_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	unsigned short intv = ctrl_inw(IRQ01_STS);
	unsigned int ext_irq = 0;

	intv &= (1 << SE7722_FPGA_IRQ_NR) - 1;

	for (; intv; intv >>= 1, ext_irq++) {
		if (!(intv & 1))
			continue;

		generic_handle_irq(se7722_fpga_irq[ext_irq]);
	}
}

/*
 * Initialize IRQ setting
 */
void __init init_se7722_IRQ(void)
{
	int i, irq;

	ctrl_outw(0, IRQ01_MASK);       /* disable all irqs */
	ctrl_outw(0x2000, 0xb03fffec);  /* mrshpc irq enable */

	for (i = 0; i < SE7722_FPGA_IRQ_NR; i++) {
		irq = create_irq();
		if (irq < 0)
			return;
		se7722_fpga_irq[i] = irq;

		set_irq_chip_and_handler_name(se7722_fpga_irq[i],
					      &se7722_irq_chip,
					      handle_level_irq, "level");

		set_irq_chip_data(se7722_fpga_irq[i], (void *)i);
	}

	set_irq_chained_handler(IRQ0_IRQ, se7722_irq_demux);
	set_irq_type(IRQ0_IRQ, IRQ_TYPE_LEVEL_LOW);

	set_irq_chained_handler(IRQ1_IRQ, se7722_irq_demux);
	set_irq_type(IRQ1_IRQ, IRQ_TYPE_LEVEL_LOW);
}
