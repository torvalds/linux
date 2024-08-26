// SPDX-License-Identifier: GPL-2.0+
/*
 * Core driver for Renesas Synchronization Management Unit (SMU) devices.
 *
 * Copyright (C) 2021 Integrated Device Technology, Inc., a Renesas Company.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rsmu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "rsmu.h"

enum {
	RSMU_PHC = 0,
	RSMU_CDEV = 1,
	RSMU_N_DEVS = 2,
};

static struct mfd_cell rsmu_cm_devs[] = {
	[RSMU_PHC] = {
		.name = "8a3400x-phc",
	},
	[RSMU_CDEV] = {
		.name = "8a3400x-cdev",
	},
};

static struct mfd_cell rsmu_sabre_devs[] = {
	[RSMU_PHC] = {
		.name = "82p33x1x-phc",
	},
	[RSMU_CDEV] = {
		.name = "82p33x1x-cdev",
	},
};

static struct mfd_cell rsmu_sl_devs[] = {
	[RSMU_PHC] = {
		.name = "8v19n85x-phc",
	},
	[RSMU_CDEV] = {
		.name = "8v19n85x-cdev",
	},
};

int rsmu_core_init(struct rsmu_ddata *rsmu)
{
	struct mfd_cell *cells;
	int ret;

	switch (rsmu->type) {
	case RSMU_CM:
		cells = rsmu_cm_devs;
		break;
	case RSMU_SABRE:
		cells = rsmu_sabre_devs;
		break;
	case RSMU_SL:
		cells = rsmu_sl_devs;
		break;
	default:
		dev_err(rsmu->dev, "Unsupported RSMU device type: %d\n", rsmu->type);
		return -ENODEV;
	}

	mutex_init(&rsmu->lock);

	ret = devm_mfd_add_devices(rsmu->dev, PLATFORM_DEVID_AUTO, cells,
				   RSMU_N_DEVS, NULL, 0, NULL);
	if (ret < 0)
		dev_err(rsmu->dev, "Failed to register sub-devices: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(rsmu_core_init);

void rsmu_core_exit(struct rsmu_ddata *rsmu)
{
	mutex_destroy(&rsmu->lock);
}
EXPORT_SYMBOL_GPL(rsmu_core_exit);

MODULE_DESCRIPTION("Renesas SMU core driver");
MODULE_LICENSE("GPL");
