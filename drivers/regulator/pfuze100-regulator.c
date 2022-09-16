// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2011-2013 Freescale Semiconductor, Inc. All Rights Reserved.

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/pfuze100.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#define PFUZE_FLAG_DISABLE_SW	BIT(1)

#define PFUZE_NUMREGS		128
#define PFUZE100_VOL_OFFSET	0
#define PFUZE100_STANDBY_OFFSET	1
#define PFUZE100_MODE_OFFSET	3
#define PFUZE100_CONF_OFFSET	4

#define PFUZE100_DEVICEID	0x0
#define PFUZE100_REVID		0x3
#define PFUZE100_FABID		0x4

#define PFUZE100_COINVOL	0x1a
#define PFUZE100_SW1ABVOL	0x20
#define PFUZE100_SW1ABMODE	0x23
#define PFUZE100_SW1CVOL	0x2e
#define PFUZE100_SW1CMODE	0x31
#define PFUZE100_SW2VOL		0x35
#define PFUZE100_SW2MODE	0x38
#define PFUZE100_SW3AVOL	0x3c
#define PFUZE100_SW3AMODE	0x3f
#define PFUZE100_SW3BVOL	0x43
#define PFUZE100_SW3BMODE	0x46
#define PFUZE100_SW4VOL		0x4a
#define PFUZE100_SW4MODE	0x4d
#define PFUZE100_SWBSTCON1	0x66
#define PFUZE100_VREFDDRCON	0x6a
#define PFUZE100_VSNVSVOL	0x6b
#define PFUZE100_VGEN1VOL	0x6c
#define PFUZE100_VGEN2VOL	0x6d
#define PFUZE100_VGEN3VOL	0x6e
#define PFUZE100_VGEN4VOL	0x6f
#define PFUZE100_VGEN5VOL	0x70
#define PFUZE100_VGEN6VOL	0x71

#define PFUZE100_SWxMODE_MASK	0xf
#define PFUZE100_SWxMODE_APS_APS	0x8
#define PFUZE100_SWxMODE_APS_OFF	0x4

#define PFUZE100_VGENxLPWR	BIT(6)
#define PFUZE100_VGENxSTBY	BIT(5)

enum chips { PFUZE100, PFUZE200, PFUZE3000 = 3, PFUZE3001 = 0x31, };

struct pfuze_regulator {
	struct regulator_desc desc;
	unsigned char stby_reg;
	unsigned char stby_mask;
	bool sw_reg;
};

struct pfuze_chip {
	int	chip_id;
	int     flags;
	struct regmap *regmap;
	struct device *dev;
	struct pfuze_regulator regulator_descs[PFUZE100_MAX_REGULATOR];
	struct regulator_dev *regulators[PFUZE100_MAX_REGULATOR];
	struct pfuze_regulator *pfuze_regulators;
};

static const int pfuze100_swbst[] = {
	5000000, 5050000, 5100000, 5150000,
};

static const int pfuze100_vsnvs[] = {
	1000000, 1100000, 1200000, 1300000, 1500000, 1800000, 3000000,
};

static const int pfuze100_coin[] = {
	2500000, 2700000, 2800000, 2900000, 3000000, 3100000, 3200000, 3300000,
};

static const int pfuze3000_sw1a[] = {
	700000, 725000, 750000, 775000, 800000, 825000, 850000, 875000,
	900000, 925000, 950000, 975000, 1000000, 1025000, 1050000, 1075000,
	1100000, 1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000,
	1300000, 1325000, 1350000, 1375000, 1400000, 1425000, 1800000, 3300000,
};

static const int pfuze3000_sw2lo[] = {
	1500000, 1550000, 1600000, 1650000, 1700000, 1750000, 1800000, 1850000,
};

static const int pfuze3000_sw2hi[] = {
	2500000, 2800000, 2850000, 3000000, 3100000, 3150000, 3200000, 3300000,
};

static const struct of_device_id pfuze_dt_ids[] = {
	{ .compatible = "fsl,pfuze100", .data = (void *)PFUZE100},
	{ .compatible = "fsl,pfuze200", .data = (void *)PFUZE200},
	{ .compatible = "fsl,pfuze3000", .data = (void *)PFUZE3000},
	{ .compatible = "fsl,pfuze3001", .data = (void *)PFUZE3001},
	{ }
};
MODULE_DEVICE_TABLE(of, pfuze_dt_ids);

