// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 InvenSense, Inc.
 *
 * Driver for InvenSense ICP-1010xx barometric pressure and temperature sensor.
 *
 * Datasheet:
 * http://www.invensense.com/wp-content/uploads/2018/01/DS-000186-ICP-101xx-v1.2.pdf
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/crc8.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/iio.h>

#define ICP10100_ID_REG_GET(_reg)	((_reg) & 0x003F)
#define ICP10100_ID_REG			0x08
#define ICP10100_RESPONSE_WORD_LENGTH	3
#define ICP10100_CRC8_WORD_LENGTH	2
#define ICP10100_CRC8_POLYNOMIAL	0x31
#define ICP10100_CRC8_INIT		0xFF

enum icp10100_mode {
	ICP10100_MODE_LP,	/* Low power mode: 1x sampling */
	ICP10100_MODE_N,	/* Normal mode: 2x sampling */
	ICP10100_MODE_LN,	/* Low noise mode: 4x sampling */
	ICP10100_MODE_ULN,	/* Ultra low noise mode: 8x sampling */
	ICP10100_MODE_NB,
};

struct icp10100_state {
	struct mutex lock;
	struct i2c_client *client;
	struct regulator *vdd;
	enum icp10100_mode mode;
	int16_t cal[4];
};

struct icp10100_command {
	__be16 cmd;
	unsigned long wait_us;
	unsigned long wait_max_us;
	size_t response_word_nb;
};

static const struct icp10100_command icp10100_cmd_soft_reset = {
	.cmd = cpu_to_be16(0x805D),
	.wait_us = 170,
	.wait_max_us = 200,
	.response_word_nb = 0,
};

static const struct icp10100_command icp10100_cmd_read_id = {
	.cmd = cpu_to_be16(0xEFC8),
	.wait_us = 0,
	.response_word_nb = 1,
};

static const struct icp10100_command icp10100_cmd_read_otp = {
	.cmd = cpu_to_be16(0xC7F7),
	.wait_us = 0,
	.response_word_nb = 1,
};

static const struct icp10100_command icp10100_cmd_measure[] = {
	[ICP10100_MODE_LP] = {
		.cmd = cpu_to_be16(0x401A),
		.wait_us = 1800,
		.wait_max_us = 2000,
		.response_word_nb = 3,
	},
	[ICP10100_MODE_N] = {
		.cmd = cpu_to_be16(0x48A3),
		.wait_us = 6300,
		.wait_max_us = 6500,
		.response_word_nb = 3,
	},
	[ICP10100_MODE_LN] = {
		.cmd = cpu_to_be16(0x5059),
		.wait_us = 23800,
		.wait_max_us = 24000,
		.response_word_nb = 3,
	},
	[ICP10100_MODE_ULN] = {
		.cmd = cpu_to_be16(0x58E0),
		.wait_us = 94500,
		.wait_max_us = 94700,
		.response_word_nb = 3,
	},
};

static const uint8_t icp10100_switch_mode_otp[] =
	{0xC5, 0x95, 0x00, 0x66, 0x9c};

DECLARE_CRC8_TABLE(icp10100_crc8_table);

static inline int icp10100_i2c_xfer(struct i2c_adapter *adap,
				    struct i2c_msg *msgs, int num)
{
	int ret;

	ret = i2c_transfer(adap, msgs, num);
	if (ret < 0)
		return ret;

	if (ret != num)
		return -EIO;

	return 0;
}

