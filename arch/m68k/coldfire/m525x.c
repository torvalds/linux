// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	525x.c  -- platform support for ColdFire 525x based boards
 *
 *	Copyright (C) 2012, Steven King <sfking@fdwdc.com>
 */

/***************************************************************************/

#include <linux/clkdev.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfclk.h>

/***************************************************************************/

DEFINE_CLK(pll, "pll.0", MCF_CLK);
DEFINE_CLK(sys, "sys.0", MCF_BUSCLK);

static struct clk_lookup m525x_clk_lookup[] = {
	CLKDEV_INIT(NULL, "pll.0", &pll),
	CLKDEV_INIT(NULL, "sys.0", &clk_sys),
	CLKDEV_INIT("mcftmr.0", NULL, &clk_sys),
	CLKDEV_INIT("mcftmr.1", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.0", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.1", NULL, &clk_sys),
	CLKDEV_INIT("mcfqspi.0", NULL, &clk_sys),
	CLKDEV_INIT("imx1-i2c.0", NULL, &clk_sys),
	CLKDEV_INIT("imx1-i2c.1", NULL, &clk_sys),
};

/***************************************************************************/

static void __init m525x_qspi_init(void)
{
#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)
	/* set the GPIO function for the qspi cs gpios */
	/* FIXME: replace with pinmux/pinctl support */
	u32 f = readl(MCFSIM2_GPIOFUNC);
	f |= (1 << MCFQSPI_CS2) | (1 << MCFQSPI_CS1) | (1 << MCFQSPI_CS0);
	writel(f, MCFSIM2_GPIOFUNC);

	/* QSPI irq setup */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL4 | MCFSIM_ICR_PRI0,
	       MCFSIM_QSPIICR);
	mcf_mapirq2imr(MCF_IRQ_QSPI, MCFINTC_QSPI);
#endif /* IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI) */
}

static void __init m525x_i2c_init(void)
{
#if IS_ENABLED(CONFIG_I2C_IMX)
	u32 r;

	/* first I2C controller uses regular irq setup */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL5 | MCFSIM_ICR_PRI0,
	       MCFSIM_I2CICR);
	mcf_mapirq2imr(MCF_IRQ_I2C0, MCFINTC_I2C);

	/* second I2C controller is completely different */
	r = readl(MCFINTC2_INTPRI_REG(MCF_IRQ_I2C1));
	r &= ~MCFINTC2_INTPRI_BITS(0xf, MCF_IRQ_I2C1);
	r |= MCFINTC2_INTPRI_BITS(0x5, MCF_IRQ_I2C1);
	writel(r, MCFINTC2_INTPRI_REG(MCF_IRQ_I2C1));
#endif /* IS_ENABLED(CONFIG_I2C_IMX) */
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_sched_init = hw_timer_init;

	m525x_qspi_init();
	m525x_i2c_init();

	clkdev_add_table(m525x_clk_lookup, ARRAY_SIZE(m525x_clk_lookup));
}

/***************************************************************************/
