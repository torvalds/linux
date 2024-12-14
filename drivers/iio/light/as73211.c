// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for AMS AS73211 JENCOLOR(R) Digital XYZ Sensor and AMS AS7331
 * UVA, UVB and UVC (DUV) Ultraviolet Sensor
 *
 * Author: Christian Eggers <ceggers@arri.de>
 *
 * Copyright (c) 2020 ARRI Lighting
 *
 * Color light sensor with 16-bit channels for x, y, z and temperature);
 * 7-bit I2C slave address 0x74 .. 0x77.
 *
 * Datasheets:
 * AS73211: https://ams.com/documents/20143/36005/AS73211_DS000556_3-01.pdf
 * AS7331: https://ams.com/documents/20143/9106314/AS7331_DS001047_4-00.pdf
 */

#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/units.h>

#define AS73211_DRV_NAME "as73211"

/* AS73211 configuration registers */
#define AS73211_REG_OSR    0x0
#define AS73211_REG_AGEN   0x2
#define AS73211_REG_CREG1  0x6
#define AS73211_REG_CREG2  0x7
#define AS73211_REG_CREG3  0x8

/* AS73211 output register bank */
#define AS73211_OUT_OSR_STATUS    0
#define AS73211_OUT_TEMP          1
#define AS73211_OUT_MRES1         2
#define AS73211_OUT_MRES2         3
#define AS73211_OUT_MRES3         4

#define AS73211_OSR_SS            BIT(7)
#define AS73211_OSR_PD            BIT(6)
#define AS73211_OSR_SW_RES        BIT(3)
#define AS73211_OSR_DOS_MASK      GENMASK(2, 0)
#define AS73211_OSR_DOS_CONFIG    FIELD_PREP(AS73211_OSR_DOS_MASK, 0x2)
#define AS73211_OSR_DOS_MEASURE   FIELD_PREP(AS73211_OSR_DOS_MASK, 0x3)

#define AS73211_AGEN_DEVID_MASK   GENMASK(7, 4)
#define AS73211_AGEN_DEVID(x)     FIELD_PREP(AS73211_AGEN_DEVID_MASK, (x))
#define AS73211_AGEN_MUT_MASK     GENMASK(3, 0)
#define AS73211_AGEN_MUT(x)       FIELD_PREP(AS73211_AGEN_MUT_MASK, (x))

#define AS73211_CREG1_GAIN_MASK   GENMASK(7, 4)
#define AS73211_CREG1_GAIN_1      11
#define AS73211_CREG1_TIME_MASK   GENMASK(3, 0)

#define AS73211_CREG3_CCLK_MASK   GENMASK(1, 0)

#define AS73211_OSR_STATUS_OUTCONVOF  BIT(15)
#define AS73211_OSR_STATUS_MRESOF     BIT(14)
#define AS73211_OSR_STATUS_ADCOF      BIT(13)
#define AS73211_OSR_STATUS_LDATA      BIT(12)
#define AS73211_OSR_STATUS_NDATA      BIT(11)
#define AS73211_OSR_STATUS_NOTREADY   BIT(10)

#define AS73211_SAMPLE_FREQ_BASE      1024000

#define AS73211_SAMPLE_TIME_NUM       15
#define AS73211_SAMPLE_TIME_MAX_MS    BIT(AS73211_SAMPLE_TIME_NUM - 1)

/* Available sample frequencies are 1.024MHz multiplied by powers of two. */
static const int as73211_samp_freq_avail[] = {
	AS73211_SAMPLE_FREQ_BASE * 1,
	AS73211_SAMPLE_FREQ_BASE * 2,
	AS73211_SAMPLE_FREQ_BASE * 4,
	AS73211_SAMPLE_FREQ_BASE * 8,
};

static const int as73211_hardwaregain_avail[] = {
	1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048,
};

struct as73211_data;

/**
 * struct as73211_spec_dev_data - device-specific data
 * @intensity_scale:  Function to retrieve intensity scale values.
 * @channels:          Device channels.
 * @num_channels:     Number of channels of the device.
 */
struct as73211_spec_dev_data {
	int (*intensity_scale)(struct as73211_data *data, int chan, int *val, int *val2);
	struct iio_chan_spec const *channels;
	int num_channels;
};

