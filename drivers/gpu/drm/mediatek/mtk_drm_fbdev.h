/*
 * Copyright (c) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef MTK_DRM_FBDEV_H
#define MTK_DRM_FBDEV_H

#ifdef CONFIG_DRM_FBDEV_EMULATION
int mtk_fbdev_init(struct drm_device *dev);
void mtk_fbdev_fini(struct drm_device *dev);
#else
int mtk_fbdev_init(struct drm_device *dev)
{
	return 0;
}

void mtk_fbdev_fini(struct drm_device *dev)
{

}
#endif /* CONFIG_DRM_FBDEV_EMULATION */

#endif /* MTK_DRM_FBDEV_H */