static int pfuze100_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct pfuze_chip *pfuze100 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	bool reg_has_ramp_delay;
	unsigned int ramp_bits = 0;
	int ret;

	switch (pfuze100->chip_id) {
	case PFUZE3001:
		/* no dynamic voltage scaling for PF3001 */
		reg_has_ramp_delay = false;
		break;
	case PFUZE3000:
		reg_has_ramp_delay = (id < PFUZE3000_SWBST);
		break;
	case PFUZE200:
		reg_has_ramp_delay = (id < PFUZE200_SWBST);
		break;
	case PFUZE100:
	default:
		reg_has_ramp_delay = (id < PFUZE100_SWBST);
		break;
	}

	if (reg_has_ramp_delay) {
		if (ramp_delay > 0) {
			ramp_delay = 12500 / ramp_delay;
			ramp_bits = (ramp_delay >> 1) - (ramp_delay >> 3);
		}

		ret = regmap_update_bits(pfuze100->regmap,
					 rdev->desc->vsel_reg + 4,
					 0xc0, ramp_bits << 6);
		if (ret < 0)
			dev_err(pfuze100->dev, "ramp failed, err %d\n", ret);
	} else {
		ret = -EACCES;
	}

	return ret;
}

static const struct regulator_ops pfuze100_ldo_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_ops pfuze100_fixed_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
};

static const struct regulator_ops pfuze100_sw_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay = pfuze100_set_ramp_delay,
};

static const struct regulator_ops pfuze100_sw_disable_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay = pfuze100_set_ramp_delay,
};

static const struct regulator_ops pfuze100_swb_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,

};

static const struct regulator_ops pfuze3000_sw_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay = pfuze100_set_ramp_delay,

};

#define PFUZE100_FIXED_REG(_chip, _name, base, voltage)	\
	[_chip ## _ ## _name] = {	\
		.desc = {	\
			.name = #_name,	\
			.n_voltages = 1,	\
			.ops = &pfuze100_fixed_regulator_ops,	\
			.type = REGULATOR_VOLTAGE,	\
			.id = _chip ## _ ## _name,	\
			.owner = THIS_MODULE,	\
			.min_uV = (voltage),	\
			.enable_reg = (base),	\
			.enable_mask = 0x10,	\
		},	\
	}

#define PFUZE100_SW_REG(_chip, _name, base, min, max, step)	\
	[_chip ## _ ## _name] = {	\
		.desc = {	\
			.name = #_name,\
			.n_voltages = ((max) - (min)) / (step) + 1,	\
			.ops = &pfuze100_sw_regulator_ops,	\
			.type = REGULATOR_VOLTAGE,	\
			.id = _chip ## _ ## _name,	\
			.owner = THIS_MODULE,	\
			.min_uV = (min),	\
			.uV_step = (step),	\
			.vsel_reg = (base) + PFUZE100_VOL_OFFSET,	\
			.vsel_mask = 0x3f,	\
			.enable_reg = (base) + PFUZE100_MODE_OFFSET,	\
			.enable_mask = 0xf,	\
		},	\
		.stby_reg = (base) + PFUZE100_STANDBY_OFFSET,	\
		.stby_mask = 0x3f,	\
		.sw_reg = true,		\
	}

#define PFUZE100_SWB_REG(_chip, _name, base, mask, voltages)	\
	[_chip ## _ ##  _name] = {	\
		.desc = {	\
			.name = #_name,	\
			.n_voltages = ARRAY_SIZE(voltages),	\
			.ops = &pfuze100_swb_regulator_ops,	\
			.type = REGULATOR_VOLTAGE,	\
			.id = _chip ## _ ## _name,	\
			.owner = THIS_MODULE,	\
			.volt_table = voltages,	\
			.vsel_reg = (base),	\
			.vsel_mask = (mask),	\
			.enable_reg = (base),	\
			.enable_mask = 0x48,	\
		},	\
	}

#define PFUZE100_VGEN_REG(_chip, _name, base, min, max, step)	\
	[_chip ## _ ## _name] = {	\
		.desc = {	\
			.name = #_name,	\
			.n_voltages = ((max) - (min)) / (step) + 1,	\
			.ops = &pfuze100_ldo_regulator_ops,	\
			.type = REGULATOR_VOLTAGE,	\
			.id = _chip ## _ ## _name,	\
			.owner = THIS_MODULE,	\
			.min_uV = (min),	\
			.uV_step = (step),	\
			.vsel_reg = (base),	\
			.vsel_mask = 0xf,	\
			.enable_reg = (base),	\
			.enable_mask = 0x10,	\
		},	\
		.stby_reg = (base),	\
		.stby_mask = 0x20,	\
	}

