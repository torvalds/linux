// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Axis Communications AB
 *
 * Datasheet: https://www.ti.com/lit/gpn/opt4001
 *
 * Device driver for the Texas Instruments OPT4001.
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

/* OPT4001 register set */
#define OPT4001_LIGHT1_MSB    0x00
#define OPT4001_LIGHT1_LSB    0x01
#define OPT4001_CTRL          0x0A
#define OPT4001_DEVICE_ID     0x11

/* OPT4001 register mask */
#define OPT4001_EXPONENT_MASK    GENMASK(15, 12)
#define OPT4001_MSB_MASK         GENMASK(11, 0)
#define OPT4001_LSB_MASK         GENMASK(15, 8)
#define OPT4001_COUNTER_MASK     GENMASK(7, 4)
#define OPT4001_CRC_MASK         GENMASK(3, 0)

/* OPT4001 device id mask */
#define OPT4001_DEVICE_ID_MASK   GENMASK(11, 0)

/* OPT4001 control registers mask */
#define OPT4001_CTRL_QWAKE_MASK          GENMASK(15, 15)
#define OPT4001_CTRL_RANGE_MASK          GENMASK(13, 10)
#define OPT4001_CTRL_CONV_TIME_MASK      GENMASK(9, 6)
#define OPT4001_CTRL_OPER_MODE_MASK      GENMASK(5, 4)
#define OPT4001_CTRL_LATCH_MASK          GENMASK(3, 3)
#define OPT4001_CTRL_INT_POL_MASK        GENMASK(2, 2)
#define OPT4001_CTRL_FAULT_COUNT         GENMASK(0, 1)

/* OPT4001 constants */
#define OPT4001_DEVICE_ID_VAL            0x121

/* OPT4001 operating modes */
#define OPT4001_CTRL_OPER_MODE_OFF        0x0
#define OPT4001_CTRL_OPER_MODE_FORCED     0x1
#define OPT4001_CTRL_OPER_MODE_ONE_SHOT   0x2
#define OPT4001_CTRL_OPER_MODE_CONTINUOUS 0x3

/* OPT4001 conversion control register definitions */
#define OPT4001_CTRL_CONVERSION_0_6MS   0x0
#define OPT4001_CTRL_CONVERSION_1MS     0x1
#define OPT4001_CTRL_CONVERSION_1_8MS   0x2
#define OPT4001_CTRL_CONVERSION_3_4MS   0x3
#define OPT4001_CTRL_CONVERSION_6_5MS   0x4
#define OPT4001_CTRL_CONVERSION_12_7MS  0x5
#define OPT4001_CTRL_CONVERSION_25MS    0x6
#define OPT4001_CTRL_CONVERSION_50MS    0x7
#define OPT4001_CTRL_CONVERSION_100MS   0x8
#define OPT4001_CTRL_CONVERSION_200MS   0x9
#define OPT4001_CTRL_CONVERSION_400MS   0xa
#define OPT4001_CTRL_CONVERSION_800MS   0xb

/* OPT4001 scale light level range definitions */
#define OPT4001_CTRL_LIGHT_SCALE_AUTO   12

/* OPT4001 default values */
#define OPT4001_DEFAULT_CONVERSION_TIME OPT4001_CTRL_CONVERSION_800MS

/*
 * The different packaging of OPT4001 has different constants used when calculating
 * lux values.
 */
struct opt4001_chip_info {
	int mul;
	int div;
	const char *name;
};

struct opt4001_chip {
	struct regmap *regmap;
	struct i2c_client *client;
	u8 int_time;
	const struct opt4001_chip_info *chip_info;
};

static const struct opt4001_chip_info opt4001_sot_5x3_info = {
	.mul = 4375,
	.div = 10000000,
	.name = "opt4001-sot-5x3"
};

static const struct opt4001_chip_info opt4001_picostar_info = {
	.mul = 3125,
	.div = 10000000,
	.name = "opt4001-picostar"
};

static const int opt4001_int_time_available[][2] = {
	{ 0,    600 },
	{ 0,   1000 },
	{ 0,   1800 },
	{ 0,   3400 },
	{ 0,   6500 },
	{ 0,  12700 },
	{ 0,  25000 },
	{ 0,  50000 },
	{ 0, 100000 },
	{ 0, 200000 },
	{ 0, 400000 },
	{ 0, 800000 },
};

/*
 * Conversion time is integration time + time to set register
 * this is used as integration time.
 */
