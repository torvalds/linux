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
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define REVISION_MASK				0xF
#define REVISION_SHIFT				28

#define AM33XX_800M_ARM_MPU_MAX_FREQ		0x1E2F
#define AM43XX_600M_ARM_MPU_MAX_FREQ		0xFFA

#define DRA7_EFUSE_HAS_OD_MPU_OPP		11
#define DRA7_EFUSE_HAS_HIGH_MPU_OPP		15
#define DRA76_EFUSE_HAS_PLUS_MPU_OPP		18
#define DRA7_EFUSE_HAS_ALL_MPU_OPP		23
#define DRA76_EFUSE_HAS_ALL_MPU_OPP		24

#define DRA7_EFUSE_NOM_MPU_OPP			BIT(0)
#define DRA7_EFUSE_OD_MPU_OPP			BIT(1)
#define DRA7_EFUSE_HIGH_MPU_OPP			BIT(2)
#define DRA76_EFUSE_PLUS_MPU_OPP		BIT(3)

#define OMAP3_CONTROL_DEVICE_STATUS		0x4800244C
#define OMAP3_CONTROL_IDCODE			0x4830A204
#define OMAP34xx_ProdID_SKUID			0x4830A20C
#define OMAP3_SYSCON_BASE	(0x48000000 + 0x2000 + 0x270)

#define AM625_EFUSE_K_MPU_OPP			11
#define AM625_EFUSE_S_MPU_OPP			19
#define AM625_EFUSE_T_MPU_OPP			20

#define AM625_SUPPORT_K_MPU_OPP			BIT(0)
#define AM625_SUPPORT_S_MPU_OPP			BIT(1)
#define AM625_SUPPORT_T_MPU_OPP			BIT(2)

enum {
	AM62A7_EFUSE_M_MPU_OPP =		13,
	AM62A7_EFUSE_N_MPU_OPP,
	AM62A7_EFUSE_O_MPU_OPP,
	AM62A7_EFUSE_P_MPU_OPP,
	AM62A7_EFUSE_Q_MPU_OPP,
	AM62A7_EFUSE_R_MPU_OPP,
	AM62A7_EFUSE_S_MPU_OPP,
	/*
	 * The V, U, and T speed grade numbering is out of order
	 * to align with the AM625 more uniformly. I promise I know
	 * my ABCs ;)
	 */
	AM62A7_EFUSE_V_MPU_OPP,
	AM62A7_EFUSE_U_MPU_OPP,
	AM62A7_EFUSE_T_MPU_OPP,
};

#define AM62A7_SUPPORT_N_MPU_OPP		BIT(0)
#define AM62A7_SUPPORT_R_MPU_OPP		BIT(1)
#define AM62A7_SUPPORT_V_MPU_OPP		BIT(2)

#define AM62P5_EFUSE_O_MPU_OPP			15
#define AM62P5_EFUSE_S_MPU_OPP			19
#define AM62P5_EFUSE_U_MPU_OPP			21

#define AM62P5_SUPPORT_O_MPU_OPP		BIT(0)
#define AM62P5_SUPPORT_U_MPU_OPP		BIT(2)

#define VERSION_COUNT				2

struct ti_cpufreq_data;

struct ti_cpufreq_soc_data {
	const char * const *reg_names;
	unsigned long (*efuse_xlate)(struct ti_cpufreq_data *opp_data,
				     unsigned long efuse);
	unsigned long efuse_fallback;
	unsigned long efuse_offset;
	unsigned long efuse_mask;
	unsigned long efuse_shift;
	unsigned long rev_offset;
	bool multi_regulator;
/* Backward compatibility hack: Might have missing syscon */
#define TI_QUIRK_SYSCON_MAY_BE_MISSING	0x1
	u8 quirks;
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
	case DRA76_EFUSE_HAS_PLUS_MPU_OPP:
	case DRA76_EFUSE_HAS_ALL_MPU_OPP:
		calculated_efuse |= DRA76_EFUSE_PLUS_MPU_OPP;
		fallthrough;
	case DRA7_EFUSE_HAS_ALL_MPU_OPP:
	case DRA7_EFUSE_HAS_HIGH_MPU_OPP:
		calculated_efuse |= DRA7_EFUSE_HIGH_MPU_OPP;
		fallthrough;
	case DRA7_EFUSE_HAS_OD_MPU_OPP:
		calculated_efuse |= DRA7_EFUSE_OD_MPU_OPP;
	}

	return calculated_efuse;
}

static unsigned long omap3_efuse_xlate(struct ti_cpufreq_data *opp_data,
				      unsigned long efuse)
{
	/* OPP enable bit ("Speed Binned") */
	return BIT(efuse);
}

