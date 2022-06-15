// SPDX-License-Identifier: GPL-2.0-only
/*
 * vl6180.c - Support for STMicroelectronics VL6180 ALS, range and proximity
 * sensor
 *
 * Copyright 2017 Peter Meerwald-Stadler <pmeerw@pmeerw.net>
 * Copyright 2017 Manivannan Sadhasivam <manivannanece23@gmail.com>
 *
 * IIO driver for VL6180 (7-bit I2C slave address 0x29)
 *
 * Range: 0 to 100mm
 * ALS: < 1 Lux up to 100 kLux
 * IR: 850nm
 *
 * TODO: threshold events, continuous mode
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/util_macros.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/buffer.h>

#define VL6180_DRV_NAME "vl6180"

/* Device identification register and value */
#define VL6180_MODEL_ID	0x000
#define VL6180_MODEL_ID_VAL 0xb4

/* Configuration registers */
#define VL6180_SYS_MODE_GPIO1 0x011
#define VL6180_INTR_CONFIG 0x014
#define VL6180_INTR_CLEAR 0x015
#define VL6180_OUT_OF_RESET 0x016
#define VL6180_HOLD 0x017
#define VL6180_RANGE_START 0x018
#define VL6180_RANGE_INTER_MES_PERIOD 0x01b
#define VL6180_ALS_START 0x038
#define VL6180_ALS_THRESH_HIGH 0x03a
#define VL6180_ALS_THRESH_LOW 0x03c
#define VL6180_ALS_INTER_MES_PERIOD 0x03e
#define VL6180_ALS_GAIN 0x03f
#define VL6180_ALS_IT 0x040

/* Status registers */
#define VL6180_RANGE_STATUS 0x04d
#define VL6180_ALS_STATUS 0x04e
#define VL6180_INTR_STATUS 0x04f

/* Result value registers */
#define VL6180_ALS_VALUE 0x050
#define VL6180_RANGE_VALUE 0x062
#define VL6180_RANGE_RATE 0x066

#define VL6180_RANGE_THRESH_HIGH 0x019
#define VL6180_RANGE_THRESH_LOW 0x01a
#define VL6180_RANGE_MAX_CONVERGENCE_TIME 0x01c
#define VL6180_RANGE_CROSSTALK_COMPENSATION_RATE 0x01e
#define VL6180_RANGE_PART_TO_PART_RANGE_OFFSET 0x024
#define VL6180_RANGE_RANGE_IGNORE_VALID_HEIGHT 0x025
#define VL6180_RANGE_RANGE_IGNORE_THRESHOLD 0x026
#define VL6180_RANGE_MAX_AMBIENT_LEVEL_MULT 0x02c
#define VL6180_RANGE_RANGE_CHECK_ENABLES 0x02d
#define VL6180_RANGE_VHV_RECALIBRATE 0x02e
#define VL6180_RANGE_VHV_REPEAT_RATE 0x031
#define VL6180_READOUT_AVERAGING_SAMPLE_PERIOD 0x10a

/* bits of the SYS_MODE_GPIO1 register */
#define VL6180_SYS_GPIO1_POLARITY BIT(5) /* active high */
#define VL6180_SYS_GPIO1_SELECT BIT(4) /* configure GPIO interrupt output */

/* bits of the RANGE_START and ALS_START register */
#define VL6180_MODE_CONT BIT(1) /* continuous mode */
#define VL6180_STARTSTOP BIT(0) /* start measurement, auto-reset */

/* bits of the INTR_STATUS and INTR_CONFIG register */
#define VL6180_ALS_LEVEL_LOW BIT(3)
#define VL6180_ALS_LEVEL_HIGH BIT(4)
#define VL6180_ALS_OUT_OF_WINDOW (BIT(3) | BIT(4))
#define VL6180_ALS_READY BIT(5)
#define VL6180_RANGE_LEVEL_LOW BIT(0)
#define VL6180_RANGE_LEVEL_HIGH BIT(1)
#define VL6180_RANGE_OUT_OF_WINDOW (BIT(0) | BIT(1))
#define VL6180_RANGE_READY BIT(2)
#define VL6180_INT_RANGE_GPIO_MASK GENMASK(2, 0)
#define VL6180_INT_ALS_GPIO_MASK GENMASK(5, 3)
#define VL6180_INT_ERR_GPIO_MASK GENMASK(7, 6)

