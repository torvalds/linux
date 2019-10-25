// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas USB driver RZ/A2 initialization and power control
 *
 * Copyright (C) 2019 Chris Brandt
 * Copyright (C) 2019 Renesas Electronics Corporation
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include "common.h"
#include "rza.h"

static int usbhs_rza2_hardware_init(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);
	struct phy *phy = phy_get(&pdev->dev, "usb");

	if (IS_ERR(phy))
		return PTR_ERR(phy);

	priv->phy = phy;
	return 0;
}

static int usbhs_rza2_hardware_exit(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);

	phy_put(priv->phy);
	priv->phy = NULL;

	return 0;
}

static int usbhs_rza2_power_ctrl(struct platform_device *pdev,
				void __iomem *base, int enable)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);
	int retval = 0;

	if (!priv->phy)
		return -ENODEV;

	if (enable) {
		retval = phy_init(priv->phy);
		usbhs_bset(priv, SUSPMODE, SUSPM, SUSPM);
		udelay(100);	/* Wait for PLL to become stable */
		if (!retval)
			retval = phy_power_on(priv->phy);
	} else {
		usbhs_bset(priv, SUSPMODE, SUSPM, 0);
		phy_power_off(priv->phy);
		phy_exit(priv->phy);
	}

	return retval;
}

const struct renesas_usbhs_platform_info usbhs_rza2_plat_info = {
	.platform_callback = {
		.hardware_init = usbhs_rza2_hardware_init,
		.hardware_exit = usbhs_rza2_hardware_exit,
		.power_ctrl = usbhs_rza2_power_ctrl,
		.get_id = usbhs_get_id_as_gadget,
	},
	.driver_param = {
		.has_cnen = 1,
		.cfifo_byte_addr = 1,
		.has_new_pipe_configs = 1,
	},
};