#define PFUZE100_COIN_REG(_chip, _name, base, mask, voltages)	\
	[_chip ## _ ##  _name] = {	\
		.desc = {	\
			.name = #_name,	\
			.n_voltages = ARRAY_SIZE(voltages),	\
			.ops = &pfuze100_swb_regulator_ops,	\
			.type = REGULATOR_VOLTAGE,	\
			.id = _chip ## _ ## _name,	\
			.owner = THIS_MODULE,	\
			.volt_table = voltages,	\
			.vsel_reg = (base),	\
			.vsel_mask = (mask),	\
			.enable_reg = (base),	\
			.enable_mask = 0x8,	\
		},	\
	}

#define PFUZE3000_VCC_REG(_chip, _name, base, min, max, step)	{	\
	.desc = {	\
		.name = #_name,	\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.ops = &pfuze100_ldo_regulator_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = _chip ## _ ## _name,	\
		.owner = THIS_MODULE,	\
		.min_uV = (min),	\
		.uV_step = (step),	\
		.vsel_reg = (base),	\
		.vsel_mask = 0x3,	\
		.enable_reg = (base),	\
		.enable_mask = 0x10,	\
	},	\
	.stby_reg = (base),	\
	.stby_mask = 0x20,	\
}

/* No linar case for the some switches of PFUZE3000 */
#define PFUZE3000_SW_REG(_chip, _name, base, mask, voltages)	\
	[_chip ## _ ##  _name] = {	\
		.desc = {	\
			.name = #_name,	\
			.n_voltages = ARRAY_SIZE(voltages),	\
			.ops = &pfuze3000_sw_regulator_ops,	\
			.type = REGULATOR_VOLTAGE,	\
			.id = _chip ## _ ## _name,	\
			.owner = THIS_MODULE,	\
			.volt_table = voltages,	\
			.vsel_reg = (base) + PFUZE100_VOL_OFFSET,	\
			.vsel_mask = (mask),	\
			.enable_reg = (base) + PFUZE100_MODE_OFFSET,	\
			.enable_mask = 0xf,	\
			.enable_val = 0x8,	\
			.enable_time = 500,	\
		},	\
		.stby_reg = (base) + PFUZE100_STANDBY_OFFSET,	\
		.stby_mask = (mask),	\
		.sw_reg = true,		\
	}

#define PFUZE3000_SW3_REG(_chip, _name, base, min, max, step)	{	\
	.desc = {	\
		.name = #_name,\
		.n_voltages = ((max) - (min)) / (step) + 1,	\
		.ops = &pfuze100_sw_regulator_ops,	\
		.type = REGULATOR_VOLTAGE,	\
		.id = _chip ## _ ## _name,	\
		.owner = THIS_MODULE,	\
		.min_uV = (min),	\
		.uV_step = (step),	\
		.vsel_reg = (base) + PFUZE100_VOL_OFFSET,	\
		.vsel_mask = 0xf,	\
	},	\
	.stby_reg = (base) + PFUZE100_STANDBY_OFFSET,	\
	.stby_mask = 0xf,	\
}

