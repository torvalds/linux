// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2021 MediaTek Inc.

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/mt6315-regulator.h>
#include <linux/regulator/of_regulator.h>
#include <linux/spmi.h>

#define MT6315_BUCK_MODE_AUTO		0
#define MT6315_BUCK_MODE_FORCE_PWM	1
#define MT6315_BUCK_MODE_LP		2

struct mt6315_regulator_info {
	struct regulator_desc desc;
	u32 status_reg;
	u32 lp_mode_mask;
	u32 lp_mode_shift;
};

struct mt_regulator_init_data {
	u32 modeset_mask[MT6315_VBUCK_MAX];
};

struct mt6315_chip {
	struct device *dev;
	struct regmap *regmap;
};

#define MT_BUCK(_name, _bid, _vsel)				\
[_bid] = {							\
	.desc = {						\
		.name = _name,					\
		.of_match = of_match_ptr(_name),		\
		.regulators_node = "regulators",		\
		.ops = &mt6315_volt_range_ops,			\
		.type = REGULATOR_VOLTAGE,			\
		.id = _bid,					\
		.owner = THIS_MODULE,				\
		.n_voltages = 0xc0,				\
		.linear_ranges = mt_volt_range1,		\
		.n_linear_ranges = ARRAY_SIZE(mt_volt_range1),	\
		.vsel_reg = _vsel,				\
		.vsel_mask = 0xff,				\
		.enable_reg = MT6315_BUCK_TOP_CON0,		\
		.enable_mask = BIT(_bid),			\
		.of_map_mode = mt6315_map_mode,			\
	},							\
	.status_reg = _bid##_DBG4,				\
	.lp_mode_mask = BIT(_bid),				\
	.lp_mode_shift = _bid,					\
}

static const struct linear_range mt_volt_range1[] = {
	REGULATOR_LINEAR_RANGE(0, 0, 0xbf, 6250),
};

static unsigned int mt6315_map_mode(unsigned int mode)
{
	switch (mode) {
	case MT6315_BUCK_MODE_AUTO:
		return REGULATOR_MODE_NORMAL;
	case MT6315_BUCK_MODE_FORCE_PWM:
		return REGULATOR_MODE_FAST;
	case MT6315_BUCK_MODE_LP:
		return REGULATOR_MODE_IDLE;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static unsigned int mt6315_regulator_get_mode(struct regulator_dev *rdev)
{
	struct mt_regulator_init_data *init = rdev_get_drvdata(rdev);
	const struct mt6315_regulator_info *info;
	int ret, regval;
	u32 modeset_mask;

	info = container_of(rdev->desc, struct mt6315_regulator_info, desc);
	modeset_mask = init->modeset_mask[rdev_get_id(rdev)];
	ret = regmap_read(rdev->regmap, MT6315_BUCK_TOP_4PHASE_ANA_CON42, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev, "Failed to get mode: %d\n", ret);
		return ret;
	}

	if ((regval & modeset_mask) == modeset_mask)
		return REGULATOR_MODE_FAST;

	ret = regmap_read(rdev->regmap, MT6315_BUCK_TOP_CON1, &regval);
	if (ret != 0) {
		dev_err(&rdev->dev, "Failed to get lp mode: %d\n", ret);
		return ret;
	}

	if (regval & info->lp_mode_mask)
		return REGULATOR_MODE_IDLE;
	else
		return REGULATOR_MODE_NORMAL;
}

static int mt6315_regulator_set_mode(struct regulator_dev *rdev,
				     u32 mode)
{
	struct mt_regulator_init_data *init = rdev_get_drvdata(rdev);
	const struct mt6315_regulator_info *info;
	int ret, val, curr_mode;
	u32 modeset_mask;

	info = container_of(rdev->desc, struct mt6315_regulator_info, desc);
	modeset_mask = init->modeset_mask[rdev_get_id(rdev)];
	curr_mode = mt6315_regulator_get_mode(rdev);
	switch (mode) {
	case REGULATOR_MODE_FAST:
		ret = regmap_update_bits(rdev->regmap,
					 MT6315_BUCK_TOP_4PHASE_ANA_CON42,
					 modeset_mask,
					 modeset_mask);
		break;
	case REGULATOR_MODE_NORMAL:
		if (curr_mode == REGULATOR_MODE_FAST) {
			ret = regmap_update_bits(rdev->regmap,
						 MT6315_BUCK_TOP_4PHASE_ANA_CON42,
						 modeset_mask,
						 0);
		} else if (curr_mode == REGULATOR_MODE_IDLE) {
			ret = regmap_update_bits(rdev->regmap,
						 MT6315_BUCK_TOP_CON1,
						 info->lp_mode_mask,
						 0);
			usleep_range(100, 110);
		} else {
			ret = -EINVAL;
		}
		break;
	case REGULATOR_MODE_IDLE:
		val = MT6315_BUCK_MODE_LP >> 1;
		val <<= info->lp_mode_shift;
		ret = regmap_update_bits(rdev->regmap,
					 MT6315_BUCK_TOP_CON1,
					 info->lp_mode_mask,
					 val);
		break;
	default:
		ret = -EINVAL;
		dev_err(&rdev->dev, "Unsupported mode: %d\n", mode);
		break;
	}

	if (ret != 0) {
		dev_err(&rdev->dev, "Failed to set mode: %d\n", ret);
		return ret;
	}

	return 0;
}

static int mt6315_get_status(struct regulator_dev *rdev)
{
	const struct mt6315_regulator_info *info;
	int ret;
	u32 regval;

	info = container_of(rdev->desc, struct mt6315_regulator_info, desc);
	ret = regmap_read(rdev->regmap, info->status_reg, &regval);
	if (ret < 0) {
		dev_err(&rdev->dev, "Failed to get enable reg: %d\n", ret);
		return ret;
	}

	return (regval & BIT(0)) ? REGULATOR_STATUS_ON : REGULATOR_STATUS_OFF;
}

static const struct regulator_ops mt6315_volt_range_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_status = mt6315_get_status,
	.set_mode = mt6315_regulator_set_mode,
	.get_mode = mt6315_regulator_get_mode,
};

