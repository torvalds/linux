/* exynos_drm_drv.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_DRM_DRV_H_
#define _EXYNOS_DRM_DRV_H_

#include <drm/drmP.h>
#include <linux/module.h>

#define MAX_CRTC	3
#define MAX_PLANE	5
#define MAX_FB_BUFFER	4

#define DEFAULT_WIN	0

#define to_exynos_crtc(x)	container_of(x, struct exynos_drm_crtc, base)
#define to_exynos_plane(x)	container_of(x, struct exynos_drm_plane, base)

/* this enumerates display type. */
enum exynos_drm_output_type {
	EXYNOS_DISPLAY_TYPE_NONE,
	/* RGB or CPU Interface. */
	EXYNOS_DISPLAY_TYPE_LCD,
	/* HDMI Interface. */
	EXYNOS_DISPLAY_TYPE_HDMI,
	/* Virtual Display Interface. */
	EXYNOS_DISPLAY_TYPE_VIDI,
};

struct exynos_drm_rect {
	unsigned int x, y;
	unsigned int w, h;
};

/*
 * Exynos drm plane state structure.
 *
 * @base: plane_state object (contains drm_framebuffer pointer)
 * @src: rectangle of the source image data to be displayed (clipped to
 *       visible part).
 * @crtc: rectangle of the target image position on hardware screen
 *       (clipped to visible part).
 * @h_ratio: horizontal scaling ratio, 16.16 fixed point
 * @v_ratio: vertical scaling ratio, 16.16 fixed point
 *
 * this structure consists plane state data that will be applied to hardware
 * specific overlay info.
 */

struct exynos_drm_plane_state {
	struct drm_plane_state base;
	struct exynos_drm_rect crtc;
	struct exynos_drm_rect src;
	unsigned int h_ratio;
	unsigned int v_ratio;
};

static inline struct exynos_drm_plane_state *
to_exynos_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct exynos_drm_plane_state, base);
}

/*
 * Exynos drm common overlay structure.
 *
 * @base: plane object
 * @index: hardware index of the overlay layer
 *
 * this structure is common to exynos SoC and its contents would be copied
 * to hardware specific overlay info.
 */

struct exynos_drm_plane {
	struct drm_plane base;
	const struct exynos_drm_plane_config *config;
	unsigned int index;
};

#define EXYNOS_DRM_PLANE_CAP_DOUBLE	(1 << 0)
#define EXYNOS_DRM_PLANE_CAP_SCALE	(1 << 1)
#define EXYNOS_DRM_PLANE_CAP_ZPOS	(1 << 2)
#define EXYNOS_DRM_PLANE_CAP_TILE	(1 << 3)
#define EXYNOS_DRM_PLANE_CAP_PIX_BLEND	(1 << 4)
#define EXYNOS_DRM_PLANE_CAP_WIN_BLEND	(1 << 5)

/*
 * Exynos DRM plane configuration structure.
 *
 * @zpos: initial z-position of the plane.
 * @type: type of the plane (primary, cursor or overlay).
 * @pixel_formats: supported pixel formats.
 * @num_pixel_formats: number of elements in 'pixel_formats'.
 * @capabilities: supported features (see EXYNOS_DRM_PLANE_CAP_*)
 */

struct exynos_drm_plane_config {
	unsigned int zpos;
	enum drm_plane_type type;
	const uint32_t *pixel_formats;
	unsigned int num_pixel_formats;
	unsigned int capabilities;
};

/*
 * Exynos drm crtc ops
 *
 * @enable: enable the device
 * @disable: disable the device
 * @enable_vblank: specific driver callback for enabling vblank interrupt.
 * @disable_vblank: specific driver callback for disabling vblank interrupt.
 * @mode_valid: specific driver callback for mode validation
 * @atomic_check: validate state
 * @atomic_begin: prepare device to receive an update
 * @atomic_flush: mark the end of device update
 * @update_plane: apply hardware specific overlay data to registers.
 * @disable_plane: disable hardware specific overlay.
 * @te_handler: trigger to transfer video image at the tearing effect
 *	synchronization signal if there is a page flip request.
 */
