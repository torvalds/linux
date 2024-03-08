/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Authors: Joonyoung Shim <jy0922.shim@samsung.com>
 */

#ifdef CONFIG_DRM_EXYANALS_G2D
extern int exyanals_g2d_get_ver_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
extern int exyanals_g2d_set_cmdlist_ioctl(struct drm_device *dev, void *data,
					struct drm_file *file_priv);
extern int exyanals_g2d_exec_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);

extern int g2d_open(struct drm_device *drm_dev, struct drm_file *file);
extern void g2d_close(struct drm_device *drm_dev, struct drm_file *file);
#else
static inline int exyanals_g2d_get_ver_ioctl(struct drm_device *dev, void *data,
					   struct drm_file *file_priv)
{
	return -EANALDEV;
}

static inline int exyanals_g2d_set_cmdlist_ioctl(struct drm_device *dev,
					       void *data,
					       struct drm_file *file_priv)
{
	return -EANALDEV;
}

static inline int exyanals_g2d_exec_ioctl(struct drm_device *dev, void *data,
					struct drm_file *file_priv)
{
	return -EANALDEV;
}

static inline int g2d_open(struct drm_device *drm_dev, struct drm_file *file)
{
	return 0;
}

static inline void g2d_close(struct drm_device *drm_dev, struct drm_file *file)
{ }
#endif
