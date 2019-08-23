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
#include <linux/regulator/ab8500.h>

static struct regulator_consumer_supply ab8500_vaux1_consumers[] = {
	/* Main display, u8500 R3 uib */
	REGULATOR_SUPPLY("vddi", "mcde_disp_sony_acx424akp.0"),
	/* Main display, u8500 uib and ST uib */
	REGULATOR_SUPPLY("vdd1", "samsung_s6d16d0.0"),
	/* Secondary display, ST uib */
	REGULATOR_SUPPLY("vdd1", "samsung_s6d16d0.1"),
	/* SFH7741 proximity sensor */
	REGULATOR_SUPPLY("vcc", "gpio-keys.0"),
	/* BH1780GLS ambient light sensor */
	REGULATOR_SUPPLY("vcc", "2-0029"),
	/* lsm303dlh accelerometer */
	REGULATOR_SUPPLY("vdd", "2-0018"),
	/* lsm303dlhc accelerometer */
	REGULATOR_SUPPLY("vdd", "2-0019"),
	/* lsm303dlh magnetometer */
	REGULATOR_SUPPLY("vdd", "2-001e"),
	/* Rohm BU21013 Touchscreen devices */
	REGULATOR_SUPPLY("avdd", "3-005c"),
	REGULATOR_SUPPLY("avdd", "3-005d"),
	/* Synaptics RMI4 Touchscreen device */
	REGULATOR_SUPPLY("vdd", "3-004b"),
	/* L3G4200D Gyroscope device */
	REGULATOR_SUPPLY("vdd", "2-0068"),
	/* Ambient light sensor device */
	REGULATOR_SUPPLY("vdd", "3-0029"),
	/* Pressure sensor device */
	REGULATOR_SUPPLY("vdd", "2-005c"),
	/* Cypress TrueTouch Touchscreen device */
	REGULATOR_SUPPLY("vcpin", "spi8.0"),
	/* Camera device */
	REGULATOR_SUPPLY("vaux12v5", "mmio_camera"),
};

static struct regulator_consumer_supply ab8500_vaux2_consumers[] = {
	/* On-board eMMC power */
	REGULATOR_SUPPLY("vmmc", "sdi4"),
	/* AB8500 audio codec */
	REGULATOR_SUPPLY("vcc-N2158", "ab8500-codec.0"),
	/* AB8500 accessory detect 1 */
	REGULATOR_SUPPLY("vcc-N2158", "ab8500-acc-det.0"),
	/* AB8500 Tv-out device */
	REGULATOR_SUPPLY("vcc-N2158", "mcde_tv_ab8500.4"),
	/* AV8100 HDMI device */
	REGULATOR_SUPPLY("vcc-N2158", "av8100_hdmi.3"),
};

static struct regulator_consumer_supply ab8500_vaux3_consumers[] = {
	REGULATOR_SUPPLY("v-SD-STM", "stm"),
	/* External MMC slot power */
	REGULATOR_SUPPLY("vmmc", "sdi0"),
};

static struct regulator_consumer_supply ab8500_vtvout_consumers[] = {
	/* TV-out DENC supply */
	REGULATOR_SUPPLY("vtvout", "ab8500-denc.0"),
	/* Internal general-purpose ADC */
	REGULATOR_SUPPLY("vddadc", "ab8500-gpadc.0"),
	/* ADC for charger */
	REGULATOR_SUPPLY("vddadc", "ab8500-charger.0"),
	/* AB8500 Tv-out device */
	REGULATOR_SUPPLY("vtvout", "mcde_tv_ab8500.4"),
};

static struct regulator_consumer_supply ab8500_vaud_consumers[] = {
	/* AB8500 audio-codec main supply */
	REGULATOR_SUPPLY("vaud", "ab8500-codec.0"),
};

static struct regulator_consumer_supply ab8500_vamic1_consumers[] = {
	/* AB8500 audio-codec Mic1 supply */
	REGULATOR_SUPPLY("vamic1", "ab8500-codec.0"),
};

static struct regulator_consumer_supply ab8500_vamic2_consumers[] = {
	/* AB8500 audio-codec Mic2 supply */
	REGULATOR_SUPPLY("vamic2", "ab8500-codec.0"),
};

