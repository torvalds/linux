/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ARMADA_DRM_H
#define ARMADA_DRM_H

#include <linux/kfifo.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <drm/drmP.h>

struct armada_crtc;
struct armada_gem_object;
struct clk;
struct drm_fb_helper;

static inline void
armada_updatel(uint32_t val, uint32_t mask, void __iomem *ptr)
{
	uint32_t ov, v;

	ov = v = readl_relaxed(ptr);
	v = (v & ~mask) | val;
	if (ov != v)
		writel_relaxed(v, ptr);
}

static inline uint32_t armada_pitch(uint32_t width, uint32_t bpp)
{
	uint32_t pitch = bpp != 4 ? width * ((bpp + 7) / 8) : width / 2;

	/* 88AP510 spec recommends pitch be a multiple of 128 */
	return ALIGN(pitch, 128);
}


struct armada_private;

struct armada_variant {
	bool has_spu_adv_reg;
	uint32_t spu_adv_reg;
	int (*init)(struct armada_crtc *, struct device *);
	int (*compute_clock)(struct armada_crtc *,
			     const struct drm_display_mode *,
			     uint32_t *);
};

/* Variant ops */
extern const struct armada_variant armada510_ops;

struct armada_private {
	struct drm_device	drm;
	struct work_struct	fb_unref_work;
	DECLARE_KFIFO(fb_unref, struct drm_framebuffer *, 8);
	struct drm_fb_helper	*fbdev;
	struct armada_crtc	*dcrtc[2];
	struct drm_mm		linear; /* protected by linear_lock */
	struct mutex		linear_lock;
	struct drm_property	*csc_yuv_prop;
	struct drm_property	*csc_rgb_prop;
	struct drm_property	*colorkey_prop;
	struct drm_property	*colorkey_min_prop;
	struct drm_property	*colorkey_max_prop;
	struct drm_property	*colorkey_val_prop;
	struct drm_property	*colorkey_alpha_prop;
	struct drm_property	*colorkey_mode_prop;
	struct drm_property	*brightness_prop;
	struct drm_property	*contrast_prop;
	struct drm_property	*saturation_prop;
#ifdef CONFIG_DEBUG_FS
	struct dentry		*de;
#endif
};

void __armada_drm_queue_unref_work(struct drm_device *,
	struct drm_framebuffer *);
void armada_drm_queue_unref_work(struct drm_device *,
	struct drm_framebuffer *);

extern const struct drm_mode_config_funcs armada_drm_mode_config_funcs;

int armada_fbdev_init(struct drm_device *);
void armada_fbdev_lastclose(struct drm_device *);
void armada_fbdev_fini(struct drm_device *);

int armada_overlay_plane_create(struct drm_device *, unsigned long);

int armada_drm_debugfs_init(struct drm_minor *);
void armada_drm_debugfs_cleanup(struct drm_minor *);

#endif
