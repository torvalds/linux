/*
 * AD7746 capacitive sensor driver supporting AD7745, AD7746 and AD7747
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/stat.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "ad7746.h"

/*
 * AD7746 Register Definition
 */

#define AD7746_REG_STATUS		0
#define AD7746_REG_CAP_DATA_HIGH	1
#define AD7746_REG_VT_DATA_HIGH		4
#define AD7746_REG_CAP_SETUP		7
#define AD7746_REG_VT_SETUP		8
#define AD7746_REG_EXC_SETUP		9
#define AD7746_REG_CFG			10
#define AD7746_REG_CAPDACA		11
#define AD7746_REG_CAPDACB		12
#define AD7746_REG_CAP_OFFH		13
#define AD7746_REG_CAP_GAINH		15
#define AD7746_REG_VOLT_GAINH		17

/* Status Register Bit Designations (AD7746_REG_STATUS) */
#define AD7746_STATUS_EXCERR		BIT(3)
#define AD7746_STATUS_RDY		BIT(2)
#define AD7746_STATUS_RDYVT		BIT(1)
#define AD7746_STATUS_RDYCAP		BIT(0)

/* Capacitive Channel Setup Register Bit Designations (AD7746_REG_CAP_SETUP) */
#define AD7746_CAPSETUP_CAPEN		BIT(7)
#define AD7746_CAPSETUP_CIN2		BIT(6) /* AD7746 only */
#define AD7746_CAPSETUP_CAPDIFF		BIT(5)
#define AD7746_CAPSETUP_CACHOP		BIT(0)

/* Voltage/Temperature Setup Register Bit Designations (AD7746_REG_VT_SETUP) */
#define AD7746_VTSETUP_VTEN		(1 << 7)
#define AD7746_VTSETUP_VTMD_INT_TEMP	(0 << 5)
#define AD7746_VTSETUP_VTMD_EXT_TEMP	(1 << 5)
#define AD7746_VTSETUP_VTMD_VDD_MON	(2 << 5)
#define AD7746_VTSETUP_VTMD_EXT_VIN	(3 << 5)
#define AD7746_VTSETUP_EXTREF		BIT(4)
#define AD7746_VTSETUP_VTSHORT		BIT(1)
#define AD7746_VTSETUP_VTCHOP		BIT(0)

/* Excitation Setup Register Bit Designations (AD7746_REG_EXC_SETUP) */
#define AD7746_EXCSETUP_CLKCTRL		BIT(7)
#define AD7746_EXCSETUP_EXCON		BIT(6)
#define AD7746_EXCSETUP_EXCB		BIT(5)
#define AD7746_EXCSETUP_NEXCB		BIT(4)
#define AD7746_EXCSETUP_EXCA		BIT(3)
#define AD7746_EXCSETUP_NEXCA		BIT(2)
#define AD7746_EXCSETUP_EXCLVL(x)	(((x) & 0x3) << 0)

/* Config Register Bit Designations (AD7746_REG_CFG) */
#define AD7746_CONF_VTFS_SHIFT		6
#define AD7746_CONF_CAPFS_SHIFT		3
#define AD7746_CONF_VTFS_MASK		GENMASK(7, 6)
#define AD7746_CONF_CAPFS_MASK		GENMASK(5, 3)
#define AD7746_CONF_MODE_IDLE		(0 << 0)
#define AD7746_CONF_MODE_CONT_CONV	(1 << 0)
#define AD7746_CONF_MODE_SINGLE_CONV	(2 << 0)
#define AD7746_CONF_MODE_PWRDN		(3 << 0)
#define AD7746_CONF_MODE_OFFS_CAL	(5 << 0)
#define AD7746_CONF_MODE_GAIN_CAL	(6 << 0)

/* CAPDAC Register Bit Designations (AD7746_REG_CAPDACx) */
#define AD7746_CAPDAC_DACEN		BIT(7)
#define AD7746_CAPDAC_DACP(x)		((x) & 0x7F)

/*
 * struct ad7746_chip_info - chip specific information
 */

struct ad7746_chip_info {
	struct i2c_client *client;
	struct mutex lock; /* protect sensor state */
	/*
	 * Capacitive channel digital filter setup;
	 * conversion time/update rate setup per channel
	 */
	u8	config;
	u8	cap_setup;
	u8	vt_setup;
	u8	capdac[2][2];
	s8	capdac_set;

