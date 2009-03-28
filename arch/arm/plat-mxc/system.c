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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <mach/hardware.h>
#include <asm/proc-fns.h>
#include <asm/system.h>

#ifdef CONFIG_ARCH_MX1
#define WDOG_WCR_REG		IO_ADDRESS(WDT_BASE_ADDR)
#define WDOG_WCR_ENABLE		(1 << 0)
#else
#define WDOG_WCR_REG		IO_ADDRESS(WDOG_BASE_ADDR)
#define WDOG_WCR_ENABLE		(1 << 2)
#endif

/*
 * Reset the system. It is called by machine_restart().
 */
void arch_reset(char mode, const char *cmd)
{
	if (!cpu_is_mx1()) {
		struct clk *clk;

		clk = clk_get_sys("imx-wdt.0", NULL);
		if (!IS_ERR(clk))
			clk_enable(clk);
	}

	/* Assert SRS signal */
	__raw_writew(WDOG_WCR_ENABLE, WDOG_WCR_REG);

	/* wait for reset to assert... */
	mdelay(500);

	printk(KERN_ERR "Watchdog reset failed to assert reset\n");

	/* delay to allow the serial port to show the message */
	mdelay(50);

	/* we'll take a jump through zero as a poor second */
	cpu_reset(0);
}
