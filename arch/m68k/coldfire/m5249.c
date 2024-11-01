// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	m5249.c  -- platform support for ColdFire 5249 based boards
 *
 *	Copyright (C) 2002, Greg Ungerer (gerg@snapgear.com)
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

struct clk_lookup m5249_clk_lookup[] = {
	CLKDEV_INIT(NULL, "pll.0", &clk_pll),
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

#ifdef CONFIG_M5249C3

static struct resource m5249_smc91x_resources[] = {
	{
		.start		= 0xe0000300,
		.end		= 0xe0000300 + 0x100,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= MCF_IRQ_GPIO6,
		.end		= MCF_IRQ_GPIO6,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device m5249_smc91x = {
	.name			= "smc91x",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m5249_smc91x_resources),
	.resource		= m5249_smc91x_resources,
};

#endif /* CONFIG_M5249C3 */

static struct platform_device *m5249_devices[] __initdata = {
#ifdef CONFIG_M5249C3
	&m5249_smc91x,
#endif
};

/***************************************************************************/

static void __init m5249_qspi_init(void)
{
#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)
	/* QSPI irq setup */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL4 | MCFSIM_ICR_PRI0,
	       MCFSIM_QSPIICR);
	mcf_mapirq2imr(MCF_IRQ_QSPI, MCFINTC_QSPI);
#endif /* IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI) */
}

/***************************************************************************/

static void __init m5249_i2c_init(void)
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
#endif /* CONFIG_I2C_IMX */
}

/***************************************************************************/

#ifdef CONFIG_M5249C3

static void __init m5249_smc91x_init(void)
{
	u32  gpio;

	/* Set the GPIO line as interrupt source for smc91x device */
	gpio = readl(MCFSIM2_GPIOINTENABLE);
	writel(gpio | 0x40, MCFSIM2_GPIOINTENABLE);

	gpio = readl(MCFINTC2_INTPRI5);
	writel(gpio | 0x04000000, MCFINTC2_INTPRI5);
}

#endif /* CONFIG_M5249C3 */

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_sched_init = hw_timer_init;

#ifdef CONFIG_M5249C3
	m5249_smc91x_init();
#endif
	m5249_qspi_init();
	m5249_i2c_init();

	clkdev_add_table(m5249_clk_lookup, ARRAY_SIZE(m5249_clk_lookup));
}

/***************************************************************************/

static int __init init_BSP(void)
{
	platform_add_devices(m5249_devices, ARRAY_SIZE(m5249_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
