// SPDX-License-Identifier: GPL-2.0
/*
 * AD7746 capacitive sensor driver supporting AD7745, AD7746 and AD7747
 *
 * Copyright 2011 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/sysfs.h>

#include <linux/unaligned.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* AD7746 Register Definition */

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
#define AD7746_VTSETUP_VTEN		BIT(7)
#define AD7746_VTSETUP_VTMD_MASK	GENMASK(6, 5)
#define AD7746_VTSETUP_VTMD_INT_TEMP	0
#define AD7746_VTSETUP_VTMD_EXT_TEMP	1
#define AD7746_VTSETUP_VTMD_VDD_MON	2
#define AD7746_VTSETUP_VTMD_EXT_VIN	3
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
#define AD7746_EXCSETUP_EXCLVL_MASK	GENMASK(1, 0)

/* Config Register Bit Designations (AD7746_REG_CFG) */
#define AD7746_CONF_VTFS_MASK		GENMASK(7, 6)
#define AD7746_CONF_CAPFS_MASK		GENMASK(5, 3)
#define AD7746_CONF_MODE_MASK		GENMASK(2, 0)
#define AD7746_CONF_MODE_IDLE		0
#define AD7746_CONF_MODE_CONT_CONV	1
#define AD7746_CONF_MODE_SINGLE_CONV	2
#define AD7746_CONF_MODE_PWRDN		3
#define AD7746_CONF_MODE_OFFS_CAL	5
#define AD7746_CONF_MODE_GAIN_CAL	6

/* CAPDAC Register Bit Designations (AD7746_REG_CAPDACx) */
#define AD7746_CAPDAC_DACEN		BIT(7)
#define AD7746_CAPDAC_DACP_MASK		GENMASK(6, 0)

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

struct ad7746_chan_info {
	u8 addr;
	union {
		u8 vtmd;
		struct { /* CAP SETUP fields */
			unsigned int cin2 : 1;
			unsigned int capdiff : 1;
		};
	};
};

static const struct ad7746_chan_info ad7746_chan_info[] = {
	[VIN] = {
		.addr = AD7746_REG_VT_DATA_HIGH,
		.vtmd = AD7746_VTSETUP_VTMD_EXT_VIN,
	},
	[VIN_VDD] = {
		.addr = AD7746_REG_VT_DATA_HIGH,
		.vtmd = AD7746_VTSETUP_VTMD_VDD_MON,
	},
	[TEMP_INT] = {
		.addr = AD7746_REG_VT_DATA_HIGH,
		.vtmd = AD7746_VTSETUP_VTMD_INT_TEMP,
	},
	[TEMP_EXT] = {
		.addr = AD7746_REG_VT_DATA_HIGH,
		.vtmd = AD7746_VTSETUP_VTMD_EXT_TEMP,
	},
	[CIN1] = {
		.addr = AD7746_REG_CAP_DATA_HIGH,
	},
	[CIN1_DIFF] = {
		.addr =  AD7746_REG_CAP_DATA_HIGH,
		.capdiff = 1,
	},
	[CIN2] = {
		.addr = AD7746_REG_CAP_DATA_HIGH,
		.cin2 = 1,
	},
	[CIN2_DIFF] = {
		.addr = AD7746_REG_CAP_DATA_HIGH,
		.cin2 = 1,
		.capdiff = 1,
	},
};

static const struct iio_chan_spec ad7746_channels[] = {
	[VIN] = {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = VIN,
	},
	[VIN_VDD] = {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.extend_name = "supply",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = VIN_VDD,
	},
	[TEMP_INT] = {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = TEMP_INT,
	},
	[TEMP_EXT] = {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.address = TEMP_EXT,
	},
	[CIN1] = {
		.type = IIO_CAPACITANCE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) | BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = CIN1,
	},
	[CIN1_DIFF] = {
		.type = IIO_CAPACITANCE,
		.differential = 1,
		.indexed = 1,
		.channel = 0,
		.channel2 = 2,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) | BIT(IIO_CHAN_INFO_ZEROPOINT),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = CIN1_DIFF,
	},
	[CIN2] = {
		.type = IIO_CAPACITANCE,
		.indexed = 1,
		.channel = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) | BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = CIN2,
	},
	[CIN2_DIFF] = {
		.type = IIO_CAPACITANCE,
		.differential = 1,
		.indexed = 1,
		.channel = 1,
		.channel2 = 3,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE) | BIT(IIO_CHAN_INFO_ZEROPOINT),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBBIAS) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = CIN2_DIFF,
	}
};

