// SPDX-License-Identifier: GPL-2.0
/*
 * Sensirion SCD30 carbon dioxide sensor serial driver
 *
 * Copyright (c) 2020 Tomasz Duszynski <tomasz.duszynski@octakon.com>
 */
#include <linux/crc16.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/iio/iio.h>
#include <linux/jiffies.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/serdev.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/unaligned.h>

#include "scd30.h"

#define SCD30_SERDEV_ADDR 0x61
#define SCD30_SERDEV_WRITE 0x06
#define SCD30_SERDEV_READ 0x03
#define SCD30_SERDEV_MAX_BUF_SIZE 17
#define SCD30_SERDEV_RX_HEADER_SIZE 3
#define SCD30_SERDEV_CRC_SIZE 2
#define SCD30_SERDEV_TIMEOUT msecs_to_jiffies(200)

struct scd30_serdev_priv {
	struct completion meas_ready;
	char *buf;
	int num_expected;
	int num;
};

static u16 scd30_serdev_cmd_lookup_tbl[] = {
	[CMD_START_MEAS] = 0x0036,
	[CMD_STOP_MEAS] = 0x0037,
	[CMD_MEAS_INTERVAL] = 0x0025,
	[CMD_MEAS_READY] = 0x0027,
	[CMD_READ_MEAS] = 0x0028,
	[CMD_ASC] = 0x003a,
	[CMD_FRC] = 0x0039,
	[CMD_TEMP_OFFSET] = 0x003b,
	[CMD_FW_VERSION] = 0x0020,
	[CMD_RESET] = 0x0034,
};

static u16 scd30_serdev_calc_crc(const char *buf, int size)
{
	return crc16(0xffff, buf, size);
}

static int scd30_serdev_xfer(struct scd30_state *state, char *txbuf, int txsize,
			     char *rxbuf, int rxsize)
{
	struct serdev_device *serdev = to_serdev_device(state->dev);
	struct scd30_serdev_priv *priv = state->priv;
	int ret;

	priv->buf = rxbuf;
	priv->num_expected = rxsize;
	priv->num = 0;

	ret = serdev_device_write(serdev, txbuf, txsize, SCD30_SERDEV_TIMEOUT);
	if (ret < 0)
		return ret;
	if (ret != txsize)
		return -EIO;

	ret = wait_for_completion_interruptible_timeout(&priv->meas_ready, SCD30_SERDEV_TIMEOUT);
	if (ret < 0)
		return ret;
	if (!ret)
		return -ETIMEDOUT;

	return 0;
}

