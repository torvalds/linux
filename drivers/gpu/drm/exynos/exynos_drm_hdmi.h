/* exynos_drm_hdmi.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authoer: Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_DRM_HDMI_H_
#define _EXYNOS_DRM_HDMI_H_

#define MIXER_WIN_NR		3
#define MIXER_DEFAULT_WIN	0

/*
 * exynos hdmi common context structure.
 *
 * @drm_dev: pointer to drm_device.
 * @pipe: pipe for mixer
 * @ctx: pointer to the context of specific device driver.
 *	this context should be hdmi_context or mixer_context.
 */
struct exynos_drm_hdmi_context {
	int			pipe;
	void			*ctx;
};

struct exynos_hdmi_ops {
	/* display */
	int (*initialize)(void *ctx, struct drm_device *drm_dev);
	bool (*is_connected)(void *ctx);
	struct edid *(*get_edid)(void *ctx,
			struct drm_connector *connector);
	int (*check_mode)(void *ctx, struct drm_display_mode *mode);
	void (*dpms)(void *ctx, int mode);

	/* manager */
	void (*mode_set)(void *ctx, struct drm_display_mode *mode);
	void (*get_max_resol)(void *ctx, unsigned int *width,
				unsigned int *height);
	void (*commit)(void *ctx);
};

struct exynos_mixer_ops {
	/* manager */
	int (*initialize)(void *ctx, struct drm_device *drm_dev);
	int (*iommu_on)(void *ctx, bool enable);
	int (*enable_vblank)(void *ctx, int pipe);
	void (*disable_vblank)(void *ctx);
	void (*wait_for_vblank)(void *ctx);
	void (*dpms)(void *ctx, int mode);

	/* overlay */
	void (*win_mode_set)(void *ctx, struct exynos_drm_overlay *overlay);
	void (*win_commit)(void *ctx, int zpos);
	void (*win_disable)(void *ctx, int zpos);

	/* display */
	int (*check_mode)(void *ctx, struct drm_display_mode *mode);
};

void exynos_hdmi_drv_attach(struct exynos_drm_hdmi_context *ctx);
void exynos_mixer_drv_attach(struct exynos_drm_hdmi_context *ctx);
void exynos_hdmi_ops_register(struct exynos_hdmi_ops *ops);
void exynos_mixer_ops_register(struct exynos_mixer_ops *ops);
#endif
