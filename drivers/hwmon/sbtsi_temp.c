// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sbtsi_temp.c - hwmon driver for a SBI Temperature Sensor Interface (SB-TSI)
 *                compliant AMD SoC temperature device.
 *
 * Copyright (c) 2020, Google Inc.
 * Copyright (c) 2020, Kun Yi <kunyi@google.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/misc/tsi.h>

/*
 * SB-TSI registers only support SMBus byte data access. "_INT" registers are
 * the integer part of a temperature value or limit, and "_DEC" registers are
 * corresponding decimal parts.
 */
#define SBTSI_REG_TEMP_INT		0x01 /* RO */
#define SBTSI_REG_STATUS		0x02 /* RO */
#define SBTSI_REG_TEMP_HIGH_INT		0x07 /* RW */
#define SBTSI_REG_TEMP_LOW_INT		0x08 /* RW */
#define SBTSI_REG_TEMP_DEC		0x10 /* RW */
#define SBTSI_REG_TEMP_HIGH_DEC		0x13 /* RW */
#define SBTSI_REG_TEMP_LOW_DEC		0x14 /* RW */

#define SBTSI_TEMP_EXT_RANGE_ADJ	49000

#define SBTSI_TEMP_MIN	0
#define SBTSI_TEMP_MAX	255875

/*
 * From SB-TSI spec: CPU temperature readings and limit registers encode the
 * temperature in increments of 0.125 from 0 to 255.875. The "high byte"
 * register encodes the base-2 of the integer portion, and the upper 3 bits of
 * the "low byte" encode in base-2 the decimal portion.
 *
 * e.g. INT=0x19, DEC=0x20 represents 25.125 degrees Celsius
 *
 * Therefore temperature in millidegree Celsius =
 *   (INT + DEC / 256) * 1000 = (INT * 8 + DEC / 32) * 125
 */
static inline int sbtsi_reg_to_mc(s32 integer, s32 decimal)
{
	return ((integer << 3) + (decimal >> 5)) * 125;
}

/*
 * Inversely, given temperature in millidegree Celsius
 *   INT = (TEMP / 125) / 8
 *   DEC = ((TEMP / 125) % 8) * 32
 * Caller have to make sure temp doesn't exceed 255875, the max valid value.
 */
static inline void sbtsi_mc_to_reg(s32 temp, u8 *integer, u8 *decimal)
{
	temp /= 125;
	*integer = temp >> 3;
	*decimal = (temp & 0x7) << 5;
}

/*
 * Read integer and decimal parts of an SB-TSI temperature register pair
 * The read order is determined by the ReadOrder bit to ensure atomic latching.
 */
static int sbtsi_temp_read(struct sbtsi_data *data, u8 reg1, u8 reg2,
			   u8 *val1, u8 *val2)
{
	int ret;

	guard(sbtsi)(data);
	ret = sbtsi_xfer(data, reg1, val1, true);
	if (!ret)
		ret = sbtsi_xfer(data, reg2, val2, true);
	return ret;
}

/*
 * Write integer and decimal parts of an SB-TSI temperature register pair.
 */
static int sbtsi_temp_write(struct sbtsi_data *data, u8 reg_int, u8 reg_dec,
			    u8 val_int, u8 val_dec)
{
	int ret;

	guard(sbtsi)(data);
	ret = sbtsi_xfer(data, reg_int, &val_int, false);
	if (!ret)
		ret = sbtsi_xfer(data, reg_dec, &val_dec, false);
	return ret;
}

static int sbtsi_read(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long *val)
{
	struct sbtsi_data *data = dev_get_drvdata(dev);
	s32 temp_int, temp_dec;
	int err;
	u8 val_int, val_dec;

	switch (attr) {
	case hwmon_temp_input:
		if (data->read_order)
			err = sbtsi_temp_read(data,
					      SBTSI_REG_TEMP_DEC, SBTSI_REG_TEMP_INT,
					      &val_dec, &val_int);
		else
			err = sbtsi_temp_read(data,
					      SBTSI_REG_TEMP_INT, SBTSI_REG_TEMP_DEC,
					      &val_int, &val_dec);
		if (err < 0)
			return err;
		break;
	case hwmon_temp_max:
		err = sbtsi_temp_read(data,
				      SBTSI_REG_TEMP_HIGH_INT, SBTSI_REG_TEMP_HIGH_DEC,
				      &val_int, &val_dec);
		if (err < 0)
			return err;
		break;
	case hwmon_temp_min:
		err = sbtsi_temp_read(data,
				      SBTSI_REG_TEMP_LOW_INT, SBTSI_REG_TEMP_LOW_DEC,
				      &val_int, &val_dec);

		if (err < 0)
			return err;
		break;
	default:
		return -EINVAL;
	}

	temp_int = val_int;
	temp_dec = val_dec;
	*val = sbtsi_reg_to_mc(temp_int, temp_dec);
	if (data->ext_range_mode)
		*val -= SBTSI_TEMP_EXT_RANGE_ADJ;

	return 0;
}

static int sbtsi_write(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long val)
{
	struct sbtsi_data *data = dev_get_drvdata(dev);
	int reg_int, reg_dec;
	u8 temp_int, temp_dec;

	switch (attr) {
	case hwmon_temp_max:
		reg_int = SBTSI_REG_TEMP_HIGH_INT;
		reg_dec = SBTSI_REG_TEMP_HIGH_DEC;
		break;
	case hwmon_temp_min:
		reg_int = SBTSI_REG_TEMP_LOW_INT;
		reg_dec = SBTSI_REG_TEMP_LOW_DEC;
		break;
	default:
		return -EINVAL;
	}

	if (data->ext_range_mode)
		val += SBTSI_TEMP_EXT_RANGE_ADJ;
	val = clamp_val(val, SBTSI_TEMP_MIN, SBTSI_TEMP_MAX);
	sbtsi_mc_to_reg(val, &temp_int, &temp_dec);

	return sbtsi_temp_write(data, reg_int, reg_dec, temp_int, temp_dec);
}

static umode_t sbtsi_is_visible(const void *data,
				enum hwmon_sensor_types type,
				u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		case hwmon_temp_min:
			return 0644;
		case hwmon_temp_max:
			return 0644;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info * const sbtsi_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX),
	NULL
};

static const struct hwmon_ops sbtsi_hwmon_ops = {
	.is_visible = sbtsi_is_visible,
	.read = sbtsi_read,
	.write = sbtsi_write,
};

static const struct hwmon_chip_info sbtsi_chip_info = {
	.ops = &sbtsi_hwmon_ops,
	.info = sbtsi_info,
};

static int sbtsi_probe(struct auxiliary_device *adev,
		       const struct auxiliary_device_id *id)
{
	struct sbtsi_data *data = dev_get_drvdata(adev->dev.parent);
	struct device *dev = &adev->dev;
	struct device *hwmon_dev;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "sbtsi", data,
							 &sbtsi_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct auxiliary_device_id sbtsi_id[] = {
	{ .name = AMD_SBTSI_ADEV "." AMD_SBTSI_AUX_HWMON },
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, sbtsi_id);

static struct auxiliary_driver sbtsi_driver = {
	.driver = {
		.name = "sbtsi",
	},
	.probe = sbtsi_probe,
	.id_table = sbtsi_id,
};
module_auxiliary_driver(sbtsi_driver);

MODULE_AUTHOR("Kun Yi <kunyi@google.com>");
MODULE_DESCRIPTION("Hwmon driver for AMD SB-TSI emulated sensor");
MODULE_LICENSE("GPL");
