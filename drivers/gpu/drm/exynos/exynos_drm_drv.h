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

#define to_exynos_crtc(x)	container_of(x, struct exynos_drm_crtc, base)
#define to_exynos_plane(x)	container_of(x, struct exynos_drm_plane, base)

/* This enumerates device type. */
enum exynos_drm_device_type {
	EXYNOS_DEVICE_TYPE_NONE,
	EXYNOS_DEVICE_TYPE_CRTC,
	EXYNOS_DEVICE_TYPE_CONNECTOR,
};

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
 * @src_width: width of a partial image to be displayed from framebuffer.
 * @src_height: height of a partial image to be displayed from framebuffer.
 * @fb_width: width of a framebuffer.
 * @fb_height: height of a framebuffer.
 * @crtc_x: offset x on hardware screen.
 * @crtc_y: offset y on hardware screen.
 * @crtc_width: window width to be displayed (hardware screen).
 * @crtc_height: window height to be displayed (hardware screen).
 * @mode_width: width of screen mode.
 * @mode_height: height of screen mode.
 * @h_ratio: horizontal scaling ratio, 16.16 fixed point
 * @v_ratio: vertical scaling ratio, 16.16 fixed point
 * @refresh: refresh rate.
 * @scan_flag: interlace or progressive way.
 *	(it could be DRM_MODE_FLAG_*)
 * @bpp: pixel size.(in bit)
 * @pixel_format: fourcc pixel format of this overlay
 * @dma_addr: array of bus(accessed by dma) address to the memory region
 *	      allocated for a overlay.
 * @zpos: order of overlay layer(z position).
 * @index_color: if using color key feature then this value would be used
 *			as index color.
 * @default_win: a window to be enabled.
 * @color_key: color key on or off.
 * @local_path: in case of lcd type, local path mode on or off.
 * @transparency: transparency on or off.
 * @activated: activated or not.
 * @enabled: enabled or not.
 * @resume: to resume or not.
 *
 * this structure is common to exynos SoC and its contents would be copied
 * to hardware specific overlay info.
 */

struct exynos_drm_plane {
	struct drm_plane base;
	unsigned int src_x;
	unsigned int src_y;
	unsigned int src_width;
	unsigned int src_height;
	unsigned int fb_width;
	unsigned int fb_height;
	unsigned int crtc_x;
	unsigned int crtc_y;
	unsigned int crtc_width;
	unsigned int crtc_height;
	unsigned int mode_width;
	unsigned int mode_height;
	unsigned int h_ratio;
	unsigned int v_ratio;
	unsigned int refresh;
	unsigned int scan_flag;
	unsigned int bpp;
	unsigned int pitch;
	uint32_t pixel_format;
	dma_addr_t dma_addr[MAX_FB_BUFFER];
	unsigned int zpos;
	unsigned int index_color;

	bool default_win:1;
	bool color_key:1;
	bool local_path:1;
	bool transparency:1;
	bool activated:1;
	bool enabled:1;
	bool resume:1;
};

/*
 * Exynos DRM Display Structure.
 *	- this structure is common to analog tv, digital tv and lcd panel.
 *
 * @create_connector: initialize and register a new connector
 * @remove: cleans up the display for removal
 * @mode_fixup: fix mode data comparing to hw specific display mode.
 * @mode_set: convert drm_display_mode to hw specific display mode and
 *	      would be called by encoder->mode_set().
 * @check_mode: check if mode is valid or not.
 * @dpms: display device on or off.
 * @commit: apply changes to hw
 */
struct exynos_drm_display;
struct exynos_drm_display_ops {
	int (*create_connector)(struct exynos_drm_display *display,
				struct drm_encoder *encoder);
	void (*remove)(struct exynos_drm_display *display);
	void (*mode_fixup)(struct exynos_drm_display *display,
				struct drm_connector *connector,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode);
	void (*mode_set)(struct exynos_drm_display *display,
				struct drm_display_mode *mode);
	int (*check_mode)(struct exynos_drm_display *display,
				struct drm_display_mode *mode);
	void (*dpms)(struct exynos_drm_display *display, int mode);
	void (*commit)(struct exynos_drm_display *display);
};