/**
 * struct as73211_data - Instance data for one AS73211
 * @client: I2C client.
 * @osr:    Cached Operational State Register.
 * @creg1:  Cached Configuration Register 1.
 * @creg2:  Cached Configuration Register 2.
 * @creg3:  Cached Configuration Register 3.
 * @mutex:  Keeps cached registers in sync with the device.
 * @completion: Completion to wait for interrupt.
 * @int_time_avail: Available integration times (depend on sampling frequency).
 * @spec_dev: device-specific configuration.
 */
struct as73211_data {
	struct i2c_client *client;
	u8 osr;
	u8 creg1;
	u8 creg2;
	u8 creg3;
	struct mutex mutex;
	struct completion completion;
	int int_time_avail[AS73211_SAMPLE_TIME_NUM * 2];
	const struct as73211_spec_dev_data *spec_dev;
};

#define AS73211_COLOR_CHANNEL(_color, _si, _addr) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_type = \
		BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
		BIT(IIO_CHAN_INFO_HARDWAREGAIN) | \
		BIT(IIO_CHAN_INFO_INT_TIME), \
	.info_mask_shared_by_type_available = \
		BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
		BIT(IIO_CHAN_INFO_HARDWAREGAIN) | \
		BIT(IIO_CHAN_INFO_INT_TIME), \
	.channel2 = IIO_MOD_##_color, \
	.address = _addr, \
	.scan_index = _si, \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_LE, \
	}, \
}

#define AS73211_OFFSET_TEMP_INT    (-66)
#define AS73211_OFFSET_TEMP_MICRO  900000
#define AS73211_SCALE_TEMP_INT     0
#define AS73211_SCALE_TEMP_MICRO   50000

#define AS73211_SCALE_X 277071108  /* nW/m^2 */
#define AS73211_SCALE_Y 298384270  /* nW/m^2 */
#define AS73211_SCALE_Z 160241927  /* nW/m^2 */

#define AS7331_SCALE_UVA 340000  /* nW/cm^2 */
#define AS7331_SCALE_UVB 378000  /* nW/cm^2 */
#define AS7331_SCALE_UVC 166000  /* nW/cm^2 */

/* Channel order MUST match devices result register order */
#define AS73211_SCAN_INDEX_TEMP 0
#define AS73211_SCAN_INDEX_X    1
#define AS73211_SCAN_INDEX_Y    2
#define AS73211_SCAN_INDEX_Z    3
#define AS73211_SCAN_INDEX_TS   4

#define AS73211_SCAN_MASK_COLOR ( \
	BIT(AS73211_SCAN_INDEX_X) |   \
	BIT(AS73211_SCAN_INDEX_Y) |   \
	BIT(AS73211_SCAN_INDEX_Z))

#define AS73211_SCAN_MASK_ALL (    \
	BIT(AS73211_SCAN_INDEX_TEMP) | \
	AS73211_SCAN_MASK_COLOR)

static const unsigned long as73211_scan_masks[] = {
	AS73211_SCAN_MASK_COLOR,
	AS73211_SCAN_MASK_ALL,
	0
};

static const struct iio_chan_spec as73211_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_OFFSET) |
			BIT(IIO_CHAN_INFO_SCALE),
		.address = AS73211_OUT_TEMP,
		.scan_index = AS73211_SCAN_INDEX_TEMP,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		}
	},
	AS73211_COLOR_CHANNEL(X, AS73211_SCAN_INDEX_X, AS73211_OUT_MRES1),
	AS73211_COLOR_CHANNEL(Y, AS73211_SCAN_INDEX_Y, AS73211_OUT_MRES2),
	AS73211_COLOR_CHANNEL(Z, AS73211_SCAN_INDEX_Z, AS73211_OUT_MRES3),
	IIO_CHAN_SOFT_TIMESTAMP(AS73211_SCAN_INDEX_TS),
};

