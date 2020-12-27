// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Device driver for monitoring ambient light intensity (lux)
 * within the TAOS tsl258x family of devices (tsl2580, tsl2581, tsl2583).
 *
 * Copyright (c) 2011, TAOS Corporation.
 * Copyright (c) 2016-2017 Brian Masney <masneyb@onstation.org>
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
#include <linux/pm_runtime.h>

/* Device Registers and Masks */
#define TSL2583_CNTRL			0x00
#define TSL2583_ALS_TIME		0X01
#define TSL2583_INTERRUPT		0x02
#define TSL2583_GAIN			0x07
#define TSL2583_REVID			0x11
#define TSL2583_CHIPID			0x12
#define TSL2583_ALS_CHAN0LO		0x14
#define TSL2583_ALS_CHAN0HI		0x15
#define TSL2583_ALS_CHAN1LO		0x16
#define TSL2583_ALS_CHAN1HI		0x17
#define TSL2583_TMR_LO			0x18
#define TSL2583_TMR_HI			0x19

/* tsl2583 cmd reg masks */
#define TSL2583_CMD_REG			0x80
#define TSL2583_CMD_SPL_FN		0x60
#define TSL2583_CMD_ALS_INT_CLR		0x01

/* tsl2583 cntrl reg masks */
#define TSL2583_CNTL_ADC_ENBL		0x02
#define TSL2583_CNTL_PWR_OFF		0x00
#define TSL2583_CNTL_PWR_ON		0x01

/* tsl2583 status reg masks */
#define TSL2583_STA_ADC_VALID		0x01
#define TSL2583_STA_ADC_INTR		0x10

/* Lux calculation constants */
#define TSL2583_LUX_CALC_OVER_FLOW	65535

#define TSL2583_INTERRUPT_DISABLED	0x00

#define TSL2583_CHIP_ID			0x90
#define TSL2583_CHIP_ID_MASK		0xf0

#define TSL2583_POWER_OFF_DELAY_MS	2000

/* Per-device data */
struct tsl2583_als_info {
	u16 als_ch0;
	u16 als_ch1;
	u16 lux;
};

struct tsl2583_lux {
	unsigned int ratio;
	unsigned int ch0;
	unsigned int ch1;
};

static const struct tsl2583_lux tsl2583_default_lux[] = {
	{  9830,  8520, 15729 },
	{ 12452, 10807, 23344 },
	{ 14746,  6383, 11705 },
	{ 17695,  4063,  6554 },
	{     0,     0,     0 }  /* Termination segment */
};

#define TSL2583_MAX_LUX_TABLE_ENTRIES 11

struct tsl2583_settings {
	int als_time;
	int als_gain;
	int als_gain_trim;
	int als_cal_target;

	/*
	 * This structure is intentionally large to accommodate updates via
	 * sysfs. Sized to 11 = max 10 segments + 1 termination segment.
	 * Assumption is that one and only one type of glass used.
	 */
	struct tsl2583_lux als_device_lux[TSL2583_MAX_LUX_TABLE_ENTRIES];
};

