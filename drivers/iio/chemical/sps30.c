// SPDX-License-Identifier: GPL-2.0
/*
 * Sensirion SPS30 particulate matter sensor driver
 *
 * Copyright (c) Tomasz Duszynski <tduszyns@gmail.com>
 *
 * I2C slave address: 0x69
 */

#include <asm/unaligned.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define SPS30_CRC8_POLYNOMIAL 0x31
/* max number of bytes needed to store PM measurements or serial string */
#define SPS30_MAX_READ_SIZE 48
/* sensor measures reliably up to 3000 ug / m3 */
#define SPS30_MAX_PM 3000
/* minimum and maximum self cleaning periods in seconds */
#define SPS30_AUTO_CLEANING_PERIOD_MIN 0
#define SPS30_AUTO_CLEANING_PERIOD_MAX 604800

/* SPS30 commands */
#define SPS30_START_MEAS 0x0010
#define SPS30_STOP_MEAS 0x0104
#define SPS30_RESET 0xd304
#define SPS30_READ_DATA_READY_FLAG 0x0202
#define SPS30_READ_DATA 0x0300
#define SPS30_READ_SERIAL 0xd033
#define SPS30_START_FAN_CLEANING 0x5607
#define SPS30_AUTO_CLEANING_PERIOD 0x8004
/* not a sensor command per se, used only to distinguish write from read */
#define SPS30_READ_AUTO_CLEANING_PERIOD 0x8005

enum {
	PM1,
	PM2P5,
	PM4,
	PM10,
};

enum {
	RESET,
	MEASURING,
};

struct sps30_state {
	struct i2c_client *client;
	/*
	 * Guards against concurrent access to sensor registers.
	 * Must be held whenever sequence of commands is to be executed.
	 */
	struct mutex lock;
	int state;
};

DECLARE_CRC8_TABLE(sps30_crc8_table);