/* PFUZE100 */
static struct pfuze_regulator pfuze100_regulators[] = {
	PFUZE100_SW_REG(PFUZE100, SW1AB, PFUZE100_SW1ABVOL, 300000, 1875000, 25000),
	PFUZE100_SW_REG(PFUZE100, SW1C, PFUZE100_SW1CVOL, 300000, 1875000, 25000),
	PFUZE100_SW_REG(PFUZE100, SW2, PFUZE100_SW2VOL, 400000, 1975000, 25000),
	PFUZE100_SW_REG(PFUZE100, SW3A, PFUZE100_SW3AVOL, 400000, 1975000, 25000),
	PFUZE100_SW_REG(PFUZE100, SW3B, PFUZE100_SW3BVOL, 400000, 1975000, 25000),
	PFUZE100_SW_REG(PFUZE100, SW4, PFUZE100_SW4VOL, 400000, 1975000, 25000),
	PFUZE100_SWB_REG(PFUZE100, SWBST, PFUZE100_SWBSTCON1, 0x3 , pfuze100_swbst),
	PFUZE100_SWB_REG(PFUZE100, VSNVS, PFUZE100_VSNVSVOL, 0x7, pfuze100_vsnvs),
	PFUZE100_FIXED_REG(PFUZE100, VREFDDR, PFUZE100_VREFDDRCON, 750000),
	PFUZE100_VGEN_REG(PFUZE100, VGEN1, PFUZE100_VGEN1VOL, 800000, 1550000, 50000),
	PFUZE100_VGEN_REG(PFUZE100, VGEN2, PFUZE100_VGEN2VOL, 800000, 1550000, 50000),
	PFUZE100_VGEN_REG(PFUZE100, VGEN3, PFUZE100_VGEN3VOL, 1800000, 3300000, 100000),
	PFUZE100_VGEN_REG(PFUZE100, VGEN4, PFUZE100_VGEN4VOL, 1800000, 3300000, 100000),
	PFUZE100_VGEN_REG(PFUZE100, VGEN5, PFUZE100_VGEN5VOL, 1800000, 3300000, 100000),
	PFUZE100_VGEN_REG(PFUZE100, VGEN6, PFUZE100_VGEN6VOL, 1800000, 3300000, 100000),
	PFUZE100_COIN_REG(PFUZE100, COIN, PFUZE100_COINVOL, 0x7, pfuze100_coin),
};

static struct pfuze_regulator pfuze200_regulators[] = {
	PFUZE100_SW_REG(PFUZE200, SW1AB, PFUZE100_SW1ABVOL, 300000, 1875000, 25000),
	PFUZE100_SW_REG(PFUZE200, SW2, PFUZE100_SW2VOL, 400000, 1975000, 25000),
	PFUZE100_SW_REG(PFUZE200, SW3A, PFUZE100_SW3AVOL, 400000, 1975000, 25000),
	PFUZE100_SW_REG(PFUZE200, SW3B, PFUZE100_SW3BVOL, 400000, 1975000, 25000),
	PFUZE100_SWB_REG(PFUZE200, SWBST, PFUZE100_SWBSTCON1, 0x3 , pfuze100_swbst),
	PFUZE100_SWB_REG(PFUZE200, VSNVS, PFUZE100_VSNVSVOL, 0x7, pfuze100_vsnvs),
	PFUZE100_FIXED_REG(PFUZE200, VREFDDR, PFUZE100_VREFDDRCON, 750000),
	PFUZE100_VGEN_REG(PFUZE200, VGEN1, PFUZE100_VGEN1VOL, 800000, 1550000, 50000),
	PFUZE100_VGEN_REG(PFUZE200, VGEN2, PFUZE100_VGEN2VOL, 800000, 1550000, 50000),
	PFUZE100_VGEN_REG(PFUZE200, VGEN3, PFUZE100_VGEN3VOL, 1800000, 3300000, 100000),
	PFUZE100_VGEN_REG(PFUZE200, VGEN4, PFUZE100_VGEN4VOL, 1800000, 3300000, 100000),
	PFUZE100_VGEN_REG(PFUZE200, VGEN5, PFUZE100_VGEN5VOL, 1800000, 3300000, 100000),
	PFUZE100_VGEN_REG(PFUZE200, VGEN6, PFUZE100_VGEN6VOL, 1800000, 3300000, 100000),
	PFUZE100_COIN_REG(PFUZE200, COIN, PFUZE100_COINVOL, 0x7, pfuze100_coin),
};

static struct pfuze_regulator pfuze3000_regulators[] = {
	PFUZE3000_SW_REG(PFUZE3000, SW1A, PFUZE100_SW1ABVOL, 0x1f, pfuze3000_sw1a),
	PFUZE100_SW_REG(PFUZE3000, SW1B, PFUZE100_SW1CVOL, 700000, 1475000, 25000),
	PFUZE3000_SW_REG(PFUZE3000, SW2, PFUZE100_SW2VOL, 0x7, pfuze3000_sw2lo),
	PFUZE3000_SW3_REG(PFUZE3000, SW3, PFUZE100_SW3AVOL, 900000, 1650000, 50000),
	PFUZE100_SWB_REG(PFUZE3000, SWBST, PFUZE100_SWBSTCON1, 0x3, pfuze100_swbst),
	PFUZE100_SWB_REG(PFUZE3000, VSNVS, PFUZE100_VSNVSVOL, 0x7, pfuze100_vsnvs),
	PFUZE100_FIXED_REG(PFUZE3000, VREFDDR, PFUZE100_VREFDDRCON, 750000),
	PFUZE100_VGEN_REG(PFUZE3000, VLDO1, PFUZE100_VGEN1VOL, 1800000, 3300000, 100000),
	PFUZE100_VGEN_REG(PFUZE3000, VLDO2, PFUZE100_VGEN2VOL, 800000, 1550000, 50000),
	PFUZE3000_VCC_REG(PFUZE3000, VCCSD, PFUZE100_VGEN3VOL, 2850000, 3300000, 150000),
	PFUZE3000_VCC_REG(PFUZE3000, V33, PFUZE100_VGEN4VOL, 2850000, 3300000, 150000),
	PFUZE100_VGEN_REG(PFUZE3000, VLDO3, PFUZE100_VGEN5VOL, 1800000, 3300000, 100000),
	PFUZE100_VGEN_REG(PFUZE3000, VLDO4, PFUZE100_VGEN6VOL, 1800000, 3300000, 100000),
};