static struct regulator_consumer_supply ab8500_vdmic_consumers[] = {
	/* AB8500 audio-codec DMic supply */
	REGULATOR_SUPPLY("vdmic", "ab8500-codec.0"),
};

static struct regulator_consumer_supply ab8500_vintcore_consumers[] = {
	/* SoC core supply, no device */
	REGULATOR_SUPPLY("v-intcore", NULL),
	/* USB Transceiver */
	REGULATOR_SUPPLY("vddulpivio18", "ab8500-usb.0"),
	/* Handled by abx500 clk driver */
	REGULATOR_SUPPLY("v-intcore", "abx500-clk.0"),
};

static struct regulator_consumer_supply ab8500_vana_consumers[] = {
	/* DB8500 DSI */
	REGULATOR_SUPPLY("vdddsi1v2", "mcde"),
	REGULATOR_SUPPLY("vdddsi1v2", "b2r2_core"),
	REGULATOR_SUPPLY("vdddsi1v2", "b2r2_1_core"),
	REGULATOR_SUPPLY("vdddsi1v2", "dsilink.0"),
	REGULATOR_SUPPLY("vdddsi1v2", "dsilink.1"),
	REGULATOR_SUPPLY("vdddsi1v2", "dsilink.2"),
	/* DB8500 CSI */
	REGULATOR_SUPPLY("vddcsi1v2", "mmio_camera"),
};

