// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Ethernet driver for the WIZnet W5100/W5200/W5500 chip.
 *
 * Copyright (C) 2016 Akinobu Mita <akinobu.mita@gmail.com>
 *
 * Datasheet:
 * http://www.wiznet.co.kr/wp-content/uploads/wiznethome/Chip/W5100/Document/W5100_Datasheet_v1.2.6.pdf
 * http://wiznethome.cafe24.com/wp-content/uploads/wiznethome/Chip/W5200/Documents/W5200_DS_V140E.pdf
 * http://wizwiki.net/wiki/lib/exe/fetch.php?media=products:w5500:w5500_ds_v106e_141230.pdf
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/of_net.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>

#include "w5100.h"

#define W5100_SPI_WRITE_OPCODE 0xf0
#define W5100_SPI_READ_OPCODE 0x0f

static int w5100_spi_read(struct net_device *ndev, u32 addr)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[3] = { W5100_SPI_READ_OPCODE, addr >> 8, addr & 0xff };
	u8 data;
	int ret;

	ret = spi_write_then_read(spi, cmd, sizeof(cmd), &data, 1);

	return ret ? ret : data;
}

static int w5100_spi_write(struct net_device *ndev, u32 addr, u8 data)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[4] = { W5100_SPI_WRITE_OPCODE, addr >> 8, addr & 0xff, data};

	return spi_write_then_read(spi, cmd, sizeof(cmd), NULL, 0);
}

static int w5100_spi_read16(struct net_device *ndev, u32 addr)
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

static int w5100_spi_write16(struct net_device *ndev, u32 addr, u16 data)
{
	int ret;

	ret = w5100_spi_write(ndev, addr, data >> 8);
	if (ret)
		return ret;

	return w5100_spi_write(ndev, addr + 1, data & 0xff);
}

