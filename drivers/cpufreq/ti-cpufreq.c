// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI CPUFreq/OPP hw-supported driver
 *
 * Copyright (C) 2016-2017 Texas Instruments, Inc.
 *	 Dave Gerlach <d-gerlach@ti.com>
 */

#include <linux/cpu.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define REVISION_MASK				0xF
#define REVISION_SHIFT				28

#define AM33XX_800M_ARM_MPU_MAX_FREQ		0x1E2F
#define AM43XX_600M_ARM_MPU_MAX_FREQ		0xFFA

#define DRA7_EFUSE_HAS_OD_MPU_OPP		11
#define DRA7_EFUSE_HAS_HIGH_MPU_OPP		15
#define DRA7_EFUSE_HAS_ALL_MPU_OPP		23

#define DRA7_EFUSE_NOM_MPU_OPP			BIT(0)
#define DRA7_EFUSE_OD_MPU_OPP			BIT(1)
#define DRA7_EFUSE_HIGH_MPU_OPP			BIT(2)

#define VERSION_COUNT				2

struct ti_cpufreq_data;

struct ti_cpufreq_soc_data {
	unsigned long (*efuse_xlate)(struct ti_cpufreq_data *opp_data,
				     unsigned long efuse);
	unsigned long efuse_fallback;
	unsigned long efuse_offset;
	unsigned long efuse_mask;
	unsigned long efuse_shift;
	unsigned long rev_offset;
	bool multi_regulator;
};

struct ti_cpufreq_data {
	struct device *cpu_dev;
	struct device_node *opp_node;
	struct regmap *syscon;
	const struct ti_cpufreq_soc_data *soc_data;
	struct opp_table *opp_table;
};

static unsigned long amx3_efuse_xlate(struct ti_cpufreq_data *opp_data,
				      unsigned long efuse)
{
	if (!efuse)
		efuse = opp_data->soc_data->efuse_fallback;
	/* AM335x and AM437x use "OPP disable" bits, so invert */
	return ~efuse;
}

static unsigned long dra7_efuse_xlate(struct ti_cpufreq_data *opp_data,
				      unsigned long efuse)
{
	unsigned long calculated_efuse = DRA7_EFUSE_NOM_MPU_OPP;

	/*
	 * The efuse on dra7 and am57 parts contains a specific
	 * value indicating the highest available OPP.
	 */

	switch (efuse) {
	case DRA7_EFUSE_HAS_ALL_MPU_OPP:
	case DRA7_EFUSE_HAS_HIGH_MPU_OPP:
		calculated_efuse |= DRA7_EFUSE_HIGH_MPU_OPP;
	case DRA7_EFUSE_HAS_OD_MPU_OPP:
		calculated_efuse |= DRA7_EFUSE_OD_MPU_OPP;
	}

	return calculated_efuse;
}

static struct ti_cpufreq_soc_data am3x_soc_data = {
	.efuse_xlate = amx3_efuse_xlate,
	.efuse_fallback = AM33XX_800M_ARM_MPU_MAX_FREQ,
	.efuse_offset = 0x07fc,
	.efuse_mask = 0x1fff,
	.rev_offset = 0x600,
	.multi_regulator = false,
};

static struct ti_cpufreq_soc_data am4x_soc_data = {
	.efuse_xlate = amx3_efuse_xlate,
	.efuse_fallback = AM43XX_600M_ARM_MPU_MAX_FREQ,
	.efuse_offset = 0x0610,
	.efuse_mask = 0x3f,
	.rev_offset = 0x600,
	.multi_regulator = false,
};

static struct ti_cpufreq_soc_data dra7_soc_data = {
	.efuse_xlate = dra7_efuse_xlate,
	.efuse_offset = 0x020c,
	.efuse_mask = 0xf80000,
	.efuse_shift = 19,
	.rev_offset = 0x204,
	.multi_regulator = true,
};

/**
 * ti_cpufreq_get_efuse() - Parse and return efuse value present on SoC
 * @opp_data: pointer to ti_cpufreq_data context
 * @efuse_value: Set to the value parsed from efuse
 *
 * Returns error code if efuse not read properly.
 */
static int ti_cpufreq_get_efuse(struct ti_cpufreq_data *opp_data,
				u32 *efuse_value)
{
	struct device *dev = opp_data->cpu_dev;
	u32 efuse;
	int ret;

	ret = regmap_read(opp_data->syscon, opp_data->soc_data->efuse_offset,
			  &efuse);
	if (ret) {
		dev_err(dev,
			"Failed to read the efuse value from syscon: %d\n",
			ret);
		return ret;
	}

	efuse = (efuse & opp_data->soc_data->efuse_mask);
	efuse >>= opp_data->soc_data->efuse_shift;

	*efuse_value = opp_data->soc_data->efuse_xlate(opp_data, efuse);

	return 0;
}

/**
 * ti_cpufreq_get_rev() - Parse and return rev value present on SoC
 * @opp_data: pointer to ti_cpufreq_data context
 * @revision_value: Set to the value parsed from revision register
 *
 * Returns error code if revision not read properly.
 */
