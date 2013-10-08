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
#include <linux/delay.h>

#include "ci_hdrc_imx.h"

#define MX25_USB_PHY_CTRL_OFFSET	0x08
#define MX25_BM_EXTERNAL_VBUS_DIVIDER	BIT(23)

#define MX53_USB_OTG_PHY_CTRL_0_OFFSET	0x08
#define MX53_USB_UH2_CTRL_OFFSET	0x14
#define MX53_USB_UH3_CTRL_OFFSET	0x18
#define MX53_BM_OVER_CUR_DIS_H1		BIT(5)
#define MX53_BM_OVER_CUR_DIS_OTG	BIT(8)
#define MX53_BM_OVER_CUR_DIS_UHx	BIT(30)

#define MX6_BM_OVER_CUR_DIS		BIT(7)

struct usbmisc_ops {
	/* It's called once when probe a usb device */
	int (*init)(struct imx_usbmisc_data *data);
	/* It's called once after adding a usb device */
	int (*post)(struct imx_usbmisc_data *data);
};

struct imx_usbmisc {
	void __iomem *base;
	spinlock_t lock;
	struct clk *clk;
	const struct usbmisc_ops *ops;
};

static struct imx_usbmisc *usbmisc;

static int usbmisc_imx25_post(struct imx_usbmisc_data *data)
{
	void __iomem *reg;
	unsigned long flags;
	u32 val;

	if (data->index > 2)
		return -EINVAL;

	reg = usbmisc->base + MX25_USB_PHY_CTRL_OFFSET;

	if (data->evdo) {
		spin_lock_irqsave(&usbmisc->lock, flags);
		val = readl(reg);
		writel(val | MX25_BM_EXTERNAL_VBUS_DIVIDER, reg);
		spin_unlock_irqrestore(&usbmisc->lock, flags);
		usleep_range(5000, 10000); /* needed to stabilize voltage */
	}

	return 0;
}

static int usbmisc_imx53_init(struct imx_usbmisc_data *data)
{
	void __iomem *reg = NULL;
	unsigned long flags;
	u32 val = 0;

	if (data->index > 3)
		return -EINVAL;

	if (data->disable_oc) {
		spin_lock_irqsave(&usbmisc->lock, flags);
		switch (data->index) {
		case 0:
			reg = usbmisc->base + MX53_USB_OTG_PHY_CTRL_0_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_OTG;
			break;
		case 1:
			reg = usbmisc->base + MX53_USB_OTG_PHY_CTRL_0_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_H1;
			break;
		case 2:
			reg = usbmisc->base + MX53_USB_UH2_CTRL_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_UHx;
			break;
		case 3:
			reg = usbmisc->base + MX53_USB_UH3_CTRL_OFFSET;
			val = readl(reg) | MX53_BM_OVER_CUR_DIS_UHx;
			break;
		}
		if (reg && val)
			writel(val, reg);
		spin_unlock_irqrestore(&usbmisc->lock, flags);
	}

	return 0;
}

static int usbmisc_imx6q_init(struct imx_usbmisc_data *data)
{
	unsigned long flags;
	u32 reg;

	if (data->index > 3)
		return -EINVAL;

	if (data->disable_oc) {
		spin_lock_irqsave(&usbmisc->lock, flags);
		reg = readl(usbmisc->base + data->index * 4);
		writel(reg | MX6_BM_OVER_CUR_DIS,
			usbmisc->base + data->index * 4);
		spin_unlock_irqrestore(&usbmisc->lock, flags);
	}

	return 0;
}

static const struct usbmisc_ops imx25_usbmisc_ops = {
	.post = usbmisc_imx25_post,
};

static const struct usbmisc_ops imx53_usbmisc_ops = {
	.init = usbmisc_imx53_init,
};

static const struct usbmisc_ops imx6q_usbmisc_ops = {
	.init = usbmisc_imx6q_init,
};

int imx_usbmisc_init(struct imx_usbmisc_data *data)
{
	if (!usbmisc)
		return -EPROBE_DEFER;
	if (!usbmisc->ops->init)
		return 0;
	return usbmisc->ops->init(data);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_init);

int imx_usbmisc_init_post(struct imx_usbmisc_data *data)
{
	if (!usbmisc)
		return -EPROBE_DEFER;
	if (!usbmisc->ops->post)
		return 0;
	return usbmisc->ops->post(data);
}
EXPORT_SYMBOL_GPL(imx_usbmisc_init_post);

static const struct of_device_id usbmisc_imx_dt_ids[] = {
	{
		.compatible = "fsl,imx25-usbmisc",
		.data = &imx25_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx53-usbmisc",
		.data = &imx53_usbmisc_ops,
	},
	{
		.compatible = "fsl,imx6q-usbmisc",
		.data = &imx6q_usbmisc_ops,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, usbmisc_imx_dt_ids);

static int usbmisc_imx_probe(struct platform_device *pdev)
{
	struct resource	*res;
	struct imx_usbmisc *data;
	int ret;
	struct of_device_id *tmp_dev;

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

	tmp_dev = (struct of_device_id *)
		of_match_device(usbmisc_imx_dt_ids, &pdev->dev);
	data->ops = (const struct usbmisc_ops *)tmp_dev->data;
	usbmisc = data;

	return 0;
}

static int usbmisc_imx_remove(struct platform_device *pdev)
{
	clk_disable_unprepare(usbmisc->clk);
	usbmisc = NULL;
	return 0;
}

static struct platform_driver usbmisc_imx_driver = {
	.probe = usbmisc_imx_probe,
	.remove = usbmisc_imx_remove,
	.driver = {
		.name = "usbmisc_imx",
		.owner = THIS_MODULE,
		.of_match_table = usbmisc_imx_dt_ids,
	 },
};

module_platform_driver(usbmisc_imx_driver);

MODULE_ALIAS("platform:usbmisc-imx");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("driver for imx usb non-core registers");
MODULE_AUTHOR("Richard Zhao <richard.zhao@freescale.com>");
