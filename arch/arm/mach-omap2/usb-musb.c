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

#include "omap_device.h"
#include "soc.h"
#include "mux.h"
#include "usb.h"

static struct musb_hdrc_config musb_config = {
	.multipoint	= 1,
	.dyn_fifo	= 1,
	.num_eps	= 16,
	.ram_bits	= 12,
};

static struct musb_hdrc_platform_data musb_plat = {
#ifdef CONFIG_USB_GADGET_MUSB_HDRC
	.mode		= MUSB_OTG,
#else
	.mode		= MUSB_HOST,
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

static struct omap_musb_board_data musb_default_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_OTG,
	.power			= 100,
};

void __init usb_musb_init(struct omap_musb_board_data *musb_board_data)
{
	struct omap_hwmod		*oh;
	struct platform_device		*pdev;
	struct device			*dev;
	int				bus_id = -1;
	const char			*oh_name, *name;
	struct omap_musb_board_data	*board_data;

	if (musb_board_data)
		board_data = musb_board_data;
	else
		board_data = &musb_default_board_data;

	/*
	 * REVISIT: This line can be removed once all the platforms using
	 * musb_core.c have been converted to use use clkdev.
	 */
	musb_plat.clock = "ick";
	musb_plat.board_data = board_data;
	musb_plat.power = board_data->power >> 1;
	musb_plat.mode = board_data->mode;
	musb_plat.extvbus = board_data->extvbus;

	if (soc_is_am35xx()) {
		oh_name = "am35x_otg_hs";
		name = "musb-am35x";
	} else if (cpu_is_ti81xx()) {
		oh_name = "usb_otg_hs";
		name = "musb-ti81xx";
	} else {
		oh_name = "usb_otg_hs";
		name = "musb-omap2430";
	}

        oh = omap_hwmod_lookup(oh_name);
        if (WARN(!oh, "%s: could not find omap_hwmod for %s\n",
                 __func__, oh_name))
                return;

	pdev = omap_device_build(name, bus_id, oh, &musb_plat,
				 sizeof(musb_plat));
	if (IS_ERR(pdev)) {
		pr_err("Could not build omap_device for %s %s\n",
						name, oh_name);
		return;
	}

	dev = &pdev->dev;
	get_device(dev);
	dev->dma_mask = &musb_dmamask;
	dev->coherent_dma_mask = musb_dmamask;
	put_device(dev);
}
