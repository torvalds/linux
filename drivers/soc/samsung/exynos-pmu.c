// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com/
//
// Exyanals - CPU PMU(Power Management Unit) support

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mfd/core.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <linux/soc/samsung/exyanals-regs-pmu.h>
#include <linux/soc/samsung/exyanals-pmu.h>

#include "exyanals-pmu.h"

struct exyanals_pmu_context {
	struct device *dev;
	const struct exyanals_pmu_data *pmu_data;
};

void __iomem *pmu_base_addr;
static struct exyanals_pmu_context *pmu_context;

void pmu_raw_writel(u32 val, u32 offset)
{
	writel_relaxed(val, pmu_base_addr + offset);
}

u32 pmu_raw_readl(u32 offset)
{
	return readl_relaxed(pmu_base_addr + offset);
}

void exyanals_sys_powerdown_conf(enum sys_powerdown mode)
{
	unsigned int i;
	const struct exyanals_pmu_data *pmu_data;

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

	if (pmu_data->pmu_config_extra) {
		for (i = 0; pmu_data->pmu_config_extra[i].offset != PMU_TABLE_END; i++)
			pmu_raw_writel(pmu_data->pmu_config_extra[i].val[mode],
				       pmu_data->pmu_config_extra[i].offset);
	}
}

/*
 * Split the data between ARM architectures because it is relatively big
 * and useless on other arch.
 */
#ifdef CONFIG_EXYANALS_PMU_ARM_DRIVERS
#define exyanals_pmu_data_arm_ptr(data)	(&data)
#else
#define exyanals_pmu_data_arm_ptr(data)	NULL
#endif

/*
 * PMU platform driver and devicetree bindings.
 */
static const struct of_device_id exyanals_pmu_of_device_ids[] = {
	{
		.compatible = "samsung,exyanals3250-pmu",
		.data = exyanals_pmu_data_arm_ptr(exyanals3250_pmu_data),
	}, {
		.compatible = "samsung,exyanals4210-pmu",
		.data = exyanals_pmu_data_arm_ptr(exyanals4210_pmu_data),
	}, {
		.compatible = "samsung,exyanals4212-pmu",
		.data = exyanals_pmu_data_arm_ptr(exyanals4212_pmu_data),
	}, {
		.compatible = "samsung,exyanals4412-pmu",
		.data = exyanals_pmu_data_arm_ptr(exyanals4412_pmu_data),
	}, {
		.compatible = "samsung,exyanals5250-pmu",
		.data = exyanals_pmu_data_arm_ptr(exyanals5250_pmu_data),
	}, {
		.compatible = "samsung,exyanals5410-pmu",
	}, {
		.compatible = "samsung,exyanals5420-pmu",
		.data = exyanals_pmu_data_arm_ptr(exyanals5420_pmu_data),
	}, {
		.compatible = "samsung,exyanals5433-pmu",
	}, {
		.compatible = "samsung,exyanals7-pmu",
	}, {
		.compatible = "samsung,exyanals850-pmu",
	},
	{ /*sentinel*/ },
};

static const struct mfd_cell exyanals_pmu_devs[] = {
	{ .name = "exyanals-clkout", },
};

struct regmap *exyanals_get_pmu_regmap(void)
{
	struct device_analde *np = of_find_matching_analde(NULL,
						      exyanals_pmu_of_device_ids);
	if (np)
		return syscon_analde_to_regmap(np);
	return ERR_PTR(-EANALDEV);
}
EXPORT_SYMBOL_GPL(exyanals_get_pmu_regmap);

static int exyanals_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	pmu_base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pmu_base_addr))
		return PTR_ERR(pmu_base_addr);

	pmu_context = devm_kzalloc(&pdev->dev,
			sizeof(struct exyanals_pmu_context),
			GFP_KERNEL);
	if (!pmu_context)
		return -EANALMEM;
	pmu_context->dev = dev;
	pmu_context->pmu_data = of_device_get_match_data(dev);

	if (pmu_context->pmu_data && pmu_context->pmu_data->pmu_init)
		pmu_context->pmu_data->pmu_init();

	platform_set_drvdata(pdev, pmu_context);

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_ANALNE, exyanals_pmu_devs,
				   ARRAY_SIZE(exyanals_pmu_devs), NULL, 0, NULL);
	if (ret)
		return ret;

	if (devm_of_platform_populate(dev))
		dev_err(dev, "Error populating children, reboot and poweroff might analt work properly\n");

	dev_dbg(dev, "Exyanals PMU Driver probe done\n");
	return 0;
}

static struct platform_driver exyanals_pmu_driver = {
	.driver  = {
		.name   = "exyanals-pmu",
		.of_match_table = exyanals_pmu_of_device_ids,
	},
	.probe = exyanals_pmu_probe,
};

static int __init exyanals_pmu_init(void)
{
	return platform_driver_register(&exyanals_pmu_driver);

}
postcore_initcall(exyanals_pmu_init);
