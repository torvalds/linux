// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regulator driver for National Semiconductors LP3972 PMIC chip
 *
 * Based on lp3971.c
 */

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/lp3972.h>
#include <linux/slab.h>

struct lp3972 {
	struct device *dev;
	struct mutex io_lock;
	struct i2c_client *i2c;
};

/* LP3972 Control Registers */
#define LP3972_SCR_REG		0x07
#define LP3972_OVER1_REG	0x10
#define LP3972_OVSR1_REG	0x11
#define LP3972_OVER2_REG	0x12
#define LP3972_OVSR2_REG	0x13
#define LP3972_VCC1_REG		0x20
#define LP3972_ADTV1_REG	0x23
#define LP3972_ADTV2_REG	0x24
#define LP3972_AVRC_REG		0x25
#define LP3972_CDTC1_REG	0x26
#define LP3972_CDTC2_REG	0x27
#define LP3972_SDTV1_REG	0x29
#define LP3972_SDTV2_REG	0x2A
#define LP3972_MDTV1_REG	0x32
#define LP3972_MDTV2_REG	0x33
#define LP3972_L2VCR_REG	0x39
#define LP3972_L34VCR_REG	0x3A
#define LP3972_SCR1_REG		0x80
#define LP3972_SCR2_REG		0x81
#define LP3972_OEN3_REG		0x82
#define LP3972_OSR3_REG		0x83
#define LP3972_LOER4_REG	0x84
#define LP3972_B2TV_REG		0x85
#define LP3972_B3TV_REG		0x86
#define LP3972_B32RC_REG	0x87
#define LP3972_ISRA_REG		0x88
#define LP3972_BCCR_REG		0x89
#define LP3972_II1RR_REG	0x8E
#define LP3972_II2RR_REG	0x8F

#define LP3972_SYS_CONTROL1_REG		LP3972_SCR1_REG
/* System control register 1 initial value,
 * bits 5, 6 and 7 are EPROM programmable */
#define SYS_CONTROL1_INIT_VAL		0x02
#define SYS_CONTROL1_INIT_MASK		0x1F

#define LP3972_VOL_CHANGE_REG		LP3972_VCC1_REG
#define LP3972_VOL_CHANGE_FLAG_GO	0x01
#define LP3972_VOL_CHANGE_FLAG_MASK	0x03

/* LDO output enable mask */
#define LP3972_OEN3_L1EN	BIT(0)
#define LP3972_OVER2_LDO2_EN	BIT(2)
#define LP3972_OVER2_LDO3_EN	BIT(3)
#define LP3972_OVER2_LDO4_EN	BIT(4)
#define LP3972_OVER1_S_EN	BIT(2)

static const unsigned int ldo1_voltage_map[] = {
	1700000, 1725000, 1750000, 1775000, 1800000, 1825000, 1850000, 1875000,
	1900000, 1925000, 1950000, 1975000, 2000000,
};

static const unsigned int ldo23_voltage_map[] = {
	1800000, 1900000, 2000000, 2100000, 2200000, 2300000, 2400000, 2500000,
	2600000, 2700000, 2800000, 2900000, 3000000, 3100000, 3200000, 3300000,
};

static const unsigned int ldo4_voltage_map[] = {
	1000000, 1050000, 1100000, 1150000, 1200000, 1250000, 1300000, 1350000,
	1400000, 1500000, 1800000, 1900000, 2500000, 2800000, 3000000, 3300000,
};

static const unsigned int ldo5_voltage_map[] = {
	      0,       0,       0,       0,       0,  850000,  875000,  900000,
	 925000,  950000,  975000, 1000000, 1025000, 1050000, 1075000, 1100000,
	1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000, 1300000,
	1325000, 1350000, 1375000, 1400000, 1425000, 1450000, 1475000, 1500000,
};

static const unsigned int buck1_voltage_map[] = {
	 725000,  750000,  775000,  800000,  825000,  850000,  875000,  900000,
	 925000,  950000,  975000, 1000000, 1025000, 1050000, 1075000, 1100000,
	1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000, 1300000,
	1325000, 1350000, 1375000, 1400000, 1425000, 1450000, 1475000, 1500000,
};

static const unsigned int buck23_voltage_map[] = {
	      0,  800000,  850000,  900000,  950000, 1000000, 1050000, 1100000,
	1150000, 1200000, 1250000, 1300000, 1350000, 1400000, 1450000, 1500000,
	1550000, 1600000, 1650000, 1700000, 1800000, 1900000, 2500000, 2800000,
	3000000, 3300000,
};

static const int ldo_output_enable_mask[] = {
	LP3972_OEN3_L1EN,
	LP3972_OVER2_LDO2_EN,
	LP3972_OVER2_LDO3_EN,
	LP3972_OVER2_LDO4_EN,
	LP3972_OVER1_S_EN,
};