static int sps30_write_then_read(struct sps30_state *state, u8 *txbuf,
				 int txsize, u8 *rxbuf, int rxsize)
{
	int ret;

	/*
	 * Sensor does not support repeated start so instead of
	 * sending two i2c messages in a row we just send one by one.
	 */
	ret = i2c_master_send(state->client, txbuf, txsize);
	if (ret != txsize)
		return ret < 0 ? ret : -EIO;

	if (!rxbuf)
		return 0;

	ret = i2c_master_recv(state->client, rxbuf, rxsize);
	if (ret != rxsize)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int sps30_do_cmd(struct sps30_state *state, u16 cmd, u8 *data, int size)
{
	/*
	 * Internally sensor stores measurements in a following manner:
	 *
	 * PM1: upper two bytes, crc8, lower two bytes, crc8
	 * PM2P5: upper two bytes, crc8, lower two bytes, crc8
	 * PM4: upper two bytes, crc8, lower two bytes, crc8
	 * PM10: upper two bytes, crc8, lower two bytes, crc8
	 *
	 * What follows next are number concentration measurements and
	 * typical particle size measurement which we omit.
	 */
	u8 buf[SPS30_MAX_READ_SIZE] = { cmd >> 8, cmd };
	int i, ret = 0;

	switch (cmd) {
	case SPS30_START_MEAS:
		buf[2] = 0x03;
		buf[3] = 0x00;
		buf[4] = crc8(sps30_crc8_table, &buf[2], 2, CRC8_INIT_VALUE);
		ret = sps30_write_then_read(state, buf, 5, NULL, 0);
		break;
	case SPS30_STOP_MEAS:
	case SPS30_RESET:
	case SPS30_START_FAN_CLEANING:
		ret = sps30_write_then_read(state, buf, 2, NULL, 0);
		break;
	case SPS30_READ_AUTO_CLEANING_PERIOD:
		buf[0] = SPS30_AUTO_CLEANING_PERIOD >> 8;
		buf[1] = (u8)(SPS30_AUTO_CLEANING_PERIOD & 0xff);
		/* fall through */
	case SPS30_READ_DATA_READY_FLAG:
	case SPS30_READ_DATA:
	case SPS30_READ_SERIAL:
		/* every two data bytes are checksummed */
		size += size / 2;
		ret = sps30_write_then_read(state, buf, 2, buf, size);
		break;
	case SPS30_AUTO_CLEANING_PERIOD:
		buf[2] = data[0];
		buf[3] = data[1];
		buf[4] = crc8(sps30_crc8_table, &buf[2], 2, CRC8_INIT_VALUE);
		buf[5] = data[2];
		buf[6] = data[3];
		buf[7] = crc8(sps30_crc8_table, &buf[5], 2, CRC8_INIT_VALUE);
		ret = sps30_write_then_read(state, buf, 8, NULL, 0);
		break;
	}

	if (ret)
		return ret;

	/* validate received data and strip off crc bytes */
	for (i = 0; i < size; i += 3) {
		u8 crc = crc8(sps30_crc8_table, &buf[i], 2, CRC8_INIT_VALUE);

		if (crc != buf[i + 2]) {
			dev_err(&state->client->dev,
				"data integrity check failed\n");
			return -EIO;
		}

		*data++ = buf[i];
		*data++ = buf[i + 1];
	}

	return 0;
}

static s32 sps30_float_to_int_clamped(const u8 *fp)
{
	int val = get_unaligned_be32(fp);
	int mantissa = val & GENMASK(22, 0);
	/* this is fine since passed float is always non-negative */
	int exp = val >> 23;
	int fraction, shift;

	/* special case 0 */
	if (!exp && !mantissa)
		return 0;

	exp -= 127;
	if (exp < 0) {
		/* return values ranging from 1 to 99 */
		return ((((1 << 23) + mantissa) * 100) >> 23) >> (-exp);
	}

	/* return values ranging from 100 to 300000 */
	shift = 23 - exp;
	val = (1 << exp) + (mantissa >> shift);
	if (val >= SPS30_MAX_PM)
		return SPS30_MAX_PM * 100;

	fraction = mantissa & GENMASK(shift - 1, 0);

	return val * 100 + ((fraction * 100) >> shift);
}

static int sps30_do_meas(struct sps30_state *state, s32 *data, int size)
{
	int i, ret, tries = 5;
	u8 tmp[16];

	if (state->state == RESET) {
		ret = sps30_do_cmd(state, SPS30_START_MEAS, NULL, 0);
		if (ret)
			return ret;

		state->state = MEASURING;
	}

	while (tries--) {
		ret = sps30_do_cmd(state, SPS30_READ_DATA_READY_FLAG, tmp, 2);
		if (ret)
			return -EIO;

		/* new measurements ready to be read */
		if (tmp[1] == 1)
			break;

		msleep_interruptible(300);
	}

	if (tries == -1)
		return -ETIMEDOUT;

	ret = sps30_do_cmd(state, SPS30_READ_DATA, tmp, sizeof(int) * size);
	if (ret)
		return ret;

	for (i = 0; i < size; i++)
		data[i] = sps30_float_to_int_clamped(&tmp[4 * i]);

	return 0;
}

static irqreturn_t sps30_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct sps30_state *state = iio_priv(indio_dev);
	int ret;
	struct {
		s32 data[4]; /* PM1, PM2P5, PM4, PM10 */
		s64 ts;
	} scan;

	mutex_lock(&state->lock);
	ret = sps30_do_meas(state, scan.data, ARRAY_SIZE(scan.data));
	mutex_unlock(&state->lock);
	if (ret)
		goto err;

	iio_push_to_buffers_with_timestamp(indio_dev, &scan,
					   iio_get_time_ns(indio_dev));
err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int sps30_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	struct sps30_state *state = iio_priv(indio_dev);
	int data[4], ret = -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_MASSCONCENTRATION:
			mutex_lock(&state->lock);
			/* read up to the number of bytes actually needed */
			switch (chan->channel2) {
			case IIO_MOD_PM1:
				ret = sps30_do_meas(state, data, 1);
				break;
			case IIO_MOD_PM2P5:
				ret = sps30_do_meas(state, data, 2);
				break;
			case IIO_MOD_PM4:
				ret = sps30_do_meas(state, data, 3);
				break;
			case IIO_MOD_PM10:
				ret = sps30_do_meas(state, data, 4);
				break;
			}
			mutex_unlock(&state->lock);
			if (ret)
				return ret;

			*val = data[chan->address] / 100;
			*val2 = (data[chan->address] % 100) * 10000;

			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_MASSCONCENTRATION:
			switch (chan->channel2) {
			case IIO_MOD_PM1:
			case IIO_MOD_PM2P5:
			case IIO_MOD_PM4:
			case IIO_MOD_PM10:
				*val = 0;
				*val2 = 10000;

				return IIO_VAL_INT_PLUS_MICRO;
			default:
				return -EINVAL;
			}
		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}

static int sps30_do_cmd_reset(struct sps30_state *state)
{
	int ret;

	ret = sps30_do_cmd(state, SPS30_RESET, NULL, 0);
	msleep(300);
	/*
	 * Power-on-reset causes sensor to produce some glitch on i2c bus and
	 * some controllers end up in error state. Recover simply by placing
	 * some data on the bus, for example STOP_MEAS command, which
	 * is NOP in this case.
	 */
	sps30_do_cmd(state, SPS30_STOP_MEAS, NULL, 0);
	state->state = RESET;

	return ret;
}

static ssize_t start_cleaning_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sps30_state *state = iio_priv(indio_dev);
	int val, ret;

	if (kstrtoint(buf, 0, &val) || val != 1)
		return -EINVAL;

	mutex_lock(&state->lock);
	ret = sps30_do_cmd(state, SPS30_START_FAN_CLEANING, NULL, 0);
	mutex_unlock(&state->lock);
	if (ret)
		return ret;

	return len;
}