/* bits of the INTR_CLEAR register */
#define VL6180_CLEAR_ERROR BIT(2)
#define VL6180_CLEAR_ALS BIT(1)
#define VL6180_CLEAR_RANGE BIT(0)

/* bits of the HOLD register */
#define VL6180_HOLD_ON BIT(0)

/* default value for the ALS_IT register */
#define VL6180_ALS_IT_100 0x63 /* 100 ms */

/* values for the ALS_GAIN register */
#define VL6180_ALS_GAIN_1 0x46
#define VL6180_ALS_GAIN_1_25 0x45
#define VL6180_ALS_GAIN_1_67 0x44
#define VL6180_ALS_GAIN_2_5 0x43
#define VL6180_ALS_GAIN_5 0x42
#define VL6180_ALS_GAIN_10 0x41
#define VL6180_ALS_GAIN_20 0x40
#define VL6180_ALS_GAIN_40 0x47

struct vl6180_data {
	struct i2c_client *client;
	struct mutex lock;
	unsigned int als_gain_milli;
	unsigned int als_it_ms;
	struct gpio_desc *avdd;
	struct gpio_desc *chip_enable;

	/* Ensure natural alignment of timestamp */
	struct {
		u16 channels[3];
		u16 reserved;
		s64 ts;
	} scan;
};

enum { VL6180_ALS, VL6180_RANGE, VL6180_PROX };

/**
 * struct vl6180_chan_regs - Registers for accessing channels
 * @drdy_mask:			Data ready bit in status register
 * @start_reg:			Conversion start register
 * @value_reg:			Result value register
 * @word:			Register word length
 */
struct vl6180_chan_regs {
	u8 drdy_mask;
	u16 start_reg, value_reg;
	bool word;
};

static const struct vl6180_chan_regs vl6180_chan_regs_table[] = {
	[VL6180_ALS] = {
		.drdy_mask = VL6180_ALS_READY,
		.start_reg = VL6180_ALS_START,
		.value_reg = VL6180_ALS_VALUE,
		.word = true,
	},
	[VL6180_RANGE] = {
		.drdy_mask = VL6180_RANGE_READY,
		.start_reg = VL6180_RANGE_START,
		.value_reg = VL6180_RANGE_VALUE,
		.word = false,
	},
	[VL6180_PROX] = {
		.drdy_mask = VL6180_RANGE_READY,
		.start_reg = VL6180_RANGE_START,
		.value_reg = VL6180_RANGE_RATE,
		.word = true,
	},
};

/**
 * struct vl6180_custom_data - Data for custom initialization
 * @reg:			Register
 * @val:			Value
 */
struct vl6180_custom_data {
	u16 reg;
	u8 val;
};

static const struct vl6180_custom_data vl6180_custom_data_table[] = {
	{ .reg = 0x207, .val = 0x01, },
	{ .reg = 0x208, .val = 0x01, },
	{ .reg = 0x096, .val = 0x00, },
	{ .reg = 0x097, .val = 0xfd, },
	{ .reg = 0x0e3, .val = 0x00, },
	{ .reg = 0x0e4, .val = 0x04, },
	{ .reg = 0x0e5, .val = 0x02, },
	{ .reg = 0x0e6, .val = 0x01, },
	{ .reg = 0x0e7, .val = 0x03, },
	{ .reg = 0x0f5, .val = 0x02, },
	{ .reg = 0x0d9, .val = 0x05, },
	{ .reg = 0x0db, .val = 0xce, },
	{ .reg = 0x0dc, .val = 0x03, },
	{ .reg = 0x0dd, .val = 0xf8, },
	{ .reg = 0x09f, .val = 0x00, },
	{ .reg = 0x0a3, .val = 0x3c, },
	{ .reg = 0x0b7, .val = 0x00, },
	{ .reg = 0x0bb, .val = 0x3c, },
	{ .reg = 0x0b2, .val = 0x09, },
	{ .reg = 0x0ca, .val = 0x09, },
	{ .reg = 0x198, .val = 0x01, },
	{ .reg = 0x1b0, .val = 0x17, },
	{ .reg = 0x1ad, .val = 0x00, },
	{ .reg = 0x0ff, .val = 0x05, },
	{ .reg = 0x100, .val = 0x05, },
	{ .reg = 0x199, .val = 0x05, },
	{ .reg = 0x1a6, .val = 0x1b, },
	{ .reg = 0x1ac, .val = 0x3e, },
	{ .reg = 0x1a7, .val = 0x1f, },
	{ .reg = 0x030, .val = 0x00, },
};

