// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas USB driver RZ/A initialization and power control
 *
 * Copyright (C) 2018 Chris Brandt
 * Copyright (C) 2018-2019 Renesas Electronics Corporation
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include "common.h"
#include "rza.h"

static int usbhs_rza1_hardware_init(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);
	struct device_node *usb_x1_clk, *extal_clk;
	u32 freq_usb = 0, freq_extal = 0;

	/* Input Clock Selection (NOTE: ch0 controls both ch0 and ch1) */
	usb_x1_clk = of_find_node_by_name(NULL, "usb_x1");
	extal_clk = of_find_node_by_name(NULL, "extal");
	of_property_read_u32(usb_x1_clk, "clock-frequency", &freq_usb);
	of_property_read_u32(extal_clk, "clock-frequency", &freq_extal);

	of_node_put(usb_x1_clk);
	of_node_put(extal_clk);

	if (freq_usb == 0) {
		if (freq_extal == 12000000) {
			/* Select 12MHz XTAL */
			usbhs_bset(priv, SYSCFG, UCKSEL, UCKSEL);
		} else {
			dev_err(usbhs_priv_to_dev(priv), "A 48MHz USB clock or 12MHz main clock is required.\n");
			return -EIO;
		}
	}

	/* Enable USB PLL (NOTE: ch0 controls both ch0 and ch1) */
	usbhs_bset(priv, SYSCFG, UPLLE, UPLLE);
	usleep_range(1000, 2000);
	usbhs_bset(priv, SUSPMODE, SUSPM, SUSPM);

	return 0;
}

const struct renesas_usbhs_platform_info usbhs_rza1_plat_info = {
	.platform_callback = {
		.hardware_init = usbhs_rza1_hardware_init,
		.get_id = usbhs_get_id_as_gadget,
	},
	.driver_param = {
		.has_new_pipe_configs = 1,
	},
};
