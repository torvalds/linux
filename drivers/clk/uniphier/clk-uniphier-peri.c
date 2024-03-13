// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include "clk-uniphier.h"

#define UNIPHIER_PERI_CLK_UART(idx, ch)					\
	UNIPHIER_CLK_GATE("uart" #ch, (idx), "uart", 0x24, 19 + (ch))

#define UNIPHIER_PERI_CLK_I2C_COMMON					\
	UNIPHIER_CLK_GATE("i2c-common", -1, "i2c", 0x20, 1)

#define UNIPHIER_PERI_CLK_I2C(idx, ch)					\
	UNIPHIER_CLK_GATE("i2c" #ch, (idx), "i2c-common", 0x24, 5 + (ch))

#define UNIPHIER_PERI_CLK_FI2C(idx, ch)					\
	UNIPHIER_CLK_GATE("i2c" #ch, (idx), "i2c", 0x24, 24 + (ch))

#define UNIPHIER_PERI_CLK_SCSSI(idx, ch)				\
	UNIPHIER_CLK_GATE("scssi" #ch, (idx), "spi", 0x20, 17 + (ch))

#define UNIPHIER_PERI_CLK_MCSSI(idx)					\
	UNIPHIER_CLK_GATE("mcssi", (idx), "spi", 0x24, 14)

const struct uniphier_clk_data uniphier_ld4_peri_clk_data[] = {
	UNIPHIER_PERI_CLK_UART(0, 0),
	UNIPHIER_PERI_CLK_UART(1, 1),
	UNIPHIER_PERI_CLK_UART(2, 2),
	UNIPHIER_PERI_CLK_UART(3, 3),
	UNIPHIER_PERI_CLK_I2C_COMMON,
	UNIPHIER_PERI_CLK_I2C(4, 0),
	UNIPHIER_PERI_CLK_I2C(5, 1),
	UNIPHIER_PERI_CLK_I2C(6, 2),
	UNIPHIER_PERI_CLK_I2C(7, 3),
	UNIPHIER_PERI_CLK_I2C(8, 4),
	UNIPHIER_PERI_CLK_SCSSI(11, 0),
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_pro4_peri_clk_data[] = {
	UNIPHIER_PERI_CLK_UART(0, 0),
	UNIPHIER_PERI_CLK_UART(1, 1),
	UNIPHIER_PERI_CLK_UART(2, 2),
	UNIPHIER_PERI_CLK_UART(3, 3),
	UNIPHIER_PERI_CLK_FI2C(4, 0),
	UNIPHIER_PERI_CLK_FI2C(5, 1),
	UNIPHIER_PERI_CLK_FI2C(6, 2),
	UNIPHIER_PERI_CLK_FI2C(7, 3),
	UNIPHIER_PERI_CLK_FI2C(8, 4),
	UNIPHIER_PERI_CLK_FI2C(9, 5),
	UNIPHIER_PERI_CLK_FI2C(10, 6),
	UNIPHIER_PERI_CLK_SCSSI(11, 0),
	UNIPHIER_PERI_CLK_SCSSI(12, 1),
	UNIPHIER_PERI_CLK_SCSSI(13, 2),
	UNIPHIER_PERI_CLK_SCSSI(14, 3),
	UNIPHIER_PERI_CLK_MCSSI(15),
	{ /* sentinel */ }
};