static int vl6180_read(struct i2c_client *client, u16 cmd, void *databuf,
		       u8 len)
{
	__be16 cmdbuf = cpu_to_be16(cmd);
	struct i2c_msg msgs[2] = {
		{ .addr = client->addr, .len = sizeof(cmdbuf), .buf = (u8 *) &cmdbuf },
		{ .addr = client->addr, .len = len, .buf = databuf,
		  .flags = I2C_M_RD } };
	int ret;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		dev_err(&client->dev, "failed reading register 0x%04x\n", cmd);

	return ret;
}

static int vl6180_read_byte(struct i2c_client *client, u16 cmd)
{
	u8 data;
	int ret;

	ret = vl6180_read(client, cmd, &data, sizeof(data));
	if (ret < 0)
		return ret;

	return data;
}

static int vl6180_read_word(struct i2c_client *client, u16 cmd)
{
	__be16 data;
	int ret;

	ret = vl6180_read(client, cmd, &data, sizeof(data));
	if (ret < 0)
		return ret;

	return be16_to_cpu(data);
}

static int vl6180_write_byte(struct i2c_client *client, u16 cmd, u8 val)
{
	u8 buf[3];
	struct i2c_msg msgs[1] = {
		{ .addr = client->addr, .len = sizeof(buf), .buf = (u8 *) &buf } };
	int ret;

	buf[0] = cmd >> 8;
	buf[1] = cmd & 0xff;
	buf[2] = val;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0) {
		dev_err(&client->dev, "failed writing register 0x%04x\n", cmd);
		return ret;
	}

	return 0;
}

static int vl6180_write_word(struct i2c_client *client, u16 cmd, u16 val)
{
	__be16 buf[2];
	struct i2c_msg msgs[1] = {
		{ .addr = client->addr, .len = sizeof(buf), .buf = (u8 *) &buf } };
	int ret;

	buf[0] = cpu_to_be16(cmd);
	buf[1] = cpu_to_be16(val);

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0) {
		dev_err(&client->dev, "failed writing register 0x%04x\n", cmd);
		return ret;
	}

	return 0;
}

static int vl6180_measure(struct vl6180_data *data, int addr)
{
	struct i2c_client *client = data->client;
	int tries = 20, ret;
	u16 value;

	mutex_lock(&data->lock);
	/* Start single shot measurement */
	ret = vl6180_write_byte(client,
		vl6180_chan_regs_table[addr].start_reg, VL6180_STARTSTOP);
	if (ret < 0)
		goto fail;

	while (tries--) {
		ret = vl6180_read_byte(client, VL6180_INTR_STATUS);
		if (ret < 0)
			goto fail;

		if (ret & vl6180_chan_regs_table[addr].drdy_mask)
			break;
		msleep(20);
	}

	if (tries < 0) {
		ret = -EIO;
		goto fail;
	}

	/* Read result value from appropriate registers */
	ret = vl6180_chan_regs_table[addr].word ?
		vl6180_read_word(client, vl6180_chan_regs_table[addr].value_reg) :
		vl6180_read_byte(client, vl6180_chan_regs_table[addr].value_reg);
	if (ret < 0)
		goto fail;
	value = ret;

	/* Clear the interrupt flag after data read */
	ret = vl6180_write_byte(client, VL6180_INTR_CLEAR,
		VL6180_CLEAR_ERROR | VL6180_CLEAR_ALS | VL6180_CLEAR_RANGE);
	if (ret < 0)
		goto fail;

	ret = value;

fail:
	mutex_unlock(&data->lock);

	return ret;
}

