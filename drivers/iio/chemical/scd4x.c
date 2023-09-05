// SPDX-License-Identifier: GPL-2.0
/*
 * Sensirion SCD4X carbon dioxide sensor i2c driver
 *
 * Copyright (C) 2021 Protonic Holland
 * Author: Roan van Dijk <roan@protonic.nl>
 *
 * I2C slave address: 0x62
 *
 * Datasheets:
 * https://www.sensirion.com/file/datasheet_scd4x
 */

#include <asm/unaligned.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/types.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#define SCD4X_CRC8_POLYNOMIAL 0x31
#define SCD4X_TIMEOUT_ERR 1000
#define SCD4X_READ_BUF_SIZE 9
#define SCD4X_COMMAND_BUF_SIZE 2
#define SCD4X_WRITE_BUF_SIZE 5
#define SCD4X_FRC_MIN_PPM 0
#define SCD4X_FRC_MAX_PPM 2000
#define SCD4X_READY_MASK 0x01

/*Commands SCD4X*/
enum scd4x_cmd {
	CMD_START_MEAS          = 0x21b1,
	CMD_READ_MEAS           = 0xec05,
	CMD_STOP_MEAS           = 0x3f86,
	CMD_SET_TEMP_OFFSET     = 0x241d,
	CMD_GET_TEMP_OFFSET     = 0x2318,
	CMD_FRC                 = 0x362f,
	CMD_SET_ASC             = 0x2416,
	CMD_GET_ASC             = 0x2313,
	CMD_GET_DATA_READY      = 0xe4b8,
};

enum scd4x_channel_idx {
	SCD4X_CO2,
	SCD4X_TEMP,
	SCD4X_HR,
};

struct scd4x_state {
	struct i2c_client *client;
	/* maintain access to device, to prevent concurrent reads/writes */
	struct mutex lock;
	struct regulator *vdd;
};

DECLARE_CRC8_TABLE(scd4x_crc8_table);

static int scd4x_i2c_xfer(struct scd4x_state *state, char *txbuf, int txsize,
				char *rxbuf, int rxsize)
{
	struct i2c_client *client = state->client;
	int ret;

	ret = i2c_master_send(client, txbuf, txsize);

	if (ret < 0)
		return ret;
	if (ret != txsize)
		return -EIO;

	if (rxsize == 0)
		return 0;

	ret = i2c_master_recv(client, rxbuf, rxsize);
	if (ret < 0)
		return ret;
	if (ret != rxsize)
		return -EIO;

	return 0;
}

static int scd4x_send_command(struct scd4x_state *state, enum scd4x_cmd cmd)
{
	char buf[SCD4X_COMMAND_BUF_SIZE];
	int ret;

	/*
	 * Measurement needs to be stopped before sending commands.
	 * Except stop and start command.
	 */
	if ((cmd != CMD_STOP_MEAS) && (cmd != CMD_START_MEAS)) {

		ret = scd4x_send_command(state, CMD_STOP_MEAS);
		if (ret)
			return ret;

		/* execution time for stopping measurement */
		msleep_interruptible(500);
	}

	put_unaligned_be16(cmd, buf);
	ret = scd4x_i2c_xfer(state, buf, 2, buf, 0);
	if (ret)
		return ret;

	if ((cmd != CMD_STOP_MEAS) && (cmd != CMD_START_MEAS)) {
		ret = scd4x_send_command(state, CMD_START_MEAS);
		if (ret)
			return ret;
	}

	return 0;
}

static int scd4x_read(struct scd4x_state *state, enum scd4x_cmd cmd,
			void *response, int response_sz)
{
	struct i2c_client *client = state->client;
	char buf[SCD4X_READ_BUF_SIZE];
	char *rsp = response;
	int i, ret;
	char crc;

	/*
	 * Measurement needs to be stopped before sending commands.
	 * Except for reading measurement and data ready command.
	 */
	if ((cmd != CMD_GET_DATA_READY) && (cmd != CMD_READ_MEAS)) {
		ret = scd4x_send_command(state, CMD_STOP_MEAS);
		if (ret)
			return ret;

		/* execution time for stopping measurement */
		msleep_interruptible(500);
	}

	/* CRC byte for every 2 bytes of data */
	response_sz += response_sz / 2;

	put_unaligned_be16(cmd, buf);
	ret = scd4x_i2c_xfer(state, buf, 2, buf, response_sz);
	if (ret)
		return ret;

	for (i = 0; i < response_sz; i += 3) {
		crc = crc8(scd4x_crc8_table, buf + i, 2, CRC8_INIT_VALUE);
		if (crc != buf[i + 2]) {
			dev_err(&client->dev, "CRC error\n");
			return -EIO;
		}

		*rsp++ = buf[i];
		*rsp++ = buf[i + 1];
	}

	/* start measurement */
	if ((cmd != CMD_GET_DATA_READY) && (cmd != CMD_READ_MEAS)) {
		ret = scd4x_send_command(state, CMD_START_MEAS);
		if (ret)
			return ret;
	}

	return 0;
}

