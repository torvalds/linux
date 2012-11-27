/*
 * Regulator driver for TPS6524x PMIC
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define REG_LDO_SET		0x0
#define LDO_ILIM_MASK		1	/* 0 = 400-800, 1 = 900-1500 */
#define LDO_VSEL_MASK		0x0f
#define LDO2_ILIM_SHIFT		12
#define LDO2_VSEL_SHIFT		4
#define LDO1_ILIM_SHIFT		8
#define LDO1_VSEL_SHIFT		0

#define REG_BLOCK_EN		0x1
#define BLOCK_MASK		1
#define BLOCK_LDO1_SHIFT	0
#define BLOCK_LDO2_SHIFT	1
#define BLOCK_LCD_SHIFT		2
#define BLOCK_USB_SHIFT		3

#define REG_DCDC_SET		0x2
#define DCDC_VDCDC_MASK		0x1f
#define DCDC_VDCDC1_SHIFT	0
#define DCDC_VDCDC2_SHIFT	5
#define DCDC_VDCDC3_SHIFT	10

#define REG_DCDC_EN		0x3
#define DCDCDCDC_EN_MASK	0x1
#define DCDCDCDC1_EN_SHIFT	0
#define DCDCDCDC1_PG_MSK	BIT(1)
#define DCDCDCDC2_EN_SHIFT	2
#define DCDCDCDC2_PG_MSK	BIT(3)
#define DCDCDCDC3_EN_SHIFT	4
#define DCDCDCDC3_PG_MSK	BIT(5)

#define REG_USB			0x4
#define USB_ILIM_SHIFT		0
#define USB_ILIM_MASK		0x3
#define USB_TSD_SHIFT		2
#define USB_TSD_MASK		0x3
#define USB_TWARN_SHIFT		4
#define USB_TWARN_MASK		0x3
#define USB_IWARN_SD		BIT(6)
#define USB_FAST_LOOP		BIT(7)

#define REG_ALARM		0x5
#define ALARM_LDO1		BIT(0)
#define ALARM_DCDC1		BIT(1)
#define ALARM_DCDC2		BIT(2)
#define ALARM_DCDC3		BIT(3)
#define ALARM_LDO2		BIT(4)
#define ALARM_USB_WARN		BIT(5)
#define ALARM_USB_ALARM		BIT(6)
#define ALARM_LCD		BIT(9)
#define ALARM_TEMP_WARM		BIT(10)
#define ALARM_TEMP_HOT		BIT(11)
#define ALARM_NRST		BIT(14)
#define ALARM_POWERUP		BIT(15)

#define REG_INT_ENABLE		0x6
#define INT_LDO1		BIT(0)
#define INT_DCDC1		BIT(1)
#define INT_DCDC2		BIT(2)
#define INT_DCDC3		BIT(3)
#define INT_LDO2		BIT(4)
#define INT_USB_WARN		BIT(5)
#define INT_USB_ALARM		BIT(6)
#define INT_LCD			BIT(9)
#define INT_TEMP_WARM		BIT(10)
#define INT_TEMP_HOT		BIT(11)
#define INT_GLOBAL_EN		BIT(15)

#define REG_INT_STATUS		0x7
#define STATUS_LDO1		BIT(0)
#define STATUS_DCDC1		BIT(1)
#define STATUS_DCDC2		BIT(2)
#define STATUS_DCDC3		BIT(3)
#define STATUS_LDO2		BIT(4)
#define STATUS_USB_WARN		BIT(5)
#define STATUS_USB_ALARM	BIT(6)
#define STATUS_LCD		BIT(9)
#define STATUS_TEMP_WARM	BIT(10)
#define STATUS_TEMP_HOT		BIT(11)

#define REG_SOFTWARE_RESET	0xb
#define REG_WRITE_ENABLE	0xd
#define REG_REV_ID		0xf

#define N_DCDC			3
#define N_LDO			2
#define N_SWITCH		2
#define N_REGULATORS		(N_DCDC + N_LDO + N_SWITCH)

#define CMD_READ(reg)		((reg) << 6)
#define CMD_WRITE(reg)		(BIT(5) | (reg) << 6)
#define STAT_CLK		BIT(3)
#define STAT_WRITE		BIT(2)
#define STAT_INVALID		BIT(1)
#define STAT_WP			BIT(0)