static const int ldo_output_enable_addr[] = {
	LP3972_OEN3_REG,
	LP3972_OVER2_REG,
	LP3972_OVER2_REG,
	LP3972_OVER2_REG,
	LP3972_OVER1_REG,
};

static const int ldo_vol_ctl_addr[] = {
	LP3972_MDTV1_REG,
	LP3972_L2VCR_REG,
	LP3972_L34VCR_REG,
	LP3972_L34VCR_REG,
	LP3972_SDTV1_REG,
};

static const int buck_vol_enable_addr[] = {
	LP3972_OVER1_REG,
	LP3972_OEN3_REG,
	LP3972_OEN3_REG,
};

static const int buck_base_addr[] = {
	LP3972_ADTV1_REG,
	LP3972_B2TV_REG,
	LP3972_B3TV_REG,
};

#define LP3972_LDO_OUTPUT_ENABLE_MASK(x) (ldo_output_enable_mask[x])
#define LP3972_LDO_OUTPUT_ENABLE_REG(x) (ldo_output_enable_addr[x])

/*	LDO voltage control registers shift:
	LP3972_LDO1 -> 0, LP3972_LDO2 -> 4
	LP3972_LDO3 -> 0, LP3972_LDO4 -> 4
	LP3972_LDO5 -> 0
*/
#define LP3972_LDO_VOL_CONTR_SHIFT(x) (((x) & 1) << 2)
#define LP3972_LDO_VOL_CONTR_REG(x) (ldo_vol_ctl_addr[x])
#define LP3972_LDO_VOL_CHANGE_SHIFT(x) ((x) ? 4 : 6)

#define LP3972_LDO_VOL_MASK(x) (((x) % 4) ? 0x0f : 0x1f)
#define LP3972_LDO_VOL_MIN_IDX(x) (((x) == 4) ? 0x05 : 0x00)
#define LP3972_LDO_VOL_MAX_IDX(x) ((x) ? (((x) == 4) ? 0x1f : 0x0f) : 0x0c)

#define LP3972_BUCK_VOL_ENABLE_REG(x) (buck_vol_enable_addr[x])
#define LP3972_BUCK_VOL1_REG(x) (buck_base_addr[x])
#define LP3972_BUCK_VOL_MASK 0x1f

static int lp3972_i2c_read(struct i2c_client *i2c, char reg, int count,
	u16 *dest)
{
	int ret;

	if (count != 1)
		return -EIO;
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret < 0)
		return ret;

	*dest = ret;
	return 0;
}

static int lp3972_i2c_write(struct i2c_client *i2c, char reg, int count,
	const u16 *src)
{
	if (count != 1)
		return -EIO;
	return i2c_smbus_write_byte_data(i2c, reg, *src);
}

static u8 lp3972_reg_read(struct lp3972 *lp3972, u8 reg)
{
	u16 val = 0;

	mutex_lock(&lp3972->io_lock);

	lp3972_i2c_read(lp3972->i2c, reg, 1, &val);

	dev_dbg(lp3972->dev, "reg read 0x%02x -> 0x%02x\n", (int)reg,
		(unsigned)val & 0xff);

	mutex_unlock(&lp3972->io_lock);

	return val & 0xff;
}

static int lp3972_set_bits(struct lp3972 *lp3972, u8 reg, u16 mask, u16 val)
{
	u16 tmp;
	int ret;

	mutex_lock(&lp3972->io_lock);

	ret = lp3972_i2c_read(lp3972->i2c, reg, 1, &tmp);
	if (ret == 0) {
		tmp = (tmp & ~mask) | val;
		ret = lp3972_i2c_write(lp3972->i2c, reg, 1, &tmp);
		dev_dbg(lp3972->dev, "reg write 0x%02x -> 0x%02x\n", (int)reg,
			(unsigned)val & 0xff);
	}
	mutex_unlock(&lp3972->io_lock);

	return ret;
}

static int lp3972_ldo_is_enabled(struct regulator_dev *dev)
{
	struct lp3972 *lp3972 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3972_LDO1;
	u16 mask = LP3972_LDO_OUTPUT_ENABLE_MASK(ldo);
	u16 val;

	val = lp3972_reg_read(lp3972, LP3972_LDO_OUTPUT_ENABLE_REG(ldo));
	return !!(val & mask);
}

static int lp3972_ldo_enable(struct regulator_dev *dev)
{
	struct lp3972 *lp3972 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3972_LDO1;
	u16 mask = LP3972_LDO_OUTPUT_ENABLE_MASK(ldo);

	return lp3972_set_bits(lp3972, LP3972_LDO_OUTPUT_ENABLE_REG(ldo),
				mask, mask);
}