static int scd4x_write(struct scd4x_state *state, enum scd4x_cmd cmd, uint16_t arg)
{
	char buf[SCD4X_WRITE_BUF_SIZE];
	int ret;
	char crc;

	put_unaligned_be16(cmd, buf);
	put_unaligned_be16(arg, buf + 2);

	crc = crc8(scd4x_crc8_table, buf + 2, 2, CRC8_INIT_VALUE);
	buf[4] = crc;

	/* measurement needs to be stopped before sending commands */
	ret = scd4x_send_command(state, CMD_STOP_MEAS);
	if (ret)
		return ret;

	/* execution time */
	msleep_interruptible(500);

	ret = scd4x_i2c_xfer(state, buf, SCD4X_WRITE_BUF_SIZE, buf, 0);
	if (ret)
		return ret;

	/* start measurement, except for forced calibration command */
	if (cmd != CMD_FRC) {
		ret = scd4x_send_command(state, CMD_START_MEAS);
		if (ret)
			return ret;
	}

	return 0;
}

static int scd4x_write_and_fetch(struct scd4x_state *state, enum scd4x_cmd cmd,
				uint16_t arg, void *response, int response_sz)
{
	struct i2c_client *client = state->client;
	char buf[SCD4X_READ_BUF_SIZE];
	char *rsp = response;
	int i, ret;
	char crc;

	ret = scd4x_write(state, CMD_FRC, arg);
	if (ret)
		goto err;

	/* execution time */
	msleep_interruptible(400);

	/* CRC byte for every 2 bytes of data */
	response_sz += response_sz / 2;

	ret = i2c_master_recv(client, buf, response_sz);
	if (ret < 0)
		goto err;
	if (ret != response_sz) {
		ret = -EIO;
		goto err;
	}

	for (i = 0; i < response_sz; i += 3) {
		crc = crc8(scd4x_crc8_table, buf + i, 2, CRC8_INIT_VALUE);
		if (crc != buf[i + 2]) {
			dev_err(&client->dev, "CRC error\n");
			ret = -EIO;
			goto err;
		}

		*rsp++ = buf[i];
		*rsp++ = buf[i + 1];
	}

	return scd4x_send_command(state, CMD_START_MEAS);

err:
	/*
	 * on error try to start the measurement,
	 * puts sensor back into continuous measurement
	 */
	scd4x_send_command(state, CMD_START_MEAS);

	return ret;
}

static int scd4x_read_meas(struct scd4x_state *state, uint16_t *meas)
{
	int i, ret;
	__be16 buf[3];

	ret = scd4x_read(state, CMD_READ_MEAS, buf, sizeof(buf));
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(buf); i++)
		meas[i] = be16_to_cpu(buf[i]);

	return 0;
}

static int scd4x_wait_meas_poll(struct scd4x_state *state)
{
	struct i2c_client *client = state->client;
	int tries = 6;
	int ret;

	do {
		__be16 bval;
		uint16_t val;

		ret = scd4x_read(state, CMD_GET_DATA_READY, &bval, sizeof(bval));
		if (ret)
			return -EIO;
		val = be16_to_cpu(bval);

		/* new measurement available */
		if (val & 0x7FF)
			return 0;

		msleep_interruptible(1000);
	} while (--tries);

	/* try to start sensor on timeout */
	ret = scd4x_send_command(state, CMD_START_MEAS);
	if (ret)
		dev_err(&client->dev, "failed to start measurement: %d\n", ret);

	return -ETIMEDOUT;
}

