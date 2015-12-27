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

/*
 * Exynos drm common overlay structure.
 *
 * @base: plane object
 * @src_x: offset x on a framebuffer to be displayed.
 *	- the unit is screen coordinates.
 * @src_y: offset y on a framebuffer to be displayed.
 *	- the unit is screen coordinates.
 * @src_w: width of a partial image to be displayed from framebuffer.
 * @src_h: height of a partial image to be displayed from framebuffer.
 * @crtc_x: offset x on hardware screen.
 * @crtc_y: offset y on hardware screen.
 * @crtc_w: window width to be displayed (hardware screen).
 * @crtc_h: window height to be displayed (hardware screen).
 * @h_ratio: horizontal scaling ratio, 16.16 fixed point
 * @v_ratio: vertical scaling ratio, 16.16 fixed point
 * @dma_addr: array of bus(accessed by dma) address to the memory region
 *	      allocated for a overlay.
 * @zpos: order of overlay layer(z position).
 *
 * this structure is common to exynos SoC and its contents would be copied
 * to hardware specific overlay info.
 */

struct exynos_drm_plane {
	struct drm_plane base;
	unsigned int src_x;
	unsigned int src_y;
	unsigned int src_w;
	unsigned int src_h;
	unsigned int crtc_x;
	unsigned int crtc_y;
	unsigned int crtc_w;
	unsigned int crtc_h;
	unsigned int h_ratio;
	unsigned int v_ratio;
	dma_addr_t dma_addr[MAX_FB_BUFFER];
	unsigned int zpos;
	struct drm_framebuffer *pending_fb;
};

/*
 * Exynos drm crtc ops
 *
 * @enable: enable the device
 * @disable: disable the device
 * @commit: set current hw specific display mode to hw.
 * @enable_vblank: specific driver callback for enabling vblank interrupt.
 * @disable_vblank: specific driver callback for disabling vblank interrupt.
 * @wait_for_vblank: wait for vblank interrupt to make sure that
 *	hardware overlay is updated.
 * @atomic_check: validate state
 * @atomic_begin: prepare a window to receive a update
 * @atomic_flush: mark the end of a window update
 * @update_plane: apply hardware specific overlay data to registers.
 * @disable_plane: disable hardware specific overlay.
 * @te_handler: trigger to transfer video image at the tearing effect
 *	synchronization signal if there is a page flip request.
 * @clock_enable: optional function enabling/disabling display domain clock,
 *	called from exynos-dp driver before powering up (with
 *	'enable' argument as true) and after powering down (with
 *	'enable' as false).
 */
struct exynos_drm_crtc;
struct exynos_drm_crtc_ops {
	void (*enable)(struct exynos_drm_crtc *crtc);
	void (*disable)(struct exynos_drm_crtc *crtc);
	void (*commit)(struct exynos_drm_crtc *crtc);
	int (*enable_vblank)(struct exynos_drm_crtc *crtc);
	void (*disable_vblank)(struct exynos_drm_crtc *crtc);
	void (*wait_for_vblank)(struct exynos_drm_crtc *crtc);
	int (*atomic_check)(struct exynos_drm_crtc *crtc,
			    struct drm_crtc_state *state);
	void (*atomic_begin)(struct exynos_drm_crtc *crtc,
			      struct exynos_drm_plane *plane);
	void (*update_plane)(struct exynos_drm_crtc *crtc,
			     struct exynos_drm_plane *plane);
	void (*disable_plane)(struct exynos_drm_crtc *crtc,
			      struct exynos_drm_plane *plane);
	void (*atomic_flush)(struct exynos_drm_crtc *crtc,
			      struct exynos_drm_plane *plane);
	void (*te_handler)(struct exynos_drm_crtc *crtc);
	void (*clock_enable)(struct exynos_drm_crtc *crtc, bool enable);
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
 * @enabled: if the crtc is enabled or not
 * @event: vblank event that is currently queued for flip
 * @wait_update: wait all pending planes updates to finish
 * @pending_update: number of pending plane updates in this crtc
 * @ops: pointer to callbacks for exynos drm specific functionality
 * @ctx: A pointer to the crtc's implementation specific context
 */
struct exynos_drm_crtc {
	struct drm_crtc			base;
	enum exynos_drm_output_type	type;
	unsigned int			pipe;
	struct drm_pending_vblank_event	*event;
	wait_queue_head_t		wait_update;
	atomic_t			pending_update;
	const struct exynos_drm_crtc_ops	*ops;
	void				*ctx;
};

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

	/*
	 * created crtc object would be contained at this array and
	 * this array is used to be aware of which crtc did it request vblank.
	 */
	struct drm_crtc *crtc[MAX_CRTC];
	struct drm_property *plane_zpos_property;

	unsigned long da_start;
	unsigned long da_space_size;

	unsigned int pipe;

	/* for atomic commit */
	u32			pending;
	spinlock_t		lock;
	wait_queue_head_t	wait;
};

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
			 bool async);


extern struct platform_driver fimd_driver;
extern struct platform_driver exynos5433_decon_driver;
extern struct platform_driver decon_driver;
extern struct platform_driver dp_driver;
extern struct platform_driver dsi_driver;
extern struct platform_driver mixer_driver;
extern struct platform_driver hdmi_driver;
extern struct platform_driver exynos_drm_common_hdmi_driver;
extern struct platform_driver vidi_driver;
extern struct platform_driver g2d_driver;
extern struct platform_driver fimc_driver;
extern struct platform_driver rotator_driver;
extern struct platform_driver gsc_driver;
extern struct platform_driver ipp_driver;
extern struct platform_driver mic_driver;
#endif
