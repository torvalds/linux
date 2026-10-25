// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2026 Roman Vivchar <rva333@protonmail.com>
 */

#include <linux/err.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <linux/mfd/mt6323/registers.h>

#define MT6323_EFUSE_DOUT_BASE	MT6323_EFUSE_DOUT_0_15
#define MT6323_EFUSE_SIZE	24

static int mt6323_efuse_read(void *context, unsigned int offset, void *val,
			     size_t bytes)
{
	struct regmap *map = context;
	u16 *buf = val;
	u32 tmp;
	int ret;

	/*
	 * A manual loop using regmap_read is required because PWRAP is not
	 * a continuous MMIO space, but rather a FSM that doesn't implement the
	 * necessary read callback for the regmap_read_raw and regmap_read_bulk
	 * functions.
	 */
	for (size_t i = 0; i < bytes; i += sizeof(*buf)) {
		ret = regmap_read(map, MT6323_EFUSE_DOUT_BASE + offset + i, &tmp);
		if (ret)
			return ret;

		*buf++ = tmp;
	}

	return 0;
}

static int mt6323_efuse_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6323 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct nvmem_config config = {
		.name = "mt6323-efuse",
		.stride = 2,
		.word_size = 2,
		.size = MT6323_EFUSE_SIZE,
		.reg_read = mt6323_efuse_read,
	};
	struct nvmem_device *nvmem;

	if (!mt6323 || !mt6323->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get parent\n");

	config.dev = dev;
	config.priv = mt6323->regmap;

	nvmem = devm_nvmem_register(dev, &config);
	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id mt6323_efuse_of_match[] = {
	{ .compatible = "mediatek,mt6323-efuse" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6323_efuse_of_match);

static struct platform_driver mt6323_efuse_driver = {
	.probe = mt6323_efuse_probe,
	.driver = {
		.name = "mt6323-efuse",
		.of_match_table = mt6323_efuse_of_match,
	},
};
module_platform_driver(mt6323_efuse_driver);

MODULE_DESCRIPTION("MediaTek MT6323 PMIC EFUSE driver");
MODULE_LICENSE("GPL");