struct field {
	int		reg;
	int		shift;
	int		mask;
};

struct supply_info {
	const char	*name;
	int		n_voltages;
	const unsigned int *voltages;
	int		n_ilimsels;
	const unsigned int *ilimsels;
	struct field	enable, voltage, ilimsel;
};

struct tps6524x {
	struct device		*dev;
	struct spi_device	*spi;
	struct mutex		lock;
	struct regulator_desc	desc[N_REGULATORS];
	struct regulator_dev	*rdev[N_REGULATORS];
};

static int __read_reg(struct tps6524x *hw, int reg)
{
	int error = 0;
	u16 cmd = CMD_READ(reg), in;
	u8 status;
	struct spi_message m;
	struct spi_transfer t[3];

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = &cmd;
	t[0].len = 2;
	t[0].bits_per_word = 12;
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = &in;
	t[1].len = 2;
	t[1].bits_per_word = 16;
	spi_message_add_tail(&t[1], &m);

	t[2].rx_buf = &status;
	t[2].len = 1;
	t[2].bits_per_word = 4;
	spi_message_add_tail(&t[2], &m);

	error = spi_sync(hw->spi, &m);
	if (error < 0)
		return error;

	dev_dbg(hw->dev, "read reg %d, data %x, status %x\n",
		reg, in, status);

	if (!(status & STAT_CLK) || (status & STAT_WRITE))
		return -EIO;

	if (status & STAT_INVALID)
		return -EINVAL;

	return in;
}

static int read_reg(struct tps6524x *hw, int reg)
{
	int ret;

	mutex_lock(&hw->lock);
	ret = __read_reg(hw, reg);
	mutex_unlock(&hw->lock);

	return ret;
}

static int __write_reg(struct tps6524x *hw, int reg, int val)
{
	int error = 0;
	u16 cmd = CMD_WRITE(reg), out = val;
	u8 status;
	struct spi_message m;
	struct spi_transfer t[3];

	spi_message_init(&m);
	memset(t, 0, sizeof(t));

	t[0].tx_buf = &cmd;
	t[0].len = 2;
	t[0].bits_per_word = 12;
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = &out;
	t[1].len = 2;
	t[1].bits_per_word = 16;
	spi_message_add_tail(&t[1], &m);

	t[2].rx_buf = &status;
	t[2].len = 1;
	t[2].bits_per_word = 4;
	spi_message_add_tail(&t[2], &m);

	error = spi_sync(hw->spi, &m);
	if (error < 0)
		return error;

	dev_dbg(hw->dev, "wrote reg %d, data %x, status %x\n",
		reg, out, status);

	if (!(status & STAT_CLK) || !(status & STAT_WRITE))
		return -EIO;

	if (status & (STAT_INVALID | STAT_WP))
		return -EINVAL;

	return error;
}

static int __rmw_reg(struct tps6524x *hw, int reg, int mask, int val)
{
	int ret;

	ret = __read_reg(hw, reg);
	if (ret < 0)
		return ret;

	ret &= ~mask;
	ret |= val;

	ret = __write_reg(hw, reg, ret);

	return (ret < 0) ? ret : 0;
}

static int rmw_protect(struct tps6524x *hw, int reg, int mask, int val)
{
	int ret;

	mutex_lock(&hw->lock);

	ret = __write_reg(hw, REG_WRITE_ENABLE, 1);
	if (ret) {
		dev_err(hw->dev, "failed to set write enable\n");
		goto error;
	}

	ret = __rmw_reg(hw, reg, mask, val);
	if (ret)
		dev_err(hw->dev, "failed to rmw register %d\n", reg);

	ret = __write_reg(hw, REG_WRITE_ENABLE, 0);
	if (ret) {
		dev_err(hw->dev, "failed to clear write enable\n");
		goto error;
	}

error:
	mutex_unlock(&hw->lock);

	return ret;
}

static int read_field(struct tps6524x *hw, const struct field *field)
{
	int tmp;

	tmp = read_reg(hw, field->reg);
	if (tmp < 0)
		return tmp;

	return (tmp >> field->shift) & field->mask;
}

static int write_field(struct tps6524x *hw, const struct field *field,
		       int val)
{
	if (val & ~field->mask)
		return -EOVERFLOW;

	return rmw_protect(hw, field->reg,
				    field->mask << field->shift,
				    val << field->shift);
}