struct exynos_drm_crtc;
struct exynos_drm_crtc_ops {
	void (*enable)(struct exynos_drm_crtc *crtc);
	void (*disable)(struct exynos_drm_crtc *crtc);
	int (*enable_vblank)(struct exynos_drm_crtc *crtc);
	void (*disable_vblank)(struct exynos_drm_crtc *crtc);
	enum drm_mode_status (*mode_valid)(struct exynos_drm_crtc *crtc,
		const struct drm_display_mode *mode);
	bool (*mode_fixup)(struct exynos_drm_crtc *crtc,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
	int (*atomic_check)(struct exynos_drm_crtc *crtc,
			    struct drm_crtc_state *state);
	void (*atomic_begin)(struct exynos_drm_crtc *crtc);
	void (*update_plane)(struct exynos_drm_crtc *crtc,
			     struct exynos_drm_plane *plane);
	void (*disable_plane)(struct exynos_drm_crtc *crtc,
			      struct exynos_drm_plane *plane);
	void (*atomic_flush)(struct exynos_drm_crtc *crtc);
	void (*te_handler)(struct exynos_drm_crtc *crtc);
};

struct exynos_drm_clk {
	void (*enable)(struct exynos_drm_clk *clk, bool enable);
};

/*
 * Exynos specific crtc structure.
 *
 * @base: crtc object.
 * @type: one of EXYNOS_DISPLAY_TYPE_LCD and HDMI.
 * @ops: pointer to callbacks for exynos drm specific functionality
 * @ctx: A pointer to the crtc's implementation specific context
 * @pipe_clk: A pointer to the crtc's pipeline clock.
 */
struct exynos_drm_crtc {
	struct drm_crtc			base;
	enum exynos_drm_output_type	type;
	const struct exynos_drm_crtc_ops	*ops;
	void				*ctx;
	struct exynos_drm_clk		*pipe_clk;
	bool				i80_mode : 1;
};

static inline void exynos_drm_pipe_clk_enable(struct exynos_drm_crtc *crtc,
					      bool enable)
{
	if (crtc->pipe_clk)
		crtc->pipe_clk->enable(crtc->pipe_clk, enable);
}

struct drm_exynos_file_private {
	/* for g2d api */
	struct list_head	inuse_cmdlist;
	struct list_head	event_list;
	struct list_head	userptr_list;
};

/*
 * Exynos drm private structure.
 *
 * @pending: the crtcs that have pending updates to finish
 * @lock: protect access to @pending
 * @wait: wait an atomic commit to finish
 */
struct exynos_drm_private {
	struct drm_fb_helper *fb_helper;

	struct device *g2d_dev;
	struct device *dma_dev;
	void *mapping;

	/* for atomic commit */
	u32			pending;
	spinlock_t		lock;
	wait_queue_head_t	wait;
};

static inline struct device *to_dma_dev(struct drm_device *dev)
{
	struct exynos_drm_private *priv = dev->dev_private;

	return priv->dma_dev;
}

static inline bool is_drm_iommu_supported(struct drm_device *drm_dev)
{
	struct exynos_drm_private *priv = drm_dev->dev_private;

	return priv->mapping ? true : false;
}

int exynos_drm_register_dma(struct drm_device *drm, struct device *dev);
void exynos_drm_unregister_dma(struct drm_device *drm, struct device *dev);
void exynos_drm_cleanup_dma(struct drm_device *drm);

#ifdef CONFIG_DRM_EXYNOS_DPI
struct drm_encoder *exynos_dpi_probe(struct device *dev);
int exynos_dpi_remove(struct drm_encoder *encoder);
int exynos_dpi_bind(struct drm_device *dev, struct drm_encoder *encoder);
#else
static inline struct drm_encoder *
exynos_dpi_probe(struct device *dev) { return NULL; }
static inline int exynos_dpi_remove(struct drm_encoder *encoder)
{
	return 0;
}
static inline int exynos_dpi_bind(struct drm_device *dev,
				  struct drm_encoder *encoder)
{
	return 0;
}
#endif

#ifdef CONFIG_DRM_EXYNOS_FIMC
int exynos_drm_check_fimc_device(struct device *dev);
#else
static inline int exynos_drm_check_fimc_device(struct device *dev)
{
	return 0;
}
#endif

int exynos_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state,
			 bool nonblock);


extern struct platform_driver fimd_driver;
extern struct platform_driver exynos5433_decon_driver;
extern struct platform_driver decon_driver;
extern struct platform_driver dp_driver;
extern struct platform_driver dsi_driver;
extern struct platform_driver mixer_driver;
extern struct platform_driver hdmi_driver;
extern struct platform_driver vidi_driver;
extern struct platform_driver g2d_driver;
extern struct platform_driver fimc_driver;
extern struct platform_driver rotator_driver;
extern struct platform_driver scaler_driver;
extern struct platform_driver gsc_driver;
extern struct platform_driver mic_driver;
#endif