static int ti_cpufreq_get_rev(struct ti_cpufreq_data *opp_data,
			      u32 *revision_value)
{
	struct device *dev = opp_data->cpu_dev;
	u32 revision;
	int ret;

	ret = regmap_read(opp_data->syscon, opp_data->soc_data->rev_offset,
			  &revision);
	if (ret) {
		dev_err(dev,
			"Failed to read the revision number from syscon: %d\n",
			ret);
		return ret;
	}

	*revision_value = BIT((revision >> REVISION_SHIFT) & REVISION_MASK);

	return 0;
}

static int ti_cpufreq_setup_syscon_register(struct ti_cpufreq_data *opp_data)
{
	struct device *dev = opp_data->cpu_dev;
	struct device_node *np = opp_data->opp_node;

	opp_data->syscon = syscon_regmap_lookup_by_phandle(np,
							"syscon");
	if (IS_ERR(opp_data->syscon)) {
		dev_err(dev,
			"\"syscon\" is missing, cannot use OPPv2 table.\n");
		return PTR_ERR(opp_data->syscon);
	}

	return 0;
}

static const struct of_device_id ti_cpufreq_of_match[] = {
	{ .compatible = "ti,am33xx", .data = &am3x_soc_data, },
	{ .compatible = "ti,am43", .data = &am4x_soc_data, },
	{ .compatible = "ti,dra7", .data = &dra7_soc_data },
	{},
};

static const struct of_device_id *ti_cpufreq_match_node(void)
{
	struct device_node *np;
	const struct of_device_id *match;

	np = of_find_node_by_path("/");
	match = of_match_node(ti_cpufreq_of_match, np);
	of_node_put(np);

	return match;
}

static int ti_cpufreq_probe(struct platform_device *pdev)
{
	u32 version[VERSION_COUNT];
	const struct of_device_id *match;
	struct opp_table *ti_opp_table;
	struct ti_cpufreq_data *opp_data;
	const char * const reg_names[] = {"vdd", "vbb"};
	int ret;

	match = dev_get_platdata(&pdev->dev);
	if (!match)
		return -ENODEV;

	opp_data = devm_kzalloc(&pdev->dev, sizeof(*opp_data), GFP_KERNEL);
	if (!opp_data)
		return -ENOMEM;

	opp_data->soc_data = match->data;

	opp_data->cpu_dev = get_cpu_device(0);
	if (!opp_data->cpu_dev) {
		pr_err("%s: Failed to get device for CPU0\n", __func__);
		return -ENODEV;
	}

	opp_data->opp_node = dev_pm_opp_of_get_opp_desc_node(opp_data->cpu_dev);
	if (!opp_data->opp_node) {
		dev_info(opp_data->cpu_dev,
			 "OPP-v2 not supported, cpufreq-dt will attempt to use legacy tables.\n");
		goto register_cpufreq_dt;
	}

	ret = ti_cpufreq_setup_syscon_register(opp_data);
	if (ret)
		goto fail_put_node;

	/*
	 * OPPs determine whether or not they are supported based on
	 * two metrics:
	 *	0 - SoC Revision
	 *	1 - eFuse value
	 */
	ret = ti_cpufreq_get_rev(opp_data, &version[0]);
	if (ret)
		goto fail_put_node;

	ret = ti_cpufreq_get_efuse(opp_data, &version[1]);
	if (ret)
		goto fail_put_node;

	ti_opp_table = dev_pm_opp_set_supported_hw(opp_data->cpu_dev,
						   version, VERSION_COUNT);
	if (IS_ERR(ti_opp_table)) {
		dev_err(opp_data->cpu_dev,
			"Failed to set supported hardware\n");
		ret = PTR_ERR(ti_opp_table);
		goto fail_put_node;
	}

	opp_data->opp_table = ti_opp_table;

	if (opp_data->soc_data->multi_regulator) {
		ti_opp_table = dev_pm_opp_set_regulators(opp_data->cpu_dev,
							 reg_names,
							 ARRAY_SIZE(reg_names));
		if (IS_ERR(ti_opp_table)) {
			dev_pm_opp_put_supported_hw(opp_data->opp_table);
			ret =  PTR_ERR(ti_opp_table);
			goto fail_put_node;
		}
	}

	of_node_put(opp_data->opp_node);
register_cpufreq_dt:
	platform_device_register_simple("cpufreq-dt", -1, NULL, 0);

	return 0;

fail_put_node:
	of_node_put(opp_data->opp_node);

	return ret;
}

static int ti_cpufreq_init(void)
{
	const struct of_device_id *match;

	/* Check to ensure we are on a compatible platform */
	match = ti_cpufreq_match_node();
	if (match)
		platform_device_register_data(NULL, "ti-cpufreq", -1, match,
					      sizeof(*match));

	return 0;
}
module_init(ti_cpufreq_init);

static struct platform_driver ti_cpufreq_driver = {
	.probe = ti_cpufreq_probe,
	.driver = {
		.name = "ti-cpufreq",
	},
};
builtin_platform_driver(ti_cpufreq_driver);

MODULE_DESCRIPTION("TI CPUFreq/OPP hw-supported driver");
MODULE_AUTHOR("Dave Gerlach <d-gerlach@ti.com>");
MODULE_LICENSE("GPL v2");
