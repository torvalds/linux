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
 * @commit: set current hw specific display mode to hw.
 * @enable_vblank: specific driver callback for enabling vblank interrupt.
 * @disable_vblank: specific driver callback for disabling vblank interrupt.
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
	void (*commit)(struct exynos_drm_crtc *crtc);
	int (*enable_vblank)(struct exynos_drm_crtc *crtc);
	void (*disable_vblank)(struct exynos_drm_crtc *crtc);
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
 * @pipe: a crtc index created at load() with a new crtc object creation
 *	and the crtc object would be set to private->crtc array
 *	to get a crtc object corresponding to this pipe from private->crtc
 *	array when irq interrupt occurred. the reason of using this pipe is that
 *	drm framework doesn't support multiple irq yet.
 *	we can refer to the crtc to current hardware interrupt occurred through
 *	this pipe value.
 * @ops: pointer to callbacks for exynos drm specific functionality
 * @ctx: A pointer to the crtc's implementation specific context
 * @pipe_clk: A pointer to the crtc's pipeline clock.
 */
struct exynos_drm_crtc {
	struct drm_crtc			base;
	enum exynos_drm_output_type	type;
	unsigned int			pipe;
	const struct exynos_drm_crtc_ops	*ops;
	void				*ctx;
	struct exynos_drm_clk		*pipe_clk;
};

static inline void exynos_drm_pipe_clk_enable(struct exynos_drm_crtc *crtc,
					      bool enable)
{
	if (crtc->pipe_clk)
		crtc->pipe_clk->enable(crtc->pipe_clk, enable);
}

struct exynos_drm_g2d_private {
	struct device		*dev;
	struct list_head	inuse_cmdlist;
	struct list_head	event_list;
	struct list_head	userptr_list;
};

struct drm_exynos_file_private {
	struct exynos_drm_g2d_private	*g2d_priv;
	struct device			*ipp_dev;
};

/*
 * Exynos drm private structure.
 *
 * @da_start: start address to device address space.
 *	with iommu, device address space starts from this address
 *	otherwise default one.
 * @da_space_size: size of device address space.
 *	if 0 then default value is used for it.
 * @pipe: the pipe number for this crtc/manager.
 * @pending: the crtcs that have pending updates to finish
 * @lock: protect access to @pending
 * @wait: wait an atomic commit to finish
 */
struct exynos_drm_private {
	struct drm_fb_helper *fb_helper;

	struct device *dma_dev;
	void *mapping;

	unsigned int pipe;

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

/*
 * Exynos drm sub driver structure.
 *
 * @list: sub driver has its own list object to register to exynos drm driver.
 * @dev: pointer to device object for subdrv device driver.
 * @drm_dev: pointer to drm_device and this pointer would be set
 *	when sub driver calls exynos_drm_subdrv_register().
 * @probe: this callback would be called by exynos drm driver after
 *     subdrv is registered to it.
 * @remove: this callback is used to release resources created
 *     by probe callback.
 * @open: this would be called with drm device file open.
 * @close: this would be called with drm device file close.
 */
struct exynos_drm_subdrv {
	struct list_head list;
	struct device *dev;
	struct drm_device *drm_dev;

	int (*probe)(struct drm_device *drm_dev, struct device *dev);
	void (*remove)(struct drm_device *drm_dev, struct device *dev);
	int (*open)(struct drm_device *drm_dev, struct device *dev,
			struct drm_file *file);
	void (*close)(struct drm_device *drm_dev, struct device *dev,
			struct drm_file *file);
};

 /* This function would be called by non kms drivers such as g2d and ipp. */
int exynos_drm_subdrv_register(struct exynos_drm_subdrv *drm_subdrv);

/* this function removes subdrv list from exynos drm driver */
int exynos_drm_subdrv_unregister(struct exynos_drm_subdrv *drm_subdrv);

int exynos_drm_device_subdrv_probe(struct drm_device *dev);
int exynos_drm_device_subdrv_remove(struct drm_device *dev);
int exynos_drm_subdrv_open(struct drm_device *dev, struct drm_file *file);
void exynos_drm_subdrv_close(struct drm_device *dev, struct drm_file *file);

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

int exynos_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state,
			 bool nonblock);
int exynos_atomic_check(struct drm_device *dev, struct drm_atomic_state *state);


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
extern struct platform_driver gsc_driver;
extern struct platform_driver ipp_driver;
extern struct platform_driver mic_driver;
#endif