struct tsl2583_chip {
	struct mutex als_mutex;
	struct i2c_client *client;
	struct tsl2583_als_info als_cur_info;
	struct tsl2583_settings als_settings;
	int als_time_scale;
	int als_saturation;
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
static void tsl2583_defaults(struct tsl2583_chip *chip)
{
	/*
	 * The integration time must be a multiple of 50ms and within the
	 * range [50, 600] ms.
	 */
	chip->als_settings.als_time = 100;

	/*
	 * This is an index into the gainadj table. Assume clear glass as the
	 * default.
	 */
	chip->als_settings.als_gain = 0;

	/* Default gain trim to account for aperture effects */
	chip->als_settings.als_gain_trim = 1000;

	/* Known external ALS reading used for calibration */
	chip->als_settings.als_cal_target = 130;

	/* Default lux table. */
	memcpy(chip->als_settings.als_device_lux, tsl2583_default_lux,
	       sizeof(tsl2583_default_lux));
}

/*
 * Reads and calculates current lux value.
 * The raw ch0 and ch1 values of the ambient light sensed in the last
 * integration cycle are read from the device.
 * Time scale factor array values are adjusted based on the integration time.
 * The raw values are multiplied by a scale factor, and device gain is obtained
 * using gain index. Limit checks are done next, then the ratio of a multiple
 * of ch1 value, to the ch0 value, is calculated. The array als_device_lux[]
 * declared above is then scanned to find the first ratio value that is just
 * above the ratio we just calculated. The ch0 and ch1 multiplier constants in
 * the array are then used along with the time scale factor array values, to
 * calculate the lux.
 */
static int tsl2583_get_lux(struct iio_dev *indio_dev)
{
	u16 ch0, ch1; /* separated ch0/ch1 data from device */
	u32 lux; /* raw lux calculated from device data */
	u64 lux64;
	u32 ratio;
	u8 buf[5];
	struct tsl2583_lux *p;
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int i, ret;

	ret = i2c_smbus_read_byte_data(chip->client, TSL2583_CMD_REG);
	if (ret < 0) {
		dev_err(&chip->client->dev, "%s: failed to read CMD_REG register\n",
			__func__);
		goto done;
	}

	/* is data new & valid */
	if (!(ret & TSL2583_STA_ADC_INTR)) {
		dev_err(&chip->client->dev, "%s: data not valid; returning last value\n",
			__func__);
		ret = chip->als_cur_info.lux; /* return LAST VALUE */
		goto done;
	}

	for (i = 0; i < 4; i++) {
		int reg = TSL2583_CMD_REG | (TSL2583_ALS_CHAN0LO + i);

		ret = i2c_smbus_read_byte_data(chip->client, reg);
		if (ret < 0) {
			dev_err(&chip->client->dev, "%s: failed to read register %x\n",
				__func__, reg);
			goto done;
		}
		buf[i] = ret;
	}

	/*
	 * Clear the pending interrupt status bit on the chip to allow the next
	 * integration cycle to start. This has to be done even though this
	 * driver currently does not support interrupts.
	 */
	ret = i2c_smbus_write_byte(chip->client,
				   (TSL2583_CMD_REG | TSL2583_CMD_SPL_FN |
				    TSL2583_CMD_ALS_INT_CLR));
	if (ret < 0) {
		dev_err(&chip->client->dev, "%s: failed to clear the interrupt bit\n",
			__func__);
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
		/*
		 * The sensor appears to be in total darkness so set the
		 * calculated lux to 0 and return early to avoid a division by
		 * zero below when calculating the ratio.
		 */
		ret = 0;
		chip->als_cur_info.lux = 0;
		goto done;
	}

	/* calculate ratio */
	ratio = (ch1 << 15) / ch0;

	/* convert to unscaled lux using the pointer to the table */
	for (p = (struct tsl2583_lux *)chip->als_settings.als_device_lux;
	     p->ratio != 0 && p->ratio < ratio; p++)
		;

	if (p->ratio == 0) {
		lux = 0;
	} else {
		u32 ch0lux, ch1lux;

		ch0lux = ((ch0 * p->ch0) +
			  (gainadj[chip->als_settings.als_gain].ch0 >> 1))
			 / gainadj[chip->als_settings.als_gain].ch0;
		ch1lux = ((ch1 * p->ch1) +
			  (gainadj[chip->als_settings.als_gain].ch1 >> 1))
			 / gainadj[chip->als_settings.als_gain].ch1;

		/* note: lux is 31 bit max at this point */
		if (ch1lux > ch0lux) {
			dev_dbg(&chip->client->dev, "%s: No Data - Returning 0\n",
				__func__);
			ret = 0;
			chip->als_cur_info.lux = 0;
			goto done;
		}

		lux = ch0lux - ch1lux;
	}

	/* adjust for active time scale */
	if (chip->als_time_scale == 0)
		lux = 0;
	else
		lux = (lux + (chip->als_time_scale >> 1)) /
			chip->als_time_scale;

	/*
	 * Adjust for active gain scale.
	 * The tsl2583_default_lux tables above have a factor of 8192 built in,
	 * so we need to shift right.
	 * User-specified gain provides a multiplier.
	 * Apply user-specified gain before shifting right to retain precision.
	 * Use 64 bits to avoid overflow on multiplication.
	 * Then go back to 32 bits before division to avoid using div_u64().
	 */
	lux64 = lux;
	lux64 = lux64 * chip->als_settings.als_gain_trim;
	lux64 >>= 13;
	lux = lux64;
	lux = DIV_ROUND_CLOSEST(lux, 1000);

	if (lux > TSL2583_LUX_CALC_OVER_FLOW) { /* check for overflow */
return_max:
		lux = TSL2583_LUX_CALC_OVER_FLOW;
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
static int tsl2583_als_calibrate(struct iio_dev *indio_dev)
{
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	unsigned int gain_trim_val;
	int ret;
	int lux_val;

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2583_CMD_REG | TSL2583_CNTRL);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to read from the CNTRL register\n",
			__func__);
		return ret;
	}

