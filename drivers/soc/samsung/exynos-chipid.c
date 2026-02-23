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

#include <linux/array_size.h>
#include <linux/device.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/ioport.h>
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
	unsigned int main_rev_reg;	/* main revision register offset */
	unsigned int sub_rev_reg;	/* sub revision register offset */
	unsigned int main_rev_shift;	/* main revision offset in rev_reg */
	unsigned int sub_rev_shift;	/* sub revision offset in rev_reg */
	bool efuse;
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
	{ "EXYNOS7870", 0xE7870000 },
	{ "EXYNOS8890", 0xE8890000 },
	/* Compatible with: samsung,exynos850-chipid */
	{ "EXYNOS2200", 0xE9925000 },
	{ "EXYNOS7885", 0xE7885000 },
	{ "EXYNOS850", 0xE3830000 },
	{ "EXYNOS8895", 0xE8895000 },
	{ "EXYNOS9610", 0xE9610000 },
	{ "EXYNOS9810", 0xE9810000 },
	{ "EXYNOS990", 0xE9830000 },
	{ "EXYNOSAUTOV9", 0xAAA80000 },
	{ "EXYNOSAUTOV920", 0x0A920000 },
	/* Compatible with: google,gs101-otp */
	{ "GS101", 0x9845000 },
};

static const char *exynos_product_id_to_name(unsigned int product_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(soc_ids); i++)
		if (product_id == soc_ids[i].id)
			return soc_ids[i].name;
	return NULL;
}

static int exynos_chipid_get_chipid_info(struct device *dev,
		struct regmap *regmap, const struct exynos_chipid_variant *data,
		struct exynos_chipid_info *soc_info)
{
	int ret;
	unsigned int val, main_rev, sub_rev;

	ret = regmap_read(regmap, EXYNOS_CHIPID_REG_PRO_ID, &val);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to read Product ID\n");
	soc_info->product_id = val & EXYNOS_MASK;

	if (data->sub_rev_reg == EXYNOS_CHIPID_REG_PRO_ID) {
		/* exynos4210 case */
		main_rev = (val >> data->main_rev_shift) & EXYNOS_REV_PART_MASK;
		sub_rev = (val >> data->sub_rev_shift) & EXYNOS_REV_PART_MASK;
	} else {
		unsigned int val2;

		ret = regmap_read(regmap, data->sub_rev_reg, &val2);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "failed to read revision\n");

		if (data->main_rev_reg == EXYNOS_CHIPID_REG_PRO_ID)
			/* gs101 case */
			main_rev = (val >> data->main_rev_shift) & EXYNOS_REV_PART_MASK;
		else
			/* exynos850 case */
			main_rev = (val2 >> data->main_rev_shift) & EXYNOS_REV_PART_MASK;

		sub_rev = (val2 >> data->sub_rev_shift) & EXYNOS_REV_PART_MASK;
	}

	soc_info->revision = (main_rev << EXYNOS_REV_PART_SHIFT) | sub_rev;

	return 0;
}

static struct regmap *exynos_chipid_get_efuse_regmap(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *base;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return ERR_CAST(base);

	const struct regmap_config reg_config = {
		.reg_bits = 32,
		.reg_stride = 4,
		.val_bits = 32,
		.use_relaxed_mmio = true,
		.max_register = (resource_size(res) - reg_config.reg_stride),
	};

	return devm_regmap_init_mmio_clk(&pdev->dev, "pclk", base, &reg_config);
}

static void exynos_chipid_unregister_soc(void *data)
{
	soc_device_unregister(data);
}

static int exynos_chipid_probe(struct platform_device *pdev)
{
	const struct exynos_chipid_variant *drv_data;
	struct exynos_chipid_info soc_info;
	struct soc_device_attribute *soc_dev_attr;
	struct device *dev = &pdev->dev;
	struct soc_device *soc_dev;
	struct device_node *root;
	struct regmap *regmap;
	int ret;

	drv_data = of_device_get_match_data(dev);
	if (!drv_data)
		return dev_err_probe(dev, -EINVAL,
				     "failed to get match data\n");

	if (drv_data->efuse)
		regmap = exynos_chipid_get_efuse_regmap(pdev);
	else
		regmap = device_node_to_regmap(dev->of_node);

	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to get regmap\n");

	ret = exynos_chipid_get_chipid_info(dev, regmap, drv_data, &soc_info);
	if (ret < 0)
		return ret;

	soc_dev_attr = devm_kzalloc(dev, sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = "Samsung Exynos";

	root = of_find_node_by_path("/");
	of_property_read_string(root, "model", &soc_dev_attr->machine);
	of_node_put(root);

	soc_dev_attr->revision = devm_kasprintf(dev, GFP_KERNEL, "%x",
						soc_info.revision);
	if (!soc_dev_attr->revision)
		return -ENOMEM;

	soc_dev_attr->soc_id = exynos_product_id_to_name(soc_info.product_id);
	if (!soc_dev_attr->soc_id)
		return dev_err_probe(dev, -ENODEV, "Unknown SoC\n");

	/* please note that the actual registration will be deferred */
	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev))
		return dev_err_probe(dev, PTR_ERR(soc_dev),
				     "failed to register to the soc interface\n");

	ret = devm_add_action_or_reset(dev, exynos_chipid_unregister_soc,
				       soc_dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add devm action\n");

	ret = exynos_asv_init(dev, regmap);
	if (ret)
		return ret;

	dev_dbg(dev, "Exynos: CPU[%s] PRO_ID[0x%x] REV[0x%x] Detected\n",
		soc_dev_attr->soc_id, soc_info.product_id, soc_info.revision);

	return 0;
}

static const struct exynos_chipid_variant exynos4210_chipid_drv_data = {
	.main_rev_shift	= 4,
	.sub_rev_shift	= 0,
};

static const struct exynos_chipid_variant exynos850_chipid_drv_data = {
	.main_rev_reg	= 0x10,
	.sub_rev_reg	= 0x10,
	.main_rev_shift	= 20,
	.sub_rev_shift	= 16,
};

static const struct exynos_chipid_variant gs101_chipid_drv_data = {
	.sub_rev_reg	= 0x10,
	.sub_rev_shift	= 16,
	.efuse = true,
};

static const struct of_device_id exynos_chipid_of_device_ids[] = {
	{
		.compatible	= "google,gs101-otp",
		.data		= &gs101_chipid_drv_data,
	}, {
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
	.probe = exynos_chipid_probe,
};
module_platform_driver(exynos_chipid_driver);

MODULE_DESCRIPTION("Samsung Exynos ChipID controller and ASV driver");
MODULE_AUTHOR("Bartlomiej Zolnierkiewicz <b.zolnierkie@samsung.com>");
MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_AUTHOR("Pankaj Dubey <pankaj.dubey@samsung.com>");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL");
