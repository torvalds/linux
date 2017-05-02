/*
 * Copyright (c) 2016 Linaro Limited.
 * Copyright (c) 2014-2016 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __KIRIN_DRM_DRV_H__
#define __KIRIN_DRM_DRV_H__

#define MAX_CRTC	2

/* display controller init/cleanup ops */
struct kirin_dc_ops {
	int (*init)(struct platform_device *pdev);
	void (*cleanup)(struct platform_device *pdev);
};

struct kirin_drm_private {
#ifdef CONFIG_DRM_FBDEV_EMULATION
	struct drm_fbdev_cma *fbdev;
#endif
};

extern const struct kirin_dc_ops ade_dc_ops;

#endif /* __KIRIN_DRM_DRV_H__ */
