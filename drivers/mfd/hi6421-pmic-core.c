// SPDX-License-Identifier: GPL-2.0-only
/*
 * Device driver for Hi6421 PMIC
 *
 * Copyright (c) <2011-2014> HiSilicon Technologies Co., Ltd.
 *              http://www.hisilicon.com
 * Copyright (c) <2013-2017> Linaro Ltd.
 *              https://www.linaro.org
 *
 * Author: Guodong Xu <guodong.xu@linaro.org>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/mfd/core.h>
#include <linux/mfd/hi6421-pmic.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

static const struct mfd_cell hi6421_devs[] = {
	{ .name = "hi6421-regulator", },
};

static const struct mfd_cell hi6421v530_devs[] = {
	{ .name = "hi6421v530-regulator", },
};

static const struct regmap_config hi6421_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 8,
	.max_register = HI6421_REG_TO_BUS_ADDR(HI6421_REG_MAX),
};

static const struct of_device_id of_hi6421_pmic_match[] = {
	{
		.compatible = "hisilicon,hi6421-pmic",
		.data = (void *)HI6421
	},
	{
		.compatible = "hisilicon,hi6421v530-pmic",
		.data = (void *)HI6421_V530
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_hi6421_pmic_match);

static int hi6421_pmic_probe(struct platform_device *pdev)
{
	struct hi6421_pmic *pmic;
	const struct mfd_cell *subdevs;
	enum hi6421_type type;
	void __iomem *base;
	int n_subdevs, ret;

	type = (uintptr_t)device_get_match_data(&pdev->dev);

	pmic = devm_kzalloc(&pdev->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(base))
		return PTR_ERR(base);

	pmic->regmap = devm_regmap_init_mmio_clk(&pdev->dev, NULL, base,
						 &hi6421_regmap_config);
	if (IS_ERR(pmic->regmap)) {
		dev_err(&pdev->dev, "Failed to initialise Regmap: %ld\n",
						PTR_ERR(pmic->regmap));
		return PTR_ERR(pmic->regmap);
	}

	platform_set_drvdata(pdev, pmic);

	switch (type) {
	case HI6421:
		/* set over-current protection debounce 8ms */
		regmap_update_bits(pmic->regmap, HI6421_OCP_DEB_CTRL_REG,
				(HI6421_OCP_DEB_SEL_MASK
				 | HI6421_OCP_EN_DEBOUNCE_MASK
				 | HI6421_OCP_AUTO_STOP_MASK),
				(HI6421_OCP_DEB_SEL_8MS
				 | HI6421_OCP_EN_DEBOUNCE_ENABLE));

		subdevs = hi6421_devs;
		n_subdevs = ARRAY_SIZE(hi6421_devs);
		break;
	case HI6421_V530:
		subdevs = hi6421v530_devs;
		n_subdevs = ARRAY_SIZE(hi6421v530_devs);
		break;
	default:
		dev_err(&pdev->dev, "Unknown device type %d\n",
						(unsigned int)type);
		return -EINVAL;
	}

	ret = devm_mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE,
				   subdevs, n_subdevs, NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add child devices: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct platform_driver hi6421_pmic_driver = {
	.driver = {
		.name = "hi6421_pmic",
		.of_match_table = of_hi6421_pmic_match,
	},
	.probe	= hi6421_pmic_probe,
};
module_platform_driver(hi6421_pmic_driver);

MODULE_AUTHOR("Guodong Xu <guodong.xu@linaro.org>");
MODULE_DESCRIPTION("Hi6421 PMIC driver");
MODULE_LICENSE("GPL v2");
