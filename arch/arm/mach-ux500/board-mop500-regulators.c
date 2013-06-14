/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Authors: Sundar Iyer <sundar.iyer@stericsson.com>
 *          Bengt Jonsson <bengt.g.jonsson@stericsson.com>
 *          Daniel Willerud <daniel.willerud@stericsson.com>
 *
 * MOP500 board specific initialization for regulators
 */
#include <linux/kernel.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/ab8500.h>
#include "board-mop500-regulators.h"
#include "id.h"

static struct regulator_consumer_supply gpio_en_3v3_consumers[] = {
       REGULATOR_SUPPLY("vdd33a", "smsc911x.0"),
};

struct regulator_init_data gpio_en_3v3_regulator = {
       .constraints = {
               .name = "EN-3V3",
               .min_uV = 3300000,
               .max_uV = 3300000,
               .valid_ops_mask = REGULATOR_CHANGE_STATUS,
       },
       .num_consumer_supplies = ARRAY_SIZE(gpio_en_3v3_consumers),
       .consumer_supplies = gpio_en_3v3_consumers,
};

static struct regulator_consumer_supply sdi0_reg_consumers[] = {
        REGULATOR_SUPPLY("vqmmc", "sdi0"),
};

struct regulator_init_data sdi0_reg_init_data = {
        .constraints = {
                .min_uV         = 1800000,
                .max_uV         = 2900000,
                .valid_ops_mask = REGULATOR_CHANGE_VOLTAGE|REGULATOR_CHANGE_STATUS,
        },
        .num_consumer_supplies  = ARRAY_SIZE(sdi0_reg_consumers),
        .consumer_supplies      = sdi0_reg_consumers,
};

/*
 * TPS61052 regulator
 */
static struct regulator_consumer_supply tps61052_vaudio_consumers[] = {
	/*
	 * Boost converter supply to raise voltage on audio speaker, this
	 * is actually connected to three pins, VInVhfL (left amplifier)
	 * VInVhfR (right amplifier) and VIntDClassInt - all three must
	 * be connected to the same voltage.
	 */
	REGULATOR_SUPPLY("vintdclassint", "ab8500-codec.0"),
};

