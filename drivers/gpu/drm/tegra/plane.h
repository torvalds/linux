/*
 * Copyright (C) 2017 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef TEGRA_PLANE_H
#define TEGRA_PLANE_H 1

#include <drm/drm_plane.h>

struct tegra_bo;
struct tegra_dc;

struct tegra_plane {
	struct drm_plane base;
	struct tegra_dc *dc;
	unsigned int offset;
	unsigned int index;
};

struct tegra_cursor {
	struct tegra_plane base;

	struct tegra_bo *bo;
	unsigned int width;
	unsigned int height;
};

static inline struct tegra_plane *to_tegra_plane(struct drm_plane *plane)
{
	return container_of(plane, struct tegra_plane, base);
}

struct tegra_plane_legacy_blending_state {
	bool alpha;
	bool top;
};

struct tegra_plane_state {
	struct drm_plane_state base;

	struct tegra_bo_tiling tiling;
	u32 format;
	u32 swap;

	bool bottom_up;

	/* used for legacy blending support only */
	struct tegra_plane_legacy_blending_state blending[2];
	bool opaque;
};

static inline struct tegra_plane_state *
to_tegra_plane_state(struct drm_plane_state *state)
{
	if (state)
		return container_of(state, struct tegra_plane_state, base);

	return NULL;
}

extern const struct drm_plane_funcs tegra_plane_funcs;

int tegra_plane_state_add(struct tegra_plane *plane,
			  struct drm_plane_state *state);

int tegra_plane_format(u32 fourcc, u32 *format, u32 *swap);
bool tegra_plane_format_is_yuv(unsigned int format, bool *planar);
int tegra_plane_setup_legacy_state(struct tegra_plane *tegra,
				   struct tegra_plane_state *state);

#endif /* TEGRA_PLANE_H */
