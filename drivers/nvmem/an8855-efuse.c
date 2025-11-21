// SPDX-License-Identifier: GPL-2.0
/*
 *  Airoha AN8855 Switch EFUSE Driver
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define AN8855_EFUSE_CELL		50

#define AN8855_EFUSE_DATA0		0x1000a500
#define   AN8855_EFUSE_R50O		GENMASK(30, 24)

static int an8855_efuse_read(void *context, unsigned int offset,
			     void *val, size_t bytes)
{
	struct regmap *regmap = context;

	return regmap_bulk_read(regmap, AN8855_EFUSE_DATA0 + offset,
				val, bytes / sizeof(u32));
}

static int an8855_efuse_probe(struct platform_device *pdev)
{
	struct nvmem_config an8855_nvmem_config = {
		.name = "an8855-efuse",
		.size = AN8855_EFUSE_CELL * sizeof(u32),
		.stride = sizeof(u32),
		.word_size = sizeof(u32),
		.reg_read = an8855_efuse_read,
	};
	struct device *dev = &pdev->dev;
	struct nvmem_device *nvmem;
	struct regmap *regmap;

	/* Assign NVMEM priv to MFD regmap */
	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENOENT;

	an8855_nvmem_config.priv = regmap;
	an8855_nvmem_config.dev = dev;
	nvmem = devm_nvmem_register(dev, &an8855_nvmem_config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id an8855_efuse_of_match[] = {
	{ .compatible = "airoha,an8855-efuse", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, an8855_efuse_of_match);

static struct platform_driver an8855_efuse_driver = {
	.probe = an8855_efuse_probe,
	.driver = {
		.name = "an8855-efuse",
		.of_match_table = an8855_efuse_of_match,
	},
};
module_platform_driver(an8855_efuse_driver);

MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_DESCRIPTION("Driver for AN8855 Switch EFUSE");
MODULE_LICENSE("GPL");