static struct pfuze_regulator pfuze3001_regulators[] = {
	PFUZE3000_SW_REG(PFUZE3001, SW1, PFUZE100_SW1ABVOL, 0x1f, pfuze3000_sw1a),
	PFUZE3000_SW_REG(PFUZE3001, SW2, PFUZE100_SW2VOL, 0x7, pfuze3000_sw2lo),
	PFUZE3000_SW3_REG(PFUZE3001, SW3, PFUZE100_SW3AVOL, 900000, 1650000, 50000),
	PFUZE100_SWB_REG(PFUZE3001, VSNVS, PFUZE100_VSNVSVOL, 0x7, pfuze100_vsnvs),
	PFUZE100_VGEN_REG(PFUZE3001, VLDO1, PFUZE100_VGEN1VOL, 1800000, 3300000, 100000),
	PFUZE100_VGEN_REG(PFUZE3001, VLDO2, PFUZE100_VGEN2VOL, 800000, 1550000, 50000),
	PFUZE3000_VCC_REG(PFUZE3001, VCCSD, PFUZE100_VGEN3VOL, 2850000, 3300000, 150000),
	PFUZE3000_VCC_REG(PFUZE3001, V33, PFUZE100_VGEN4VOL, 2850000, 3300000, 150000),
	PFUZE100_VGEN_REG(PFUZE3001, VLDO3, PFUZE100_VGEN5VOL, 1800000, 3300000, 100000),
	PFUZE100_VGEN_REG(PFUZE3001, VLDO4, PFUZE100_VGEN6VOL, 1800000, 3300000, 100000),
};

/* PFUZE100 */
static struct of_regulator_match pfuze100_matches[] = {
	{ .name = "sw1ab",	},
	{ .name = "sw1c",	},
	{ .name = "sw2",	},
	{ .name = "sw3a",	},
	{ .name = "sw3b",	},
	{ .name = "sw4",	},
	{ .name = "swbst",	},
	{ .name = "vsnvs",	},
	{ .name = "vrefddr",	},
	{ .name = "vgen1",	},
	{ .name = "vgen2",	},
	{ .name = "vgen3",	},
	{ .name = "vgen4",	},
	{ .name = "vgen5",	},
	{ .name = "vgen6",	},
	{ .name = "coin",	},
};

/* PFUZE200 */
static struct of_regulator_match pfuze200_matches[] = {

	{ .name = "sw1ab",	},
	{ .name = "sw2",	},
	{ .name = "sw3a",	},
	{ .name = "sw3b",	},
	{ .name = "swbst",	},
	{ .name = "vsnvs",	},
	{ .name = "vrefddr",	},
	{ .name = "vgen1",	},
	{ .name = "vgen2",	},
	{ .name = "vgen3",	},
	{ .name = "vgen4",	},
	{ .name = "vgen5",	},
	{ .name = "vgen6",	},
	{ .name = "coin",	},
};

/* PFUZE3000 */
static struct of_regulator_match pfuze3000_matches[] = {

	{ .name = "sw1a",	},
	{ .name = "sw1b",	},
	{ .name = "sw2",	},
	{ .name = "sw3",	},
	{ .name = "swbst",	},
	{ .name = "vsnvs",	},
	{ .name = "vrefddr",	},
	{ .name = "vldo1",	},
	{ .name = "vldo2",	},
	{ .name = "vccsd",	},
	{ .name = "v33",	},
	{ .name = "vldo3",	},
	{ .name = "vldo4",	},
};

/* PFUZE3001 */
static struct of_regulator_match pfuze3001_matches[] = {