static const unsigned int dcdc1_voltages[] = {
	 800000,  825000,  850000,  875000,
	 900000,  925000,  950000,  975000,
	1000000, 1025000, 1050000, 1075000,
	1100000, 1125000, 1150000, 1175000,
	1200000, 1225000, 1250000, 1275000,
	1300000, 1325000, 1350000, 1375000,
	1400000, 1425000, 1450000, 1475000,
	1500000, 1525000, 1550000, 1575000,
};

static const unsigned int dcdc2_voltages[] = {
	1400000, 1450000, 1500000, 1550000,
	1600000, 1650000, 1700000, 1750000,
	1800000, 1850000, 1900000, 1950000,
	2000000, 2050000, 2100000, 2150000,
	2200000, 2250000, 2300000, 2350000,
	2400000, 2450000, 2500000, 2550000,
	2600000, 2650000, 2700000, 2750000,
	2800000, 2850000, 2900000, 2950000,
};

static const unsigned int dcdc3_voltages[] = {
	2400000, 2450000, 2500000, 2550000, 2600000,
	2650000, 2700000, 2750000, 2800000, 2850000,
	2900000, 2950000, 3000000, 3050000, 3100000,
	3150000, 3200000, 3250000, 3300000, 3350000,
	3400000, 3450000, 3500000, 3550000, 3600000,
};

static const unsigned int ldo1_voltages[] = {
	4300000, 4350000, 4400000, 4450000,
	4500000, 4550000, 4600000, 4650000,
	4700000, 4750000, 4800000, 4850000,
	4900000, 4950000, 5000000, 5050000,
};

static const unsigned int ldo2_voltages[] = {
	1100000, 1150000, 1200000, 1250000,
	1300000, 1700000, 1750000, 1800000,
	1850000, 1900000, 3150000, 3200000,
	3250000, 3300000, 3350000, 3400000,
};

static const unsigned int fixed_5000000_voltage[] = {
	5000000
};

static const unsigned int ldo_ilimsel[] = {
	400000, 1500000
};

static const unsigned int usb_ilimsel[] = {
	200000, 400000, 800000, 1000000
};

static const unsigned int fixed_2400000_ilimsel[] = {
	2400000
};

static const unsigned int fixed_1200000_ilimsel[] = {
	1200000
};

static const unsigned int fixed_400000_ilimsel[] = {
	400000
};

#define __MK_FIELD(_reg, _mask, _shift) \
	{ .reg = (_reg), .mask = (_mask), .shift = (_shift), }