static int icp10100_send_cmd(struct icp10100_state *st,
			     const struct icp10100_command *cmd,
			     __be16 *buf, size_t buf_len)
{
	size_t size = cmd->response_word_nb * ICP10100_RESPONSE_WORD_LENGTH;
	uint8_t data[16];
	uint8_t *ptr;
	uint8_t *buf_ptr = (uint8_t *)buf;
	struct i2c_msg msgs[2] = {
		{
			.addr = st->client->addr,
			.flags = 0,
			.len = 2,
			.buf = (uint8_t *)&cmd->cmd,
		}, {
			.addr = st->client->addr,
			.flags = I2C_M_RD,
			.len = size,
			.buf = data,
		},
	};
	uint8_t crc;
	unsigned int i;
	int ret;

	if (size > sizeof(data))
		return -EINVAL;

	if (cmd->response_word_nb > 0 &&
			(buf == NULL || buf_len < (cmd->response_word_nb * 2)))
		return -EINVAL;

	dev_dbg(&st->client->dev, "sending cmd %#x\n", be16_to_cpu(cmd->cmd));

	if (cmd->response_word_nb > 0 && cmd->wait_us == 0) {
		/* direct command-response without waiting */
		ret = icp10100_i2c_xfer(st->client->adapter, msgs,
					ARRAY_SIZE(msgs));
		if (ret)
			return ret;
	} else {
		/* transfer command write */
		ret = icp10100_i2c_xfer(st->client->adapter, &msgs[0], 1);
		if (ret)
			return ret;
		if (cmd->wait_us > 0)
			usleep_range(cmd->wait_us, cmd->wait_max_us);
		/* transfer response read if needed */
		if (cmd->response_word_nb > 0) {
			ret = icp10100_i2c_xfer(st->client->adapter, &msgs[1], 1);
			if (ret)
				return ret;
		} else {
			return 0;
		}
	}

	/* process read words with crc checking */
	for (i = 0; i < cmd->response_word_nb; ++i) {
		ptr = &data[i * ICP10100_RESPONSE_WORD_LENGTH];
		crc = crc8(icp10100_crc8_table, ptr, ICP10100_CRC8_WORD_LENGTH,
			   ICP10100_CRC8_INIT);
		if (crc != ptr[ICP10100_CRC8_WORD_LENGTH]) {
			dev_err(&st->client->dev, "crc error recv=%#x calc=%#x\n",
				ptr[ICP10100_CRC8_WORD_LENGTH], crc);
			return -EIO;
		}
		*buf_ptr++ = ptr[0];
		*buf_ptr++ = ptr[1];
	}

	return 0;
}

static int icp10100_read_cal_otp(struct icp10100_state *st)
{
	__be16 val;
	int i;
	int ret;

	/* switch into OTP read mode */
	ret = i2c_master_send(st->client, icp10100_switch_mode_otp,
			      ARRAY_SIZE(icp10100_switch_mode_otp));
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(icp10100_switch_mode_otp))
		return -EIO;

	/* read 4 calibration values */
	for (i = 0; i < 4; ++i) {
		ret = icp10100_send_cmd(st, &icp10100_cmd_read_otp,
					&val, sizeof(val));
		if (ret)
			return ret;
		st->cal[i] = be16_to_cpu(val);
		dev_dbg(&st->client->dev, "cal[%d] = %d\n", i, st->cal[i]);
	}

	return 0;
}

static int icp10100_init_chip(struct icp10100_state *st)
{
	__be16 val;
	uint16_t id;
	int ret;

	/* read and check id */
	ret = icp10100_send_cmd(st, &icp10100_cmd_read_id, &val, sizeof(val));
	if (ret)
		return ret;
	id = ICP10100_ID_REG_GET(be16_to_cpu(val));
	if (id != ICP10100_ID_REG) {
		dev_err(&st->client->dev, "invalid id %#x\n", id);
		return -ENODEV;
	}

	/* read calibration data from OTP */
	ret = icp10100_read_cal_otp(st);
	if (ret)
		return ret;

	/* reset chip */
	return icp10100_send_cmd(st, &icp10100_cmd_soft_reset, NULL, 0);
}

static int icp10100_get_measures(struct icp10100_state *st,
				uint32_t *pressure, uint16_t *temperature)
{
	const struct icp10100_command *cmd;
	__be16 measures[3];
	int ret;

	ret = pm_runtime_resume_and_get(&st->client->dev);
	if (ret < 0)
		return ret;

	mutex_lock(&st->lock);
	cmd = &icp10100_cmd_measure[st->mode];
	ret = icp10100_send_cmd(st, cmd, measures, sizeof(measures));
	mutex_unlock(&st->lock);
	if (ret)
		goto error_measure;

	*pressure = (be16_to_cpu(measures[0]) << 8) |
			(be16_to_cpu(measures[1]) >> 8);
	*temperature = be16_to_cpu(measures[2]);

error_measure:
	pm_runtime_put_autosuspend(&st->client->dev);
	return ret;
}