	{ .name = "sw1",	},
	{ .name = "sw2",	},
	{ .name = "sw3",	},
	{ .name = "vsnvs",	},
	{ .name = "vldo1",	},
	{ .name = "vldo2",	},
	{ .name = "vccsd",	},
	{ .name = "v33",	},
	{ .name = "vldo3",	},
	{ .name = "vldo4",	},
};

static struct of_regulator_match *pfuze_matches;

static int pfuze_parse_regulators_dt(struct pfuze_chip *chip)
{
	struct device *dev = chip->dev;
	struct device_node *np, *parent;
	int ret;

	np = of_node_get(dev->of_node);
	if (!np)
		return -EINVAL;

	if (of_property_read_bool(np, "fsl,pfuze-support-disable-sw"))
		chip->flags |= PFUZE_FLAG_DISABLE_SW;

	parent = of_get_child_by_name(np, "regulators");
	if (!parent) {
		dev_err(dev, "regulators node not found\n");
		of_node_put(np);
		return -EINVAL;
	}

	switch (chip->chip_id) {
	case PFUZE3001:
		pfuze_matches = pfuze3001_matches;
		ret = of_regulator_match(dev, parent, pfuze3001_matches,
					 ARRAY_SIZE(pfuze3001_matches));
		break;
	case PFUZE3000:
		pfuze_matches = pfuze3000_matches;
		ret = of_regulator_match(dev, parent, pfuze3000_matches,
					 ARRAY_SIZE(pfuze3000_matches));
		break;
	case PFUZE200:
		pfuze_matches = pfuze200_matches;
		ret = of_regulator_match(dev, parent, pfuze200_matches,
					 ARRAY_SIZE(pfuze200_matches));
		break;

	case PFUZE100:
	default:
		pfuze_matches = pfuze100_matches;
		ret = of_regulator_match(dev, parent, pfuze100_matches,
					 ARRAY_SIZE(pfuze100_matches));
		break;
	}

	of_node_put(parent);
	of_node_put(np);
	if (ret < 0) {
		dev_err(dev, "Error parsing regulator init data: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static inline struct regulator_init_data *match_init_data(int index)
{
	return pfuze_matches[index].init_data;
}

static inline struct device_node *match_of_node(int index)
{
	return pfuze_matches[index].of_node;
}

static int pfuze_power_off_prepare(struct sys_off_data *data)
{
	struct pfuze_chip *syspm_pfuze_chip = data->cb_data;

	dev_info(syspm_pfuze_chip->dev, "Configure standby mode for power off");

	/* Switch from default mode: APS/APS to APS/Off */
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_SW1ABMODE,
			   PFUZE100_SWxMODE_MASK, PFUZE100_SWxMODE_APS_OFF);
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_SW1CMODE,
			   PFUZE100_SWxMODE_MASK, PFUZE100_SWxMODE_APS_OFF);
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_SW2MODE,
			   PFUZE100_SWxMODE_MASK, PFUZE100_SWxMODE_APS_OFF);
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_SW3AMODE,
			   PFUZE100_SWxMODE_MASK, PFUZE100_SWxMODE_APS_OFF);
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_SW3BMODE,
			   PFUZE100_SWxMODE_MASK, PFUZE100_SWxMODE_APS_OFF);
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_SW4MODE,
			   PFUZE100_SWxMODE_MASK, PFUZE100_SWxMODE_APS_OFF);

	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_VGEN1VOL,
			   PFUZE100_VGENxLPWR | PFUZE100_VGENxSTBY,
			   PFUZE100_VGENxSTBY);
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_VGEN2VOL,
			   PFUZE100_VGENxLPWR | PFUZE100_VGENxSTBY,
			   PFUZE100_VGENxSTBY);
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_VGEN3VOL,
			   PFUZE100_VGENxLPWR | PFUZE100_VGENxSTBY,
			   PFUZE100_VGENxSTBY);
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_VGEN4VOL,
			   PFUZE100_VGENxLPWR | PFUZE100_VGENxSTBY,
			   PFUZE100_VGENxSTBY);
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_VGEN5VOL,
			   PFUZE100_VGENxLPWR | PFUZE100_VGENxSTBY,
			   PFUZE100_VGENxSTBY);
	regmap_update_bits(syspm_pfuze_chip->regmap, PFUZE100_VGEN6VOL,
			   PFUZE100_VGENxLPWR | PFUZE100_VGENxSTBY,
			   PFUZE100_VGENxSTBY);

	return NOTIFY_DONE;
}

