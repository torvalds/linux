// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	m523x.c  -- platform support for ColdFire 523x based boards
 *
 *	Sub-architcture dependent initialization code for the Freescale
 *	523x CPUs.
 *
 *	Copyright (C) 1999-2005, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/clkdev.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfclk.h>

/***************************************************************************/

DEFINE_CLK(pll, "pll.0", MCF_CLK);
DEFINE_CLK(sys, "sys.0", MCF_BUSCLK);

static struct clk_lookup m523x_clk_lookup[] = {
	CLKDEV_INIT(NULL, "pll.0", &clk_pll),
	CLKDEV_INIT(NULL, "sys.0", &clk_sys),
	CLKDEV_INIT("mcfpit.0", NULL, &clk_pll),
	CLKDEV_INIT("mcfpit.1", NULL, &clk_pll),
	CLKDEV_INIT("mcfpit.2", NULL, &clk_pll),
	CLKDEV_INIT("mcfpit.3", NULL, &clk_pll),
	CLKDEV_INIT("mcfuart.0", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.1", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.2", NULL, &clk_sys),
	CLKDEV_INIT("mcfqspi.0", NULL, &clk_sys),
	CLKDEV_INIT("fec.0", NULL, &clk_sys),
	CLKDEV_INIT("imx1-i2c.0", NULL, &clk_sys),
};

/***************************************************************************/

static void __init m523x_qspi_init(void)
{
#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)
	u16 par;

	/* setup QSPS pins for QSPI with gpio CS control */
	writeb(0x1f, MCFGPIO_PAR_QSPI);
	/* and CS2 & CS3 as gpio */
	par = readw(MCFGPIO_PAR_TIMER);
	par &= 0x3f3f;
	writew(par, MCFGPIO_PAR_TIMER);
#endif /* IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI) */
}

/***************************************************************************/

static void __init m523x_i2c_init(void)
{
#if IS_ENABLED(CONFIG_I2C_IMX)
	u8 par;

	/* setup Port AS Pin Assignment Register for I2C */
	/*  set PASPA0 to SCL and PASPA1 to SDA */
	par = readb(MCFGPIO_PAR_FECI2C);
	par |= 0x0f;
	writeb(par, MCFGPIO_PAR_FECI2C);
#endif /* IS_ENABLED(CONFIG_I2C_IMX) */
}

/***************************************************************************/

static void __init m523x_fec_init(void)
{
	/* Set multi-function pins to ethernet use */
	writeb(readb(MCFGPIO_PAR_FECI2C) | 0xf0, MCFGPIO_PAR_FECI2C);
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_sched_init = hw_timer_init;
	m523x_fec_init();
	m523x_qspi_init();
	m523x_i2c_init();

	clkdev_add_table(m523x_clk_lookup, ARRAY_SIZE(m523x_clk_lookup));
}

/***************************************************************************/