static const struct iio_chan_spec vl6180_channels[] = {
	{
		.type = IIO_LIGHT,
		.address = VL6180_ALS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_INT_TIME) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_HARDWAREGAIN),
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
		}
	}, {
		.type = IIO_DISTANCE,
		.address = VL6180_RANGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 1,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
		}
	}, {
		.type = IIO_PROXIMITY,
		.address = VL6180_PROX,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = 2,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
		}
	},
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

/*
 * Available Ambient Light Sensor gain settings, 1/1000th, and
 * corresponding setting for the VL6180_ALS_GAIN register
 */
static const int vl6180_als_gain_tab[8] = {
	1000, 1250, 1670, 2500, 5000, 10000, 20000, 40000
};
static const u8 vl6180_als_gain_tab_bits[8] = {
	VL6180_ALS_GAIN_1,    VL6180_ALS_GAIN_1_25,
	VL6180_ALS_GAIN_1_67, VL6180_ALS_GAIN_2_5,
	VL6180_ALS_GAIN_5,    VL6180_ALS_GAIN_10,
	VL6180_ALS_GAIN_20,   VL6180_ALS_GAIN_40
};

static int vl6180_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct vl6180_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = vl6180_measure(data, chan->address);
		if (ret < 0)
			return ret;
		*val = ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		*val = data->als_it_ms;
		*val2 = 1000;

		return IIO_VAL_FRACTIONAL;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_LIGHT:
			/* one ALS count is 0.32 Lux @ gain 1, IT 100 ms */
			*val = 32000; /* 0.32 * 1000 * 100 */
			*val2 = data->als_gain_milli * data->als_it_ms;

			return IIO_VAL_FRACTIONAL;

		case IIO_DISTANCE:
			*val = 0; /* sensor reports mm, scale to meter */
			*val2 = 1000;
			break;
		default:
			return -EINVAL;
		}

		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_HARDWAREGAIN:
		*val = data->als_gain_milli;
		*val2 = 1000;

		return IIO_VAL_FRACTIONAL;

	default:
		return -EINVAL;
	}
}

static IIO_CONST_ATTR(als_gain_available, "1 1.25 1.67 2.5 5 10 20 40");

static struct attribute *vl6180_attributes[] = {
	&iio_const_attr_als_gain_available.dev_attr.attr,
	NULL
};

static const struct attribute_group vl6180_attribute_group = {
	.attrs = vl6180_attributes,
};

/* HOLD is needed before updating any config registers */
static int vl6180_hold(struct vl6180_data *data, bool hold)
{
	return vl6180_write_byte(data->client, VL6180_HOLD,
		hold ? VL6180_HOLD_ON : 0);
}

static int vl6180_set_als_gain(struct vl6180_data *data, int val, int val2)
{
	int i, ret, gain;

	if (val < 1 || val > 40)
		return -EINVAL;

	gain = (val * 1000000 + val2) / 1000;
	if (gain < 1 || gain > 40000)
		return -EINVAL;

	i = find_closest(gain, vl6180_als_gain_tab,
			 ARRAY_SIZE(vl6180_als_gain_tab));

	mutex_lock(&data->lock);
	ret = vl6180_hold(data, true);
	if (ret < 0)
		goto fail;

	ret = vl6180_write_byte(data->client, VL6180_ALS_GAIN,
				vl6180_als_gain_tab_bits[i]);

	if (ret >= 0)
		data->als_gain_milli = vl6180_als_gain_tab[i];

fail:
	vl6180_hold(data, false);
	mutex_unlock(&data->lock);
	return ret;
}

static int vl6180_set_it(struct vl6180_data *data, int val, int val2)
{
	int ret, it_ms;

	it_ms = (val2 + 500) / 1000; /* round to ms */
	if (val != 0 || it_ms < 1 || it_ms > 512)
		return -EINVAL;

	mutex_lock(&data->lock);
	ret = vl6180_hold(data, true);
	if (ret < 0)
		goto fail;

	ret = vl6180_write_word(data->client, VL6180_ALS_IT, it_ms - 1);

	if (ret >= 0)
		data->als_it_ms = it_ms;

fail:
	vl6180_hold(data, false);
	mutex_unlock(&data->lock);

	return ret;
}

static int vl6180_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct vl6180_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return vl6180_set_it(data, val, val2);

	case IIO_CHAN_INFO_HARDWAREGAIN:
		if (chan->type != IIO_LIGHT)
			return -EINVAL;

		return vl6180_set_als_gain(data, val, val2);
	default:
		return -EINVAL;
	}
}

