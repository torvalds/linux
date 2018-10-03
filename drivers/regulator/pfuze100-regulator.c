/*
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/pfuze100.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#define PFUZE_NUMREGS		128
#define PFUZE100_VOL_OFFSET	0
#define PFUZE100_STANDBY_OFFSET	1
#define PFUZE100_MODE_OFFSET	3
#define PFUZE100_CONF_OFFSET	4

#define PFUZE100_DEVICEID	0x0
#define PFUZE100_REVID		0x3
#define PFUZE100_FABID		0x4

#define PFUZE100_SW1ABVOL	0x20
#define PFUZE100_SW1CVOL	0x2e
#define PFUZE100_SW2VOL		0x35
#define PFUZE100_SW3AVOL	0x3c
#define PFUZE100_SW3BVOL	0x43
#define PFUZE100_SW4VOL		0x4a
#define PFUZE100_SWBSTCON1	0x66
#define PFUZE100_VREFDDRCON	0x6a
#define PFUZE100_VSNVSVOL	0x6b
#define PFUZE100_VGEN1VOL	0x6c
#define PFUZE100_VGEN2VOL	0x6d
#define PFUZE100_VGEN3VOL	0x6e
#define PFUZE100_VGEN4VOL	0x6f
#define PFUZE100_VGEN5VOL	0x70
#define PFUZE100_VGEN6VOL	0x71

enum chips { PFUZE100, PFUZE200, PFUZE3000 = 3 };

struct pfuze_regulator {
	struct regulator_desc desc;
	unsigned char stby_reg;
	unsigned char stby_mask;
};

struct pfuze_chip {
	int	chip_id;
	struct regmap *regmap;
	struct device *dev;
	struct pfuze_regulator regulator_descs[PFUZE100_MAX_REGULATOR];
	struct regulator_dev *regulators[PFUZE100_MAX_REGULATOR];
};

static const int pfuze100_swbst[] = {
	5000000, 5050000, 5100000, 5150000,
};

static const int pfuze100_vsnvs[] = {
	1000000, 1100000, 1200000, 1300000, 1500000, 1800000, 3000000,
};

static const int pfuze3000_sw2lo[] = {
	1500000, 1550000, 1600000, 1650000, 1700000, 1750000, 1800000, 1850000,
};

static const int pfuze3000_sw2hi[] = {
	2500000, 2800000, 2850000, 3000000, 3100000, 3150000, 3200000, 3300000,
};

static const struct i2c_device_id pfuze_device_id[] = {
	{.name = "pfuze100", .driver_data = PFUZE100},
	{.name = "pfuze200", .driver_data = PFUZE200},
	{.name = "pfuze3000", .driver_data = PFUZE3000},
	{ }
};
MODULE_DEVICE_TABLE(i2c, pfuze_device_id);

static const struct of_device_id pfuze_dt_ids[] = {
	{ .compatible = "fsl,pfuze100", .data = (void *)PFUZE100},
	{ .compatible = "fsl,pfuze200", .data = (void *)PFUZE200},
	{ .compatible = "fsl,pfuze3000", .data = (void *)PFUZE3000},
	{ }
};
MODULE_DEVICE_TABLE(of, pfuze_dt_ids);

static int pfuze100_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct pfuze_chip *pfuze100 = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int ramp_bits;
	int ret;

	if (id < PFUZE100_SWBST) {
		ramp_delay = 12500 / ramp_delay;
		ramp_bits = (ramp_delay >> 1) - (ramp_delay >> 3);
		ret = regmap_update_bits(pfuze100->regmap,
					 rdev->desc->vsel_reg + 4,
					 0xc0, ramp_bits << 6);
		if (ret < 0)
			dev_err(pfuze100->dev, "ramp failed, err %d\n", ret);
	} else
		ret = -EACCES;

	return ret;
}

static struct regulator_ops pfuze100_ldo_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static struct regulator_ops pfuze100_fixed_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
};

static struct regulator_ops pfuze100_sw_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_ramp_delay = pfuze100_set_ramp_delay,
};

static struct regulator_ops pfuze100_swb_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,

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
		},	\
		.stby_reg = (base) + PFUZE100_STANDBY_OFFSET,	\
		.stby_mask = 0x3f,	\
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


#define PFUZE3000_SW2_REG(_chip, _name, base, min, max, step)	{	\
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
		.vsel_mask = 0x7,	\
	},	\
	.stby_reg = (base) + PFUZE100_STANDBY_OFFSET,	\
	.stby_mask = 0x7,	\
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
};

static struct pfuze_regulator pfuze3000_regulators[] = {
	PFUZE100_SW_REG(PFUZE3000, SW1A, PFUZE100_SW1ABVOL, 700000, 1475000, 25000),
	PFUZE100_SW_REG(PFUZE3000, SW1B, PFUZE100_SW1CVOL, 700000, 1475000, 25000),
	PFUZE100_SWB_REG(PFUZE3000, SW2, PFUZE100_SW2VOL, 0x7, pfuze3000_sw2lo),
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

static struct pfuze_regulator *pfuze_regulators;

#ifdef CONFIG_OF
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

static struct of_regulator_match *pfuze_matches;

static int pfuze_parse_regulators_dt(struct pfuze_chip *chip)
{
	struct device *dev = chip->dev;
	struct device_node *np, *parent;
	int ret;

	np = of_node_get(dev->of_node);
	if (!np)
		return -EINVAL;

	parent = of_get_child_by_name(np, "regulators");
	if (!parent) {
		dev_err(dev, "regulators node not found\n");
		return -EINVAL;
	}

	switch (chip->chip_id) {
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
#else
static int pfuze_parse_regulators_dt(struct pfuze_chip *chip)
{
	return 0;
}

static inline struct regulator_init_data *match_init_data(int index)
{
	return NULL;
}

static inline struct device_node *match_of_node(int index)
{
	return NULL;
}
#endif

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
		   (value & 0xf0) >> 4 != pfuze_chip->chip_id) {
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
	struct pfuze_regulator_platform_data *pdata =
	    dev_get_platdata(&client->dev);
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
	case PFUZE3000:
		pfuze_regulators = pfuze3000_regulators;
		regulator_num = ARRAY_SIZE(pfuze3000_regulators);
		sw_check_start = PFUZE3000_SW2;
		sw_check_end = PFUZE3000_SW2;
		sw_hi = 1 << 3;
		break;
	case PFUZE200:
		pfuze_regulators = pfuze200_regulators;
		regulator_num = ARRAY_SIZE(pfuze200_regulators);
		sw_check_start = PFUZE200_SW2;
		sw_check_end = PFUZE200_SW3B;
		break;
	case PFUZE100:
	default:
		pfuze_regulators = pfuze100_regulators;
		regulator_num = ARRAY_SIZE(pfuze100_regulators);
		sw_check_start = PFUZE100_SW2;
		sw_check_end = PFUZE100_SW4;
		break;
	}
	dev_info(&client->dev, "pfuze%s found.\n",
		(pfuze_chip->chip_id == PFUZE100) ? "100" :
		((pfuze_chip->chip_id == PFUZE200) ? "200" : "3000"));

	memcpy(pfuze_chip->regulator_descs, pfuze_regulators,
		sizeof(pfuze_chip->regulator_descs));

	ret = pfuze_parse_regulators_dt(pfuze_chip);
	if (ret)
		return ret;

	for (i = 0; i < regulator_num; i++) {
		struct regulator_init_data *init_data;
		struct regulator_desc *desc;
		int val;

		desc = &pfuze_chip->regulator_descs[i].desc;

		if (pdata)
			init_data = pdata->init_data[i];
		else
			init_data = match_init_data(i);

		/* SW2~SW4 high bit check and modify the voltage value table */
		if (i >= sw_check_start && i <= sw_check_end) {
			regmap_read(pfuze_chip->regmap, desc->vsel_reg, &val);
			if (val & sw_hi) {
				if (pfuze_chip->chip_id == PFUZE3000) {
					desc->volt_table = pfuze3000_sw2hi;
					desc->n_voltages = ARRAY_SIZE(pfuze3000_sw2hi);
				} else {
					desc->min_uV = 800000;
					desc->uV_step = 50000;
					desc->n_voltages = 51;
				}
			}
		}

		config.dev = &client->dev;
		config.init_data = init_data;
		config.driver_data = pfuze_chip;
		config.of_node = match_of_node(i);
		config.ena_gpio = -EINVAL;

		pfuze_chip->regulators[i] =
			devm_regulator_register(&client->dev, desc, &config);
		if (IS_ERR(pfuze_chip->regulators[i])) {
			dev_err(&client->dev, "register regulator%s failed\n",
				pfuze_regulators[i].desc.name);
			return PTR_ERR(pfuze_chip->regulators[i]);
		}
	}

	return 0;
}

static struct i2c_driver pfuze_driver = {
	.id_table = pfuze_device_id,
	.driver = {
		.name = "pfuze100-regulator",
		.of_match_table = pfuze_dt_ids,
	},
	.probe = pfuze100_regulator_probe,
};
module_i2c_driver(pfuze_driver);

MODULE_AUTHOR("Robin Gong <b38343@freescale.com>");
MODULE_DESCRIPTION("Regulator Driver for Freescale PFUZE100/PFUZE200 PMIC");
MODULE_LICENSE("GPL v2");