static uint32_t icp10100_get_pressure(struct icp10100_state *st,
				      uint32_t raw_pressure, uint16_t raw_temp)
{
	static int32_t p_calib[] = {45000, 80000, 105000};
	static int32_t lut_lower = 3670016;
	static int32_t lut_upper = 12058624;
	static int32_t inv_quadr_factor = 16777216;
	static int32_t offset_factor = 2048;
	int64_t val1, val2;
	int32_t p_lut[3];
	int32_t t, t_square;
	int64_t a, b, c;
	uint32_t pressure_mPa;

	dev_dbg(&st->client->dev, "raw: pressure = %u, temp = %u\n",
		raw_pressure, raw_temp);

	/* compute p_lut values */
	t = (int32_t)raw_temp - 32768;
	t_square = t * t;
	val1 = (int64_t)st->cal[0] * (int64_t)t_square;
	p_lut[0] = lut_lower + (int32_t)div_s64(val1, inv_quadr_factor);
	val1 = (int64_t)st->cal[1] * (int64_t)t_square;
	p_lut[1] = offset_factor * st->cal[3] +
			(int32_t)div_s64(val1, inv_quadr_factor);
	val1 = (int64_t)st->cal[2] * (int64_t)t_square;
	p_lut[2] = lut_upper + (int32_t)div_s64(val1, inv_quadr_factor);
	dev_dbg(&st->client->dev, "p_lut = [%d, %d, %d]\n",
		p_lut[0], p_lut[1], p_lut[2]);

	/* compute a, b, c factors */
	val1 = (int64_t)p_lut[0] * (int64_t)p_lut[1] *
			(int64_t)(p_calib[0] - p_calib[1]) +
		(int64_t)p_lut[1] * (int64_t)p_lut[2] *
			(int64_t)(p_calib[1] - p_calib[2]) +
		(int64_t)p_lut[2] * (int64_t)p_lut[0] *
			(int64_t)(p_calib[2] - p_calib[0]);
	val2 = (int64_t)p_lut[2] * (int64_t)(p_calib[0] - p_calib[1]) +
		(int64_t)p_lut[0] * (int64_t)(p_calib[1] - p_calib[2]) +
		(int64_t)p_lut[1] * (int64_t)(p_calib[2] - p_calib[0]);
	c = div64_s64(val1, val2);
	dev_dbg(&st->client->dev, "val1 = %lld, val2 = %lld, c = %lld\n",
		val1, val2, c);
	val1 = (int64_t)p_calib[0] * (int64_t)p_lut[0] -
		(int64_t)p_calib[1] * (int64_t)p_lut[1] -
		(int64_t)(p_calib[1] - p_calib[0]) * c;
	val2 = (int64_t)p_lut[0] - (int64_t)p_lut[1];
	a = div64_s64(val1, val2);
	dev_dbg(&st->client->dev, "val1 = %lld, val2 = %lld, a = %lld\n",
		val1, val2, a);
	b = ((int64_t)p_calib[0] - a) * ((int64_t)p_lut[0] + c);
	dev_dbg(&st->client->dev, "b = %lld\n", b);

	/*
	 * pressure_Pa = a + (b / (c + raw_pressure))
	 * pressure_mPa = 1000 * pressure_Pa
	 */
	pressure_mPa = 1000LL * a + div64_s64(1000LL * b, c + raw_pressure);

	return pressure_mPa;
}

static int icp10100_read_raw_measures(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      int *val, int *val2)
{
	struct icp10100_state *st = iio_priv(indio_dev);
	uint32_t raw_pressure;
	uint16_t raw_temp;
	uint32_t pressure_mPa;
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = icp10100_get_measures(st, &raw_pressure, &raw_temp);
	if (ret)
		goto error_release;

	switch (chan->type) {
	case IIO_PRESSURE:
		pressure_mPa = icp10100_get_pressure(st, raw_pressure,
						     raw_temp);
		/* mPa to kPa */
		*val = pressure_mPa / 1000000;
		*val2 = pressure_mPa % 1000000;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_TEMP:
		*val = raw_temp;
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

error_release:
	iio_device_release_direct(indio_dev);
	return ret;
}

static int icp10100_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct icp10100_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		return icp10100_read_raw_measures(indio_dev, chan, val, val2);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_TEMP:
			/* 1000 * 175°C / 65536 in m°C */
			*val = 2;
			*val2 = 670288;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_TEMP:
			/* 1000 * -45°C in m°C */
			*val = -45000;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		mutex_lock(&st->lock);
		*val = 1 << st->mode;
		mutex_unlock(&st->lock);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int icp10100_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	static int oversamplings[] = {1, 2, 4, 8};

	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = oversamplings;
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(oversamplings);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int icp10100_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct icp10100_state *st = iio_priv(indio_dev);
	unsigned int mode;

	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		/* oversampling is always positive and a power of 2 */
		if (val <= 0 || !is_power_of_2(val))
			return -EINVAL;
		mode = ilog2(val);
		if (mode >= ICP10100_MODE_NB)
			return -EINVAL;
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		mutex_lock(&st->lock);
		st->mode = mode;
		mutex_unlock(&st->lock);
		iio_device_release_direct(indio_dev);
		return 0;
	default:
		return -EINVAL;
	}
}

