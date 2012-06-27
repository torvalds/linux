/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

struct drm_plane *exynos_plane_init(struct drm_device *dev,
				    unsigned int possible_crtcs, bool priv);
int exynos_plane_set_zpos_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
struct exynos_drm_overlay *get_exynos_drm_overlay(struct drm_plane *plane);
