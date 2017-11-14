// SPDX-License-Identifier: GPL-1.0+
/*
 * Renesas USB driver R-Car Gen. 2 initialization and power control
 *
 * Copyright (C) 2014 Ulrich Hecht
 */

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/phy/phy.h>
#include <linux/usb/phy.h>
#include "common.h"
#include "rcar2.h"

static int usbhs_rcar2_hardware_init(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);

	if (IS_ENABLED(CONFIG_GENERIC_PHY)) {
		struct phy *phy = phy_get(&pdev->dev, "usb");

		if (IS_ERR(phy))
			return PTR_ERR(phy);

		priv->phy = phy;
		return 0;
	}

	if (IS_ENABLED(CONFIG_USB_PHY)) {
		struct usb_phy *usb_phy = usb_get_phy_dev(&pdev->dev, 0);

		if (IS_ERR(usb_phy))
			return PTR_ERR(usb_phy);

		priv->usb_phy = usb_phy;
		return 0;
	}

	return -ENXIO;
}

static int usbhs_rcar2_hardware_exit(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);

	if (priv->phy) {
		phy_put(priv->phy);
		priv->phy = NULL;
	}

	if (priv->usb_phy) {
		usb_put_phy(priv->usb_phy);
		priv->usb_phy = NULL;
	}

	return 0;
}

static int usbhs_rcar2_power_ctrl(struct platform_device *pdev,
				void __iomem *base, int enable)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);
	int retval = -ENODEV;

	if (priv->phy) {
		if (enable) {
			retval = phy_init(priv->phy);

			if (!retval)
				retval = phy_power_on(priv->phy);
		} else {
			phy_power_off(priv->phy);
			phy_exit(priv->phy);
			retval = 0;
		}
	}

	if (priv->usb_phy) {
		if (enable) {
			retval = usb_phy_init(priv->usb_phy);

			if (!retval)
				retval = usb_phy_set_suspend(priv->usb_phy, 0);
		} else {
			usb_phy_set_suspend(priv->usb_phy, 1);
			usb_phy_shutdown(priv->usb_phy);
			retval = 0;
		}
	}

	return retval;
}

static int usbhs_rcar2_get_id(struct platform_device *pdev)
{
	return USBHS_GADGET;
}

const struct renesas_usbhs_platform_callback usbhs_rcar2_ops = {
	.hardware_init = usbhs_rcar2_hardware_init,
	.hardware_exit = usbhs_rcar2_hardware_exit,
	.power_ctrl = usbhs_rcar2_power_ctrl,
	.get_id = usbhs_rcar2_get_id,
};
