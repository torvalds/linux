/*
 * Copyright (C) Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef _SUN8I_VI_LAYER_H_
#define _SUN8I_VI_LAYER_H_

#include <drm/drm_plane.h>

#define SUN8I_MIXER_CHAN_VI_LAYER_ATTR(ch, layer) \
		(0x2000 + 0x1000 * (ch) + 0x30 * (layer) + 0x0)
#define SUN8I_MIXER_CHAN_VI_LAYER_SIZE(ch, layer) \
		(0x2000 + 0x1000 * (ch) + 0x30 * (layer) + 0x4)
#define SUN8I_MIXER_CHAN_VI_LAYER_COORD(ch, layer) \
		(0x2000 + 0x1000 * (ch) + 0x30 * (layer) + 0x8)
#define SUN8I_MIXER_CHAN_VI_LAYER_PITCH(ch, layer, plane) \
		(0x2000 + 0x1000 * (ch) + 0x30 * (layer) + 0xc + 4 * (plane))
#define SUN8I_MIXER_CHAN_VI_LAYER_TOP_LADDR(ch, layer, plane) \
		(0x2000 + 0x1000 * (ch) + 0x30 * (layer) + 0x18 + 4 * (plane))
#define SUN8I_MIXER_CHAN_VI_OVL_SIZE(ch)	(0x2000 + 0x1000 * (ch) + 0xe8)

#define SUN8I_MIXER_CHAN_VI_LAYER_ATTR_EN		BIT(0)
/* RGB mode should be set for RGB formats and cleared for YCbCr */
#define SUN8I_MIXER_CHAN_VI_LAYER_ATTR_RGB_MODE		BIT(15)
#define SUN8I_MIXER_CHAN_VI_LAYER_ATTR_FBFMT_OFFSET	8
#define SUN8I_MIXER_CHAN_VI_LAYER_ATTR_FBFMT_MASK	GENMASK(12, 8)

struct sun8i_mixer;

struct sun8i_vi_layer {
	struct drm_plane	plane;
	struct sun8i_mixer	*mixer;
	int			channel;
	int			overlay;
};

static inline struct sun8i_vi_layer *
plane_to_sun8i_vi_layer(struct drm_plane *plane)
{
	return container_of(plane, struct sun8i_vi_layer, plane);
}

struct sun8i_vi_layer *sun8i_vi_layer_init_one(struct drm_device *drm,
					       struct sun8i_mixer *mixer,
					       int index);
#endif /* _SUN8I_VI_LAYER_H_ */
