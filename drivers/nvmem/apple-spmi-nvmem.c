// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple SPMI NVMEM driver
 *
 * Copyright The Asahi Linux Contributors
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/spmi.h>
#include <linux/regmap.h>

static const struct regmap_config apple_spmi_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xffff,
};

static int apple_spmi_nvmem_probe(struct spmi_device *sdev)
{
	struct regmap *regmap;
	struct nvmem_config nvmem_cfg = {
		.dev = &sdev->dev,
		.name = "spmi_nvmem",
		.id = NVMEM_DEVID_AUTO,
		.word_size = 1,
		.stride = 1,
		.size = 0xffff,
		.reg_read = (void *)regmap_bulk_read,
		.reg_write = (void *)regmap_bulk_write,
	};

	regmap = devm_regmap_init_spmi_ext(sdev, &apple_spmi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	nvmem_cfg.priv = regmap;

	return PTR_ERR_OR_ZERO(devm_nvmem_register(&sdev->dev, &nvmem_cfg));
}

static const struct of_device_id apple_spmi_nvmem_id_table[] = {
	{ .compatible = "apple,spmi-nvmem" },
	{ },
};
MODULE_DEVICE_TABLE(of, apple_spmi_nvmem_id_table);

static struct spmi_driver apple_spmi_nvmem_driver = {
	.probe = apple_spmi_nvmem_probe,
	.driver = {
		.name = "apple-spmi-nvmem",
		.of_match_table	= apple_spmi_nvmem_id_table,
	},
};

module_spmi_driver(apple_spmi_nvmem_driver);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Hector Martin <marcan@marcan.st>");
MODULE_DESCRIPTION("Apple SPMI NVMEM driver");