	if ((ret & (TSL2583_CNTL_ADC_ENBL | TSL2583_CNTL_PWR_ON))
			!= (TSL2583_CNTL_ADC_ENBL | TSL2583_CNTL_PWR_ON)) {
		dev_err(&chip->client->dev,
			"%s: Device is not powered on and/or ADC is not enabled\n",
			__func__);
		return -EINVAL;
	} else if ((ret & TSL2583_STA_ADC_VALID) != TSL2583_STA_ADC_VALID) {
		dev_err(&chip->client->dev,
			"%s: The two ADC channels have not completed an integration cycle\n",
			__func__);
		return -ENODATA;
	}

	lux_val = tsl2583_get_lux(indio_dev);
	if (lux_val < 0) {
		dev_err(&chip->client->dev, "%s: failed to get lux\n",
			__func__);
		return lux_val;
	}

	gain_trim_val = (unsigned int)(((chip->als_settings.als_cal_target)
			* chip->als_settings.als_gain_trim) / lux_val);
	if ((gain_trim_val < 250) || (gain_trim_val > 4000)) {
		dev_err(&chip->client->dev,
			"%s: trim_val of %d is not within the range [250, 4000]\n",
			__func__, gain_trim_val);
		return -ENODATA;
	}

	chip->als_settings.als_gain_trim = (int)gain_trim_val;

	return 0;
}

static int tsl2583_set_als_time(struct tsl2583_chip *chip)
{
	int als_count, als_time, ret;
	u8 val;

	/* determine als integration register */
	als_count = DIV_ROUND_CLOSEST(chip->als_settings.als_time * 100, 270);
	if (!als_count)
		als_count = 1; /* ensure at least one cycle */

	/* convert back to time (encompasses overrides) */
	als_time = DIV_ROUND_CLOSEST(als_count * 27, 10);

	val = 256 - als_count;
	ret = i2c_smbus_write_byte_data(chip->client,
					TSL2583_CMD_REG | TSL2583_ALS_TIME,
					val);
	if (ret < 0) {
		dev_err(&chip->client->dev, "%s: failed to set the als time to %d\n",
			__func__, val);
		return ret;
	}

	/* set chip struct re scaling and saturation */
	chip->als_saturation = als_count * 922; /* 90% of full scale */
	chip->als_time_scale = DIV_ROUND_CLOSEST(als_time, 50);

	return ret;
}

static int tsl2583_set_als_gain(struct tsl2583_chip *chip)
{
	int ret;

	/* Set the gain based on als_settings struct */
	ret = i2c_smbus_write_byte_data(chip->client,
					TSL2583_CMD_REG | TSL2583_GAIN,
					chip->als_settings.als_gain);
	if (ret < 0)
		dev_err(&chip->client->dev,
			"%s: failed to set the gain to %d\n", __func__,
			chip->als_settings.als_gain);

	return ret;
}