struct regulator_init_data tps61052_regulator = {
	.constraints = {
		.name = "vaudio-hf",
		.min_uV = 4500000,
		.max_uV = 4500000,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(tps61052_vaudio_consumers),
	.consumer_supplies = tps61052_vaudio_consumers,
};

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

static struct regulator_consumer_supply ab8505_vaux4_consumers[] = {
};

static struct regulator_consumer_supply ab8505_vaux5_consumers[] = {
};

static struct regulator_consumer_supply ab8505_vaux6_consumers[] = {
};

static struct regulator_consumer_supply ab8505_vaux8_consumers[] = {
	/* AB8500 audio codec device */
	REGULATOR_SUPPLY("v-aux8", NULL),
};

static struct regulator_consumer_supply ab8505_vadc_consumers[] = {
	/* Internal general-purpose ADC */
	REGULATOR_SUPPLY("vddadc", "ab8500-gpadc.0"),
	/* ADC for charger */
	REGULATOR_SUPPLY("vddadc", "ab8500-charger.0"),
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

static struct regulator_consumer_supply ab8505_usb_consumers[] = {
	/* HS USB OTG physical interface */
	REGULATOR_SUPPLY("v-ape", NULL),
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

/* extended configuration for VextSupply2, only used for HREFP_V20 boards */
static struct ab8500_ext_regulator_cfg ab8500_ext_supply2 = {
	.hwreq = true,
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

/* ab8505 regulator register initialization */
static struct ab8500_regulator_reg_init ab8505_reg_init[] = {
	/*
	 * VarmRequestCtrl
	 * VsmpsCRequestCtrl
	 * VsmpsARequestCtrl
	 * VsmpsBRequestCtrl
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUREQUESTCTRL1,       0x00, 0x00),
	/*
	 * VsafeRequestCtrl
	 * VpllRequestCtrl
	 * VanaRequestCtrl          = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUREQUESTCTRL2,       0x30, 0x00),
	/*
	 * Vaux1RequestCtrl         = HP/LP depending on VxRequest
	 * Vaux2RequestCtrl         = HP/LP depending on VxRequest
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUREQUESTCTRL3,       0xf0, 0x00),
	/*
	 * Vaux3RequestCtrl         = HP/LP depending on VxRequest
	 * SwHPReq                  = Control through SWValid disabled
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUREQUESTCTRL4,       0x07, 0x00),
	/*
	 * VsmpsASysClkReq1HPValid
	 * VsmpsBSysClkReq1HPValid
	 * VsafeSysClkReq1HPValid
	 * VanaSysClkReq1HPValid    = disabled
	 * VpllSysClkReq1HPValid
	 * Vaux1SysClkReq1HPValid   = disabled
	 * Vaux2SysClkReq1HPValid   = disabled
	 * Vaux3SysClkReq1HPValid   = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUSYSCLKREQ1HPVALID1, 0xe8, 0x00),
	/*
	 * VsmpsCSysClkReq1HPValid
	 * VarmSysClkReq1HPValid
	 * VbbSysClkReq1HPValid
	 * VsmpsMSysClkReq1HPValid
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUSYSCLKREQ1HPVALID2, 0x00, 0x00),
	/*
	 * VsmpsAHwHPReq1Valid
	 * VsmpsBHwHPReq1Valid
	 * VsafeHwHPReq1Valid
	 * VanaHwHPReq1Valid        = disabled
	 * VpllHwHPReq1Valid
	 * Vaux1HwHPreq1Valid       = disabled
	 * Vaux2HwHPReq1Valid       = disabled
	 * Vaux3HwHPReqValid        = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUHWHPREQ1VALID1,     0xe8, 0x00),
	/*
	 * VsmpsMHwHPReq1Valid
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUHWHPREQ1VALID2,     0x00, 0x00),
	/*
	 * VsmpsAHwHPReq2Valid
	 * VsmpsBHwHPReq2Valid
	 * VsafeHwHPReq2Valid
	 * VanaHwHPReq2Valid        = disabled
	 * VpllHwHPReq2Valid
	 * Vaux1HwHPReq2Valid       = disabled
	 * Vaux2HwHPReq2Valid       = disabled
	 * Vaux3HwHPReq2Valid       = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUHWHPREQ2VALID1,     0xe8, 0x00),
	/*
	 * VsmpsMHwHPReq2Valid
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUHWHPREQ2VALID2,     0x00, 0x00),
	/**
	 * VsmpsCSwHPReqValid
	 * VarmSwHPReqValid
	 * VsmpsASwHPReqValid
	 * VsmpsBSwHPReqValid
	 * VsafeSwHPReqValid
	 * VanaSwHPReqValid
	 * VanaSwHPReqValid         = disabled
	 * VpllSwHPReqValid
	 * Vaux1SwHPReqValid        = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUSWHPREQVALID1,      0xa0, 0x00),
	/*
	 * Vaux2SwHPReqValid        = disabled
	 * Vaux3SwHPReqValid        = disabled
	 * VsmpsMSwHPReqValid
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUSWHPREQVALID2,      0x03, 0x00),
	/*
	 * SysClkReq2Valid1         = SysClkReq2 controlled
	 * SysClkReq3Valid1         = disabled
	 * SysClkReq4Valid1         = SysClkReq4 controlled
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUSYSCLKREQVALID1,    0x0e, 0x0a),
	/*
	 * SysClkReq2Valid2         = disabled
	 * SysClkReq3Valid2         = disabled
	 * SysClkReq4Valid2         = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUSYSCLKREQVALID2,    0x0e, 0x00),
	/*
	 * Vaux4SwHPReqValid
	 * Vaux4HwHPReq2Valid
	 * Vaux4HwHPReq1Valid
	 * Vaux4SysClkReq1HPValid
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUVAUX4REQVALID,    0x00, 0x00),
	/*
	 * VadcEna                  = disabled
	 * VintCore12Ena            = disabled
	 * VintCore12Sel            = 1.25 V
	 * VintCore12LP             = inactive (HP)
	 * VadcLP                   = inactive (HP)
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUMISC1,              0xfe, 0x10),
	/*
	 * VaudioEna                = disabled
	 * Vaux8Ena                 = disabled
	 * Vamic1Ena                = disabled
	 * Vamic2Ena                = disabled
	 */
	INIT_REGULATOR_REGISTER(AB8505_VAUDIOSUPPLY,           0x1e, 0x00),
	/*
	 * Vamic1_dzout             = high-Z when Vamic1 is disabled
	 * Vamic2_dzout             = high-Z when Vamic2 is disabled
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUCTRL1VAMIC,         0x03, 0x00),
	/*
	 * VsmpsARegu
	 * VsmpsASelCtrl
	 * VsmpsAAutoMode
	 * VsmpsAPWMMode
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSMPSAREGU,    0x00, 0x00),
	/*
	 * VsmpsBRegu
	 * VsmpsBSelCtrl
	 * VsmpsBAutoMode
	 * VsmpsBPWMMode
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSMPSBREGU,    0x00, 0x00),
	/*
	 * VsafeRegu
	 * VsafeSelCtrl
	 * VsafeAutoMode
	 * VsafePWMMode
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSAFEREGU,    0x00, 0x00),
	/*
	 * VPll                     = Hw controlled (NOTE! PRCMU bits)
	 * VanaRegu                 = force off
	 */
	INIT_REGULATOR_REGISTER(AB8505_VPLLVANAREGU,           0x0f, 0x02),
	/*
	 * VextSupply1Regu          = force OFF (OTP_ExtSupply12LPnPolarity 1)
	 * VextSupply2Regu          = force OFF (OTP_ExtSupply12LPnPolarity 1)
	 * VextSupply3Regu          = force OFF (OTP_ExtSupply3LPnPolarity 0)
	 * ExtSupply2Bypass         = ExtSupply12LPn ball is 0 when Ena is 0
	 * ExtSupply3Bypass         = ExtSupply3LPn ball is 0 when Ena is 0
	 */
	INIT_REGULATOR_REGISTER(AB8505_EXTSUPPLYREGU,          0xff, 0x30),
	/*
	 * Vaux1Regu                = force HP
	 * Vaux2Regu                = force off
	 */
	INIT_REGULATOR_REGISTER(AB8505_VAUX12REGU,             0x0f, 0x01),
	/*
	 * Vaux3Regu                = force off
	 */
	INIT_REGULATOR_REGISTER(AB8505_VRF1VAUX3REGU,          0x03, 0x00),
	/*
	 * VsmpsASel1
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSMPSASEL1,    0x00, 0x00),
	/*
	 * VsmpsASel2
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSMPSASEL2,    0x00, 0x00),
	/*
	 * VsmpsASel3
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSMPSASEL3,    0x00, 0x00),
	/*
	 * VsmpsBSel1
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSMPSBSEL1,    0x00, 0x00),
	/*
	 * VsmpsBSel2
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSMPSBSEL2,    0x00, 0x00),
	/*
	 * VsmpsBSel3
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSMPSBSEL3,    0x00, 0x00),
	/*
	 * VsafeSel1
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSAFESEL1,    0x00, 0x00),
	/*
	 * VsafeSel2
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSAFESEL2,    0x00, 0x00),
	/*
	 * VsafeSel3
	 */
	INIT_REGULATOR_REGISTER(AB8505_VSAFESEL3,    0x00, 0x00),
	/*
	 * Vaux1Sel                 = 2.8 V
	 */
	INIT_REGULATOR_REGISTER(AB8505_VAUX1SEL,               0x0f, 0x0C),
	/*
	 * Vaux2Sel                 = 2.9 V
	 */
	INIT_REGULATOR_REGISTER(AB8505_VAUX2SEL,               0x0f, 0x0d),
	/*
	 * Vaux3Sel                 = 2.91 V
	 */
	INIT_REGULATOR_REGISTER(AB8505_VRF1VAUX3SEL,           0x07, 0x07),
	/*
	 * Vaux4RequestCtrl
	 */
	INIT_REGULATOR_REGISTER(AB8505_VAUX4REQCTRL,    0x00, 0x00),
	/*
	 * Vaux4Regu
	 */
	INIT_REGULATOR_REGISTER(AB8505_VAUX4REGU,    0x00, 0x00),
	/*
	 * Vaux4Sel
	 */
	INIT_REGULATOR_REGISTER(AB8505_VAUX4SEL,    0x00, 0x00),
	/*
	 * Vaux1Disch               = short discharge time
	 * Vaux2Disch               = short discharge time
	 * Vaux3Disch               = short discharge time
	 * Vintcore12Disch          = short discharge time
	 * VTVoutDisch              = short discharge time
	 * VaudioDisch              = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUCTRLDISCH,          0xfc, 0x00),
	/*
	 * VanaDisch                = short discharge time
	 * Vaux8PullDownEna         = pulldown disabled when Vaux8 is disabled
	 * Vaux8Disch               = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUCTRLDISCH2,         0x16, 0x00),
	/*
	 * Vaux4Disch               = short discharge time
	 */
	INIT_REGULATOR_REGISTER(AB8505_REGUCTRLDISCH3,         0x01, 0x00),
	/*
	 * Vaux5Sel
	 * Vaux5LP
	 * Vaux5Ena
	 * Vaux5Disch
	 * Vaux5DisSfst
	 * Vaux5DisPulld
	 */
	INIT_REGULATOR_REGISTER(AB8505_CTRLVAUX5,              0x00, 0x00),
	/*
	 * Vaux6Sel
	 * Vaux6LP
	 * Vaux6Ena
	 * Vaux6DisPulld
	 */
	INIT_REGULATOR_REGISTER(AB8505_CTRLVAUX6,              0x00, 0x00),
};

struct regulator_init_data ab8505_regulators[AB8505_NUM_REGULATORS] = {
	/* supplies to the display/camera */
	[AB8505_LDO_AUX1] = {
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
	[AB8505_LDO_AUX2] = {
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
	[AB8505_LDO_AUX3] = {
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
	/* supply for VAUX4, supplies to NFC and standalone secure element */
	[AB8505_LDO_AUX4] = {
		.constraints = {
			.name = "V-NFC-SE",
			.min_uV = 1100000,
			.max_uV = 3300000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8505_vaux4_consumers),
		.consumer_supplies = ab8505_vaux4_consumers,
	},
	/* supply for VAUX5, supplies to TBD */
	[AB8505_LDO_AUX5] = {
		.constraints = {
			.name = "V-AUX5",
			.min_uV = 1050000,
			.max_uV = 2790000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8505_vaux5_consumers),
		.consumer_supplies = ab8505_vaux5_consumers,
	},
	/* supply for VAUX6, supplies to TBD */
	[AB8505_LDO_AUX6] = {
		.constraints = {
			.name = "V-AUX6",
			.min_uV = 1050000,
			.max_uV = 2790000,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8505_vaux6_consumers),
		.consumer_supplies = ab8505_vaux6_consumers,
	},
	/* supply for gpadc, ADC LDO */
	[AB8505_LDO_ADC] = {
		.constraints = {
			.name = "V-ADC",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8505_vadc_consumers),
		.consumer_supplies = ab8505_vadc_consumers,
	},
	/* supply for ab8500-vaudio, VAUDIO LDO */
	[AB8505_LDO_AUDIO] = {
		.constraints = {
			.name = "V-AUD",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vaud_consumers),
		.consumer_supplies = ab8500_vaud_consumers,
	},
	/* supply for v-anamic1 VAMic1-LDO */
	[AB8505_LDO_ANAMIC1] = {
		.constraints = {
			.name = "V-AMIC1",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vamic1_consumers),
		.consumer_supplies = ab8500_vamic1_consumers,
	},
	/* supply for v-amic2, VAMIC2 LDO, reuse constants for AMIC1 */
	[AB8505_LDO_ANAMIC2] = {
		.constraints = {
			.name = "V-AMIC2",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vamic2_consumers),
		.consumer_supplies = ab8500_vamic2_consumers,
	},
	/* supply for v-aux8, VAUX8 LDO */
	[AB8505_LDO_AUX8] = {
		.constraints = {
			.name = "V-AUX8",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8505_vaux8_consumers),
		.consumer_supplies = ab8505_vaux8_consumers,
	},
	/* supply for v-intcore12, VINTCORE12 LDO */
	[AB8505_LDO_INTCORE] = {
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
	/* supply for LDO USB */
	[AB8505_LDO_USB] = {
		.constraints = {
			.name = "V-USB",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_MODE,
			.valid_modes_mask = REGULATOR_MODE_NORMAL |
					    REGULATOR_MODE_IDLE,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8505_usb_consumers),
		.consumer_supplies = ab8505_usb_consumers,
	},
	/* supply for U8500 CSI-DSI, VANA LDO */
	[AB8505_LDO_ANA] = {
		.constraints = {
			.name = "V-CSI-DSI",
			.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		},
		.num_consumer_supplies = ARRAY_SIZE(ab8500_vana_consumers),
		.consumer_supplies = ab8500_vana_consumers,
	},
};

struct ab8500_regulator_platform_data ab8500_regulator_plat_data = {
	.reg_init               = ab8500_reg_init,
	.num_reg_init           = ARRAY_SIZE(ab8500_reg_init),
	.regulator              = ab8500_regulators,
	.num_regulator          = ARRAY_SIZE(ab8500_regulators),
	.ext_regulator          = ab8500_ext_regulators,
	.num_ext_regulator      = ARRAY_SIZE(ab8500_ext_regulators),
};

struct ab8500_regulator_platform_data ab8505_regulator_plat_data = {
	.reg_init               = ab8505_reg_init,
	.num_reg_init           = ARRAY_SIZE(ab8505_reg_init),
	.regulator              = ab8505_regulators,
	.num_regulator          = ARRAY_SIZE(ab8505_regulators),
};

static void ab8500_modify_reg_init(int id, u8 mask, u8 value)
{
	int i;

	if (cpu_is_u8520()) {
		for (i = ARRAY_SIZE(ab8505_reg_init) - 1; i >= 0; i--) {
			if (ab8505_reg_init[i].id == id) {
				u8 initval = ab8505_reg_init[i].value;
				initval = (initval & ~mask) | (value & mask);
				ab8505_reg_init[i].value = initval;

				BUG_ON(mask & ~ab8505_reg_init[i].mask);
				return;
			}
		}
	} else {
		for (i = ARRAY_SIZE(ab8500_reg_init) - 1; i >= 0; i--) {
			if (ab8500_reg_init[i].id == id) {
				u8 initval = ab8500_reg_init[i].value;
				initval = (initval & ~mask) | (value & mask);
				ab8500_reg_init[i].value = initval;

				BUG_ON(mask & ~ab8500_reg_init[i].mask);
				return;
			}
		}
	}

	BUG_ON(1);
}

void mop500_regulator_init(void)
{
	struct regulator_init_data *regulator;

	/*
	 * Temporarily turn on Vaux2 on 8520 machine
	 */
	if (cpu_is_u8520()) {
		/* Vaux2 initialized to be on */
		ab8500_modify_reg_init(AB8505_VAUX12REGU, 0x0f, 0x05);
	}

	/*
	 * Handle AB8500_EXT_SUPPLY2 on HREFP_V20_V50 boards (do it for
	 * all HREFP_V20 boards)
	 */
	if (cpu_is_u8500v20()) {
		/* VextSupply2RequestCtrl =  HP/OFF depending on VxRequest */
		ab8500_modify_reg_init(AB8500_REGUREQUESTCTRL3, 0x01, 0x01);

		/* VextSupply2SysClkReq1HPValid = SysClkReq1 controlled */
		ab8500_modify_reg_init(AB8500_REGUSYSCLKREQ1HPVALID2,
			0x20, 0x20);

		/* VextSupply2 = force HP at initialization */
		ab8500_modify_reg_init(AB8500_EXTSUPPLYREGU, 0x0c, 0x04);

		/* enable VextSupply2 during platform active */
		regulator = &ab8500_ext_regulators[AB8500_EXT_SUPPLY2];
		regulator->constraints.always_on = 1;

		/* disable VextSupply2 in suspend */
		regulator = &ab8500_ext_regulators[AB8500_EXT_SUPPLY2];
		regulator->constraints.state_mem.disabled = 1;
		regulator->constraints.state_standby.disabled = 1;

		/* enable VextSupply2 HW control (used in suspend) */
		regulator->driver_data = (void *)&ab8500_ext_supply2;
	}
}