static const struct supply_info supply_info[N_REGULATORS] = {
	{
		.name		= "DCDC1",
		.n_voltages	= ARRAY_SIZE(dcdc1_voltages),
		.voltages	= dcdc1_voltages,
		.n_ilimsels	= ARRAY_SIZE(fixed_2400000_ilimsel),
		.ilimsels	= fixed_2400000_ilimsel,
		.enable		= __MK_FIELD(REG_DCDC_EN, DCDCDCDC_EN_MASK,
					     DCDCDCDC1_EN_SHIFT),
		.voltage	= __MK_FIELD(REG_DCDC_SET, DCDC_VDCDC_MASK,
					     DCDC_VDCDC1_SHIFT),
	},
	{
		.name		= "DCDC2",
		.n_voltages	= ARRAY_SIZE(dcdc2_voltages),
		.voltages	= dcdc2_voltages,
		.n_ilimsels	= ARRAY_SIZE(fixed_1200000_ilimsel),
		.ilimsels	= fixed_1200000_ilimsel,
		.enable		= __MK_FIELD(REG_DCDC_EN, DCDCDCDC_EN_MASK,
					     DCDCDCDC2_EN_SHIFT),
		.voltage	= __MK_FIELD(REG_DCDC_SET, DCDC_VDCDC_MASK,
					     DCDC_VDCDC2_SHIFT),
	},
	{
		.name		= "DCDC3",
		.n_voltages	= ARRAY_SIZE(dcdc3_voltages),
		.voltages	= dcdc3_voltages,
		.n_ilimsels	= ARRAY_SIZE(fixed_1200000_ilimsel),
		.ilimsels	= fixed_1200000_ilimsel,
		.enable		= __MK_FIELD(REG_DCDC_EN, DCDCDCDC_EN_MASK,
					DCDCDCDC3_EN_SHIFT),
		.voltage	= __MK_FIELD(REG_DCDC_SET, DCDC_VDCDC_MASK,
					     DCDC_VDCDC3_SHIFT),
	},
	{
		.name		= "LDO1",
		.n_voltages	= ARRAY_SIZE(ldo1_voltages),
		.voltages	= ldo1_voltages,
		.n_ilimsels	= ARRAY_SIZE(ldo_ilimsel),
		.ilimsels	= ldo_ilimsel,
		.enable		= __MK_FIELD(REG_BLOCK_EN, BLOCK_MASK,
					     BLOCK_LDO1_SHIFT),
		.voltage	= __MK_FIELD(REG_LDO_SET, LDO_VSEL_MASK,
					     LDO1_VSEL_SHIFT),
		.ilimsel	= __MK_FIELD(REG_LDO_SET, LDO_ILIM_MASK,
					     LDO1_ILIM_SHIFT),
	},
	{
		.name		= "LDO2",
		.n_voltages	= ARRAY_SIZE(ldo2_voltages),
		.voltages	= ldo2_voltages,
		.n_ilimsels	= ARRAY_SIZE(ldo_ilimsel),
		.ilimsels	= ldo_ilimsel,
		.enable		= __MK_FIELD(REG_BLOCK_EN, BLOCK_MASK,
					     BLOCK_LDO2_SHIFT),
		.voltage	= __MK_FIELD(REG_LDO_SET, LDO_VSEL_MASK,
					     LDO2_VSEL_SHIFT),
		.ilimsel	= __MK_FIELD(REG_LDO_SET, LDO_ILIM_MASK,
					     LDO2_ILIM_SHIFT),
	},
	{
		.name		= "USB",
		.n_voltages	= ARRAY_SIZE(fixed_5000000_voltage),
		.voltages	= fixed_5000000_voltage,
		.n_ilimsels	= ARRAY_SIZE(usb_ilimsel),
		.ilimsels	= usb_ilimsel,
		.enable		= __MK_FIELD(REG_BLOCK_EN, BLOCK_MASK,
					     BLOCK_USB_SHIFT),
		.ilimsel	= __MK_FIELD(REG_USB, USB_ILIM_MASK,
					     USB_ILIM_SHIFT),
	},
	{
		.name		= "LCD",
		.n_voltages	= ARRAY_SIZE(fixed_5000000_voltage),
		.voltages	= fixed_5000000_voltage,
		.n_ilimsels	= ARRAY_SIZE(fixed_400000_ilimsel),
		.ilimsels	= fixed_400000_ilimsel,
		.enable		= __MK_FIELD(REG_BLOCK_EN, BLOCK_MASK,
					     BLOCK_LCD_SHIFT),
	},
};

static int set_voltage_sel(struct regulator_dev *rdev, unsigned selector)
{
	const struct supply_info *info;
	struct tps6524x *hw;

	hw	= rdev_get_drvdata(rdev);
	info	= &supply_info[rdev_get_id(rdev)];

	if (rdev->desc->n_voltages == 1)
		return -EINVAL;

	return write_field(hw, &info->voltage, selector);
}

static int get_voltage_sel(struct regulator_dev *rdev)
{
	const struct supply_info *info;
	struct tps6524x *hw;
	int ret;

	hw	= rdev_get_drvdata(rdev);
	info	= &supply_info[rdev_get_id(rdev)];

	if (rdev->desc->n_voltages == 1)
		return 0;

	ret = read_field(hw, &info->voltage);
	if (ret < 0)
		return ret;
	if (WARN_ON(ret >= info->n_voltages))
		return -EIO;

	return ret;
}

static int set_current_limit(struct regulator_dev *rdev, int min_uA,
			     int max_uA)
{
	const struct supply_info *info;
	struct tps6524x *hw;
	int i;

	hw	= rdev_get_drvdata(rdev);
	info	= &supply_info[rdev_get_id(rdev)];

	if (info->n_ilimsels == 1)
		return -EINVAL;

	for (i = info->n_ilimsels - 1; i >= 0; i--) {
		if (min_uA <= info->ilimsels[i] &&
		    max_uA >= info->ilimsels[i])
			return write_field(hw, &info->ilimsel, i);
	}

	return -EINVAL;
}

