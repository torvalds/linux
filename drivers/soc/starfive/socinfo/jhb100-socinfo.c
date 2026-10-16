// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 StarFive Technology Co., Ltd.
 *
 * Author: Changhuang Liang <changhuang.liang@starfivetech.com>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sys_soc.h>

#define JHB100_REV_ID			0x38
#define JHB100_REV_ID_CHAR		GENMASK(3, 2)
#define JHB100_REV_ID_NUM		GENMASK(1, 0)

/*
 * The sys0 system controller region only contains the PLL controls, this
 * revision ID register and a few debug registers, so instead of describing
 * a child node in the devicetree the PLL device is instantiated from here.
 */
static const struct mfd_cell jhb100_sys0_syscon_cells[] = {
	{ .name = "jhb100-sys0-pll", },
};

static void jhb100_socinfo_unregister(void *data)
{
	soc_device_unregister(data);
}

static int jhb100_socinfo_probe(struct platform_device *pdev)
{
	struct soc_device_attribute *attrs;
	struct device *dev = &pdev->dev;
	struct soc_device *soc_dev;
	struct regmap *regmap;
	char rev_char;
	u32 rev_id;
	int ret;

	regmap = syscon_node_to_regmap(dev->of_node);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to get syscon regmap\n");

	ret = regmap_read(regmap, JHB100_REV_ID, &rev_id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read revision ID\n");

	attrs = devm_kzalloc(dev, sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	rev_char = FIELD_GET(JHB100_REV_ID_CHAR, rev_id) + 'A';

	attrs->family = "JH";
	attrs->soc_id = "JHB100";
	attrs->revision = devm_kasprintf(dev, GFP_KERNEL, "%c%lu", rev_char,
					 FIELD_GET(JHB100_REV_ID_NUM, rev_id));
	if (!attrs->revision)
		return -ENOMEM;

	soc_dev = soc_device_register(attrs);
	if (IS_ERR(soc_dev))
		return dev_err_probe(dev, PTR_ERR(soc_dev),
				     "failed to register SoC device\n");

	ret = devm_add_action_or_reset(dev, jhb100_socinfo_unregister, soc_dev);
	if (ret)
		return ret;

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO,
				   jhb100_sys0_syscon_cells,
				   ARRAY_SIZE(jhb100_sys0_syscon_cells),
				   NULL, 0, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add mfd cells\n");

	dev_info(dev, "StarFive %s SoC rev(%s)\n", attrs->soc_id, attrs->revision);

	return 0;
}

static const struct of_device_id jhb100_socinfo_of_match[] = {
	{ .compatible = "starfive,jhb100-sys0-syscon" },
	{ /* sentinel */ }
};

static struct platform_driver jhb100_socinfo_driver = {
	.probe  = jhb100_socinfo_probe,
	.driver = {
		.name = "jhb100-socinfo",
		.of_match_table = jhb100_socinfo_of_match,
	},
};

static int __init jhb100_socinfo_init(void)
{
	return platform_driver_register(&jhb100_socinfo_driver);
}
subsys_initcall(jhb100_socinfo_init);
