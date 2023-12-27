/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_CRTC_H__
#define __VS_CRTC_H__

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "vs_type.h"

struct vs_crtc_funcs {
	void (*enable)(struct device *dev, struct drm_crtc *crtc);
	void (*disable)(struct device *dev, struct drm_crtc *crtc);
	bool (*mode_fixup)(struct device *dev,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
	void (*set_gamma)(struct device *dev, struct drm_crtc *crtc,
			   struct drm_color_lut *lut, unsigned int size);
	void (*enable_gamma)(struct device *dev, struct drm_crtc *crtc,
			   bool enable);
	void (*enable_vblank)(struct device *dev, bool enable, u32 ctrc_mask);
	void (*commit)(struct device *dev);
};

struct vs_crtc_state {
	struct drm_crtc_state base;

	u32 sync_mode;
	u32 output_fmt;
	u32 bg_color;
	u8 encoder_type;
	u8 mmu_prefetch;
	u8 bpp;
	bool sync_enable;
	bool dither_enable;
	bool underflow;
};

struct vs_crtc {
	struct drm_crtc base;
	struct device *dev;
	struct drm_pending_vblank_event *event;
	unsigned int max_bpc;
	unsigned int color_formats; /* supported color format */

	struct drm_property *sync_mode;
	struct drm_property *mmu_prefetch;
	struct drm_property *bg_color;
	struct drm_property *panel_sync;
	struct drm_property *dither;

	const struct vs_crtc_funcs *funcs;
};

void vs_crtc_destroy(struct drm_crtc *crtc);

struct vs_crtc *vs_crtc_create(struct drm_device *drm_dev,
				   struct vs_dc_info *info);
void vs_crtc_handle_vblank(struct drm_crtc *crtc, bool underflow);

static inline struct vs_crtc *to_vs_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct vs_crtc, base);
}

static inline struct vs_crtc_state *
to_vs_crtc_state(struct drm_crtc_state *state)
{
	return container_of(state, struct vs_crtc_state, base);
}
#endif /* __VS_CRTC_H__ */
