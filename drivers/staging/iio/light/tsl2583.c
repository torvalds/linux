/*
 * Device driver for monitoring ambient light intensity (lux)
 * within the TAOS tsl258x family of devices (tsl2580, tsl2581).
 *
 * Copyright (c) 2011, TAOS Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA	02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* Triton register offsets */
#define	TSL258X_REG_MAX		8

/* Device Registers and Masks */
#define TSL258X_CNTRL			0x00
#define TSL258X_ALS_TIME		0X01
#define TSL258X_INTERRUPT		0x02
#define TSL258X_GAIN			0x07
#define TSL258X_REVID			0x11
#define TSL258X_CHIPID			0x12
#define TSL258X_ALS_CHAN0LO		0x14
#define TSL258X_ALS_CHAN0HI		0x15
#define TSL258X_ALS_CHAN1LO		0x16
#define TSL258X_ALS_CHAN1HI		0x17
#define TSL258X_TMR_LO			0x18
#define TSL258X_TMR_HI			0x19

/* tsl2583 cmd reg masks */
#define TSL258X_CMD_REG			0x80
#define TSL258X_CMD_SPL_FN		0x60
#define TSL258X_CMD_ALS_INT_CLR	0X01

/* tsl2583 cntrl reg masks */
#define TSL258X_CNTL_ADC_ENBL	0x02
#define TSL258X_CNTL_PWR_ON		0x01

/* tsl2583 status reg masks */
#define TSL258X_STA_ADC_VALID	0x01
#define TSL258X_STA_ADC_INTR	0x10

/* Lux calculation constants */
#define	TSL258X_LUX_CALC_OVER_FLOW		65535

#define TSL2583_CHIP_ID			0x90
#define TSL2583_CHIP_ID_MASK		0xf0

enum {
	TSL258X_CHIP_UNKNOWN = 0,
	TSL258X_CHIP_WORKING = 1,
	TSL258X_CHIP_SUSPENDED = 2
};

/* Per-device data */
struct taos_als_info {
	u16 als_ch0;
	u16 als_ch1;
	u16 lux;
};

struct taos_settings {
	int als_time;
	int als_gain;
	int als_gain_trim;
	int als_cal_target;
};

struct tsl2583_chip {
	struct mutex als_mutex;
	struct i2c_client *client;
	struct taos_als_info als_cur_info;
	struct taos_settings taos_settings;
	int als_time_scale;
	int als_saturation;
	int taos_chip_status;
	u8 taos_config[8];
};

/*
 * Initial values for device - this values can/will be changed by driver.
 * and applications as needed.
 * These values are dynamic.
 */
static const u8 taos_config[8] = {
		0x00, 0xee, 0x00, 0x03, 0x00, 0xFF, 0xFF, 0x00
}; /*	cntrl atime intC  Athl0 Athl1 Athh0 Athh1 gain */

struct taos_lux {
	unsigned int ratio;
	unsigned int ch0;
	unsigned int ch1;
};

/* This structure is intentionally large to accommodate updates via sysfs. */
/* Sized to 11 = max 10 segments + 1 termination segment */
/* Assumption is one and only one type of glass used  */
static struct taos_lux taos_device_lux[11] = {
	{  9830,  8520, 15729 },
	{ 12452, 10807, 23344 },
	{ 14746,  6383, 11705 },
	{ 17695,  4063,  6554 },
};

struct gainadj {
	s16 ch0;
	s16 ch1;
	s16 mean;
};

/* Index = (0 - 3) Used to validate the gain selection index */
static const struct gainadj gainadj[] = {
	{ 1, 1, 1 },
	{ 8, 8, 8 },
	{ 16, 16, 16 },
	{ 107, 115, 111 }
};

/*
 * Provides initial operational parameter defaults.
 * These defaults may be changed through the device's sysfs files.
 */
static void taos_defaults(struct tsl2583_chip *chip)
{
	/*
	 * The integration time must be a multiple of 50ms and within the
	 * range [50, 600] ms.
	 */
	chip->taos_settings.als_time = 100;

	/*
	 * This is an index into the gainadj table. Assume clear glass as the
	 * default.
	 */
	chip->taos_settings.als_gain = 0;

	/* Default gain trim to account for aperture effects */
	chip->taos_settings.als_gain_trim = 1000;

	/* Known external ALS reading used for calibration */
	chip->taos_settings.als_cal_target = 130;
}

