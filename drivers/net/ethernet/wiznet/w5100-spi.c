/*
 * Ethernet driver for the WIZnet W5100 chip.
 *
 * Copyright (C) 2016 Akinobu Mita <akinobu.mita@gmail.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/spi/spi.h>

#include "w5100.h"

#define W5100_SPI_WRITE_OPCODE 0xf0
#define W5100_SPI_READ_OPCODE 0x0f

static int w5100_spi_read(struct net_device *ndev, u16 addr)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[3] = { W5100_SPI_READ_OPCODE, addr >> 8, addr & 0xff };
	u8 data;
	int ret;

	ret = spi_write_then_read(spi, cmd, sizeof(cmd), &data, 1);

	return ret ? ret : data;
}

static int w5100_spi_write(struct net_device *ndev, u16 addr, u8 data)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[4] = { W5100_SPI_WRITE_OPCODE, addr >> 8, addr & 0xff, data};

	return spi_write_then_read(spi, cmd, sizeof(cmd), NULL, 0);
}

static int w5100_spi_read16(struct net_device *ndev, u16 addr)
{
	u16 data;
	int ret;

	ret = w5100_spi_read(ndev, addr);
	if (ret < 0)
		return ret;
	data = ret << 8;
	ret = w5100_spi_read(ndev, addr + 1);

	return ret < 0 ? ret : data | ret;
}

static int w5100_spi_write16(struct net_device *ndev, u16 addr, u16 data)
{
	int ret;

	ret = w5100_spi_write(ndev, addr, data >> 8);
	if (ret)
		return ret;

	return w5100_spi_write(ndev, addr + 1, data & 0xff);
}

static int w5100_spi_readbulk(struct net_device *ndev, u16 addr, u8 *buf,
			      int len)
{
	int i;

	for (i = 0; i < len; i++) {
		int ret = w5100_spi_read(ndev, addr + i);

		if (ret < 0)
			return ret;
		buf[i] = ret;
	}

	return 0;
}

static int w5100_spi_writebulk(struct net_device *ndev, u16 addr, const u8 *buf,
			       int len)
{
	int i;

	for (i = 0; i < len; i++) {
		int ret = w5100_spi_write(ndev, addr + i, buf[i]);

		if (ret)
			return ret;
	}

	return 0;
}

static const struct w5100_ops w5100_spi_ops = {
	.may_sleep = true,
	.read = w5100_spi_read,
	.write = w5100_spi_write,
	.read16 = w5100_spi_read16,
	.write16 = w5100_spi_write16,
	.readbulk = w5100_spi_readbulk,
	.writebulk = w5100_spi_writebulk,
};

static int w5100_spi_probe(struct spi_device *spi)
{
	return w5100_probe(&spi->dev, &w5100_spi_ops, 0, NULL, spi->irq,
			   -EINVAL);
}

static int w5100_spi_remove(struct spi_device *spi)
{
	return w5100_remove(&spi->dev);
}

static const struct spi_device_id w5100_spi_ids[] = {
	{ "w5100", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, w5100_spi_ids);

static struct spi_driver w5100_spi_driver = {
	.driver		= {
		.name	= "w5100",
		.pm	= &w5100_pm_ops,
	},
	.probe		= w5100_spi_probe,
	.remove		= w5100_spi_remove,
	.id_table	= w5100_spi_ids,
};
module_spi_driver(w5100_spi_driver);

MODULE_DESCRIPTION("WIZnet W5100 Ethernet driver for SPI mode");
MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
MODULE_LICENSE("GPL");