static int lp3972_ldo_disable(struct regulator_dev *dev)
{
	struct lp3972 *lp3972 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3972_LDO1;
	u16 mask = LP3972_LDO_OUTPUT_ENABLE_MASK(ldo);

	return lp3972_set_bits(lp3972, LP3972_LDO_OUTPUT_ENABLE_REG(ldo),
				mask, 0);
}

static int lp3972_ldo_get_voltage_sel(struct regulator_dev *dev)
{
	struct lp3972 *lp3972 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3972_LDO1;
	u16 mask = LP3972_LDO_VOL_MASK(ldo);
	u16 val, reg;

	reg = lp3972_reg_read(lp3972, LP3972_LDO_VOL_CONTR_REG(ldo));
	val = (reg >> LP3972_LDO_VOL_CONTR_SHIFT(ldo)) & mask;

	return val;
}

static int lp3972_ldo_set_voltage_sel(struct regulator_dev *dev,
				      unsigned int selector)
{
	struct lp3972 *lp3972 = rdev_get_drvdata(dev);
	int ldo = rdev_get_id(dev) - LP3972_LDO1;
	int shift, ret;

	shift = LP3972_LDO_VOL_CONTR_SHIFT(ldo);
	ret = lp3972_set_bits(lp3972, LP3972_LDO_VOL_CONTR_REG(ldo),
		LP3972_LDO_VOL_MASK(ldo) << shift, selector << shift);

	if (ret)
		return ret;

	/*
	 * LDO1 and LDO5 support voltage control by either target voltage1
	 * or target voltage2 register.
	 * We use target voltage1 register for LDO1 and LDO5 in this driver.
	 * We need to update voltage change control register(0x20) to enable
	 * LDO1 and LDO5 to change to their programmed target values.
	 */
	switch (ldo) {
	case LP3972_LDO1:
	case LP3972_LDO5:
		shift = LP3972_LDO_VOL_CHANGE_SHIFT(ldo);
		ret = lp3972_set_bits(lp3972, LP3972_VOL_CHANGE_REG,
			LP3972_VOL_CHANGE_FLAG_MASK << shift,
			LP3972_VOL_CHANGE_FLAG_GO << shift);
		if (ret)
			return ret;

		ret = lp3972_set_bits(lp3972, LP3972_VOL_CHANGE_REG,
			LP3972_VOL_CHANGE_FLAG_MASK << shift, 0);
		break;
	}

	return ret;
}

static const struct regulator_ops lp3972_ldo_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.is_enabled = lp3972_ldo_is_enabled,
	.enable = lp3972_ldo_enable,
	.disable = lp3972_ldo_disable,
	.get_voltage_sel = lp3972_ldo_get_voltage_sel,
	.set_voltage_sel = lp3972_ldo_set_voltage_sel,
};

static int lp3972_dcdc_is_enabled(struct regulator_dev *dev)
{
	struct lp3972 *lp3972 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3972_DCDC1;
	u16 mask = 1 << (buck * 2);
	u16 val;

	val = lp3972_reg_read(lp3972, LP3972_BUCK_VOL_ENABLE_REG(buck));
	return !!(val & mask);
}

static int lp3972_dcdc_enable(struct regulator_dev *dev)
{
	struct lp3972 *lp3972 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3972_DCDC1;
	u16 mask = 1 << (buck * 2);
	u16 val;

	val = lp3972_set_bits(lp3972, LP3972_BUCK_VOL_ENABLE_REG(buck),
				mask, mask);
	return val;
}

static int lp3972_dcdc_disable(struct regulator_dev *dev)
{
	struct lp3972 *lp3972 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3972_DCDC1;
	u16 mask = 1 << (buck * 2);
	u16 val;

	val = lp3972_set_bits(lp3972, LP3972_BUCK_VOL_ENABLE_REG(buck),
				mask, 0);
	return val;
}

static int lp3972_dcdc_get_voltage_sel(struct regulator_dev *dev)
{
	struct lp3972 *lp3972 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3972_DCDC1;
	u16 reg;

	reg = lp3972_reg_read(lp3972, LP3972_BUCK_VOL1_REG(buck));
	reg &= LP3972_BUCK_VOL_MASK;

	return reg;
}

static int lp3972_dcdc_set_voltage_sel(struct regulator_dev *dev,
				       unsigned int selector)
{
	struct lp3972 *lp3972 = rdev_get_drvdata(dev);
	int buck = rdev_get_id(dev) - LP3972_DCDC1;
	int ret;

	ret = lp3972_set_bits(lp3972, LP3972_BUCK_VOL1_REG(buck),
				LP3972_BUCK_VOL_MASK, selector);
	if (ret)
		return ret;

	if (buck != 0)
		return ret;

	ret = lp3972_set_bits(lp3972, LP3972_VOL_CHANGE_REG,
		LP3972_VOL_CHANGE_FLAG_MASK, LP3972_VOL_CHANGE_FLAG_GO);
	if (ret)
		return ret;

	return lp3972_set_bits(lp3972, LP3972_VOL_CHANGE_REG,
				LP3972_VOL_CHANGE_FLAG_MASK, 0);
}

