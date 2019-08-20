/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Linaro Limited.
 * Copyright (c) 2014-2016 Hisilicon Limited.
 */

#ifndef __KIRIN_DRM_DRV_H__
#define __KIRIN_DRM_DRV_H__

#define MAX_CRTC	2

#define to_kirin_crtc(crtc) \
	container_of(crtc, struct kirin_crtc, base)

#define to_kirin_plane(plane) \
	container_of(plane, struct kirin_plane, base)

/* kirin-format translate table */
struct kirin_format {
	u32 pixel_format;
	u32 hw_format;
};

struct kirin_crtc {
	struct drm_crtc base;
	void *hw_ctx;
	bool enable;
};

struct kirin_plane {
	struct drm_plane base;
	void *hw_ctx;
	u32 ch;
};

/* display controller init/cleanup ops */
struct kirin_dc_ops {
	int (*init)(struct platform_device *pdev);
	void (*cleanup)(struct platform_device *pdev);
};

extern const struct kirin_dc_ops ade_dc_ops;

#endif /* __KIRIN_DRM_DRV_H__ */