static int pfuze_power_off_prepare_init(struct pfuze_chip *pfuze_chip)
{
	int err;

	if (pfuze_chip->chip_id != PFUZE100) {
		dev_warn(pfuze_chip->dev, "Requested pm_power_off_prepare handler for not supported chip\n");
		return -ENODEV;
	}

	err = devm_register_sys_off_handler(pfuze_chip->dev,
					    SYS_OFF_MODE_POWER_OFF_PREPARE,
					    SYS_OFF_PRIO_DEFAULT,
					    pfuze_power_off_prepare,
					    pfuze_chip);
	if (err) {
		dev_err(pfuze_chip->dev, "failed to register sys-off handler: %d\n",
			err);
		return err;
	}

	return 0;
}

static int pfuze_identify(struct pfuze_chip *pfuze_chip)
{
	unsigned int value;
	int ret;

	ret = regmap_read(pfuze_chip->regmap, PFUZE100_DEVICEID, &value);
	if (ret)
		return ret;

	if (((value & 0x0f) == 0x8) && (pfuze_chip->chip_id == PFUZE100)) {
		/*
		 * Freescale misprogrammed 1-3% of parts prior to week 8 of 2013
		 * as ID=8 in PFUZE100
		 */
		dev_info(pfuze_chip->dev, "Assuming misprogrammed ID=0x8");
	} else if ((value & 0x0f) != pfuze_chip->chip_id &&
		   (value & 0xf0) >> 4 != pfuze_chip->chip_id &&
		   (value != pfuze_chip->chip_id)) {
		/* device id NOT match with your setting */
		dev_warn(pfuze_chip->dev, "Illegal ID: %x\n", value);
		return -ENODEV;
	}

	ret = regmap_read(pfuze_chip->regmap, PFUZE100_REVID, &value);
	if (ret)
		return ret;
	dev_info(pfuze_chip->dev,
		 "Full layer: %x, Metal layer: %x\n",
		 (value & 0xf0) >> 4, value & 0x0f);

	ret = regmap_read(pfuze_chip->regmap, PFUZE100_FABID, &value);
	if (ret)
		return ret;
	dev_info(pfuze_chip->dev, "FAB: %x, FIN: %x\n",
		 (value & 0xc) >> 2, value & 0x3);

	return 0;
}

static const struct regmap_config pfuze_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PFUZE_NUMREGS - 1,
	.cache_type = REGCACHE_RBTREE,
};

