// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *
 * Power domain regulators on DB8500
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/db8500-prcmu.h>
#include <linux/regulator/of_regulator.h>
#include <linux/of.h>
#include <linux/module.h>
#include "dbx500-prcmu.h"

static int db8500_regulator_enable(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL)
		return -EINVAL;

	dev_vdbg(rdev_get_dev(rdev), "regulator-%s-enable\n",
		info->desc.name);

	if (!info->is_enabled) {
		info->is_enabled = true;
		if (!info->exclude_from_power_state)
			power_state_active_enable();
	}

	return 0;
}

static int db8500_regulator_disable(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);
	int ret = 0;

	if (info == NULL)
		return -EINVAL;

	dev_vdbg(rdev_get_dev(rdev), "regulator-%s-disable\n",
		info->desc.name);

	if (info->is_enabled) {
		info->is_enabled = false;
		if (!info->exclude_from_power_state)
			ret = power_state_active_disable();
	}

	return ret;
}

static int db8500_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL)
		return -EINVAL;

	dev_vdbg(rdev_get_dev(rdev), "regulator-%s-is_enabled (is_enabled):"
		" %i\n", info->desc.name, info->is_enabled);

	return info->is_enabled;
}

/* db8500 regulator operations */
static const struct regulator_ops db8500_regulator_ops = {
	.enable			= db8500_regulator_enable,
	.disable		= db8500_regulator_disable,
	.is_enabled		= db8500_regulator_is_enabled,
};

/*
 * EPOD control
 */
static bool epod_on[NUM_EPOD_ID];
static bool epod_ramret[NUM_EPOD_ID];

static int enable_epod(u16 epod_id, bool ramret)
{
	int ret;

	if (ramret) {
		if (!epod_on[epod_id]) {
			ret = prcmu_set_epod(epod_id, EPOD_STATE_RAMRET);
			if (ret < 0)
				return ret;
		}
		epod_ramret[epod_id] = true;
	} else {
		ret = prcmu_set_epod(epod_id, EPOD_STATE_ON);
		if (ret < 0)
			return ret;
		epod_on[epod_id] = true;
	}

	return 0;
}

static int disable_epod(u16 epod_id, bool ramret)
{
	int ret;

	if (ramret) {
		if (!epod_on[epod_id]) {
			ret = prcmu_set_epod(epod_id, EPOD_STATE_OFF);
			if (ret < 0)
				return ret;
		}
		epod_ramret[epod_id] = false;
	} else {
		if (epod_ramret[epod_id]) {
			ret = prcmu_set_epod(epod_id, EPOD_STATE_RAMRET);
			if (ret < 0)
				return ret;
		} else {
			ret = prcmu_set_epod(epod_id, EPOD_STATE_OFF);
			if (ret < 0)
				return ret;
		}
		epod_on[epod_id] = false;
	}

	return 0;
}

/*
 * Regulator switch
 */
static int db8500_regulator_switch_enable(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	if (info == NULL)
		return -EINVAL;

	dev_vdbg(rdev_get_dev(rdev), "regulator-switch-%s-enable\n",
		info->desc.name);

	ret = enable_epod(info->epod_id, info->is_ramret);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"regulator-switch-%s-enable: prcmu call failed\n",
			info->desc.name);
		goto out;
	}

	info->is_enabled = true;
out:
	return ret;
}

static int db8500_regulator_switch_disable(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	if (info == NULL)
		return -EINVAL;

	dev_vdbg(rdev_get_dev(rdev), "regulator-switch-%s-disable\n",
		info->desc.name);

	ret = disable_epod(info->epod_id, info->is_ramret);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"regulator_switch-%s-disable: prcmu call failed\n",
			info->desc.name);
		goto out;
	}

	info->is_enabled = false;
out:
	return ret;
}

static int db8500_regulator_switch_is_enabled(struct regulator_dev *rdev)
{
	struct dbx500_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL)
		return -EINVAL;

	dev_vdbg(rdev_get_dev(rdev),
		"regulator-switch-%s-is_enabled (is_enabled): %i\n",
		info->desc.name, info->is_enabled);

	return info->is_enabled;
}

static const struct regulator_ops db8500_regulator_switch_ops = {
	.enable			= db8500_regulator_switch_enable,
	.disable		= db8500_regulator_switch_disable,
	.is_enabled		= db8500_regulator_switch_is_enabled,
};

/*
 * Regulator information
 */