/* ab8500 regulator register initialization */
static struct ab8500_regulator_reg_init ab8500_reg_init[] = {
	/*
	 * VanaRequestCtrl          = HP/LP depending on VxRequest
	 * VextSupply1RequestCtrl   = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUREQUESTCTRL2,       0xf0, 0x00),
	/*
	 * VextSupply2RequestCtrl   = HP/LP depending on VxRequest
	 * VextSupply3RequestCtrl   = HP/LP depending on VxRequest
	 * Vaux1RequestCtrl         = HP/LP depending on VxRequest
	 * Vaux2RequestCtrl         = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUREQUESTCTRL3,       0xff, 0x00),
	/*
	 * Vaux3RequestCtrl         = HP/LP depending on VxRequest
	 * SwHPReq                  = Control through SWValid disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUREQUESTCTRL4,       0x07, 0x00),
	/*
	 * VanaSysClkReq1HPValid    = disabled
	 * Vaux1SysClkReq1HPValid   = disabled
	 * Vaux2SysClkReq1HPValid   = disabled
	 * Vaux3SysClkReq1HPValid   = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQ1HPVALID1, 0xe8, 0x00),
	/*
	 * VextSupply1SysClkReq1HPValid = disabled
	 * VextSupply2SysClkReq1HPValid = disabled
	 * VextSupply3SysClkReq1HPValid = SysClkReq1 controlled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQ1HPVALID2, 0x70, 0x40),
	/*
	 * VanaHwHPReq1Valid        = disabled
	 * Vaux1HwHPreq1Valid       = disabled
	 * Vaux2HwHPReq1Valid       = disabled
	 * Vaux3HwHPReqValid        = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUHWHPREQ1VALID1,     0xe8, 0x00),
	/*
	 * VextSupply1HwHPReq1Valid = disabled
	 * VextSupply2HwHPReq1Valid = disabled
	 * VextSupply3HwHPReq1Valid = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUHWHPREQ1VALID2,     0x07, 0x00),
	/*
	 * VanaHwHPReq2Valid        = disabled
	 * Vaux1HwHPReq2Valid       = disabled
	 * Vaux2HwHPReq2Valid       = disabled
	 * Vaux3HwHPReq2Valid       = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUHWHPREQ2VALID1,     0xe8, 0x00),
	/*
	 * VextSupply1HwHPReq2Valid = disabled
	 * VextSupply2HwHPReq2Valid = disabled
	 * VextSupply3HwHPReq2Valid = HWReq2 controlled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUHWHPREQ2VALID2,     0x07, 0x04),
	/*
	 * VanaSwHPReqValid         = disabled
	 * Vaux1SwHPReqValid        = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSWHPREQVALID1,      0xa0, 0x00),
	/*
	 * Vaux2SwHPReqValid        = disabled
	 * Vaux3SwHPReqValid        = disabled
	 * VextSupply1SwHPReqValid  = disabled
	 * VextSupply2SwHPReqValid  = disabled
	 * VextSupply3SwHPReqValid  = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSWHPREQVALID2,      0x1f, 0x00),
	/*
	 * SysClkReq2Valid1         = SysClkReq2 controlled
	 * SysClkReq3Valid1         = disabled
	 * SysClkReq4Valid1         = SysClkReq4 controlled
	 * SysClkReq5Valid1         = disabled
	 * SysClkReq6Valid1         = SysClkReq6 controlled
	 * SysClkReq7Valid1         = disabled
	 * SysClkReq8Valid1         = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQVALID1,    0xfe, 0x2a),
	/*
	 * SysClkReq2Valid2         = disabled
	 * SysClkReq3Valid2         = disabled
	 * SysClkReq4Valid2         = disabled
	 * SysClkReq5Valid2         = disabled
	 * SysClkReq6Valid2         = SysClkReq6 controlled
	 * SysClkReq7Valid2         = disabled
	 * SysClkReq8Valid2         = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUSYSCLKREQVALID2,    0xfe, 0x20),
	/*
	 * VTVoutEna                = disabled
	 * Vintcore12Ena            = disabled
	 * Vintcore12Sel            = 1.25 V
	 * Vintcore12LP             = inactive (HP)
	 * VTVoutLP                 = inactive (HP)
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUMISC1,              0xfe, 0x10),
	/*
	 * VaudioEna                = disabled
	 * VdmicEna                 = disabled
	 * Vamic1Ena                = disabled
	 * Vamic2Ena                = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUDIOSUPPLY,           0x1e, 0x00),
	/*
	 * Vamic1_dzout             = high-Z when Vamic1 is disabled
	 * Vamic2_dzout             = high-Z when Vamic2 is disabled
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRL1VAMIC,         0x03, 0x00),
	/*
	 * VPll                     = Hw controlled (NOTE! PRCMU bits)
	 * VanaRegu                 = force off
	 */
	INIT_REGULATOR_REGISTER(AB8500_VPLLVANAREGU,           0x0f, 0x02),
	/*
	 * VrefDDREna               = disabled
	 * VrefDDRSleepMode         = inactive (no pulldown)
	 */
	INIT_REGULATOR_REGISTER(AB8500_VREFDDR,                0x03, 0x00),
	/*
	 * VextSupply1Regu          = force LP
	 * VextSupply2Regu          = force OFF
	 * VextSupply3Regu          = force HP (-> STBB2=LP and TPS=LP)
	 * ExtSupply2Bypass         = ExtSupply12LPn ball is 0 when Ena is 0
	 * ExtSupply3Bypass         = ExtSupply3LPn ball is 0 when Ena is 0
	 */
	INIT_REGULATOR_REGISTER(AB8500_EXTSUPPLYREGU,          0xff, 0x13),
	/*
	 * Vaux1Regu                = force HP
	 * Vaux2Regu                = force off
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUX12REGU,             0x0f, 0x01),
	/*
	 * Vaux3Regu                = force off
	 */
	INIT_REGULATOR_REGISTER(AB8500_VRF1VAUX3REGU,          0x03, 0x00),
	/*
	 * Vaux1Sel                 = 2.8 V
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUX1SEL,               0x0f, 0x0C),
	/*
	 * Vaux2Sel                 = 2.9 V
	 */
	INIT_REGULATOR_REGISTER(AB8500_VAUX2SEL,               0x0f, 0x0d),
	/*
	 * Vaux3Sel                 = 2.91 V
	 */
	INIT_REGULATOR_REGISTER(AB8500_VRF1VAUX3SEL,           0x07, 0x07),
	/*
	 * VextSupply12LP           = disabled (no LP)
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRL2SPARE,         0x01, 0x00),
	/*
	 * Vaux1Disch               = short discharge time
	 * Vaux2Disch               = short discharge time
	 * Vaux3Disch               = short discharge time
	 * Vintcore12Disch          = short discharge time
	 * VTVoutDisch              = short discharge time
	 * VaudioDisch              = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRLDISCH,          0xfc, 0x00),
	/*
	 * VanaDisch                = short discharge time
	 * VdmicPullDownEna         = pulldown disabled when Vdmic is disabled
	 * VdmicDisch               = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB8500_REGUCTRLDISCH2,         0x16, 0x00),
};

/* AB8500 regulators */
static struct regulator_init_data ab8500_regulators[AB8500_NUM_REGULATORS] = {
	/* supplies to the display/camera */
	[AB8500_LDO_AUX1] = {
		.supply_regulator = "ab8500-ext-supply3",
		.constraints = {
			.name = "V-DISPLAY",
			.min_uV = 2800000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS,
			.boot_on = 1, /* display is on at boot */
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaux1_consumers),
		.consumer_supplies = ab8500_vaux1_consumers,
	},
	/* supplies to the on-board eMMC */
	[AB8500_LDO_AUX2] = {
		.supply_regulator = "ab8500-ext-supply3",
		.constraints = {
			.name = "V-eMMC1",
			.min_uV = 1100000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaux2_consumers),
		.consumer_supplies = ab8500_vaux2_consumers,
	},
	/* supply for VAUX3, supplies to SDcard slots */
	[AB8500_LDO_AUX3] = {
		.supply_regulator = "ab8500-ext-supply3",
		.constraints = {
			.name = "V-MMC-SD",
			.min_uV = 1100000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
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
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaud_consumers),
		.consumer_supplies = ab8500_vaud_consumers,
	},
	/* supply for v-anamic1 VAMic1-LDO */
	[AB8500_LDO_ANAMIC1] = {
		.constraints = {
			.name = "V-AMIC1",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vamic1_consumers),
		.consumer_supplies = ab8500_vamic1_consumers,
	},
	/* supply for v-amic2, VAMIC2 LDO, reuse constants for AMIC1 */
	[AB8500_LDO_ANAMIC2] = {
		.constraints = {
			.name = "V-AMIC2",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vamic2_consumers),
		.consumer_supplies = ab8500_vamic2_consumers,
	},
	/* supply for v-dmic, VDMIC LDO */
	[AB8500_LDO_DMIC] = {
		.constraints = {
			.name = "V-DMIC",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vdmic_consumers),
		.consumer_supplies = ab8500_vdmic_consumers,
	},
	/* supply for v-intcore12, VINTCORE12 LDO */
	[AB8500_LDO_INTCORE] = {
		.constraints = {
			.name = "V-INTCORE",
			.min_uV = 1250000,
			.max_uV = 1350000,
			.input_uV = 1800000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE |
					  REGULATOR_CHANGE_DRMS,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vintcore_consumers),
		.consumer_supplies = ab8500_vintcore_consumers,
	},
	/* supply for U8500 CSI-DSI, VANA LDO */
	[AB8500_LDO_ANA] = {
		.constraints = {
			.name = "V-CSI-DSI",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vana_consumers),
		.consumer_supplies = ab8500_vana_consumers,
	},
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

static struct ab8500_regulator_platform_data ab8500_regulator_plat_data = {
	.reg_init               = ab8500_reg_init,
	.num_reg_init           = ARRAY_SIZE(ab8500_reg_init),
	.regulator              = ab8500_regulators,
	.num_regulator          = ARRAY_SIZE(ab8500_regulators),
	.ext_regulator          = ab8500_ext_regulators,
	.num_ext_regulator      = ARRAY_SIZE(ab8500_ext_regulators),
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
	struct ab8500_regulator_platform_data *pdata = &ab8500_regulator_plat_data;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int i;

	if (!ab8500) {
		dev_err(&pdev->dev, "null mfd parent\n");
		return -EINVAL;
	}

	/* make sure the platform data has the correct size */
	if (pdata->num_ext_regulator != ARRAY_SIZE(ab8500_ext_regulator_info)) {
		dev_err(&pdev->dev, "Configuration error: size mismatch.\n");
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
			pdata->ext_regulator[i].driver_data;

		config.dev = &pdev->dev;
		config.driver_data = info;
		config.init_data = &pdata->ext_regulator[i];

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
