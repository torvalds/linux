/*
 * Samsung LSI S5C73M3 8M pixel camera driver
 *
 * Copyright (C) 2012, Samsung Electronics, Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/sizes.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "s5c73m3.h"

#define S5C73M3_SPI_DRV_NAME "S5C73M3-SPI"

static const struct of_device_id s5c73m3_spi_ids[] = {
	{ .compatible = "samsung,s5c73m3" },
	{ }
};

enum spi_direction {
	SPI_DIR_RX,
	SPI_DIR_TX
};

static int spi_xmit(struct spi_device *spi_dev, void *addr, const int len,
							enum spi_direction dir)
{
	struct spi_message msg;
	int r;
	struct spi_transfer xfer = {
		.len	= len,
	};

	if (dir == SPI_DIR_TX)
		xfer.tx_buf = addr;
	else
		xfer.rx_buf = addr;

	if (spi_dev == NULL) {
		pr_err("SPI device is uninitialized\n");
		return -ENODEV;
	}

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	r = spi_sync(spi_dev, &msg);
	if (r < 0)
		dev_err(&spi_dev->dev, "%s spi_sync failed %d\n", __func__, r);

	return r;
}

int s5c73m3_spi_write(struct s5c73m3 *state, const void *addr,
		      const unsigned int len, const unsigned int tx_size)
{
	struct spi_device *spi_dev = state->spi_dev;
	u32 count = len / tx_size;
	u32 extra = len % tx_size;
	unsigned int i, j = 0;
	u8 padding[32];
	int r = 0;

	memset(padding, 0, sizeof(padding));

	for (i = 0; i < count; i++) {
		r = spi_xmit(spi_dev, (void *)addr + j, tx_size, SPI_DIR_TX);
		if (r < 0)
			return r;
		j += tx_size;
	}

	if (extra > 0) {
		r = spi_xmit(spi_dev, (void *)addr + j, extra, SPI_DIR_TX);
		if (r < 0)
			return r;
	}

	return spi_xmit(spi_dev, padding, sizeof(padding), SPI_DIR_TX);
}

int s5c73m3_spi_read(struct s5c73m3 *state, void *addr,
		     const unsigned int len, const unsigned int tx_size)
{
	struct spi_device *spi_dev = state->spi_dev;
	u32 count = len / tx_size;
	u32 extra = len % tx_size;
	unsigned int i, j = 0;
	int r = 0;

	for (i = 0; i < count; i++) {
		r = spi_xmit(spi_dev, addr + j, tx_size, SPI_DIR_RX);
		if (r < 0)
			return r;
		j += tx_size;
	}

	if (extra > 0)
		return spi_xmit(spi_dev, addr + j, extra, SPI_DIR_RX);

	return 0;
}

static int s5c73m3_spi_probe(struct spi_device *spi)
{
	int r;
	struct s5c73m3 *state = container_of(spi->dev.driver, struct s5c73m3,
					     spidrv.driver);
	spi->bits_per_word = 32;

	r = spi_setup(spi);
	if (r < 0) {
		dev_err(&spi->dev, "spi_setup() failed\n");
		return r;
	}

	mutex_lock(&state->lock);
	state->spi_dev = spi;
	mutex_unlock(&state->lock);

	v4l2_info(&state->sensor_sd, "S5C73M3 SPI probed successfully\n");
	return 0;
}

static int s5c73m3_spi_remove(struct spi_device *spi)
{
	return 0;
}

int s5c73m3_register_spi_driver(struct s5c73m3 *state)
{
	struct spi_driver *spidrv = &state->spidrv;

	spidrv->remove = s5c73m3_spi_remove;
	spidrv->probe = s5c73m3_spi_probe;
	spidrv->driver.name = S5C73M3_SPI_DRV_NAME;
	spidrv->driver.bus = &spi_bus_type;
	spidrv->driver.owner = THIS_MODULE;
	spidrv->driver.of_match_table = s5c73m3_spi_ids;

	return spi_register_driver(spidrv);
}

void s5c73m3_unregister_spi_driver(struct s5c73m3 *state)
{
	spi_unregister_driver(&state->spidrv);
}
