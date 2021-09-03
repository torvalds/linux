// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADXL355 3-Axis Digital Accelerometer IIO core driver
 *
 * Copyright (c) 2021 Puranjay Mohan <puranjay12@gmail.com>
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/adxl354_adxl355.pdf
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/iio/iio.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <asm/unaligned.h>

#include "adxl355.h"

/* ADXL355 Register Definitions */
#define ADXL355_DEVID_AD_REG		0x00
#define ADXL355_DEVID_MST_REG		0x01
#define ADXL355_PARTID_REG		0x02
#define ADXL355_STATUS_REG		0x04
#define ADXL355_FIFO_ENTRIES_REG	0x05
#define ADXL355_TEMP2_REG		0x06
#define ADXL355_XDATA3_REG		0x08
#define ADXL355_YDATA3_REG		0x0B
#define ADXL355_ZDATA3_REG		0x0E
#define ADXL355_FIFO_DATA_REG		0x11
#define ADXL355_OFFSET_X_H_REG		0x1E
#define ADXL355_OFFSET_Y_H_REG		0x20
#define ADXL355_OFFSET_Z_H_REG		0x22
#define ADXL355_ACT_EN_REG		0x24
#define ADXL355_ACT_THRESH_H_REG	0x25
#define ADXL355_ACT_THRESH_L_REG	0x26
#define ADXL355_ACT_COUNT_REG		0x27
#define ADXL355_FILTER_REG		0x28
#define  ADXL355_FILTER_ODR_MSK GENMASK(3, 0)
#define  ADXL355_FILTER_HPF_MSK	GENMASK(6, 4)
#define ADXL355_FIFO_SAMPLES_REG	0x29
#define ADXL355_INT_MAP_REG		0x2A
#define ADXL355_SYNC_REG		0x2B
#define ADXL355_RANGE_REG		0x2C
#define ADXL355_POWER_CTL_REG		0x2D
#define  ADXL355_POWER_CTL_MODE_MSK	GENMASK(1, 0)
#define ADXL355_SELF_TEST_REG		0x2E
#define ADXL355_RESET_REG		0x2F

#define ADXL355_DEVID_AD_VAL		0xAD
#define ADXL355_DEVID_MST_VAL		0x1D
#define ADXL355_PARTID_VAL		0xED
#define ADXL355_RESET_CODE		0x52

#define MEGA 1000000UL
#define TERA 1000000000000ULL

static const struct regmap_range adxl355_read_reg_range[] = {
	regmap_reg_range(ADXL355_DEVID_AD_REG, ADXL355_FIFO_DATA_REG),
	regmap_reg_range(ADXL355_OFFSET_X_H_REG, ADXL355_SELF_TEST_REG),
};

const struct regmap_access_table adxl355_readable_regs_tbl = {
	.yes_ranges = adxl355_read_reg_range,
	.n_yes_ranges = ARRAY_SIZE(adxl355_read_reg_range),
};
EXPORT_SYMBOL_GPL(adxl355_readable_regs_tbl);

static const struct regmap_range adxl355_write_reg_range[] = {
	regmap_reg_range(ADXL355_OFFSET_X_H_REG, ADXL355_RESET_REG),
};

const struct regmap_access_table adxl355_writeable_regs_tbl = {
	.yes_ranges = adxl355_write_reg_range,
	.n_yes_ranges = ARRAY_SIZE(adxl355_write_reg_range),
};
EXPORT_SYMBOL_GPL(adxl355_writeable_regs_tbl);

enum adxl355_op_mode {
	ADXL355_MEASUREMENT,
	ADXL355_STANDBY,
	ADXL355_TEMP_OFF,
};

enum adxl355_odr {
	ADXL355_ODR_4000HZ,
	ADXL355_ODR_2000HZ,
	ADXL355_ODR_1000HZ,
	ADXL355_ODR_500HZ,
	ADXL355_ODR_250HZ,
	ADXL355_ODR_125HZ,
	ADXL355_ODR_62_5HZ,
	ADXL355_ODR_31_25HZ,
	ADXL355_ODR_15_625HZ,
	ADXL355_ODR_7_813HZ,
	ADXL355_ODR_3_906HZ,
};

enum adxl355_hpf_3db {
	ADXL355_HPF_OFF,
	ADXL355_HPF_24_7,
	ADXL355_HPF_6_2084,
	ADXL355_HPF_1_5545,
	ADXL355_HPF_0_3862,
	ADXL355_HPF_0_0954,
	ADXL355_HPF_0_0238,
};

