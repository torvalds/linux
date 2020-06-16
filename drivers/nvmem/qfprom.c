// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/io.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>

struct qfprom_priv {
	void __iomem *base;
};

static int qfprom_reg_read(void *context,
			unsigned int reg, void *_val, size_t bytes)
{
	struct qfprom_priv *priv = context;
	u8 *val = _val;
	int i = 0, words = bytes;

	while (words--)
		*val++ = readb(priv->base + reg + i++);

	return 0;
}

static struct nvmem_config econfig = {
	.name = "qfprom",
	.stride = 1,
	.word_size = 1,
	.reg_read = qfprom_reg_read,
};

static int qfprom_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	struct qfprom_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	econfig.size = resource_size(res);
	econfig.dev = dev;
	econfig.priv = priv;

	nvmem = devm_nvmem_register(dev, &econfig);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id qfprom_of_match[] = {
	{ .compatible = "qcom,qfprom",},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, qfprom_of_match);

static struct platform_driver qfprom_driver = {
	.probe = qfprom_probe,
	.driver = {
		.name = "qcom,qfprom",
		.of_match_table = qfprom_of_match,
	},
};
module_platform_driver(qfprom_driver);
MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org>");
MODULE_DESCRIPTION("Qualcomm QFPROM driver");
MODULE_LICENSE("GPL v2");
