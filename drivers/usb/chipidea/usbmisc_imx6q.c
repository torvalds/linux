/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>

#include "ci13xxx_imx.h"

#define USB_DEV_MAX 4

#define BM_OVER_CUR_DIS		BIT(7)

struct imx6q_usbmisc {
	void __iomem *base;
	spinlock_t lock;
	struct clk *clk;
	struct usbmisc_usb_device usbdev[USB_DEV_MAX];
};

static struct imx6q_usbmisc *usbmisc;

static struct usbmisc_usb_device *get_usbdev(struct device *dev)
{
	int i, ret;

	for (i = 0; i < USB_DEV_MAX; i++) {
		if (usbmisc->usbdev[i].dev == dev)
			return &usbmisc->usbdev[i];
		else if (!usbmisc->usbdev[i].dev)
			break;
	}

	if (i >= USB_DEV_MAX)
		return ERR_PTR(-EBUSY);

	ret = usbmisc_get_init_data(dev, &usbmisc->usbdev[i]);
	if (ret)
		return ERR_PTR(ret);

	return &usbmisc->usbdev[i];
}

static int usbmisc_imx6q_init(struct device *dev)
{

	struct usbmisc_usb_device *usbdev;
	unsigned long flags;
	u32 reg;

	usbdev = get_usbdev(dev);
	if (IS_ERR(usbdev))
		return PTR_ERR(usbdev);

	if (usbdev->disable_oc) {
		spin_lock_irqsave(&usbmisc->lock, flags);
		reg = readl(usbmisc->base + usbdev->index * 4);
		writel(reg | BM_OVER_CUR_DIS,
			usbmisc->base + usbdev->index * 4);
		spin_unlock_irqrestore(&usbmisc->lock, flags);
	}

	return 0;
}

static const struct usbmisc_ops imx6q_usbmisc_ops = {
	.init = usbmisc_imx6q_init,
};

static const struct of_device_id usbmisc_imx6q_dt_ids[] = {
	{ .compatible = "fsl,imx6q-usbmisc"},
	{ /* sentinel */ }
};

static int usbmisc_imx6q_probe(struct platform_device *pdev)
{
	struct resource	*res;
	struct imx6q_usbmisc *data;
	int ret;

	if (usbmisc)
		return -EBUSY;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	spin_lock_init(&data->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	data->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(data->clk)) {
		dev_err(&pdev->dev,
			"failed to get clock, err=%ld\n", PTR_ERR(data->clk));
		return PTR_ERR(data->clk);
	}

	ret = clk_prepare_enable(data->clk);
	if (ret) {
		dev_err(&pdev->dev,
			"clk_prepare_enable failed, err=%d\n", ret);
		return ret;
	}

	ret = usbmisc_set_ops(&imx6q_usbmisc_ops);
	if (ret) {
		clk_disable_unprepare(data->clk);
		return ret;
	}

	usbmisc = data;

	return 0;
}

static int usbmisc_imx6q_remove(struct platform_device *pdev)
{
	usbmisc_unset_ops(&imx6q_usbmisc_ops);
	clk_disable_unprepare(usbmisc->clk);
	return 0;
}

static struct platform_driver usbmisc_imx6q_driver = {
	.probe = usbmisc_imx6q_probe,
	.remove = usbmisc_imx6q_remove,
	.driver = {
		.name = "usbmisc_imx6q",
		.owner = THIS_MODULE,
		.of_match_table = usbmisc_imx6q_dt_ids,
	 },
};

int __init usbmisc_imx6q_drv_init(void)
{
	return platform_driver_register(&usbmisc_imx6q_driver);
}
subsys_initcall(usbmisc_imx6q_drv_init);

void __exit usbmisc_imx6q_drv_exit(void)
{
	platform_driver_unregister(&usbmisc_imx6q_driver);
}
module_exit(usbmisc_imx6q_drv_exit);

MODULE_ALIAS("platform:usbmisc-imx6q");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("driver for imx6q usb non-core registers");
MODULE_AUTHOR("Richard Zhao <richard.zhao@freescale.com>");
