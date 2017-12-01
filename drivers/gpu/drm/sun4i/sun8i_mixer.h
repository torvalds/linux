/*
 * Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.io>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef _SUN8I_MIXER_H_
#define _SUN8I_MIXER_H_

#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "sunxi_engine.h"

#define SUN8I_MIXER_SIZE(w, h)			(((h) - 1) << 16 | ((w) - 1))
#define SUN8I_MIXER_COORD(x, y)			((y) << 16 | (x))

#define SUN8I_MIXER_GLOBAL_CTL			0x0
#define SUN8I_MIXER_GLOBAL_STATUS		0x4
#define SUN8I_MIXER_GLOBAL_DBUFF		0x8
#define SUN8I_MIXER_GLOBAL_SIZE			0xc

#define SUN8I_MIXER_GLOBAL_CTL_RT_EN		BIT(0)

#define SUN8I_MIXER_GLOBAL_DBUFF_ENABLE		BIT(0)

#define SUN8I_MIXER_BLEND_PIPE_CTL		0x1000
#define SUN8I_MIXER_BLEND_ATTR_FCOLOR(x)	(0x1004 + 0x10 * (x) + 0x0)
#define SUN8I_MIXER_BLEND_ATTR_INSIZE(x)	(0x1004 + 0x10 * (x) + 0x4)
#define SUN8I_MIXER_BLEND_ATTR_COORD(x)		(0x1004 + 0x10 * (x) + 0x8)
#define SUN8I_MIXER_BLEND_ROUTE			0x1080
#define SUN8I_MIXER_BLEND_PREMULTIPLY		0x1084
#define SUN8I_MIXER_BLEND_BKCOLOR		0x1088
#define SUN8I_MIXER_BLEND_OUTSIZE		0x108c
#define SUN8I_MIXER_BLEND_MODE(x)		(0x1090 + 0x04 * (x))
#define SUN8I_MIXER_BLEND_CK_CTL		0x10b0
#define SUN8I_MIXER_BLEND_CK_CFG		0x10b4
#define SUN8I_MIXER_BLEND_CK_MAX(x)		(0x10c0 + 0x04 * (x))
#define SUN8I_MIXER_BLEND_CK_MIN(x)		(0x10e0 + 0x04 * (x))
#define SUN8I_MIXER_BLEND_OUTCTL		0x10fc

#define SUN8I_MIXER_BLEND_PIPE_CTL_EN(pipe)	BIT(8 + pipe)
#define SUN8I_MIXER_BLEND_PIPE_CTL_FC_EN(pipe)	BIT(pipe)
/* colors are always in AARRGGBB format */
#define SUN8I_MIXER_BLEND_COLOR_BLACK		0xff000000
/* The following numbers are some still unknown magic numbers */
#define SUN8I_MIXER_BLEND_MODE_DEF		0x03010301

#define SUN8I_MIXER_BLEND_OUTCTL_INTERLACED	BIT(1)

#define SUN8I_MIXER_FBFMT_ARGB8888	0
#define SUN8I_MIXER_FBFMT_ABGR8888	1
#define SUN8I_MIXER_FBFMT_RGBA8888	2
#define SUN8I_MIXER_FBFMT_BGRA8888	3
#define SUN8I_MIXER_FBFMT_XRGB8888	4
#define SUN8I_MIXER_FBFMT_XBGR8888	5
#define SUN8I_MIXER_FBFMT_RGBX8888	6
#define SUN8I_MIXER_FBFMT_BGRX8888	7
#define SUN8I_MIXER_FBFMT_RGB888	8
#define SUN8I_MIXER_FBFMT_BGR888	9
#define SUN8I_MIXER_FBFMT_RGB565	10
#define SUN8I_MIXER_FBFMT_BGR565	11
#define SUN8I_MIXER_FBFMT_ARGB4444	12
#define SUN8I_MIXER_FBFMT_ABGR4444	13
#define SUN8I_MIXER_FBFMT_RGBA4444	14
#define SUN8I_MIXER_FBFMT_BGRA4444	15
#define SUN8I_MIXER_FBFMT_ARGB1555	16
#define SUN8I_MIXER_FBFMT_ABGR1555	17
#define SUN8I_MIXER_FBFMT_RGBA5551	18
#define SUN8I_MIXER_FBFMT_BGRA5551	19

/*
 * These sub-engines are still unknown now, the EN registers are here only to
 * be used to disable these sub-engines.
 */
#define SUN8I_MIXER_VSU_EN			0x20000
#define SUN8I_MIXER_GSU1_EN			0x30000
#define SUN8I_MIXER_GSU2_EN			0x40000
#define SUN8I_MIXER_GSU3_EN			0x50000
#define SUN8I_MIXER_FCE_EN			0xa0000
#define SUN8I_MIXER_BWS_EN			0xa2000
#define SUN8I_MIXER_LTI_EN			0xa4000
#define SUN8I_MIXER_PEAK_EN			0xa6000
#define SUN8I_MIXER_ASE_EN			0xa8000
#define SUN8I_MIXER_FCC_EN			0xaa000
#define SUN8I_MIXER_DCSC_EN			0xb0000

struct de2_fmt_info {
	u32 drm_fmt;
	u32 de2_fmt;
};

struct sun8i_mixer_cfg {
	int		vi_num;
	int		ui_num;
};

struct sun8i_mixer {
	struct sunxi_engine		engine;

	const struct sun8i_mixer_cfg	*cfg;

	struct reset_control		*reset;

	struct clk			*bus_clk;
	struct clk			*mod_clk;
};

static inline struct sun8i_mixer *
engine_to_sun8i_mixer(struct sunxi_engine *engine)
{
	return container_of(engine, struct sun8i_mixer, engine);
}

const struct de2_fmt_info *sun8i_mixer_format_info(u32 format);
#endif /* _SUN8I_MIXER_H_ */
