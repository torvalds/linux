/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include "meson_drv.h"
#include "meson_canvas.h"
#include "meson_registers.h"

/**
 * DOC: Canvas
 *
 * CANVAS is a memory zone where physical memory frames information
 * are stored for the VIU to scanout.
 */

/* DMC Registers */
#define DMC_CAV_LUT_DATAL	0x48 /* 0x12 offset in data sheet */
#define CANVAS_WIDTH_LBIT	29
#define CANVAS_WIDTH_LWID       3
#define DMC_CAV_LUT_DATAH	0x4c /* 0x13 offset in data sheet */
#define CANVAS_WIDTH_HBIT       0
#define CANVAS_HEIGHT_BIT       9
#define CANVAS_BLKMODE_BIT      24
#define DMC_CAV_LUT_ADDR	0x50 /* 0x14 offset in data sheet */
#define CANVAS_LUT_WR_EN        (0x2 << 8)
#define CANVAS_LUT_RD_EN        (0x1 << 8)

void meson_canvas_setup(struct meson_drm *priv,
			uint32_t canvas_index, uint32_t addr,
			uint32_t stride, uint32_t height,
			unsigned int wrap,
			unsigned int blkmode)
{
	unsigned int val;

	regmap_write(priv->dmc, DMC_CAV_LUT_DATAL,
		(((addr + 7) >> 3)) |
		(((stride + 7) >> 3) << CANVAS_WIDTH_LBIT));

	regmap_write(priv->dmc, DMC_CAV_LUT_DATAH,
		((((stride + 7) >> 3) >> CANVAS_WIDTH_LWID) <<
						CANVAS_WIDTH_HBIT) |
		(height << CANVAS_HEIGHT_BIT) |
		(wrap << 22) |
		(blkmode << CANVAS_BLKMODE_BIT));

	regmap_write(priv->dmc, DMC_CAV_LUT_ADDR,
			CANVAS_LUT_WR_EN | canvas_index);

	/* Force a read-back to make sure everything is flushed. */
	regmap_read(priv->dmc, DMC_CAV_LUT_DATAH, &val);
}
