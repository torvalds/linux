/*
 * Copyright (C) 1999 ARM Limited
 * Copyright (C) 2000 Deep Blue Solutions Ltd
 * Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
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

#include <mach/hardware.h>
#include <asm/proc-fns.h>
#include <asm/system.h>

/*
 * Put the CPU into idle mode. It is called by default_idle()
 * in process.c file.
 */
void arch_idle(void)
{
	/*
	 * This should do all the clock switching
	 * and wait for interrupt tricks.
	 */
	cpu_do_idle();
}

#define WDOG_WCR_REG                    IO_ADDRESS(WDOG_BASE_ADDR)
#define WDOG_WCR_SRS                    (1 << 4)

/*
 * Reset the system. It is called by machine_restart().
 */
void arch_reset(char mode, const char *cmd)
{
	struct clk *clk;

	clk = clk_get(NULL, "wdog_clk");
	if (!clk) {
		printk(KERN_ERR"Cannot activate the watchdog. Giving up\n");
		return;
	}

	clk_enable(clk);

	/* Assert SRS signal */
	__raw_writew(__raw_readw(WDOG_WCR_REG) & ~WDOG_WCR_SRS, WDOG_WCR_REG);
}