/*
 * Reads and calculates current lux value.
 * The raw ch0 and ch1 values of the ambient light sensed in the last
 * integration cycle are read from the device.
 * Time scale factor array values are adjusted based on the integration time.
 * The raw values are multiplied by a scale factor, and device gain is obtained
 * using gain index. Limit checks are done next, then the ratio of a multiple
 * of ch1 value, to the ch0 value, is calculated. The array taos_device_lux[]
 * declared above is then scanned to find the first ratio value that is just
 * above the ratio we just calculated. The ch0 and ch1 multiplier constants in
 * the array are then used along with the time scale factor array values, to
 * calculate the lux.
 */
static int taos_get_lux(struct iio_dev *indio_dev)
{
	u16 ch0, ch1; /* separated ch0/ch1 data from device */
	u32 lux; /* raw lux calculated from device data */
	u64 lux64;
	u32 ratio;
	u8 buf[5];
	struct taos_lux *p;
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int i, ret;
	u32 ch0lux = 0;
	u32 ch1lux = 0;

	if (chip->taos_chip_status != TSL258X_CHIP_WORKING) {
		/* device is not enabled */
		dev_err(&chip->client->dev, "taos_get_lux device is not enabled\n");
		ret = -EBUSY;
		goto done;
	}

	ret = i2c_smbus_read_byte_data(chip->client, TSL258X_CMD_REG);
	if (ret < 0) {
		dev_err(&chip->client->dev, "taos_get_lux failed to read CMD_REG\n");
		goto done;
	}

	/* is data new & valid */
	if (!(ret & TSL258X_STA_ADC_INTR)) {
		dev_err(&chip->client->dev, "taos_get_lux data not valid\n");
		ret = chip->als_cur_info.lux; /* return LAST VALUE */
		goto done;
	}

	for (i = 0; i < 4; i++) {
		int reg = TSL258X_CMD_REG | (TSL258X_ALS_CHAN0LO + i);

		ret = i2c_smbus_read_byte_data(chip->client, reg);
		if (ret < 0) {
			dev_err(&chip->client->dev,
				"taos_get_lux failed to read register %x\n",
				reg);
			goto done;
		}
		buf[i] = ret;
	}

	/*
	 * clear status, really interrupt status (interrupts are off), but
	 * we use the bit anyway - don't forget 0x80 - this is a command
	 */
	ret = i2c_smbus_write_byte(chip->client,
				   (TSL258X_CMD_REG | TSL258X_CMD_SPL_FN |
				    TSL258X_CMD_ALS_INT_CLR));

	if (ret < 0) {
		dev_err(&chip->client->dev,
			"taos_i2c_write_command failed in taos_get_lux, err = %d\n",
			ret);
		goto done; /* have no data, so return failure */
	}

	/* extract ALS/lux data */
	ch0 = le16_to_cpup((const __le16 *)&buf[0]);
	ch1 = le16_to_cpup((const __le16 *)&buf[2]);

	chip->als_cur_info.als_ch0 = ch0;
	chip->als_cur_info.als_ch1 = ch1;

	if ((ch0 >= chip->als_saturation) || (ch1 >= chip->als_saturation))
		goto return_max;

	if (!ch0) {
		/* have no data, so return LAST VALUE */
		ret = 0;
		chip->als_cur_info.lux = 0;
		goto done;
	}
	/* calculate ratio */
	ratio = (ch1 << 15) / ch0;
	/* convert to unscaled lux using the pointer to the table */
	for (p = (struct taos_lux *)taos_device_lux;
	     p->ratio != 0 && p->ratio < ratio; p++)
		;

	if (p->ratio == 0) {
		lux = 0;
	} else {
		ch0lux = ((ch0 * p->ch0) +
			  (gainadj[chip->taos_settings.als_gain].ch0 >> 1))
			 / gainadj[chip->taos_settings.als_gain].ch0;
		ch1lux = ((ch1 * p->ch1) +
			  (gainadj[chip->taos_settings.als_gain].ch1 >> 1))
			 / gainadj[chip->taos_settings.als_gain].ch1;
		lux = ch0lux - ch1lux;
	}

	/* note: lux is 31 bit max at this point */
	if (ch1lux > ch0lux) {
		dev_dbg(&chip->client->dev, "No Data - Return last value\n");
		ret = 0;
		chip->als_cur_info.lux = 0;
		goto done;
	}

	/* adjust for active time scale */
	if (chip->als_time_scale == 0)
		lux = 0;
	else
		lux = (lux + (chip->als_time_scale >> 1)) /
			chip->als_time_scale;

	/* Adjust for active gain scale.
	 * The taos_device_lux tables above have a factor of 8192 built in,
	 * so we need to shift right.
	 * User-specified gain provides a multiplier.
	 * Apply user-specified gain before shifting right to retain precision.
	 * Use 64 bits to avoid overflow on multiplication.
	 * Then go back to 32 bits before division to avoid using div_u64().
	 */
	lux64 = lux;
	lux64 = lux64 * chip->taos_settings.als_gain_trim;
	lux64 >>= 13;
	lux = lux64;
	lux = (lux + 500) / 1000;
	if (lux > TSL258X_LUX_CALC_OVER_FLOW) { /* check for overflow */
return_max:
		lux = TSL258X_LUX_CALC_OVER_FLOW;
	}

	/* Update the structure with the latest VALID lux. */
	chip->als_cur_info.lux = lux;
	ret = lux;

done:
	return ret;
}

