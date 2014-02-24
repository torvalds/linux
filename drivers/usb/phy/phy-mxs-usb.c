/*
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Marek Vasut <marex@denx.de>
 * on behalf of DENX Software Engineering GmbH
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/usb/otg.h>
#include <linux/stmp_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_device.h>

#define DRIVER_NAME "mxs_phy"

#define HW_USBPHY_PWD				0x00
#define HW_USBPHY_CTRL				0x30
#define HW_USBPHY_CTRL_SET			0x34
#define HW_USBPHY_CTRL_CLR			0x38

#define BM_USBPHY_CTRL_SFTRST			BIT(31)
#define BM_USBPHY_CTRL_CLKGATE			BIT(30)
#define BM_USBPHY_CTRL_ENUTMILEVEL3		BIT(15)
#define BM_USBPHY_CTRL_ENUTMILEVEL2		BIT(14)
#define BM_USBPHY_CTRL_ENHOSTDISCONDETECT	BIT(1)

#define to_mxs_phy(p) container_of((p), struct mxs_phy, phy)

/* Do disconnection between PHY and controller without vbus */
#define MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS	BIT(0)

/*
 * The PHY will be in messy if there is a wakeup after putting
 * bus to suspend (set portsc.suspendM) but before setting PHY to low
 * power mode (set portsc.phcd).
 */
#define MXS_PHY_ABNORMAL_IN_SUSPEND		BIT(1)

/*
 * The SOF sends too fast after resuming, it will cause disconnection
 * between host and high speed device.
 */
#define MXS_PHY_SENDING_SOF_TOO_FAST		BIT(2)

struct mxs_phy_data {
	unsigned int flags;
};

static const struct mxs_phy_data imx23_phy_data = {
	.flags = MXS_PHY_ABNORMAL_IN_SUSPEND | MXS_PHY_SENDING_SOF_TOO_FAST,
};

static const struct mxs_phy_data imx6q_phy_data = {
	.flags = MXS_PHY_SENDING_SOF_TOO_FAST |
		MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS,
};

static const struct mxs_phy_data imx6sl_phy_data = {
	.flags = MXS_PHY_DISCONNECT_LINE_WITHOUT_VBUS,
};