	union {
		__be32 d32;
		u8 d8[4];
	} data ____cacheline_aligned;
};

enum ad7746_chan {
	VIN,
	VIN_VDD,
	TEMP_INT,
	TEMP_EXT,
	CIN1,
	CIN1_DIFF,
	CIN2,
	CIN2_DIFF,
};

static const struct iio_chan_spec ad7746_channels[] = {
	[VIN] = {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = AD7746_REG_VT_DATA_HIGH << 8 |
			AD7746_VTSETUP_VTMD_EXT_VIN,
	},
	[VIN_VDD] = {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.extend_name = "supply",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = AD7746_REG_VT_DATA_HIGH << 8 |
			AD7746_VTSETUP_VTMD_VDD_MON,
	},
	[TEMP_INT] = {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.address = AD7746_REG_VT_DATA_HIGH << 8 |
			AD7746_VTSETUP_VTMD_INT_TEMP,
	},
	[TEMP_EXT] = {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.address = AD7746_REG_VT_DATA_HIGH << 8 |
			AD7746_VTSETUP_VTMD_EXT_TEMP,
	},
	[CIN1] = {
		.type = IIO_CAPACITANCE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) | BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = AD7746_REG_CAP_DATA_HIGH << 8,
	},
	[CIN1_DIFF] = {
		.type = IIO_CAPACITANCE,
		.differential = 1,
		.indexed = 1,
		.channel = 0,
		.channel2 = 2,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) | BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = AD7746_REG_CAP_DATA_HIGH << 8 |
			AD7746_CAPSETUP_CAPDIFF
	},
	[CIN2] = {
		.type = IIO_CAPACITANCE,
		.indexed = 1,
		.channel = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) | BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = AD7746_REG_CAP_DATA_HIGH << 8 |
			AD7746_CAPSETUP_CIN2,
	},
	[CIN2_DIFF] = {
		.type = IIO_CAPACITANCE,
		.differential = 1,
		.indexed = 1,
		.channel = 1,
		.channel2 = 3,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) | BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = AD7746_REG_CAP_DATA_HIGH << 8 |
			AD7746_CAPSETUP_CAPDIFF | AD7746_CAPSETUP_CIN2,
	}
};

/* Values are Update Rate (Hz), Conversion Time (ms) + 1*/
static const unsigned char ad7746_vt_filter_rate_table[][2] = {
	{50, 20 + 1}, {31, 32 + 1}, {16, 62 + 1}, {8, 122 + 1},
};

static const unsigned char ad7746_cap_filter_rate_table[][2] = {
	{91, 11 + 1}, {84, 12 + 1}, {50, 20 + 1}, {26, 38 + 1},
	{16, 62 + 1}, {13, 77 + 1}, {11, 92 + 1}, {9, 110 + 1},
};

static int ad7746_select_channel(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan)
{
	struct ad7746_chip_info *chip = iio_priv(indio_dev);
	int ret, delay, idx;
	u8 vt_setup, cap_setup;

	switch (chan->type) {
	case IIO_CAPACITANCE:
		cap_setup = (chan->address & 0xFF) | AD7746_CAPSETUP_CAPEN;
		vt_setup = chip->vt_setup & ~AD7746_VTSETUP_VTEN;
		idx = (chip->config & AD7746_CONF_CAPFS_MASK) >>
			AD7746_CONF_CAPFS_SHIFT;
		delay = ad7746_cap_filter_rate_table[idx][1];

		if (chip->capdac_set != chan->channel) {
			ret = i2c_smbus_write_byte_data(chip->client,
				AD7746_REG_CAPDACA,
				chip->capdac[chan->channel][0]);
			if (ret < 0)
				return ret;
			ret = i2c_smbus_write_byte_data(chip->client,
				AD7746_REG_CAPDACB,
				chip->capdac[chan->channel][1]);
			if (ret < 0)
				return ret;

			chip->capdac_set = chan->channel;
		}
		break;
	case IIO_VOLTAGE:
	case IIO_TEMP:
		vt_setup = (chan->address & 0xFF) | AD7746_VTSETUP_VTEN;
		cap_setup = chip->cap_setup & ~AD7746_CAPSETUP_CAPEN;
		idx = (chip->config & AD7746_CONF_VTFS_MASK) >>
			AD7746_CONF_VTFS_SHIFT;
		delay = ad7746_cap_filter_rate_table[idx][1];
		break;
	default:
		return -EINVAL;
	}

	if (chip->cap_setup != cap_setup) {
		ret = i2c_smbus_write_byte_data(chip->client,
						AD7746_REG_CAP_SETUP,
						cap_setup);
		if (ret < 0)
			return ret;

		chip->cap_setup = cap_setup;
	}

	if (chip->vt_setup != vt_setup) {
		ret = i2c_smbus_write_byte_data(chip->client,
						AD7746_REG_VT_SETUP,
						vt_setup);
		if (ret < 0)
			return ret;

		chip->vt_setup = vt_setup;
	}

	return delay;
}

