// SPDX-License-Identifier: GPL-2.0
/*
 * Sensirion SPS30 particulate matter sensor i2c driver
 *
 * Copyright (c) 2020 Tomasz Duszynski <tomasz.duszynski@octakon.com>
 *
 * I2C slave address: 0x69
 */
#include <linux/unaligned.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/types.h>

#include "sps30.h"

#define SPS30_I2C_CRC8_POLYNOMIAL 0x31
/* max number of bytes needed to store PM measurements or serial string */
#define SPS30_I2C_MAX_BUF_SIZE 48

DECLARE_CRC8_TABLE(sps30_i2c_crc8_table);

#define SPS30_I2C_START_MEAS 0x0010
#define SPS30_I2C_STOP_MEAS 0x0104
#define SPS30_I2C_READ_MEAS 0x0300
#define SPS30_I2C_MEAS_READY 0x0202
#define SPS30_I2C_RESET 0xd304
#define SPS30_I2C_CLEAN_FAN 0x5607
#define SPS30_I2C_PERIOD 0x8004
#define SPS30_I2C_READ_SERIAL 0xd033
#define SPS30_I2C_READ_VERSION 0xd100

static int sps30_i2c_xfer(struct sps30_state *state, unsigned char *txbuf, size_t txsize,
			  unsigned char *rxbuf, size_t rxsize)
{
	struct i2c_client *client = to_i2c_client(state->dev);
	int ret;

	/*
	 * Sensor does not support repeated start so instead of
	 * sending two i2c messages in a row we just send one by one.
	 */
	ret = i2c_master_send(client, txbuf, txsize);
	if (ret < 0)
		return ret;
	if (ret != txsize)
		return -EIO;

	if (!rxsize)
		return 0;

	ret = i2c_master_recv(client, rxbuf, rxsize);
	if (ret < 0)
		return ret;
	if (ret != rxsize)
		return -EIO;

	return 0;
}

static int sps30_i2c_command(struct sps30_state *state, u16 cmd, void *arg, size_t arg_size,
			     void *rsp, size_t rsp_size)
{
	/*
	 * Internally sensor stores measurements in a following manner:
	 *
	 * PM1:   upper two bytes, crc8, lower two bytes, crc8
	 * PM2P5: upper two bytes, crc8, lower two bytes, crc8
	 * PM4:   upper two bytes, crc8, lower two bytes, crc8
	 * PM10:  upper two bytes, crc8, lower two bytes, crc8
	 *
	 * What follows next are number concentration measurements and
	 * typical particle size measurement which we omit.
	 */
	unsigned char buf[SPS30_I2C_MAX_BUF_SIZE];
	unsigned char *tmp;
	unsigned char crc;
	size_t i;
	int ret;

	put_unaligned_be16(cmd, buf);
	i = 2;

	if (rsp) {
		/* each two bytes are followed by a crc8 */
		rsp_size += rsp_size / 2;
	} else {
		tmp = arg;

		while (arg_size) {
			buf[i] = *tmp++;
			buf[i + 1] = *tmp++;
			buf[i + 2] = crc8(sps30_i2c_crc8_table, buf + i, 2, CRC8_INIT_VALUE);
			arg_size -= 2;
			i += 3;
		}
	}

	ret = sps30_i2c_xfer(state, buf, i, buf, rsp_size);
	if (ret)
		return ret;

	/* validate received data and strip off crc bytes */
	tmp = rsp;
	for (i = 0; i < rsp_size; i += 3) {
		crc = crc8(sps30_i2c_crc8_table, buf + i, 2, CRC8_INIT_VALUE);
		if (crc != buf[i + 2]) {
			dev_err(state->dev, "data integrity check failed\n");
			return -EIO;
		}

		*tmp++ = buf[i];
		*tmp++ = buf[i + 1];
	}

	return 0;
}

static int sps30_i2c_start_meas(struct sps30_state *state)
{
	/* request BE IEEE754 formatted data */
	unsigned char buf[] = { 0x03, 0x00 };

	return sps30_i2c_command(state, SPS30_I2C_START_MEAS, buf, sizeof(buf), NULL, 0);
}

static int sps30_i2c_stop_meas(struct sps30_state *state)
{
	return sps30_i2c_command(state, SPS30_I2C_STOP_MEAS, NULL, 0, NULL, 0);
}

