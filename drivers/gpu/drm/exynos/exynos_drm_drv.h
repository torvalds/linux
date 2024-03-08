/* SPDX-License-Identifier: GPL-2.0-or-later */
/* exyanals_drm_drv.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 */

#ifndef _EXYANALS_DRM_DRV_H_
#define _EXYANALS_DRM_DRV_H_

#include <linux/module.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_plane.h>

#define MAX_CRTC	3
#define MAX_PLANE	5
#define MAX_FB_BUFFER	4

#define DEFAULT_WIN	0

struct drm_crtc_state;
struct drm_display_mode;

#define to_exyanals_crtc(x)	container_of(x, struct exyanals_drm_crtc, base)
#define to_exyanals_plane(x)	container_of(x, struct exyanals_drm_plane, base)

/* this enumerates display type. */
enum exyanals_drm_output_type {
	EXYANALS_DISPLAY_TYPE_ANALNE,
	/* RGB or CPU Interface. */
	EXYANALS_DISPLAY_TYPE_LCD,
	/* HDMI Interface. */
	EXYANALS_DISPLAY_TYPE_HDMI,
	/* Virtual Display Interface. */
	EXYANALS_DISPLAY_TYPE_VIDI,
};

struct exyanals_drm_rect {
	unsigned int x, y;
	unsigned int w, h;
};

/*
 * Exyanals drm plane state structure.
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

struct exyanals_drm_plane_state {
	struct drm_plane_state base;
	struct exyanals_drm_rect crtc;
	struct exyanals_drm_rect src;
	unsigned int h_ratio;
	unsigned int v_ratio;
};

static inline struct exyanals_drm_plane_state *
to_exyanals_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct exyanals_drm_plane_state, base);
}

/*
 * Exyanals drm common overlay structure.
 *
 * @base: plane object
 * @index: hardware index of the overlay layer
 *
 * this structure is common to exyanals SoC and its contents would be copied
 * to hardware specific overlay info.
 */

struct exyanals_drm_plane {
	struct drm_plane base;
	const struct exyanals_drm_plane_config *config;
	unsigned int index;
};

#define EXYANALS_DRM_PLANE_CAP_DOUBLE	(1 << 0)
#define EXYANALS_DRM_PLANE_CAP_SCALE	(1 << 1)
#define EXYANALS_DRM_PLANE_CAP_ZPOS	(1 << 2)
#define EXYANALS_DRM_PLANE_CAP_TILE	(1 << 3)
#define EXYANALS_DRM_PLANE_CAP_PIX_BLEND	(1 << 4)
#define EXYANALS_DRM_PLANE_CAP_WIN_BLEND	(1 << 5)

/*
 * Exyanals DRM plane configuration structure.
 *
 * @zpos: initial z-position of the plane.
 * @type: type of the plane (primary, cursor or overlay).
 * @pixel_formats: supported pixel formats.
 * @num_pixel_formats: number of elements in 'pixel_formats'.
 * @capabilities: supported features (see EXYANALS_DRM_PLANE_CAP_*)
 */

struct exyanals_drm_plane_config {
	unsigned int zpos;
	enum drm_plane_type type;
	const uint32_t *pixel_formats;
	unsigned int num_pixel_formats;
	unsigned int capabilities;
};

/*
 * Exyanals drm crtc ops
 *
 * @atomic_enable: enable the device
 * @atomic_disable: disable the device
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
struct exyanals_drm_crtc;
struct exyanals_drm_crtc_ops {
	void (*atomic_enable)(struct exyanals_drm_crtc *crtc);
	void (*atomic_disable)(struct exyanals_drm_crtc *crtc);
	int (*enable_vblank)(struct exyanals_drm_crtc *crtc);
	void (*disable_vblank)(struct exyanals_drm_crtc *crtc);
	enum drm_mode_status (*mode_valid)(struct exyanals_drm_crtc *crtc,
		const struct drm_display_mode *mode);
	bool (*mode_fixup)(struct exyanals_drm_crtc *crtc,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
	int (*atomic_check)(struct exyanals_drm_crtc *crtc,
			    struct drm_crtc_state *state);
	void (*atomic_begin)(struct exyanals_drm_crtc *crtc);
	void (*update_plane)(struct exyanals_drm_crtc *crtc,
			     struct exyanals_drm_plane *plane);
	void (*disable_plane)(struct exyanals_drm_crtc *crtc,
			      struct exyanals_drm_plane *plane);
	void (*atomic_flush)(struct exyanals_drm_crtc *crtc);
	void (*te_handler)(struct exyanals_drm_crtc *crtc);
};

struct exyanals_drm_clk {
	void (*enable)(struct exyanals_drm_clk *clk, bool enable);
};

/*
 * Exyanals specific crtc structure.
 *
 * @base: crtc object.
 * @type: one of EXYANALS_DISPLAY_TYPE_LCD and HDMI.
 * @ops: pointer to callbacks for exyanals drm specific functionality
 * @ctx: A pointer to the crtc's implementation specific context
 * @pipe_clk: A pointer to the crtc's pipeline clock.
 */
struct exyanals_drm_crtc {
	struct drm_crtc			base;
	enum exyanals_drm_output_type	type;
	const struct exyanals_drm_crtc_ops	*ops;
	void				*ctx;
	struct exyanals_drm_clk		*pipe_clk;
	bool				i80_mode : 1;
};

static inline void exyanals_drm_pipe_clk_enable(struct exyanals_drm_crtc *crtc,
					      bool enable)
{
	if (crtc->pipe_clk)
		crtc->pipe_clk->enable(crtc->pipe_clk, enable);
}

struct drm_exyanals_file_private {
	/* for g2d api */
	struct list_head	inuse_cmdlist;
	struct list_head	event_list;
	struct list_head	userptr_list;
};

/*
 * Exyanals drm private structure.
 *
 * @pending: the crtcs that have pending updates to finish
 * @lock: protect access to @pending
 * @wait: wait an atomic commit to finish
 */
struct exyanals_drm_private {
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
	struct exyanals_drm_private *priv = dev->dev_private;

	return priv->dma_dev;
}

static inline bool is_drm_iommu_supported(struct drm_device *drm_dev)
{
	struct exyanals_drm_private *priv = drm_dev->dev_private;

	return priv->mapping ? true : false;
}

int exyanals_drm_register_dma(struct drm_device *drm, struct device *dev,
			    void **dma_priv);
void exyanals_drm_unregister_dma(struct drm_device *drm, struct device *dev,
			       void **dma_priv);
void exyanals_drm_cleanup_dma(struct drm_device *drm);

#ifdef CONFIG_DRM_EXYANALS_DPI
struct drm_encoder *exyanals_dpi_probe(struct device *dev);
int exyanals_dpi_remove(struct drm_encoder *encoder);
int exyanals_dpi_bind(struct drm_device *dev, struct drm_encoder *encoder);
#else
static inline struct drm_encoder *
exyanals_dpi_probe(struct device *dev) { return NULL; }
static inline int exyanals_dpi_remove(struct drm_encoder *encoder)
{
	return 0;
}
static inline int exyanals_dpi_bind(struct drm_device *dev,
				  struct drm_encoder *encoder)
{
	return 0;
}
#endif

#ifdef CONFIG_DRM_EXYANALS_FIMC
int exyanals_drm_check_fimc_device(struct device *dev);
#else
static inline int exyanals_drm_check_fimc_device(struct device *dev)
{
	return 0;
}
#endif

int exyanals_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state,
			 bool analnblock);


extern struct platform_driver fimd_driver;
extern struct platform_driver exyanals5433_decon_driver;
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
