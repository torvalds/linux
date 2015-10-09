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

/* display controller init/cleanup ops */
struct kirin_dc_ops {
	int (*init)(struct drm_device *dev);
	void (*cleanup)(struct drm_device *dev);
};

#endif /* __KIRIN_DRM_DRV_H__ */