static const struct iio_chan_spec as7331_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_OFFSET) |
			BIT(IIO_CHAN_INFO_SCALE),
		.address = AS73211_OUT_TEMP,
		.scan_index = AS73211_SCAN_INDEX_TEMP,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		}
	},
	AS73211_COLOR_CHANNEL(LIGHT_UVA, AS73211_SCAN_INDEX_X, AS73211_OUT_MRES1),
	AS73211_COLOR_CHANNEL(LIGHT_UVB, AS73211_SCAN_INDEX_Y, AS73211_OUT_MRES2),
	AS73211_COLOR_CHANNEL(LIGHT_DUV, AS73211_SCAN_INDEX_Z, AS73211_OUT_MRES3),
	IIO_CHAN_SOFT_TIMESTAMP(AS73211_SCAN_INDEX_TS),
};

static unsigned int as73211_integration_time_1024cyc(struct as73211_data *data)
{
	/*
	 * Return integration time in units of 1024 clock cycles. Integration time
	 * in CREG1 is in powers of 2 (x 1024 cycles).
	 */
	return BIT(FIELD_GET(AS73211_CREG1_TIME_MASK, data->creg1));
}

static unsigned int as73211_integration_time_us(struct as73211_data *data,
						 unsigned int integration_time_1024cyc)
{
	/*
	 * f_samp is configured in CREG3 in powers of 2 (x 1.024 MHz)
	 * t_cycl is configured in CREG1 in powers of 2 (x 1024 cycles)
	 * t_int_us = 1 / (f_samp) * t_cycl * US_PER_SEC
	 *          = 1 / (2^CREG3_CCLK * 1,024,000) * 2^CREG1_CYCLES * 1,024 * US_PER_SEC
	 *          = 2^(-CREG3_CCLK) * 2^CREG1_CYCLES * 1,000
	 * In order to get rid of negative exponents, we extend the "fraction"
	 * by 2^3 (CREG3_CCLK,max = 3)
	 * t_int_us = 2^(3-CREG3_CCLK) * 2^CREG1_CYCLES * 125
	 */
	return BIT(3 - FIELD_GET(AS73211_CREG3_CCLK_MASK, data->creg3)) *
		integration_time_1024cyc * 125;
}

static void as73211_integration_time_calc_avail(struct as73211_data *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(data->int_time_avail) / 2; i++) {
		unsigned int time_us = as73211_integration_time_us(data, BIT(i));

		data->int_time_avail[i * 2 + 0] = time_us / USEC_PER_SEC;
		data->int_time_avail[i * 2 + 1] = time_us % USEC_PER_SEC;
	}
}

static unsigned int as73211_gain(struct as73211_data *data)
{
	/* gain can be calculated from CREG1 as 2^(11 - CREG1_GAIN) */
	return BIT(AS73211_CREG1_GAIN_1 - FIELD_GET(AS73211_CREG1_GAIN_MASK, data->creg1));
}

