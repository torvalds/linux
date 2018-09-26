/*
 * Microchip KSZ series register access through SPI
 *
 * Copyright (C) 2017
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

#include "ksz_priv.h"

/* SPI frame opcodes */
#define KS_SPIOP_RD			3
#define KS_SPIOP_WR			2

#define SPI_ADDR_SHIFT			24
#define SPI_ADDR_MASK			(BIT(SPI_ADDR_SHIFT) - 1)
#define SPI_TURNAROUND_SHIFT		5

static int ksz_spi_read_reg(struct spi_device *spi, u32 reg, u8 *val,
			    unsigned int len)
{
	u32 txbuf;
	int ret;

	txbuf = reg & SPI_ADDR_MASK;
	txbuf |= KS_SPIOP_RD << SPI_ADDR_SHIFT;
	txbuf <<= SPI_TURNAROUND_SHIFT;
	txbuf = cpu_to_be32(txbuf);

	ret = spi_write_then_read(spi, &txbuf, 4, val, len);
	return ret;
}

static int ksz_spi_read(struct ksz_device *dev, u32 reg, u8 *data,
			unsigned int len)
{
	struct spi_device *spi = dev->priv;

	return ksz_spi_read_reg(spi, reg, data, len);
}

static int ksz_spi_read8(struct ksz_device *dev, u32 reg, u8 *val)
{
	return ksz_spi_read(dev, reg, val, 1);
}

static int ksz_spi_read16(struct ksz_device *dev, u32 reg, u16 *val)
{
	int ret = ksz_spi_read(dev, reg, (u8 *)val, 2);

	if (!ret)
		*val = be16_to_cpu(*val);

	return ret;
}

static int ksz_spi_read24(struct ksz_device *dev, u32 reg, u32 *val)
{
	int ret;

	*val = 0;
	ret = ksz_spi_read(dev, reg, (u8 *)val, 3);
	if (!ret) {
		*val = be32_to_cpu(*val);
		/* convert to 24bit */
		*val >>= 8;
	}

	return ret;
}

static int ksz_spi_read32(struct ksz_device *dev, u32 reg, u32 *val)
{
	int ret = ksz_spi_read(dev, reg, (u8 *)val, 4);

	if (!ret)
		*val = be32_to_cpu(*val);

	return ret;
}

static int ksz_spi_write_reg(struct spi_device *spi, u32 reg, u8 *val,
			     unsigned int len)
{
	u32 txbuf;
	u8 data[12];
	int i;

	txbuf = reg & SPI_ADDR_MASK;
	txbuf |= (KS_SPIOP_WR << SPI_ADDR_SHIFT);
	txbuf <<= SPI_TURNAROUND_SHIFT;
	txbuf = cpu_to_be32(txbuf);

	data[0] = txbuf & 0xFF;
	data[1] = (txbuf & 0xFF00) >> 8;
	data[2] = (txbuf & 0xFF0000) >> 16;
	data[3] = (txbuf & 0xFF000000) >> 24;
	for (i = 0; i < len; i++)
		data[i + 4] = val[i];

	return spi_write(spi, &data, 4 + len);
}

static int ksz_spi_write8(struct ksz_device *dev, u32 reg, u8 value)
{
	struct spi_device *spi = dev->priv;

	return ksz_spi_write_reg(spi, reg, &value, 1);
}

static int ksz_spi_write16(struct ksz_device *dev, u32 reg, u16 value)
{
	struct spi_device *spi = dev->priv;

	value = cpu_to_be16(value);
	return ksz_spi_write_reg(spi, reg, (u8 *)&value, 2);
}

static int ksz_spi_write24(struct ksz_device *dev, u32 reg, u32 value)
{
	struct spi_device *spi = dev->priv;

	/* make it to big endian 24bit from MSB */
	value <<= 8;
	value = cpu_to_be32(value);
	return ksz_spi_write_reg(spi, reg, (u8 *)&value, 3);
}

static int ksz_spi_write32(struct ksz_device *dev, u32 reg, u32 value)
{
	struct spi_device *spi = dev->priv;

	value = cpu_to_be32(value);
	return ksz_spi_write_reg(spi, reg, (u8 *)&value, 4);
}

static const struct ksz_io_ops ksz_spi_ops = {
	.read8 = ksz_spi_read8,
	.read16 = ksz_spi_read16,
	.read24 = ksz_spi_read24,
	.read32 = ksz_spi_read32,
	.write8 = ksz_spi_write8,
	.write16 = ksz_spi_write16,
	.write24 = ksz_spi_write24,
	.write32 = ksz_spi_write32,
};

static int ksz_spi_probe(struct spi_device *spi)
{
	struct ksz_device *dev;
	int ret;

	dev = ksz_switch_alloc(&spi->dev, &ksz_spi_ops, spi);
	if (!dev)
		return -ENOMEM;

	if (spi->dev.platform_data)
		dev->pdata = spi->dev.platform_data;

	ret = ksz_switch_register(dev);
	if (ret)
		return ret;

	spi_set_drvdata(spi, dev);

	return 0;
}

static int ksz_spi_remove(struct spi_device *spi)
{
	struct ksz_device *dev = spi_get_drvdata(spi);

	if (dev)
		ksz_switch_remove(dev);

	return 0;
}

static const struct of_device_id ksz_dt_ids[] = {
	{ .compatible = "microchip,ksz9477" },
	{ .compatible = "microchip,ksz9897" },
	{},
};
MODULE_DEVICE_TABLE(of, ksz_dt_ids);

static struct spi_driver ksz_spi_driver = {
	.driver = {
		.name	= "ksz9477-switch",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ksz_dt_ids),
	},
	.probe	= ksz_spi_probe,
	.remove	= ksz_spi_remove,
};

module_spi_driver(ksz_spi_driver);

MODULE_AUTHOR("Woojung Huh <Woojung.Huh@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ Series Switch SPI access Driver");
MODULE_LICENSE("GPL");
