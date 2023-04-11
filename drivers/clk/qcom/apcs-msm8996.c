// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm APCS clock controller driver
 *
 * Copyright (c) 2022, Linaro Limited
 * Author: Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define APCS_AUX_OFFSET	0x50

#define APCS_AUX_DIV_MASK GENMASK(17, 16)
#define APCS_AUX_DIV_2 0x1

static int qcom_apcs_msm8996_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *parent = dev->parent;
	struct regmap *regmap;
	struct clk_hw *hw;
	unsigned int val;
	int ret = -ENODEV;

	regmap = dev_get_regmap(parent, NULL);
	if (!regmap) {
		dev_err(dev, "failed to get regmap: %d\n", ret);
		return ret;
	}

	regmap_read(regmap, APCS_AUX_OFFSET, &val);
	regmap_update_bits(regmap, APCS_AUX_OFFSET, APCS_AUX_DIV_MASK,
			   FIELD_PREP(APCS_AUX_DIV_MASK, APCS_AUX_DIV_2));

	/*
	 * This clock is used during CPU cluster setup while setting up CPU PLLs.
	 * Add hardware mandated delay to make sure that the sys_apcs_aux clock
	 * is stable (after setting the divider) before continuing
	 * bootstrapping to keep CPUs from ending up in a weird state.
	 */
	udelay(5);

	/*
	 * As this clocks is a parent of the CPU cluster clocks and is actually
	 * used as a parent during CPU clocks setup, we want for it to register
	 * as early as possible, without letting fw_devlink to delay probing of
	 * either of the drivers.
	 *
	 * The sys_apcs_aux is a child (divider) of gpll0, but we register it
	 * as a fixed rate clock instead to ease bootstrapping procedure. By
	 * doing this we make sure that CPU cluster clocks are able to be setup
	 * early during the boot process (as it is recommended by Qualcomm).
	 */
	hw = devm_clk_hw_register_fixed_rate(dev, "sys_apcs_aux", NULL, 0, 300000000);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
}

static struct platform_driver qcom_apcs_msm8996_clk_driver = {
	.probe = qcom_apcs_msm8996_clk_probe,
	.driver = {
		.name = "qcom-apcs-msm8996-clk",
	},
};

/* Register early enough to fix the clock to be used for other cores */
static int __init qcom_apcs_msm8996_clk_init(void)
{
	return platform_driver_register(&qcom_apcs_msm8996_clk_driver);
}
postcore_initcall(qcom_apcs_msm8996_clk_init);

static void __exit qcom_apcs_msm8996_clk_exit(void)
{
	platform_driver_unregister(&qcom_apcs_msm8996_clk_driver);
}
module_exit(qcom_apcs_msm8996_clk_exit);

MODULE_AUTHOR("Dmitry Baryshkov <dmitry.baryshkov@linaro.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm MSM8996 APCS clock driver");