static int icp10100_write_raw_get_fmt(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info icp10100_info = {
	.read_raw = icp10100_read_raw,
	.read_avail = icp10100_read_avail,
	.write_raw = icp10100_write_raw,
	.write_raw_get_fmt = icp10100_write_raw_get_fmt,
};

static const struct iio_chan_spec icp10100_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all =
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available =
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	}, {
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_all =
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all_available =
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
	},
};

static int icp10100_enable_regulator(struct icp10100_state *st)
{
	int ret;

	ret = regulator_enable(st->vdd);
	if (ret)
		return ret;
	msleep(100);

	return 0;
}

static void icp10100_disable_regulator_action(void *data)
{
	struct icp10100_state *st = data;
	int ret;

	ret = regulator_disable(st->vdd);
	if (ret)
		dev_err(&st->client->dev, "error %d disabling vdd\n", ret);
}

static void icp10100_pm_disable(void *data)
{
	struct device *dev = data;

	pm_runtime_disable(dev);
}

static int icp10100_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct icp10100_state *st;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "plain i2c transactions not supported\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	i2c_set_clientdata(client, indio_dev);
	indio_dev->name = client->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = icp10100_channels;
	indio_dev->num_channels = ARRAY_SIZE(icp10100_channels);
	indio_dev->info = &icp10100_info;

	st = iio_priv(indio_dev);
	mutex_init(&st->lock);
	st->client = client;
	st->mode = ICP10100_MODE_N;

	st->vdd = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(st->vdd))
		return PTR_ERR(st->vdd);

	ret = icp10100_enable_regulator(st);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&client->dev,
				       icp10100_disable_regulator_action, st);
	if (ret)
		return ret;

	/* has to be done before the first i2c communication */
	crc8_populate_msb(icp10100_crc8_table, ICP10100_CRC8_POLYNOMIAL);

	ret = icp10100_init_chip(st);
	if (ret) {
		dev_err(&client->dev, "init chip error %d\n", ret);
		return ret;
	}

	/* enable runtime pm with autosuspend delay of 2s */
	pm_runtime_get_noresume(&client->dev);
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, 2000);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_put(&client->dev);
	ret = devm_add_action_or_reset(&client->dev, icp10100_pm_disable,
				       &client->dev);
	if (ret)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static int icp10100_suspend(struct device *dev)
{
	struct icp10100_state *st = iio_priv(dev_get_drvdata(dev));
	int ret;

	mutex_lock(&st->lock);
	ret = regulator_disable(st->vdd);
	mutex_unlock(&st->lock);

	return ret;
}

static int icp10100_resume(struct device *dev)
{
	struct icp10100_state *st = iio_priv(dev_get_drvdata(dev));
	int ret;

	mutex_lock(&st->lock);

	ret = icp10100_enable_regulator(st);
	if (ret)
		goto out_unlock;

	/* reset chip */
	ret = icp10100_send_cmd(st, &icp10100_cmd_soft_reset, NULL, 0);

out_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(icp10100_pm, icp10100_suspend, icp10100_resume,
				 NULL);

static const struct of_device_id icp10100_of_match[] = {
	{
		.compatible = "invensense,icp10100",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, icp10100_of_match);

static const struct i2c_device_id icp10100_id[] = {
	{ "icp10100" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, icp10100_id);

static struct i2c_driver icp10100_driver = {
	.driver = {
		.name = "icp10100",
		.pm = pm_ptr(&icp10100_pm),
		.of_match_table = icp10100_of_match,
	},
	.probe = icp10100_probe,
	.id_table = icp10100_id,
};
module_i2c_driver(icp10100_driver);

MODULE_AUTHOR("InvenSense, Inc.");
MODULE_DESCRIPTION("InvenSense icp10100 driver");
MODULE_LICENSE("GPL");
