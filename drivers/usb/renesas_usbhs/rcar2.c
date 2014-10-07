/*
 * Renesas USB driver R-Car Gen. 2 initialization and power control
 *
 * Copyright (C) 2014 Ulrich Hecht
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/usb/phy.h>
#include "common.h"
#include "rcar2.h"

static int usbhs_rcar2_hardware_init(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);

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
