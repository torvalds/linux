// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Authors: Bengt Jonsson <bengt.g.jonsson@stericsson.com>
 *
 * This file is based on drivers/regulator/ab8500.c
 *
 * AB8500 external regulators
 *
 * ab8500-ext supports the following regulators:
 * - VextSupply3
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>

/* AB8500 external regulators */
enum ab8500_ext_regulator_id {
	AB8500_EXT_SUPPLY1,
	AB8500_EXT_SUPPLY2,
	AB8500_EXT_SUPPLY3,
	AB8500_NUM_EXT_REGULATORS,
};

struct ab8500_ext_regulator_cfg {
	bool hwreq; /* requires hw mode or high power mode */
};

/* supply for VextSupply3 */
static struct regulator_consumer_supply ab8500_ext_supply3_consumers[] = {
	/* SIM supply for 3 V SIM cards */
	REGULATOR_SUPPLY("vinvsim", "sim-detect.0"),
};

/*
 * AB8500 external regulators
 */
static struct regulator_init_data ab8500_ext_regulators[] = {
	/* fixed Vbat supplies VSMPS1_EXT_1V8 */
	[AB8500_EXT_SUPPLY1] = {
		.constraints = {
			.name = "ab8500-ext-supply1",
			.min_uV = 1800000,
			.max_uV = 1800000,
			.initial_mode = REGULATOR_MODE_IDLE,
			.boot_on = 1,
			.always_on = 1,
		},
	},
	/* fixed Vbat supplies VSMPS2_EXT_1V36 and VSMPS5_EXT_1V15 */
	[AB8500_EXT_SUPPLY2] = {
		.constraints = {
			.name = "ab8500-ext-supply2",
			.min_uV = 1360000,
			.max_uV = 1360000,
		},
	},
	/* fixed Vbat supplies VSMPS3_EXT_3V4 and VSMPS4_EXT_3V4 */
	[AB8500_EXT_SUPPLY3] = {
		.constraints = {
			.name = "ab8500-ext-supply3",
			.min_uV = 3400000,
			.max_uV = 3400000,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
			.boot_on = 1,
		},
		.num_consumer_supplies =
			ARRAY_SIZE(ab8500_ext_supply3_consumers),
		.consumer_supplies = ab8500_ext_supply3_consumers,
	},
};

/**
 * struct ab8500_ext_regulator_info - ab8500 regulator information
 * @dev: device pointer
 * @desc: regulator description
 * @cfg: regulator configuration (extension of regulator FW configuration)
 * @update_bank: bank to control on/off
 * @update_reg: register to control on/off
 * @update_mask: mask to enable/disable and set mode of regulator
 * @update_val: bits holding the regulator current mode
 * @update_val_hp: bits to set EN pin active (LPn pin deactive)
 *                 normally this means high power mode
 * @update_val_lp: bits to set EN pin active and LPn pin active
 *                 normally this means low power mode
 * @update_val_hw: bits to set regulator pins in HW control
 *                 SysClkReq pins and logic will choose mode
 */
struct ab8500_ext_regulator_info {
	struct device *dev;
	struct regulator_desc desc;
	struct ab8500_ext_regulator_cfg *cfg;
	u8 update_bank;
	u8 update_reg;
	u8 update_mask;
	u8 update_val;
	u8 update_val_hp;
	u8 update_val_lp;
	u8 update_val_hw;
};

static int ab8500_ext_regulator_enable(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_ext_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	/*
	 * To satisfy both HW high power request and SW request, the regulator
	 * must be on in high power.
	 */
	if (info->cfg && info->cfg->hwreq)
		regval = info->update_val_hp;
	else
		regval = info->update_val;

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, regval);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't set enable bits for regulator\n");
		return ret;
	}

	dev_dbg(rdev_get_dev(rdev),
		"%s-enable (bank, reg, mask, value): 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, regval);

	return 0;
}

static int ab8500_ext_regulator_disable(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_ext_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	/*
	 * Set the regulator in HW request mode if configured
	 */
	if (info->cfg && info->cfg->hwreq)
		regval = info->update_val_hw;
	else
		regval = 0;

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg,
		info->update_mask, regval);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't set disable bits for regulator\n");
		return ret;
	}

	dev_dbg(rdev_get_dev(rdev), "%s-disable (bank, reg, mask, value):"
		" 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, regval);

	return 0;
}

