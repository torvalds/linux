// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/slab.h>

struct imem_chip {
	struct regmap *regmap;
	struct nvmem_config imem_config;
	void __iomem *base;
	unsigned int size;
};

static int imem_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct imem_chip *imem = priv;

	if (offset + bytes > imem->size)
		return -EINVAL;

	memcpy_fromio(val, imem->base + offset, bytes);

	return 0;
}

static int imem_write(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct imem_chip *imem = priv;

	if (offset + bytes > imem->size)
		return -EINVAL;

	memcpy_toio(imem->base + offset, val, bytes);

	return 0;
}

static int imem_probe(struct platform_device *pdev)
{
	struct imem_chip *imem;
	struct resource *res;
	struct nvmem_device *nvmem;

	imem = devm_kzalloc(&pdev->dev, sizeof(*imem), GFP_KERNEL);
	if (!imem)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get memory resource\n");
		return -ENODEV;
	}

	imem->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(imem->base))
		return PTR_ERR(imem->base);

	imem->size = resource_size(res);
	imem->imem_config.dev = &pdev->dev;
	imem->imem_config.name = "restart_reason";
	imem->imem_config.id = NVMEM_DEVID_AUTO;
	imem->imem_config.owner = THIS_MODULE;
	imem->imem_config.stride = 1;
	imem->imem_config.word_size = 1;
	imem->imem_config.reg_read = imem_read;
	imem->imem_config.reg_write = imem_write;
	imem->imem_config.priv = imem;
	imem->imem_config.size = resource_size(res);
	nvmem = devm_nvmem_register(&pdev->dev, &imem->imem_config);
	if (IS_ERR(nvmem)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(nvmem),
			"Couldn't register nvmem device");
	}

	dev_dbg(&pdev->dev, "IMEM base=%#x size=%u registered successfully\n",
		imem->base, imem->size);

	return 0;
}

static const struct of_device_id imem_match_table[] = {
	{ .compatible = "qcom,msm-imem-restart_reason" },
	{},
};
MODULE_DEVICE_TABLE(of, imem_match_table);

static struct platform_driver imem_driver = {
	.probe = imem_probe,
	.driver = {
		.name = "qcom,msm-imem-restart_reason",
		.of_match_table = imem_match_table,
	},
};

module_platform_driver(imem_driver);

MODULE_DESCRIPTION("QCOM MSM imem nvmem provider driver");
MODULE_LICENSE("GPL");
