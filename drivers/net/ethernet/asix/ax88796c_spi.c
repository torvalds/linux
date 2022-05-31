// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010 ASIX Electronics Corporation
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 *
 * ASIX AX88796C SPI Fast Ethernet Linux driver
 */

#define pr_fmt(fmt)	"ax88796c: " fmt

#include <linux/string.h>
#include <linux/spi/spi.h>

#include "ax88796c_spi.h"

const u8 ax88796c_rx_cmd_buf[5] = {AX_SPICMD_READ_RXQ, 0xFF, 0xFF, 0xFF, 0xFF};
const u8 ax88796c_tx_cmd_buf[4] = {AX_SPICMD_WRITE_TXQ, 0xFF, 0xFF, 0xFF};

/* driver bus management functions */
int axspi_wakeup(struct axspi_data *ax_spi)
{
	int ret;

	ax_spi->cmd_buf[0] = AX_SPICMD_EXIT_PWD;	/* OP */
	ret = spi_write(ax_spi->spi, ax_spi->cmd_buf, 1);
	if (ret)
		dev_err(&ax_spi->spi->dev, "%s() failed: ret = %d\n", __func__, ret);
	return ret;
}

int axspi_read_status(struct axspi_data *ax_spi, struct spi_status *status)
{
	int ret;

	/* OP */
	ax_spi->cmd_buf[0] = AX_SPICMD_READ_STATUS;
	ret = spi_write_then_read(ax_spi->spi, ax_spi->cmd_buf, 1, (u8 *)status, 3);
	if (ret)
		dev_err(&ax_spi->spi->dev, "%s() failed: ret = %d\n", __func__, ret);
	else
		le16_to_cpus(&status->isr);

	return ret;
}

int axspi_read_rxq(struct axspi_data *ax_spi, void *data, int len)
{
	struct spi_transfer *xfer = ax_spi->spi_rx_xfer;
	int ret;

	memcpy(ax_spi->cmd_buf, ax88796c_rx_cmd_buf, 5);

	xfer->tx_buf = ax_spi->cmd_buf;
	xfer->rx_buf = NULL;
	xfer->len = ax_spi->comp ? 2 : 5;
	xfer->bits_per_word = 8;
	spi_message_add_tail(xfer, &ax_spi->rx_msg);

	xfer++;
	xfer->rx_buf = data;
	xfer->tx_buf = NULL;
	xfer->len = len;
	xfer->bits_per_word = 8;
	spi_message_add_tail(xfer, &ax_spi->rx_msg);
	ret = spi_sync(ax_spi->spi, &ax_spi->rx_msg);
	if (ret)
		dev_err(&ax_spi->spi->dev, "%s() failed: ret = %d\n", __func__, ret);

	return ret;
}

int axspi_write_txq(const struct axspi_data *ax_spi, void *data, int len)
{
	return spi_write(ax_spi->spi, data, len);
}

u16 axspi_read_reg(struct axspi_data *ax_spi, u8 reg)
{
	int ret;
	int len = ax_spi->comp ? 3 : 4;

	ax_spi->cmd_buf[0] = 0x03;	/* OP code read register */
	ax_spi->cmd_buf[1] = reg;	/* register address */
	ax_spi->cmd_buf[2] = 0xFF;	/* dumy cycle */
	ax_spi->cmd_buf[3] = 0xFF;	/* dumy cycle */
	ret = spi_write_then_read(ax_spi->spi,
				  ax_spi->cmd_buf, len,
				  ax_spi->rx_buf, 2);
	if (ret) {
		dev_err(&ax_spi->spi->dev,
			"%s() failed: ret = %d\n", __func__, ret);
		return 0xFFFF;
	}

	le16_to_cpus((u16 *)ax_spi->rx_buf);

	return *(u16 *)ax_spi->rx_buf;
}

int axspi_write_reg(struct axspi_data *ax_spi, u8 reg, u16 value)
{
	int ret;

	memset(ax_spi->cmd_buf, 0, sizeof(ax_spi->cmd_buf));
	ax_spi->cmd_buf[0] = AX_SPICMD_WRITE_REG;	/* OP code read register */
	ax_spi->cmd_buf[1] = reg;			/* register address */
	ax_spi->cmd_buf[2] = value;
	ax_spi->cmd_buf[3] = value >> 8;

	ret = spi_write(ax_spi->spi, ax_spi->cmd_buf, 4);
	if (ret)
		dev_err(&ax_spi->spi->dev, "%s() failed: ret = %d\n", __func__, ret);
	return ret;
}

