/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __TILCDC_DRV_H__
#define __TILCDC_DRV_H__

#include <linux/cpufreq.h>
#include <linux/irqreturn.h>

#include <drm/drm_print.h>

struct clk;
struct workqueue_struct;

struct drm_connector;
struct drm_connector_helper_funcs;
struct drm_crtc;
struct drm_device;
struct drm_display_mode;
struct drm_encoder;
struct drm_framebuffer;
struct drm_minor;
struct drm_pending_vblank_event;
struct drm_plane;

/* Defaulting to pixel clock defined on AM335x */
#define TILCDC_DEFAULT_MAX_PIXELCLOCK  126000
/* Maximum display width for LCDC V1 */
#define TILCDC_DEFAULT_MAX_WIDTH_V1  1024
/* ... and for LCDC V2 found on AM335x: */
#define TILCDC_DEFAULT_MAX_WIDTH_V2  2048
/*
 * This may need some tweaking, but want to allow at least 1280x1024@60
 * with optimized DDR & EMIF settings tweaked 1920x1080@24 appears to
 * be supportable
 */
#define TILCDC_DEFAULT_MAX_BANDWIDTH  (1280*1024*60)


struct tilcdc_drm_private {
	void __iomem *mmio;

	struct clk *clk;         /* functional clock */
	int rev;                 /* IP revision */

	unsigned int irq;

	struct drm_device ddev;

	/* don't attempt resolutions w/ higher W * H * Hz: */
	uint32_t max_bandwidth;
	/*
	 * Pixel Clock will be restricted to some value as
	 * defined in the device datasheet measured in KHz
	 */
	uint32_t max_pixelclock;
	/*
	 * Max allowable width is limited on a per device basis
	 * measured in pixels
	 */
	uint32_t max_width;

	u32 fifo_th;

	/* Supported pixel formats */
	const uint32_t *pixelformats;
	uint32_t num_pixelformats;

#ifdef CONFIG_CPU_FREQ
	struct notifier_block freq_transition;
#endif

	struct workqueue_struct *wq;

	struct drm_crtc *crtc;

	struct tilcdc_encoder *encoder;
	struct drm_connector *connector;

	bool irq_enabled;
};

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

#define ddev_to_tilcdc_priv(x) container_of(x, struct tilcdc_drm_private, ddev)

int tilcdc_crtc_create(struct drm_device *dev);
irqreturn_t tilcdc_crtc_irq(struct drm_crtc *crtc);
void tilcdc_crtc_update_clk(struct drm_crtc *crtc);
void tilcdc_crtc_shutdown(struct drm_crtc *crtc);
int tilcdc_crtc_update_fb(struct drm_crtc *crtc,
		struct drm_framebuffer *fb,
		struct drm_pending_vblank_event *event);

struct tilcdc_plane {
	struct drm_plane base;
};

struct tilcdc_encoder {
	struct drm_encoder base;
};

struct tilcdc_plane *tilcdc_plane_init(struct drm_device *dev);

#endif /* __TILCDC_DRV_H__ */