static unsigned long am62p5_efuse_xlate(struct ti_cpufreq_data *opp_data,
					unsigned long efuse)
{
	unsigned long calculated_efuse = AM62P5_SUPPORT_O_MPU_OPP;

	switch (efuse) {
	case AM62P5_EFUSE_U_MPU_OPP:
	case AM62P5_EFUSE_S_MPU_OPP:
		calculated_efuse |= AM62P5_SUPPORT_U_MPU_OPP;
		fallthrough;
	case AM62P5_EFUSE_O_MPU_OPP:
		calculated_efuse |= AM62P5_SUPPORT_O_MPU_OPP;
	}

	return calculated_efuse;
}

static unsigned long am62a7_efuse_xlate(struct ti_cpufreq_data *opp_data,
					unsigned long efuse)
{
	unsigned long calculated_efuse = AM62A7_SUPPORT_N_MPU_OPP;

	switch (efuse) {
	case AM62A7_EFUSE_V_MPU_OPP:
	case AM62A7_EFUSE_U_MPU_OPP:
	case AM62A7_EFUSE_T_MPU_OPP:
	case AM62A7_EFUSE_S_MPU_OPP:
		calculated_efuse |= AM62A7_SUPPORT_V_MPU_OPP;
		fallthrough;
	case AM62A7_EFUSE_R_MPU_OPP:
	case AM62A7_EFUSE_Q_MPU_OPP:
	case AM62A7_EFUSE_P_MPU_OPP:
	case AM62A7_EFUSE_O_MPU_OPP:
		calculated_efuse |= AM62A7_SUPPORT_R_MPU_OPP;
		fallthrough;
	case AM62A7_EFUSE_N_MPU_OPP:
	case AM62A7_EFUSE_M_MPU_OPP:
		calculated_efuse |= AM62A7_SUPPORT_N_MPU_OPP;
	}

	return calculated_efuse;
}