/* Values are Update Rate (Hz), Conversion Time (ms) + 1*/
static const unsigned char ad7746_vt_filter_rate_table[][2] = {
	{ 50, 20 + 1 }, { 31, 32 + 1 }, { 16, 62 + 1 }, { 8, 122 + 1 },
};

static const unsigned char ad7746_cap_filter_rate_table[][2] = {
	{ 91, 11 + 1 }, { 84, 12 + 1 }, { 50, 20 + 1 }, { 26, 38 + 1 },
	{ 16, 62 + 1 }, { 13, 77 + 1 }, { 11, 92 + 1 }, { 9, 110 + 1 },
};

static int ad7746_set_capdac(struct ad7746_chip_info *chip, int channel)
{
	int ret = i2c_smbus_write_byte_data(chip->client,
					    AD7746_REG_CAPDACA,
					    chip->capdac[channel][0]);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(chip->client,
					  AD7746_REG_CAPDACB,
					  chip->capdac[channel][1]);
}

static int ad7746_select_channel(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan)
{
	struct ad7746_chip_info *chip = iio_priv(indio_dev);
	u8 vt_setup, cap_setup;
	int ret, delay, idx;

	switch (chan->type) {
	case IIO_CAPACITANCE:
		cap_setup = FIELD_PREP(AD7746_CAPSETUP_CIN2,
				       ad7746_chan_info[chan->address].cin2) |
			FIELD_PREP(AD7746_CAPSETUP_CAPDIFF,
				   ad7746_chan_info[chan->address].capdiff) |
			FIELD_PREP(AD7746_CAPSETUP_CAPEN, 1);
		vt_setup = chip->vt_setup & ~AD7746_VTSETUP_VTEN;
		idx = FIELD_GET(AD7746_CONF_CAPFS_MASK, chip->config);
		delay = ad7746_cap_filter_rate_table[idx][1];

		ret = ad7746_set_capdac(chip, chan->channel);
		if (ret < 0)
			return ret;

		chip->capdac_set = chan->channel;
		break;
	case IIO_VOLTAGE:
	case IIO_TEMP:
		vt_setup = FIELD_PREP(AD7746_VTSETUP_VTMD_MASK,
				      ad7746_chan_info[chan->address].vtmd) |
			FIELD_PREP(AD7746_VTSETUP_VTEN, 1);
		cap_setup = chip->cap_setup & ~AD7746_CAPSETUP_CAPEN;
		idx = FIELD_GET(AD7746_CONF_VTFS_MASK, chip->config);
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
	int ret, timeout = 10;
	bool doit;

	ret = kstrtobool(buf, &doit);
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
				  FIELD_PREP(AD7746_CONF_MODE_MASK,
					     AD7746_CONF_MODE_OFFS_CAL));
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
				  FIELD_PREP(AD7746_CONF_MODE_MASK,
					     AD7746_CONF_MODE_GAIN_CAL));
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
	chip->config |= FIELD_PREP(AD7746_CONF_CAPFS_MASK, i);

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
	chip->config |= FIELD_PREP(AD7746_CONF_VTFS_MASK, i);

	return 0;
}

static struct attribute *ad7746_attributes[] = {
	&iio_dev_attr_in_capacitance0_calibbias_calibration.dev_attr.attr,
	&iio_dev_attr_in_capacitance0_calibscale_calibration.dev_attr.attr,
	&iio_dev_attr_in_capacitance1_calibscale_calibration.dev_attr.attr,
	&iio_dev_attr_in_capacitance1_calibbias_calibration.dev_attr.attr,
	&iio_dev_attr_in_voltage0_calibscale_calibration.dev_attr.attr,
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

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		if (val != 1)
			return -EINVAL;

		val = (val2 * 1024) / 15625;

		switch (chan->type) {
		case IIO_CAPACITANCE:
			reg = AD7746_REG_CAP_GAINH;
			break;
		case IIO_VOLTAGE:
			reg = AD7746_REG_VOLT_GAINH;
			break;
		default:
			return -EINVAL;
		}

		mutex_lock(&chip->lock);
		ret = i2c_smbus_write_word_swapped(chip->client, reg, val);
		mutex_unlock(&chip->lock);
		if (ret < 0)
			return ret;

		return 0;
	case IIO_CHAN_INFO_CALIBBIAS:
		if (val < 0 || val > 0xFFFF)
			return -EINVAL;

