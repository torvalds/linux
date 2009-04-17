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
#include <linux/irq.h>
#include <linux/io.h>
#include <mach-se/mach/se7780.h>

#define INTC_BASE	0xffd00000
#define INTC_ICR1	(INTC_BASE+0x1c)

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

	plat_irq_setup_pins(IRQ_MODE_IRQ); /* install handlers for IRQ0-7 */

	/* ICR1: detect low level(for 2ndcut) */
	ctrl_outl(0xAAAA0000, INTC_ICR1);
}