static const int opt4001_int_time_reg[][2] = {
	{    600,  OPT4001_CTRL_CONVERSION_0_6MS  },
	{   1000,  OPT4001_CTRL_CONVERSION_1MS    },
	{   1800,  OPT4001_CTRL_CONVERSION_1_8MS  },
	{   3400,  OPT4001_CTRL_CONVERSION_3_4MS  },
	{   6500,  OPT4001_CTRL_CONVERSION_6_5MS  },
	{  12700,  OPT4001_CTRL_CONVERSION_12_7MS },
	{  25000,  OPT4001_CTRL_CONVERSION_25MS   },
	{  50000,  OPT4001_CTRL_CONVERSION_50MS   },
	{ 100000,  OPT4001_CTRL_CONVERSION_100MS  },
	{ 200000,  OPT4001_CTRL_CONVERSION_200MS  },
	{ 400000,  OPT4001_CTRL_CONVERSION_400MS  },
	{ 800000,  OPT4001_CTRL_CONVERSION_800MS  },
};

static int opt4001_als_time_to_index(const u32 als_integration_time)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(opt4001_int_time_available); i++) {
		if (als_integration_time == opt4001_int_time_available[i][1])
			return i;
	}

	return -EINVAL;
}

static u8 opt4001_calculate_crc(u8 exp, u32 mantissa, u8 count)
{
	u8 crc;

	crc = (hweight32(mantissa) + hweight32(exp) + hweight32(count)) % 2;
	crc |= ((hweight32(mantissa & 0xAAAAA) + hweight32(exp & 0xA)
		 + hweight32(count & 0xA)) % 2) << 1;
	crc |= ((hweight32(mantissa & 0x88888) + hweight32(exp & 0x8)
		 + hweight32(count & 0x8)) % 2) << 2;
	crc |= (hweight32(mantissa & 0x80808) % 2) << 3;

	return crc;
}

static int opt4001_read_lux_value(struct iio_dev *indio_dev,
				  int *val, int *val2)
{
	struct opt4001_chip *chip = iio_priv(indio_dev);
	struct device *dev = &chip->client->dev;
	unsigned int light1;
	unsigned int light2;
	u16 msb;
	u16 lsb;
	u8 exp;
	u8 count;
	u8 crc;
	u8 calc_crc;
	u64 lux_raw;
	int ret;

	ret = regmap_read(chip->regmap, OPT4001_LIGHT1_MSB, &light1);
	if (ret < 0) {
		dev_err(dev, "Failed to read data bytes");
		return ret;
	}

	ret = regmap_read(chip->regmap, OPT4001_LIGHT1_LSB, &light2);
	if (ret < 0) {
		dev_err(dev, "Failed to read data bytes");
		return ret;
	}

	count = FIELD_GET(OPT4001_COUNTER_MASK, light2);
	exp = FIELD_GET(OPT4001_EXPONENT_MASK, light1);
	crc = FIELD_GET(OPT4001_CRC_MASK, light2);
	msb = FIELD_GET(OPT4001_MSB_MASK, light1);
	lsb = FIELD_GET(OPT4001_LSB_MASK, light2);
	lux_raw = (msb << 8) + lsb;
	calc_crc = opt4001_calculate_crc(exp, lux_raw, count);
	if (calc_crc != crc)
		return -EIO;

	lux_raw = lux_raw << exp;
	lux_raw = lux_raw * chip->chip_info->mul;
	*val = div_u64_rem(lux_raw, chip->chip_info->div, val2);
	*val2 = *val2 * 100;

	return IIO_VAL_INT_PLUS_NANO;
}

static int opt4001_set_conf(struct opt4001_chip *chip)
{
	struct device *dev = &chip->client->dev;
	u16 reg;
	int ret;

	reg = FIELD_PREP(OPT4001_CTRL_RANGE_MASK, OPT4001_CTRL_LIGHT_SCALE_AUTO);
	reg |= FIELD_PREP(OPT4001_CTRL_CONV_TIME_MASK, chip->int_time);
	reg |= FIELD_PREP(OPT4001_CTRL_OPER_MODE_MASK, OPT4001_CTRL_OPER_MODE_CONTINUOUS);

	ret = regmap_write(chip->regmap, OPT4001_CTRL, reg);
	if (ret)
		dev_err(dev, "Failed to set configuration\n");

	return ret;
}

static int opt4001_power_down(struct opt4001_chip *chip)
{
	struct device *dev = &chip->client->dev;
	int ret;
	unsigned int reg;

	ret = regmap_read(chip->regmap, OPT4001_DEVICE_ID, &reg);
	if (ret) {
		dev_err(dev, "Failed to read configuration\n");
		return ret;
	}

	/* MODE_OFF is 0x0 so just set bits to 0 */
	reg &= ~OPT4001_CTRL_OPER_MODE_MASK;

	ret = regmap_write(chip->regmap, OPT4001_CTRL, reg);
	if (ret)
		dev_err(dev, "Failed to set configuration to power down\n");

	return ret;
}

static void opt4001_chip_off_action(void *data)
{
	struct opt4001_chip *chip = data;

	opt4001_power_down(chip);
}

static const struct iio_chan_spec opt4001_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME)
	},
};

