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

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/spi/spi.h>

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
	struct spi_transfer transfer[2];
	struct spi_message msg;
	int ret;

	memset(transfer, 0, sizeof(transfer));

	spi_message_init(&msg);

	tx_data = cpu_to_be16(QCA7K_SPI_READ | QCA7K_SPI_INTERNAL | reg);
	*result = 0;

	transfer[0].tx_buf = &tx_data;
	transfer[0].len = QCASPI_CMD_LEN;
	transfer[1].rx_buf = &rx_data;
	transfer[1].len = QCASPI_CMD_LEN;

	spi_message_add_tail(&transfer[0], &msg);

	if (qca->legacy_mode) {
		spi_sync(qca->spi_dev, &msg);
		spi_message_init(&msg);
	}
	spi_message_add_tail(&transfer[1], &msg);
	ret = spi_sync(qca->spi_dev, &msg);

	if (!ret)
		ret = msg.status;

	if (ret)
		qcaspi_spi_error(qca);
	else
		*result = be16_to_cpu(rx_data);

	return ret;
}

static int
__qcaspi_write_register(struct qcaspi *qca, u16 reg, u16 value)
{
	__be16 tx_data[2];
	struct spi_transfer transfer[2];
	struct spi_message msg;
	int ret;

	memset(&transfer, 0, sizeof(transfer));

	spi_message_init(&msg);

	tx_data[0] = cpu_to_be16(QCA7K_SPI_WRITE | QCA7K_SPI_INTERNAL | reg);
	tx_data[1] = cpu_to_be16(value);

	transfer[0].tx_buf = &tx_data[0];
	transfer[0].len = QCASPI_CMD_LEN;
	transfer[1].tx_buf = &tx_data[1];
	transfer[1].len = QCASPI_CMD_LEN;

	spi_message_add_tail(&transfer[0], &msg);
	if (qca->legacy_mode) {
		spi_sync(qca->spi_dev, &msg);
		spi_message_init(&msg);
	}
	spi_message_add_tail(&transfer[1], &msg);
	ret = spi_sync(qca->spi_dev, &msg);

	if (!ret)
		ret = msg.status;

	if (ret)
		qcaspi_spi_error(qca);

	return ret;
}

int
qcaspi_write_register(struct qcaspi *qca, u16 reg, u16 value, int retry)
{
	int ret, i = 0;
	u16 confirmed;

	do {
		ret = __qcaspi_write_register(qca, reg, value);
		if (ret)
			return ret;

		if (!retry)
			return 0;

		ret = qcaspi_read_register(qca, reg, &confirmed);
		if (ret)
			return ret;

		ret = confirmed != value;
		if (!ret)
			return 0;

		i++;
		qca->stats.write_verify_failed++;

	} while (i <= retry);

	return ret;
}
