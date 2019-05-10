/*
 * Platform support for LPC32xx SoC
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2012 Roland Stigge <stigge@antcom.de>
 * Copyright (C) 2010 NXP Semiconductors
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

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/amba/pl08x.h>
#include <linux/amba/mmci.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/mtd/lpc32xx_slc.h>
#include <linux/mtd/lpc32xx_mlc.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include <mach/board.h>
#include "common.h"

static struct pl08x_channel_data pl08x_slave_channels[] = {
	{
		.bus_id = "nand-slc",
		.min_signal = 1, /* SLC NAND Flash */
		.max_signal = 1,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "nand-mlc",
		.min_signal = 12, /* MLC NAND Flash */
		.max_signal = 12,
		.periph_buses = PL08X_AHB1,
	},
};

static int pl08x_get_signal(const struct pl08x_channel_data *cd)
{
	return cd->min_signal;
}

static void pl08x_put_signal(const struct pl08x_channel_data *cd, int ch)
{
}

static struct pl08x_platform_data pl08x_pd = {
	/* Some reasonable memcpy defaults */
	.memcpy_burst_size = PL08X_BURST_SZ_256,
	.memcpy_bus_width = PL08X_BUS_WIDTH_32_BITS,
	.slave_channels = &pl08x_slave_channels[0],
	.num_slave_channels = ARRAY_SIZE(pl08x_slave_channels),
	.get_xfer_signal = pl08x_get_signal,
	.put_xfer_signal = pl08x_put_signal,
	.lli_buses = PL08X_AHB1,
	.mem_buses = PL08X_AHB1,
};

static struct lpc32xx_slc_platform_data lpc32xx_slc_data = {
	.dma_filter = pl08x_filter_id,
};

static struct lpc32xx_mlc_platform_data lpc32xx_mlc_data = {
	.dma_filter = pl08x_filter_id,
};

static const struct of_dev_auxdata lpc32xx_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("arm,pl022", 0x20084000, "dev:ssp0", NULL),
	OF_DEV_AUXDATA("arm,pl022", 0x2008C000, "dev:ssp1", NULL),
	OF_DEV_AUXDATA("arm,pl080", 0x31000000, "pl08xdmac", &pl08x_pd),
	OF_DEV_AUXDATA("nxp,lpc3220-slc", 0x20020000, "20020000.flash",
		       &lpc32xx_slc_data),
	OF_DEV_AUXDATA("nxp,lpc3220-mlc", 0x200a8000, "200a8000.flash",
		       &lpc32xx_mlc_data),
	{ }
};

static void __init lpc3250_machine_init(void)
{
	lpc32xx_serial_init();

	/* Test clock needed for UDA1380 initial init */
	__raw_writel(LPC32XX_CLKPWR_TESTCLK2_SEL_MOSC |
		LPC32XX_CLKPWR_TESTCLK_TESTCLK2_EN,
		LPC32XX_CLKPWR_TEST_CLK_SEL);

	of_platform_default_populate(NULL, lpc32xx_auxdata_lookup, NULL);
}

static const char *const lpc32xx_dt_compat[] __initconst = {
	"nxp,lpc3220",
	"nxp,lpc3230",
	"nxp,lpc3240",
	"nxp,lpc3250",
	NULL
};

DT_MACHINE_START(LPC32XX_DT, "LPC32XX SoC (Flattened Device Tree)")
	.atag_offset	= 0x100,
	.map_io		= lpc32xx_map_io,
	.init_machine	= lpc3250_machine_init,
	.dt_compat	= lpc32xx_dt_compat,
MACHINE_END