/*
 * Obtain single reading and calculate the als_gain_trim (later used
 * to derive actual lux).
 * Return updated gain_trim value.
 */
static int taos_als_calibrate(struct iio_dev *indio_dev)
{
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	unsigned int gain_trim_val;
	int ret;
	int lux_val;

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL258X_CMD_REG | TSL258X_CNTRL);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s failed to read from the CNTRL register\n",
			__func__);
		return ret;
	}

	if ((ret & (TSL258X_CNTL_ADC_ENBL | TSL258X_CNTL_PWR_ON))
			!= (TSL258X_CNTL_ADC_ENBL | TSL258X_CNTL_PWR_ON)) {
		dev_err(&chip->client->dev,
			"taos_als_calibrate failed: device not powered on with ADC enabled\n");
		return -EINVAL;
	} else if ((ret & TSL258X_STA_ADC_VALID) != TSL258X_STA_ADC_VALID) {
		dev_err(&chip->client->dev,
			"taos_als_calibrate failed: STATUS - ADC not valid.\n");
		return -ENODATA;
	}
	lux_val = taos_get_lux(indio_dev);
	if (lux_val < 0) {
		dev_err(&chip->client->dev, "taos_als_calibrate failed to get lux\n");
		return lux_val;
	}
	gain_trim_val = (unsigned int)(((chip->taos_settings.als_cal_target)
			* chip->taos_settings.als_gain_trim) / lux_val);

	if ((gain_trim_val < 250) || (gain_trim_val > 4000)) {
		dev_err(&chip->client->dev,
			"taos_als_calibrate failed: trim_val of %d is out of range\n",
			gain_trim_val);
		return -ENODATA;
	}
	chip->taos_settings.als_gain_trim = (int)gain_trim_val;

	return (int)gain_trim_val;
}

/*
 * Turn the device on.
 * Configuration must be set before calling this function.
 */
static int taos_chip_on(struct iio_dev *indio_dev)
{
	int i;
	int ret;
	u8 *uP;
	u8 utmp;
	int als_count;
	int als_time;
	struct tsl2583_chip *chip = iio_priv(indio_dev);

	/* and make sure we're not already on */
	if (chip->taos_chip_status == TSL258X_CHIP_WORKING) {
		/* if forcing a register update - turn off, then on */
		dev_info(&chip->client->dev, "device is already enabled\n");
		return -EINVAL;
	}

	/* determine als integration register */
	als_count = (chip->taos_settings.als_time * 100 + 135) / 270;
	if (!als_count)
		als_count = 1; /* ensure at least one cycle */

	/* convert back to time (encompasses overrides) */
	als_time = (als_count * 27 + 5) / 10;
	chip->taos_config[TSL258X_ALS_TIME] = 256 - als_count;

	/* Set the gain based on taos_settings struct */
	chip->taos_config[TSL258X_GAIN] = chip->taos_settings.als_gain;

	/* set chip struct re scaling and saturation */
	chip->als_saturation = als_count * 922; /* 90% of full scale */
	chip->als_time_scale = (als_time + 25) / 50;

	/* Power on the device; ADC off. */
	chip->taos_config[TSL258X_CNTRL] = TSL258X_CNTL_PWR_ON;

	/*
	 * Use the following shadow copy for our delay before enabling ADC.
	 * Write all the registers.
	 */
	for (i = 0, uP = chip->taos_config; i < TSL258X_REG_MAX; i++) {
		ret = i2c_smbus_write_byte_data(chip->client,
						TSL258X_CMD_REG + i,
						*uP++);
		if (ret < 0) {
			dev_err(&chip->client->dev,
				"taos_chip_on failed on reg %d.\n", i);
			return ret;
		}
	}

	usleep_range(3000, 3500);
	/*
	 * NOW enable the ADC
	 * initialize the desired mode of operation
	 */
	utmp = TSL258X_CNTL_PWR_ON | TSL258X_CNTL_ADC_ENBL;
	ret = i2c_smbus_write_byte_data(chip->client,
					TSL258X_CMD_REG | TSL258X_CNTRL,
					utmp);
	if (ret < 0) {
		dev_err(&chip->client->dev, "taos_chip_on failed on 2nd CTRL reg.\n");
		return ret;
	}
	chip->taos_chip_status = TSL258X_CHIP_WORKING;

	return ret;
}

