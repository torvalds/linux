/*
 * drivers/staging/omapdrm/omap_debugfs.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "omap_drv.h"
#include "omap_dmm_tiler.h"

#ifdef CONFIG_DEBUG_FS

static struct drm_info_list omap_debugfs_list[] = {
	{"tiler_map", tiler_map_show, 0},
};

int omap_debugfs_init(struct drm_minor *minor)
{
	return drm_debugfs_create_files(omap_debugfs_list,
			ARRAY_SIZE(omap_debugfs_list),
			minor->debugfs_root, minor);
}

void omap_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(omap_debugfs_list,
			ARRAY_SIZE(omap_debugfs_list), minor);
}

#endif