static inline ssize_t ad7746_start_calib(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len,
					 u8 regval)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7746_chip_info *chip = iio_priv(indio_dev);
	bool doit;
	int ret, timeout = 10;

	ret = strtobool(buf, &doit);
	if (ret < 0)
		return ret;

	if (!doit)
		return 0;

	mutex_lock(&chip->lock);
	regval |= chip->config;
	ret = i2c_smbus_write_byte_data(chip->client, AD7746_REG_CFG, regval);
	if (ret < 0)
		goto unlock;

	do {
		msleep(20);
		ret = i2c_smbus_read_byte_data(chip->client, AD7746_REG_CFG);
		if (ret < 0)
			goto unlock;

	} while ((ret == regval) && timeout--);

	mutex_unlock(&chip->lock);

	return len;

unlock:
	mutex_unlock(&chip->lock);
	return ret;
}

static ssize_t ad7746_start_offset_calib(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	int ret = ad7746_select_channel(indio_dev,
			      &ad7746_channels[to_iio_dev_attr(attr)->address]);
	if (ret < 0)
		return ret;

	return ad7746_start_calib(dev, attr, buf, len,
				  AD7746_CONF_MODE_OFFS_CAL);
}

static ssize_t ad7746_start_gain_calib(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	int ret = ad7746_select_channel(indio_dev,
			      &ad7746_channels[to_iio_dev_attr(attr)->address]);
	if (ret < 0)
		return ret;

	return ad7746_start_calib(dev, attr, buf, len,
				  AD7746_CONF_MODE_GAIN_CAL);
}

static IIO_DEVICE_ATTR(in_capacitance0_calibbias_calibration,
		       0200, NULL, ad7746_start_offset_calib, CIN1);
static IIO_DEVICE_ATTR(in_capacitance1_calibbias_calibration,
		       0200, NULL, ad7746_start_offset_calib, CIN2);
static IIO_DEVICE_ATTR(in_capacitance0_calibscale_calibration,
		       0200, NULL, ad7746_start_gain_calib, CIN1);
static IIO_DEVICE_ATTR(in_capacitance1_calibscale_calibration,
		       0200, NULL, ad7746_start_gain_calib, CIN2);
static IIO_DEVICE_ATTR(in_voltage0_calibscale_calibration,
		       0200, NULL, ad7746_start_gain_calib, VIN);

static int ad7746_store_cap_filter_rate_setup(struct ad7746_chip_info *chip,
					      int val)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ad7746_cap_filter_rate_table); i++)
		if (val >= ad7746_cap_filter_rate_table[i][0])
			break;

	if (i >= ARRAY_SIZE(ad7746_cap_filter_rate_table))
		i = ARRAY_SIZE(ad7746_cap_filter_rate_table) - 1;

	chip->config &= ~AD7746_CONF_CAPFS_MASK;
	chip->config |= i << AD7746_CONF_CAPFS_SHIFT;

	return 0;
}

static int ad7746_store_vt_filter_rate_setup(struct ad7746_chip_info *chip,
					     int val)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ad7746_vt_filter_rate_table); i++)
		if (val >= ad7746_vt_filter_rate_table[i][0])
			break;

	if (i >= ARRAY_SIZE(ad7746_vt_filter_rate_table))
		i = ARRAY_SIZE(ad7746_vt_filter_rate_table) - 1;

	chip->config &= ~AD7746_CONF_VTFS_MASK;
	chip->config |= i << AD7746_CONF_VTFS_SHIFT;

	return 0;
}

static IIO_CONST_ATTR(in_voltage_sampling_frequency_available, "50 31 16 8");
static IIO_CONST_ATTR(in_capacitance_sampling_frequency_available,
		       "91 84 50 26 16 13 11 9");

