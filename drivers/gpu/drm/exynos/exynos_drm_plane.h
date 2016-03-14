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

int exynos_plane_init(struct drm_device *dev,
		      struct exynos_drm_plane *exynos_plane, unsigned int index,
		      unsigned long possible_crtcs,
		      const struct exynos_drm_plane_config *config);
