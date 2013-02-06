/*
 * driver for S5C73M3 SPI
 *
 * Copyright (c) 2011, Samsung Electronics. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

static struct spi_device *g_spi;

static inline
int spi_xmit(const u8 *addr, const int len)
{
	int ret;

	struct spi_message msg;

	struct spi_transfer xfer = {
		.len = len,
		.tx_buf = addr,
	};

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(g_spi, &msg);

	if (ret < 0)
		dev_err(&g_spi->dev, "%s - error %d\n",
				__func__, ret);

	return ret;
}

static inline
int spi_xmit_rx(u8 *in_buf, size_t len)
{
	int ret;
	u8 read_out_buf[2];

	struct spi_message msg;
	struct spi_transfer xfer = {
		.tx_buf = read_out_buf,
		.rx_buf = in_buf,
		.len	= len,
		.cs_change = 0,
	};

	spi_message_init(&msg);

	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(g_spi, &msg);

	if (ret < 0)
		dev_err(&g_spi->dev, "%s - error %d\n",
				__func__, ret);

	return ret;
}

int s5c73m3_spi_read(u8 *buf, size_t len, const int rxSize)
{
	int k;
	int ret = 0;
	int z = 0;

	u8 paddingData[32];
	u32 count = len/rxSize;
	u32 extra = len%rxSize;

	for (k = 0; k < count; k++) {
		ret = spi_xmit_rx(&buf[rxSize*k], rxSize);
		if (ret < 0)
			return -EINVAL;
	}

	if (extra != 0) {
		ret = spi_xmit_rx(&buf[rxSize*k], extra);
		if (ret < 0)
			return -EINVAL;
	}

	return 0;
}

int s5c73m3_spi_write(const u8 *addr, const int len, const int txSize)
{
	int i, j = 0;
	int ret = 0;
	u8 paddingData[32];
	u32 count = len/txSize;
	u32 extra = len%txSize;

	memset(paddingData, 0, sizeof(paddingData));

	for (i = 0 ; i < count ; i++) {
		ret = spi_xmit(&addr[j], txSize);
		j += txSize;
		if (ret < 0)
			goto exit_err;
	}

	if (extra) {
		ret = spi_xmit(&addr[j], extra);
		if (ret < 0)
			goto exit_err;
	}

	ret = spi_xmit(paddingData, 32);
	if (ret < 0)
		goto exit_err;

exit_err:
	return ret;
}

static
int __devinit s5c73m3_spi_probe(struct spi_device *spi)
{
	int ret;

	spi->bits_per_word = 32;
	if (spi_setup(spi)) {
		pr_err("failed to setup spi for s5c73m3_spi\n");
		ret = -EINVAL;
		goto err_setup;
	}

	g_spi = spi;

	pr_err("s5c73m3_spi successfully probed\n");

	return 0;

err_setup:
	return ret;
}

static
int __devexit s5c73m3_spi_remove(struct spi_device *spi)
{
	return 0;
}

static
struct spi_driver s5c73m3_spi_driver = {
	.driver = {
		.name = "s5c73m3_spi",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
	},
	.probe = s5c73m3_spi_probe,
	.remove = __devexit_p(s5c73m3_spi_remove),
};

static
int __init s5c73m3_spi_init(void)
{
	int ret;
	pr_err("%s\n", __func__);

	ret = spi_register_driver(&s5c73m3_spi_driver);

	if (ret)
		pr_err("failed to register s5c73mc fw - %x\n", ret);

	return ret;
}

static
void __exit s5c73m3_spi_exit(void)
{
	spi_unregister_driver(&s5c73m3_spi_driver);
}

module_init(s5c73m3_spi_init);
module_exit(s5c73m3_spi_exit);

MODULE_DESCRIPTION("S5C73M3 SPI driver");
MODULE_LICENSE("GPL");
