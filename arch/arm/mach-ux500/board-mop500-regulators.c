/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com>
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com>
 *
 * MOP500 board specific initialization for regulators
 */
#include <linux/kernel.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/ab8500.h>

/* AB8500 regulators */
struct regulator_init_data ab8500_regulators[AB8500_NUM_REGULATORS] = {
	/* supplies to the display/camera */
	[AB8500_LDO_AUX1] = {
		.constraints = {
			.name = "V-DISPLAY",
			.min_uV = 2500000,
			.max_uV = 2900000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS,
		},
	},
	/* supplies to the on-board eMMC */
	[AB8500_LDO_AUX2] = {
		.constraints = {
			.name = "V-eMMC1",
			.min_uV = 1100000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS,
		},
	},
	/* supply for VAUX3, supplies to SDcard slots */
	[AB8500_LDO_AUX3] = {
		.constraints = {
			.name = "V-MMC-SD",
			.min_uV = 1100000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS,
		},
	},
	/* supply for tvout, gpadc, TVOUT LDO */
	[AB8500_LDO_TVOUT] = {
		.constraints = {
			.name = "V-TVOUT",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	/* supply for ab8500-vaudio, VAUDIO LDO */
	[AB8500_LDO_AUDIO] = {
		.constraints = {
			.name = "V-AUD",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	/* supply for v-anamic1 VAMic1-LDO */
	[AB8500_LDO_ANAMIC1] = {
		.constraints = {
			.name = "V-AMIC1",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	/* supply for v-amic2, VAMIC2 LDO, reuse constants for AMIC1 */
	[AB8500_LDO_ANAMIC2] = {
		.constraints = {
			.name = "V-AMIC2",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	/* supply for v-dmic, VDMIC LDO */
	[AB8500_LDO_DMIC] = {
		.constraints = {
			.name = "V-DMIC",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	/* supply for v-intcore12, VINTCORE12 LDO */
	[AB8500_LDO_INTCORE] = {
		.constraints = {
			.name = "V-INTCORE",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
	/* supply for U8500 CSI/DSI, VANA LDO */
	[AB8500_LDO_ANA] = {
		.constraints = {
			.name = "V-CSI/DSI",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
	},
};
