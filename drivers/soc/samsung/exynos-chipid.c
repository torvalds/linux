// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 * Copyright (c) 2020 Krzysztof Kozlowski <krzk@kernel.org>
 *
 * Exynos - CHIP ID support
 * Author: Pankaj Dubey <pankaj.dubey@samsung.com>
 * Author: Bartlomiej Zolnierkiewicz <b.zolnierkie@samsung.com>
 * Author: Krzysztof Kozlowski <krzk@kernel.org>
 *
 * Samsung Exynos SoC Adaptive Supply Voltage and Chip ID support
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/soc/samsung/exynos-chipid.h>
#include <linux/sys_soc.h>

#include "exynos-asv.h"

struct exynos_chipid_variant {
	unsigned int rev_reg;		/* revision register offset */
	unsigned int main_rev_shift;	/* main revision offset in rev_reg */
	unsigned int sub_rev_shift;	/* sub revision offset in rev_reg */
};

struct exynos_chipid_info {
	u32 product_id;
	u32 revision;
};

static const struct exynos_soc_id {
	const char *name;
	unsigned int id;
} soc_ids[] = {
	/* List ordered by SoC name */
	/* Compatible with: samsung,exynos4210-chipid */
	{ "EXYNOS3250", 0xE3472000 },
	{ "EXYNOS4210", 0x43200000 },	/* EVT0 revision */
	{ "EXYNOS4210", 0x43210000 },
	{ "EXYNOS4212", 0x43220000 },
	{ "EXYNOS4412", 0xE4412000 },
	{ "EXYNOS5250", 0x43520000 },
	{ "EXYNOS5260", 0xE5260000 },
	{ "EXYNOS5410", 0xE5410000 },
	{ "EXYNOS5420", 0xE5420000 },
	{ "EXYNOS5433", 0xE5433000 },
	{ "EXYNOS5440", 0xE5440000 },
	{ "EXYNOS5800", 0xE5422000 },
	{ "EXYNOS7420", 0xE7420000 },
	/* Compatible with: samsung,exynos850-chipid */
	{ "EXYNOS7885", 0xE7885000 },
	{ "EXYNOS850", 0xE3830000 },
	{ "EXYNOSAUTOV9", 0xAAA80000 },
};

static const char *product_id_to_soc_id(unsigned int product_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(soc_ids); i++)
		if (product_id == soc_ids[i].id)
			return soc_ids[i].name;
	return NULL;
}

static int exynos_chipid_get_chipid_info(struct regmap *regmap,
		const struct exynos_chipid_variant *data,
		struct exynos_chipid_info *soc_info)
{
	int ret;
	unsigned int val, main_rev, sub_rev;

	ret = regmap_read(regmap, EXYNOS_CHIPID_REG_PRO_ID, &val);
	if (ret < 0)
		return ret;
	soc_info->product_id = val & EXYNOS_MASK;

	if (data->rev_reg != EXYNOS_CHIPID_REG_PRO_ID) {
		ret = regmap_read(regmap, data->rev_reg, &val);
		if (ret < 0)
			return ret;
	}
	main_rev = (val >> data->main_rev_shift) & EXYNOS_REV_PART_MASK;
	sub_rev = (val >> data->sub_rev_shift) & EXYNOS_REV_PART_MASK;
	soc_info->revision = (main_rev << EXYNOS_REV_PART_SHIFT) | sub_rev;

	return 0;
}

static int exynos_chipid_probe(struct platform_device *pdev)
{
	const struct exynos_chipid_variant *drv_data;
	struct exynos_chipid_info soc_info;
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;
	struct device_node *root;
	struct regmap *regmap;
	int ret;

	drv_data = of_device_get_match_data(&pdev->dev);
	if (!drv_data)
		return -EINVAL;

	regmap = device_node_to_regmap(pdev->dev.of_node);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = exynos_chipid_get_chipid_info(regmap, drv_data, &soc_info);
	if (ret < 0)
		return ret;

	soc_dev_attr = devm_kzalloc(&pdev->dev, sizeof(*soc_dev_attr),
				    GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = "Samsung Exynos";

	root = of_find_node_by_path("/");
	of_property_read_string(root, "model", &soc_dev_attr->machine);
	of_node_put(root);

	soc_dev_attr->revision = devm_kasprintf(&pdev->dev, GFP_KERNEL,
						"%x", soc_info.revision);
	soc_dev_attr->soc_id = product_id_to_soc_id(soc_info.product_id);
	if (!soc_dev_attr->soc_id) {
		pr_err("Unknown SoC\n");
		return -ENODEV;
	}

	/* please note that the actual registration will be deferred */
	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev))
		return PTR_ERR(soc_dev);

	ret = exynos_asv_init(&pdev->dev, regmap);
	if (ret)
		goto err;

	platform_set_drvdata(pdev, soc_dev);

	dev_info(&pdev->dev, "Exynos: CPU[%s] PRO_ID[0x%x] REV[0x%x] Detected\n",
		 soc_dev_attr->soc_id, soc_info.product_id, soc_info.revision);

	return 0;

err:
	soc_device_unregister(soc_dev);

	return ret;
}

static int exynos_chipid_remove(struct platform_device *pdev)
{
	struct soc_device *soc_dev = platform_get_drvdata(pdev);

	soc_device_unregister(soc_dev);

	return 0;
}

static const struct exynos_chipid_variant exynos4210_chipid_drv_data = {
	.rev_reg	= 0x0,
	.main_rev_shift	= 4,
	.sub_rev_shift	= 0,
};

static const struct exynos_chipid_variant exynos850_chipid_drv_data = {
	.rev_reg	= 0x10,
	.main_rev_shift	= 20,
	.sub_rev_shift	= 16,
};

static const struct of_device_id exynos_chipid_of_device_ids[] = {
	{
		.compatible	= "samsung,exynos4210-chipid",
		.data		= &exynos4210_chipid_drv_data,
	}, {
		.compatible	= "samsung,exynos850-chipid",
		.data		= &exynos850_chipid_drv_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_chipid_of_device_ids);

static struct platform_driver exynos_chipid_driver = {
	.driver = {
		.name = "exynos-chipid",
		.of_match_table = exynos_chipid_of_device_ids,
	},
	.probe	= exynos_chipid_probe,
	.remove	= exynos_chipid_remove,
};
module_platform_driver(exynos_chipid_driver);

MODULE_DESCRIPTION("Samsung Exynos ChipID controller and ASV driver");
MODULE_AUTHOR("Bartlomiej Zolnierkiewicz <b.zolnierkie@samsung.com>");
MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_AUTHOR("Pankaj Dubey <pankaj.dubey@samsung.com>");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL");