static const struct mt6315_regulator_info mt6315_regulators[MT6315_VBUCK_MAX] = {
	MT_BUCK("vbuck1", MT6315_VBUCK1, MT6315_BUCK_TOP_ELR0),
	MT_BUCK("vbuck2", MT6315_VBUCK2, MT6315_BUCK_TOP_ELR2),
	MT_BUCK("vbuck3", MT6315_VBUCK3, MT6315_BUCK_TOP_ELR4),
	MT_BUCK("vbuck4", MT6315_VBUCK4, MT6315_BUCK_TOP_ELR6),
};

static const struct regmap_config mt6315_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0x16d0,
	.fast_io	= true,
};

static const struct of_device_id mt6315_of_match[] = {
	{
		.compatible = "mediatek,mt6315-regulator",
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, mt6315_of_match);

static int mt6315_regulator_probe(struct spmi_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct mt6315_chip *chip;
	struct mt_regulator_init_data *init_data;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	int i;

	regmap = devm_regmap_init_spmi_ext(pdev, &mt6315_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	chip = devm_kzalloc(dev, sizeof(struct mt6315_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	init_data = devm_kzalloc(dev, sizeof(struct mt_regulator_init_data), GFP_KERNEL);
	if (!init_data)
		return -ENOMEM;

	switch (pdev->usid) {
	case MT6315_PP:
		init_data->modeset_mask[MT6315_VBUCK1] = BIT(MT6315_VBUCK1) | BIT(MT6315_VBUCK2) |
							 BIT(MT6315_VBUCK4);
		break;
	case MT6315_SP:
	case MT6315_RP:
		init_data->modeset_mask[MT6315_VBUCK1] = BIT(MT6315_VBUCK1) | BIT(MT6315_VBUCK2);
		break;
	default:
		init_data->modeset_mask[MT6315_VBUCK1] = BIT(MT6315_VBUCK1);
		break;
	}
	for (i = MT6315_VBUCK2; i < MT6315_VBUCK_MAX; i++)
		init_data->modeset_mask[i] = BIT(i);

	chip->dev = dev;
	chip->regmap = regmap;
	dev_set_drvdata(dev, chip);

	config.dev = dev;
	config.regmap = regmap;
	for (i = MT6315_VBUCK1; i < MT6315_VBUCK_MAX; i++) {
		config.driver_data = init_data;
		rdev = devm_regulator_register(dev, &mt6315_regulators[i].desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "Failed to register %s\n",
				mt6315_regulators[i].desc.name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static void mt6315_regulator_shutdown(struct spmi_device *pdev)
{
	struct mt6315_chip *chip = dev_get_drvdata(&pdev->dev);
	int ret = 0;

	ret |= regmap_write(chip->regmap, MT6315_TOP_TMA_KEY_H, PROTECTION_KEY_H);
	ret |= regmap_write(chip->regmap, MT6315_TOP_TMA_KEY, PROTECTION_KEY);
	ret |= regmap_update_bits(chip->regmap, MT6315_TOP2_ELR7, 1, 1);
	ret |= regmap_write(chip->regmap, MT6315_TOP_TMA_KEY, 0);
	ret |= regmap_write(chip->regmap, MT6315_TOP_TMA_KEY_H, 0);
	if (ret < 0)
		dev_err(&pdev->dev, "[%#x] Failed to enable power off sequence. %d\n",
			   pdev->usid, ret);
}

static struct spmi_driver mt6315_regulator_driver = {
	.driver		= {
		.name	= "mt6315-regulator",
		.of_match_table = mt6315_of_match,
	},
	.probe = mt6315_regulator_probe,
	.shutdown = mt6315_regulator_shutdown,
};

module_spmi_driver(mt6315_regulator_driver);

MODULE_AUTHOR("Hsin-Hsiung Wang <hsin-hsiung.wang@mediatek.com>");
MODULE_DESCRIPTION("Regulator Driver for MediaTek MT6315 PMIC");
MODULE_LICENSE("GPL");