/*
 * Exynos drm display structure, maps 1:1 with an encoder/connector
 *
 * @list: the list entry for this manager
 * @type: one of EXYNOS_DISPLAY_TYPE_LCD and HDMI.
 * @encoder: encoder object this display maps to
 * @connector: connector object this display maps to
 * @ops: pointer to callbacks for exynos drm specific functionality
 * @ctx: A pointer to the display's implementation specific context
 */
struct exynos_drm_display {
	struct list_head list;
	enum exynos_drm_output_type type;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct exynos_drm_display_ops *ops;
};

/*
 * Exynos drm crtc ops
 *
 * @dpms: control device power.
 * @mode_fixup: fix mode data before applying it
 * @commit: set current hw specific display mode to hw.
 * @enable_vblank: specific driver callback for enabling vblank interrupt.
 * @disable_vblank: specific driver callback for disabling vblank interrupt.
 * @wait_for_vblank: wait for vblank interrupt to make sure that
 *	hardware overlay is updated.
 * @win_commit: apply hardware specific overlay data to registers.
 * @win_disable: disable hardware specific overlay.
 * @te_handler: trigger to transfer video image at the tearing effect
 *	synchronization signal if there is a page flip request.
 */
struct exynos_drm_crtc;
struct exynos_drm_crtc_ops {
	void (*dpms)(struct exynos_drm_crtc *crtc, int mode);
	bool (*mode_fixup)(struct exynos_drm_crtc *crtc,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode);
	void (*commit)(struct exynos_drm_crtc *crtc);
	int (*enable_vblank)(struct exynos_drm_crtc *crtc);
	void (*disable_vblank)(struct exynos_drm_crtc *crtc);
	void (*wait_for_vblank)(struct exynos_drm_crtc *crtc);
	void (*win_commit)(struct exynos_drm_crtc *crtc, unsigned int zpos);
	void (*win_disable)(struct exynos_drm_crtc *crtc, unsigned int zpos);
	void (*te_handler)(struct exynos_drm_crtc *crtc);
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
 * @dpms: store the crtc dpms value
 * @event: vblank event that is currently queued for flip
 * @ops: pointer to callbacks for exynos drm specific functionality
 * @ctx: A pointer to the crtc's implementation specific context
 */
struct exynos_drm_crtc {
	struct drm_crtc			base;
	enum exynos_drm_output_type	type;
	unsigned int			pipe;
	unsigned int			dpms;
	wait_queue_head_t		pending_flip_queue;
	struct drm_pending_vblank_event	*event;
	struct exynos_drm_crtc_ops	*ops;
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

#ifdef CONFIG_DRM_EXYNOS_IPP
int exynos_platform_device_ipp_register(void);
void exynos_platform_device_ipp_unregister(void);
#else
static inline int exynos_platform_device_ipp_register(void) { return 0; }
static inline void exynos_platform_device_ipp_unregister(void) {}
#endif


#ifdef CONFIG_DRM_EXYNOS_DPI
struct exynos_drm_display * exynos_dpi_probe(struct device *dev);
int exynos_dpi_remove(struct exynos_drm_display *display);
#else
static inline struct exynos_drm_display *
exynos_dpi_probe(struct device *dev) { return NULL; }
static inline int exynos_dpi_remove(struct exynos_drm_display *display)
{
	return 0;
}
#endif

#ifdef CONFIG_DRM_EXYNOS_VIDI
int exynos_drm_probe_vidi(void);
void exynos_drm_remove_vidi(void);
#else
static inline int exynos_drm_probe_vidi(void) { return 0; }
static inline void exynos_drm_remove_vidi(void) {}
#endif

/* This function creates a encoder and a connector, and initializes them. */
int exynos_drm_create_enc_conn(struct drm_device *dev,
				struct exynos_drm_display *display);

int exynos_drm_component_add(struct device *dev,
				enum exynos_drm_device_type dev_type,
				enum exynos_drm_output_type out_type);

void exynos_drm_component_del(struct device *dev,
				enum exynos_drm_device_type dev_type);

extern struct platform_driver fimd_driver;
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
#endif