static struct dbx500_regulator_info
dbx500_regulator_info[DB8500_NUM_REGULATORS] = {
	[DB8500_REGULATOR_VAPE] = {
		.desc = {
			.name	= "db8500-vape",
			.of_match = of_match_ptr("db8500_vape"),
			.id	= DB8500_REGULATOR_VAPE,
			.ops	= &db8500_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
	},
	[DB8500_REGULATOR_VARM] = {
		.desc = {
			.name	= "db8500-varm",
			.of_match = of_match_ptr("db8500_varm"),
			.id	= DB8500_REGULATOR_VARM,
			.ops	= &db8500_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
	},
	[DB8500_REGULATOR_VMODEM] = {
		.desc = {
			.name	= "db8500-vmodem",
			.of_match = of_match_ptr("db8500_vmodem"),
			.id	= DB8500_REGULATOR_VMODEM,
			.ops	= &db8500_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
	},
	[DB8500_REGULATOR_VPLL] = {
		.desc = {
			.name	= "db8500-vpll",
			.of_match = of_match_ptr("db8500_vpll"),
			.id	= DB8500_REGULATOR_VPLL,
			.ops	= &db8500_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
	},
	[DB8500_REGULATOR_VSMPS1] = {
		.desc = {
			.name	= "db8500-vsmps1",
			.of_match = of_match_ptr("db8500_vsmps1"),
			.id	= DB8500_REGULATOR_VSMPS1,
			.ops	= &db8500_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
	},
	[DB8500_REGULATOR_VSMPS2] = {
		.desc = {
			.name	= "db8500-vsmps2",
			.of_match = of_match_ptr("db8500_vsmps2"),
			.id	= DB8500_REGULATOR_VSMPS2,
			.ops	= &db8500_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
			.fixed_uV = 1800000,
			.n_voltages = 1,
		},
		.exclude_from_power_state = true,
	},
	[DB8500_REGULATOR_VSMPS3] = {
		.desc = {
			.name	= "db8500-vsmps3",
			.of_match = of_match_ptr("db8500_vsmps3"),
			.id	= DB8500_REGULATOR_VSMPS3,
			.ops	= &db8500_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
	},
	[DB8500_REGULATOR_VRF1] = {
		.desc = {
			.name	= "db8500-vrf1",
			.of_match = of_match_ptr("db8500_vrf1"),
			.id	= DB8500_REGULATOR_VRF1,
			.ops	= &db8500_regulator_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
	},
	[DB8500_REGULATOR_SWITCH_SVAMMDSP] = {
		.desc = {
			.name	= "db8500-sva-mmdsp",
			.of_match = of_match_ptr("db8500_sva_mmdsp"),
			.id	= DB8500_REGULATOR_SWITCH_SVAMMDSP,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id = EPOD_ID_SVAMMDSP,
	},
	[DB8500_REGULATOR_SWITCH_SVAMMDSPRET] = {
		.desc = {
			.name	= "db8500-sva-mmdsp-ret",
			.of_match = of_match_ptr("db8500_sva_mmdsp_ret"),
			.id	= DB8500_REGULATOR_SWITCH_SVAMMDSPRET,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id = EPOD_ID_SVAMMDSP,
		.is_ramret = true,
	},
	[DB8500_REGULATOR_SWITCH_SVAPIPE] = {
		.desc = {
			.name	= "db8500-sva-pipe",
			.of_match = of_match_ptr("db8500_sva_pipe"),
			.id	= DB8500_REGULATOR_SWITCH_SVAPIPE,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id = EPOD_ID_SVAPIPE,
	},
	[DB8500_REGULATOR_SWITCH_SIAMMDSP] = {
		.desc = {
			.name	= "db8500-sia-mmdsp",
			.of_match = of_match_ptr("db8500_sia_mmdsp"),
			.id	= DB8500_REGULATOR_SWITCH_SIAMMDSP,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id = EPOD_ID_SIAMMDSP,
	},
	[DB8500_REGULATOR_SWITCH_SIAMMDSPRET] = {
		.desc = {
			.name	= "db8500-sia-mmdsp-ret",
			.of_match = of_match_ptr("db8500_sia_mmdsp_ret"),
			.id	= DB8500_REGULATOR_SWITCH_SIAMMDSPRET,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id = EPOD_ID_SIAMMDSP,
		.is_ramret = true,
	},
	[DB8500_REGULATOR_SWITCH_SIAPIPE] = {
		.desc = {
			.name	= "db8500-sia-pipe",
			.of_match = of_match_ptr("db8500_sia_pipe"),
			.id	= DB8500_REGULATOR_SWITCH_SIAPIPE,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id = EPOD_ID_SIAPIPE,
	},
	[DB8500_REGULATOR_SWITCH_SGA] = {
		.desc = {
			.name	= "db8500-sga",
			.of_match = of_match_ptr("db8500_sga"),
			.id	= DB8500_REGULATOR_SWITCH_SGA,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id = EPOD_ID_SGA,
	},
	[DB8500_REGULATOR_SWITCH_B2R2_MCDE] = {
		.desc = {
			.name	= "db8500-b2r2-mcde",
			.of_match = of_match_ptr("db8500_b2r2_mcde"),
			.id	= DB8500_REGULATOR_SWITCH_B2R2_MCDE,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id = EPOD_ID_B2R2_MCDE,
	},
	[DB8500_REGULATOR_SWITCH_ESRAM12] = {
		.desc = {
			.name	= "db8500-esram12",
			.of_match = of_match_ptr("db8500_esram12"),
			.id	= DB8500_REGULATOR_SWITCH_ESRAM12,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id	= EPOD_ID_ESRAM12,
		.is_enabled	= true,
	},
	[DB8500_REGULATOR_SWITCH_ESRAM12RET] = {
		.desc = {
			.name	= "db8500-esram12-ret",
			.of_match = of_match_ptr("db8500_esram12_ret"),
			.id	= DB8500_REGULATOR_SWITCH_ESRAM12RET,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id = EPOD_ID_ESRAM12,
		.is_ramret = true,
	},
	[DB8500_REGULATOR_SWITCH_ESRAM34] = {
		.desc = {
			.name	= "db8500-esram34",
			.of_match = of_match_ptr("db8500_esram34"),
			.id	= DB8500_REGULATOR_SWITCH_ESRAM34,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id	= EPOD_ID_ESRAM34,
		.is_enabled	= true,
	},
	[DB8500_REGULATOR_SWITCH_ESRAM34RET] = {
		.desc = {
			.name	= "db8500-esram34-ret",
			.of_match = of_match_ptr("db8500_esram34_ret"),
			.id	= DB8500_REGULATOR_SWITCH_ESRAM34RET,
			.ops	= &db8500_regulator_switch_ops,
			.type	= REGULATOR_VOLTAGE,
			.owner	= THIS_MODULE,
		},
		.epod_id = EPOD_ID_ESRAM34,
		.is_ramret = true,
	},
};

static int db8500_regulator_probe(struct platform_device *pdev)
{
	struct regulator_init_data *db8500_init_data;
	struct dbx500_regulator_info *info;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int err, i;

	db8500_init_data = dev_get_platdata(&pdev->dev);

	for (i = 0; i < ARRAY_SIZE(dbx500_regulator_info); i++) {
		/* assign per-regulator data */
		info = &dbx500_regulator_info[i];

		config.driver_data = info;
		config.dev = &pdev->dev;
		if (db8500_init_data)
			config.init_data = &db8500_init_data[i];

		rdev = devm_regulator_register(&pdev->dev, &info->desc,
					       &config);
		if (IS_ERR(rdev)) {
			err = PTR_ERR(rdev);
			dev_err(&pdev->dev, "failed to register %s: err %i\n",
				info->desc.name, err);
			return err;
		}
		dev_dbg(&pdev->dev, "regulator-%s-probed\n", info->desc.name);
	}

	ux500_regulator_debug_init(pdev, dbx500_regulator_info,
				   ARRAY_SIZE(dbx500_regulator_info));
	return 0;
}

static void db8500_regulator_remove(struct platform_device *pdev)
{
	ux500_regulator_debug_exit();
}

static struct platform_driver db8500_regulator_driver = {
	.driver = {
		.name = "db8500-prcmu-regulators",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = db8500_regulator_probe,
	.remove_new = db8500_regulator_remove,
};

static int __init db8500_regulator_init(void)
{
	return platform_driver_register(&db8500_regulator_driver);
}

static void __exit db8500_regulator_exit(void)
{
	platform_driver_unregister(&db8500_regulator_driver);
}

arch_initcall(db8500_regulator_init);
module_exit(db8500_regulator_exit);

MODULE_AUTHOR("STMicroelectronics/ST-Ericsson");
MODULE_DESCRIPTION("DB8500 regulator driver");
MODULE_LICENSE("GPL v2");