static int taos_chip_off(struct iio_dev *indio_dev)
{
	struct tsl2583_chip *chip = iio_priv(indio_dev);

	/* turn device off */
	chip->taos_chip_status = TSL258X_CHIP_SUSPENDED;
	return i2c_smbus_write_byte_data(chip->client,
					TSL258X_CMD_REG | TSL258X_CNTRL,
					0x00);
}

/* Sysfs Interface Functions */

static ssize_t in_illuminance_input_target_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int ret;

	mutex_lock(&chip->als_mutex);
	ret = sprintf(buf, "%d\n", chip->taos_settings.als_cal_target);
	mutex_unlock(&chip->als_mutex);

	return ret;
}

static ssize_t in_illuminance_input_target_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int value;

	if (kstrtoint(buf, 0, &value) || !value)
		return -EINVAL;

	mutex_lock(&chip->als_mutex);
	chip->taos_settings.als_cal_target = value;
	mutex_unlock(&chip->als_mutex);

	return len;
}

static ssize_t in_illuminance_calibrate_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int value;

	if (kstrtoint(buf, 0, &value) || value != 1)
		return -EINVAL;

	mutex_lock(&chip->als_mutex);
	taos_als_calibrate(indio_dev);
	mutex_unlock(&chip->als_mutex);

	return len;
}

static ssize_t in_illuminance_lux_table_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	int i;
	int offset = 0;

	for (i = 0; i < ARRAY_SIZE(taos_device_lux); i++) {
		offset += sprintf(buf + offset, "%u,%u,%u,",
				  taos_device_lux[i].ratio,
				  taos_device_lux[i].ch0,
				  taos_device_lux[i].ch1);
		if (taos_device_lux[i].ratio == 0) {
			/*
			 * We just printed the first "0" entry.
			 * Now get rid of the extra "," and break.
			 */
			offset--;
			break;
		}
	}

	offset += sprintf(buf + offset, "\n");
	return offset;
}

static ssize_t in_illuminance_lux_table_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int value[ARRAY_SIZE(taos_device_lux) * 3 + 1];
	int n, ret = -EINVAL;

	mutex_lock(&chip->als_mutex);

	get_options(buf, ARRAY_SIZE(value), value);

	/* We now have an array of ints starting at value[1], and
	 * enumerated by value[0].
	 * We expect each group of three ints is one table entry,
	 * and the last table entry is all 0.
	 */
	n = value[0];
	if ((n % 3) || n < 6 || n > ((ARRAY_SIZE(taos_device_lux) - 1) * 3)) {
		dev_info(dev, "LUX TABLE INPUT ERROR 1 Value[0]=%d\n", n);
		goto done;
	}
	if ((value[(n - 2)] | value[(n - 1)] | value[n]) != 0) {
		dev_info(dev, "LUX TABLE INPUT ERROR 2 Value[0]=%d\n", n);
		goto done;
	}

	/* Zero out the table */
	memset(taos_device_lux, 0, sizeof(taos_device_lux));
	memcpy(taos_device_lux, &value[1], (value[0] * 4));

	ret = len;

done:
	mutex_unlock(&chip->als_mutex);

	return ret;
}

static IIO_CONST_ATTR(in_illuminance_calibscale_available, "1 8 16 111");
static IIO_CONST_ATTR(in_illuminance_integration_time_available,
		      "0.000050 0.000100 0.000150 0.000200 0.000250 0.000300 0.000350 0.000400 0.000450 0.000500 0.000550 0.000600 0.000650");
