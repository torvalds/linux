// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	m54xx.c  -- platform support for ColdFire 54xx based boards
 *
 *	Copyright (C) 2010, Philippe De Muyter <phdm@macqel.be>
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/clk.h>
#include <linux/bootmem.h>
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
DEFINE_CLK(mcfslt0, "mcfslt.0", MCF_BUSCLK);
DEFINE_CLK(mcfslt1, "mcfslt.1", MCF_BUSCLK);
DEFINE_CLK(mcfuart0, "mcfuart.0", MCF_BUSCLK);
DEFINE_CLK(mcfuart1, "mcfuart.1", MCF_BUSCLK);
DEFINE_CLK(mcfuart2, "mcfuart.2", MCF_BUSCLK);
DEFINE_CLK(mcfuart3, "mcfuart.3", MCF_BUSCLK);
DEFINE_CLK(mcfi2c0, "imx1-i2c.0", MCF_BUSCLK);

struct clk *mcf_clks[] = {
	&clk_pll,
	&clk_sys,
	&clk_mcfslt0,
	&clk_mcfslt1,
	&clk_mcfuart0,
	&clk_mcfuart1,
	&clk_mcfuart2,
	&clk_mcfuart3,
	&clk_mcfi2c0,
	NULL
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
}

/***************************************************************************/
