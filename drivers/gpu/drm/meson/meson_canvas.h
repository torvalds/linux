/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
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

/* Canvas LUT Memory */

#ifndef __MESON_CANVAS_H
#define __MESON_CANVAS_H

#define MESON_CANVAS_ID_OSD1	0x4e
#define MESON_CANVAS_ID_VD1_0	0x60
#define MESON_CANVAS_ID_VD1_1	0x61
#define MESON_CANVAS_ID_VD1_2	0x62

/* Canvas configuration. */
#define MESON_CANVAS_WRAP_NONE	0x00
#define	MESON_CANVAS_WRAP_X	0x01
#define	MESON_CANVAS_WRAP_Y	0x02

#define	MESON_CANVAS_BLKMODE_LINEAR	0x00
#define	MESON_CANVAS_BLKMODE_32x32	0x01
#define	MESON_CANVAS_BLKMODE_64x64	0x02

#define MESON_CANVAS_ENDIAN_SWAP16	0x1
#define MESON_CANVAS_ENDIAN_SWAP32	0x3
#define MESON_CANVAS_ENDIAN_SWAP64	0x7
#define MESON_CANVAS_ENDIAN_SWAP128	0xf

void meson_canvas_setup(struct meson_drm *priv,
			uint32_t canvas_index, uint32_t addr,
			uint32_t stride, uint32_t height,
			unsigned int wrap,
			unsigned int blkmode,
			unsigned int endian);

#endif /* __MESON_CANVAS_H */
