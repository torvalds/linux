// SPDX-License-Identifier: GPL-2.0
/*
 * Sensirion SCD30 carbon dioxide sensor i2c driver
 *
 * Copyright (c) 2020 Tomasz Duszynski <tomasz.duszynski@octakon.com>
 *
 * I2C slave address: 0x61
 */
#include <linux/crc8.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#include "scd30.h"

#define SCD30_I2C_MAX_BUF_SIZE 18
#define SCD30_I2C_CRC8_POLYNOMIAL 0x31

static u16 scd30_i2c_cmd_lookup_tbl[] = {
	[CMD_START_MEAS] = 0x0010,
	[CMD_STOP_MEAS] = 0x0104,
	[CMD_MEAS_INTERVAL] = 0x4600,
	[CMD_MEAS_READY] = 0x0202,
	[CMD_READ_MEAS] = 0x0300,
	[CMD_ASC] = 0x5306,
	[CMD_FRC] = 0x5204,
	[CMD_TEMP_OFFSET] = 0x5403,
	[CMD_FW_VERSION] = 0xd100,
	[CMD_RESET] = 0xd304,
};

DECLARE_CRC8_TABLE(scd30_i2c_crc8_tbl);

static int scd30_i2c_xfer(struct scd30_state *state, char *txbuf, int txsize,
			  char *rxbuf, int rxsize)
{
	struct i2c_client *client = to_i2c_client(state->dev);
	int ret;

	/*
	 * repeated start is not supported hence instead of sending two i2c
	 * messages in a row we send one by one
	 */
	ret = i2c_master_send(client, txbuf, txsize);
	if (ret < 0)
		return ret;
	if (ret != txsize)
		return -EIO;

	if (!rxbuf)
		return 0;

	ret = i2c_master_recv(client, rxbuf, rxsize);
	if (ret < 0)
		return ret;
	if (ret != rxsize)
		return -EIO;

	return 0;
}

static int scd30_i2c_command(struct scd30_state *state, enum scd30_cmd cmd, u16 arg,
			     void *response, int size)
{
	char buf[SCD30_I2C_MAX_BUF_SIZE];
	char *rsp = response;
	int i, ret;
	char crc;

	put_unaligned_be16(scd30_i2c_cmd_lookup_tbl[cmd], buf);
	i = 2;

	if (rsp) {
		/* each two bytes are followed by a crc8 */
		size += size / 2;
	} else {
		put_unaligned_be16(arg, buf + i);
		crc = crc8(scd30_i2c_crc8_tbl, buf + i, 2, CRC8_INIT_VALUE);
		i += 2;
		buf[i] = crc;
		i += 1;

		/* commands below don't take an argument */
		if ((cmd == CMD_STOP_MEAS) || (cmd == CMD_RESET))
			i -= 3;
	}

	ret = scd30_i2c_xfer(state, buf, i, buf, size);
	if (ret)
		return ret;

	/* validate received data and strip off crc bytes */
	for (i = 0; i < size; i += 3) {
		crc = crc8(scd30_i2c_crc8_tbl, buf + i, 2, CRC8_INIT_VALUE);
		if (crc != buf[i + 2]) {
			dev_err(state->dev, "data integrity check failed\n");
			return -EIO;
		}

		*rsp++ = buf[i];
		*rsp++ = buf[i + 1];
	}

	return 0;
}

static int scd30_i2c_probe(struct i2c_client *client)
{
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	crc8_populate_msb(scd30_i2c_crc8_tbl, SCD30_I2C_CRC8_POLYNOMIAL);

	return scd30_probe(&client->dev, client->irq, client->name, NULL, scd30_i2c_command);
}

static const struct of_device_id scd30_i2c_of_match[] = {
	{ .compatible = "sensirion,scd30" },
	{ }
};
MODULE_DEVICE_TABLE(of, scd30_i2c_of_match);

static struct i2c_driver scd30_i2c_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = scd30_i2c_of_match,
		.pm = pm_sleep_ptr(&scd30_pm_ops),
	},
	.probe = scd30_i2c_probe,
};
module_i2c_driver(scd30_i2c_driver);

MODULE_AUTHOR("Tomasz Duszynski <tomasz.duszynski@octakon.com>");
MODULE_DESCRIPTION("Sensirion SCD30 carbon dioxide sensor i2c driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_SCD30);