static IIO_DEVICE_ATTR_RW(in_illuminance_input_target, 0);
static IIO_DEVICE_ATTR_WO(in_illuminance_calibrate, 0);
static IIO_DEVICE_ATTR_RW(in_illuminance_lux_table, 0);

static struct attribute *sysfs_attrs_ctrl[] = {
	&iio_const_attr_in_illuminance_calibscale_available.dev_attr.attr,
	&iio_const_attr_in_illuminance_integration_time_available.dev_attr.attr,
	&iio_dev_attr_in_illuminance_input_target.dev_attr.attr,
	&iio_dev_attr_in_illuminance_calibrate.dev_attr.attr,
	&iio_dev_attr_in_illuminance_lux_table.dev_attr.attr,
	NULL
};

static const struct attribute_group tsl2583_attribute_group = {
	.attrs = sysfs_attrs_ctrl,
};

static const struct iio_chan_spec tsl2583_channels[] = {
	{
		.type = IIO_LIGHT,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
	{
		.type = IIO_LIGHT,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_CALIBBIAS) |
				      BIT(IIO_CHAN_INFO_CALIBSCALE) |
				      BIT(IIO_CHAN_INFO_INT_TIME),
	},
};

static int tsl2583_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&chip->als_mutex);

	if (chip->taos_chip_status != TSL258X_CHIP_WORKING) {
		ret = -EBUSY;
		goto read_done;
	}

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_LIGHT) {
			ret = taos_get_lux(indio_dev);
			if (ret < 0)
				goto read_done;

			/*
			 * From page 20 of the TSL2581, TSL2583 data
			 * sheet (TAOS134 − MARCH 2011):
			 *
			 * One of the photodiodes (channel 0) is
			 * sensitive to both visible and infrared light,
			 * while the second photodiode (channel 1) is
			 * sensitive primarily to infrared light.
			 */
			if (chan->channel2 == IIO_MOD_LIGHT_BOTH)
				*val = chip->als_cur_info.als_ch0;
			else
				*val = chip->als_cur_info.als_ch1;

			ret = IIO_VAL_INT;
		}
		break;
	case IIO_CHAN_INFO_PROCESSED:
		if (chan->type == IIO_LIGHT) {
			ret = taos_get_lux(indio_dev);
			if (ret < 0)
				goto read_done;

			*val = ret;
			ret = IIO_VAL_INT;
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		if (chan->type == IIO_LIGHT) {
			*val = chip->taos_settings.als_gain_trim;
			ret = IIO_VAL_INT;
		}
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_LIGHT) {
			*val = gainadj[chip->taos_settings.als_gain].mean;
			ret = IIO_VAL_INT;
		}
		break;
	case IIO_CHAN_INFO_INT_TIME:
		if (chan->type == IIO_LIGHT) {
			*val = 0;
			*val2 = chip->taos_settings.als_time;
			ret = IIO_VAL_INT_PLUS_MICRO;
		}
		break;
	default:
		break;
	}

read_done:
	mutex_unlock(&chip->als_mutex);

	return ret;
}

static int tsl2583_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&chip->als_mutex);

	if (chip->taos_chip_status != TSL258X_CHIP_WORKING) {
		ret = -EBUSY;
		goto write_done;
	}

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		if (chan->type == IIO_LIGHT) {
			chip->taos_settings.als_gain_trim = val;
			ret = 0;
		}
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_LIGHT) {
			int i;

			for (i = 0; i < ARRAY_SIZE(gainadj); i++) {
				if (gainadj[i].mean == val) {
					chip->taos_settings.als_gain = i;
					ret = 0;
					break;
				}
			}
		}
		break;
	case IIO_CHAN_INFO_INT_TIME:
		if (chan->type == IIO_LIGHT && !val && val2 >= 50 &&
		    val2 <= 650 && !(val2 % 50)) {
			chip->taos_settings.als_time = val2;
			ret = 0;
		}
		break;
	default:
		break;
	}

write_done:
	mutex_unlock(&chip->als_mutex);

	return ret;
}

static const struct iio_info tsl2583_info = {
	.attrs = &tsl2583_attribute_group,
	.driver_module = THIS_MODULE,
	.read_raw = tsl2583_read_raw,
	.write_raw = tsl2583_write_raw,
};

/*
 * Client probe function - When a valid device is found, the driver's device
 * data structure is updated, and initialization completes successfully.
 */