static const struct of_device_id mxs_phy_dt_ids[] = {
	{ .compatible = "fsl,imx6sl-usbphy", .data = &imx6sl_phy_data, },
	{ .compatible = "fsl,imx6q-usbphy", .data = &imx6q_phy_data, },
	{ .compatible = "fsl,imx23-usbphy", .data = &imx23_phy_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_phy_dt_ids);

struct mxs_phy {
	struct usb_phy phy;
	struct clk *clk;
	const struct mxs_phy_data *data;
};

static int mxs_phy_hw_init(struct mxs_phy *mxs_phy)
{
	int ret;
	void __iomem *base = mxs_phy->phy.io_priv;

	ret = stmp_reset_block(base + HW_USBPHY_CTRL);
	if (ret)
		return ret;

	/* Power up the PHY */
	writel(0, base + HW_USBPHY_PWD);

	/* enable FS/LS device */
	writel(BM_USBPHY_CTRL_ENUTMILEVEL2 |
	       BM_USBPHY_CTRL_ENUTMILEVEL3,
	       base + HW_USBPHY_CTRL_SET);

	return 0;
}

static int mxs_phy_init(struct usb_phy *phy)
{
	int ret;
	struct mxs_phy *mxs_phy = to_mxs_phy(phy);

	ret = clk_prepare_enable(mxs_phy->clk);
	if (ret)
		return ret;

	return mxs_phy_hw_init(mxs_phy);
}

static void mxs_phy_shutdown(struct usb_phy *phy)
{
	struct mxs_phy *mxs_phy = to_mxs_phy(phy);

	writel(BM_USBPHY_CTRL_CLKGATE,
	       phy->io_priv + HW_USBPHY_CTRL_SET);

	clk_disable_unprepare(mxs_phy->clk);
}

static int mxs_phy_suspend(struct usb_phy *x, int suspend)
{
	int ret;
	struct mxs_phy *mxs_phy = to_mxs_phy(x);

	if (suspend) {
		writel(0xffffffff, x->io_priv + HW_USBPHY_PWD);
		writel(BM_USBPHY_CTRL_CLKGATE,
		       x->io_priv + HW_USBPHY_CTRL_SET);
		clk_disable_unprepare(mxs_phy->clk);
	} else {
		ret = clk_prepare_enable(mxs_phy->clk);
		if (ret)
			return ret;
		writel(BM_USBPHY_CTRL_CLKGATE,
		       x->io_priv + HW_USBPHY_CTRL_CLR);
		writel(0, x->io_priv + HW_USBPHY_PWD);
	}

	return 0;
}

static int mxs_phy_on_connect(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	dev_dbg(phy->dev, "%s speed device has connected\n",
		(speed == USB_SPEED_HIGH) ? "high" : "non-high");

	if (speed == USB_SPEED_HIGH)
		writel(BM_USBPHY_CTRL_ENHOSTDISCONDETECT,
		       phy->io_priv + HW_USBPHY_CTRL_SET);

	return 0;
}

static int mxs_phy_on_disconnect(struct usb_phy *phy,
		enum usb_device_speed speed)
{
	dev_dbg(phy->dev, "%s speed device has disconnected\n",
		(speed == USB_SPEED_HIGH) ? "high" : "non-high");

	if (speed == USB_SPEED_HIGH)
		writel(BM_USBPHY_CTRL_ENHOSTDISCONDETECT,
		       phy->io_priv + HW_USBPHY_CTRL_CLR);

	return 0;
}

static int mxs_phy_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *base;
	struct clk *clk;
	struct mxs_phy *mxs_phy;
	int ret;
	const struct of_device_id *of_id =
			of_match_device(mxs_phy_dt_ids, &pdev->dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev,
			"can't get the clock, err=%ld", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	mxs_phy = devm_kzalloc(&pdev->dev, sizeof(*mxs_phy), GFP_KERNEL);
	if (!mxs_phy) {
		dev_err(&pdev->dev, "Failed to allocate USB PHY structure!\n");
		return -ENOMEM;
	}

	mxs_phy->phy.io_priv		= base;
	mxs_phy->phy.dev		= &pdev->dev;
	mxs_phy->phy.label		= DRIVER_NAME;
	mxs_phy->phy.init		= mxs_phy_init;
	mxs_phy->phy.shutdown		= mxs_phy_shutdown;
	mxs_phy->phy.set_suspend	= mxs_phy_suspend;
	mxs_phy->phy.notify_connect	= mxs_phy_on_connect;
	mxs_phy->phy.notify_disconnect	= mxs_phy_on_disconnect;
	mxs_phy->phy.type		= USB_PHY_TYPE_USB2;

	mxs_phy->clk = clk;
	mxs_phy->data = of_id->data;

	platform_set_drvdata(pdev, mxs_phy);

	ret = usb_add_phy_dev(&mxs_phy->phy);
	if (ret)
		return ret;

	return 0;
}

static int mxs_phy_remove(struct platform_device *pdev)
{
	struct mxs_phy *mxs_phy = platform_get_drvdata(pdev);

	usb_remove_phy(&mxs_phy->phy);

	return 0;
}

static struct platform_driver mxs_phy_driver = {
	.probe = mxs_phy_probe,
	.remove = mxs_phy_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = mxs_phy_dt_ids,
	 },
};

static int __init mxs_phy_module_init(void)
{
	return platform_driver_register(&mxs_phy_driver);
}
postcore_initcall(mxs_phy_module_init);

static void __exit mxs_phy_module_exit(void)
{
	platform_driver_unregister(&mxs_phy_driver);
}
module_exit(mxs_phy_module_exit);

MODULE_ALIAS("platform:mxs-usb-phy");
MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_AUTHOR("Richard Zhao <richard.zhao@freescale.com>");
MODULE_DESCRIPTION("Freescale MXS USB PHY driver");
MODULE_LICENSE("GPL");
