// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP
 */

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include "cpufreq-dt.h"

#define OCOTP_CFG3_SPEED_GRADE_SHIFT	8
#define OCOTP_CFG3_SPEED_GRADE_MASK	(0x3 << 8)
#define IMX8MN_OCOTP_CFG3_SPEED_GRADE_MASK	(0xf << 8)
#define OCOTP_CFG3_MKT_SEGMENT_SHIFT    6
#define OCOTP_CFG3_MKT_SEGMENT_MASK     (0x3 << 6)
#define IMX8MP_OCOTP_CFG3_MKT_SEGMENT_SHIFT    5
#define IMX8MP_OCOTP_CFG3_MKT_SEGMENT_MASK     (0x3 << 5)

#define IMX7ULP_MAX_RUN_FREQ	528000

/* cpufreq-dt device registered by imx-cpufreq-dt */
static struct platform_device *cpufreq_dt_pdev;
static struct device *cpu_dev;
static int cpufreq_opp_token;

enum IMX7ULP_CPUFREQ_CLKS {
	ARM,
	CORE,
	SCS_SEL,
	HSRUN_CORE,
	HSRUN_SCS_SEL,
	FIRC,
};

static struct clk_bulk_data imx7ulp_clks[] = {
	{ .id = "arm" },
	{ .id = "core" },
	{ .id = "scs_sel" },
	{ .id = "hsrun_core" },
	{ .id = "hsrun_scs_sel" },
	{ .id = "firc" },
};

static unsigned int imx7ulp_get_intermediate(struct cpufreq_policy *policy,
					     unsigned int index)
{
	return clk_get_rate(imx7ulp_clks[FIRC].clk);
}

static int imx7ulp_target_intermediate(struct cpufreq_policy *policy,
					unsigned int index)
{
	unsigned int newfreq = policy->freq_table[index].frequency;

	clk_set_parent(imx7ulp_clks[SCS_SEL].clk, imx7ulp_clks[FIRC].clk);
	clk_set_parent(imx7ulp_clks[HSRUN_SCS_SEL].clk, imx7ulp_clks[FIRC].clk);

	if (newfreq > IMX7ULP_MAX_RUN_FREQ)
		clk_set_parent(imx7ulp_clks[ARM].clk,
			       imx7ulp_clks[HSRUN_CORE].clk);
	else
		clk_set_parent(imx7ulp_clks[ARM].clk, imx7ulp_clks[CORE].clk);

	return 0;
}

static struct cpufreq_dt_platform_data imx7ulp_data = {
	.target_intermediate = imx7ulp_target_intermediate,
	.get_intermediate = imx7ulp_get_intermediate,
};

static int imx_cpufreq_dt_probe(struct platform_device *pdev)
{
	struct platform_device *dt_pdev;
	u32 cell_value, supported_hw[2];
	int speed_grade, mkt_segment;
	int ret;

	cpu_dev = get_cpu_device(0);

	if (!of_property_present(cpu_dev->of_node, "cpu-supply"))
		return -ENODEV;

	if (of_machine_is_compatible("fsl,imx7ulp")) {
		ret = clk_bulk_get(cpu_dev, ARRAY_SIZE(imx7ulp_clks),
				   imx7ulp_clks);
		if (ret)
			return ret;

		dt_pdev = platform_device_register_data(NULL, "cpufreq-dt",
							-1, &imx7ulp_data,
							sizeof(imx7ulp_data));
		if (IS_ERR(dt_pdev)) {
			clk_bulk_put(ARRAY_SIZE(imx7ulp_clks), imx7ulp_clks);
			ret = PTR_ERR(dt_pdev);
			dev_err(&pdev->dev, "Failed to register cpufreq-dt: %d\n", ret);
			return ret;
		}

		cpufreq_dt_pdev = dt_pdev;

		return 0;
	}

	ret = nvmem_cell_read_u32(cpu_dev, "speed_grade", &cell_value);
	if (ret)
		return ret;

	if (of_machine_is_compatible("fsl,imx8mn") ||
	    of_machine_is_compatible("fsl,imx8mp"))
		speed_grade = (cell_value & IMX8MN_OCOTP_CFG3_SPEED_GRADE_MASK)
			      >> OCOTP_CFG3_SPEED_GRADE_SHIFT;
	else
		speed_grade = (cell_value & OCOTP_CFG3_SPEED_GRADE_MASK)
			      >> OCOTP_CFG3_SPEED_GRADE_SHIFT;

	if (of_machine_is_compatible("fsl,imx8mp"))
		mkt_segment = (cell_value & IMX8MP_OCOTP_CFG3_MKT_SEGMENT_MASK)
			       >> IMX8MP_OCOTP_CFG3_MKT_SEGMENT_SHIFT;
	else
		mkt_segment = (cell_value & OCOTP_CFG3_MKT_SEGMENT_MASK)
			       >> OCOTP_CFG3_MKT_SEGMENT_SHIFT;

	/*
	 * Early samples without fuses written report "0 0" which may NOT
	 * match any OPP defined in DT. So clamp to minimum OPP defined in
	 * DT to avoid warning for "no OPPs".
	 *
	 * Applies to i.MX8M series SoCs.
	 */
	if (mkt_segment == 0 && speed_grade == 0) {
		if (of_machine_is_compatible("fsl,imx8mm") ||
		    of_machine_is_compatible("fsl,imx8mq"))
			speed_grade = 1;
		if (of_machine_is_compatible("fsl,imx8mn") ||
		    of_machine_is_compatible("fsl,imx8mp"))
			speed_grade = 0xb;
	}

	supported_hw[0] = BIT(speed_grade);
	supported_hw[1] = BIT(mkt_segment);
	dev_info(&pdev->dev, "cpu speed grade %d mkt segment %d supported-hw %#x %#x\n",
			speed_grade, mkt_segment, supported_hw[0], supported_hw[1]);

	cpufreq_opp_token = dev_pm_opp_set_supported_hw(cpu_dev, supported_hw, 2);
	if (cpufreq_opp_token < 0) {
		ret = cpufreq_opp_token;
		dev_err(&pdev->dev, "Failed to set supported opp: %d\n", ret);
		return ret;
	}

	cpufreq_dt_pdev = platform_device_register_data(
			&pdev->dev, "cpufreq-dt", -1, NULL, 0);
	if (IS_ERR(cpufreq_dt_pdev)) {
		dev_pm_opp_put_supported_hw(cpufreq_opp_token);
		ret = PTR_ERR(cpufreq_dt_pdev);
		dev_err(&pdev->dev, "Failed to register cpufreq-dt: %d\n", ret);
		return ret;
	}

	return 0;
}

static void imx_cpufreq_dt_remove(struct platform_device *pdev)
{
	platform_device_unregister(cpufreq_dt_pdev);
	if (!of_machine_is_compatible("fsl,imx7ulp"))
		dev_pm_opp_put_supported_hw(cpufreq_opp_token);
	else
		clk_bulk_put(ARRAY_SIZE(imx7ulp_clks), imx7ulp_clks);
}

static struct platform_driver imx_cpufreq_dt_driver = {
	.probe = imx_cpufreq_dt_probe,
	.remove_new = imx_cpufreq_dt_remove,
	.driver = {
		.name = "imx-cpufreq-dt",
	},
};
module_platform_driver(imx_cpufreq_dt_driver);

MODULE_ALIAS("platform:imx-cpufreq-dt");
MODULE_DESCRIPTION("Freescale i.MX cpufreq speed grading driver");
MODULE_LICENSE("GPL v2");
