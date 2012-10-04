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

#ifndef __NV50_DISPLAY_H__
#define __NV50_DISPLAY_H__

#include "nouveau_display.h"
#include "nouveau_crtc.h"
#include "nouveau_reg.h"
#include "nv50_evo.h"

struct nv50_display_crtc {
	struct nouveau_channel *sync;
	struct {
		struct nouveau_bo *bo;
		u32 offset;
		u16 value;
	} sem;
};

struct nv50_display {
	struct nouveau_channel *master;

	struct nouveau_gpuobj *ramin;
	u32 dmao;
	u32 hash;

	struct nv50_display_crtc crtc[2];

	struct tasklet_struct tasklet;
	struct {
		struct dcb_output *dcb;
		u16 script;
		u32 pclk;
	} irq;
};

static inline struct nv50_display *
nv50_display(struct drm_device *dev)
{
	return nouveau_display(dev)->priv;
}

int nv50_display_early_init(struct drm_device *dev);
void nv50_display_late_takedown(struct drm_device *dev);
int nv50_display_create(struct drm_device *dev);
int nv50_display_init(struct drm_device *dev);
void nv50_display_fini(struct drm_device *dev);
void nv50_display_destroy(struct drm_device *dev);
void nv50_display_intr(struct drm_device *);
int nv50_crtc_blank(struct nouveau_crtc *, bool blank);
int nv50_crtc_set_clock(struct drm_device *, int head, int pclk);

u32  nv50_display_active_crtcs(struct drm_device *);

int  nv50_display_sync(struct drm_device *);
int  nv50_display_flip_next(struct drm_crtc *, struct drm_framebuffer *,
			    struct nouveau_channel *chan);
void nv50_display_flip_stop(struct drm_crtc *);

int  nv50_evo_create(struct drm_device *dev);
void nv50_evo_destroy(struct drm_device *dev);
int  nv50_evo_init(struct drm_device *dev);
void nv50_evo_fini(struct drm_device *dev);
void nv50_evo_dmaobj_init(struct nouveau_gpuobj *, u32 memtype, u64 base,
			  u64 size);
int  nv50_evo_dmaobj_new(struct nouveau_channel *, u32 handle, u32 memtype,
			 u64 base, u64 size, struct nouveau_gpuobj **);

int  nvd0_display_create(struct drm_device *);
void nvd0_display_destroy(struct drm_device *);
int  nvd0_display_init(struct drm_device *);
void nvd0_display_fini(struct drm_device *);
void nvd0_display_intr(struct drm_device *);

void nvd0_display_flip_stop(struct drm_crtc *);
int  nvd0_display_flip_next(struct drm_crtc *, struct drm_framebuffer *,
			    struct nouveau_channel *, u32 swap_interval);

struct nouveau_bo *nv50_display_crtc_sema(struct drm_device *, int head);
struct nouveau_bo *nvd0_display_crtc_sema(struct drm_device *, int head);

#endif /* __NV50_DISPLAY_H__ */
