/*
 * cyttsp5_spi.c
 * Parade TrueTouch(TM) Standard Product V5 SPI Module.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * CYTMA5XX
 * CYTMA448
 * CYTMA445A
 * CYTT21XXX
 * CYTT31XXX
 *
 * Copyright (C) 2015 Parade Technologies
 * Copyright (C) 2012-2015 Cypress Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Parade Technologies at www.paradetech.com <ttdrivers@paradetech.com>
 *
 */

#include "cyttsp5_regs.h"

#include <linux/spi/spi.h>
#include <linux/version.h>

#define CY_SPI_WR_OP		0x00 /* r/~w */
#define CY_SPI_RD_OP		0x01
#define CY_SPI_BITS_PER_WORD	8
#define CY_SPI_SYNC_ACK         0x62

#define CY_SPI_CMD_BYTES	0
#define CY_SPI_DATA_SIZE	(2 * 256)
#define CY_SPI_DATA_BUF_SIZE	(CY_SPI_CMD_BYTES + CY_SPI_DATA_SIZE)

static void cyttsp5_spi_add_rw_msg(struct spi_message *msg,
		struct spi_transfer *xfer, u8 *w_header, u8 *r_header, u8 op)
{
	xfer->tx_buf = w_header;
	xfer->rx_buf = r_header;
	w_header[0] = op;
	xfer->len = 1;
	spi_message_add_tail(xfer, msg);
}

static int cyttsp5_spi_xfer(struct device *dev, u8 op, u8 *buf, int length)
{
	struct spi_device *spi = to_spi_device(dev);
	struct spi_message msg;
	struct spi_transfer xfer[2];
	u8 w_header[2];
	u8 r_header[2];
	int rc;

	memset(xfer, 0, sizeof(xfer));

	spi_message_init(&msg);
	cyttsp5_spi_add_rw_msg(&msg, &xfer[0], w_header, r_header, op);

	switch (op) {
	case CY_SPI_RD_OP:
		xfer[1].rx_buf = buf;
		xfer[1].len = length;
		spi_message_add_tail(&xfer[1], &msg);
		break;
	case CY_SPI_WR_OP:
		xfer[1].tx_buf = buf;
		xfer[1].len = length;
		spi_message_add_tail(&xfer[1], &msg);
		break;
	default:
		rc = -EIO;
		goto exit;
	}

	rc = spi_sync(spi, &msg);
exit:
	if (rc < 0)
		parade_debug(dev, DEBUG_LEVEL_2, "%s: spi_sync() error %d\n",
			__func__, rc);

	if (r_header[0] != CY_SPI_SYNC_ACK)
		return -EIO;

	return rc;
}

static int cyttsp5_spi_read_default(struct device *dev, void *buf, int size)
{
	if (!buf || !size)
		return 0;

	return cyttsp5_spi_xfer(dev, CY_SPI_RD_OP, buf, size);
}

static int cyttsp5_spi_read_default_nosize(struct device *dev, u8 *buf, u32 max)
{
	u32 size;
	int rc;

	if (!buf)
		return 0;

	rc = cyttsp5_spi_xfer(dev, CY_SPI_RD_OP, buf, 2);
	if (rc < 0)
		return rc;

	size = get_unaligned_le16(&buf[0]);
	if (!size)
		return rc;

	if (size > max)
		return -EINVAL;

	return cyttsp5_spi_read_default(dev, buf, size);
}

static int cyttsp5_spi_write_read_specific(struct device *dev, u8 write_len,
		u8 *write_buf, u8 *read_buf)
{
	int rc;

	rc = cyttsp5_spi_xfer(dev, CY_SPI_WR_OP, write_buf, write_len);
	if (rc < 0)
		return rc;

	if (read_buf)
		rc = cyttsp5_spi_read_default_nosize(dev, read_buf,
				CY_SPI_DATA_SIZE);

	return rc;
}

static struct cyttsp5_bus_ops cyttsp5_spi_bus_ops = {
	.bustype = BUS_SPI,
	.read_default = cyttsp5_spi_read_default,
	.read_default_nosize = cyttsp5_spi_read_default_nosize,
	.write_read_specific = cyttsp5_spi_write_read_specific,
};

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
static const struct of_device_id cyttsp5_spi_of_match[] = {
	{ .compatible = "cy,cyttsp5_spi_adapter", },
	{ }
};
MODULE_DEVICE_TABLE(of, cyttsp5_spi_of_match);
#endif

static int cyttsp5_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	const struct of_device_id *match;
#endif
	int rc;

	/* Set up SPI*/
	spi->bits_per_word = CY_SPI_BITS_PER_WORD;
	spi->mode = SPI_MODE_0;
	rc = spi_setup(spi);
	if (rc < 0) {
		dev_err(dev, "%s: SPI setup error %d\n", __func__, rc);
		return rc;
	}

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	match = of_match_device(of_match_ptr(cyttsp5_spi_of_match), dev);
	if (match) {
		rc = cyttsp5_devtree_create_and_get_pdata(dev);
		if (rc < 0)
			return rc;
	}
#endif

	rc = cyttsp5_probe(&cyttsp5_spi_bus_ops, &spi->dev, spi->irq,
			  CY_SPI_DATA_BUF_SIZE);

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	if (rc && match)
		cyttsp5_devtree_clean_pdata(dev);
#endif

	return rc;
}

static int cyttsp5_spi_remove(struct spi_device *spi)
{
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	struct device *dev = &spi->dev;
	const struct of_device_id *match;
#endif
	struct cyttsp5_core_data *cd = dev_get_drvdata(&spi->dev);

	cyttsp5_release(cd);

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	match = of_match_device(of_match_ptr(cyttsp5_spi_of_match), dev);
	if (match)
		cyttsp5_devtree_clean_pdata(dev);
#endif

	return 0;
}

static const struct spi_device_id cyttsp5_spi_id[] = {
	{ CYTTSP5_SPI_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, cyttsp5_spi_id);

static struct spi_driver cyttsp5_spi_driver = {
	.driver = {
		.name = CYTTSP5_SPI_NAME,
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
		.pm = &cyttsp5_pm_ops,
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
		.of_match_table = cyttsp5_spi_of_match,
#endif
	},
	.probe = cyttsp5_spi_probe,
	.remove = (cyttsp5_spi_remove),
	.id_table = cyttsp5_spi_id,
};

#if (KERNEL_VERSION(3, 3, 0) <= LINUX_VERSION_CODE)
module_spi_driver(cyttsp5_spi_driver);
#else
static int __init cyttsp5_spi_init(void)
{
	int err = spi_register_driver(&cyttsp5_spi_driver);

	pr_info("%s: Parade TTSP SPI Driver (Built %s) rc=%d\n",
		 __func__, CY_DRIVER_VERSION, err);
	return err;
}
module_init(cyttsp5_spi_init);

static void __exit cyttsp5_spi_exit(void)
{
	spi_unregister_driver(&cyttsp5_spi_driver);
}
module_exit(cyttsp5_spi_exit);
#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Parade TrueTouch(R) Standard Product SPI Driver");
MODULE_AUTHOR("Parade Technologies <ttdrivers@paradetech.com>");
