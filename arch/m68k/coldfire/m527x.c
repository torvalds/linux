// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	m527x.c  -- platform support for ColdFire 527x based boards
 *
 *	Sub-architcture dependent initialization code for the Freescale
 *	5270/5271 and 5274/5275 CPUs.
 *
 *	Copyright (C) 1999-2004, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2004, SnapGear Inc. (www.snapgear.com)
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
#include <asm/mcfuart.h>
#include <asm/mcfclk.h>

/***************************************************************************/

DEFINE_CLK(pll, "pll.0", MCF_CLK);
DEFINE_CLK(sys, "sys.0", MCF_BUSCLK);

static struct clk_lookup m527x_clk_lookup[] = {
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
	CLKDEV_INIT("fec.1", NULL, &clk_sys),
	CLKDEV_INIT("imx1-i2c.0", NULL, &clk_sys),
};

/***************************************************************************/

static void __init m527x_qspi_init(void)
{
#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)
#if defined(CONFIG_M5271)
	u16 par;

	/* setup QSPS pins for QSPI with gpio CS control */
	writeb(0x1f, MCFGPIO_PAR_QSPI);
	/* and CS2 & CS3 as gpio */
	par = readw(MCFGPIO_PAR_TIMER);
	par &= 0x3f3f;
	writew(par, MCFGPIO_PAR_TIMER);
#elif defined(CONFIG_M5275)
	/* setup QSPS pins for QSPI with gpio CS control */
	writew(0x003e, MCFGPIO_PAR_QSPI);
#endif
#endif /* IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI) */
}

/***************************************************************************/

static void __init m527x_i2c_init(void)
{
#if IS_ENABLED(CONFIG_I2C_IMX)
#if defined(CONFIG_M5271)
	u8 par;

	/* setup Port FECI2C Pin Assignment Register for I2C */
	/*  set PAR_SCL to SCL and PAR_SDA to SDA */
	par = readb(MCFGPIO_PAR_FECI2C);
	par |= 0x0f;
	writeb(par, MCFGPIO_PAR_FECI2C);
#elif defined(CONFIG_M5275)
	u16 par;

	/* setup Port FECI2C Pin Assignment Register for I2C */
	/*  set PAR_SCL to SCL and PAR_SDA to SDA */
	par = readw(MCFGPIO_PAR_FECI2C);
	par |= 0x0f;
	writew(par, MCFGPIO_PAR_FECI2C);
#endif
#endif /* IS_ENABLED(CONFIG_I2C_IMX) */
}

/***************************************************************************/

static void __init m527x_uarts_init(void)
{
	u16 sepmask;

	/*
	 * External Pin Mask Setting & Enable External Pin for Interface
	 */
	sepmask = readw(MCFGPIO_PAR_UART);
	sepmask |= UART0_ENABLE_MASK | UART1_ENABLE_MASK | UART2_ENABLE_MASK;
	writew(sepmask, MCFGPIO_PAR_UART);
}

/***************************************************************************/

static void __init m527x_fec_init(void)
{
	u8 v;

	/* Set multi-function pins to ethernet mode for fec0 */
#if defined(CONFIG_M5271)
	v = readb(MCFGPIO_PAR_FECI2C);
	writeb(v | 0xf0, MCFGPIO_PAR_FECI2C);
#else
	u16 par;

	par = readw(MCFGPIO_PAR_FECI2C);
	writew(par | 0xf00, MCFGPIO_PAR_FECI2C);
	v = readb(MCFGPIO_PAR_FEC0HL);
	writeb(v | 0xc0, MCFGPIO_PAR_FEC0HL);

	/* Set multi-function pins to ethernet mode for fec1 */
	par = readw(MCFGPIO_PAR_FECI2C);
	writew(par | 0xa0, MCFGPIO_PAR_FECI2C);
	v = readb(MCFGPIO_PAR_FEC1HL);
	writeb(v | 0xc0, MCFGPIO_PAR_FEC1HL);
#endif
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_sched_init = hw_timer_init;
	m527x_uarts_init();
	m527x_fec_init();
	m527x_qspi_init();
	m527x_i2c_init();
	clkdev_add_table(m527x_clk_lookup, ARRAY_SIZE(m527x_clk_lookup));
}

/***************************************************************************/
