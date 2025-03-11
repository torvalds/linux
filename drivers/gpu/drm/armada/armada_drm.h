/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Russell King
 */
#ifndef ARMADA_DRM_H
#define ARMADA_DRM_H

#include <linux/kfifo.h>
#include <linux/io.h>
#include <linux/workqueue.h>

#include <drm/drm_device.h>
#include <drm/drm_mm.h>

struct armada_crtc;
struct armada_gem_object;
struct clk;
struct drm_display_mode;
struct drm_fb_helper;
struct drm_fb_helper_surface_size;

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
	int (*init)(struct armada_crtc *, struct device *);
	int (*compute_clock)(struct armada_crtc *,
			     const struct drm_display_mode *,
			     uint32_t *);
	void (*disable)(struct armada_crtc *);
	void (*enable)(struct armada_crtc *, const struct drm_display_mode *);
};

/* Variant ops */
extern const struct armada_variant armada510_ops;

struct armada_private {
	struct drm_device	drm;
	struct armada_crtc	*dcrtc[2];
	struct drm_mm		linear; /* protected by linear_lock */
	struct mutex		linear_lock;
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

#define drm_to_armada_dev(dev) container_of(dev, struct armada_private, drm)

#if defined(CONFIG_DRM_FBDEV_EMULATION)
int armada_fbdev_driver_fbdev_probe(struct drm_fb_helper *fbh,
				    struct drm_fb_helper_surface_size *sizes);
#define ARMADA_FBDEV_DRIVER_OPS \
	.fbdev_probe = armada_fbdev_driver_fbdev_probe
#else
#define ARMADA_FBDEV_DRIVER_OPS \
	.fbdev_probe = NULL
#endif

int armada_overlay_plane_create(struct drm_device *, unsigned long);

void armada_drm_crtc_debugfs_init(struct armada_crtc *dcrtc);
int armada_drm_debugfs_init(struct drm_minor *);

#endif
