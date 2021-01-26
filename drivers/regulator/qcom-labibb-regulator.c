// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020, The Linux Foundation. All rights reserved.

#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define REG_PERPH_TYPE                  0x04

#define QCOM_LAB_TYPE			0x24
#define QCOM_IBB_TYPE			0x20

#define PMI8998_LAB_REG_BASE		0xde00
#define PMI8998_IBB_REG_BASE		0xdc00

#define REG_LABIBB_STATUS1		0x08
#define REG_LABIBB_ENABLE_CTL		0x46
#define LABIBB_STATUS1_VREG_OK_BIT	BIT(7)
#define LABIBB_CONTROL_ENABLE		BIT(7)

#define LAB_ENABLE_CTL_MASK		BIT(7)
#define IBB_ENABLE_CTL_MASK		(BIT(7) | BIT(6))

#define LABIBB_OFF_ON_DELAY		1000
#define LAB_ENABLE_TIME			(LABIBB_OFF_ON_DELAY * 2)
#define IBB_ENABLE_TIME			(LABIBB_OFF_ON_DELAY * 10)
#define LABIBB_POLL_ENABLED_TIME	1000

struct labibb_regulator {
	struct regulator_desc		desc;
	struct device			*dev;
	struct regmap			*regmap;
	struct regulator_dev		*rdev;
	u16				base;
	u8				type;
};

struct labibb_regulator_data {
	const char			*name;
	u8				type;
	u16				base;
	const struct regulator_desc	*desc;
};

static const struct regulator_ops qcom_labibb_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_desc pmi8998_lab_desc = {
	.enable_mask		= LAB_ENABLE_CTL_MASK,
	.enable_reg		= (PMI8998_LAB_REG_BASE + REG_LABIBB_ENABLE_CTL),
	.enable_val		= LABIBB_CONTROL_ENABLE,
	.enable_time		= LAB_ENABLE_TIME,
	.poll_enabled_time	= LABIBB_POLL_ENABLED_TIME,
	.off_on_delay		= LABIBB_OFF_ON_DELAY,
	.owner			= THIS_MODULE,
	.type			= REGULATOR_VOLTAGE,
	.ops			= &qcom_labibb_ops,
};

static const struct regulator_desc pmi8998_ibb_desc = {
	.enable_mask		= IBB_ENABLE_CTL_MASK,
	.enable_reg		= (PMI8998_IBB_REG_BASE + REG_LABIBB_ENABLE_CTL),
	.enable_val		= LABIBB_CONTROL_ENABLE,
	.enable_time		= IBB_ENABLE_TIME,
	.poll_enabled_time	= LABIBB_POLL_ENABLED_TIME,
	.off_on_delay		= LABIBB_OFF_ON_DELAY,
	.owner			= THIS_MODULE,
	.type			= REGULATOR_VOLTAGE,
	.ops			= &qcom_labibb_ops,
};

static const struct labibb_regulator_data pmi8998_labibb_data[] = {
	{"lab", QCOM_LAB_TYPE, PMI8998_LAB_REG_BASE, &pmi8998_lab_desc},
	{"ibb", QCOM_IBB_TYPE, PMI8998_IBB_REG_BASE, &pmi8998_ibb_desc},
	{ },
};

static const struct of_device_id qcom_labibb_match[] = {
	{ .compatible = "qcom,pmi8998-lab-ibb", .data = &pmi8998_labibb_data},
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_labibb_match);

static int qcom_labibb_regulator_probe(struct platform_device *pdev)
{
	struct labibb_regulator *vreg;
	struct device *dev = &pdev->dev;
	struct regulator_config cfg = {};

	const struct of_device_id *match;
	const struct labibb_regulator_data *reg_data;
	struct regmap *reg_regmap;
	unsigned int type;
	int ret;

	reg_regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!reg_regmap) {
		dev_err(&pdev->dev, "Couldn't get parent's regmap\n");
		return -ENODEV;
	}

	match = of_match_device(qcom_labibb_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	for (reg_data = match->data; reg_data->name; reg_data++) {

		/* Validate if the type of regulator is indeed
		 * what's mentioned in DT.
		 */
		ret = regmap_read(reg_regmap, reg_data->base + REG_PERPH_TYPE,
				  &type);
		if (ret < 0) {
			dev_err(dev,
				"Peripheral type read failed ret=%d\n",
				ret);
			return -EINVAL;
		}

		if (WARN_ON((type != QCOM_LAB_TYPE) && (type != QCOM_IBB_TYPE)) ||
		    WARN_ON(type != reg_data->type))
			return -EINVAL;

		vreg  = devm_kzalloc(&pdev->dev, sizeof(*vreg),
					   GFP_KERNEL);
		if (!vreg)
			return -ENOMEM;

		vreg->regmap = reg_regmap;
		vreg->dev = dev;
		vreg->base = reg_data->base;
		vreg->type = reg_data->type;

		memcpy(&vreg->desc, reg_data->desc, sizeof(vreg->desc));
		vreg->desc.of_match = reg_data->name;
		vreg->desc.name = reg_data->name;

		cfg.dev = vreg->dev;
		cfg.driver_data = vreg;
		cfg.regmap = vreg->regmap;

		vreg->rdev = devm_regulator_register(vreg->dev, &vreg->desc,
							&cfg);

		if (IS_ERR(vreg->rdev)) {
			dev_err(dev, "qcom_labibb: error registering %s : %d\n",
					reg_data->name, ret);
			return PTR_ERR(vreg->rdev);
		}
	}

	return 0;
}

static struct platform_driver qcom_labibb_regulator_driver = {
	.driver	= {
		.name = "qcom-lab-ibb-regulator",
		.of_match_table	= qcom_labibb_match,
	},
	.probe = qcom_labibb_regulator_probe,
};
module_platform_driver(qcom_labibb_regulator_driver);

MODULE_DESCRIPTION("Qualcomm labibb driver");
MODULE_AUTHOR("Nisha Kumari <nishakumari@codeaurora.org>");
MODULE_AUTHOR("Sumit Semwal <sumit.semwal@linaro.org>");
MODULE_LICENSE("GPL v2");
