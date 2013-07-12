/*
 * Copyright (C) 1999 ARM Limited
 * Copyright (C) 2000 Deep Blue Solutions Ltd
 * Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 * Copyright 2009 Ilya Yanok, Emcraft Systems Ltd, yanok@emcraft.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/system_misc.h>
#include <asm/proc-fns.h>
#include <asm/mach-types.h>

#include "common.h"
#include "hardware.h"

static void __iomem *wdog_base;
static struct clk *wdog_clk;

/*
 * Reset the system. It is called by machine_restart().
 */
void mxc_restart(enum reboot_mode mode, const char *cmd)
{
	unsigned int wcr_enable;

	if (wdog_clk)
		clk_enable(wdog_clk);

	if (cpu_is_mx1())
		wcr_enable = (1 << 0);
	else
		wcr_enable = (1 << 2);

	/* Assert SRS signal */
	__raw_writew(wcr_enable, wdog_base);

	/* wait for reset to assert... */
	mdelay(500);

	pr_err("%s: Watchdog reset failed to assert reset\n", __func__);

	/* delay to allow the serial port to show the message */
	mdelay(50);

	/* we'll take a jump through zero as a poor second */
	soft_restart(0);
}

void __init mxc_arch_reset_init(void __iomem *base)
{
	wdog_base = base;

	wdog_clk = clk_get_sys("imx2-wdt.0", NULL);
	if (IS_ERR(wdog_clk)) {
		pr_warn("%s: failed to get wdog clock\n", __func__);
		wdog_clk = NULL;
		return;
	}

	clk_prepare(wdog_clk);
}

void __init mxc_arch_reset_init_dt(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx21-wdt");
	wdog_base = of_iomap(np, 0);
	WARN_ON(!wdog_base);

	wdog_clk = of_clk_get(np, 0);
	if (IS_ERR(wdog_clk)) {
		pr_warn("%s: failed to get wdog clock\n", __func__);
		wdog_clk = NULL;
		return;
	}

	clk_prepare(wdog_clk);
}