/* must be called with as73211_data::mutex held. */
static int as73211_req_data(struct as73211_data *data)
{
	unsigned int time_us = as73211_integration_time_us(data,
							    as73211_integration_time_1024cyc(data));
	struct device *dev = &data->client->dev;
	union i2c_smbus_data smbus_data;
	u16 osr_status;
	int ret;

	if (data->client->irq)
		reinit_completion(&data->completion);

	/*
	 * During measurement, there should be no traffic on the i2c bus as the
	 * electrical noise would disturb the measurement process.
	 */
	i2c_lock_bus(data->client->adapter, I2C_LOCK_SEGMENT);

	data->osr &= ~AS73211_OSR_DOS_MASK;
	data->osr |= AS73211_OSR_DOS_MEASURE | AS73211_OSR_SS;

	smbus_data.byte = data->osr;
	ret = __i2c_smbus_xfer(data->client->adapter, data->client->addr,
			data->client->flags, I2C_SMBUS_WRITE,
			AS73211_REG_OSR, I2C_SMBUS_BYTE_DATA, &smbus_data);
	if (ret < 0) {
		i2c_unlock_bus(data->client->adapter, I2C_LOCK_SEGMENT);
		return ret;
	}

	/*
	 * Reset AS73211_OSR_SS (is self clearing) in order to avoid unintentional
	 * triggering of further measurements later.
	 */
	data->osr &= ~AS73211_OSR_SS;

	/*
	 * Add 33% extra margin for the timeout. fclk,min = fclk,typ - 27%.
	 */
	time_us += time_us / 3;
	if (data->client->irq) {
		ret = wait_for_completion_timeout(&data->completion, usecs_to_jiffies(time_us));
		if (!ret) {
			dev_err(dev, "timeout waiting for READY IRQ\n");
			i2c_unlock_bus(data->client->adapter, I2C_LOCK_SEGMENT);
			return -ETIMEDOUT;
		}
	} else {
		/* Wait integration time */
		usleep_range(time_us, 2 * time_us);
	}

	i2c_unlock_bus(data->client->adapter, I2C_LOCK_SEGMENT);

	ret = i2c_smbus_read_word_data(data->client, AS73211_OUT_OSR_STATUS);
	if (ret < 0)
		return ret;

	osr_status = ret;
	if (osr_status != (AS73211_OSR_DOS_MEASURE | AS73211_OSR_STATUS_NDATA)) {
		if (osr_status & AS73211_OSR_SS) {
			dev_err(dev, "%s() Measurement has not stopped\n", __func__);
			return -ETIME;
		}
		if (osr_status & AS73211_OSR_STATUS_NOTREADY) {
			dev_err(dev, "%s() Data is not ready\n", __func__);
			return -ENODATA;
		}
		if (!(osr_status & AS73211_OSR_STATUS_NDATA)) {
			dev_err(dev, "%s() No new data available\n", __func__);
			return -ENODATA;
		}
		if (osr_status & AS73211_OSR_STATUS_LDATA) {
			dev_err(dev, "%s() Result buffer overrun\n", __func__);
			return -ENOBUFS;
		}
		if (osr_status & AS73211_OSR_STATUS_ADCOF) {
			dev_err(dev, "%s() ADC overflow\n", __func__);
			return -EOVERFLOW;
		}
		if (osr_status & AS73211_OSR_STATUS_MRESOF) {
			dev_err(dev, "%s() Measurement result overflow\n", __func__);
			return -EOVERFLOW;
		}
		if (osr_status & AS73211_OSR_STATUS_OUTCONVOF) {
			dev_err(dev, "%s() Timer overflow\n", __func__);
			return -EOVERFLOW;
		}
		dev_err(dev, "%s() Unexpected status value\n", __func__);
		return -EIO;
	}

	return 0;
}

static int as73211_intensity_scale(struct as73211_data *data, int chan,
				   int *val, int *val2)
{
	switch (chan) {
	case IIO_MOD_X:
		*val = AS73211_SCALE_X;
		break;
	case IIO_MOD_Y:
		*val = AS73211_SCALE_Y;
		break;
	case IIO_MOD_Z:
		*val = AS73211_SCALE_Z;
		break;
	default:
		return -EINVAL;
	}
	*val2 = as73211_integration_time_1024cyc(data) * as73211_gain(data);

	return IIO_VAL_FRACTIONAL;
}

static int as7331_intensity_scale(struct as73211_data *data, int chan,
				  int *val, int *val2)
{
	switch (chan) {
	case IIO_MOD_LIGHT_UVA:
		*val = AS7331_SCALE_UVA;
		break;
	case IIO_MOD_LIGHT_UVB:
		*val = AS7331_SCALE_UVB;
		break;
	case IIO_MOD_LIGHT_DUV:
		*val = AS7331_SCALE_UVC;
		break;
	default:
		return -EINVAL;
	}
	*val2 = as73211_integration_time_1024cyc(data) * as73211_gain(data);

	return IIO_VAL_FRACTIONAL;
}

static int as73211_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct as73211_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		int ret;

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret < 0)
			return ret;

		ret = as73211_req_data(data);
		if (ret < 0) {
			iio_device_release_direct_mode(indio_dev);
			return ret;
		}

		ret = i2c_smbus_read_word_data(data->client, chan->address);
		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;

		*val = ret;
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_OFFSET:
		*val = AS73211_OFFSET_TEMP_INT;
		*val2 = AS73211_OFFSET_TEMP_MICRO;
		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_TEMP:
			*val = AS73211_SCALE_TEMP_INT;
			*val2 = AS73211_SCALE_TEMP_MICRO;
			return IIO_VAL_INT_PLUS_MICRO;

		case IIO_INTENSITY:
			return data->spec_dev->intensity_scale(data, chan->channel2,
							       val, val2);

		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_SAMP_FREQ:
		/* f_samp is configured in CREG3 in powers of 2 (x 1.024 MHz) */
		*val = BIT(FIELD_GET(AS73211_CREG3_CCLK_MASK, data->creg3)) *
			AS73211_SAMPLE_FREQ_BASE;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_HARDWAREGAIN:
		*val = as73211_gain(data);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_INT_TIME: {
		unsigned int time_us;

		mutex_lock(&data->mutex);
		time_us = as73211_integration_time_us(data, as73211_integration_time_1024cyc(data));
		mutex_unlock(&data->mutex);
		*val = time_us / USEC_PER_SEC;
		*val2 = time_us % USEC_PER_SEC;
		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}}
}