static const int adxl355_odr_table[][2] = {
	[0] = {4000, 0},
	[1] = {2000, 0},
	[2] = {1000, 0},
	[3] = {500, 0},
	[4] = {250, 0},
	[5] = {125, 0},
	[6] = {62, 500000},
	[7] = {31, 250000},
	[8] = {15, 625000},
	[9] = {7, 813000},
	[10] = {3, 906000},
};

static const int adxl355_hpf_3db_multipliers[] = {
	0,
	247000,
	62084,
	15545,
	3862,
	954,
	238,
};

enum adxl355_chans {
	chan_x, chan_y, chan_z,
};

struct adxl355_chan_info {
	u8 data_reg;
	u8 offset_reg;
};

static const struct adxl355_chan_info adxl355_chans[] = {
	[chan_x] = {
		.data_reg = ADXL355_XDATA3_REG,
		.offset_reg = ADXL355_OFFSET_X_H_REG
	},
	[chan_y] = {
		.data_reg = ADXL355_YDATA3_REG,
		.offset_reg = ADXL355_OFFSET_Y_H_REG
	},
	[chan_z] = {
		.data_reg = ADXL355_ZDATA3_REG,
		.offset_reg = ADXL355_OFFSET_Z_H_REG
	},
};

struct adxl355_data {
	struct regmap *regmap;
	struct device *dev;
	struct mutex lock; /* lock to protect op_mode */
	enum adxl355_op_mode op_mode;
	enum adxl355_odr odr;
	enum adxl355_hpf_3db hpf_3db;
	int calibbias[3];
	int adxl355_hpf_3db_table[7][2];
	u8 transf_buf[3] ____cacheline_aligned;
};

static int adxl355_set_op_mode(struct adxl355_data *data,
			       enum adxl355_op_mode op_mode)
{
	int ret;

	if (data->op_mode == op_mode)
		return 0;

	ret = regmap_update_bits(data->regmap, ADXL355_POWER_CTL_REG,
				 ADXL355_POWER_CTL_MODE_MSK, op_mode);
	if (ret)
		return ret;

	data->op_mode = op_mode;

	return ret;
}

static void adxl355_fill_3db_frequency_table(struct adxl355_data *data)
{
	u32 multiplier;
	u64 div, rem;
	u64 odr;
	int i;

	odr = mul_u64_u32_shr(adxl355_odr_table[data->odr][0], MEGA, 0) +
			      adxl355_odr_table[data->odr][1];

	for (i = 0; i < ARRAY_SIZE(adxl355_hpf_3db_multipliers); i++) {
		multiplier = adxl355_hpf_3db_multipliers[i];
		div = div64_u64_rem(mul_u64_u32_shr(odr, multiplier, 0),
				    TERA * 100, &rem);

		data->adxl355_hpf_3db_table[i][0] = div;
		data->adxl355_hpf_3db_table[i][1] = div_u64(rem, MEGA * 100);
	}
}

static int adxl355_setup(struct adxl355_data *data)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, ADXL355_DEVID_AD_REG, &regval);
	if (ret)
		return ret;

	if (regval != ADXL355_DEVID_AD_VAL) {
		dev_err(data->dev, "Invalid ADI ID 0x%02x\n", regval);
		return -ENODEV;
	}

	ret = regmap_read(data->regmap, ADXL355_DEVID_MST_REG, &regval);
	if (ret)
		return ret;

	if (regval != ADXL355_DEVID_MST_VAL) {
		dev_err(data->dev, "Invalid MEMS ID 0x%02x\n", regval);
		return -ENODEV;
	}

	ret = regmap_read(data->regmap, ADXL355_PARTID_REG, &regval);
	if (ret)
		return ret;

	if (regval != ADXL355_PARTID_VAL) {
		dev_err(data->dev, "Invalid DEV ID 0x%02x\n", regval);
		return -ENODEV;
	}

	/*
	 * Perform a software reset to make sure the device is in a consistent
	 * state after start-up.
	 */
	ret = regmap_write(data->regmap, ADXL355_RESET_REG, ADXL355_RESET_CODE);
	if (ret)
		return ret;

	adxl355_fill_3db_frequency_table(data);

	return adxl355_set_op_mode(data, ADXL355_MEASUREMENT);
}

static int adxl355_get_temp_data(struct adxl355_data *data, u8 addr)
{
	return regmap_bulk_read(data->regmap, addr, data->transf_buf, 2);
}