static int w5100_spi_readbulk(struct net_device *ndev, u32 addr, u8 *buf,
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

static int w5100_spi_writebulk(struct net_device *ndev, u32 addr, const u8 *buf,
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
	.chip_id = W5100,
	.read = w5100_spi_read,
	.write = w5100_spi_write,
	.read16 = w5100_spi_read16,
	.write16 = w5100_spi_write16,
	.readbulk = w5100_spi_readbulk,
	.writebulk = w5100_spi_writebulk,
};

#define W5200_SPI_WRITE_OPCODE 0x80

struct w5200_spi_priv {
	/* Serialize access to cmd_buf */
	struct mutex cmd_lock;

	/* DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	u8 cmd_buf[4] ____cacheline_aligned;
};

static struct w5200_spi_priv *w5200_spi_priv(struct net_device *ndev)
{
	return w5100_ops_priv(ndev);
}

static int w5200_spi_init(struct net_device *ndev)
{
	struct w5200_spi_priv *spi_priv = w5200_spi_priv(ndev);

	mutex_init(&spi_priv->cmd_lock);

	return 0;
}

static int w5200_spi_read(struct net_device *ndev, u32 addr)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[4] = { addr >> 8, addr & 0xff, 0, 1 };
	u8 data;
	int ret;

	ret = spi_write_then_read(spi, cmd, sizeof(cmd), &data, 1);

	return ret ? ret : data;
}

static int w5200_spi_write(struct net_device *ndev, u32 addr, u8 data)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[5] = { addr >> 8, addr & 0xff, W5200_SPI_WRITE_OPCODE, 1, data };

	return spi_write_then_read(spi, cmd, sizeof(cmd), NULL, 0);
}

static int w5200_spi_read16(struct net_device *ndev, u32 addr)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[4] = { addr >> 8, addr & 0xff, 0, 2 };
	__be16 data;
	int ret;

	ret = spi_write_then_read(spi, cmd, sizeof(cmd), &data, sizeof(data));

	return ret ? ret : be16_to_cpu(data);
}

static int w5200_spi_write16(struct net_device *ndev, u32 addr, u16 data)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[6] = {
		addr >> 8, addr & 0xff,
		W5200_SPI_WRITE_OPCODE, 2,
		data >> 8, data & 0xff
	};

	return spi_write_then_read(spi, cmd, sizeof(cmd), NULL, 0);
}

static int w5200_spi_readbulk(struct net_device *ndev, u32 addr, u8 *buf,
			      int len)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	struct w5200_spi_priv *spi_priv = w5200_spi_priv(ndev);
	struct spi_transfer xfer[] = {
		{
			.tx_buf = spi_priv->cmd_buf,
			.len = sizeof(spi_priv->cmd_buf),
		},
		{
			.rx_buf = buf,
			.len = len,
		},
	};
	int ret;

	mutex_lock(&spi_priv->cmd_lock);

	spi_priv->cmd_buf[0] = addr >> 8;
	spi_priv->cmd_buf[1] = addr;
	spi_priv->cmd_buf[2] = len >> 8;
	spi_priv->cmd_buf[3] = len;
	ret = spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));

	mutex_unlock(&spi_priv->cmd_lock);

	return ret;
}

static int w5200_spi_writebulk(struct net_device *ndev, u32 addr, const u8 *buf,
			       int len)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	struct w5200_spi_priv *spi_priv = w5200_spi_priv(ndev);
	struct spi_transfer xfer[] = {
		{
			.tx_buf = spi_priv->cmd_buf,
			.len = sizeof(spi_priv->cmd_buf),
		},
		{
			.tx_buf = buf,
			.len = len,
		},
	};
	int ret;

	mutex_lock(&spi_priv->cmd_lock);

	spi_priv->cmd_buf[0] = addr >> 8;
	spi_priv->cmd_buf[1] = addr;
	spi_priv->cmd_buf[2] = W5200_SPI_WRITE_OPCODE | (len >> 8);
	spi_priv->cmd_buf[3] = len;
	ret = spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));

	mutex_unlock(&spi_priv->cmd_lock);

	return ret;
}

static const struct w5100_ops w5200_ops = {
	.may_sleep = true,
	.chip_id = W5200,
	.read = w5200_spi_read,
	.write = w5200_spi_write,
	.read16 = w5200_spi_read16,
	.write16 = w5200_spi_write16,
	.readbulk = w5200_spi_readbulk,
	.writebulk = w5200_spi_writebulk,
	.init = w5200_spi_init,
};

#define W5500_SPI_BLOCK_SELECT(addr) (((addr) >> 16) & 0x1f)
#define W5500_SPI_READ_CONTROL(addr) (W5500_SPI_BLOCK_SELECT(addr) << 3)
#define W5500_SPI_WRITE_CONTROL(addr)	\
	((W5500_SPI_BLOCK_SELECT(addr) << 3) | BIT(2))

struct w5500_spi_priv {
	/* Serialize access to cmd_buf */
	struct mutex cmd_lock;

	/* DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	u8 cmd_buf[3] ____cacheline_aligned;
};

static struct w5500_spi_priv *w5500_spi_priv(struct net_device *ndev)
{
	return w5100_ops_priv(ndev);
}

static int w5500_spi_init(struct net_device *ndev)
{
	struct w5500_spi_priv *spi_priv = w5500_spi_priv(ndev);

	mutex_init(&spi_priv->cmd_lock);

	return 0;
}

static int w5500_spi_read(struct net_device *ndev, u32 addr)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[3] = {
		addr >> 8,
		addr,
		W5500_SPI_READ_CONTROL(addr)
	};
	u8 data;
	int ret;

	ret = spi_write_then_read(spi, cmd, sizeof(cmd), &data, 1);

	return ret ? ret : data;
}

static int w5500_spi_write(struct net_device *ndev, u32 addr, u8 data)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[4] = {
		addr >> 8,
		addr,
		W5500_SPI_WRITE_CONTROL(addr),
		data
	};

	return spi_write_then_read(spi, cmd, sizeof(cmd), NULL, 0);
}

static int w5500_spi_read16(struct net_device *ndev, u32 addr)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[3] = {
		addr >> 8,
		addr,
		W5500_SPI_READ_CONTROL(addr)
	};
	__be16 data;
	int ret;

	ret = spi_write_then_read(spi, cmd, sizeof(cmd), &data, sizeof(data));

	return ret ? ret : be16_to_cpu(data);
}

static int w5500_spi_write16(struct net_device *ndev, u32 addr, u16 data)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	u8 cmd[5] = {
		addr >> 8,
		addr,
		W5500_SPI_WRITE_CONTROL(addr),
		data >> 8,
		data
	};

	return spi_write_then_read(spi, cmd, sizeof(cmd), NULL, 0);
}

static int w5500_spi_readbulk(struct net_device *ndev, u32 addr, u8 *buf,
			      int len)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	struct w5500_spi_priv *spi_priv = w5500_spi_priv(ndev);
	struct spi_transfer xfer[] = {
		{
			.tx_buf = spi_priv->cmd_buf,
			.len = sizeof(spi_priv->cmd_buf),
		},
		{
			.rx_buf = buf,
			.len = len,
		},
	};
	int ret;

	mutex_lock(&spi_priv->cmd_lock);

	spi_priv->cmd_buf[0] = addr >> 8;
	spi_priv->cmd_buf[1] = addr;
	spi_priv->cmd_buf[2] = W5500_SPI_READ_CONTROL(addr);
	ret = spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));

	mutex_unlock(&spi_priv->cmd_lock);

	return ret;
}

static int w5500_spi_writebulk(struct net_device *ndev, u32 addr, const u8 *buf,
			       int len)
{
	struct spi_device *spi = to_spi_device(ndev->dev.parent);
	struct w5500_spi_priv *spi_priv = w5500_spi_priv(ndev);
	struct spi_transfer xfer[] = {
		{
			.tx_buf = spi_priv->cmd_buf,
			.len = sizeof(spi_priv->cmd_buf),
		},
		{
			.tx_buf = buf,
			.len = len,
		},
	};
	int ret;

	mutex_lock(&spi_priv->cmd_lock);

	spi_priv->cmd_buf[0] = addr >> 8;
	spi_priv->cmd_buf[1] = addr;
	spi_priv->cmd_buf[2] = W5500_SPI_WRITE_CONTROL(addr);
	ret = spi_sync_transfer(spi, xfer, ARRAY_SIZE(xfer));

	mutex_unlock(&spi_priv->cmd_lock);

	return ret;
}

static const struct w5100_ops w5500_ops = {
	.may_sleep = true,
	.chip_id = W5500,
	.read = w5500_spi_read,
	.write = w5500_spi_write,
	.read16 = w5500_spi_read16,
	.write16 = w5500_spi_write16,
	.readbulk = w5500_spi_readbulk,
	.writebulk = w5500_spi_writebulk,
	.init = w5500_spi_init,
};

static const struct of_device_id w5100_of_match[] = {
	{ .compatible = "wiznet,w5100", .data = (const void*)W5100, },
	{ .compatible = "wiznet,w5200", .data = (const void*)W5200, },
	{ .compatible = "wiznet,w5500", .data = (const void*)W5500, },
	{ },
};
MODULE_DEVICE_TABLE(of, w5100_of_match);

static int w5100_spi_probe(struct spi_device *spi)
{
	const struct of_device_id *of_id;
	const struct w5100_ops *ops;
	kernel_ulong_t driver_data;
	const void *mac = NULL;
	u8 tmpmac[ETH_ALEN];
	int priv_size;
	int ret;

	ret = of_get_mac_address(spi->dev.of_node, tmpmac);
	if (!ret)
		mac = tmpmac;

	if (spi->dev.of_node) {
		of_id = of_match_device(w5100_of_match, &spi->dev);
		if (!of_id)
			return -ENODEV;
		driver_data = (kernel_ulong_t)of_id->data;
	} else {
		driver_data = spi_get_device_id(spi)->driver_data;
	}

	switch (driver_data) {
	case W5100:
		ops = &w5100_spi_ops;
		priv_size = 0;
		break;
	case W5200:
		ops = &w5200_ops;
		priv_size = sizeof(struct w5200_spi_priv);
		break;
	case W5500:
		ops = &w5500_ops;
		priv_size = sizeof(struct w5500_spi_priv);
		break;
	default:
		return -EINVAL;
	}

	return w5100_probe(&spi->dev, ops, priv_size, mac, spi->irq, -EINVAL);
}

static int w5100_spi_remove(struct spi_device *spi)
{
	return w5100_remove(&spi->dev);
}

static const struct spi_device_id w5100_spi_ids[] = {
	{ "w5100", W5100 },
	{ "w5200", W5200 },
	{ "w5500", W5500 },
	{}
};
MODULE_DEVICE_TABLE(spi, w5100_spi_ids);

static struct spi_driver w5100_spi_driver = {
	.driver		= {
		.name	= "w5100",
		.pm	= &w5100_pm_ops,
		.of_match_table = w5100_of_match,
	},
	.probe		= w5100_spi_probe,
	.remove		= w5100_spi_remove,
	.id_table	= w5100_spi_ids,
};
module_spi_driver(w5100_spi_driver);

MODULE_DESCRIPTION("WIZnet W5100/W5200/W5500 Ethernet driver for SPI mode");
MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
MODULE_LICENSE("GPL");
