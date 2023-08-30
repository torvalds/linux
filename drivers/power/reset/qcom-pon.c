// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-18 Linaro Limited

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/reboot-mode.h>
#include <linux/regmap.h>

#define PON_SOFT_RB_SPARE		0x8f

#define GEN1_REASON_SHIFT		2
#define GEN2_REASON_SHIFT		1

struct pm8916_pon {
	struct device *dev;
	struct regmap *regmap;
	u32 baseaddr;
	struct reboot_mode_driver reboot_mode;
	long reason_shift;
};

static int pm8916_reboot_mode_write(struct reboot_mode_driver *reboot,
				    unsigned int magic)
{
	struct pm8916_pon *pon = container_of
			(reboot, struct pm8916_pon, reboot_mode);
	int ret;

	ret = regmap_update_bits(pon->regmap,
				 pon->baseaddr + PON_SOFT_RB_SPARE,
				 GENMASK(7, pon->reason_shift),
				 magic << pon->reason_shift);
	if (ret < 0)
		dev_err(pon->dev, "update reboot mode bits failed\n");

	return ret;
}

static int pm8916_pon_probe(struct platform_device *pdev)
{
	struct pm8916_pon *pon;
	int error;

	pon = devm_kzalloc(&pdev->dev, sizeof(*pon), GFP_KERNEL);
	if (!pon)
		return -ENOMEM;

	pon->dev = &pdev->dev;

	pon->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pon->regmap) {
		dev_err(&pdev->dev, "failed to locate regmap\n");
		return -ENODEV;
	}

	error = of_property_read_u32(pdev->dev.of_node, "reg",
				     &pon->baseaddr);
	if (error)
		return error;

	pon->reboot_mode.dev = &pdev->dev;
	pon->reason_shift = (long)of_device_get_match_data(&pdev->dev);
	pon->reboot_mode.write = pm8916_reboot_mode_write;
	error = devm_reboot_mode_register(&pdev->dev, &pon->reboot_mode);
	if (error) {
		dev_err(&pdev->dev, "can't register reboot mode\n");
		return error;
	}

	platform_set_drvdata(pdev, pon);

	return devm_of_platform_populate(&pdev->dev);
}

static const struct of_device_id pm8916_pon_id_table[] = {
	{ .compatible = "qcom,pm8916-pon", .data = (void *)GEN1_REASON_SHIFT },
	{ .compatible = "qcom,pms405-pon", .data = (void *)GEN1_REASON_SHIFT },
	{ .compatible = "qcom,pm8998-pon", .data = (void *)GEN2_REASON_SHIFT },
	{ .compatible = "qcom,pmk8350-pon", .data = (void *)GEN2_REASON_SHIFT },
	{ }
};
MODULE_DEVICE_TABLE(of, pm8916_pon_id_table);

static struct platform_driver pm8916_pon_driver = {
	.probe = pm8916_pon_probe,
	.driver = {
		.name = "pm8916-pon",
		.of_match_table = pm8916_pon_id_table,
	},
};
module_platform_driver(pm8916_pon_driver);

MODULE_DESCRIPTION("pm8916 Power On driver");
MODULE_LICENSE("GPL v2");
