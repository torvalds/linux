/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Linaro Limited.
 * Copyright (c) 2014-2016 Hisilicon Limited.
 */

#ifndef __KIRIN_DRM_DRV_H__
#define __KIRIN_DRM_DRV_H__

#define MAX_CRTC	2

/* display controller init/cleanup ops */
struct kirin_dc_ops {
	int (*init)(struct platform_device *pdev);
	void (*cleanup)(struct platform_device *pdev);
};

extern const struct kirin_dc_ops ade_dc_ops;

#endif /* __KIRIN_DRM_DRV_H__ */