static const struct iio_info vl6180_info = {
	.read_raw = vl6180_read_raw,
	.write_raw = vl6180_write_raw,
	.attrs = &vl6180_attribute_group,
};

static int vl6180_power_enable(struct vl6180_data *data)
{
	/* Enable power supply. */
	if (!IS_ERR_OR_NULL(data->avdd))
		gpiod_set_value_cansleep(data->avdd, 1);

	/* Power-up default is chip enable (CE). */
	if (!IS_ERR_OR_NULL(data->chip_enable)) {
		gpiod_set_value_cansleep(data->chip_enable, 0);
		usleep_range(500, 1000);
		gpiod_set_value_cansleep(data->chip_enable, 1);
	}

	return 0;
}

static int vl6180_custom_init(struct vl6180_data *data)
{
	struct i2c_client *client = data->client;
	int ret;
	int i;

	/* REGISTER_TUNING_SR03_270514_CustomerView.txt */
	for (i = 0; i < ARRAY_SIZE(vl6180_custom_data_table); ++i) {
		ret = vl6180_write_byte(client,
					vl6180_custom_data_table[i].reg,
					vl6180_custom_data_table[i].val);

		if (ret < 0)
			break;
	}

	return ret;
}

static int vl6180_range_init(struct vl6180_data *data)
{
	struct i2c_client *client = data->client;
	int ret;
	u8 enables;
	u8 offset;
	u8 xtalk = 3;

	/* Enables polling for ‘New Sample ready’ when measurement completes */
	ret = vl6180_write_byte(client, VL6180_SYS_MODE_GPIO1,
				(VL6180_SYS_GPIO1_POLARITY |
				 VL6180_SYS_GPIO1_SELECT));
	if (ret < 0)
		goto out;

	/* Set the averaging sample period (compromise between lower noise and
	 * increased execution time), 0x30 equals to 4.3 ms.
	 */
	ret = vl6180_write_byte(client, VL6180_READOUT_AVERAGING_SAMPLE_PERIOD,
				0x30);
	if (ret < 0)
		goto out;

	/* Sets the # of range measurements after which auto calibration of
	 * system is performed
	 */
	ret = vl6180_write_byte(client, VL6180_RANGE_VHV_REPEAT_RATE, 0xff);
	if (ret < 0)
		goto out;

	/* Perform a single temperature calibration of the ranging sensor */
	ret = vl6180_write_byte(client, VL6180_RANGE_VHV_RECALIBRATE, 0x01);
	if (ret < 0)
		goto out;

	/* Set SNR limit to 0.06 */
	ret = vl6180_write_byte(client, VL6180_RANGE_MAX_AMBIENT_LEVEL_MULT,
				0xff);
	if (ret < 0)
		goto out;

	/* Set default ranging inter-measurement period to 100ms */
	ret = vl6180_write_byte(client, VL6180_RANGE_INTER_MES_PERIOD, 0x09);
	if (ret < 0)
		goto out;

	/* Copy registers */
	/* NOTE: 0x0da, 0x027, 0x0db, 0x028, 0x0dc, 0x029 and 0x0dd are
	 * unavailable on the datasheet.
	 */
	ret = vl6180_read_byte(client, VL6180_RANGE_RANGE_IGNORE_THRESHOLD);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, 0x0da, ret);
	if (ret < 0)
		goto out;

	ret = vl6180_read_byte(client, 0x027);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, 0x0db, ret);
	if (ret < 0)
		goto out;

	ret = vl6180_read_byte(client, 0x028);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, 0x0dc, ret);
	if (ret < 0)
		goto out;

	ret = vl6180_read_byte(client, 0x029);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, 0x0dd, ret);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, VL6180_RANGE_MAX_CONVERGENCE_TIME, 0x32);
	if (ret < 0)
		goto out;

	ret = vl6180_read_byte(client, VL6180_RANGE_RANGE_CHECK_ENABLES);
	if (ret < 0)
		goto out;

	/* Disable early convergence */
	enables = ret & 0xfe;
	ret = vl6180_write_byte(client, VL6180_RANGE_RANGE_CHECK_ENABLES, enables);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, VL6180_RANGE_THRESH_HIGH, 0xc8);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, VL6180_RANGE_THRESH_LOW, 0x00);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, VL6180_ALS_IT, VL6180_ALS_IT_100);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, VL6180_ALS_INTER_MES_PERIOD, 0x13);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, VL6180_ALS_GAIN, VL6180_ALS_GAIN_1);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, VL6180_ALS_THRESH_LOW, 0x00);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, VL6180_ALS_THRESH_HIGH, 0xff);
	if (ret < 0)
		goto out;

	/* Cover glass ignore */
	ret = vl6180_write_byte(client,
				VL6180_RANGE_RANGE_IGNORE_VALID_HEIGHT, 0xff);
	if (ret < 0)
		goto out;

	ret = vl6180_read_byte(client, VL6180_RANGE_PART_TO_PART_RANGE_OFFSET);
	if (ret < 0)
		goto out;

	/* Apply default calibration on part to part offset */
	offset = ret / 4;
	ret = vl6180_write_byte(client, VL6180_RANGE_PART_TO_PART_RANGE_OFFSET,
				offset);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client,
				VL6180_RANGE_CROSSTALK_COMPENSATION_RATE,
				0x00);
	if (ret < 0)
		goto out;

	ret = vl6180_write_byte(client, 0x01f, xtalk);

