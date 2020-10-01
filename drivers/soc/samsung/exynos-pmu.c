// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com/
//
// Exynos - CPU PMU(Power Management Unit) support

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <linux/soc/samsung/exynos-regs-pmu.h>
#include <linux/soc/samsung/exynos-pmu.h>

#include "exynos-pmu.h"

struct exynos_pmu_context {
	struct device *dev;
	const struct exynos_pmu_data *pmu_data;
};

void __iomem *pmu_base_addr;
static struct exynos_pmu_context *pmu_context;

void pmu_raw_writel(u32 val, u32 offset)
{
	writel_relaxed(val, pmu_base_addr + offset);
}

u32 pmu_raw_readl(u32 offset)
{
	return readl_relaxed(pmu_base_addr + offset);
}

void exynos_sys_powerdown_conf(enum sys_powerdown mode)
{
	unsigned int i;
	const struct exynos_pmu_data *pmu_data;

	if (!pmu_context || !pmu_context->pmu_data)
		return;

	pmu_data = pmu_context->pmu_data;

	if (pmu_data->powerdown_conf)
		pmu_data->powerdown_conf(mode);

	if (pmu_data->pmu_config) {
		for (i = 0; (pmu_data->pmu_config[i].offset != PMU_TABLE_END); i++)
			pmu_raw_writel(pmu_data->pmu_config[i].val[mode],
					pmu_data->pmu_config[i].offset);
	}

	if (pmu_data->powerdown_conf_extra)
		pmu_data->powerdown_conf_extra(mode);
}

/*
 * Split the data between ARM architectures because it is relatively big
 * and useless on other arch.
 */
#ifdef CONFIG_EXYNOS_PMU_ARM_DRIVERS
#define exynos_pmu_data_arm_ptr(data)	(&data)
#else
#define exynos_pmu_data_arm_ptr(data)	NULL
#endif

/*
 * PMU platform driver and devicetree bindings.
 */
static const struct of_device_id exynos_pmu_of_device_ids[] = {
	{
		.compatible = "samsung,exynos3250-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos3250_pmu_data),
	}, {
		.compatible = "samsung,exynos4210-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos4210_pmu_data),
	}, {
		.compatible = "samsung,exynos4412-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos4412_pmu_data),
	}, {
		.compatible = "samsung,exynos5250-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos5250_pmu_data),
	}, {
		.compatible = "samsung,exynos5410-pmu",
	}, {
		.compatible = "samsung,exynos5420-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos5420_pmu_data),
	}, {
		.compatible = "samsung,exynos5433-pmu",
	}, {
		.compatible = "samsung,exynos7-pmu",
	},
	{ /*sentinel*/ },
};

static const struct mfd_cell exynos_pmu_devs[] = {
	{ .name = "exynos-clkout", },
};

struct regmap *exynos_get_pmu_regmap(void)
{
	struct device_node *np = of_find_matching_node(NULL,
						      exynos_pmu_of_device_ids);
	if (np)
		return syscon_node_to_regmap(np);
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(exynos_get_pmu_regmap);

static int exynos_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	pmu_base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pmu_base_addr))
		return PTR_ERR(pmu_base_addr);

	pmu_context = devm_kzalloc(&pdev->dev,
			sizeof(struct exynos_pmu_context),
			GFP_KERNEL);
	if (!pmu_context)
		return -ENOMEM;
	pmu_context->dev = dev;
	pmu_context->pmu_data = of_device_get_match_data(dev);

	if (pmu_context->pmu_data && pmu_context->pmu_data->pmu_init)
		pmu_context->pmu_data->pmu_init();

	platform_set_drvdata(pdev, pmu_context);

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, exynos_pmu_devs,
				   ARRAY_SIZE(exynos_pmu_devs), NULL, 0, NULL);
	if (ret)
		return ret;

	if (devm_of_platform_populate(dev))
		dev_err(dev, "Error populating children, reboot and poweroff might not work properly\n");

	dev_dbg(dev, "Exynos PMU Driver probe done\n");
	return 0;
}

static struct platform_driver exynos_pmu_driver = {
	.driver  = {
		.name   = "exynos-pmu",
		.of_match_table = exynos_pmu_of_device_ids,
	},
	.probe = exynos_pmu_probe,
};

static int __init exynos_pmu_init(void)
{
	return platform_driver_register(&exynos_pmu_driver);

}
postcore_initcall(exynos_pmu_init);
