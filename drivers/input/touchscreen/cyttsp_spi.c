// SPDX-License-Identifier: GPL-2.0-only
/*
 * Source for:
 * Cypress TrueTouch(TM) Standard Product (TTSP) SPI touchscreen driver.
 * For use with Cypress Txx3xx parts.
 * Supported parts include:
 * CY8CTST341
 * CY8CTMA340
 *
 * Copyright (C) 2009, 2010, 2011 Cypress Semiconductor, Inc.
 * Copyright (C) 2012 Javier Martinez Canillas <javier@dowhile0.org>
 * Copyright (C) 2013 Cypress Semiconductor
 *
 * Contact Cypress Semiconductor at www.cypress.com <ttdrivers@cypress.com>
 */

#include "cyttsp_core.h"

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/spi/spi.h>

#define CY_SPI_NAME		"cyttsp-spi"

#define CY_SPI_WR_OP		0x00 /* r/~w */
#define CY_SPI_RD_OP		0x01
#define CY_SPI_CMD_BYTES	4
#define CY_SPI_SYNC_BYTE	2
#define CY_SPI_SYNC_ACK1	0x62 /* from protocol v.2 */
#define CY_SPI_SYNC_ACK2	0x9D /* from protocol v.2 */
#define CY_SPI_DATA_SIZE	128
#define CY_SPI_DATA_BUF_SIZE	(CY_SPI_CMD_BYTES + CY_SPI_DATA_SIZE)
#define CY_SPI_BITS_PER_WORD	8

static int cyttsp_spi_xfer(struct device *dev, u8 *xfer_buf,
			   u8 op, u16 reg, u8 *buf, int length)
{
	struct spi_device *spi = to_spi_device(dev);
	struct spi_message msg;
	struct spi_transfer xfer[2];
	u8 *wr_buf = &xfer_buf[0];
	u8 *rd_buf = &xfer_buf[CY_SPI_DATA_BUF_SIZE];
	int retval;
	int i;

	if (length > CY_SPI_DATA_SIZE) {
		dev_err(dev, "%s: length %d is too big.\n",
			__func__, length);
		return -EINVAL;
	}

	memset(wr_buf, 0, CY_SPI_DATA_BUF_SIZE);
	memset(rd_buf, 0, CY_SPI_DATA_BUF_SIZE);

	wr_buf[0] = 0x00; /* header byte 0 */
	wr_buf[1] = 0xFF; /* header byte 1 */
	wr_buf[2] = reg;  /* reg index */
	wr_buf[3] = op;   /* r/~w */
	if (op == CY_SPI_WR_OP)
		memcpy(wr_buf + CY_SPI_CMD_BYTES, buf, length);

	memset(xfer, 0, sizeof(xfer));
	spi_message_init(&msg);

	/*
	  We set both TX and RX buffers because Cypress TTSP
	  requires full duplex operation.
	*/
	xfer[0].tx_buf = wr_buf;
	xfer[0].rx_buf = rd_buf;
	switch (op) {
	case CY_SPI_WR_OP:
		xfer[0].len = length + CY_SPI_CMD_BYTES;
		spi_message_add_tail(&xfer[0], &msg);
		break;

	case CY_SPI_RD_OP:
		xfer[0].len = CY_SPI_CMD_BYTES;
		spi_message_add_tail(&xfer[0], &msg);

		xfer[1].rx_buf = buf;
		xfer[1].len = length;
		spi_message_add_tail(&xfer[1], &msg);
		break;

	default:
		dev_err(dev, "%s: bad operation code=%d\n", __func__, op);
		return -EINVAL;
	}

	retval = spi_sync(spi, &msg);
	if (retval < 0) {
		dev_dbg(dev, "%s: spi_sync() error %d, len=%d, op=%d\n",
			__func__, retval, xfer[1].len, op);

		/*
		 * do not return here since was a bad ACK sequence
		 * let the following ACK check handle any errors and
		 * allow silent retries
		 */
	}

	if (rd_buf[CY_SPI_SYNC_BYTE] != CY_SPI_SYNC_ACK1 ||
	    rd_buf[CY_SPI_SYNC_BYTE + 1] != CY_SPI_SYNC_ACK2) {
		dev_dbg(dev, "%s: operation %d failed\n", __func__, op);

		for (i = 0; i < CY_SPI_CMD_BYTES; i++)
			dev_dbg(dev, "%s: test rd_buf[%d]:0x%02x\n",
				__func__, i, rd_buf[i]);
		for (i = 0; i < length; i++)
			dev_dbg(dev, "%s: test buf[%d]:0x%02x\n",
				__func__, i, buf[i]);

		return -EIO;
	}

	return 0;
}

static int cyttsp_spi_read_block_data(struct device *dev, u8 *xfer_buf,
				      u16 addr, u8 length, void *data)
{
	return cyttsp_spi_xfer(dev, xfer_buf, CY_SPI_RD_OP, addr, data,
			length);
}

static int cyttsp_spi_write_block_data(struct device *dev, u8 *xfer_buf,
				       u16 addr, u8 length, const void *data)
{
	return cyttsp_spi_xfer(dev, xfer_buf, CY_SPI_WR_OP, addr, (void *)data,
			length);
}

static const struct cyttsp_bus_ops cyttsp_spi_bus_ops = {
	.bustype	= BUS_SPI,
	.write		= cyttsp_spi_write_block_data,
	.read		= cyttsp_spi_read_block_data,
};

static int cyttsp_spi_probe(struct spi_device *spi)
{
	struct cyttsp *ts;
	int error;

	/* Set up SPI*/
	spi->bits_per_word = CY_SPI_BITS_PER_WORD;
	spi->mode = SPI_MODE_0;
	error = spi_setup(spi);
	if (error < 0) {
		dev_err(&spi->dev, "%s: SPI setup error %d\n",
			__func__, error);
		return error;
	}

	ts = cyttsp_probe(&cyttsp_spi_bus_ops, &spi->dev, spi->irq,
			  CY_SPI_DATA_BUF_SIZE * 2);
	if (IS_ERR(ts))
		return PTR_ERR(ts);

	spi_set_drvdata(spi, ts);

	return 0;
}

static const struct of_device_id cyttsp_of_spi_match[] = {
	{ .compatible = "cypress,cy8ctma340", },
	{ .compatible = "cypress,cy8ctst341", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cyttsp_of_spi_match);

static struct spi_driver cyttsp_spi_driver = {
	.driver = {
		.name	= CY_SPI_NAME,
		.pm	= &cyttsp_pm_ops,
		.of_match_table = cyttsp_of_spi_match,
	},
	.probe  = cyttsp_spi_probe,
};

module_spi_driver(cyttsp_spi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch(R) Standard Product (TTSP) SPI driver");
MODULE_AUTHOR("Cypress");
MODULE_ALIAS("spi:cyttsp");