static unsigned long am625_efuse_xlate(struct ti_cpufreq_data *opp_data,
				       unsigned long efuse)
{
	unsigned long calculated_efuse = AM625_SUPPORT_K_MPU_OPP;

	switch (efuse) {
	case AM625_EFUSE_T_MPU_OPP:
		calculated_efuse |= AM625_SUPPORT_T_MPU_OPP;
		fallthrough;
	case AM625_EFUSE_S_MPU_OPP:
		calculated_efuse |= AM625_SUPPORT_S_MPU_OPP;
		fallthrough;
	case AM625_EFUSE_K_MPU_OPP:
		calculated_efuse |= AM625_SUPPORT_K_MPU_OPP;
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

/*
 * OMAP35x TRM (SPRUF98K):
 *  CONTROL_IDCODE (0x4830 A204) describes Silicon revisions.
 *  Control OMAP Status Register 15:0 (Address 0x4800 244C)
 *    to separate between omap3503, omap3515, omap3525, omap3530
 *    and feature presence.
 *    There are encodings for versions limited to 400/266MHz
 *    but we ignore.
 *    Not clear if this also holds for omap34xx.
 *  some eFuse values e.g. CONTROL_FUSE_OPP1_VDD1
 *    are stored in the SYSCON register range
 *  Register 0x4830A20C [ProdID.SKUID] [0:3]
 *    0x0 for normal 600/430MHz device.
 *    0x8 for 720/520MHz device.
 *    Not clear what omap34xx value is.
 */

static struct ti_cpufreq_soc_data omap34xx_soc_data = {
	.efuse_xlate = omap3_efuse_xlate,
	.efuse_offset = OMAP34xx_ProdID_SKUID - OMAP3_SYSCON_BASE,
	.efuse_shift = 3,
	.efuse_mask = BIT(3),
	.rev_offset = OMAP3_CONTROL_IDCODE - OMAP3_SYSCON_BASE,
	.multi_regulator = false,
	.quirks = TI_QUIRK_SYSCON_MAY_BE_MISSING,
};

/*
 * AM/DM37x TRM (SPRUGN4M)
 *  CONTROL_IDCODE (0x4830 A204) describes Silicon revisions.
 *  Control Device Status Register 15:0 (Address 0x4800 244C)
 *    to separate between am3703, am3715, dm3725, dm3730
 *    and feature presence.
 *   Speed Binned = Bit 9
 *     0 800/600 MHz
 *     1 1000/800 MHz
 *  some eFuse values e.g. CONTROL_FUSE_OPP 1G_VDD1
 *    are stored in the SYSCON register range.
 *  There is no 0x4830A20C [ProdID.SKUID] register (exists but
 *    seems to always read as 0).
 */

static const char * const omap3_reg_names[] = {"cpu0", "vbb", NULL};

static struct ti_cpufreq_soc_data omap36xx_soc_data = {
	.reg_names = omap3_reg_names,
	.efuse_xlate = omap3_efuse_xlate,
	.efuse_offset = OMAP3_CONTROL_DEVICE_STATUS - OMAP3_SYSCON_BASE,
	.efuse_shift = 9,
	.efuse_mask = BIT(9),
	.rev_offset = OMAP3_CONTROL_IDCODE - OMAP3_SYSCON_BASE,
	.multi_regulator = true,
	.quirks = TI_QUIRK_SYSCON_MAY_BE_MISSING,
};

/*
 * AM3517 is quite similar to AM/DM37x except that it has no
 * high speed grade eFuse and no abb ldo
 */

static struct ti_cpufreq_soc_data am3517_soc_data = {
	.efuse_xlate = omap3_efuse_xlate,
	.efuse_offset = OMAP3_CONTROL_DEVICE_STATUS - OMAP3_SYSCON_BASE,
	.efuse_shift = 0,
	.efuse_mask = 0,
	.rev_offset = OMAP3_CONTROL_IDCODE - OMAP3_SYSCON_BASE,
	.multi_regulator = false,
	.quirks = TI_QUIRK_SYSCON_MAY_BE_MISSING,
};

static struct ti_cpufreq_soc_data am625_soc_data = {
	.efuse_xlate = am625_efuse_xlate,
	.efuse_offset = 0x0018,
	.efuse_mask = 0x07c0,
	.efuse_shift = 0x6,
	.rev_offset = 0x0014,
	.multi_regulator = false,
};

static struct ti_cpufreq_soc_data am62a7_soc_data = {
	.efuse_xlate = am62a7_efuse_xlate,
	.efuse_offset = 0x0,
	.efuse_mask = 0x07c0,
	.efuse_shift = 0x6,
	.rev_offset = 0x0014,
	.multi_regulator = false,
};

static struct ti_cpufreq_soc_data am62p5_soc_data = {
	.efuse_xlate = am62p5_efuse_xlate,
	.efuse_offset = 0x0,
	.efuse_mask = 0x07c0,
	.efuse_shift = 0x6,
	.rev_offset = 0x0014,
	.multi_regulator = false,
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
	if (opp_data->soc_data->quirks & TI_QUIRK_SYSCON_MAY_BE_MISSING && ret == -EIO) {
		/* not a syscon register! */
		void __iomem *regs = ioremap(OMAP3_SYSCON_BASE +
				opp_data->soc_data->efuse_offset, 4);

		if (!regs)
			return -ENOMEM;
		efuse = readl(regs);
		iounmap(regs);
		}
	else if (ret) {
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
	if (opp_data->soc_data->quirks & TI_QUIRK_SYSCON_MAY_BE_MISSING && ret == -EIO) {
		/* not a syscon register! */
		void __iomem *regs = ioremap(OMAP3_SYSCON_BASE +
				opp_data->soc_data->rev_offset, 4);

		if (!regs)
			return -ENOMEM;
		revision = readl(regs);
		iounmap(regs);
		}
	else if (ret) {
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
	{ .compatible = "ti,am3517", .data = &am3517_soc_data, },
	{ .compatible = "ti,am43", .data = &am4x_soc_data, },
	{ .compatible = "ti,dra7", .data = &dra7_soc_data },
	{ .compatible = "ti,omap34xx", .data = &omap34xx_soc_data, },
	{ .compatible = "ti,omap36xx", .data = &omap36xx_soc_data, },
	{ .compatible = "ti,am625", .data = &am625_soc_data, },
	{ .compatible = "ti,am62a7", .data = &am62a7_soc_data, },
	{ .compatible = "ti,am62p5", .data = &am62p5_soc_data, },
	/* legacy */
	{ .compatible = "ti,omap3430", .data = &omap34xx_soc_data, },
	{ .compatible = "ti,omap3630", .data = &omap36xx_soc_data, },
	{},
};

static const struct of_device_id *ti_cpufreq_match_node(void)
{
	struct device_node *np __free(device_node) = of_find_node_by_path("/");
	const struct of_device_id *match;

	match = of_match_node(ti_cpufreq_of_match, np);

	return match;
}

static int ti_cpufreq_probe(struct platform_device *pdev)
{
	u32 version[VERSION_COUNT];
	const struct of_device_id *match;
	struct ti_cpufreq_data *opp_data;
	const char * const default_reg_names[] = {"vdd", "vbb", NULL};
	int ret;
	struct dev_pm_opp_config config = {
		.supported_hw = version,
		.supported_hw_count = ARRAY_SIZE(version),
	};

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

	if (opp_data->soc_data->multi_regulator) {
		if (opp_data->soc_data->reg_names)
			config.regulator_names = opp_data->soc_data->reg_names;
		else
			config.regulator_names = default_reg_names;
	}

	ret = dev_pm_opp_set_config(opp_data->cpu_dev, &config);
	if (ret < 0) {
		dev_err_probe(opp_data->cpu_dev, ret, "Failed to set OPP config\n");
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

static int __init ti_cpufreq_init(void)
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
