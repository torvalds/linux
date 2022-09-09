// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	m5206.c  -- platform support for ColdFire 5206 based boards
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 * 	Copyright (C) 2000-2001, Lineo Inc. (www.lineo.com) 
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

static struct clk_lookup m5206_clk_lookup[] = {
	CLKDEV_INIT(NULL, "pll.0", &clk_pll),
	CLKDEV_INIT(NULL, "sys.0", &clk_sys),
	CLKDEV_INIT("mcftmr.0", NULL, &clk_sys),
	CLKDEV_INIT("mcftmr.1", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.0", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.1", NULL, &clk_sys),
	CLKDEV_INIT("imx1-i2c.0", NULL, &clk_sys),
};

/***************************************************************************/

static void __init m5206_i2c_init(void)
{
#if IS_ENABLED(CONFIG_I2C_IMX)
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL5 | MCFSIM_ICR_PRI0,
	       MCFSIM_I2CICR);
	mcf_mapirq2imr(MCF_IRQ_I2C0, MCFINTC_I2C);
#endif /* IS_ENABLED(CONFIG_I2C_IMX) */
}

void __init config_BSP(char *commandp, int size)
{
#if defined(CONFIG_NETtel)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0004000, size);
	commandp[size-1] = 0;
#endif /* CONFIG_NETtel */

	mach_sched_init = hw_timer_init;

	/* Only support the external interrupts on their primary level */
	mcf_mapirq2imr(25, MCFINTC_EINT1);
	mcf_mapirq2imr(28, MCFINTC_EINT4);
	mcf_mapirq2imr(31, MCFINTC_EINT7);
	m5206_i2c_init();

	clkdev_add_table(m5206_clk_lookup, ARRAY_SIZE(m5206_clk_lookup));
}

/***************************************************************************/
