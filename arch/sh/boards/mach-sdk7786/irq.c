// SPDX-License-Identifier: GPL-2.0
/*
 * SDK7786 FPGA IRQ Controller Support.
 *
 * Copyright (C) 2010  Matt Fleming
 * Copyright (C) 2010  Paul Mundt
 */
#include <linux/irq.h>
#include <mach/fpga.h>
#include <mach/irq.h>

enum {
	ATA_IRQ_BIT		= 1,
	SPI_BUSY_BIT		= 2,
	LIRQ5_BIT		= 3,
	LIRQ6_BIT		= 4,
	LIRQ7_BIT		= 5,
	LIRQ8_BIT		= 6,
	KEY_IRQ_BIT		= 7,
	PEN_IRQ_BIT		= 8,
	ETH_IRQ_BIT		= 9,
	RTC_ALARM_BIT		= 10,
	CRYSTAL_FAIL_BIT	= 12,
	ETH_PME_BIT		= 14,
};

void __init sdk7786_init_irq(void)
{
	unsigned int tmp;

	/* Enable priority encoding for all IRLs */
	fpga_write_reg(fpga_read_reg(INTMSR) | 0x0303, INTMSR);

	/* Clear FPGA interrupt status registers */
	fpga_write_reg(0x0000, INTASR);
	fpga_write_reg(0x0000, INTBSR);

	/* Unmask FPGA interrupts */
	tmp = fpga_read_reg(INTAMR);
	tmp &= ~(1 << ETH_IRQ_BIT);
	fpga_write_reg(tmp, INTAMR);

	plat_irq_setup_pins(IRQ_MODE_IRL7654_MASK);
	plat_irq_setup_pins(IRQ_MODE_IRL3210_MASK);
}
