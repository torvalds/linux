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
	__raw_writew(0, FPGA_INTMSK1);
	/* mask SM501 interrupt */
	__raw_writew((__raw_readw(FPGA_INTMSK1) | 0x0002), FPGA_INTMSK1);
	/* enable all interrupt at FPGA */
	__raw_writew(0, FPGA_INTMSK2);

	/* set FPGA INTSEL register */
	/* FPGA + 0x06 */
	__raw_writew( ((IRQPIN_SM501 << IRQPOS_SM501) |
		(IRQPIN_SMC91CX << IRQPOS_SMC91CX)), FPGA_INTSEL1);

	/* FPGA + 0x08 */
	__raw_writew(((IRQPIN_EXTINT4 << IRQPOS_EXTINT4) |
		(IRQPIN_EXTINT3 << IRQPOS_EXTINT3) |
		(IRQPIN_EXTINT2 << IRQPOS_EXTINT2) |
		(IRQPIN_EXTINT1 << IRQPOS_EXTINT1)), FPGA_INTSEL2);

	/* FPGA + 0x0A */
	__raw_writew((IRQPIN_PCCPW << IRQPOS_PCCPW), FPGA_INTSEL3);

	plat_irq_setup_pins(IRQ_MODE_IRQ); /* install handlers for IRQ0-7 */

	/* ICR1: detect low level(for 2ndcut) */
	__raw_writel(0xAAAA0000, INTC_ICR1);

	/*
	 * FPGA PCISEL register initialize
	 *
	 *  CPU  || SLOT1 | SLOT2 | S-ATA | USB
	 *  -------------------------------------
	 *  INTA || INTA  | INTD  |  --   | INTB
	 *  -------------------------------------
	 *  INTB || INTB  | INTA  |  --   | INTC
	 *  -------------------------------------
	 *  INTC || INTC  | INTB  | INTA  |  --
	 *  -------------------------------------
	 *  INTD || INTD  | INTC  |  --   | INTA
	 *  -------------------------------------
	 */
	__raw_writew(0x0013, FPGA_PCI_INTSEL1);
	__raw_writew(0xE402, FPGA_PCI_INTSEL2);
}
