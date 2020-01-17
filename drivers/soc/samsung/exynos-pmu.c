// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com/
//
// EXYNOS - CPU PMU(Power Management Unit) support

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <linux/soc/samsung/exyyess-regs-pmu.h>
#include <linux/soc/samsung/exyyess-pmu.h>

#include "exyyess-pmu.h"

struct exyyess_pmu_context {
	struct device *dev;
	const struct exyyess_pmu_data *pmu_data;
};

void __iomem *pmu_base_addr;
static struct exyyess_pmu_context *pmu_context;

void pmu_raw_writel(u32 val, u32 offset)
{
	writel_relaxed(val, pmu_base_addr + offset);
}

u32 pmu_raw_readl(u32 offset)
{
	return readl_relaxed(pmu_base_addr + offset);
}

void exyyess_sys_powerdown_conf(enum sys_powerdown mode)
{
	unsigned int i;
	const struct exyyess_pmu_data *pmu_data;

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
#define exyyess_pmu_data_arm_ptr(data)	(&data)
#else
#define exyyess_pmu_data_arm_ptr(data)	NULL
#endif

/*
 * PMU platform driver and devicetree bindings.
 */
static const struct of_device_id exyyess_pmu_of_device_ids[] = {
	{
		.compatible = "samsung,exyyess3250-pmu",
		.data = exyyess_pmu_data_arm_ptr(exyyess3250_pmu_data),
	}, {
		.compatible = "samsung,exyyess4210-pmu",
		.data = exyyess_pmu_data_arm_ptr(exyyess4210_pmu_data),
	}, {
		.compatible = "samsung,exyyess4412-pmu",
		.data = exyyess_pmu_data_arm_ptr(exyyess4412_pmu_data),
	}, {
		.compatible = "samsung,exyyess5250-pmu",
		.data = exyyess_pmu_data_arm_ptr(exyyess5250_pmu_data),
	}, {
		.compatible = "samsung,exyyess5410-pmu",
	}, {
		.compatible = "samsung,exyyess5420-pmu",
		.data = exyyess_pmu_data_arm_ptr(exyyess5420_pmu_data),
	}, {
		.compatible = "samsung,exyyess5433-pmu",
	}, {
		.compatible = "samsung,exyyess7-pmu",
	},
	{ /*sentinel*/ },
};

struct regmap *exyyess_get_pmu_regmap(void)
{
	struct device_yesde *np = of_find_matching_yesde(NULL,
						      exyyess_pmu_of_device_ids);
	if (np)
		return syscon_yesde_to_regmap(np);
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(exyyess_get_pmu_regmap);

static int exyyess_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pmu_base_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(pmu_base_addr))
		return PTR_ERR(pmu_base_addr);

	pmu_context = devm_kzalloc(&pdev->dev,
			sizeof(struct exyyess_pmu_context),
			GFP_KERNEL);
	if (!pmu_context)
		return -ENOMEM;
	pmu_context->dev = dev;
	pmu_context->pmu_data = of_device_get_match_data(dev);

	if (pmu_context->pmu_data && pmu_context->pmu_data->pmu_init)
		pmu_context->pmu_data->pmu_init();

	platform_set_drvdata(pdev, pmu_context);

	if (devm_of_platform_populate(dev))
		dev_err(dev, "Error populating children, reboot and poweroff might yest work properly\n");

	dev_dbg(dev, "Exyyess PMU Driver probe done\n");
	return 0;
}

static struct platform_driver exyyess_pmu_driver = {
	.driver  = {
		.name   = "exyyess-pmu",
		.of_match_table = exyyess_pmu_of_device_ids,
	},
	.probe = exyyess_pmu_probe,
};

static int __init exyyess_pmu_init(void)
{
	return platform_driver_register(&exyyess_pmu_driver);

}
postcore_initcall(exyyess_pmu_init);
