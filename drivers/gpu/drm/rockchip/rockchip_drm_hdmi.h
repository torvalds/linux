/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_hdmi.h
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _ROCKCHIP_DRM_HDMI_H_
#define _ROCKCHIP_DRM_HDMI_H_

#define MIXER_WIN_NR		3
#define MIXER_DEFAULT_WIN	0

/*
 * rockchip hdmi common context structure.
 *
 * @drm_dev: pointer to drm_device.
 * @ctx: pointer to the context of specific device driver.
 *	this context should be hdmi_context or mixer_context.
 */
struct rockchip_drm_hdmi_context {
	struct drm_device	*drm_dev;
	void			*ctx;
};

struct rockchip_hdmi_ops {
	/* display */
	bool (*is_connected)(void *ctx);
	struct edid *(*get_edid)(void *ctx,
			struct drm_connector *connector);
	int (*check_timing)(void *ctx, struct fb_videomode *timing);
	int (*power_on)(void *ctx, int mode);

	/* manager */
	void (*mode_set)(void *ctx, void *mode);
	void (*get_max_resol)(void *ctx, unsigned int *width,
				unsigned int *height);
	void (*commit)(void *ctx);
	void (*dpms)(void *ctx, int mode);
};

struct rockchip_mixer_ops {
	/* manager */
	int (*iommu_on)(void *ctx, bool enable);
	int (*enable_vblank)(void *ctx, int pipe);
	void (*disable_vblank)(void *ctx);
	void (*wait_for_vblank)(void *ctx);
	void (*dpms)(void *ctx, int mode);

	/* overlay */
	void (*win_mode_set)(void *ctx, struct rockchip_drm_overlay *overlay);
	void (*win_commit)(void *ctx, int zpos);
	void (*win_disable)(void *ctx, int zpos);

	/* display */
	int (*check_timing)(void *ctx, struct fb_videomode *timing);
};

void rockchip_hdmi_drv_attach(struct rockchip_drm_hdmi_context *ctx);
void rockchip_mixer_drv_attach(struct rockchip_drm_hdmi_context *ctx);
void rockchip_hdmi_ops_register(struct rockchip_hdmi_ops *ops);
void rockchip_mixer_ops_register(struct rockchip_mixer_ops *ops);
#endif