static int tsl2583_set_power_state(struct tsl2583_chip *chip, u8 state)
{
	int ret;

	ret = i2c_smbus_write_byte_data(chip->client,
					TSL2583_CMD_REG | TSL2583_CNTRL, state);
	if (ret < 0)
		dev_err(&chip->client->dev,
			"%s: failed to set the power state to %d\n", __func__,
			state);

	return ret;
}

/*
 * Turn the device on.
 * Configuration must be set before calling this function.
 */
static int tsl2583_chip_init_and_power_on(struct iio_dev *indio_dev)
{
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int ret;

	/* Power on the device; ADC off. */
	ret = tsl2583_set_power_state(chip, TSL2583_CNTL_PWR_ON);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(chip->client,
					TSL2583_CMD_REG | TSL2583_INTERRUPT,
					TSL2583_INTERRUPT_DISABLED);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to disable interrupts\n", __func__);
		return ret;
	}

	ret = tsl2583_set_als_time(chip);
	if (ret < 0)
		return ret;

	ret = tsl2583_set_als_gain(chip);
	if (ret < 0)
		return ret;

	usleep_range(3000, 3500);

	ret = tsl2583_set_power_state(chip, TSL2583_CNTL_PWR_ON |
					    TSL2583_CNTL_ADC_ENBL);
	if (ret < 0)
		return ret;

	return ret;
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
	ret = sprintf(buf, "%d\n", chip->als_settings.als_cal_target);
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
	chip->als_settings.als_cal_target = value;
	mutex_unlock(&chip->als_mutex);

	return len;
}

static ssize_t in_illuminance_calibrate_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int value, ret;

	if (kstrtoint(buf, 0, &value) || value != 1)
		return -EINVAL;

	mutex_lock(&chip->als_mutex);

	ret = tsl2583_als_calibrate(indio_dev);
	if (ret < 0)
		goto done;

	ret = len;
done:
	mutex_unlock(&chip->als_mutex);

	return ret;
}