static int scd30_serdev_command(struct scd30_state *state, enum scd30_cmd cmd, u16 arg,
				void *response, int size)
{
	/*
	 * Communication over serial line is based on modbus protocol (or rather
	 * its variation called modbus over serial to be precise). Upon
	 * receiving a request device should reply with response.
	 *
	 * Frame below represents a request message. Each field takes
	 * exactly one byte.
	 *
	 * +------+------+-----+-----+-------+-------+-----+-----+
	 * | dev  | op   | reg | reg | byte1 | byte0 | crc | crc |
	 * | addr | code | msb | lsb |       |       | lsb | msb |
	 * +------+------+-----+-----+-------+-------+-----+-----+
	 *
	 * The message device replies with depends on the 'op code' field from
	 * the request. In case it was set to SCD30_SERDEV_WRITE sensor should
	 * reply with unchanged request. Otherwise 'op code' was set to
	 * SCD30_SERDEV_READ and response looks like the one below. As with
	 * request, each field takes one byte.
	 *
	 * +------+------+--------+-------+-----+-------+-----+-----+
	 * | dev  | op   | num of | byte0 | ... | byteN | crc | crc |
	 * | addr | code | bytes  |       |     |       | lsb | msb |
	 * +------+------+--------+-------+-----+-------+-----+-----+
	 */
	char txbuf[SCD30_SERDEV_MAX_BUF_SIZE] = { SCD30_SERDEV_ADDR },
	     rxbuf[SCD30_SERDEV_MAX_BUF_SIZE];
	int ret, rxsize, txsize = 2;
	char *rsp = response;
	u16 crc;

	put_unaligned_be16(scd30_serdev_cmd_lookup_tbl[cmd], txbuf + txsize);
	txsize += 2;

	if (rsp) {
		txbuf[1] = SCD30_SERDEV_READ;
		if (cmd == CMD_READ_MEAS)
			/* number of u16 words to read */
			put_unaligned_be16(size / 2, txbuf + txsize);
		else
			put_unaligned_be16(0x0001, txbuf + txsize);
		txsize += 2;
		crc = scd30_serdev_calc_crc(txbuf, txsize);
		put_unaligned_le16(crc, txbuf + txsize);
		txsize += 2;
		rxsize = SCD30_SERDEV_RX_HEADER_SIZE + size + SCD30_SERDEV_CRC_SIZE;
	} else {
		if ((cmd == CMD_STOP_MEAS) || (cmd == CMD_RESET))
			arg = 0x0001;

		txbuf[1] = SCD30_SERDEV_WRITE;
		put_unaligned_be16(arg, txbuf + txsize);
		txsize += 2;
		crc = scd30_serdev_calc_crc(txbuf, txsize);
		put_unaligned_le16(crc, txbuf + txsize);
		txsize += 2;
		rxsize = txsize;
	}

	ret = scd30_serdev_xfer(state, txbuf, txsize, rxbuf, rxsize);
	if (ret)
		return ret;

	switch (txbuf[1]) {
	case SCD30_SERDEV_WRITE:
		if (memcmp(txbuf, rxbuf, txsize)) {
			dev_err(state->dev, "wrong message received\n");
			return -EIO;
		}
		break;
	case SCD30_SERDEV_READ:
		if (rxbuf[2] != (rxsize - SCD30_SERDEV_RX_HEADER_SIZE - SCD30_SERDEV_CRC_SIZE)) {
			dev_err(state->dev, "received data size does not match header\n");
			return -EIO;
		}

		rxsize -= SCD30_SERDEV_CRC_SIZE;
		crc = get_unaligned_le16(rxbuf + rxsize);
		if (crc != scd30_serdev_calc_crc(rxbuf, rxsize)) {
			dev_err(state->dev, "data integrity check failed\n");
			return -EIO;
		}

		rxsize -= SCD30_SERDEV_RX_HEADER_SIZE;
		memcpy(rsp, rxbuf + SCD30_SERDEV_RX_HEADER_SIZE, rxsize);
		break;
	default:
		dev_err(state->dev, "received unknown op code\n");
		return -EIO;
	}

	return 0;
}

static ssize_t scd30_serdev_receive_buf(struct serdev_device *serdev,
					const u8 *buf, size_t size)
{
	struct iio_dev *indio_dev = serdev_device_get_drvdata(serdev);
	struct scd30_serdev_priv *priv;
	struct scd30_state *state;
	size_t num;

	if (!indio_dev)
		return 0;

	state = iio_priv(indio_dev);
	priv = state->priv;

	/* just in case sensor puts some unexpected bytes on the bus */
	if (!priv->buf)
		return 0;

	if (priv->num + size >= priv->num_expected)
		num = priv->num_expected - priv->num;
	else
		num = size;

	memcpy(priv->buf + priv->num, buf, num);
	priv->num += num;

	if (priv->num == priv->num_expected) {
		priv->buf = NULL;
		complete(&priv->meas_ready);
	}

	return num;
}

static const struct serdev_device_ops scd30_serdev_ops = {
	.receive_buf = scd30_serdev_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

static int scd30_serdev_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	struct scd30_serdev_priv *priv;
	int irq, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	init_completion(&priv->meas_ready);
	serdev_device_set_client_ops(serdev, &scd30_serdev_ops);

	ret = devm_serdev_device_open(dev, serdev);
	if (ret)
		return ret;

	serdev_device_set_baudrate(serdev, 19200);
	serdev_device_set_flow_control(serdev, false);

	ret = serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);
	if (ret)
		return ret;

	irq = fwnode_irq_get(dev_fwnode(dev), 0);

	return scd30_probe(dev, irq, KBUILD_MODNAME, priv, scd30_serdev_command);
}

static const struct of_device_id scd30_serdev_of_match[] = {
	{ .compatible = "sensirion,scd30" },
	{ }
};
MODULE_DEVICE_TABLE(of, scd30_serdev_of_match);

static struct serdev_device_driver scd30_serdev_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = scd30_serdev_of_match,
		.pm = pm_sleep_ptr(&scd30_pm_ops),
	},
	.probe = scd30_serdev_probe,
};
module_serdev_device_driver(scd30_serdev_driver);

MODULE_AUTHOR("Tomasz Duszynski <tomasz.duszynski@octakon.com>");
MODULE_DESCRIPTION("Sensirion SCD30 carbon dioxide sensor serial driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_SCD30);
