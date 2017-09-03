/*
 * TI CPUFreq/OPP hw-supported driver
 *
 * Copyright (C) 2016-2017 Texas Instruments, Inc.
 *	 Dave Gerlach <d-gerlach@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/cpu.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
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
};

struct ti_cpufreq_data {
	struct device *cpu_dev;
	struct device_node *opp_node;
	struct regmap *syscon;
	const struct ti_cpufreq_soc_data *soc_data;
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
};

static struct ti_cpufreq_soc_data am4x_soc_data = {
	.efuse_xlate = amx3_efuse_xlate,
	.efuse_fallback = AM43XX_600M_ARM_MPU_MAX_FREQ,
	.efuse_offset = 0x0610,
	.efuse_mask = 0x3f,
	.rev_offset = 0x600,
};

static struct ti_cpufreq_soc_data dra7_soc_data = {
	.efuse_xlate = dra7_efuse_xlate,
	.efuse_offset = 0x020c,
	.efuse_mask = 0xf80000,
	.efuse_shift = 19,
	.rev_offset = 0x204,
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
	{ .compatible = "ti,am4372", .data = &am4x_soc_data, },
	{ .compatible = "ti,dra7", .data = &dra7_soc_data },
	{},
};

static int ti_cpufreq_init(void)
{
	u32 version[VERSION_COUNT];
	struct device_node *np;
	const struct of_device_id *match;
	struct ti_cpufreq_data *opp_data;
	int ret;

	np = of_find_node_by_path("/");
	match = of_match_node(ti_cpufreq_of_match, np);
	if (!match)
		return -ENODEV;

	opp_data = kzalloc(sizeof(*opp_data), GFP_KERNEL);
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

	ret = PTR_ERR_OR_ZERO(dev_pm_opp_set_supported_hw(opp_data->cpu_dev,
							  version, VERSION_COUNT));
	if (ret) {
		dev_err(opp_data->cpu_dev,
			"Failed to set supported hardware\n");
		goto fail_put_node;
	}

	of_node_put(opp_data->opp_node);

register_cpufreq_dt:
	platform_device_register_simple("cpufreq-dt", -1, NULL, 0);

	return 0;

fail_put_node:
	of_node_put(opp_data->opp_node);

	return ret;
}
device_initcall(ti_cpufreq_init);