static int ab8500_ext_regulator_is_enabled(struct regulator_dev *rdev)
{
	int ret;
	struct ab8500_ext_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	ret = abx500_get_register_interruptible(info->dev,
		info->update_bank, info->update_reg, &regval);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read 0x%x register\n", info->update_reg);
		return ret;
	}

	dev_dbg(rdev_get_dev(rdev), "%s-is_enabled (bank, reg, mask, value):"
		" 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
		info->desc.name, info->update_bank, info->update_reg,
		info->update_mask, regval);

	if (((regval & info->update_mask) == info->update_val_lp) ||
	    ((regval & info->update_mask) == info->update_val_hp))
		return 1;
	else
		return 0;
}

static int ab8500_ext_regulator_set_mode(struct regulator_dev *rdev,
					 unsigned int mode)
{
	int ret = 0;
	struct ab8500_ext_regulator_info *info = rdev_get_drvdata(rdev);
	u8 regval;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		regval = info->update_val_hp;
		break;
	case REGULATOR_MODE_IDLE:
		regval = info->update_val_lp;
		break;

	default:
		return -EINVAL;
	}

	/* If regulator is enabled and info->cfg->hwreq is set, the regulator
	   must be on in high power, so we don't need to write the register with
	   the same value.
	 */
	if (ab8500_ext_regulator_is_enabled(rdev) &&
	    !(info->cfg && info->cfg->hwreq)) {
		ret = abx500_mask_and_set_register_interruptible(info->dev,
					info->update_bank, info->update_reg,
					info->update_mask, regval);
		if (ret < 0) {
			dev_err(rdev_get_dev(rdev),
				"Could not set regulator mode.\n");
			return ret;
		}

		dev_dbg(rdev_get_dev(rdev),
			"%s-set_mode (bank, reg, mask, value): "
			"0x%x, 0x%x, 0x%x, 0x%x\n",
			info->desc.name, info->update_bank, info->update_reg,
			info->update_mask, regval);
	}

	info->update_val = regval;

	return 0;
}

static unsigned int ab8500_ext_regulator_get_mode(struct regulator_dev *rdev)
{
	struct ab8500_ext_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return -EINVAL;
	}

	if (info->update_val == info->update_val_hp)
		ret = REGULATOR_MODE_NORMAL;
	else if (info->update_val == info->update_val_lp)
		ret = REGULATOR_MODE_IDLE;
	else
		ret = -EINVAL;

	return ret;
}

static int ab8500_ext_set_voltage(struct regulator_dev *rdev, int min_uV,
				  int max_uV, unsigned *selector)
{
	struct regulation_constraints *regu_constraints = rdev->constraints;

	if (!regu_constraints) {
		dev_err(rdev_get_dev(rdev), "No regulator constraints\n");
		return -EINVAL;
	}

	if (regu_constraints->min_uV == min_uV &&
	    regu_constraints->max_uV == max_uV)
		return 0;

	dev_err(rdev_get_dev(rdev),
		"Requested min %duV max %duV != constrained min %duV max %duV\n",
		min_uV, max_uV,
		regu_constraints->min_uV, regu_constraints->max_uV);

	return -EINVAL;
}

static int ab8500_ext_list_voltage(struct regulator_dev *rdev,
				   unsigned selector)
{
	struct regulation_constraints *regu_constraints = rdev->constraints;

	if (regu_constraints == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator constraints null pointer\n");
		return -EINVAL;
	}
	/* return the uV for the fixed regulators */
	if (regu_constraints->min_uV && regu_constraints->max_uV) {
		if (regu_constraints->min_uV == regu_constraints->max_uV)
			return regu_constraints->min_uV;
	}
	return -EINVAL;
}

static const struct regulator_ops ab8500_ext_regulator_ops = {
	.enable			= ab8500_ext_regulator_enable,
	.disable		= ab8500_ext_regulator_disable,
	.is_enabled		= ab8500_ext_regulator_is_enabled,
	.set_mode		= ab8500_ext_regulator_set_mode,
	.get_mode		= ab8500_ext_regulator_get_mode,
	.set_voltage		= ab8500_ext_set_voltage,
	.list_voltage		= ab8500_ext_list_voltage,
};

