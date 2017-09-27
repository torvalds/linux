/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
	{ /* sentinel */ }
};