static struct attribute *ad7746_attributes[] = {
	&iio_dev_attr_in_capacitance0_calibbias_calibration.dev_attr.attr,
	&iio_dev_attr_in_capacitance0_calibscale_calibration.dev_attr.attr,
	&iio_dev_attr_in_capacitance1_calibscale_calibration.dev_attr.attr,
	&iio_dev_attr_in_capacitance1_calibbias_calibration.dev_attr.attr,
	&iio_dev_attr_in_voltage0_calibscale_calibration.dev_attr.attr,
	&iio_const_attr_in_voltage_sampling_frequency_available.dev_attr.attr,
	&iio_const_attr_in_capacitance_sampling_frequency_available.
	dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7746_attribute_group = {
	.attrs = ad7746_attributes,
};

static int ad7746_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad7746_chip_info *chip = iio_priv(indio_dev);
	int ret, reg;

	mutex_lock(&chip->lock);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		if (val != 1) {
			ret = -EINVAL;
			goto out;
		}

		val = (val2 * 1024) / 15625;

		switch (chan->type) {
		case IIO_CAPACITANCE:
			reg = AD7746_REG_CAP_GAINH;
			break;
		case IIO_VOLTAGE:
			reg = AD7746_REG_VOLT_GAINH;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}

		ret = i2c_smbus_write_word_data(chip->client, reg, swab16(val));
		if (ret < 0)
			goto out;

		ret = 0;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		if ((val < 0) | (val > 0xFFFF)) {
			ret = -EINVAL;
			goto out;
		}
		ret = i2c_smbus_write_word_data(chip->client,
				AD7746_REG_CAP_OFFH, swab16(val));
		if (ret < 0)
			goto out;

		ret = 0;
		break;
	case IIO_CHAN_INFO_OFFSET:
		if ((val < 0) | (val > 43008000)) { /* 21pF */
			ret = -EINVAL;
			goto out;
		}

		/*
		 * CAPDAC Scale = 21pF_typ / 127
		 * CIN Scale = 8.192pF / 2^24
		 * Offset Scale = CAPDAC Scale / CIN Scale = 338646
		 */

		val /= 338646;

		chip->capdac[chan->channel][chan->differential] = val > 0 ?
			AD7746_CAPDAC_DACP(val) | AD7746_CAPDAC_DACEN : 0;

		ret = i2c_smbus_write_byte_data(chip->client,
						AD7746_REG_CAPDACA,
						chip->capdac[chan->channel][0]);
		if (ret < 0)
			goto out;
		ret = i2c_smbus_write_byte_data(chip->client,
						AD7746_REG_CAPDACB,
						chip->capdac[chan->channel][1]);
		if (ret < 0)
			goto out;

		chip->capdac_set = chan->channel;

		ret = 0;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2) {
			ret = -EINVAL;
			goto out;
		}

		switch (chan->type) {
		case IIO_CAPACITANCE:
			ret = ad7746_store_cap_filter_rate_setup(chip, val);
			break;
		case IIO_VOLTAGE:
			ret = ad7746_store_vt_filter_rate_setup(chip, val);
			break;
		default:
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&chip->lock);
	return ret;
}

