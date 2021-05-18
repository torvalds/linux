// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>

struct brcm_nvram {
	struct device *dev;
	void __iomem *base;
};

static int brcm_nvram_read(void *context, unsigned int offset, void *val,
			   size_t bytes)
{
	struct brcm_nvram *priv = context;
	u8 *dst = val;

	while (bytes--)
		*dst++ = readb(priv->base + offset++);

	return 0;
}

static int brcm_nvram_probe(struct platform_device *pdev)
{
	struct nvmem_config config = {
		.name = "brcm-nvram",
		.reg_read = brcm_nvram_read,
	};
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct brcm_nvram *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	config.dev = dev;
	config.priv = priv;
	config.size = resource_size(res);

	return PTR_ERR_OR_ZERO(devm_nvmem_register(dev, &config));
}

static const struct of_device_id brcm_nvram_of_match_table[] = {
	{ .compatible = "brcm,nvram", },
	{},
};

static struct platform_driver brcm_nvram_driver = {
	.probe = brcm_nvram_probe,
	.driver = {
		.name = "brcm_nvram",
		.of_match_table = brcm_nvram_of_match_table,
	},
};

static int __init brcm_nvram_init(void)
{
	return platform_driver_register(&brcm_nvram_driver);
}

subsys_initcall_sync(brcm_nvram_init);

MODULE_AUTHOR("Rafał Miłecki");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, brcm_nvram_of_match_table);