static int adxl355_read_axis(struct adxl355_data *data, u8 addr)
{
	int ret;

	ret = regmap_bulk_read(data->regmap, addr, data->transf_buf,
			       ARRAY_SIZE(data->transf_buf));
	if (ret)
		return ret;

	return get_unaligned_be24(data->transf_buf);
}

static int adxl355_find_match(const int (*freq_tbl)[2], const int n,
			      const int val, const int val2)
{
	int i;

	for (i = 0; i < n; i++) {
		if (freq_tbl[i][0] == val && freq_tbl[i][1] == val2)
			return i;
	}

	return -EINVAL;
}

static int adxl355_set_odr(struct adxl355_data *data,
			   enum adxl355_odr odr)
{
	int ret;

	mutex_lock(&data->lock);

	if (data->odr == odr) {
		mutex_unlock(&data->lock);
		return 0;
	}

	ret = adxl355_set_op_mode(data, ADXL355_STANDBY);
	if (ret)
		goto err_unlock;

	ret = regmap_update_bits(data->regmap, ADXL355_FILTER_REG,
				 ADXL355_FILTER_ODR_MSK,
				 FIELD_PREP(ADXL355_FILTER_ODR_MSK, odr));
	if (ret)
		goto err_set_opmode;

	data->odr = odr;
	adxl355_fill_3db_frequency_table(data);

	ret = adxl355_set_op_mode(data, ADXL355_MEASUREMENT);
	if (ret)
		goto err_set_opmode;

	mutex_unlock(&data->lock);
	return 0;

err_set_opmode:
	adxl355_set_op_mode(data, ADXL355_MEASUREMENT);
err_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static int adxl355_set_hpf_3db(struct adxl355_data *data,
			       enum adxl355_hpf_3db hpf)
{
	int ret;

	mutex_lock(&data->lock);

	if (data->hpf_3db == hpf) {
		mutex_unlock(&data->lock);
		return 0;
	}

	ret = adxl355_set_op_mode(data, ADXL355_STANDBY);
	if (ret)
		goto err_unlock;

	ret = regmap_update_bits(data->regmap, ADXL355_FILTER_REG,
				 ADXL355_FILTER_HPF_MSK,
				 FIELD_PREP(ADXL355_FILTER_HPF_MSK, hpf));
	if (ret)
		goto err_set_opmode;

	data->hpf_3db = hpf;

	ret = adxl355_set_op_mode(data, ADXL355_MEASUREMENT);
	if (ret)
		goto err_set_opmode;

	mutex_unlock(&data->lock);
	return 0;

err_set_opmode:
	adxl355_set_op_mode(data, ADXL355_MEASUREMENT);
err_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static int adxl355_set_calibbias(struct adxl355_data *data,
				 enum adxl355_chans chan, int calibbias)
{
	int ret;

	mutex_lock(&data->lock);

	ret = adxl355_set_op_mode(data, ADXL355_STANDBY);
	if (ret)
		goto err_unlock;

	put_unaligned_be16(calibbias, data->transf_buf);
	ret = regmap_bulk_write(data->regmap,
				adxl355_chans[chan].offset_reg,
				data->transf_buf, 2);
	if (ret)
		goto err_set_opmode;

	data->calibbias[chan] = calibbias;

	ret = adxl355_set_op_mode(data, ADXL355_MEASUREMENT);
	if (ret)
		goto err_set_opmode;

	mutex_unlock(&data->lock);
	return 0;

err_set_opmode:
	adxl355_set_op_mode(data, ADXL355_MEASUREMENT);
err_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static int adxl355_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct adxl355_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_TEMP:
			ret = adxl355_get_temp_data(data, chan->address);
			if (ret < 0)
				return ret;
			*val = get_unaligned_be16(data->transf_buf);