static ssize_t in_illuminance_lux_table_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	unsigned int i;
	int offset = 0;

	for (i = 0; i < ARRAY_SIZE(chip->als_settings.als_device_lux); i++) {
		offset += sprintf(buf + offset, "%u,%u,%u,",
				  chip->als_settings.als_device_lux[i].ratio,
				  chip->als_settings.als_device_lux[i].ch0,
				  chip->als_settings.als_device_lux[i].ch1);
		if (chip->als_settings.als_device_lux[i].ratio == 0) {
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
	const unsigned int max_ints = TSL2583_MAX_LUX_TABLE_ENTRIES * 3;
	int value[TSL2583_MAX_LUX_TABLE_ENTRIES * 3 + 1];
	int ret = -EINVAL;
	unsigned int n;

	mutex_lock(&chip->als_mutex);

	get_options(buf, ARRAY_SIZE(value), value);

	/*
	 * We now have an array of ints starting at value[1], and
	 * enumerated by value[0].
	 * We expect each group of three ints is one table entry,
	 * and the last table entry is all 0.
	 */
	n = value[0];
	if ((n % 3) || n < 6 || n > max_ints) {
		dev_err(dev,
			"%s: The number of entries in the lux table must be a multiple of 3 and within the range [6, %d]\n",
			__func__, max_ints);
		goto done;
	}
	if ((value[n - 2] | value[n - 1] | value[n]) != 0) {
		dev_err(dev, "%s: The last 3 entries in the lux table must be zeros.\n",
			__func__);
		goto done;
	}

	memcpy(chip->als_settings.als_device_lux, &value[1],
	       value[0] * sizeof(value[1]));

	ret = len;

done:
	mutex_unlock(&chip->als_mutex);

	return ret;
}

static IIO_CONST_ATTR(in_illuminance_calibscale_available, "1 8 16 111");
static IIO_CONST_ATTR(in_illuminance_integration_time_available,
		      "0.050 0.100 0.150 0.200 0.250 0.300 0.350 0.400 0.450 0.500 0.550 0.600 0.650");
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

static int tsl2583_set_pm_runtime_busy(struct tsl2583_chip *chip, bool on)
{
	int ret;

	if (on) {
		ret = pm_runtime_get_sync(&chip->client->dev);
		if (ret < 0)
			pm_runtime_put_noidle(&chip->client->dev);
	} else {
		pm_runtime_mark_last_busy(&chip->client->dev);
		ret = pm_runtime_put_autosuspend(&chip->client->dev);
	}

	return ret;
}

static int tsl2583_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int ret, pm_ret;

	ret = tsl2583_set_pm_runtime_busy(chip, true);
	if (ret < 0)
		return ret;

	mutex_lock(&chip->als_mutex);

	ret = -EINVAL;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_LIGHT) {
			ret = tsl2583_get_lux(indio_dev);
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
			ret = tsl2583_get_lux(indio_dev);
			if (ret < 0)
				goto read_done;

			*val = ret;
			ret = IIO_VAL_INT;
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		if (chan->type == IIO_LIGHT) {
			*val = chip->als_settings.als_gain_trim;
			ret = IIO_VAL_INT;
		}
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_LIGHT) {
			*val = gainadj[chip->als_settings.als_gain].mean;
			ret = IIO_VAL_INT;
		}
		break;
	case IIO_CHAN_INFO_INT_TIME:
		if (chan->type == IIO_LIGHT) {
			*val = 0;
			*val2 = chip->als_settings.als_time;
			ret = IIO_VAL_INT_PLUS_MICRO;
		}
		break;
	default:
		break;
	}

read_done:
	mutex_unlock(&chip->als_mutex);

	if (ret < 0)
		return ret;

	/*
	 * Preserve the ret variable if the call to
	 * tsl2583_set_pm_runtime_busy() is successful so the reading
	 * (if applicable) is returned to user space.
	 */
	pm_ret = tsl2583_set_pm_runtime_busy(chip, false);
	if (pm_ret < 0)
		return pm_ret;

	return ret;
}

static int tsl2583_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int ret;

	ret = tsl2583_set_pm_runtime_busy(chip, true);
	if (ret < 0)
		return ret;

	mutex_lock(&chip->als_mutex);

	ret = -EINVAL;
	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		if (chan->type == IIO_LIGHT) {
			chip->als_settings.als_gain_trim = val;
			ret = 0;
		}
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_LIGHT) {
			unsigned int i;

			for (i = 0; i < ARRAY_SIZE(gainadj); i++) {
				if (gainadj[i].mean == val) {
					chip->als_settings.als_gain = i;
					ret = tsl2583_set_als_gain(chip);
					break;
				}
			}
		}
		break;
	case IIO_CHAN_INFO_INT_TIME:
		if (chan->type == IIO_LIGHT && !val && val2 >= 50 &&
		    val2 <= 650 && !(val2 % 50)) {
			chip->als_settings.als_time = val2;
			ret = tsl2583_set_als_time(chip);
		}
		break;
	default:
		break;
	}

	mutex_unlock(&chip->als_mutex);

	if (ret < 0)
		return ret;

	ret = tsl2583_set_pm_runtime_busy(chip, false);
	if (ret < 0)
		return ret;

	return ret;
}

static const struct iio_info tsl2583_info = {
	.attrs = &tsl2583_attribute_group,
	.read_raw = tsl2583_read_raw,
	.write_raw = tsl2583_write_raw,
};

