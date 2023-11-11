/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2021, 2022 Innovative Advantage Inc. */

#ifndef _MFD_OCELOT_H
#define _MFD_OCELOT_H

#include <linux/kconfig.h>

struct device;
struct regmap;
struct resource;

/**
 * struct ocelot_ddata - Private data for an external Ocelot chip
 * @gcb_regmap:		General Configuration Block regmap. Used for
 *			operations like chip reset.
 * @cpuorg_regmap:	CPU Device Origin Block regmap. Used for operations
 *			like SPI bus configuration.
 * @spi_padding_bytes:	Number of padding bytes that must be thrown out before
 *			read data gets returned. This is calculated during
 *			initialization based on bus speed.
 * @dummy_buf:		Zero-filled buffer of spi_padding_bytes size. The dummy
 *			bytes that will be sent out between the address and
 *			data of a SPI read operation.
 */
struct ocelot_ddata {
	struct regmap *gcb_regmap;
	struct regmap *cpuorg_regmap;
	int spi_padding_bytes;
	void *dummy_buf;
};

int ocelot_chip_reset(struct device *dev);
int ocelot_core_init(struct device *dev);

/* SPI-specific routines that won't be necessary for other interfaces */
struct regmap *ocelot_spi_init_regmap(struct device *dev,
				      const struct resource *res);

#define OCELOT_SPI_BYTE_ORDER_LE 0x00000000
#define OCELOT_SPI_BYTE_ORDER_BE 0x81818181

#ifdef __LITTLE_ENDIAN
#define OCELOT_SPI_BYTE_ORDER OCELOT_SPI_BYTE_ORDER_LE
#else
#define OCELOT_SPI_BYTE_ORDER OCELOT_SPI_BYTE_ORDER_BE
#endif

#endif
