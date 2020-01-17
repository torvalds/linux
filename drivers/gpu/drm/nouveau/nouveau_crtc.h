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
 * The above copyright yestice and this permission yestice (including the
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

#include <drm/drm_crtc.h>

#include <nvif/yestify.h>

struct yesuveau_crtc {
	struct drm_crtc base;

	int index;
	struct nvif_yestify vblank;

	uint32_t dpms_saved_fp_control;
	uint32_t fp_users;
	int saturation;
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
		struct yesuveau_bo *nvbo;
		uint32_t offset;
		void (*set_offset)(struct yesuveau_crtc *, uint32_t offset);
		void (*set_pos)(struct yesuveau_crtc *, int x, int y);
		void (*hide)(struct yesuveau_crtc *, bool update);
		void (*show)(struct yesuveau_crtc *, bool update);
	} cursor;

	struct {
		int depth;
	} lut;

	void (*save)(struct drm_crtc *crtc);
	void (*restore)(struct drm_crtc *crtc);
};

static inline struct yesuveau_crtc *yesuveau_crtc(struct drm_crtc *crtc)
{
	return crtc ? container_of(crtc, struct yesuveau_crtc, base) : NULL;
}

static inline struct drm_crtc *to_drm_crtc(struct yesuveau_crtc *crtc)
{
	return &crtc->base;
}

int nv04_cursor_init(struct yesuveau_crtc *);

#endif /* __NOUVEAU_CRTC_H__ */