static int pfuze100_regulator_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	struct pfuze_chip *pfuze_chip;
	struct regulator_config config = { };
	int i, ret;
	const struct of_device_id *match;
	u32 regulator_num;
	u32 sw_check_start, sw_check_end, sw_hi = 0x40;

	pfuze_chip = devm_kzalloc(&client->dev, sizeof(*pfuze_chip),
			GFP_KERNEL);
	if (!pfuze_chip)
		return -ENOMEM;

	if (client->dev.of_node) {
		match = of_match_device(of_match_ptr(pfuze_dt_ids),
				&client->dev);
		if (!match) {
			dev_err(&client->dev, "Error: No device match found\n");
			return -ENODEV;
		}
		pfuze_chip->chip_id = (int)(long)match->data;
	} else if (id) {
		pfuze_chip->chip_id = id->driver_data;
	} else {
		dev_err(&client->dev, "No dts match or id table match found\n");
		return -ENODEV;
	}

	i2c_set_clientdata(client, pfuze_chip);
	pfuze_chip->dev = &client->dev;

	pfuze_chip->regmap = devm_regmap_init_i2c(client, &pfuze_regmap_config);
	if (IS_ERR(pfuze_chip->regmap)) {
		ret = PTR_ERR(pfuze_chip->regmap);
		dev_err(&client->dev,
			"regmap allocation failed with err %d\n", ret);
		return ret;
	}

	ret = pfuze_identify(pfuze_chip);
	if (ret) {
		dev_err(&client->dev, "unrecognized pfuze chip ID!\n");
		return ret;
	}

	/* use the right regulators after identify the right device */
	switch (pfuze_chip->chip_id) {
	case PFUZE3001:
		pfuze_chip->pfuze_regulators = pfuze3001_regulators;
		regulator_num = ARRAY_SIZE(pfuze3001_regulators);
		sw_check_start = PFUZE3001_SW2;
		sw_check_end = PFUZE3001_SW2;
		sw_hi = 1 << 3;
		break;
	case PFUZE3000:
		pfuze_chip->pfuze_regulators = pfuze3000_regulators;
		regulator_num = ARRAY_SIZE(pfuze3000_regulators);
		sw_check_start = PFUZE3000_SW2;
		sw_check_end = PFUZE3000_SW2;
		sw_hi = 1 << 3;
		break;
	case PFUZE200:
		pfuze_chip->pfuze_regulators = pfuze200_regulators;
		regulator_num = ARRAY_SIZE(pfuze200_regulators);
		sw_check_start = PFUZE200_SW2;
		sw_check_end = PFUZE200_SW3B;
		break;
	case PFUZE100:
	default:
		pfuze_chip->pfuze_regulators = pfuze100_regulators;
		regulator_num = ARRAY_SIZE(pfuze100_regulators);
		sw_check_start = PFUZE100_SW2;
		sw_check_end = PFUZE100_SW4;
		break;
	}
	dev_info(&client->dev, "pfuze%s found.\n",
		(pfuze_chip->chip_id == PFUZE100) ? "100" :
		(((pfuze_chip->chip_id == PFUZE200) ? "200" :
		((pfuze_chip->chip_id == PFUZE3000) ? "3000" : "3001"))));

	memcpy(pfuze_chip->regulator_descs, pfuze_chip->pfuze_regulators,
		regulator_num * sizeof(struct pfuze_regulator));

	ret = pfuze_parse_regulators_dt(pfuze_chip);
	if (ret)
		return ret;

	for (i = 0; i < regulator_num; i++) {
		struct regulator_init_data *init_data;
		struct regulator_desc *desc;
		int val;

		desc = &pfuze_chip->regulator_descs[i].desc;

		init_data = match_init_data(i);

		/* SW2~SW4 high bit check and modify the voltage value table */
		if (i >= sw_check_start && i <= sw_check_end) {
			ret = regmap_read(pfuze_chip->regmap,
						desc->vsel_reg, &val);
			if (ret) {
				dev_err(&client->dev, "Fails to read from the register.\n");
				return ret;
			}

			if (val & sw_hi) {
				if (pfuze_chip->chip_id == PFUZE3000 ||
					pfuze_chip->chip_id == PFUZE3001) {
					desc->volt_table = pfuze3000_sw2hi;
					desc->n_voltages = ARRAY_SIZE(pfuze3000_sw2hi);
				} else {
					desc->min_uV = 800000;
					desc->uV_step = 50000;
					desc->n_voltages = 51;
				}
			}
		}

		/*
		 * Allow SW regulators to turn off. Checking it trough a flag is
		 * a workaround to keep the backward compatibility with existing
		 * old dtb's which may relay on the fact that we didn't disable
		 * the switched regulator till yet.
		 */
		if (pfuze_chip->flags & PFUZE_FLAG_DISABLE_SW) {
			if (pfuze_chip->chip_id == PFUZE100 ||
				pfuze_chip->chip_id == PFUZE200) {
				if (pfuze_chip->regulator_descs[i].sw_reg) {
					desc->ops = &pfuze100_sw_disable_regulator_ops;
					desc->enable_val = 0x8;
					desc->disable_val = 0x0;
					desc->enable_time = 500;
				}
			}
		}

		config.dev = &client->dev;
		config.init_data = init_data;
		config.driver_data = pfuze_chip;
		config.of_node = match_of_node(i);

		pfuze_chip->regulators[i] =
			devm_regulator_register(&client->dev, desc, &config);
		if (IS_ERR(pfuze_chip->regulators[i])) {
			dev_err(&client->dev, "register regulator%s failed\n",
				pfuze_chip->pfuze_regulators[i].desc.name);
			return PTR_ERR(pfuze_chip->regulators[i]);
		}
	}

	if (of_property_read_bool(client->dev.of_node,
				  "fsl,pmic-stby-poweroff"))
		return pfuze_power_off_prepare_init(pfuze_chip);

	return 0;
}

static struct i2c_driver pfuze_driver = {
	.driver = {
		.name = "pfuze100-regulator",
		.of_match_table = pfuze_dt_ids,
	},
	.probe = pfuze100_regulator_probe,
};
module_i2c_driver(pfuze_driver);

MODULE_AUTHOR("Robin Gong <b38343@freescale.com>");
MODULE_DESCRIPTION("Regulator Driver for Freescale PFUZE100/200/3000/3001 PMIC");
MODULE_LICENSE("GPL v2");
