/*
 * Copyright (C) 1999 ARM Limited
 * Copyright (C) 2000 Deep Blue Solutions Ltd
 * Copyright 2006-2007,2010 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <linux/init.h>
#include <linux/module.h>

#include <asm/proc-fns.h>
#include <asm/system.h>

#include <mach/mxs.h>
#include <mach/common.h>

#define MX23_CLKCTRL_RESET_OFFSET	0x120
#define MX28_CLKCTRL_RESET_OFFSET	0x1e0
#define MXS_CLKCTRL_RESET_CHIP		(1 << 1)

#define MXS_MODULE_CLKGATE		(1 << 30)
#define MXS_MODULE_SFTRST		(1 << 31)

static void __iomem *mxs_clkctrl_reset_addr;

/*
 * Reset the system. It is called by machine_restart().
 */
void mxs_restart(char mode, const char *cmd)
{
	/* reset the chip */
	__mxs_setl(MXS_CLKCTRL_RESET_CHIP, mxs_clkctrl_reset_addr);

	pr_err("Failed to assert the chip reset\n");

	/* Delay to allow the serial port to show the message */
	mdelay(50);

	/* We'll take a jump through zero as a poor second */
	soft_restart(0);
}

static int __init mxs_arch_reset_init(void)
{
	struct clk *clk;

	mxs_clkctrl_reset_addr = MXS_IO_ADDRESS(MXS_CLKCTRL_BASE_ADDR) +
				(cpu_is_mx23() ? MX23_CLKCTRL_RESET_OFFSET :
						 MX28_CLKCTRL_RESET_OFFSET);

	clk = clk_get_sys("rtc", NULL);
	if (!IS_ERR(clk))
		clk_prepare_enable(clk);

	return 0;
}
core_initcall(mxs_arch_reset_init);

/*
 * Clear the bit and poll it cleared.  This is usually called with
 * a reset address and mask being either SFTRST(bit 31) or CLKGATE
 * (bit 30).
 */
static int clear_poll_bit(void __iomem *addr, u32 mask)
{
	int timeout = 0x400;

	/* clear the bit */
	__mxs_clrl(mask, addr);

	/*
	 * SFTRST needs 3 GPMI clocks to settle, the reference manual
	 * recommends to wait 1us.
	 */
	udelay(1);

	/* poll the bit becoming clear */
	while ((__raw_readl(addr) & mask) && --timeout)
		/* nothing */;

	return !timeout;
}

int mxs_reset_block(void __iomem *reset_addr)
{
	int ret;
	int timeout = 0x400;

	/* clear and poll SFTRST */
	ret = clear_poll_bit(reset_addr, MXS_MODULE_SFTRST);
	if (unlikely(ret))
		goto error;

	/* clear CLKGATE */
	__mxs_clrl(MXS_MODULE_CLKGATE, reset_addr);

	/* set SFTRST to reset the block */
	__mxs_setl(MXS_MODULE_SFTRST, reset_addr);
	udelay(1);

	/* poll CLKGATE becoming set */
	while ((!(__raw_readl(reset_addr) & MXS_MODULE_CLKGATE)) && --timeout)
		/* nothing */;
	if (unlikely(!timeout))
		goto error;

	/* clear and poll SFTRST */
	ret = clear_poll_bit(reset_addr, MXS_MODULE_SFTRST);
	if (unlikely(ret))
		goto error;

	/* clear and poll CLKGATE */
	ret = clear_poll_bit(reset_addr, MXS_MODULE_CLKGATE);
	if (unlikely(ret))
		goto error;

	return 0;

error:
	pr_err("%s(%p): module reset timeout\n", __func__, reset_addr);
	return -ETIMEDOUT;
}
EXPORT_SYMBOL(mxs_reset_block);
