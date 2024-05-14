// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	m54xx.c  -- platform support for ColdFire 54xx based boards
 *
 *	Copyright (C) 2010, Philippe De Muyter <phdm@macqel.be>
 */

/***************************************************************************/

#include <linux/clkdev.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/clk.h>
#include <linux/memblock.h>
#include <asm/pgalloc.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/m54xxsim.h>
#include <asm/mcfuart.h>
#include <asm/mcfclk.h>
#include <asm/m54xxgpt.h>
#ifdef CONFIG_MMU
#include <asm/mmu_context.h>
#endif

/***************************************************************************/

DEFINE_CLK(pll, "pll.0", MCF_CLK);
DEFINE_CLK(sys, "sys.0", MCF_BUSCLK);

static struct clk_lookup m54xx_clk_lookup[] = {
	CLKDEV_INIT(NULL, "pll.0", &clk_pll),
	CLKDEV_INIT(NULL, "sys.0", &clk_sys),
	CLKDEV_INIT("mcfslt.0", NULL, &clk_sys),
	CLKDEV_INIT("mcfslt.1", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.0", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.1", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.2", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.3", NULL, &clk_sys),
	CLKDEV_INIT("imx1-i2c.0", NULL, &clk_sys),
};

/***************************************************************************/

static void __init m54xx_uarts_init(void)
{
	/* enable io pins */
	__raw_writeb(MCF_PAR_PSC_TXD | MCF_PAR_PSC_RXD, MCFGPIO_PAR_PSC0);
	__raw_writeb(MCF_PAR_PSC_TXD | MCF_PAR_PSC_RXD | MCF_PAR_PSC_RTS_RTS,
		MCFGPIO_PAR_PSC1);
	__raw_writeb(MCF_PAR_PSC_TXD | MCF_PAR_PSC_RXD | MCF_PAR_PSC_RTS_RTS |
		MCF_PAR_PSC_CTS_CTS, MCFGPIO_PAR_PSC2);
	__raw_writeb(MCF_PAR_PSC_TXD | MCF_PAR_PSC_RXD, MCFGPIO_PAR_PSC3);
}

/***************************************************************************/

static void __init m54xx_i2c_init(void)
{
#if IS_ENABLED(CONFIG_I2C_IMX)
	u32 r;

	/* set the fec/i2c/irq pin assignment register for i2c */
	r = readl(MCF_PAR_FECI2CIRQ);
	r |= MCF_PAR_FECI2CIRQ_SDA | MCF_PAR_FECI2CIRQ_SCL;
	writel(r, MCF_PAR_FECI2CIRQ);
#endif /* IS_ENABLED(CONFIG_I2C_IMX) */
}

/***************************************************************************/

static void mcf54xx_reset(void)
{
	/* disable interrupts and enable the watchdog */
	asm("movew #0x2700, %sr\n");
	__raw_writel(0, MCF_GPT_GMS0);
	__raw_writel(MCF_GPT_GCIR_CNT(1), MCF_GPT_GCIR0);
	__raw_writel(MCF_GPT_GMS_WDEN | MCF_GPT_GMS_CE | MCF_GPT_GMS_TMS(4),
		MCF_GPT_GMS0);
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_reset = mcf54xx_reset;
	mach_sched_init = hw_timer_init;
	m54xx_uarts_init();
	m54xx_i2c_init();

	clkdev_add_table(m54xx_clk_lookup, ARRAY_SIZE(m54xx_clk_lookup));
}

/***************************************************************************/
