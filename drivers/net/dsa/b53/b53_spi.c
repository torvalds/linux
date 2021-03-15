/*
 * B53 register access through SPI
 *
 * Copyright (C) 2011-2013 Jonas Gorski <jogo@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <asm/unaligned.h>

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/platform_data/b53.h>

#include "b53_priv.h"

#define B53_SPI_DATA		0xf0

#define B53_SPI_STATUS		0xfe
#define B53_SPI_CMD_SPIF	BIT(7)
#define B53_SPI_CMD_RACK	BIT(5)

#define B53_SPI_CMD_READ	0x00
#define B53_SPI_CMD_WRITE	0x01
#define B53_SPI_CMD_NORMAL	0x60
#define B53_SPI_CMD_FAST	0x10

#define B53_SPI_PAGE_SELECT	0xff

static inline int b53_spi_read_reg(struct spi_device *spi, u8 reg, u8 *val,
				   unsigned int len)
{
	u8 txbuf[2];

	txbuf[0] = B53_SPI_CMD_NORMAL | B53_SPI_CMD_READ;
	txbuf[1] = reg;

	return spi_write_then_read(spi, txbuf, 2, val, len);
}

static inline int b53_spi_clear_status(struct spi_device *spi)
{
	unsigned int i;
	u8 rxbuf;
	int ret;

	for (i = 0; i < 10; i++) {
		ret = b53_spi_read_reg(spi, B53_SPI_STATUS, &rxbuf, 1);
		if (ret)
			return ret;

		if (!(rxbuf & B53_SPI_CMD_SPIF))
			break;

		mdelay(1);
	}

	if (i == 10)
		return -EIO;

	return 0;
}

static inline int b53_spi_set_page(struct spi_device *spi, u8 page)
{
	u8 txbuf[3];

	txbuf[0] = B53_SPI_CMD_NORMAL | B53_SPI_CMD_WRITE;
	txbuf[1] = B53_SPI_PAGE_SELECT;
	txbuf[2] = page;

	return spi_write(spi, txbuf, sizeof(txbuf));
}

static inline int b53_prepare_reg_access(struct spi_device *spi, u8 page)
{
	int ret = b53_spi_clear_status(spi);

	if (ret)
		return ret;

	return b53_spi_set_page(spi, page);
}

static int b53_spi_prepare_reg_read(struct spi_device *spi, u8 reg)
{
	u8 rxbuf;
	int retry_count;
	int ret;

	ret = b53_spi_read_reg(spi, reg, &rxbuf, 1);
	if (ret)
		return ret;

	for (retry_count = 0; retry_count < 10; retry_count++) {
		ret = b53_spi_read_reg(spi, B53_SPI_STATUS, &rxbuf, 1);
		if (ret)
			return ret;

		if (rxbuf & B53_SPI_CMD_RACK)
			break;

		mdelay(1);
	}

	if (retry_count == 10)
		return -EIO;

	return 0;
}

static int b53_spi_read(struct b53_device *dev, u8 page, u8 reg, u8 *data,
			unsigned int len)
{
	struct spi_device *spi = dev->priv;
	int ret;

	ret = b53_prepare_reg_access(spi, page);
	if (ret)
		return ret;

	ret = b53_spi_prepare_reg_read(spi, reg);
	if (ret)
		return ret;

	return b53_spi_read_reg(spi, B53_SPI_DATA, data, len);
}

static int b53_spi_read8(struct b53_device *dev, u8 page, u8 reg, u8 *val)
{
	return b53_spi_read(dev, page, reg, val, 1);
}

static int b53_spi_read16(struct b53_device *dev, u8 page, u8 reg, u16 *val)
{
	__le16 value;
	int ret;

	ret = b53_spi_read(dev, page, reg, (u8 *)&value, 2);

	if (!ret)
		*val = le16_to_cpu(value);

	return ret;
}

static int b53_spi_read32(struct b53_device *dev, u8 page, u8 reg, u32 *val)
{
	__le32 value;
	int ret;

	ret = b53_spi_read(dev, page, reg, (u8 *)&value, 4);

	if (!ret)
		*val = le32_to_cpu(value);

	return ret;
}

static int b53_spi_read48(struct b53_device *dev, u8 page, u8 reg, u64 *val)
{
	__le64 value;
	int ret;

	*val = 0;
	ret = b53_spi_read(dev, page, reg, (u8 *)&value, 6);
	if (!ret)
		*val = le64_to_cpu(value);

	return ret;
}

static int b53_spi_read64(struct b53_device *dev, u8 page, u8 reg, u64 *val)
{
	__le64 value;
	int ret;

	ret = b53_spi_read(dev, page, reg, (u8 *)&value, 8);

	if (!ret)
		*val = le64_to_cpu(value);

	return ret;
}

static int b53_spi_write8(struct b53_device *dev, u8 page, u8 reg, u8 value)
{
	struct spi_device *spi = dev->priv;
	int ret;
	u8 txbuf[3];

	ret = b53_prepare_reg_access(spi, page);
	if (ret)
		return ret;

	txbuf[0] = B53_SPI_CMD_NORMAL | B53_SPI_CMD_WRITE;
	txbuf[1] = reg;
	txbuf[2] = value;

	return spi_write(spi, txbuf, sizeof(txbuf));
}

static int b53_spi_write16(struct b53_device *dev, u8 page, u8 reg, u16 value)
{
	struct spi_device *spi = dev->priv;
	int ret;
	u8 txbuf[4];

	ret = b53_prepare_reg_access(spi, page);
	if (ret)
		return ret;

	txbuf[0] = B53_SPI_CMD_NORMAL | B53_SPI_CMD_WRITE;
	txbuf[1] = reg;
	put_unaligned_le16(value, &txbuf[2]);

	return spi_write(spi, txbuf, sizeof(txbuf));
}

static int b53_spi_write32(struct b53_device *dev, u8 page, u8 reg, u32 value)
{
	struct spi_device *spi = dev->priv;
	int ret;
	u8 txbuf[6];

	ret = b53_prepare_reg_access(spi, page);
	if (ret)
		return ret;

	txbuf[0] = B53_SPI_CMD_NORMAL | B53_SPI_CMD_WRITE;
	txbuf[1] = reg;
	put_unaligned_le32(value, &txbuf[2]);

	return spi_write(spi, txbuf, sizeof(txbuf));
}

static int b53_spi_write48(struct b53_device *dev, u8 page, u8 reg, u64 value)
{
	struct spi_device *spi = dev->priv;
	int ret;
	u8 txbuf[10];

	ret = b53_prepare_reg_access(spi, page);
	if (ret)
		return ret;

	txbuf[0] = B53_SPI_CMD_NORMAL | B53_SPI_CMD_WRITE;
	txbuf[1] = reg;
	put_unaligned_le64(value, &txbuf[2]);

	return spi_write(spi, txbuf, sizeof(txbuf) - 2);
}

static int b53_spi_write64(struct b53_device *dev, u8 page, u8 reg, u64 value)
{
	struct spi_device *spi = dev->priv;
	int ret;
	u8 txbuf[10];

	ret = b53_prepare_reg_access(spi, page);
	if (ret)
		return ret;

	txbuf[0] = B53_SPI_CMD_NORMAL | B53_SPI_CMD_WRITE;
	txbuf[1] = reg;
	put_unaligned_le64(value, &txbuf[2]);

	return spi_write(spi, txbuf, sizeof(txbuf));
}

static const struct b53_io_ops b53_spi_ops = {
	.read8 = b53_spi_read8,
	.read16 = b53_spi_read16,
	.read32 = b53_spi_read32,
	.read48 = b53_spi_read48,
	.read64 = b53_spi_read64,
	.write8 = b53_spi_write8,
	.write16 = b53_spi_write16,
	.write32 = b53_spi_write32,
	.write48 = b53_spi_write48,
	.write64 = b53_spi_write64,
};

static int b53_spi_probe(struct spi_device *spi)
{
	struct b53_device *dev;
	int ret;

	dev = b53_switch_alloc(&spi->dev, &b53_spi_ops, spi);
	if (!dev)
		return -ENOMEM;

	if (spi->dev.platform_data)
		dev->pdata = spi->dev.platform_data;

	ret = b53_switch_register(dev);
	if (ret)
		return ret;

	spi_set_drvdata(spi, dev);

	return 0;
}

static int b53_spi_remove(struct spi_device *spi)
{
	struct b53_device *dev = spi_get_drvdata(spi);

	if (dev)
		b53_switch_remove(dev);

	return 0;
}

static const struct of_device_id b53_spi_of_match[] = {
	{ .compatible = "brcm,bcm5325" },
	{ .compatible = "brcm,bcm5365" },
	{ .compatible = "brcm,bcm5395" },
	{ .compatible = "brcm,bcm5397" },
	{ .compatible = "brcm,bcm5398" },
	{ .compatible = "brcm,bcm53115" },
	{ .compatible = "brcm,bcm53125" },
	{ .compatible = "brcm,bcm53128" },
	{ /* sentinel */ }
};

static struct spi_driver b53_spi_driver = {
	.driver = {
		.name	= "b53-switch",
		.of_match_table = b53_spi_of_match,
	},
	.probe	= b53_spi_probe,
	.remove	= b53_spi_remove,
};

module_spi_driver(b53_spi_driver);

MODULE_AUTHOR("Jonas Gorski <jogo@openwrt.org>");
MODULE_DESCRIPTION("B53 SPI access driver");
MODULE_LICENSE("Dual BSD/GPL");
