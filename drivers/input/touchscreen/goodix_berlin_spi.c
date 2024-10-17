// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Goodix Berlin Touchscreen Driver
 *
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Based on goodix_ts_berlin driver.
 */
#include <linux/unaligned.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/input.h>

#include "goodix_berlin.h"

#define GOODIX_BERLIN_SPI_TRANS_PREFIX_LEN	1
#define GOODIX_BERLIN_REGISTER_WIDTH		4
#define GOODIX_BERLIN_SPI_READ_DUMMY_LEN	3
#define GOODIX_BERLIN_SPI_READ_PREFIX_LEN	(GOODIX_BERLIN_SPI_TRANS_PREFIX_LEN + \
						 GOODIX_BERLIN_REGISTER_WIDTH + \
						 GOODIX_BERLIN_SPI_READ_DUMMY_LEN)
#define GOODIX_BERLIN_SPI_WRITE_PREFIX_LEN	(GOODIX_BERLIN_SPI_TRANS_PREFIX_LEN + \
						 GOODIX_BERLIN_REGISTER_WIDTH)

#define GOODIX_BERLIN_SPI_WRITE_FLAG		0xF0
#define GOODIX_BERLIN_SPI_READ_FLAG		0xF1

static int goodix_berlin_spi_read(void *context, const void *reg_buf,
				  size_t reg_size, void *val_buf,
				  size_t val_size)
{
	struct spi_device *spi = context;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	const u32 *reg = reg_buf; /* reg is stored as native u32 at start of buffer */
	int error;

	if (reg_size != GOODIX_BERLIN_REGISTER_WIDTH)
		return -EINVAL;

	u8 *buf __free(kfree) =
		kzalloc(GOODIX_BERLIN_SPI_READ_PREFIX_LEN + val_size,
			GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	/* buffer format: 0xF1 + addr(4bytes) + dummy(3bytes) + data */
	buf[0] = GOODIX_BERLIN_SPI_READ_FLAG;
	put_unaligned_be32(*reg, buf + GOODIX_BERLIN_SPI_TRANS_PREFIX_LEN);
	memset(buf + GOODIX_BERLIN_SPI_TRANS_PREFIX_LEN + GOODIX_BERLIN_REGISTER_WIDTH,
	       0xff, GOODIX_BERLIN_SPI_READ_DUMMY_LEN);

	xfers.tx_buf = buf;
	xfers.rx_buf = buf;
	xfers.len = GOODIX_BERLIN_SPI_READ_PREFIX_LEN + val_size;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);

	error = spi_sync(spi, &spi_msg);
	if (error < 0) {
		dev_err(&spi->dev, "spi transfer error, %d", error);
		return error;
	}

	memcpy(val_buf, buf + GOODIX_BERLIN_SPI_READ_PREFIX_LEN, val_size);
	return error;
}

static int goodix_berlin_spi_write(void *context, const void *data,
				   size_t count)
{
	unsigned int len = count - GOODIX_BERLIN_REGISTER_WIDTH;
	struct spi_device *spi = context;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	const u32 *reg = data; /* reg is stored as native u32 at start of buffer */
	int error;

	u8 *buf __free(kfree) =
		kzalloc(GOODIX_BERLIN_SPI_WRITE_PREFIX_LEN + len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	buf[0] = GOODIX_BERLIN_SPI_WRITE_FLAG;
	put_unaligned_be32(*reg, buf + GOODIX_BERLIN_SPI_TRANS_PREFIX_LEN);
	memcpy(buf + GOODIX_BERLIN_SPI_WRITE_PREFIX_LEN,
	       data + GOODIX_BERLIN_REGISTER_WIDTH, len);

	xfers.tx_buf = buf;
	xfers.len = GOODIX_BERLIN_SPI_WRITE_PREFIX_LEN + len;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);

	error = spi_sync(spi, &spi_msg);
	if (error < 0) {
		dev_err(&spi->dev, "spi transfer error, %d", error);
		return error;
	}

	return 0;
}

static const struct regmap_config goodix_berlin_spi_regmap_conf = {
	.reg_bits = 32,
	.val_bits = 8,
	.read = goodix_berlin_spi_read,
	.write = goodix_berlin_spi_write,
};

/* vendor & product left unassigned here, should probably be updated from fw info */
static const struct input_id goodix_berlin_spi_input_id = {
	.bustype = BUS_SPI,
};

static int goodix_berlin_spi_probe(struct spi_device *spi)
{
	struct regmap_config regmap_config;
	struct regmap *regmap;
	size_t max_size;
	int error = 0;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	error = spi_setup(spi);
	if (error)
		return error;

	max_size = spi_max_transfer_size(spi);

	regmap_config = goodix_berlin_spi_regmap_conf;
	regmap_config.max_raw_read = max_size - GOODIX_BERLIN_SPI_READ_PREFIX_LEN;
	regmap_config.max_raw_write = max_size - GOODIX_BERLIN_SPI_WRITE_PREFIX_LEN;

	regmap = devm_regmap_init(&spi->dev, NULL, spi, &regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	error = goodix_berlin_probe(&spi->dev, spi->irq,
				    &goodix_berlin_spi_input_id, regmap);
	if (error)
		return error;

	return 0;
}

static const struct spi_device_id goodix_berlin_spi_ids[] = {
	{ "gt9916" },
	{ },
};
MODULE_DEVICE_TABLE(spi, goodix_berlin_spi_ids);

static const struct of_device_id goodix_berlin_spi_of_match[] = {
	{ .compatible = "goodix,gt9916", },
	{ }
};
MODULE_DEVICE_TABLE(of, goodix_berlin_spi_of_match);

static struct spi_driver goodix_berlin_spi_driver = {
	.driver = {
		.name = "goodix-berlin-spi",
		.of_match_table = goodix_berlin_spi_of_match,
		.pm = pm_sleep_ptr(&goodix_berlin_pm_ops),
		.dev_groups = goodix_berlin_groups,
	},
	.probe = goodix_berlin_spi_probe,
	.id_table = goodix_berlin_spi_ids,
};
module_spi_driver(goodix_berlin_spi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Goodix Berlin SPI Touchscreen driver");
MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
