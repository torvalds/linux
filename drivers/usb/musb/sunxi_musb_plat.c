/*
 * Allwinner SUNXI "glue layer"
 *
 * Copyright © 2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/usb/musb.h>

#include <plat/sys_config.h>
#include <mach/platform.h>
#include <mach/clock.h>
#include <mach/irqs.h>
#include "../../power/axp_power/axp-gpio.h"
#include "sunxi_musb_plat.h"

static struct resource sunxi_musb_resources[] = {
	[0] = {
		.start = SW_PA_USB0_IO_BASE,
		.end = SW_PA_USB0_IO_BASE + 0xfff,
		.flags = IORESOURCE_MEM,
		.name = "sunxi_musb0-mem",
	},
	[1] = {
		.start = SW_INT_IRQNO_USB0,
		.end = SW_INT_IRQNO_USB0,
		.flags = IORESOURCE_IRQ,
		.name = "mc", /* hardcoded in musb */
	},
};

/* Can support a maximum ep number, ep0 ~ 5 */
#define USBC_MAX_EP_NUM		6

static struct musb_fifo_cfg sunxi_musb_mode_cfg[] = {
	{ .hw_ep_num =  1, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  1, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  2, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  2, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  3, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  3, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  4, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  4, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  5, .style = FIFO_TX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
	{ .hw_ep_num =  5, .style = FIFO_RX, .maxpacket = 512,
		.mode = BUF_SINGLE, },
};

static struct musb_hdrc_config sunxi_musb_config = {
	.multipoint	= 1,
	.dyn_fifo	= 1,
	.soft_con	= 1,
	.dma		= 0,

	.num_eps	= USBC_MAX_EP_NUM,
	.ram_bits	= 11,

	.fifo_cfg	= sunxi_musb_mode_cfg,
	.fifo_cfg_size	= ARRAY_SIZE(sunxi_musb_mode_cfg),
};

static struct musb_hdrc_platform_data sunxi_musb_plat = {
	.mode		= MUSB_HOST,
	.config		= &sunxi_musb_config,
};

static struct platform_device sunxi_musb_device = {
	.name	= "sunxi_musb",
	.id	= -1,

	.dev = {
		.platform_data = &sunxi_musb_plat,
	},

	.resource = sunxi_musb_resources,
	.num_resources = ARRAY_SIZE(sunxi_musb_resources),
};

int register_musb_device(void)
{
	return platform_device_register(&sunxi_musb_device);
}

void unregister_musb_device(void)
{
	platform_device_unregister(&sunxi_musb_device);
}