static const struct regulator_ops lp3972_dcdc_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.is_enabled = lp3972_dcdc_is_enabled,
	.enable = lp3972_dcdc_enable,
	.disable = lp3972_dcdc_disable,
	.get_voltage_sel = lp3972_dcdc_get_voltage_sel,
	.set_voltage_sel = lp3972_dcdc_set_voltage_sel,
};

static const struct regulator_desc regulators[] = {
	{
		.name = "LDO1",
		.id = LP3972_LDO1,
		.ops = &lp3972_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo1_voltage_map),
		.volt_table = ldo1_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO2",
		.id = LP3972_LDO2,
		.ops = &lp3972_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo23_voltage_map),
		.volt_table = ldo23_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO3",
		.id = LP3972_LDO3,
		.ops = &lp3972_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo23_voltage_map),
		.volt_table = ldo23_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO4",
		.id = LP3972_LDO4,
		.ops = &lp3972_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo4_voltage_map),
		.volt_table = ldo4_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO5",
		.id = LP3972_LDO5,
		.ops = &lp3972_ldo_ops,
		.n_voltages = ARRAY_SIZE(ldo5_voltage_map),
		.volt_table = ldo5_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC1",
		.id = LP3972_DCDC1,
		.ops = &lp3972_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck1_voltage_map),
		.volt_table = buck1_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC2",
		.id = LP3972_DCDC2,
		.ops = &lp3972_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck23_voltage_map),
		.volt_table = buck23_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC3",
		.id = LP3972_DCDC3,
		.ops = &lp3972_dcdc_ops,
		.n_voltages = ARRAY_SIZE(buck23_voltage_map),
		.volt_table = buck23_voltage_map,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
};

static int setup_regulators(struct lp3972 *lp3972,
	struct lp3972_platform_data *pdata)
{
	int i, err;

	/* Instantiate the regulators */
	for (i = 0; i < pdata->num_regulators; i++) {
		struct lp3972_regulator_subdev *reg = &pdata->regulators[i];
		struct regulator_config config = { };
		struct regulator_dev *rdev;

		config.dev = lp3972->dev;
		config.init_data = reg->initdata;
		config.driver_data = lp3972;

		rdev = devm_regulator_register(lp3972->dev,
					       &regulators[reg->id], &config);
		if (IS_ERR(rdev)) {
			err = PTR_ERR(rdev);
			dev_err(lp3972->dev, "regulator init failed: %d\n",
				err);
			return err;
		}
	}

	return 0;
}

static int lp3972_i2c_probe(struct i2c_client *i2c)
{
	struct lp3972 *lp3972;
	struct lp3972_platform_data *pdata = dev_get_platdata(&i2c->dev);
	int ret;
	u16 val;

	if (!pdata) {
		dev_dbg(&i2c->dev, "No platform init data supplied\n");
		return -ENODEV;
	}

	lp3972 = devm_kzalloc(&i2c->dev, sizeof(struct lp3972), GFP_KERNEL);
	if (!lp3972)
		return -ENOMEM;

	lp3972->i2c = i2c;
	lp3972->dev = &i2c->dev;

	mutex_init(&lp3972->io_lock);

	/* Detect LP3972 */
	ret = lp3972_i2c_read(i2c, LP3972_SYS_CONTROL1_REG, 1, &val);
	if (ret == 0 &&
		(val & SYS_CONTROL1_INIT_MASK) != SYS_CONTROL1_INIT_VAL) {
		ret = -ENODEV;
		dev_err(&i2c->dev, "chip reported: val = 0x%x\n", val);
	}
	if (ret < 0) {
		dev_err(&i2c->dev, "failed to detect device. ret = %d\n", ret);
		return ret;
	}

	ret = setup_regulators(lp3972, pdata);
	if (ret < 0)
		return ret;

	i2c_set_clientdata(i2c, lp3972);
	return 0;
}

static const struct i2c_device_id lp3972_i2c_id[] = {
	{ "lp3972" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp3972_i2c_id);

static struct i2c_driver lp3972_i2c_driver = {
	.driver = {
		.name = "lp3972",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = lp3972_i2c_probe,
	.id_table = lp3972_i2c_id,
};

static int __init lp3972_module_init(void)
{
	return i2c_add_driver(&lp3972_i2c_driver);
}
subsys_initcall(lp3972_module_init);

static void __exit lp3972_module_exit(void)
{
	i2c_del_driver(&lp3972_i2c_driver);
}
module_exit(lp3972_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Axel Lin <axel.lin@gmail.com>");
MODULE_DESCRIPTION("LP3972 PMIC driver");