		mutex_lock(&chip->lock);
		ret = i2c_smbus_write_word_swapped(chip->client,
						   AD7746_REG_CAP_OFFH, val);
		mutex_unlock(&chip->lock);
		if (ret < 0)
			return ret;

		return 0;
	case IIO_CHAN_INFO_OFFSET:
	case IIO_CHAN_INFO_ZEROPOINT:
		if (val < 0 || val > 43008000) /* 21pF */
			return -EINVAL;

		/*
		 * CAPDAC Scale = 21pF_typ / 127
		 * CIN Scale = 8.192pF / 2^24
		 * Offset Scale = CAPDAC Scale / CIN Scale = 338646
		 */

		val /= 338646;
		mutex_lock(&chip->lock);
		chip->capdac[chan->channel][chan->differential] = val > 0 ?
			FIELD_PREP(AD7746_CAPDAC_DACP_MASK, val) | AD7746_CAPDAC_DACEN : 0;

		ret = ad7746_set_capdac(chip, chan->channel);
		if (ret < 0) {
			mutex_unlock(&chip->lock);
			return ret;
		}

		chip->capdac_set = chan->channel;
		mutex_unlock(&chip->lock);

		return 0;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2)
			return -EINVAL;

		switch (chan->type) {
		case IIO_CAPACITANCE:
			mutex_lock(&chip->lock);
			ret = ad7746_store_cap_filter_rate_setup(chip, val);
			mutex_unlock(&chip->lock);
			return ret;
		case IIO_VOLTAGE:
			mutex_lock(&chip->lock);
			ret = ad7746_store_vt_filter_rate_setup(chip, val);
			mutex_unlock(&chip->lock);
			return ret;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const int ad7746_v_samp_freq[] = { 50, 31, 16, 8, };
static const int ad7746_cap_samp_freq[] = { 91, 84, 50, 26, 16, 13, 11, 9, };

static int ad7746_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, const int **vals,
			     int *type, int *length, long mask)
{
	if (mask != IIO_CHAN_INFO_SAMP_FREQ)
		return -EINVAL;

	switch (chan->type) {
	case IIO_VOLTAGE:
		*vals = ad7746_v_samp_freq;
		*length = ARRAY_SIZE(ad7746_v_samp_freq);
		break;
	case IIO_CAPACITANCE:
		*vals = ad7746_cap_samp_freq;
		*length = ARRAY_SIZE(ad7746_cap_samp_freq);
		break;
	default:
		return -EINVAL;
	}
	*type = IIO_VAL_INT;
	return IIO_AVAIL_LIST;
}

static int ad7746_read_channel(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val)
{
	struct ad7746_chip_info *chip = iio_priv(indio_dev);
	int ret, delay;
	u8 data[3];
	u8 regval;

	ret = ad7746_select_channel(indio_dev, chan);
	if (ret < 0)
		return ret;
	delay = ret;

	regval = chip->config | FIELD_PREP(AD7746_CONF_MODE_MASK,
					   AD7746_CONF_MODE_SINGLE_CONV);
	ret = i2c_smbus_write_byte_data(chip->client, AD7746_REG_CFG, regval);
	if (ret < 0)
		return ret;

	msleep(delay);
	/* Now read the actual register */
	ret = i2c_smbus_read_i2c_block_data(chip->client,
					    ad7746_chan_info[chan->address].addr,
					    sizeof(data), data);
	if (ret < 0)
		return ret;

	/*
	 * Offset applied internally becaue the _offset userspace interface is
	 * needed for the CAP DACs which apply a controllable offset.
	 */
	*val = get_unaligned_be24(data) - 0x800000;

	return 0;
}

