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

#define UNIPHIER_MIO_CLK_SD_FIXED					\
	UNIPHIER_CLK_FACTOR("sd-44m", -1, "sd-133m", 1, 3),		\
	UNIPHIER_CLK_FACTOR("sd-33m", -1, "sd-200m", 1, 6),		\
	UNIPHIER_CLK_FACTOR("sd-50m", -1, "sd-200m", 1, 4),		\
	UNIPHIER_CLK_FACTOR("sd-67m", -1, "sd-200m", 1, 3),		\
	UNIPHIER_CLK_FACTOR("sd-100m", -1, "sd-200m", 1, 2),		\
	UNIPHIER_CLK_FACTOR("sd-40m", -1, "sd-200m", 1, 5),		\
	UNIPHIER_CLK_FACTOR("sd-25m", -1, "sd-200m", 1, 8),		\
	UNIPHIER_CLK_FACTOR("sd-22m", -1, "sd-133m", 1, 6)

#define UNIPHIER_MIO_CLK_SD(_idx, ch)					\
	{								\
		.name = "sd" #ch "-sel",				\
		.type = UNIPHIER_CLK_TYPE_MUX,				\
		.idx = -1,						\
		.data.mux = {						\
			.parent_names = {				\
				"sd-44m",				\
				"sd-33m",				\
				"sd-50m",				\
				"sd-67m",				\
				"sd-100m",				\
				"sd-40m",				\
				"sd-25m",				\
				"sd-22m",				\
			},						\
			.num_parents = 8,				\
			.reg = 0x30 + 0x200 * (ch),			\
			.masks = {					\
				0x00031000,				\
				0x00031000,				\
				0x00031000,				\
				0x00031000,				\
				0x00001300,				\
				0x00001300,				\
				0x00001300,				\
				0x00001300,				\
			},						\
			.vals = {					\
				0x00000000,				\
				0x00010000,				\
				0x00020000,				\
				0x00030000,				\
				0x00001000,				\
				0x00001100,				\
				0x00001200,				\
				0x00001300,				\
			},						\
		},							\
	},								\
	UNIPHIER_CLK_GATE("sd" #ch, (_idx), "sd" #ch "-sel", 0x20 + 0x200 * (ch), 8)

#define UNIPHIER_MIO_CLK_USB2(idx, ch)					\
	UNIPHIER_CLK_GATE("usb2" #ch, (idx), "usb2", 0x20 + 0x200 * (ch), 28)

#define UNIPHIER_MIO_CLK_USB2_PHY(idx, ch)				\
	UNIPHIER_CLK_GATE("usb2" #ch "-phy", (idx), "usb2", 0x20 + 0x200 * (ch), 29)

#define UNIPHIER_MIO_CLK_DMAC(idx)					\
	UNIPHIER_CLK_GATE("miodmac", (idx), "stdmac", 0x20, 25)

const struct uniphier_clk_data uniphier_ld4_mio_clk_data[] = {
	UNIPHIER_MIO_CLK_SD_FIXED,
	UNIPHIER_MIO_CLK_SD(0, 0),
	UNIPHIER_MIO_CLK_SD(1, 1),
	UNIPHIER_MIO_CLK_SD(2, 2),
	UNIPHIER_MIO_CLK_DMAC(7),
	UNIPHIER_MIO_CLK_USB2(8, 0),
	UNIPHIER_MIO_CLK_USB2(9, 1),
	UNIPHIER_MIO_CLK_USB2(10, 2),
	UNIPHIER_MIO_CLK_USB2_PHY(12, 0),
	UNIPHIER_MIO_CLK_USB2_PHY(13, 1),
	UNIPHIER_MIO_CLK_USB2_PHY(14, 2),
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_pro5_sd_clk_data[] = {
	UNIPHIER_MIO_CLK_SD_FIXED,
	UNIPHIER_MIO_CLK_SD(0, 0),
	UNIPHIER_MIO_CLK_SD(1, 1),
	{ /* sentinel */ }
};
