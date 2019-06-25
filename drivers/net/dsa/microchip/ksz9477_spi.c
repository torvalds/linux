// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip KSZ9477 series register access through SPI
 *
 * Copyright (C) 2017-2019 Microchip Technology Inc.
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

/* Enough to read all switch port registers. */
#define SPI_TX_BUF_LEN			0x100

static int ksz9477_spi_read_reg(struct spi_device *spi, u32 reg, u8 *val,
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

static int ksz9477_spi_write_reg(struct spi_device *spi, u32 reg, u8 *val,
				 unsigned int len)
{
	u32 *txbuf = (u32 *)val;

	*txbuf = reg & SPI_ADDR_MASK;
	*txbuf |= (KS_SPIOP_WR << SPI_ADDR_SHIFT);
	*txbuf <<= SPI_TURNAROUND_SHIFT;
	*txbuf = cpu_to_be32(*txbuf);

	return spi_write(spi, txbuf, 4 + len);
}

static int ksz_spi_read(struct ksz_device *dev, u32 reg, u8 *data,
			unsigned int len)
{
	struct spi_device *spi = dev->priv;

	return ksz9477_spi_read_reg(spi, reg, data, len);
}

static int ksz_spi_write(struct ksz_device *dev, u32 reg, void *data,
			 unsigned int len)
{
	struct spi_device *spi = dev->priv;

	if (len > SPI_TX_BUF_LEN)
		len = SPI_TX_BUF_LEN;
	memcpy(&dev->txbuf[4], data, len);
	return ksz9477_spi_write_reg(spi, reg, dev->txbuf, len);
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

static int ksz_spi_read32(struct ksz_device *dev, u32 reg, u32 *val)
{
	int ret = ksz_spi_read(dev, reg, (u8 *)val, 4);

	if (!ret)
		*val = be32_to_cpu(*val);

	return ret;
}

static int ksz_spi_write8(struct ksz_device *dev, u32 reg, u8 value)
{
	return ksz_spi_write(dev, reg, &value, 1);
}

static int ksz_spi_write16(struct ksz_device *dev, u32 reg, u16 value)
{
	value = cpu_to_be16(value);
	return ksz_spi_write(dev, reg, &value, 2);
}

static int ksz_spi_write32(struct ksz_device *dev, u32 reg, u32 value)
{
	value = cpu_to_be32(value);
	return ksz_spi_write(dev, reg, &value, 4);
}

static const struct ksz_io_ops ksz9477_spi_ops = {
	.read8 = ksz_spi_read8,
	.read16 = ksz_spi_read16,
	.read32 = ksz_spi_read32,
	.write8 = ksz_spi_write8,
	.write16 = ksz_spi_write16,
	.write32 = ksz_spi_write32,
};

static int ksz9477_spi_probe(struct spi_device *spi)
{
	struct ksz_device *dev;
	int ret;

	dev = ksz_switch_alloc(&spi->dev, &ksz9477_spi_ops, spi);
	if (!dev)
		return -ENOMEM;

	if (spi->dev.platform_data)
		dev->pdata = spi->dev.platform_data;

	dev->txbuf = devm_kzalloc(dev->dev, 4 + SPI_TX_BUF_LEN, GFP_KERNEL);

	ret = ksz9477_switch_register(dev);

	/* Main DSA driver may not be started yet. */
	if (ret)
		return ret;

	spi_set_drvdata(spi, dev);

	return 0;
}

static int ksz9477_spi_remove(struct spi_device *spi)
{
	struct ksz_device *dev = spi_get_drvdata(spi);

	if (dev)
		ksz_switch_remove(dev);

	return 0;
}

static void ksz9477_spi_shutdown(struct spi_device *spi)
{
	struct ksz_device *dev = spi_get_drvdata(spi);

	if (dev && dev->dev_ops->shutdown)
		dev->dev_ops->shutdown(dev);
}

static const struct of_device_id ksz9477_dt_ids[] = {
	{ .compatible = "microchip,ksz9477" },
	{ .compatible = "microchip,ksz9897" },
	{ .compatible = "microchip,ksz9893" },
	{ .compatible = "microchip,ksz9563" },
	{},
};
MODULE_DEVICE_TABLE(of, ksz9477_dt_ids);

static struct spi_driver ksz9477_spi_driver = {
	.driver = {
		.name	= "ksz9477-switch",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ksz9477_dt_ids),
	},
	.probe	= ksz9477_spi_probe,
	.remove	= ksz9477_spi_remove,
	.shutdown = ksz9477_spi_shutdown,
};

module_spi_driver(ksz9477_spi_driver);

MODULE_AUTHOR("Woojung Huh <Woojung.Huh@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ9477 Series Switch SPI access Driver");
MODULE_LICENSE("GPL");
