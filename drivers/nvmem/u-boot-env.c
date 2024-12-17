// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "layouts/u-boot-env.h"

struct u_boot_env {
	struct device *dev;
	struct nvmem_device *nvmem;
	enum u_boot_env_format format;

	struct mtd_info *mtd;
};

static int u_boot_env_read(void *context, unsigned int offset, void *val,
			   size_t bytes)
{
	struct u_boot_env *priv = context;
	struct device *dev = priv->dev;
	size_t bytes_read;
	int err;

	err = mtd_read(priv->mtd, offset, bytes, &bytes_read, val);
	if (err && !mtd_is_bitflip(err)) {
		dev_err(dev, "Failed to read from mtd: %d\n", err);
		return err;
	}

	if (bytes_read != bytes) {
		dev_err(dev, "Failed to read %zu bytes\n", bytes);
		return -EIO;
	}

	return 0;
}

static int u_boot_env_probe(struct platform_device *pdev)
{
	struct nvmem_config config = {
		.name = "u-boot-env",
		.reg_read = u_boot_env_read,
	};
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct u_boot_env *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	priv->format = (uintptr_t)of_device_get_match_data(dev);

	priv->mtd = of_get_mtd_device_by_node(np);
	if (IS_ERR(priv->mtd)) {
		dev_err_probe(dev, PTR_ERR(priv->mtd), "Failed to get %pOF MTD\n", np);
		return PTR_ERR(priv->mtd);
	}

	config.dev = dev;
	config.priv = priv;
	config.size = priv->mtd->size;

	priv->nvmem = devm_nvmem_register(dev, &config);
	if (IS_ERR(priv->nvmem))
		return PTR_ERR(priv->nvmem);

	return u_boot_env_parse(dev, priv->nvmem, priv->format);
}

static const struct of_device_id u_boot_env_of_match_table[] = {
	{ .compatible = "u-boot,env", .data = (void *)U_BOOT_FORMAT_SINGLE, },
	{ .compatible = "u-boot,env-redundant-bool", .data = (void *)U_BOOT_FORMAT_REDUNDANT, },
	{ .compatible = "u-boot,env-redundant-count", .data = (void *)U_BOOT_FORMAT_REDUNDANT, },
	{ .compatible = "brcm,env", .data = (void *)U_BOOT_FORMAT_BROADCOM, },
	{},
};

static struct platform_driver u_boot_env_driver = {
	.probe = u_boot_env_probe,
	.driver = {
		.name = "u_boot_env",
		.of_match_table = u_boot_env_of_match_table,
	},
};
module_platform_driver(u_boot_env_driver);

MODULE_AUTHOR("Rafał Miłecki");
MODULE_DESCRIPTION("U-Boot environment variables support module");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, u_boot_env_of_match_table);
