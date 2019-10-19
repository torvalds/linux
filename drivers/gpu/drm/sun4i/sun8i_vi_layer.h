/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) Jernej Skrabec <jernej.skrabec@siol.net>
 */

#ifndef _SUN8I_VI_LAYER_H_
#define _SUN8I_VI_LAYER_H_

#include <drm/drm_plane.h>

#define SUN8I_MIXER_CHAN_VI_LAYER_ATTR(base, layer) \
		((base) + 0x30 * (layer) + 0x0)
#define SUN8I_MIXER_CHAN_VI_LAYER_SIZE(base, layer) \
		((base) + 0x30 * (layer) + 0x4)
#define SUN8I_MIXER_CHAN_VI_LAYER_COORD(base, layer) \
		((base) + 0x30 * (layer) + 0x8)
#define SUN8I_MIXER_CHAN_VI_LAYER_PITCH(base, layer, plane) \
		((base) + 0x30 * (layer) + 0xc + 4 * (plane))
#define SUN8I_MIXER_CHAN_VI_LAYER_TOP_LADDR(base, layer, plane) \
		((base) + 0x30 * (layer) + 0x18 + 4 * (plane))
#define SUN8I_MIXER_CHAN_VI_OVL_SIZE(base) \
		((base) + 0xe8)
#define SUN8I_MIXER_CHAN_VI_HDS_Y(base) \
		((base) + 0xf0)
#define SUN8I_MIXER_CHAN_VI_HDS_UV(base) \
		((base) + 0xf4)
#define SUN8I_MIXER_CHAN_VI_VDS_Y(base) \
		((base) + 0xf8)
#define SUN8I_MIXER_CHAN_VI_VDS_UV(base) \
		((base) + 0xfc)

#define SUN8I_MIXER_CHAN_VI_LAYER_ATTR_EN		BIT(0)
/* RGB mode should be set for RGB formats and cleared for YCbCr */
#define SUN8I_MIXER_CHAN_VI_LAYER_ATTR_RGB_MODE		BIT(15)
#define SUN8I_MIXER_CHAN_VI_LAYER_ATTR_FBFMT_OFFSET	8
#define SUN8I_MIXER_CHAN_VI_LAYER_ATTR_FBFMT_MASK	GENMASK(12, 8)
#define SUN50I_MIXER_CHAN_VI_LAYER_ATTR_ALPHA_MASK	GENMASK(31, 24)
#define SUN50I_MIXER_CHAN_VI_LAYER_ATTR_ALPHA(x)	((x) << 24)

#define SUN8I_MIXER_CHAN_VI_DS_N(x)			((x) << 16)
#define SUN8I_MIXER_CHAN_VI_DS_M(x)			((x) << 0)

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