static ssize_t cleaning_period_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sps30_state *state = iio_priv(indio_dev);
	u8 tmp[4];
	int ret;

	mutex_lock(&state->lock);
	ret = sps30_do_cmd(state, SPS30_READ_AUTO_CLEANING_PERIOD, tmp, 4);
	mutex_unlock(&state->lock);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", get_unaligned_be32(tmp));
}

static ssize_t cleaning_period_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sps30_state *state = iio_priv(indio_dev);
	int val, ret;
	u8 tmp[4];

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	if ((val < SPS30_AUTO_CLEANING_PERIOD_MIN) ||
	    (val > SPS30_AUTO_CLEANING_PERIOD_MAX))
		return -EINVAL;

	put_unaligned_be32(val, tmp);

	mutex_lock(&state->lock);
	ret = sps30_do_cmd(state, SPS30_AUTO_CLEANING_PERIOD, tmp, 0);
	if (ret) {
		mutex_unlock(&state->lock);
		return ret;
	}

	msleep(20);

	/*
	 * sensor requires reset in order to return up to date self cleaning
	 * period
	 */
	ret = sps30_do_cmd_reset(state);
	if (ret)
		dev_warn(dev,
			 "period changed but reads will return the old value\n");

	mutex_unlock(&state->lock);

	return len;
}

static ssize_t cleaning_period_available_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	return snprintf(buf, PAGE_SIZE, "[%d %d %d]\n",
			SPS30_AUTO_CLEANING_PERIOD_MIN, 1,
			SPS30_AUTO_CLEANING_PERIOD_MAX);
}

static IIO_DEVICE_ATTR_WO(start_cleaning, 0);
static IIO_DEVICE_ATTR_RW(cleaning_period, 0);
static IIO_DEVICE_ATTR_RO(cleaning_period_available, 0);

static struct attribute *sps30_attrs[] = {
	&iio_dev_attr_start_cleaning.dev_attr.attr,
	&iio_dev_attr_cleaning_period.dev_attr.attr,
	&iio_dev_attr_cleaning_period_available.dev_attr.attr,
	NULL
};

static const struct attribute_group sps30_attr_group = {
	.attrs = sps30_attrs,
};

static const struct iio_info sps30_info = {
	.attrs = &sps30_attr_group,
	.read_raw = sps30_read_raw,
};

#define SPS30_CHAN(_index, _mod) { \
	.type = IIO_MASSCONCENTRATION, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## _mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.address = _mod, \
	.scan_index = _index, \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 19, \
		.storagebits = 32, \
		.endianness = IIO_CPU, \
	}, \
}

static const struct iio_chan_spec sps30_channels[] = {
	SPS30_CHAN(0, PM1),
	SPS30_CHAN(1, PM2P5),
	SPS30_CHAN(2, PM4),
	SPS30_CHAN(3, PM10),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static void sps30_stop_meas(void *data)
{
	struct sps30_state *state = data;

	sps30_do_cmd(state, SPS30_STOP_MEAS, NULL, 0);
}

static const unsigned long sps30_scan_masks[] = { 0x0f, 0x00 };

static int sps30_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct sps30_state *state;
	u8 buf[32];
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	state = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	state->client = client;
	state->state = RESET;
	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &sps30_info;
	indio_dev->name = client->name;
	indio_dev->channels = sps30_channels;
	indio_dev->num_channels = ARRAY_SIZE(sps30_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->available_scan_masks = sps30_scan_masks;

	mutex_init(&state->lock);
	crc8_populate_msb(sps30_crc8_table, SPS30_CRC8_POLYNOMIAL);

	ret = sps30_do_cmd_reset(state);
	if (ret) {
		dev_err(&client->dev, "failed to reset device\n");
		return ret;
	}

	ret = sps30_do_cmd(state, SPS30_READ_SERIAL, buf, sizeof(buf));
	if (ret) {
		dev_err(&client->dev, "failed to read serial number\n");
		return ret;
	}
	/* returned serial number is already NUL terminated */
	dev_info(&client->dev, "serial number: %s\n", buf);

	ret = devm_add_action_or_reset(&client->dev, sps30_stop_meas, state);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(&client->dev, indio_dev, NULL,
					      sps30_trigger_handler, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id sps30_id[] = {
	{ "sps30" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sps30_id);

static const struct of_device_id sps30_of_match[] = {
	{ .compatible = "sensirion,sps30" },
	{ }
};
MODULE_DEVICE_TABLE(of, sps30_of_match);

static struct i2c_driver sps30_driver = {
	.driver = {
		.name = "sps30",
		.of_match_table = sps30_of_match,
	},
	.id_table = sps30_id,
	.probe_new = sps30_probe,
};
module_i2c_driver(sps30_driver);

MODULE_AUTHOR("Tomasz Duszynski <tduszyns@gmail.com>");
MODULE_DESCRIPTION("Sensirion SPS30 particulate matter sensor driver");
MODULE_LICENSE("GPL v2");