static struct ab8500_ext_regulator_info
		ab8500_ext_regulator_info[AB8500_NUM_EXT_REGULATORS] = {
	[AB8500_EXT_SUPPLY1] = {
		.desc = {
			.name		= "VEXTSUPPLY1",
			.of_match	= of_match_ptr("ab8500_ext1"),
			.ops		= &ab8500_ext_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_EXT_SUPPLY1,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.update_bank		= 0x04,
		.update_reg		= 0x08,
		.update_mask		= 0x03,
		.update_val		= 0x01,
		.update_val_hp		= 0x01,
		.update_val_lp		= 0x03,
		.update_val_hw		= 0x02,
	},
	[AB8500_EXT_SUPPLY2] = {
		.desc = {
			.name		= "VEXTSUPPLY2",
			.of_match	= of_match_ptr("ab8500_ext2"),
			.ops		= &ab8500_ext_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_EXT_SUPPLY2,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.update_bank		= 0x04,
		.update_reg		= 0x08,
		.update_mask		= 0x0c,
		.update_val		= 0x04,
		.update_val_hp		= 0x04,
		.update_val_lp		= 0x0c,
		.update_val_hw		= 0x08,
	},
	[AB8500_EXT_SUPPLY3] = {
		.desc = {
			.name		= "VEXTSUPPLY3",
			.of_match	= of_match_ptr("ab8500_ext3"),
			.ops		= &ab8500_ext_regulator_ops,
			.type		= REGULATOR_VOLTAGE,
			.id		= AB8500_EXT_SUPPLY3,
			.owner		= THIS_MODULE,
			.n_voltages	= 1,
		},
		.update_bank		= 0x04,
		.update_reg		= 0x08,
		.update_mask		= 0x30,
		.update_val		= 0x10,
		.update_val_hp		= 0x10,
		.update_val_lp		= 0x30,
		.update_val_hw		= 0x20,
	},
};

static int ab8500_ext_regulator_probe(struct platform_device *pdev)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int i;

	if (!ab8500) {
		dev_err(&pdev->dev, "null mfd parent\n");
		return -EINVAL;
	}

	/* check for AB8500 2.x */
	if (is_ab8500_2p0_or_earlier(ab8500)) {
		struct ab8500_ext_regulator_info *info;

		/* VextSupply3LPn is inverted on AB8500 2.x */
		info = &ab8500_ext_regulator_info[AB8500_EXT_SUPPLY3];
		info->update_val = 0x30;
		info->update_val_hp = 0x30;
		info->update_val_lp = 0x10;
	}

	/* register all regulators */
	for (i = 0; i < ARRAY_SIZE(ab8500_ext_regulator_info); i++) {
		struct ab8500_ext_regulator_info *info = NULL;

		/* assign per-regulator data */
		info = &ab8500_ext_regulator_info[i];
		info->dev = &pdev->dev;
		info->cfg = (struct ab8500_ext_regulator_cfg *)
			ab8500_ext_regulators[i].driver_data;

		config.dev = &pdev->dev;
		config.driver_data = info;
		config.init_data = &ab8500_ext_regulators[i];

		/* register regulator with framework */
		rdev = devm_regulator_register(&pdev->dev, &info->desc,
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "failed to register regulator %s\n",
					info->desc.name);
			return PTR_ERR(rdev);
		}

		dev_dbg(&pdev->dev, "%s-probed\n", info->desc.name);
	}

	return 0;
}

static struct platform_driver ab8500_ext_regulator_driver = {
	.probe = ab8500_ext_regulator_probe,
	.driver         = {
		.name   = "ab8500-ext-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int __init ab8500_ext_regulator_init(void)
{
	int ret;

	ret = platform_driver_register(&ab8500_ext_regulator_driver);
	if (ret)
		pr_err("Failed to register ab8500 ext regulator: %d\n", ret);

	return ret;
}
subsys_initcall(ab8500_ext_regulator_init);

static void __exit ab8500_ext_regulator_exit(void)
{
	platform_driver_unregister(&ab8500_ext_regulator_driver);
}
module_exit(ab8500_ext_regulator_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Bengt Jonsson <bengt.g.jonsson@stericsson.com>");
MODULE_DESCRIPTION("AB8500 external regulator driver");
MODULE_ALIAS("platform:ab8500-ext-regulator");