static int as73211_read_avail(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length, long mask)
{
	struct as73211_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*length = ARRAY_SIZE(as73211_samp_freq_avail);
		*vals = as73211_samp_freq_avail;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;

	case IIO_CHAN_INFO_HARDWAREGAIN:
		*length = ARRAY_SIZE(as73211_hardwaregain_avail);
		*vals = as73211_hardwaregain_avail;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;

	case IIO_CHAN_INFO_INT_TIME:
		*length = ARRAY_SIZE(data->int_time_avail);
		*vals = data->int_time_avail;
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;

	default:
		return -EINVAL;
	}
}

static int _as73211_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan __always_unused,
			       int val, int val2, long mask)
{
	struct as73211_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ: {
		int reg_bits, freq_kHz = val / HZ_PER_KHZ;  /* 1024, 2048, ... */

		/* val must be 1024 * 2^x */
		if (val < 0 || (freq_kHz * HZ_PER_KHZ) != val ||
				!is_power_of_2(freq_kHz) || val2)
			return -EINVAL;

		/* f_samp is configured in CREG3 in powers of 2 (x 1.024 MHz (=2^10)) */
		reg_bits = ilog2(freq_kHz) - 10;
		if (!FIELD_FIT(AS73211_CREG3_CCLK_MASK, reg_bits))
			return -EINVAL;

		data->creg3 &= ~AS73211_CREG3_CCLK_MASK;
		data->creg3 |= FIELD_PREP(AS73211_CREG3_CCLK_MASK, reg_bits);
		as73211_integration_time_calc_avail(data);

		ret = i2c_smbus_write_byte_data(data->client, AS73211_REG_CREG3, data->creg3);
		if (ret < 0)
			return ret;

		return 0;
	}
	case IIO_CHAN_INFO_HARDWAREGAIN: {
		unsigned int reg_bits;

		if (val < 0 || !is_power_of_2(val) || val2)
			return -EINVAL;

		/* gain can be calculated from CREG1 as 2^(11 - CREG1_GAIN) */
		reg_bits = AS73211_CREG1_GAIN_1 - ilog2(val);
		if (!FIELD_FIT(AS73211_CREG1_GAIN_MASK, reg_bits))
			return -EINVAL;

		data->creg1 &= ~AS73211_CREG1_GAIN_MASK;
		data->creg1 |= FIELD_PREP(AS73211_CREG1_GAIN_MASK, reg_bits);

		ret = i2c_smbus_write_byte_data(data->client, AS73211_REG_CREG1, data->creg1);
		if (ret < 0)
			return ret;

		return 0;
	}
	case IIO_CHAN_INFO_INT_TIME: {
		int val_us = val * USEC_PER_SEC + val2;
		int time_ms;
		int reg_bits;

		/* f_samp is configured in CREG3 in powers of 2 (x 1.024 MHz) */
		int f_samp_1_024mhz = BIT(FIELD_GET(AS73211_CREG3_CCLK_MASK, data->creg3));

		/*
		 * time_ms = time_us * US_PER_MS * f_samp_1_024mhz / MHZ_PER_HZ
		 *         = time_us * f_samp_1_024mhz / 1000
		 */
		time_ms = (val_us * f_samp_1_024mhz) / 1000;  /* 1 ms, 2 ms, ... (power of two) */
		if (time_ms < 0 || !is_power_of_2(time_ms) || time_ms > AS73211_SAMPLE_TIME_MAX_MS)
			return -EINVAL;

		reg_bits = ilog2(time_ms);
		if (!FIELD_FIT(AS73211_CREG1_TIME_MASK, reg_bits))
			return -EINVAL;  /* not possible due to previous tests */

		data->creg1 &= ~AS73211_CREG1_TIME_MASK;
		data->creg1 |= FIELD_PREP(AS73211_CREG1_TIME_MASK, reg_bits);

		ret = i2c_smbus_write_byte_data(data->client, AS73211_REG_CREG1, data->creg1);
		if (ret < 0)
			return ret;

		return 0;

	default:
		return -EINVAL;
	}}
}

