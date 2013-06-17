/* arch/arm/plat-samsung/watchdog-reset.c
 *
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Coyright (c) 2013 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * Watchdog reset support for Samsung SoCs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define S3C2410_WTCON			0x00
#define S3C2410_WTDAT			0x04
#define S3C2410_WTCNT			0x08

#define S3C2410_WTCON_ENABLE		(1 << 5)
#define S3C2410_WTCON_DIV16		(0 << 3)
#define S3C2410_WTCON_RSTEN		(1 << 0)
#define S3C2410_WTCON_PRESCALE(x)	((x) << 8)

static void __iomem *wdt_base;
static struct clk *wdt_clock;

void samsung_wdt_reset(void)
{
	if (!wdt_base) {
		pr_err("%s: wdt reset not initialized\n", __func__);
		/* delay to allow the serial port to show the message */
		mdelay(50);
		return;
	}

	if (!IS_ERR(wdt_clock))
		clk_prepare_enable(wdt_clock);

	/* disable watchdog, to be safe  */
	__raw_writel(0, wdt_base + S3C2410_WTCON);

	/* put initial values into count and data */
	__raw_writel(0x80, wdt_base + S3C2410_WTCNT);
	__raw_writel(0x80, wdt_base + S3C2410_WTDAT);

	/* set the watchdog to go and reset... */
	__raw_writel(S3C2410_WTCON_ENABLE | S3C2410_WTCON_DIV16 |
			S3C2410_WTCON_RSTEN | S3C2410_WTCON_PRESCALE(0x20),
			wdt_base + S3C2410_WTCON);

	/* wait for reset to assert... */
	mdelay(500);

	pr_err("Watchdog reset failed to assert reset\n");

	/* delay to allow the serial port to show the message */
	mdelay(50);
}

#ifdef CONFIG_OF
static const struct of_device_id s3c2410_wdt_match[] = {
	{ .compatible = "samsung,s3c2410-wdt" },
	{},
};

void __init samsung_wdt_reset_of_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, s3c2410_wdt_match);
	if (!np) {
		pr_err("%s: failed to find watchdog node\n", __func__);
		return;
	}

	wdt_base = of_iomap(np, 0);
	if (!wdt_base) {
		pr_err("%s: failed to map watchdog registers\n", __func__);
		return;
	}

	wdt_clock = of_clk_get(np, 0);
}
#endif

void __init samsung_wdt_reset_init(void __iomem *base)
{
	wdt_base = base;
	wdt_clock = clk_get(NULL, "watchdog");
}
