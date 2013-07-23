/*
 * Renesas R-Car USB phy driver
 *
 * Copyright (C) 2012-2013 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 * Copyright (C) 2013 Cogent Embedded, Inc.
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
#include <linux/platform_data/usb-rcar-phy.h>

/* REGS block */
#define USBPCTRL0	0x00
#define USBPCTRL1	0x04
#define USBST		0x08
#define USBEH0		0x0C
#define USBOH0		0x1C
#define USBCTL0		0x58

/* High-speed signal quality characteristic control registers (R8A7778 only) */
#define HSQCTL1		0x24
#define HSQCTL2		0x28

/* USBPCTRL0 */
#define OVC2		(1 << 10) /* (R8A7779 only)			*/
				/* Switches the OVC input pin for port 2: */
				/* 1: USB_OVC2, 0: OVC2			*/
#define OVC1_VBUS1	(1 << 9) /* Switches the OVC input pin for port 1: */
				/* 1: USB_OVC1, 0: OVC1/VBUS1		*/
				/* Function mode: set to 0		*/
#define OVC0		(1 << 8) /* Switches the OVC input pin for port 0: */
				/* 1: USB_OVC0 pin, 0: OVC0		*/
#define OVC2_ACT 	(1 << 6) /* (R8A7779 only)			*/
				/* Host mode: OVC2 polarity:		*/
				/* 1: active-high, 0: active-low	*/
#define PENC		(1 << 4) /* Function mode: output level of PENC1 pin: */
				/* 1: high, 0: low			*/
#define OVC0_ACT 	(1 << 3) /* Host mode: OVC0 polarity:		*/
				/* 1: active-high, 0: active-low	*/
#define OVC1_ACT	(1 << 1) /* Host mode: OVC1 polarity:		*/
				/* 1: active-high, 0: active-low	*/
				/* Function mode: be sure to set to 1	*/
#define PORT1		(1 << 0) /* Selects port 1 mode:		*/
				/* 1: function, 0: host			*/
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
	void __iomem *reg1;
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
	struct rcar_phy_platform_data *pdata = dev->platform_data;
	void __iomem *reg0 = priv->reg0;
	void __iomem *reg1 = priv->reg1;
	static const u8 ovcn_act[] = { OVC0_ACT, OVC1_ACT, OVC2_ACT };
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

		/* (3) set USB-PHY in accord with the conditions of usage */
		if (reg1) {
			u32 hsqctl1 = pdata->ferrite_bead ? 0x41 : 0;
			u32 hsqctl2 = pdata->ferrite_bead ? 0x0d : 7;

			iowrite32(hsqctl1, reg1 + HSQCTL1);
			iowrite32(hsqctl2, reg1 + HSQCTL2);
		}

		/* (4) USB module status check */
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

		/* (5) USB-PHY reset clear */
		iowrite32(PHY_ENB | PLL_ENB | PHY_RST, (reg0 + USBPCTRL1));

		/* Board specific port settings */
		val = 0;
		if (pdata->port1_func)
			val |= PORT1;
		if (pdata->penc1)
			val |= PENC;
		for (i = 0; i < 3; i++) {
			/* OVCn bits follow each other in the right order */
			if (pdata->ovc_pin[i].select_3_3v)
				val |= OVC0 << i;
			/* OVCn_ACT bits are spaced by irregular intervals */
			if (pdata->ovc_pin[i].active_high)
				val |= ovcn_act[i];
		}
		iowrite32(val, (reg0 + USBPCTRL0));

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

	if (priv->counter-- == 1)	/* last user */
		iowrite32(0x00000000, (reg0 + USBPCTRL1));

	spin_unlock_irqrestore(&priv->lock, flags);
}

static int rcar_usb_phy_probe(struct platform_device *pdev)
{
	struct rcar_usb_phy_priv *priv;
	struct resource *res0, *res1;
	struct device *dev = &pdev->dev;
	void __iomem *reg0, *reg1 = NULL;
	int ret;

	if (!pdev->dev.platform_data) {
		dev_err(dev, "No platform data\n");
		return -EINVAL;
	}

	res0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg0 = devm_ioremap_resource(dev, res0);
	if (IS_ERR(reg0))
		return PTR_ERR(reg0);

	res1 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res1) {
		reg1 = devm_ioremap_resource(dev, res1);
		if (IS_ERR(reg1))
			return PTR_ERR(reg1);
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "priv data allocation error\n");
		return -ENOMEM;
	}

	priv->reg0		= reg0;
	priv->reg1		= reg1;
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