static int as73211_write_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct as73211_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret < 0)
		goto error_unlock;

	/* Need to switch to config mode ... */
	if ((data->osr & AS73211_OSR_DOS_MASK) != AS73211_OSR_DOS_CONFIG) {
		data->osr &= ~AS73211_OSR_DOS_MASK;
		data->osr |= AS73211_OSR_DOS_CONFIG;

		ret = i2c_smbus_write_byte_data(data->client, AS73211_REG_OSR, data->osr);
		if (ret < 0)
			goto error_release;
	}

	ret = _as73211_write_raw(indio_dev, chan, val, val2, mask);

error_release:
	iio_device_release_direct_mode(indio_dev);
error_unlock:
	mutex_unlock(&data->mutex);
	return ret;
}

static irqreturn_t as73211_ready_handler(int irq __always_unused, void *priv)
{
	struct as73211_data *data = iio_priv(priv);

	complete(&data->completion);

	return IRQ_HANDLED;
}

static irqreturn_t as73211_trigger_handler(int irq __always_unused, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct as73211_data *data = iio_priv(indio_dev);
	struct {
		__le16 chan[4];
		s64 ts __aligned(8);
	} scan;
	int data_result, ret;

	mutex_lock(&data->mutex);

	data_result = as73211_req_data(data);
	if (data_result < 0 && data_result != -EOVERFLOW)
		goto done;  /* don't push any data for errors other than EOVERFLOW */

	if (*indio_dev->active_scan_mask == AS73211_SCAN_MASK_ALL) {
		/* Optimization for reading all (color + temperature) channels */
		u8 addr = as73211_channels[0].address;
		struct i2c_msg msgs[] = {
			{
				.addr = data->client->addr,
				.flags = 0,
				.len = 1,
				.buf = &addr,
			},
			{
				.addr = data->client->addr,
				.flags = I2C_M_RD,
				.len = sizeof(scan.chan),
				.buf = (u8 *)&scan.chan,
			},
		};

		ret = i2c_transfer(data->client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret < 0)
			goto done;
	} else {
		/* Optimization for reading only color channels */

		/* AS73211 starts reading at address 2 */
		ret = i2c_master_recv(data->client,
				(char *)&scan.chan[0], 3 * sizeof(scan.chan[0]));
		if (ret < 0)
			goto done;

		/* Avoid pushing uninitialized data */
		scan.chan[3] = 0;
	}

	if (data_result) {
		/*
		 * Saturate all channels (in case of overflows). Temperature channel
		 * is not affected by overflows.
		 */
		if (*indio_dev->active_scan_mask == AS73211_SCAN_MASK_ALL) {
			scan.chan[1] = cpu_to_le16(U16_MAX);
			scan.chan[2] = cpu_to_le16(U16_MAX);
			scan.chan[3] = cpu_to_le16(U16_MAX);
		} else {
			scan.chan[0] = cpu_to_le16(U16_MAX);
			scan.chan[1] = cpu_to_le16(U16_MAX);
			scan.chan[2] = cpu_to_le16(U16_MAX);
		}
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &scan, iio_get_time_ns(indio_dev));

done:
	mutex_unlock(&data->mutex);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_info as73211_info = {
	.read_raw = as73211_read_raw,
	.read_avail = as73211_read_avail,
	.write_raw = as73211_write_raw,
};

static int as73211_power(struct iio_dev *indio_dev, bool state)
{
	struct as73211_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);

	if (state)
		data->osr &= ~AS73211_OSR_PD;
	else
		data->osr |= AS73211_OSR_PD;

	ret = i2c_smbus_write_byte_data(data->client, AS73211_REG_OSR, data->osr);

	mutex_unlock(&data->mutex);

	if (ret < 0)
		return ret;

	return 0;
}

static void as73211_power_disable(void *data)
{
	struct iio_dev *indio_dev = data;

	as73211_power(indio_dev, false);
}

