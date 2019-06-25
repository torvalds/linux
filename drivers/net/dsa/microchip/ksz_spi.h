/* SPDX-License-Identifier: GPL-2.0
 * Microchip KSZ series SPI access common header
 *
 * Copyright (C) 2017-2018 Microchip Technology Inc.
 *	Tristram Ha <Tristram.Ha@microchip.com>
 */

#ifndef __KSZ_SPI_H
#define __KSZ_SPI_H

/* Chip dependent SPI access */
static int ksz_spi_read(struct ksz_device *dev, u32 reg, u8 *data,
			unsigned int len);
static int ksz_spi_write(struct ksz_device *dev, u32 reg, void *data,
			 unsigned int len);

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

#endif
