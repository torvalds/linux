/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __NOUVEAU_CRTC_H__
#define __NOUVEAU_CRTC_H__

#include <nvif/notify.h>

struct nouveau_crtc {
	struct drm_crtc base;

	int index;
	struct nvif_notify vblank;

	uint32_t dpms_saved_fp_control;
	uint32_t fp_users;
	int saturation;
	int color_vibrance;
	int vibrant_hue;
	int sharpness;
	int last_dpms;

	int cursor_saved_x, cursor_saved_y;

	struct {
		int cpp;
		bool blanked;
		uint32_t offset;
		uint32_t handle;
	} fb;

	struct {
		struct nouveau_bo *nvbo;
		bool visible;
		uint32_t offset;
		void (*set_offset)(struct nouveau_crtc *, uint32_t offset);
		void (*set_pos)(struct nouveau_crtc *, int x, int y);
		void (*hide)(struct nouveau_crtc *, bool update);
		void (*show)(struct nouveau_crtc *, bool update);
	} cursor;

	struct {
		struct nouveau_bo *nvbo;
		uint16_t r[256];
		uint16_t g[256];
		uint16_t b[256];
		int depth;
	} lut;

	int (*set_dither)(struct nouveau_crtc *crtc, bool update);
	int (*set_scale)(struct nouveau_crtc *crtc, bool update);
	int (*set_color_vibrance)(struct nouveau_crtc *crtc, bool update);

	void (*save)(struct drm_crtc *crtc);
	void (*restore)(struct drm_crtc *crtc);
};

static inline struct nouveau_crtc *nouveau_crtc(struct drm_crtc *crtc)
{
	return crtc ? container_of(crtc, struct nouveau_crtc, base) : NULL;
}

static inline struct drm_crtc *to_drm_crtc(struct nouveau_crtc *crtc)
{
	return &crtc->base;
}

int nv04_cursor_init(struct nouveau_crtc *);

#endif /* __NOUVEAU_CRTC_H__ */
