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
/* Defaulting to max width as defined on AM335x */
#define TILCDC_DEFAULT_MAX_WIDTH  2048
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

	/* Supported pixel formats */
	const uint32_t *pixelformats;
	uint32_t num_pixelformats;

#ifdef CONFIG_CPU_FREQ
	struct notifier_block freq_transition;
#endif

	struct workqueue_struct *wq;

	struct drm_crtc *crtc;

	unsigned int num_encoders;
	struct drm_encoder *encoders[8];

	unsigned int num_connectors;
	struct drm_connector *connectors[8];

	struct drm_encoder *external_encoder;
	struct drm_connector *external_connector;
	const struct drm_connector_helper_funcs *connector_funcs;

	bool is_registered;
	bool is_componentized;
};

/* Sub-module for display.  Since we don't know at compile time what panels
 * or display adapter(s) might be present (for ex, off chip dvi/tfp410,
 * hdmi encoder, various lcd panels), the connector/encoder(s) are split into
 * separate drivers.  If they are probed and found to be present, they
 * register themselves with tilcdc_register_module().
 */
struct tilcdc_module;

struct tilcdc_module_ops {
	/* create appropriate encoders/connectors: */
	int (*modeset_init)(struct tilcdc_module *mod, struct drm_device *dev);
#ifdef CONFIG_DEBUG_FS
	/* create debugfs nodes (can be NULL): */
	int (*debugfs_init)(struct tilcdc_module *mod, struct drm_minor *minor);
#endif
};

struct tilcdc_module {
	const char *name;
	struct list_head list;
	const struct tilcdc_module_ops *funcs;
};

void tilcdc_module_init(struct tilcdc_module *mod, const char *name,
		const struct tilcdc_module_ops *funcs);
void tilcdc_module_cleanup(struct tilcdc_module *mod);

/* Panel config that needs to be set in the crtc, but is not coming from
 * the mode timings.  The display module is expected to call
 * tilcdc_crtc_set_panel_info() to set this during modeset.
 */
struct tilcdc_panel_info {

	/* AC Bias Pin Frequency */
	uint32_t ac_bias;

	/* AC Bias Pin Transitions per Interrupt */
	uint32_t ac_bias_intrpt;

	/* DMA burst size */
	uint32_t dma_burst_sz;

	/* Bits per pixel */
	uint32_t bpp;

	/* FIFO DMA Request Delay */
	uint32_t fdd;

	/* TFT Alternative Signal Mapping (Only for active) */
	bool tft_alt_mode;

	/* Invert pixel clock */
	bool invert_pxl_clk;

	/* Horizontal and Vertical Sync Edge: 0=rising 1=falling */
	uint32_t sync_edge;

	/* Horizontal and Vertical Sync: Control: 0=ignore */
	uint32_t sync_ctrl;

	/* Raster Data Order Select: 1=Most-to-least 0=Least-to-most */
	uint32_t raster_order;

	/* DMA FIFO threshold */
	uint32_t fifo_th;
};

#define DBG(fmt, ...) DRM_DEBUG(fmt"\n", ##__VA_ARGS__)

int tilcdc_crtc_create(struct drm_device *dev);
irqreturn_t tilcdc_crtc_irq(struct drm_crtc *crtc);
void tilcdc_crtc_update_clk(struct drm_crtc *crtc);
void tilcdc_crtc_set_panel_info(struct drm_crtc *crtc,
		const struct tilcdc_panel_info *info);
void tilcdc_crtc_set_simulate_vesa_sync(struct drm_crtc *crtc,
					bool simulate_vesa_sync);
int tilcdc_crtc_mode_valid(struct drm_crtc *crtc, struct drm_display_mode *mode);
int tilcdc_crtc_max_width(struct drm_crtc *crtc);
void tilcdc_crtc_shutdown(struct drm_crtc *crtc);
int tilcdc_crtc_update_fb(struct drm_crtc *crtc,
		struct drm_framebuffer *fb,
		struct drm_pending_vblank_event *event);

int tilcdc_plane_init(struct drm_device *dev, struct drm_plane *plane);

#endif /* __TILCDC_DRV_H__ */
