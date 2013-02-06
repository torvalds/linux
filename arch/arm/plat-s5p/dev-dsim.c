/* linux/arch/arm/plat-s5p/dev-dsim.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * DSIM controller configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <asm/irq.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <mach/map.h>
#include <mach/dsim.h>
#include <mach/mipi_ddi.h>
#include <linux/regulator/machine.h>

static struct dsim_config dsim_info = {
	.auto_flush = false,		/* main frame fifo auto flush at VSYNC pulse */

	.eot_disable = false,		/* only DSIM_1_02 or DSIM_1_03 */

	.auto_vertical_cnt = false,
	.hse = false,
	.hfp = true,
	.hbp = false,
	.hsa = false,

	.e_no_data_lane = DSIM_DATA_LANE_4,
	.e_byte_clk = DSIM_PLL_OUT_DIV8,

	.pll_stable_time = 500,		/* D-PHY PLL stable time spec :min = 200usec ~ max 400usec */

	.esc_clk = 20 * 1000000,	/* escape clk : 10MHz */

	.stop_holding_cnt = 0,		/* stop state holding counter after bta change count 0 ~ 0xfff */
	.bta_timeout = 0xff,		/* bta timeout 0 ~ 0xff */
	.rx_timeout = 0xffff,		/* lp rx timeout 0 ~ 0xffff */

	.e_lane_swap = DSIM_NO_CHANGE,
};

/* define ddi platform data based on MIPI-DSI. */
static struct mipi_ddi_platform_data mipi_ddi_pd = {
	.backlight_on = NULL,
};

static struct dsim_lcd_config dsim_lcd_info = {
	.e_interface		= DSIM_VIDEO,

	.parameter[DSI_VIRTUAL_CH_ID]	= (unsigned int) DSIM_VIRTUAL_CH_0,
	.parameter[DSI_FORMAT]		= (unsigned int) DSIM_24BPP_888,
	.parameter[DSI_VIDEO_MODE_SEL]	= (unsigned int) DSIM_BURST_SYNC_EVENT,
	.mipi_ddi_pd		= (void *) &mipi_ddi_pd,
};

static struct resource s5p_dsim_resource[] = {
	[0] = {
		.start = S5P_PA_DSIM0,
		.end   = S5P_PA_DSIM0 + SZ_64K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_MIPIDSI0,
		.end   = IRQ_MIPIDSI0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct s5p_platform_dsim dsim_platform_data = {
	.clk_name = "dsim0",
	.dsim_info = &dsim_info,
	.dsim_lcd_info = &dsim_lcd_info,
	.mipi_power = NULL,
	.enable_clk = s5p_dsim_enable_clk,
	.part_reset = s5p_dsim_part_reset,
	.init_d_phy = s5p_dsim_init_d_phy,
	.exit_d_phy = s5p_dsim_exit_d_phy,

	/* default platform revision is 0(evt0). */
	.platform_rev = 0,
};

struct platform_device s5p_device_dsim = {
	.name			= "s5p-dsim",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(s5p_dsim_resource),
	.resource		= s5p_dsim_resource,
	.dev			= {
		.platform_data = (void *) &dsim_platform_data,
	},
};
