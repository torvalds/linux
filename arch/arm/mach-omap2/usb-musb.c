/*
 * linux/arch/arm/mach-omap2/usb-musb.c
 *
 * This file will contain the board specific details for the
 * MENTOR USB OTG controller on OMAP3430
 *
 * Copyright (C) 2007-2008 Texas Instruments
 * Copyright (C) 2008 Nokia Corporation
 * Author: Vikram Pandita
 *
 * Generalization by:
 * Felipe Balbi <felipe.balbi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <linux/usb/musb.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/am35xx.h>
#include <plat/usb.h>
#include <plat/omap_device.h>
#include "mux.h"

#if defined(CONFIG_USB_MUSB_OMAP2PLUS) || defined (CONFIG_USB_MUSB_AM35X)

static struct musb_hdrc_config musb_config = {
	.multipoint	= 1,
	.dyn_fifo	= 1,
	.num_eps	= 16,
	.ram_bits	= 12,
};

static struct musb_hdrc_platform_data musb_plat = {
#ifdef CONFIG_USB_MUSB_OTG
	.mode		= MUSB_OTG,
#elif defined(CONFIG_USB_MUSB_HDRC_HCD)
	.mode		= MUSB_HOST,
#elif defined(CONFIG_USB_GADGET_MUSB_HDRC)
	.mode		= MUSB_PERIPHERAL,
#endif
	/* .clock is set dynamically */
	.config		= &musb_config,

	/* REVISIT charge pump on TWL4030 can supply up to
	 * 100 mA ... but this value is board-specific, like
	 * "mode", and should be passed to usb_musb_init().
	 */
	.power		= 50,			/* up to 100 mA */
};

static u64 musb_dmamask = DMA_BIT_MASK(32);

static struct omap_device_pm_latency omap_musb_latency[] = {
	{
		.deactivate_func	= omap_device_idle_hwmods,
		.activate_func		= omap_device_enable_hwmods,
		.flags			= OMAP_DEVICE_LATENCY_AUTO_ADJUST,
	},
};

static void usb_musb_mux_init(struct omap_musb_board_data *board_data)
{
	switch (board_data->interface_type) {
	case MUSB_INTERFACE_UTMI:
		omap_mux_init_signal("usba0_otg_dp", OMAP_PIN_INPUT);
		omap_mux_init_signal("usba0_otg_dm", OMAP_PIN_INPUT);
		break;
	case MUSB_INTERFACE_ULPI:
		omap_mux_init_signal("usba0_ulpiphy_clk",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_stp",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dir",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_nxt",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat0",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat1",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat2",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat3",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat4",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat5",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat6",
						OMAP_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("usba0_ulpiphy_dat7",
						OMAP_PIN_INPUT_PULLDOWN);
		break;
	default:
		break;
	}
}

void __init usb_musb_init(struct omap_musb_board_data *board_data)
{
	struct omap_hwmod		*oh;
	struct omap_device		*od;
	struct platform_device		*pdev;
	struct device			*dev;
	int				bus_id = -1;
	const char			*oh_name, *name;

	if (cpu_is_omap3517() || cpu_is_omap3505()) {
	} else if (cpu_is_omap44xx()) {
		usb_musb_mux_init(board_data);
	}

	/*
	 * REVISIT: This line can be removed once all the platforms using
	 * musb_core.c have been converted to use use clkdev.
	 */
	musb_plat.clock = "ick";
	musb_plat.board_data = board_data;
	musb_plat.power = board_data->power >> 1;
	musb_plat.mode = board_data->mode;
	musb_plat.extvbus = board_data->extvbus;

	if (cpu_is_omap44xx())
		omap4430_phy_init(dev);

	if (cpu_is_omap3517() || cpu_is_omap3505()) {
		oh_name = "am35x_otg_hs";
		name = "musb-am35x";
	} else {
		oh_name = "usb_otg_hs";
		name = "musb-omap2430";
	}

	oh = omap_hwmod_lookup(oh_name);
	if (!oh) {
		pr_err("Could not look up %s\n", oh_name);
		return;
	}

	od = omap_device_build(name, bus_id, oh, &musb_plat,
			       sizeof(musb_plat), omap_musb_latency,
			       ARRAY_SIZE(omap_musb_latency), false);
	if (IS_ERR(od)) {
		pr_err("Could not build omap_device for %s %s\n",
						name, oh_name);
		return;
	}

	pdev = &od->pdev;
	dev = &pdev->dev;
	get_device(dev);
	dev->dma_mask = &musb_dmamask;
	dev->coherent_dma_mask = musb_dmamask;
	put_device(dev);
}

#else
void __init usb_musb_init(struct omap_musb_board_data *board_data)
{
}
#endif /* CONFIG_USB_MUSB_SOC */
