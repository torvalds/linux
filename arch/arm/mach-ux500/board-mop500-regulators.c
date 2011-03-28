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
#include "board-mop500-regulators.h"

static struct regulator_consumer_supply ab8500_vaux1_consumers[] = {
	/* External displays, connector on board 2v5 power supply */
	REGULATOR_SUPPLY("vaux12v5", "mcde.0"),
	/* SFH7741 proximity sensor */
	REGULATOR_SUPPLY("vcc", "gpio-keys.0"),
	/* BH1780GLS ambient light sensor */
	REGULATOR_SUPPLY("vcc", "2-0029"),
	/* lsm303dlh accelerometer */
	REGULATOR_SUPPLY("vdd", "3-0018"),
	/* lsm303dlh magnetometer */
	REGULATOR_SUPPLY("vdd", "3-001e"),
	/* Rohm BU21013 Touchscreen devices */
	REGULATOR_SUPPLY("avdd", "3-005c"),
	REGULATOR_SUPPLY("avdd", "3-005d"),
	/* Synaptics RMI4 Touchscreen device */
	REGULATOR_SUPPLY("vdd", "3-004b"),
};

static struct regulator_consumer_supply ab8500_vaux2_consumers[] = {
	/* On-board eMMC power */
	REGULATOR_SUPPLY("vmmc", "sdi4"),
	/* AB8500 audio codec */
	REGULATOR_SUPPLY("vcc-N2158", "ab8500-codec.0"),
};

static struct regulator_consumer_supply ab8500_vaux3_consumers[] = {
	/* External MMC slot power */
	REGULATOR_SUPPLY("vmmc", "sdi0"),
};

static struct regulator_consumer_supply ab8500_vtvout_consumers[] = {
	/* TV-out DENC supply */
	REGULATOR_SUPPLY("vtvout", "ab8500-denc.0"),
	/* Internal general-purpose ADC */
	REGULATOR_SUPPLY("vddadc", "ab8500-gpadc.0"),
};

static struct regulator_consumer_supply ab8500_vintcore_consumers[] = {
	/* SoC core supply, no device */
	REGULATOR_SUPPLY("v-intcore", NULL),
	/* USB Transciever */
	REGULATOR_SUPPLY("vddulpivio18", "ab8500-usb.0"),
};

static struct regulator_consumer_supply ab8500_vana_consumers[] = {
	/* External displays, connector on board, 1v8 power supply */
	REGULATOR_SUPPLY("vsmps2", "mcde.0"),
};

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
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaux1_consumers),
		.consumer_supplies = ab8500_vaux1_consumers,
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
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaux2_consumers),
		.consumer_supplies = ab8500_vaux2_consumers,
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
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaux3_consumers),
		.consumer_supplies = ab8500_vaux3_consumers,
	},
	/* supply for tvout, gpadc, TVOUT LDO */
	[AB8500_LDO_TVOUT] = {
		.constraints = {
			.name = "V-TVOUT",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vtvout_consumers),
		.consumer_supplies = ab8500_vtvout_consumers,
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
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vintcore_consumers),
		.consumer_supplies = ab8500_vintcore_consumers,
	},
	/* supply for U8500 CSI/DSI, VANA LDO */
	[AB8500_LDO_ANA] = {
		.constraints = {
			.name = "V-CSI/DSI",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vana_consumers),
		.consumer_supplies = ab8500_vana_consumers,
	},
};