out:
	return ret;
}

static int vl6180_init(struct vl6180_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	ret = vl6180_power_enable(data);
	if (ret) {
		dev_err(&client->dev, "failed to configure power\n");
		return ret;
	}

	/*
	 * After the MCU boot sequence the device enters software standby,
	 * host initialization can commence immediately after entering
	 * software standby.
	 */
	usleep_range(500, 1000);

	ret = vl6180_read_byte(client, VL6180_MODEL_ID);
	if (ret < 0)
		return ret;

	if (ret != VL6180_MODEL_ID_VAL) {
		dev_err(&client->dev, "invalid model ID %02x\n", ret);
		return -ENODEV;
	}

	ret = vl6180_hold(data, true);
	if (ret < 0)
		return ret;

	ret = vl6180_read_byte(client, VL6180_OUT_OF_RESET);
	if (ret < 0)
		return ret;

	/*
	 * Detect false reset condition here. This bit is always set when the
	 * system comes out of reset.
	 */
	if (ret != 0x01)
		dev_info(&client->dev, "device is not fresh out of reset\n");

	/* ALS integration time: 100ms */
	data->als_it_ms = 100;
	ret = vl6180_write_word(client, VL6180_ALS_IT, VL6180_ALS_IT_100);
	if (ret < 0)
		return ret;

	/* ALS gain: 1 */
	data->als_gain_milli = 1000;
	ret = vl6180_write_byte(client, VL6180_ALS_GAIN, VL6180_ALS_GAIN_1);
	if (ret < 0)
		return ret;

	ret = vl6180_custom_init(data);
	if (ret < 0)
		return ret;

	ret = vl6180_range_init(data);
	if (ret < 0)
		return ret;

	ret = vl6180_write_byte(client, VL6180_RANGE_START,
				(VL6180_STARTSTOP | VL6180_MODE_CONT));
	if (ret < 0)
		return ret;

	ret = vl6180_write_byte(client, VL6180_OUT_OF_RESET, 0x00);
	if (ret < 0)
		return ret;

	return vl6180_hold(data, false);
}

static irqreturn_t vl6180_irq_thread(int irq, void *priv)
{
	struct vl6180_data *data = priv;
	struct i2c_client *client = data->client;
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	int ret;
	u8 val = 0;

	ret = vl6180_read_byte(client, VL6180_INTR_STATUS);
	if (ret < 0)
		goto out;

	if (ret & VL6180_INT_ALS_GPIO_MASK)
		val |= VL6180_CLEAR_ALS;

	if (ret & VL6180_INT_RANGE_GPIO_MASK)
		val |= VL6180_CLEAR_RANGE;

	if (ret & VL6180_INT_ERR_GPIO_MASK)
		val |= VL6180_CLEAR_ERROR;

	vl6180_write_byte(client, VL6180_INTR_CLEAR, val);

	ret = vl6180_read_word(client, VL6180_ALS_VALUE);
	if (ret < 0)
		goto out;
	data->scan.channels[VL6180_ALS] = ret;

	ret = vl6180_read_byte(client, VL6180_RANGE_VALUE);
	if (ret < 0)
		goto out;
	data->scan.channels[VL6180_RANGE] = ret;

	ret = vl6180_read_word(client, VL6180_RANGE_RATE);
	if (ret < 0)
		goto out;
	data->scan.channels[VL6180_PROX] = ret;

	iio_push_to_buffers_with_timestamp(indio_dev, &data->scan,
					   ktime_get_boottime_ns());

out:
	return IRQ_HANDLED;
}