static int scd4x_read_poll(struct scd4x_state *state, uint16_t *buf)
{
	int ret;

	ret = scd4x_wait_meas_poll(state);
	if (ret)
		return ret;

	return scd4x_read_meas(state, buf);
}

static int scd4x_read_channel(struct scd4x_state *state, int chan)
{
	int ret;
	uint16_t buf[3];

	ret = scd4x_read_poll(state, buf);
	if (ret)
		return ret;

	return buf[chan];
}

static int scd4x_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan, int *val,
			int *val2, long mask)
{
	struct scd4x_state *state = iio_priv(indio_dev);
	int ret;
	__be16 tmp;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		mutex_lock(&state->lock);
		ret = scd4x_read_channel(state, chan->address);
		mutex_unlock(&state->lock);

		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;

		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_CONCENTRATION) {
			*val = 0;
			*val2 = 100;
			return IIO_VAL_INT_PLUS_MICRO;
		} else if (chan->type == IIO_TEMP) {
			*val = 175000;
			*val2 = 65536;
			return IIO_VAL_FRACTIONAL;
		} else if (chan->type == IIO_HUMIDITYRELATIVE) {
			*val = 100000;
			*val2 = 65536;
			return IIO_VAL_FRACTIONAL;
		}
		return -EINVAL;
	case IIO_CHAN_INFO_OFFSET:
		*val = -16852;
		*val2 = 114286;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBBIAS:
		mutex_lock(&state->lock);
		ret = scd4x_read(state, CMD_GET_TEMP_OFFSET, &tmp, sizeof(tmp));
		mutex_unlock(&state->lock);
		if (ret)
			return ret;

		*val = be16_to_cpu(tmp);

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int scd4x_write_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	struct scd4x_state *state = iio_priv(indio_dev);
	int ret = 0;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		mutex_lock(&state->lock);
		ret = scd4x_write(state, CMD_SET_TEMP_OFFSET, val);
		mutex_unlock(&state->lock);

		return ret;
	default:
		return -EINVAL;
	}
}

static ssize_t calibration_auto_enable_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct scd4x_state *state = iio_priv(indio_dev);
	int ret;
	__be16 bval;
	u16 val;

	mutex_lock(&state->lock);
	ret = scd4x_read(state, CMD_GET_ASC, &bval, sizeof(bval));
	mutex_unlock(&state->lock);
	if (ret) {
		dev_err(dev, "failed to read automatic calibration");
		return ret;
	}

	val = (be16_to_cpu(bval) & SCD4X_READY_MASK) ? 1 : 0;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t calibration_auto_enable_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct scd4x_state *state = iio_priv(indio_dev);
	bool val;
	int ret;
	uint16_t value;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	value = val;

	mutex_lock(&state->lock);
	ret = scd4x_write(state, CMD_SET_ASC, value);
	mutex_unlock(&state->lock);
	if (ret)
		dev_err(dev, "failed to set automatic calibration");

	return ret ?: len;
}

static ssize_t calibration_forced_value_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct scd4x_state *state = iio_priv(indio_dev);
	uint16_t val, arg;
	int ret;

	ret = kstrtou16(buf, 0, &arg);
	if (ret)
		return ret;

	if (arg < SCD4X_FRC_MIN_PPM || arg > SCD4X_FRC_MAX_PPM)
		return -EINVAL;

	mutex_lock(&state->lock);
	ret = scd4x_write_and_fetch(state, CMD_FRC, arg, &val, sizeof(val));
	mutex_unlock(&state->lock);

	if (ret)
		return ret;

	if (val == 0xff) {
		dev_err(dev, "forced calibration has failed");
		return -EINVAL;
	}

	return len;
}

static IIO_DEVICE_ATTR_RW(calibration_auto_enable, 0);
static IIO_DEVICE_ATTR_WO(calibration_forced_value, 0);

static IIO_CONST_ATTR(calibration_forced_value_available,
	       __stringify([SCD4X_FRC_MIN_PPM 1 SCD4X_FRC_MAX_PPM]));

static struct attribute *scd4x_attrs[] = {
	&iio_dev_attr_calibration_auto_enable.dev_attr.attr,
	&iio_dev_attr_calibration_forced_value.dev_attr.attr,
	&iio_const_attr_calibration_forced_value_available.dev_attr.attr,
	NULL
};

static const struct attribute_group scd4x_attr_group = {
	.attrs = scd4x_attrs,
};

