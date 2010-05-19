/*
 * Copyright 2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <mach/system.h>
#include <mach/cns3xxx.h>

void cns3xxx_pwr_clk_en(unsigned int block)
{
	PM_CLK_GATE_REG |= (block & PM_CLK_GATE_REG_MASK);
}

void cns3xxx_pwr_power_up(unsigned int block)
{
	PM_PLL_HM_PD_CTRL_REG &= ~(block & CNS3XXX_PWR_PLL_ALL);

	/* Wait for 300us for the PLL output clock locked. */
	udelay(300);
};

void cns3xxx_pwr_power_down(unsigned int block)
{
	/* write '1' to power down */
	PM_PLL_HM_PD_CTRL_REG |= (block & CNS3XXX_PWR_PLL_ALL);
};

static void cns3xxx_pwr_soft_rst_force(unsigned int block)
{
	/*
	 * bit 0, 28, 29 => program low to reset,
	 * the other else program low and then high
	 */
	if (block & 0x30000001) {
		PM_SOFT_RST_REG &= ~(block & PM_SOFT_RST_REG_MASK);
	} else {
		PM_SOFT_RST_REG &= ~(block & PM_SOFT_RST_REG_MASK);
		PM_SOFT_RST_REG |= (block & PM_SOFT_RST_REG_MASK);
	}
}

void cns3xxx_pwr_soft_rst(unsigned int block)
{
	static unsigned int soft_reset;

	if (soft_reset & block) {
		/* SPI/I2C/GPIO use the same block, reset once. */
		return;
	} else {
		soft_reset |= block;
	}
	cns3xxx_pwr_soft_rst_force(block);
}

void arch_reset(char mode, const char *cmd)
{
	/*
	 * To reset, we hit the on-board reset register
	 * in the system FPGA.
	 */
	cns3xxx_pwr_soft_rst(CNS3XXX_PWR_SOFTWARE_RST(GLOBAL));
}

/*
 * cns3xxx_cpu_clock - return CPU/L2 clock
 *  aclk: cpu clock/2
 *  hclk: cpu clock/4
 *  pclk: cpu clock/8
 */
int cns3xxx_cpu_clock(void)
{
	int cpu;
	int cpu_sel;
	int div_sel;

	cpu_sel = (PM_CLK_CTRL_REG >> PM_CLK_CTRL_REG_OFFSET_PLL_CPU_SEL) & 0xf;
	div_sel = (PM_CLK_CTRL_REG >> PM_CLK_CTRL_REG_OFFSET_CPU_CLK_DIV) & 0x3;

	cpu = (300 + ((cpu_sel / 3) * 100) + ((cpu_sel % 3) * 33)) >> div_sel;

	return cpu;
}
