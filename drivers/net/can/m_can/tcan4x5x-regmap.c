// SPDX-License-Identifier: GPL-2.0
//
// tcan4x5x - Texas Instruments TCAN4x5x Family CAN controller driver
//
// Copyright (c) 2020 Pengutronix,
//                    Marc Kleine-Budde <kernel@pengutronix.de>
// Copyright (c) 2018-2019 Texas Instruments Incorporated
//                    http://www.ti.com/

#include "tcan4x5x.h"

#define TCAN4X5X_WRITE_CMD (0x61 << 24)
#define TCAN4X5X_READ_CMD (0x41 << 24)

#define TCAN4X5X_MAX_REGISTER 0x8ffc

static int tcan4x5x_regmap_gather_write(void *context, const void *reg,
					size_t reg_len, const void *val,
					size_t val_len)
{
	struct spi_device *spi = context;
	struct spi_message m;
	u32 addr;
	struct spi_transfer t[2] = {
		{ .tx_buf = &addr, .len = reg_len, .cs_change = 0,},
		{ .tx_buf = val, .len = val_len, },
	};

	addr = TCAN4X5X_WRITE_CMD | (*((u16 *)reg) << 8) | val_len >> 2;

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	return spi_sync(spi, &m);
}

static int tcan4x5x_regmap_write(void *context, const void *data, size_t count)
{
	return tcan4x5x_regmap_gather_write(context, data, sizeof(u32),
					    data + sizeof(u32),
					    count - sizeof(u32));
}

static int tcan4x5x_regmap_read(void *context,
				const void *reg, size_t reg_size,
				void *val, size_t val_size)
{
	struct spi_device *spi = context;

	u32 addr = TCAN4X5X_READ_CMD | (*((u16 *)reg) << 8) | val_size >> 2;

	return spi_write_then_read(spi, &addr, reg_size, (u32 *)val, val_size);
}

static const struct regmap_config tcan4x5x_regmap = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.cache_type = REGCACHE_NONE,
	.max_register = TCAN4X5X_MAX_REGISTER,
};

static const struct regmap_bus tcan4x5x_bus = {
	.write = tcan4x5x_regmap_write,
	.gather_write = tcan4x5x_regmap_gather_write,
	.read = tcan4x5x_regmap_read,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.max_raw_read = 256,
	.max_raw_write = 256,
};

int tcan4x5x_regmap_init(struct tcan4x5x_priv *priv)
{
	priv->regmap = devm_regmap_init(&priv->spi->dev, &tcan4x5x_bus,
					priv->spi, &tcan4x5x_regmap);
	return PTR_ERR_OR_ZERO(priv->regmap);
}
