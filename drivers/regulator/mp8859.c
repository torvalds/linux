// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 five technologies GmbH
// Author: Markus Reichl <m.reichl@fivetechno.de>

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/regulator/driver.h>
#include <linux/regmap.h>


#define VOL_MIN_IDX			0x00
#define VOL_MAX_IDX			0x7ff

/* Register definitions */
#define MP8859_VOUT_L_REG		0    //3 lo Bits
#define MP8859_VOUT_H_REG		1    //8 hi Bits
#define MP8859_VOUT_GO_REG		2
#define MP8859_IOUT_LIM_REG		3
#define MP8859_CTL1_REG			4
#define MP8859_CTL2_REG			5
#define MP8859_RESERVED1_REG		6
#define MP8859_RESERVED2_REG		7
#define MP8859_RESERVED3_REG		8
#define MP8859_STATUS_REG		9
#define MP8859_INTERRUPT_REG		0x0A
#define MP8859_MASK_REG			0x0B
#define MP8859_ID1_REG			0x0C
#define MP8859_MFR_ID_REG		0x27
#define MP8859_DEV_ID_REG		0x28
#define MP8859_IC_REV_REG		0x29

#define MP8859_MAX_REG			0x29

#define MP8859_GO_BIT			0x01


static int mp8859_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	int ret;

	ret = regmap_write(rdev->regmap, MP8859_VOUT_L_REG, sel & 0x7);

	if (ret)
		return ret;
	ret = regmap_write(rdev->regmap, MP8859_VOUT_H_REG, sel >> 3);

	if (ret)
		return ret;
	ret = regmap_update_bits(rdev->regmap, MP8859_VOUT_GO_REG,
					MP8859_GO_BIT, 1);
	return ret;
}

static int mp8859_get_voltage_sel(struct regulator_dev *rdev)
{
	unsigned int val_tmp;
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, MP8859_VOUT_H_REG, &val_tmp);

	if (ret)
		return ret;
	val = val_tmp << 3;

	ret = regmap_read(rdev->regmap, MP8859_VOUT_L_REG, &val_tmp);

	if (ret)
		return ret;
	val |= val_tmp & 0x07;
	return val;
}

static const struct linear_range mp8859_dcdc_ranges[] = {
	REGULATOR_LINEAR_RANGE(0, VOL_MIN_IDX, VOL_MAX_IDX, 10000),
};

static const struct regmap_config mp8859_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MP8859_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
};

static const struct regulator_ops mp8859_ops = {
	.set_voltage_sel = mp8859_set_voltage_sel,
	.get_voltage_sel = mp8859_get_voltage_sel,
	.list_voltage = regulator_list_voltage_linear_range,
};

static const struct regulator_desc mp8859_regulators[] = {
	{
		.id = 0,
		.type = REGULATOR_VOLTAGE,
		.name = "mp8859_dcdc",
		.supply_name = "vin",
		.of_match = of_match_ptr("mp8859_dcdc"),
		.n_voltages = VOL_MAX_IDX + 1,
		.linear_ranges = mp8859_dcdc_ranges,
		.n_linear_ranges = 1,
		.ops = &mp8859_ops,
		.owner = THIS_MODULE,
	},
};

static int mp8859_i2c_probe(struct i2c_client *i2c)
{
	int ret;
	struct regulator_config config = {.dev = &i2c->dev};
	struct regmap *regmap = devm_regmap_init_i2c(i2c, &mp8859_regmap);
	struct regulator_dev *rdev;

	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}
	rdev = devm_regulator_register(&i2c->dev, &mp8859_regulators[0],
					&config);

	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(&i2c->dev, "failed to register %s: %d\n",
			mp8859_regulators[0].name, ret);
		return ret;
	}
	return 0;
}

static const struct of_device_id mp8859_dt_id[] = {
	{.compatible =  "mps,mp8859"},
	{},
};
MODULE_DEVICE_TABLE(of, mp8859_dt_id);

static const struct i2c_device_id mp8859_i2c_id[] = {
	{ "mp8859", },
	{  },
};
MODULE_DEVICE_TABLE(i2c, mp8859_i2c_id);

static struct i2c_driver mp8859_regulator_driver = {
	.driver = {
		.name = "mp8859",
		.of_match_table = of_match_ptr(mp8859_dt_id),
	},
	.probe_new = mp8859_i2c_probe,
	.id_table = mp8859_i2c_id,
};

module_i2c_driver(mp8859_regulator_driver);

MODULE_DESCRIPTION("Monolithic Power Systems MP8859 voltage regulator driver");
MODULE_AUTHOR("Markus Reichl <m.reichl@fivetechno.de>");
MODULE_LICENSE("GPL v2");
