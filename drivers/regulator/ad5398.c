/*
 * Voltage and current regulation for AD5398 and AD5821
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define AD5398_CURRENT_EN_MASK	0x8000

struct ad5398_chip_info {
	struct i2c_client *client;
	int min_uA;
	int max_uA;
	unsigned int current_level;
	unsigned int current_mask;
	unsigned int current_offset;
	struct regulator_dev *rdev;
};

static int ad5398_calc_current(struct ad5398_chip_info *chip,
	unsigned selector)
{
	unsigned range_uA = chip->max_uA - chip->min_uA;

	return chip->min_uA + (selector * range_uA / chip->current_level);
}

static int ad5398_read_reg(struct i2c_client *client, unsigned short *data)
{
	unsigned short val;
	int ret;

	ret = i2c_master_recv(client, (char *)&val, 2);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read error\n");
		return ret;
	}
	*data = be16_to_cpu(val);

	return ret;
}

static int ad5398_write_reg(struct i2c_client *client, const unsigned short data)
{
	unsigned short val;
	int ret;

	val = cpu_to_be16(data);
	ret = i2c_master_send(client, (char *)&val, 2);
	if (ret != 2) {
		dev_err(&client->dev, "I2C write error\n");
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int ad5398_get_current_limit(struct regulator_dev *rdev)
{
	struct ad5398_chip_info *chip = rdev_get_drvdata(rdev);
	struct i2c_client *client = chip->client;
	unsigned short data;
	int ret;

	ret = ad5398_read_reg(client, &data);
	if (ret < 0)
		return ret;

	ret = (data & chip->current_mask) >> chip->current_offset;

	return ad5398_calc_current(chip, ret);
}

static int ad5398_set_current_limit(struct regulator_dev *rdev, int min_uA, int max_uA)
{
	struct ad5398_chip_info *chip = rdev_get_drvdata(rdev);
	struct i2c_client *client = chip->client;
	unsigned range_uA = chip->max_uA - chip->min_uA;
	unsigned selector;
	unsigned short data;
	int ret;

	if (min_uA < chip->min_uA)
		min_uA = chip->min_uA;
	if (max_uA > chip->max_uA)
		max_uA = chip->max_uA;

	if (min_uA > chip->max_uA || max_uA < chip->min_uA)
		return -EINVAL;

	selector = DIV_ROUND_UP((min_uA - chip->min_uA) * chip->current_level,
				range_uA);
	if (ad5398_calc_current(chip, selector) > max_uA)
		return -EINVAL;

	dev_dbg(&client->dev, "changing current %duA\n",
		ad5398_calc_current(chip, selector));

	/* read chip enable bit */
	ret = ad5398_read_reg(client, &data);
	if (ret < 0)
		return ret;

	/* prepare register data */
	selector = (selector << chip->current_offset) & chip->current_mask;
	data = (unsigned short)selector | (data & AD5398_CURRENT_EN_MASK);

	/* write the new current value back as well as enable bit */
	ret = ad5398_write_reg(client, data);

	return ret;
}

static int ad5398_is_enabled(struct regulator_dev *rdev)
{
	struct ad5398_chip_info *chip = rdev_get_drvdata(rdev);
	struct i2c_client *client = chip->client;
	unsigned short data;
	int ret;

	ret = ad5398_read_reg(client, &data);
	if (ret < 0)
		return ret;

	if (data & AD5398_CURRENT_EN_MASK)
		return 1;
	else
		return 0;
}

static int ad5398_enable(struct regulator_dev *rdev)
{
	struct ad5398_chip_info *chip = rdev_get_drvdata(rdev);
	struct i2c_client *client = chip->client;
	unsigned short data;
	int ret;

	ret = ad5398_read_reg(client, &data);
	if (ret < 0)
		return ret;

	if (data & AD5398_CURRENT_EN_MASK)
		return 0;

	data |= AD5398_CURRENT_EN_MASK;

	ret = ad5398_write_reg(client, data);

	return ret;
}

static int ad5398_disable(struct regulator_dev *rdev)
{
	struct ad5398_chip_info *chip = rdev_get_drvdata(rdev);
	struct i2c_client *client = chip->client;
	unsigned short data;
	int ret;

	ret = ad5398_read_reg(client, &data);
	if (ret < 0)
		return ret;

	if (!(data & AD5398_CURRENT_EN_MASK))
		return 0;

	data &= ~AD5398_CURRENT_EN_MASK;

	ret = ad5398_write_reg(client, data);

	return ret;
}

static const struct regulator_ops ad5398_ops = {
	.get_current_limit = ad5398_get_current_limit,
	.set_current_limit = ad5398_set_current_limit,
	.enable = ad5398_enable,
	.disable = ad5398_disable,
	.is_enabled = ad5398_is_enabled,
};

static const struct regulator_desc ad5398_reg = {
	.name = "isink",
	.id = 0,
	.ops = &ad5398_ops,
	.type = REGULATOR_CURRENT,
	.owner = THIS_MODULE,
};

struct ad5398_current_data_format {
	int current_bits;
	int current_offset;
	int min_uA;
	int max_uA;
};

static const struct ad5398_current_data_format df_10_4_120 = {10, 4, 0, 120000};

static const struct i2c_device_id ad5398_id[] = {
	{ "ad5398", (kernel_ulong_t)&df_10_4_120 },
	{ "ad5821", (kernel_ulong_t)&df_10_4_120 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad5398_id);

static int ad5398_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct regulator_init_data *init_data = dev_get_platdata(&client->dev);
	struct regulator_config config = { };
	struct ad5398_chip_info *chip;
	const struct ad5398_current_data_format *df =
			(struct ad5398_current_data_format *)id->driver_data;

	if (!init_data)
		return -EINVAL;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	config.dev = &client->dev;
	config.init_data = init_data;
	config.driver_data = chip;

	chip->client = client;

	chip->min_uA = df->min_uA;
	chip->max_uA = df->max_uA;
	chip->current_level = 1 << df->current_bits;
	chip->current_offset = df->current_offset;
	chip->current_mask = (chip->current_level - 1) << chip->current_offset;

	chip->rdev = devm_regulator_register(&client->dev, &ad5398_reg,
					     &config);
	if (IS_ERR(chip->rdev)) {
		dev_err(&client->dev, "failed to register %s %s\n",
			id->name, ad5398_reg.name);
		return PTR_ERR(chip->rdev);
	}

	i2c_set_clientdata(client, chip);
	dev_dbg(&client->dev, "%s regulator driver is registered.\n", id->name);
	return 0;
}

static struct i2c_driver ad5398_driver = {
	.probe = ad5398_probe,
	.driver		= {
		.name	= "ad5398",
	},
	.id_table	= ad5398_id,
};

static int __init ad5398_init(void)
{
	return i2c_add_driver(&ad5398_driver);
}
subsys_initcall(ad5398_init);

static void __exit ad5398_exit(void)
{
	i2c_del_driver(&ad5398_driver);
}
module_exit(ad5398_exit);

MODULE_DESCRIPTION("AD5398 and AD5821 current regulator driver");
MODULE_AUTHOR("Sonic Zhang");
MODULE_LICENSE("GPL");
