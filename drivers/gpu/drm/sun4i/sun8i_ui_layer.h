/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) Icenowy Zheng <icenowy@aosc.io>
 *
 * Based on sun4i_layer.h, which is:
 *   Copyright (C) 2015 Free Electrons
 *   Copyright (C) 2015 NextThing Co
 *
 *   Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#ifndef _SUN8I_UI_LAYER_H_
#define _SUN8I_UI_LAYER_H_

#include <drm/drm_plane.h>

#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR(base, layer) \
			((base) + 0x20 * (layer) + 0x0)
#define SUN8I_MIXER_CHAN_UI_LAYER_SIZE(base, layer) \
			((base) + 0x20 * (layer) + 0x4)
#define SUN8I_MIXER_CHAN_UI_LAYER_COORD(base, layer) \
			((base) + 0x20 * (layer) + 0x8)
#define SUN8I_MIXER_CHAN_UI_LAYER_PITCH(base, layer) \
			((base) + 0x20 * (layer) + 0xc)
#define SUN8I_MIXER_CHAN_UI_LAYER_TOP_LADDR(base, layer) \
			((base) + 0x20 * (layer) + 0x10)
#define SUN8I_MIXER_CHAN_UI_LAYER_BOT_LADDR(base, layer) \
			((base) + 0x20 * (layer) + 0x14)
#define SUN8I_MIXER_CHAN_UI_LAYER_FCOLOR(base, layer) \
			((base) + 0x20 * (layer) + 0x18)
#define SUN8I_MIXER_CHAN_UI_TOP_HADDR(base) \
			((base) + 0x80)
#define SUN8I_MIXER_CHAN_UI_BOT_HADDR(base) \
			((base) + 0x84)
#define SUN8I_MIXER_CHAN_UI_OVL_SIZE(base) \
			((base) + 0x88)

#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_EN		BIT(0)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_ALPHA_MODE_MASK	GENMASK(2, 1)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_FBFMT_MASK	GENMASK(12, 8)
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_FBFMT_OFFSET	8
#define SUN8I_MIXER_CHAN_UI_LAYER_ATTR_ALPHA_MASK	GENMASK(31, 24)

struct sun8i_mixer;

struct sun8i_ui_layer {
	struct drm_plane	plane;
	struct sun8i_mixer	*mixer;
	int			channel;
	int			overlay;
};

static inline struct sun8i_ui_layer *
plane_to_sun8i_ui_layer(struct drm_plane *plane)
{
	return container_of(plane, struct sun8i_ui_layer, plane);
}

struct sun8i_ui_layer *sun8i_ui_layer_init_one(struct drm_device *drm,
					       struct sun8i_mixer *mixer,
					       int index);
#endif /* _SUN8I_UI_LAYER_H_ */