static int opt4001_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct opt4001_chip *chip = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		return opt4001_read_lux_value(indio_dev, val, val2);
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = opt4001_int_time_reg[chip->int_time][0];
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int opt4001_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct opt4001_chip *chip = iio_priv(indio_dev);
	int int_time;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		int_time = opt4001_als_time_to_index(val2);
		if (int_time < 0)
			return int_time;
		chip->int_time = int_time;
		return opt4001_set_conf(chip);
	default:
		return -EINVAL;
	}
}

static int opt4001_read_available(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  const int **vals, int *type, int *length,
				  long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		*length = ARRAY_SIZE(opt4001_int_time_available) * 2;
		*vals = (const int *)opt4001_int_time_available;
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;

	default:
		return -EINVAL;
	}
}

static const struct iio_info opt4001_info_no_irq = {
	.read_raw = opt4001_read_raw,
	.write_raw = opt4001_write_raw,
	.read_avail = opt4001_read_available,
};

static int opt4001_load_defaults(struct opt4001_chip *chip)
{
	chip->int_time = OPT4001_DEFAULT_CONVERSION_TIME;

	return opt4001_set_conf(chip);
}

static bool opt4001_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case OPT4001_LIGHT1_MSB:
	case OPT4001_LIGHT1_LSB:
	case OPT4001_CTRL:
	case OPT4001_DEVICE_ID:
		return true;
	default:
		return false;
	}
}

static bool opt4001_writable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case OPT4001_CTRL:
		return true;
	default:
		return false;
	}
}

static bool opt4001_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case OPT4001_LIGHT1_MSB:
	case OPT4001_LIGHT1_LSB:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config opt4001_regmap_config = {
	.name = "opt4001",
	.reg_bits = 8,
	.val_bits = 16,
	.cache_type = REGCACHE_RBTREE,
	.max_register = OPT4001_DEVICE_ID,
	.readable_reg = opt4001_readable_reg,
	.writeable_reg = opt4001_writable_reg,
	.volatile_reg = opt4001_volatile_reg,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static int opt4001_probe(struct i2c_client *client)
{
	struct opt4001_chip *chip;
	struct iio_dev *indio_dev;
	int ret;
	uint dev_id;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);

	ret = devm_regulator_get_enable(&client->dev, "vdd");
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to enable vdd supply\n");

	chip->regmap = devm_regmap_init_i2c(client, &opt4001_regmap_config);
	if (IS_ERR(chip->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(chip->regmap),
				     "regmap initialization failed\n");
	chip->client = client;

	indio_dev->info = &opt4001_info_no_irq;

	ret = regmap_reinit_cache(chip->regmap, &opt4001_regmap_config);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to reinit regmap cache\n");

	ret = regmap_read(chip->regmap, OPT4001_DEVICE_ID, &dev_id);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret,
			"Failed to read the device ID register\n");

	dev_id = FIELD_GET(OPT4001_DEVICE_ID_MASK, dev_id);
	if (dev_id != OPT4001_DEVICE_ID_VAL)
		dev_warn(&client->dev, "Device ID: %#04x unknown\n", dev_id);

	chip->chip_info = device_get_match_data(&client->dev);

	indio_dev->channels = opt4001_channels;
	indio_dev->num_channels = ARRAY_SIZE(opt4001_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = chip->chip_info->name;

	ret = opt4001_load_defaults(chip);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret,
				     "Failed to set sensor defaults\n");

	ret = devm_add_action_or_reset(&client->dev,
					opt4001_chip_off_action,
					chip);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret,
				     "Failed to setup power off action\n");

	return devm_iio_device_register(&client->dev, indio_dev);
}

/*
 * The compatible string determines which constants to use depending on
 * opt4001 packaging
 */
static const struct i2c_device_id opt4001_id[] = {
	{ "opt4001-sot-5x3", (kernel_ulong_t)&opt4001_sot_5x3_info },
	{ "opt4001-picostar", (kernel_ulong_t)&opt4001_picostar_info },
	{ }
};
MODULE_DEVICE_TABLE(i2c, opt4001_id);

static const struct of_device_id opt4001_of_match[] = {
	{ .compatible = "ti,opt4001-sot-5x3", .data = &opt4001_sot_5x3_info},
	{ .compatible = "ti,opt4001-picostar", .data = &opt4001_picostar_info},
	{}
};
MODULE_DEVICE_TABLE(of, opt4001_of_match);

static struct i2c_driver opt4001_driver = {
	.driver = {
		.name = "opt4001",
		.of_match_table = opt4001_of_match,
	},
	.probe = opt4001_probe,
	.id_table = opt4001_id,
};
module_i2c_driver(opt4001_driver);

MODULE_AUTHOR("Stefan Windfeldt-Prytz <stefan.windfeldt-prytz@axis.com>");
MODULE_DESCRIPTION("Texas Instruments opt4001 ambient light sensor driver");
MODULE_LICENSE("GPL");
