/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 NVIDIA CORPORATION.  All rights reserved.
 */

#ifndef TEGRA_HUB_H
#define TEGRA_HUB_H 1

#include <drm/drm_plane.h>

#include "plane.h"

struct tegra_dc;

struct tegra_windowgroup {
	unsigned int usecount;
	struct mutex lock;

	unsigned int index;
	struct host1x_client *parent;
	struct reset_control *rst;
};

struct tegra_shared_plane {
	struct tegra_plane base;
	struct tegra_windowgroup *wgrp;
};

static inline struct tegra_shared_plane *
to_tegra_shared_plane(struct drm_plane *plane)
{
	return container_of(plane, struct tegra_shared_plane, base.base);
}

struct tegra_display_hub_soc {
	unsigned int num_wgrps;
	bool supports_dsc;
};

struct tegra_display_hub {
	struct drm_private_obj base;
	struct host1x_client client;
	struct clk *clk_disp;
	struct clk *clk_dsc;
	struct clk *clk_hub;
	struct reset_control *rst;

	unsigned int num_heads;
	struct clk **clk_heads;

	const struct tegra_display_hub_soc *soc;
	struct tegra_windowgroup *wgrps;
};

static inline struct tegra_display_hub *
to_tegra_display_hub(struct host1x_client *client)
{
	return container_of(client, struct tegra_display_hub, client);
}

struct tegra_display_hub_state {
	struct drm_private_state base;

	struct tegra_dc *dc;
	unsigned long rate;
	struct clk *clk;
};

static inline struct tegra_display_hub_state *
to_tegra_display_hub_state(struct drm_private_state *priv)
{
	return container_of(priv, struct tegra_display_hub_state, base);
}

struct tegra_dc;
struct tegra_plane;

int tegra_display_hub_prepare(struct tegra_display_hub *hub);
void tegra_display_hub_cleanup(struct tegra_display_hub *hub);

struct drm_plane *tegra_shared_plane_create(struct drm_device *drm,
					    struct tegra_dc *dc,
					    unsigned int wgrp,
					    unsigned int index);

int tegra_display_hub_atomic_check(struct drm_device *drm,
				   struct drm_atomic_state *state);
void tegra_display_hub_atomic_commit(struct drm_device *drm,
				     struct drm_atomic_state *state);

#define DC_CMD_IHUB_COMMON_MISC_CTL 0x068
#define  LATENCY_EVENT (1 << 3)

#define DC_DISP_IHUB_COMMON_DISPLAY_FETCH_METER 0x451
#define  CURS_SLOTS(x) (((x) & 0xff) << 8)
#define  WGRP_SLOTS(x) (((x) & 0xff) << 0)

#endif /* TEGRA_HUB_H */