static int ad7746_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	struct ad7746_chip_info *chip = iio_priv(indio_dev);
	int ret, delay, idx;
	u8 regval, reg;

	mutex_lock(&chip->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		ret = ad7746_select_channel(indio_dev, chan);
		if (ret < 0)
			goto out;
		delay = ret;

		regval = chip->config | AD7746_CONF_MODE_SINGLE_CONV;
		ret = i2c_smbus_write_byte_data(chip->client, AD7746_REG_CFG,
						regval);
		if (ret < 0)
			goto out;

		msleep(delay);
		/* Now read the actual register */

		ret = i2c_smbus_read_i2c_block_data(chip->client,
			chan->address >> 8, 3, &chip->data.d8[1]);

		if (ret < 0)
			goto out;

		*val = (be32_to_cpu(chip->data.d32) & 0xFFFFFF) - 0x800000;

		switch (chan->type) {
		case IIO_TEMP:
		/*
		 * temperature in milli degrees Celsius
		 * T = ((*val / 2048) - 4096) * 1000
		 */
			*val = (*val * 125) / 256;
			break;
		case IIO_VOLTAGE:
			if (chan->channel == 1) /* supply_raw*/
				*val = *val * 6;
			break;
		default:
			break;
		}

		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		switch (chan->type) {
		case IIO_CAPACITANCE:
			reg = AD7746_REG_CAP_GAINH;
			break;
		case IIO_VOLTAGE:
			reg = AD7746_REG_VOLT_GAINH;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}

		ret = i2c_smbus_read_word_data(chip->client, reg);
		if (ret < 0)
			goto out;
		/* 1 + gain_val / 2^16 */
		*val = 1;
		*val2 = (15625 * swab16(ret)) / 1024;

		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = i2c_smbus_read_word_data(chip->client,
					       AD7746_REG_CAP_OFFH);
		if (ret < 0)
			goto out;
		*val = swab16(ret);

		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = AD7746_CAPDAC_DACP(chip->capdac[chan->channel]
			[chan->differential]) * 338646;

		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_CAPACITANCE:
			/* 8.192pf / 2^24 */
			*val =  0;
			*val2 = 488;
			ret = IIO_VAL_INT_PLUS_NANO;
			break;
		case IIO_VOLTAGE:
			/* 1170mV / 2^23 */
			*val = 1170;
			*val2 = 23;
			ret = IIO_VAL_FRACTIONAL_LOG2;
			break;
		default:
			ret = -EINVAL;
			break;
		}

		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_CAPACITANCE:
			idx = (chip->config & AD7746_CONF_CAPFS_MASK) >>
				AD7746_CONF_CAPFS_SHIFT;
			*val = ad7746_cap_filter_rate_table[idx][0];
			ret = IIO_VAL_INT;
			break;
		case IIO_VOLTAGE:
			idx = (chip->config & AD7746_CONF_VTFS_MASK) >>
				AD7746_CONF_VTFS_SHIFT;
			*val = ad7746_vt_filter_rate_table[idx][0];
			ret = IIO_VAL_INT;
			break;
		default:
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
	}
out:
	mutex_unlock(&chip->lock);
	return ret;
}

static const struct iio_info ad7746_info = {
	.attrs = &ad7746_attribute_group,
	.read_raw = ad7746_read_raw,
	.write_raw = ad7746_write_raw,
};

/*
 * device probe and remove
 */

static int ad7746_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ad7746_platform_data *pdata = client->dev.platform_data;
	struct ad7746_chip_info *chip;
	struct iio_dev *indio_dev;
	int ret = 0;
	unsigned char regval = 0;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;
	chip = iio_priv(indio_dev);
	mutex_init(&chip->lock);
	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, indio_dev);

	chip->client = client;
	chip->capdac_set = -1;

	/* Establish that the iio_dev is a child of the i2c device */
	indio_dev->name = id->name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ad7746_info;
	indio_dev->channels = ad7746_channels;
	if (id->driver_data == 7746)
		indio_dev->num_channels = ARRAY_SIZE(ad7746_channels);
	else
		indio_dev->num_channels =  ARRAY_SIZE(ad7746_channels) - 2;
	indio_dev->num_channels = ARRAY_SIZE(ad7746_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (pdata) {
		if (pdata->exca_en) {
			if (pdata->exca_inv_en)
				regval |= AD7746_EXCSETUP_NEXCA;
			else
				regval |= AD7746_EXCSETUP_EXCA;
		}

		if (pdata->excb_en) {
			if (pdata->excb_inv_en)
				regval |= AD7746_EXCSETUP_NEXCB;
			else
				regval |= AD7746_EXCSETUP_EXCB;
		}

		regval |= AD7746_EXCSETUP_EXCLVL(pdata->exclvl);
	} else {
		dev_warn(&client->dev, "No platform data? using default\n");
		regval = AD7746_EXCSETUP_EXCA | AD7746_EXCSETUP_EXCB |
			AD7746_EXCSETUP_EXCLVL(3);
	}

	ret = i2c_smbus_write_byte_data(chip->client,
					AD7746_REG_EXC_SETUP, regval);
	if (ret < 0)
		return ret;

	ret = devm_iio_device_register(indio_dev->dev.parent, indio_dev);
	if (ret)
		return ret;

	return 0;
}

static const struct i2c_device_id ad7746_id[] = {
	{ "ad7745", 7745 },
	{ "ad7746", 7746 },
	{ "ad7747", 7747 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ad7746_id);

static struct i2c_driver ad7746_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
	},
	.probe = ad7746_probe,
	.id_table = ad7746_id,
};
module_i2c_driver(ad7746_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7746/5/7 capacitive sensor driver");
MODULE_LICENSE("GPL v2");
