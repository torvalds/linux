// SPDX-License-Identifier: GPL-2.0
/*
 * Device driver for regulators in HISI PMIC IC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2011 Hisilicon.
 * Copyright (c) 2020-2021 Huawei Technologies Co., Ltd.
 */

#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spmi.h>

static const struct mfd_cell hi6421v600_devs[] = {
	{ .name = "hi6421v600-irq", },
	{ .name = "hi6421v600-regulator", },
};

static const struct regmap_config regmap_config = {
	.reg_bits	= 16,
	.val_bits	= BITS_PER_BYTE,
	.max_register	= 0xffff,
	.fast_io	= true
};

static int hi6421_spmi_pmic_probe(struct spmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_spmi_ext(sdev, &regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	dev_set_drvdata(&sdev->dev, regmap);

	ret = devm_mfd_add_devices(&sdev->dev, PLATFORM_DEVID_NONE,
				   hi6421v600_devs, ARRAY_SIZE(hi6421v600_devs),
				   NULL, 0, NULL);
	if (ret < 0)
		dev_err(dev, "Failed to add child devices: %d\n", ret);

	return ret;
}

static const struct of_device_id pmic_spmi_id_table[] = {
	{ .compatible = "hisilicon,hi6421-spmi" },
	{ }
};
MODULE_DEVICE_TABLE(of, pmic_spmi_id_table);

static struct spmi_driver hi6421_spmi_pmic_driver = {
	.driver = {
		.name	= "hi6421-spmi-pmic",
		.of_match_table = pmic_spmi_id_table,
	},
	.probe	= hi6421_spmi_pmic_probe,
};
module_spmi_driver(hi6421_spmi_pmic_driver);

MODULE_DESCRIPTION("HiSilicon Hi6421v600 SPMI PMIC driver");
MODULE_LICENSE("GPL v2");
