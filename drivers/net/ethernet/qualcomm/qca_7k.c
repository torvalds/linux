/*
 *
 *   Copyright (c) 2011, 2012, Qualcomm Atheros Communications Inc.
 *   Copyright (c) 2014, I2SE GmbH
 *
 *   Permission to use, copy, modify, and/or distribute this software
 *   for any purpose with or without fee is hereby granted, provided
 *   that the above copyright notice and this permission notice appear
 *   in all copies.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 *   THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 *   CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 *   LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 *   NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 *   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/*   This module implements the Qualcomm Atheros SPI protocol for
 *   kernel-based SPI device.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spi/spi.h>
#include <linux/version.h>

#include "qca_7k.h"

void
qcaspi_spi_error(struct qcaspi *qca)
{
	if (qca->sync != QCASPI_SYNC_READY)
		return;

	netdev_err(qca->net_dev, "spi error\n");
	qca->sync = QCASPI_SYNC_UNKNOWN;
	qca->stats.spi_err++;
}

int
qcaspi_read_register(struct qcaspi *qca, u16 reg, u16 *result)
{
	__be16 rx_data;
	__be16 tx_data;
	struct spi_transfer *transfer;
	struct spi_message *msg;
	int ret;

	tx_data = cpu_to_be16(QCA7K_SPI_READ | QCA7K_SPI_INTERNAL | reg);

	if (qca->legacy_mode) {
		msg = &qca->spi_msg1;
		transfer = &qca->spi_xfer1;
		transfer->tx_buf = &tx_data;
		transfer->rx_buf = NULL;
		transfer->len = QCASPI_CMD_LEN;
		spi_sync(qca->spi_dev, msg);
	} else {
		msg = &qca->spi_msg2;
		transfer = &qca->spi_xfer2[0];
		transfer->tx_buf = &tx_data;
		transfer->rx_buf = NULL;
		transfer->len = QCASPI_CMD_LEN;
		transfer = &qca->spi_xfer2[1];
	}
	transfer->tx_buf = NULL;
	transfer->rx_buf = &rx_data;
	transfer->len = QCASPI_CMD_LEN;
	ret = spi_sync(qca->spi_dev, msg);

	if (!ret)
		ret = msg->status;

	if (ret)
		qcaspi_spi_error(qca);
	else
		*result = be16_to_cpu(rx_data);

	return ret;
}

int
qcaspi_write_register(struct qcaspi *qca, u16 reg, u16 value)
{
	__be16 tx_data[2];
	struct spi_transfer *transfer;
	struct spi_message *msg;
	int ret;

	tx_data[0] = cpu_to_be16(QCA7K_SPI_WRITE | QCA7K_SPI_INTERNAL | reg);
	tx_data[1] = cpu_to_be16(value);

	if (qca->legacy_mode) {
		msg = &qca->spi_msg1;
		transfer = &qca->spi_xfer1;
		transfer->tx_buf = &tx_data[0];
		transfer->rx_buf = NULL;
		transfer->len = QCASPI_CMD_LEN;
		spi_sync(qca->spi_dev, msg);
	} else {
		msg = &qca->spi_msg2;
		transfer = &qca->spi_xfer2[0];
		transfer->tx_buf = &tx_data[0];
		transfer->rx_buf = NULL;
		transfer->len = QCASPI_CMD_LEN;
		transfer = &qca->spi_xfer2[1];
	}
	transfer->tx_buf = &tx_data[1];
	transfer->rx_buf = NULL;
	transfer->len = QCASPI_CMD_LEN;
	ret = spi_sync(qca->spi_dev, msg);

	if (!ret)
		ret = msg->status;

	if (ret)
		qcaspi_spi_error(qca);

	return ret;
}

int
qcaspi_tx_cmd(struct qcaspi *qca, u16 cmd)
{
	__be16 tx_data;
	struct spi_message *msg = &qca->spi_msg1;
	struct spi_transfer *transfer = &qca->spi_xfer1;
	int ret;

	tx_data = cpu_to_be16(cmd);
	transfer->len = sizeof(tx_data);
	transfer->tx_buf = &tx_data;
	transfer->rx_buf = NULL;

	ret = spi_sync(qca->spi_dev, msg);

	if (!ret)
		ret = msg->status;

	if (ret)
		qcaspi_spi_error(qca);

	return ret;
}