static int ad7746_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2,
			   long mask)
{
	struct ad7746_chip_info *chip = iio_priv(indio_dev);
	int ret, idx;
	u8 reg;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&chip->lock);
		ret = ad7746_read_channel(indio_dev, chan, val);
		mutex_unlock(&chip->lock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBSCALE:
		switch (chan->type) {
		case IIO_CAPACITANCE:
			reg = AD7746_REG_CAP_GAINH;
			break;
		case IIO_VOLTAGE:
			reg = AD7746_REG_VOLT_GAINH;
			break;
		default:
			return -EINVAL;
		}

		mutex_lock(&chip->lock);
		ret = i2c_smbus_read_word_swapped(chip->client, reg);
		mutex_unlock(&chip->lock);
		if (ret < 0)
			return ret;
		/* 1 + gain_val / 2^16 */
		*val = 1;
		*val2 = (15625 * ret) / 1024;

		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBBIAS:
		mutex_lock(&chip->lock);
		ret = i2c_smbus_read_word_swapped(chip->client,
						  AD7746_REG_CAP_OFFH);
		mutex_unlock(&chip->lock);
		if (ret < 0)
			return ret;
		*val = ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
	case IIO_CHAN_INFO_ZEROPOINT:
		*val = FIELD_GET(AD7746_CAPDAC_DACP_MASK,
				 chip->capdac[chan->channel][chan->differential]) * 338646;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_CAPACITANCE:
			/* 8.192pf / 2^24 */
			*val =  0;
			*val2 = 488;
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_VOLTAGE:
			/* 1170mV / 2^23 */
			*val = 1170;
			if (chan->channel == 1)
				*val *= 6;
			*val2 = 23;
			return IIO_VAL_FRACTIONAL_LOG2;
		case IIO_TEMP:
			*val = 125;
			*val2 = 8;
			return IIO_VAL_FRACTIONAL_LOG2;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_CAPACITANCE:
			idx = FIELD_GET(AD7746_CONF_CAPFS_MASK, chip->config);
			*val = ad7746_cap_filter_rate_table[idx][0];
			return IIO_VAL_INT;
		case IIO_VOLTAGE:
			idx = FIELD_GET(AD7746_CONF_VTFS_MASK, chip->config);
			*val = ad7746_vt_filter_rate_table[idx][0];
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_info ad7746_info = {
	.attrs = &ad7746_attribute_group,
	.read_raw = ad7746_read_raw,
	.read_avail = ad7746_read_avail,
	.write_raw = ad7746_write_raw,
};

static int ad7746_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct device *dev = &client->dev;
	struct ad7746_chip_info *chip;
	struct iio_dev *indio_dev;
	unsigned char regval = 0;
	unsigned int vdd_permille;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	mutex_init(&chip->lock);

	chip->client = client;
	chip->capdac_set = -1;

	indio_dev->name = id->name;
	indio_dev->info = &ad7746_info;
	indio_dev->channels = ad7746_channels;
	if (id->driver_data == 7746)
		indio_dev->num_channels = ARRAY_SIZE(ad7746_channels);
	else
		indio_dev->num_channels =  ARRAY_SIZE(ad7746_channels) - 2;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (device_property_read_bool(dev, "adi,exca-output-en")) {
		if (device_property_read_bool(dev, "adi,exca-output-invert"))
			regval |= AD7746_EXCSETUP_NEXCA;
		else
			regval |= AD7746_EXCSETUP_EXCA;
	}

	if (device_property_read_bool(dev, "adi,excb-output-en")) {
		if (device_property_read_bool(dev, "adi,excb-output-invert"))
			regval |= AD7746_EXCSETUP_NEXCB;
		else
			regval |= AD7746_EXCSETUP_EXCB;
	}

	ret = device_property_read_u32(dev, "adi,excitation-vdd-permille",
				       &vdd_permille);
	if (!ret) {
		switch (vdd_permille) {
		case 125:
			regval |= FIELD_PREP(AD7746_EXCSETUP_EXCLVL_MASK, 0);
			break;
		case 250:
			regval |= FIELD_PREP(AD7746_EXCSETUP_EXCLVL_MASK, 1);
			break;
		case 375:
			regval |= FIELD_PREP(AD7746_EXCSETUP_EXCLVL_MASK, 2);
			break;
		case 500:
			regval |= FIELD_PREP(AD7746_EXCSETUP_EXCLVL_MASK, 3);
			break;
		default:
			break;
		}
	}

	ret = i2c_smbus_write_byte_data(chip->client, AD7746_REG_EXC_SETUP,
					regval);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(indio_dev->dev.parent, indio_dev);
}

static const struct i2c_device_id ad7746_id[] = {
	{ "ad7745", 7745 },
	{ "ad7746", 7746 },
	{ "ad7747", 7747 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ad7746_id);

static const struct of_device_id ad7746_of_match[] = {
	{ .compatible = "adi,ad7745" },
	{ .compatible = "adi,ad7746" },
	{ .compatible = "adi,ad7747" },
	{ },
};
MODULE_DEVICE_TABLE(of, ad7746_of_match);

static struct i2c_driver ad7746_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = ad7746_of_match,
	},
	.probe = ad7746_probe,
	.id_table = ad7746_id,
};
module_i2c_driver(ad7746_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7746/5/7 capacitive sensor driver");
MODULE_LICENSE("GPL v2");