			return IIO_VAL_INT;
		case IIO_ACCEL:
			ret = adxl355_read_axis(data, adxl355_chans[
						chan->address].data_reg);
			if (ret < 0)
				return ret;
			*val = sign_extend32(ret >> chan->scan_type.shift,
					     chan->scan_type.realbits - 1);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		/*
		 * The datasheet defines an intercept of 1885 LSB at 25 degC
		 * and a slope of -9.05 LSB/C. The following formula can be used
		 * to find the temperature:
		 * Temp = ((RAW - 1885)/(-9.05)) + 25 but this doesn't follow
		 * the format of the IIO which is Temp = (RAW + OFFSET) * SCALE.
		 * Hence using some rearranging we get the scale as -110.497238
		 * and offset as -2111.25.
		 */
		case IIO_TEMP:
			*val = -110;
			*val2 = 497238;
			return IIO_VAL_INT_PLUS_MICRO;
		/*
		 * At +/- 2g with 20-bit resolution, scale is given in datasheet
		 * as 3.9ug/LSB = 0.0000039 * 9.80665 = 0.00003824593 m/s^2.
		 */
		case IIO_ACCEL:
			*val = 0;
			*val2 = 38245;
			return IIO_VAL_INT_PLUS_NANO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		*val = -2111;
		*val2 = 250000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBBIAS:
		*val = sign_extend32(data->calibbias[chan->address], 15);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = adxl355_odr_table[data->odr][0];
		*val2 = adxl355_odr_table[data->odr][1];
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		*val = data->adxl355_hpf_3db_table[data->hpf_3db][0];
		*val2 = data->adxl355_hpf_3db_table[data->hpf_3db][1];
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int adxl355_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct adxl355_data *data = iio_priv(indio_dev);
	int odr_idx, hpf_idx, calibbias;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		odr_idx = adxl355_find_match(adxl355_odr_table,
					     ARRAY_SIZE(adxl355_odr_table),
					     val, val2);
		if (odr_idx < 0)
			return odr_idx;

		return adxl355_set_odr(data, odr_idx);
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		hpf_idx = adxl355_find_match(data->adxl355_hpf_3db_table,
					ARRAY_SIZE(data->adxl355_hpf_3db_table),
					     val, val2);
		if (hpf_idx < 0)
			return hpf_idx;

		return adxl355_set_hpf_3db(data, hpf_idx);
	case IIO_CHAN_INFO_CALIBBIAS:
		calibbias = clamp_t(int, val, S16_MIN, S16_MAX);

		return adxl355_set_calibbias(data, chan->address, calibbias);
	default:
		return -EINVAL;
	}
}

static int adxl355_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	struct adxl355_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (const int *)adxl355_odr_table;
		*type = IIO_VAL_INT_PLUS_MICRO;
		/* Values are stored in a 2D matrix */
		*length = ARRAY_SIZE(adxl355_odr_table) * 2;

		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		*vals = (const int *)data->adxl355_hpf_3db_table;
		*type = IIO_VAL_INT_PLUS_MICRO;
		/* Values are stored in a 2D matrix */
		*length = ARRAY_SIZE(data->adxl355_hpf_3db_table) * 2;

		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static const struct iio_info adxl355_info = {
	.read_raw	= adxl355_read_raw,
	.write_raw	= adxl355_write_raw,
	.read_avail	= &adxl355_read_avail,
};

#define ADXL355_ACCEL_CHANNEL(index, reg, axis) {			\
	.type = IIO_ACCEL,						\
	.address = reg,							\
	.modified = 1,							\
	.channel2 = IIO_MOD_##axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_CALIBBIAS),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
				    BIT(IIO_CHAN_INFO_SAMP_FREQ) |	\
		BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY),	\
	.info_mask_shared_by_type_available =				\
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |				\
		BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY),	\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 20,						\
		.storagebits = 32,					\
		.shift = 4,						\
		.endianness = IIO_BE,					\
	}								\
}

static const struct iio_chan_spec adxl355_channels[] = {
	ADXL355_ACCEL_CHANNEL(0, chan_x, X),
	ADXL355_ACCEL_CHANNEL(1, chan_y, Y),
	ADXL355_ACCEL_CHANNEL(2, chan_z, Z),
	{
		.type = IIO_TEMP,
		.address = ADXL355_TEMP2_REG,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.scan_type = {
			.sign = 's',
			.realbits = 12,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
};

int adxl355_core_probe(struct device *dev, struct regmap *regmap,
		       const char *name)
{
	struct adxl355_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->regmap = regmap;
	data->dev = dev;
	data->op_mode = ADXL355_STANDBY;
	mutex_init(&data->lock);

	indio_dev->name = name;
	indio_dev->info = &adxl355_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adxl355_channels;
	indio_dev->num_channels = ARRAY_SIZE(adxl355_channels);

	ret = adxl355_setup(data);
	if (ret) {
		dev_err(dev, "ADXL355 setup failed\n");
		return ret;
	}

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_GPL(adxl355_core_probe);

MODULE_AUTHOR("Puranjay Mohan <puranjay12@gmail.com>");
MODULE_DESCRIPTION("ADXL355 3-Axis Digital Accelerometer core driver");
MODULE_LICENSE("GPL v2");
