// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	m5272.c  -- platform support for ColdFire 5272 based boards
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2002, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/clkdev.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/mcfclk.h>

/***************************************************************************/

/*
 *	Some platforms need software versions of the GPIO data registers.
 */
unsigned short ppdata;
unsigned char ledbank = 0xff;

/***************************************************************************/

DEFINE_CLK(pll, "pll.0", MCF_CLK);
DEFINE_CLK(sys, "sys.0", MCF_BUSCLK);

static struct clk_lookup m5272_clk_lookup[] = {
	CLKDEV_INIT(NULL, "pll.0", &clk_pll),
	CLKDEV_INIT(NULL, "sys.0", &clk_sys),
	CLKDEV_INIT("mcftmr.0", NULL, &clk_sys),
	CLKDEV_INIT("mcftmr.1", NULL, &clk_sys),
	CLKDEV_INIT("mcftmr.2", NULL, &clk_sys),
	CLKDEV_INIT("mcftmr.3", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.0", NULL, &clk_sys),
	CLKDEV_INIT("mcfuart.1", NULL, &clk_sys),
	CLKDEV_INIT("mcfqspi.0", NULL, &clk_sys),
	CLKDEV_INIT("fec.0", NULL, &clk_sys),
};

/***************************************************************************/

static void __init m5272_uarts_init(void)
{
	u32 v;

	/* Enable the output lines for the serial ports */
	v = readl(MCFSIM_PBCNT);
	v = (v & ~0x000000ff) | 0x00000055;
	writel(v, MCFSIM_PBCNT);

	v = readl(MCFSIM_PDCNT);
	v = (v & ~0x000003fc) | 0x000002a8;
	writel(v, MCFSIM_PDCNT);
}

/***************************************************************************/

static void m5272_cpu_reset(void)
{
	local_irq_disable();
	/* Set watchdog to reset, and enabled */
	__raw_writew(0, MCFSIM_WIRR);
	__raw_writew(1, MCFSIM_WRRR);
	__raw_writew(0, MCFSIM_WCR);
	for (;;)
		/* wait for watchdog to timeout */;
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
#if defined (CONFIG_MOD5272)
	/* Set base of device vectors to be 64 */
	writeb(0x40, MCFSIM_PIVR);
#endif

#if defined(CONFIG_NETtel) || defined(CONFIG_SCALES)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0004000, size);
	commandp[size-1] = 0;
#elif defined(CONFIG_CANCam)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0010000, size);
	commandp[size-1] = 0;
#endif

	mach_reset = m5272_cpu_reset;
	mach_sched_init = hw_timer_init;
}

/***************************************************************************/

/*
 * Some 5272 based boards have the FEC ethernet directly connected to
 * an ethernet switch. In this case we need to use the fixed phy type,
 * and we need to declare it early in boot.
 */
static const struct fixed_phy_status nettel_fixed_phy_status __initconst = {
	.link	= 1,
	.speed	= 100,
	.duplex	= 0,
};

/***************************************************************************/

static int __init init_BSP(void)
{
	m5272_uarts_init();
	fixed_phy_add(&nettel_fixed_phy_status);
	clkdev_add_table(m5272_clk_lookup, ARRAY_SIZE(m5272_clk_lookup));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
