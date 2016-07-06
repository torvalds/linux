/*
 * i.MX6 OCOTP fusebox driver
 *
 * Copyright (c) 2015 Pengutronix, Philipp Zabel <p.zabel@pengutronix.de>
 *
 * Based on the barebox ocotp driver,
 * Copyright (c) 2010 Baruch Siach <baruch@tkos.co.il>,
 *	Orex Computed Radiography
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct ocotp_priv {
	struct device *dev;
	void __iomem *base;
	unsigned int nregs;
};

static int imx_ocotp_read(void *context, unsigned int offset,
			  void *val, size_t bytes)
{
	struct ocotp_priv *priv = context;
	unsigned int count;
	u32 *buf = val;
	int i;
	u32 index;

	index = offset >> 2;
	count = bytes >> 2;

	if (count > (priv->nregs - index))
		count = priv->nregs - index;

	for (i = index; i < (index + count); i++)
		*buf++ = readl(priv->base + 0x400 + i * 0x10);

	return 0;
}

static struct nvmem_config imx_ocotp_nvmem_config = {
	.name = "imx-ocotp",
	.read_only = true,
	.word_size = 4,
	.stride = 4,
	.owner = THIS_MODULE,
	.reg_read = imx_ocotp_read,
};

static const struct of_device_id imx_ocotp_dt_ids[] = {
	{ .compatible = "fsl,imx6q-ocotp",  (void *)128 },
	{ .compatible = "fsl,imx6sl-ocotp", (void *)32 },
	{ .compatible = "fsl,imx6sx-ocotp", (void *)128 },
	{ },
};
MODULE_DEVICE_TABLE(of, imx_ocotp_dt_ids);

static int imx_ocotp_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct ocotp_priv *priv;
	struct nvmem_device *nvmem;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	of_id = of_match_device(imx_ocotp_dt_ids, dev);
	priv->nregs = (unsigned int)of_id->data;
	imx_ocotp_nvmem_config.size = 4 * priv->nregs;
	imx_ocotp_nvmem_config.dev = dev;
	imx_ocotp_nvmem_config.priv = priv;
	nvmem = nvmem_register(&imx_ocotp_nvmem_config);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static int imx_ocotp_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static struct platform_driver imx_ocotp_driver = {
	.probe	= imx_ocotp_probe,
	.remove	= imx_ocotp_remove,
	.driver = {
		.name	= "imx_ocotp",
		.of_match_table = imx_ocotp_dt_ids,
	},
};
module_platform_driver(imx_ocotp_driver);

MODULE_AUTHOR("Philipp Zabel <p.zabel@pengutronix.de>");
MODULE_DESCRIPTION("i.MX6 OCOTP fuse box driver");
MODULE_LICENSE("GPL v2");
