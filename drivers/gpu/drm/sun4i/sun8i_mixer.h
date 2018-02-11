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

#define SUN8I_MIXER_MAX_CHAN_COUNT		4

#define SUN8I_MIXER_SIZE(w, h)			(((h) - 1) << 16 | ((w) - 1))
#define SUN8I_MIXER_COORD(x, y)			((y) << 16 | (x))

#define SUN8I_MIXER_GLOBAL_CTL			0x0
#define SUN8I_MIXER_GLOBAL_STATUS		0x4
#define SUN8I_MIXER_GLOBAL_DBUFF		0x8
#define SUN8I_MIXER_GLOBAL_SIZE			0xc

#define SUN8I_MIXER_GLOBAL_CTL_RT_EN		0x1

#define SUN8I_MIXER_GLOBAL_DBUFF_ENABLE		0x1

#define SUN8I_MIXER_BLEND_FCOLOR_CTL		0x1000
#define SUN8I_MIXER_BLEND_ATTR_FCOLOR(x)	(0x1004 + 0x10 * (x) + 0x0)
#define SUN8I_MIXER_BLEND_ATTR_INSIZE(x)	(0x1004 + 0x10 * (x) + 0x4)
#define SUN8I_MIXER_BLEND_ATTR_OFFSET(x)	(0x1004 + 0x10 * (x) + 0x8)
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

/* The following numbers are some still unknown magic numbers */
#define SUN8I_MIXER_BLEND_ATTR_FCOLOR_DEF	0xff000000
#define SUN8I_MIXER_BLEND_FCOLOR_CTL_DEF	0x00000101
#define SUN8I_MIXER_BLEND_PREMULTIPLY_DEF	0x0
#define SUN8I_MIXER_BLEND_BKCOLOR_DEF		0xff000000
#define SUN8I_MIXER_BLEND_MODE_DEF		0x03010301
#define SUN8I_MIXER_BLEND_CK_CTL_DEF		0x0

#define SUN8I_MIXER_BLEND_OUTCTL_INTERLACED	BIT(1)

/*
 * VI channels are not used now, but the support of them may be introduced in
 * the future.
 */

#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR(ch, layer) \
			(0x2000 + 0x1000 * (ch) + 0x20 * (layer) + 0x0)
#define SUN8I_MIXER_CHAN_UI_LAYER_SIZE(ch, layer) \
			(0x2000 + 0x1000 * (ch) + 0x20 * (layer) + 0x4)
#define SUN8I_MIXER_CHAN_UI_LAYER_COORD(ch, layer) \
			(0x2000 + 0x1000 * (ch) + 0x20 * (layer) + 0x8)
#define SUN8I_MIXER_CHAN_UI_LAYER_PITCH(ch, layer) \
			(0x2000 + 0x1000 * (ch) + 0x20 * (layer) + 0xc)
#define SUN8I_MIXER_CHAN_UI_LAYER_TOP_LADDR(ch, layer) \
			(0x2000 + 0x1000 * (ch) + 0x20 * (layer) + 0x10)
#define SUN8I_MIXER_CHAN_UI_LAYER_BOT_LADDR(ch, layer) \
			(0x2000 + 0x1000 * (ch) + 0x20 * (layer) + 0x14)
#define SUN8I_MIXER_CHAN_UI_LAYER_FCOLOR(ch, layer) \
			(0x2000 + 0x1000 * (ch) + 0x20 * (layer) + 0x18)
#define SUN8I_MIXER_CHAN_UI_TOP_HADDR(ch)	(0x2000 + 0x1000 * (ch) + 0x80)
#define SUN8I_MIXER_CHAN_UI_BOT_HADDR(ch)	(0x2000 + 0x1000 * (ch) + 0x84)
#define SUN8I_MIXER_CHAN_UI_OVL_SIZE(ch)	(0x2000 + 0x1000 * (ch) + 0x88)

#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_EN		BIT(0)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_ALPHA_MODE_MASK	GENMASK(2, 1)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_FBFMT_MASK	GENMASK(11, 8)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_ALPHA_MASK	GENMASK(31, 24)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_ALPHA_MODE_DEF	(1 << 1)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_FBFMT_ARGB8888	(0 << 8)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_FBFMT_XRGB8888	(4 << 8)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_FBFMT_RGB888	(8 << 8)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_ALPHA_DEF	(0xff << 24)

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

void sun8i_mixer_layer_enable(struct sun8i_mixer *mixer,
				int layer, bool enable);
int sun8i_mixer_update_layer_coord(struct sun8i_mixer *mixer,
				     int layer, struct drm_plane *plane);
int sun8i_mixer_update_layer_formats(struct sun8i_mixer *mixer,
				       int layer, struct drm_plane *plane);
int sun8i_mixer_update_layer_buffer(struct sun8i_mixer *mixer,
				      int layer, struct drm_plane *plane);
#endif /* _SUN8I_MIXER_H_ */