static int taos_probe(struct i2c_client *clientp,
		      const struct i2c_device_id *idp)
{
	int ret;
	struct tsl2583_chip *chip;
	struct iio_dev *indio_dev;

	if (!i2c_check_functionality(clientp->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&clientp->dev, "taos_probe() - i2c smbus byte data func unsupported\n");
		return -EOPNOTSUPP;
	}

	indio_dev = devm_iio_device_alloc(&clientp->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;
	chip = iio_priv(indio_dev);
	chip->client = clientp;
	i2c_set_clientdata(clientp, indio_dev);

	mutex_init(&chip->als_mutex);
	chip->taos_chip_status = TSL258X_CHIP_UNKNOWN;
	memcpy(chip->taos_config, taos_config, sizeof(chip->taos_config));

	ret = i2c_smbus_read_byte_data(clientp,
				       TSL258X_CMD_REG | TSL258X_CHIPID);
	if (ret < 0) {
		dev_err(&clientp->dev,
			"%s failed to read the chip ID register\n", __func__);
		return ret;
	}

	if ((ret & TSL2583_CHIP_ID_MASK) != TSL2583_CHIP_ID) {
		dev_info(&clientp->dev, "%s received an unknown chip ID %x\n",
			 __func__, ret);
		return -EINVAL;
	}

	ret = i2c_smbus_write_byte(clientp, (TSL258X_CMD_REG | TSL258X_CNTRL));
	if (ret < 0) {
		dev_err(&clientp->dev,
			"i2c_smbus_write_byte() to cmd reg failed in taos_probe(), err = %d\n",
			ret);
		return ret;
	}

	indio_dev->info = &tsl2583_info;
	indio_dev->channels = tsl2583_channels;
	indio_dev->num_channels = ARRAY_SIZE(tsl2583_channels);
	indio_dev->dev.parent = &clientp->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = chip->client->name;
	ret = devm_iio_device_register(indio_dev->dev.parent, indio_dev);
	if (ret) {
		dev_err(&clientp->dev, "iio registration failed\n");
		return ret;
	}

	/* Load up the V2 defaults (these are hard coded defaults for now) */
	taos_defaults(chip);

	/* Make sure the chip is on */
	ret = taos_chip_on(indio_dev);
	if (ret < 0)
		return ret;

	dev_info(&clientp->dev, "Light sensor found.\n");
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int taos_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&chip->als_mutex);

	if (chip->taos_chip_status == TSL258X_CHIP_WORKING) {
		ret = taos_chip_off(indio_dev);
		chip->taos_chip_status = TSL258X_CHIP_SUSPENDED;
	}

	mutex_unlock(&chip->als_mutex);
	return ret;
}

static int taos_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&chip->als_mutex);

	if (chip->taos_chip_status == TSL258X_CHIP_SUSPENDED)
		ret = taos_chip_on(indio_dev);

	mutex_unlock(&chip->als_mutex);
	return ret;
}

static SIMPLE_DEV_PM_OPS(taos_pm_ops, taos_suspend, taos_resume);
#define TAOS_PM_OPS (&taos_pm_ops)
#else
#define TAOS_PM_OPS NULL
#endif

static struct i2c_device_id taos_idtable[] = {
	{ "tsl2580", 0 },
	{ "tsl2581", 1 },
	{ "tsl2583", 2 },
	{}
};
MODULE_DEVICE_TABLE(i2c, taos_idtable);

#ifdef CONFIG_OF
static const struct of_device_id taos2583_of_match[] = {
	{ .compatible = "amstaos,tsl2580", },
	{ .compatible = "amstaos,tsl2581", },
	{ .compatible = "amstaos,tsl2583", },
	{ },
};
MODULE_DEVICE_TABLE(of, taos2583_of_match);
#else
#define taos2583_of_match NULL
#endif

/* Driver definition */
static struct i2c_driver taos_driver = {
	.driver = {
		.name = "tsl2583",
		.pm = TAOS_PM_OPS,
		.of_match_table = taos2583_of_match,
	},
	.id_table = taos_idtable,
	.probe = taos_probe,
};
module_i2c_driver(taos_driver);

MODULE_AUTHOR("J. August Brenner<jbrenner@taosinc.com>");
MODULE_DESCRIPTION("TAOS tsl2583 ambient light sensor driver");
MODULE_LICENSE("GPL");
