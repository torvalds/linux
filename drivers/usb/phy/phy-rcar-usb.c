/*
 * Renesas R-Car USB phy driver
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/usb/otg.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/module.h>

/* USBH common register */
#define USBPCTRL0	0x0800
#define USBPCTRL1	0x0804
#define USBST		0x0808
#define USBEH0		0x080C
#define USBOH0		0x081C
#define USBCTL0		0x0858

/* USBPCTRL1 */
#define PHY_RST		(1 << 2)
#define PLL_ENB		(1 << 1)
#define PHY_ENB		(1 << 0)

/* USBST */
#define ST_ACT		(1 << 31)
#define ST_PLL		(1 << 30)

struct rcar_usb_phy_priv {
	struct usb_phy phy;
	spinlock_t lock;

	void __iomem *reg0;
	int counter;
};

#define usb_phy_to_priv(p) container_of(p, struct rcar_usb_phy_priv, phy)


/*
 * USB initial/install operation.
 *
 * This function setup USB phy.
 * The used value and setting order came from
 * [USB :: Initial setting] on datasheet.
 */
static int rcar_usb_phy_init(struct usb_phy *phy)
{
	struct rcar_usb_phy_priv *priv = usb_phy_to_priv(phy);
	struct device *dev = phy->dev;
	void __iomem *reg0 = priv->reg0;
	int i;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->counter++ == 0) {

		/*
		 * USB phy start-up
		 */

		/* (1) USB-PHY standby release */
		iowrite32(PHY_ENB, (reg0 + USBPCTRL1));

		/* (2) start USB-PHY internal PLL */
		iowrite32(PHY_ENB | PLL_ENB, (reg0 + USBPCTRL1));

		/* (3) USB module status check */
		for (i = 0; i < 1024; i++) {
			udelay(10);
			val = ioread32(reg0 + USBST);
			if (val == (ST_ACT | ST_PLL))
				break;
		}

		if (val != (ST_ACT | ST_PLL)) {
			dev_err(dev, "USB phy not ready\n");
			goto phy_init_end;
		}

		/* (4) USB-PHY reset clear */
		iowrite32(PHY_ENB | PLL_ENB | PHY_RST, (reg0 + USBPCTRL1));

		/* set platform specific port settings */
		iowrite32(0x00000000, (reg0 + USBPCTRL0));

		/*
		 * Bus alignment settings
		 */

		/* (1) EHCI bus alignment (little endian) */
		iowrite32(0x00000000, (reg0 + USBEH0));

		/* (1) OHCI bus alignment (little endian) */
		iowrite32(0x00000000, (reg0 + USBOH0));
	}

phy_init_end:
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static void rcar_usb_phy_shutdown(struct usb_phy *phy)
{
	struct rcar_usb_phy_priv *priv = usb_phy_to_priv(phy);
	void __iomem *reg0 = priv->reg0;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	if (priv->counter-- == 1) { /* last user */
		iowrite32(0x00000000, (reg0 + USBPCTRL0));
		iowrite32(0x00000000, (reg0 + USBPCTRL1));
	}

	spin_unlock_irqrestore(&priv->lock, flags);
}

static int rcar_usb_phy_probe(struct platform_device *pdev)
{
	struct rcar_usb_phy_priv *priv;
	struct resource *res0;
	struct device *dev = &pdev->dev;
	void __iomem *reg0;
	int ret;

	res0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res0) {
		dev_err(dev, "Not enough platform resources\n");
		return -EINVAL;
	}

	/*
	 * CAUTION
	 *
	 * Because this phy address is also mapped under OHCI/EHCI address area,
	 * this driver can't use devm_request_and_ioremap(dev, res) here
	 */
	reg0 = devm_ioremap_nocache(dev, res0->start, resource_size(res0));
	if (!reg0) {
		dev_err(dev, "ioremap error\n");
		return -ENOMEM;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "priv data allocation error\n");
		return -ENOMEM;
	}

	priv->reg0		= reg0;
	priv->counter		= 0;
	priv->phy.dev		= dev;
	priv->phy.label		= dev_name(dev);
	priv->phy.init		= rcar_usb_phy_init;
	priv->phy.shutdown	= rcar_usb_phy_shutdown;
	spin_lock_init(&priv->lock);

	ret = usb_add_phy(&priv->phy, USB_PHY_TYPE_USB2);
	if (ret < 0) {
		dev_err(dev, "usb phy addition error\n");
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	return ret;
}

static int rcar_usb_phy_remove(struct platform_device *pdev)
{
	struct rcar_usb_phy_priv *priv = platform_get_drvdata(pdev);

	usb_remove_phy(&priv->phy);

	return 0;
}

static struct platform_driver rcar_usb_phy_driver = {
	.driver		= {
		.name	= "rcar_usb_phy",
	},
	.probe		= rcar_usb_phy_probe,
	.remove		= rcar_usb_phy_remove,
};

module_platform_driver(rcar_usb_phy_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas R-Car USB phy");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