static int tsl2583_probe(struct i2c_client *clientp,
			 const struct i2c_device_id *idp)
{
	int ret;
	struct tsl2583_chip *chip;
	struct iio_dev *indio_dev;

	if (!i2c_check_functionality(clientp->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&clientp->dev, "%s: i2c smbus byte data functionality is unsupported\n",
			__func__);
		return -EOPNOTSUPP;
	}

	indio_dev = devm_iio_device_alloc(&clientp->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	chip->client = clientp;
	i2c_set_clientdata(clientp, indio_dev);

	mutex_init(&chip->als_mutex);

	ret = i2c_smbus_read_byte_data(clientp,
				       TSL2583_CMD_REG | TSL2583_CHIPID);
	if (ret < 0) {
		dev_err(&clientp->dev,
			"%s: failed to read the chip ID register\n", __func__);
		return ret;
	}

	if ((ret & TSL2583_CHIP_ID_MASK) != TSL2583_CHIP_ID) {
		dev_err(&clientp->dev, "%s: received an unknown chip ID %x\n",
			__func__, ret);
		return -EINVAL;
	}

	indio_dev->info = &tsl2583_info;
	indio_dev->channels = tsl2583_channels;
	indio_dev->num_channels = ARRAY_SIZE(tsl2583_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = chip->client->name;

	pm_runtime_enable(&clientp->dev);
	pm_runtime_set_autosuspend_delay(&clientp->dev,
					 TSL2583_POWER_OFF_DELAY_MS);
	pm_runtime_use_autosuspend(&clientp->dev);

	ret = devm_iio_device_register(indio_dev->dev.parent, indio_dev);
	if (ret) {
		dev_err(&clientp->dev, "%s: iio registration failed\n",
			__func__);
		return ret;
	}

	/* Load up the V2 defaults (these are hard coded defaults for now) */
	tsl2583_defaults(chip);

	dev_info(&clientp->dev, "Light sensor found.\n");

	return 0;
}

static int tsl2583_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct tsl2583_chip *chip = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	return tsl2583_set_power_state(chip, TSL2583_CNTL_PWR_OFF);
}

static int __maybe_unused tsl2583_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int ret;

	mutex_lock(&chip->als_mutex);

	ret = tsl2583_set_power_state(chip, TSL2583_CNTL_PWR_OFF);

	mutex_unlock(&chip->als_mutex);

	return ret;
}

static int __maybe_unused tsl2583_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct tsl2583_chip *chip = iio_priv(indio_dev);
	int ret;

	mutex_lock(&chip->als_mutex);

	ret = tsl2583_chip_init_and_power_on(indio_dev);

	mutex_unlock(&chip->als_mutex);

	return ret;
}

static const struct dev_pm_ops tsl2583_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(tsl2583_suspend, tsl2583_resume, NULL)
};

static const struct i2c_device_id tsl2583_idtable[] = {
	{ "tsl2580", 0 },
	{ "tsl2581", 1 },
	{ "tsl2583", 2 },
	{}
};
MODULE_DEVICE_TABLE(i2c, tsl2583_idtable);

static const struct of_device_id tsl2583_of_match[] = {
	{ .compatible = "amstaos,tsl2580", },
	{ .compatible = "amstaos,tsl2581", },
	{ .compatible = "amstaos,tsl2583", },
	{ },
};
MODULE_DEVICE_TABLE(of, tsl2583_of_match);

/* Driver definition */
static struct i2c_driver tsl2583_driver = {
	.driver = {
		.name = "tsl2583",
		.pm = &tsl2583_pm_ops,
		.of_match_table = tsl2583_of_match,
	},
	.id_table = tsl2583_idtable,
	.probe = tsl2583_probe,
	.remove = tsl2583_remove,
};
module_i2c_driver(tsl2583_driver);

MODULE_AUTHOR("J. August Brenner <jbrenner@taosinc.com>");
MODULE_AUTHOR("Brian Masney <masneyb@onstation.org>");
MODULE_DESCRIPTION("TAOS tsl2583 ambient light sensor driver");
MODULE_LICENSE("GPL");