static int get_current_limit(struct regulator_dev *rdev)
{
	const struct supply_info *info;
	struct tps6524x *hw;
	int ret;

	hw	= rdev_get_drvdata(rdev);
	info	= &supply_info[rdev_get_id(rdev)];

	if (info->n_ilimsels == 1)
		return info->ilimsels[0];

	ret = read_field(hw, &info->ilimsel);
	if (ret < 0)
		return ret;
	if (WARN_ON(ret >= info->n_ilimsels))
		return -EIO;

	return info->ilimsels[ret];
}

static int enable_supply(struct regulator_dev *rdev)
{
	const struct supply_info *info;
	struct tps6524x *hw;

	hw	= rdev_get_drvdata(rdev);
	info	= &supply_info[rdev_get_id(rdev)];

	return write_field(hw, &info->enable, 1);
}

static int disable_supply(struct regulator_dev *rdev)
{
	const struct supply_info *info;
	struct tps6524x *hw;

	hw	= rdev_get_drvdata(rdev);
	info	= &supply_info[rdev_get_id(rdev)];

	return write_field(hw, &info->enable, 0);
}

static int is_supply_enabled(struct regulator_dev *rdev)
{
	const struct supply_info *info;
	struct tps6524x *hw;

	hw	= rdev_get_drvdata(rdev);
	info	= &supply_info[rdev_get_id(rdev)];

	return read_field(hw, &info->enable);
}

static struct regulator_ops regulator_ops = {
	.is_enabled		= is_supply_enabled,
	.enable			= enable_supply,
	.disable		= disable_supply,
	.get_voltage_sel	= get_voltage_sel,
	.set_voltage_sel	= set_voltage_sel,
	.list_voltage		= regulator_list_voltage_table,
	.set_current_limit	= set_current_limit,
	.get_current_limit	= get_current_limit,
};

static int pmic_remove(struct spi_device *spi)
{
	struct tps6524x *hw = spi_get_drvdata(spi);
	int i;

	if (!hw)
		return 0;
	for (i = 0; i < N_REGULATORS; i++) {
		if (hw->rdev[i])
			regulator_unregister(hw->rdev[i]);
		hw->rdev[i] = NULL;
	}
	spi_set_drvdata(spi, NULL);
	return 0;
}

static int pmic_probe(struct spi_device *spi)
{
	struct tps6524x *hw;
	struct device *dev = &spi->dev;
	const struct supply_info *info = supply_info;
	struct regulator_init_data *init_data;
	struct regulator_config config = { };
	int ret = 0, i;

	init_data = dev->platform_data;
	if (!init_data) {
		dev_err(dev, "could not find regulator platform data\n");
		return -EINVAL;
	}

	hw = devm_kzalloc(&spi->dev, sizeof(struct tps6524x), GFP_KERNEL);
	if (!hw) {
		dev_err(dev, "cannot allocate regulator private data\n");
		return -ENOMEM;
	}
	spi_set_drvdata(spi, hw);

	memset(hw, 0, sizeof(struct tps6524x));
	hw->dev = dev;
	hw->spi = spi_dev_get(spi);
	mutex_init(&hw->lock);

	for (i = 0; i < N_REGULATORS; i++, info++, init_data++) {
		hw->desc[i].name	= info->name;
		hw->desc[i].id		= i;
		hw->desc[i].n_voltages	= info->n_voltages;
		hw->desc[i].volt_table	= info->voltages;
		hw->desc[i].ops		= &regulator_ops;
		hw->desc[i].type	= REGULATOR_VOLTAGE;
		hw->desc[i].owner	= THIS_MODULE;

		config.dev = dev;
		config.init_data = init_data;
		config.driver_data = hw;

		hw->rdev[i] = regulator_register(&hw->desc[i], &config);
		if (IS_ERR(hw->rdev[i])) {
			ret = PTR_ERR(hw->rdev[i]);
			hw->rdev[i] = NULL;
			goto fail;
		}
	}

	return 0;

fail:
	pmic_remove(spi);
	return ret;
}

static struct spi_driver pmic_driver = {
	.probe		= pmic_probe,
	.remove		= pmic_remove,
	.driver		= {
		.name	= "tps6524x",
		.owner	= THIS_MODULE,
	},
};

module_spi_driver(pmic_driver);

MODULE_DESCRIPTION("TPS6524X PMIC Driver");
MODULE_AUTHOR("Cyril Chemparathy");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:tps6524x");