static int as73211_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct as73211_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	data->spec_dev = i2c_get_match_data(client);
	if (!data->spec_dev)
		return -EINVAL;

	mutex_init(&data->mutex);
	init_completion(&data->completion);

	indio_dev->info = &as73211_info;
	indio_dev->name = AS73211_DRV_NAME;
	indio_dev->channels = data->spec_dev->channels;
	indio_dev->num_channels = data->spec_dev->num_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->available_scan_masks = as73211_scan_masks;

	ret = i2c_smbus_read_byte_data(data->client, AS73211_REG_OSR);
	if (ret < 0)
		return ret;
	data->osr = ret;

	/* reset device */
	data->osr |= AS73211_OSR_SW_RES;
	ret = i2c_smbus_write_byte_data(data->client, AS73211_REG_OSR, data->osr);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, AS73211_REG_OSR);
	if (ret < 0)
		return ret;
	data->osr = ret;

	/*
	 * Reading AGEN is only possible after reset (AGEN is not available if
	 * device is in measurement mode).
	 */
	ret = i2c_smbus_read_byte_data(data->client, AS73211_REG_AGEN);
	if (ret < 0)
		return ret;

	/* At the time of writing this driver, only DEVID 2 and MUT 1 are known. */
	if ((ret & AS73211_AGEN_DEVID_MASK) != AS73211_AGEN_DEVID(2) ||
	    (ret & AS73211_AGEN_MUT_MASK) != AS73211_AGEN_MUT(1))
		return -ENODEV;

	ret = i2c_smbus_read_byte_data(data->client, AS73211_REG_CREG1);
	if (ret < 0)
		return ret;
	data->creg1 = ret;

	ret = i2c_smbus_read_byte_data(data->client, AS73211_REG_CREG2);
	if (ret < 0)
		return ret;
	data->creg2 = ret;

	ret = i2c_smbus_read_byte_data(data->client, AS73211_REG_CREG3);
	if (ret < 0)
		return ret;
	data->creg3 = ret;
	as73211_integration_time_calc_avail(data);

	ret = as73211_power(indio_dev, true);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(dev, as73211_power_disable, indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL, as73211_trigger_handler, NULL);
	if (ret)
		return ret;

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
				NULL,
				as73211_ready_handler,
				IRQF_ONESHOT,
				client->name, indio_dev);
		if (ret)
			return ret;
	}

	return devm_iio_device_register(dev, indio_dev);
}

static int as73211_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));

	return as73211_power(indio_dev, false);
}

static int as73211_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));

	return as73211_power(indio_dev, true);
}

static DEFINE_SIMPLE_DEV_PM_OPS(as73211_pm_ops, as73211_suspend,
				as73211_resume);

static const struct as73211_spec_dev_data as73211_spec = {
	.intensity_scale = as73211_intensity_scale,
	.channels = as73211_channels,
	.num_channels = ARRAY_SIZE(as73211_channels),
};

static const struct as73211_spec_dev_data as7331_spec = {
	.intensity_scale = as7331_intensity_scale,
	.channels = as7331_channels,
	.num_channels = ARRAY_SIZE(as7331_channels),
};

static const struct of_device_id as73211_of_match[] = {
	{ .compatible = "ams,as73211", &as73211_spec },
	{ .compatible = "ams,as7331", &as7331_spec },
	{ }
};
MODULE_DEVICE_TABLE(of, as73211_of_match);

static const struct i2c_device_id as73211_id[] = {
	{ "as73211", (kernel_ulong_t)&as73211_spec },
	{ "as7331", (kernel_ulong_t)&as7331_spec },
	{ }
};
MODULE_DEVICE_TABLE(i2c, as73211_id);

static struct i2c_driver as73211_driver = {
	.driver = {
		.name           = AS73211_DRV_NAME,
		.of_match_table = as73211_of_match,
		.pm             = pm_sleep_ptr(&as73211_pm_ops),
	},
	.probe      = as73211_probe,
	.id_table   = as73211_id,
};
module_i2c_driver(as73211_driver);

MODULE_AUTHOR("Christian Eggers <ceggers@arri.de>");
MODULE_DESCRIPTION("AS73211 XYZ True Color Sensor driver");
MODULE_LICENSE("GPL");