static int vl6180_buffer_preenable(struct iio_dev *indio_dev)
{
	struct vl6180_data *data = iio_priv(indio_dev);
	u8 val;
	int ret;

	ret = vl6180_read_byte(data->client, VL6180_INTR_CONFIG);
	if (ret < 0)
		return ret;

	/* Enable ALS and Range ready interrupts */
	val = ret | VL6180_ALS_READY | VL6180_RANGE_READY;
	ret = vl6180_write_byte(data->client, VL6180_INTR_CONFIG, val);

	return ret;
}

static int vl6180_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct vl6180_data *data = iio_priv(indio_dev);
	u8 val;
	int ret;

	ret = vl6180_read_byte(data->client, VL6180_INTR_CONFIG);
	if (ret < 0)
		return ret;

	/* Disable ALS and Range ready interrupts */
	val = ret & ~(VL6180_ALS_READY | VL6180_RANGE_READY);
	ret = vl6180_write_byte(data->client, VL6180_INTR_CONFIG, val);

	return ret;
}

static const struct iio_buffer_setup_ops vl6180_buffer_setup_ops = {
	.preenable = vl6180_buffer_preenable,
	.postdisable = vl6180_buffer_postdisable,
};

static int vl6180_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct vl6180_data *data;
	struct iio_dev *indio_dev;
	struct iio_buffer *buffer;
	u32 type;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	indio_dev->info = &vl6180_info;
	indio_dev->channels = vl6180_channels;
	indio_dev->num_channels = ARRAY_SIZE(vl6180_channels);
	indio_dev->name = VL6180_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/*
	 * NOTE: If the power is controlled by gpio, the power
	 * configuration should match the power-up timing.
	 */
	data->avdd = devm_gpiod_get_optional(&client->dev, "avdd",
					     GPIOD_OUT_HIGH);
	data->chip_enable = devm_gpiod_get_optional(&client->dev, "chip-enable",
						    GPIOD_OUT_HIGH);

	ret = vl6180_init(data);
	if (ret < 0)
		return ret;

	if (client->irq) {
		buffer = devm_iio_kfifo_allocate(&client->dev);
		if (!buffer)
			return -ENOMEM;

		iio_device_attach_buffer(indio_dev, buffer);
		indio_dev->modes |= INDIO_BUFFER_SOFTWARE;
		indio_dev->setup_ops = &vl6180_buffer_setup_ops;

		type = irqd_get_trigger_type(irq_get_irq_data(client->irq));
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, vl6180_irq_thread,
						type | IRQF_ONESHOT, "vl6180",
						data);
		if (ret) {
			dev_err(&client->dev,
				"failed to request vl6180 IRQ\n");
			return ret;
		}
	}

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct of_device_id vl6180_of_match[] = {
	{ .compatible = "st,vl6180", },
	{ },
};
MODULE_DEVICE_TABLE(of, vl6180_of_match);

static const struct i2c_device_id vl6180_id[] = {
	{ "vl6180", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vl6180_id);

static struct i2c_driver vl6180_driver = {
	.driver = {
		.name   = VL6180_DRV_NAME,
		.of_match_table = vl6180_of_match,
	},
	.probe  = vl6180_probe,
	.id_table = vl6180_id,
};

module_i2c_driver(vl6180_driver);

MODULE_AUTHOR("Peter Meerwald-Stadler <pmeerw@pmeerw.net>");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannanece23@gmail.com>");
MODULE_DESCRIPTION("STMicro VL6180 ALS, range and proximity sensor driver");
MODULE_LICENSE("GPL");