static const struct iio_info scd4x_info = {
	.attrs = &scd4x_attr_group,
	.read_raw = scd4x_read_raw,
	.write_raw = scd4x_write_raw,
};

static const struct iio_chan_spec scd4x_channels[] = {
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_CO2,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
					BIT(IIO_CHAN_INFO_SCALE),
		.address = SCD4X_CO2,
		.scan_index = SCD4X_CO2,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
					BIT(IIO_CHAN_INFO_SCALE) |
					BIT(IIO_CHAN_INFO_OFFSET) |
					BIT(IIO_CHAN_INFO_CALIBBIAS),
		.address = SCD4X_TEMP,
		.scan_index = SCD4X_TEMP,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
					BIT(IIO_CHAN_INFO_SCALE),
		.address = SCD4X_HR,
		.scan_index = SCD4X_HR,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
};

static int scd4x_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct scd4x_state *state  = iio_priv(indio_dev);
	int ret;

	ret = scd4x_send_command(state, CMD_STOP_MEAS);
	if (ret)
		return ret;

	return regulator_disable(state->vdd);
}

static int scd4x_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct scd4x_state *state = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(state->vdd);
	if (ret)
		return ret;

	return scd4x_send_command(state, CMD_START_MEAS);
}

static DEFINE_SIMPLE_DEV_PM_OPS(scd4x_pm_ops, scd4x_suspend, scd4x_resume);

static void scd4x_stop_meas(void *state)
{
	scd4x_send_command(state, CMD_STOP_MEAS);
}

static void scd4x_disable_regulator(void *data)
{
	struct scd4x_state *state = data;

	regulator_disable(state->vdd);
}

static irqreturn_t scd4x_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct scd4x_state *state = iio_priv(indio_dev);
	struct {
		uint16_t data[3];
		int64_t ts __aligned(8);
	} scan;
	int ret;

	memset(&scan, 0, sizeof(scan));
	mutex_lock(&state->lock);
	ret = scd4x_read_poll(state, scan.data);
	mutex_unlock(&state->lock);
	if (ret)
		goto out;

	iio_push_to_buffers_with_timestamp(indio_dev, &scan, iio_get_time_ns(indio_dev));
out:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int scd4x_probe(struct i2c_client *client)
{
	static const unsigned long scd4x_scan_masks[] = { 0x07, 0x00 };
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct scd4x_state *state;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	state = iio_priv(indio_dev);
	mutex_init(&state->lock);
	state->client = client;
	crc8_populate_msb(scd4x_crc8_table, SCD4X_CRC8_POLYNOMIAL);

	indio_dev->info = &scd4x_info;
	indio_dev->name = client->name;
	indio_dev->channels = scd4x_channels;
	indio_dev->num_channels = ARRAY_SIZE(scd4x_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->available_scan_masks = scd4x_scan_masks;

	state->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(state->vdd))
		return dev_err_probe(dev, PTR_ERR(state->vdd), "failed to get regulator\n");

	ret = regulator_enable(state->vdd);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, scd4x_disable_regulator, state);
	if (ret)
		return ret;

	ret = scd4x_send_command(state, CMD_STOP_MEAS);
	if (ret) {
		dev_err(dev, "failed to stop measurement: %d\n", ret);
		return ret;
	}

	/* execution time */
	msleep_interruptible(500);

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL, scd4x_trigger_handler, NULL);
	if (ret)
		return ret;

	ret = scd4x_send_command(state, CMD_START_MEAS);
	if (ret) {
		dev_err(dev, "failed to start measurement: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, scd4x_stop_meas, state);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id scd4x_dt_ids[] = {
	{ .compatible = "sensirion,scd40" },
	{ .compatible = "sensirion,scd41" },
	{ }
};
MODULE_DEVICE_TABLE(of, scd4x_dt_ids);

static struct i2c_driver scd4x_i2c_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = scd4x_dt_ids,
		.pm = pm_sleep_ptr(&scd4x_pm_ops),
	},
	.probe = scd4x_probe,
};
module_i2c_driver(scd4x_i2c_driver);

MODULE_AUTHOR("Roan van Dijk <roan@protonic.nl>");
MODULE_DESCRIPTION("Sensirion SCD4X carbon dioxide sensor core driver");
MODULE_LICENSE("GPL v2");
