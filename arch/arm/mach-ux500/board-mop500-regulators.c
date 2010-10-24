/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Sundar Iyer <sundar.iyer@stericsson.com>
 *
 * MOP500 board specific initialization for regulators
 */
#include <linux/kernel.h>
#include <linux/regulator/machine.h>

/* supplies to the display/camera */
static struct regulator_init_data ab8500_vaux1_regulator = {
	.constraints = {
		.name = "V-DISPLAY",
		.min_uV = 2500000,
		.max_uV = 2900000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE|
					REGULATOR_CHANGE_STATUS,
	},
};

/* supplies to the on-board eMMC */
static struct regulator_init_data ab8500_vaux2_regulator = {
	.constraints = {
		.name = "V-eMMC1",
		.min_uV = 1100000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE|
					REGULATOR_CHANGE_STATUS,
	},
};

/* supply for VAUX3, supplies to SDcard slots */
static struct regulator_init_data ab8500_vaux3_regulator = {
	.constraints = {
		.name = "V-MMC-SD",
		.min_uV = 1100000,
		.max_uV = 3300000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE|
					REGULATOR_CHANGE_STATUS,
	},
};

/* supply for tvout, gpadc, TVOUT LDO */
static struct regulator_init_data ab8500_vtvout_init = {
	.constraints = {
		.name = "V-TVOUT",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

/* supply for ab8500-vaudio, VAUDIO LDO */
static struct regulator_init_data ab8500_vaudio_init = {
	.constraints = {
		.name = "V-AUD",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

/* supply for v-anamic1 VAMic1-LDO */
static struct regulator_init_data ab8500_vamic1_init = {
	.constraints = {
		.name = "V-AMIC1",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

/* supply for v-amic2, VAMIC2 LDO, reuse constants for AMIC1 */
static struct regulator_init_data ab8500_vamic2_init = {
	.constraints = {
		.name = "V-AMIC2",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

/* supply for v-dmic, VDMIC LDO */
static struct regulator_init_data ab8500_vdmic_init = {
	.constraints = {
		.name = "V-DMIC",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

/* supply for v-intcore12, VINTCORE12 LDO */
static struct regulator_init_data ab8500_vintcore_init = {
	.constraints = {
		.name = "V-INTCORE",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

/* supply for U8500 CSI/DSI, VANA LDO */
static struct regulator_init_data ab8500_vana_init = {
	.constraints = {
		.name = "V-CSI/DSI",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