static int sps30_i2c_reset(struct sps30_state *state)
{
	int ret;

	ret = sps30_i2c_command(state, SPS30_I2C_RESET, NULL, 0, NULL, 0);
	msleep(500);
	/*
	 * Power-on-reset causes sensor to produce some glitch on i2c bus and
	 * some controllers end up in error state. Recover simply by placing
	 * some data on the bus, for example STOP_MEAS command, which
	 * is NOP in this case.
	 */
	sps30_i2c_stop_meas(state);

	return ret;
}

static bool sps30_i2c_meas_ready(struct sps30_state *state)
{
	unsigned char buf[2];
	int ret;

	ret = sps30_i2c_command(state, SPS30_I2C_MEAS_READY, NULL, 0, buf, sizeof(buf));
	if (ret)
		return false;

	return buf[1];
}

static int sps30_i2c_read_meas(struct sps30_state *state, __be32 *meas, size_t num)
{
	/* measurements are ready within a second */
	if (msleep_interruptible(1000))
		return -EINTR;

	if (!sps30_i2c_meas_ready(state))
		return -ETIMEDOUT;

	return sps30_i2c_command(state, SPS30_I2C_READ_MEAS, NULL, 0, meas, sizeof(num) * num);
}

static int sps30_i2c_clean_fan(struct sps30_state *state)
{
	return sps30_i2c_command(state, SPS30_I2C_CLEAN_FAN, NULL, 0, NULL, 0);
}

static int sps30_i2c_read_cleaning_period(struct sps30_state *state, __be32 *period)
{
	return sps30_i2c_command(state, SPS30_I2C_PERIOD, NULL, 0, period, sizeof(*period));
}

static int sps30_i2c_write_cleaning_period(struct sps30_state *state, __be32 period)
{
	return sps30_i2c_command(state, SPS30_I2C_PERIOD, &period, sizeof(period), NULL, 0);
}

static int sps30_i2c_show_info(struct sps30_state *state)
{
	/* extra nul just in case */
	unsigned char buf[32 + 1] = { 0x00 };
	int ret;

	ret = sps30_i2c_command(state, SPS30_I2C_READ_SERIAL, NULL, 0, buf, sizeof(buf) - 1);
	if (ret)
		return ret;

	dev_info(state->dev, "serial number: %s\n", buf);

	ret = sps30_i2c_command(state, SPS30_I2C_READ_VERSION, NULL, 0, buf, 2);
	if (ret)
		return ret;

	dev_info(state->dev, "fw version: %u.%u\n", buf[0], buf[1]);

	return 0;
}

static const struct sps30_ops sps30_i2c_ops = {
	.start_meas = sps30_i2c_start_meas,
	.stop_meas = sps30_i2c_stop_meas,
	.read_meas = sps30_i2c_read_meas,
	.reset = sps30_i2c_reset,
	.clean_fan = sps30_i2c_clean_fan,
	.read_cleaning_period = sps30_i2c_read_cleaning_period,
	.write_cleaning_period = sps30_i2c_write_cleaning_period,
	.show_info = sps30_i2c_show_info,
};

static int sps30_i2c_probe(struct i2c_client *client)
{
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	crc8_populate_msb(sps30_i2c_crc8_table, SPS30_I2C_CRC8_POLYNOMIAL);

	return sps30_probe(&client->dev, client->name, NULL, &sps30_i2c_ops);
}

static const struct i2c_device_id sps30_i2c_id[] = {
	{ "sps30" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sps30_i2c_id);

static const struct of_device_id sps30_i2c_of_match[] = {
	{ .compatible = "sensirion,sps30" },
	{ }
};
MODULE_DEVICE_TABLE(of, sps30_i2c_of_match);

static struct i2c_driver sps30_i2c_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = sps30_i2c_of_match,
	},
	.id_table = sps30_i2c_id,
	.probe = sps30_i2c_probe,
};
module_i2c_driver(sps30_i2c_driver);

MODULE_AUTHOR("Tomasz Duszynski <tomasz.duszynski@octakon.com>");
MODULE_DESCRIPTION("Sensirion SPS30 particulate matter sensor i2c driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_SPS30");
