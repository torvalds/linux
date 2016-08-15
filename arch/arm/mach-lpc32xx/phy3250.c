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
#include <linux/device.h>
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

/*
 * AMBA LCD controller
 */
static struct clcd_panel conn_lcd_panel = {
	.mode		= {
		.name		= "QVGA portrait",
		.refresh	= 60,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 191828,
		.left_margin	= 22,
		.right_margin	= 11,
		.upper_margin	= 2,
		.lower_margin	= 1,
		.hsync_len	= 5,
		.vsync_len	= 2,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= (TIM2_IVS | TIM2_IHS),
	.cntl		= (CNTL_BGR | CNTL_LCDTFT | CNTL_LCDVCOMP(1) |
				CNTL_LCDBPP16_565),
	.bpp		= 16,
};
#define PANEL_SIZE (3 * SZ_64K)

static int lpc32xx_clcd_setup(struct clcd_fb *fb)
{
	dma_addr_t dma;

	fb->fb.screen_base = dma_alloc_wc(&fb->dev->dev, PANEL_SIZE, &dma,
					  GFP_KERNEL);
	if (!fb->fb.screen_base) {
		printk(KERN_ERR "CLCD: unable to map framebuffer\n");
		return -ENOMEM;
	}

	fb->fb.fix.smem_start = dma;
	fb->fb.fix.smem_len = PANEL_SIZE;
	fb->panel = &conn_lcd_panel;

	return 0;
}

static int lpc32xx_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma)
{
	return dma_mmap_wc(&fb->dev->dev, vma, fb->fb.screen_base,
			   fb->fb.fix.smem_start, fb->fb.fix.smem_len);
}

static void lpc32xx_clcd_remove(struct clcd_fb *fb)
{
	dma_free_wc(&fb->dev->dev, fb->fb.fix.smem_len, fb->fb.screen_base,
		    fb->fb.fix.smem_start);
}

static struct clcd_board lpc32xx_clcd_data = {
	.name		= "Phytec LCD",
	.check		= clcdfb_check,
	.decode		= clcdfb_decode,
	.setup		= lpc32xx_clcd_setup,
	.mmap		= lpc32xx_clcd_mmap,
	.remove		= lpc32xx_clcd_remove,
};

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
	.slave_channels = &pl08x_slave_channels[0],
	.num_slave_channels = ARRAY_SIZE(pl08x_slave_channels),
	.get_xfer_signal = pl08x_get_signal,
	.put_xfer_signal = pl08x_put_signal,
	.lli_buses = PL08X_AHB1,
	.mem_buses = PL08X_AHB1,
};

static struct mmci_platform_data lpc32xx_mmci_data = {
	.ocr_mask	= MMC_VDD_30_31 | MMC_VDD_31_32 |
			  MMC_VDD_32_33 | MMC_VDD_33_34,
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
	OF_DEV_AUXDATA("arm,pl110", 0x31040000, "dev:clcd", &lpc32xx_clcd_data),
	OF_DEV_AUXDATA("arm,pl080", 0x31000000, "pl08xdmac", &pl08x_pd),
	OF_DEV_AUXDATA("arm,pl18x", 0x20098000, "20098000.sd",
		       &lpc32xx_mmci_data),
	OF_DEV_AUXDATA("nxp,lpc3220-slc", 0x20020000, "20020000.flash",
		       &lpc32xx_slc_data),
	OF_DEV_AUXDATA("nxp,lpc3220-mlc", 0x200a8000, "200a8000.flash",
		       &lpc32xx_mlc_data),
	{ }
};

static void __init lpc3250_machine_init(void)
{
	u32 tmp;

	/* Setup LCD muxing to RGB565 */
	tmp = __raw_readl(LPC32XX_CLKPWR_LCDCLK_CTRL) &
		~(LPC32XX_CLKPWR_LCDCTRL_LCDTYPE_MSK |
		LPC32XX_CLKPWR_LCDCTRL_PSCALE_MSK);
	tmp |= LPC32XX_CLKPWR_LCDCTRL_LCDTYPE_TFT16;
	__raw_writel(tmp, LPC32XX_CLKPWR_LCDCLK_CTRL);

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
