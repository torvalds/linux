/* SPDX-License-Identifier: MIT */

#ifndef DRM_CLIENT_INTERNAL_H
#define DRM_CLIENT_INTERNAL_H

struct drm_device;
struct drm_format_info;

#ifdef CONFIG_DRM_FBDEV_EMULATION
int drm_fbdev_client_setup(struct drm_device *dev, const struct drm_format_info *format);
#else
static inline int drm_fbdev_client_setup(struct drm_device *dev,
					 const struct drm_format_info *format)
{
	return 0;
}
#endif

#ifdef CONFIG_DRM_CLIENT_LOG
void drm_log_register(struct drm_device *dev);
#else
static inline void drm_log_register(struct drm_device *dev) {}
#endif

#endif
