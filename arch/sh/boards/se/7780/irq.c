/*
 * linux/arch/sh/boards/se/7780/irq.c
 *
 * Copyright (C) 2006,2007  Nobuhiro Iwamatsu
 *
 * Hitachi UL SolutionEngine 7780 Support.
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
#include <asm/se7780.h>

static struct intc2_data intc2_irq_table[] = {
	{ 2,  0, 31, 0, 31, 3 }, /* daughter board EXTINT1 */
	{ 4,  0, 30, 0, 30, 3 }, /* daughter board EXTINT2 */
	{ 6,  0, 29, 0, 29, 3 }, /* daughter board EXTINT3 */
	{ 8,  0, 28, 0, 28, 3 }, /* SMC 91C111 (LAN) */
	{ 10, 0, 27, 0, 27, 3 }, /* daughter board EXTINT4 */
	{ 4,  0, 30, 0, 30, 3 }, /* daughter board EXTINT5 */
	{ 2,  0, 31, 0, 31, 3 }, /* daughter board EXTINT6 */
	{ 2,  0, 31, 0, 31, 3 }, /* daughter board EXTINT7 */
	{ 2,  0, 31, 0, 31, 3 }, /* daughter board EXTINT8 */
	{ 0 , 0, 24, 0, 24, 3 }, /* SM501 */
};

static struct intc2_desc intc2_irq_desc __read_mostly = {
	.prio_base	= 0, /* N/A */
	.msk_base	= 0xffd00044,
	.mskclr_base	= 0xffd00064,

	.intc2_data	= intc2_irq_table,
	.nr_irqs	= ARRAY_SIZE(intc2_irq_table),

	.chip = {
		.name	= "INTC2-se7780",
	},
};

/*
 * Initialize IRQ setting
 */
void __init init_se7780_IRQ(void)
{
	/* enable all interrupt at FPGA */
	ctrl_outw(0, FPGA_INTMSK1);
	/* mask SM501 interrupt */
	ctrl_outw((ctrl_inw(FPGA_INTMSK1) | 0x0002), FPGA_INTMSK1);
	/* enable all interrupt at FPGA */
	ctrl_outw(0, FPGA_INTMSK2);

	/* set FPGA INTSEL register */
	/* FPGA + 0x06 */
	ctrl_outw( ((IRQPIN_SM501 << IRQPOS_SM501) |
		(IRQPIN_SMC91CX << IRQPOS_SMC91CX)), FPGA_INTSEL1);

	/* FPGA + 0x08 */
	ctrl_outw(((IRQPIN_EXTINT4 << IRQPOS_EXTINT4) |
		(IRQPIN_EXTINT3 << IRQPOS_EXTINT3) |
		(IRQPIN_EXTINT2 << IRQPOS_EXTINT2) |
		(IRQPIN_EXTINT1 << IRQPOS_EXTINT1)), FPGA_INTSEL2);

	/* FPGA + 0x0A */
	ctrl_outw((IRQPIN_PCCPW << IRQPOS_PCCPW), FPGA_INTSEL3);

	register_intc2_controller(&intc2_irq_desc);
}
