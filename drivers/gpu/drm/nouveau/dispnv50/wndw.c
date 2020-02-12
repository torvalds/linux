/*
 * Copyright 2018 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "wndw.h"
#include "wimm.h"

#include <nvif/class.h>
#include <nvif/cl0002.h>

#include <drm/drm_atomic_helper.h>
#include "nouveau_bo.h"

static void
nv50_wndw_ctxdma_del(struct nv50_wndw_ctxdma *ctxdma)
{
	nvif_object_fini(&ctxdma->object);
	list_del(&ctxdma->head);
	kfree(ctxdma);
}

static struct nv50_wndw_ctxdma *
nv50_wndw_ctxdma_new(struct nv50_wndw *wndw, struct nouveau_framebuffer *fb)
{
	struct nouveau_drm *drm = nouveau_drm(fb->base.dev);
	struct nv50_wndw_ctxdma *ctxdma;
	const u8    kind = fb->nvbo->kind;
	const u32 handle = 0xfb000000 | kind;
	struct {
		struct nv_dma_v0 base;
		union {
			struct nv50_dma_v0 nv50;
			struct gf100_dma_v0 gf100;
			struct gf119_dma_v0 gf119;
		};
	} args = {};
	u32 argc = sizeof(args.base);
	int ret;

	list_for_each_entry(ctxdma, &wndw->ctxdma.list, head) {
		if (ctxdma->object.handle == handle)
			return ctxdma;
	}

	if (!(ctxdma = kzalloc(sizeof(*ctxdma), GFP_KERNEL)))
		return ERR_PTR(-ENOMEM);
	list_add(&ctxdma->head, &wndw->ctxdma.list);

	args.base.target = NV_DMA_V0_TARGET_VRAM;
	args.base.access = NV_DMA_V0_ACCESS_RDWR;
	args.base.start  = 0;
	args.base.limit  = drm->client.device.info.ram_user - 1;

	if (drm->client.device.info.chipset < 0x80) {
		args.nv50.part = NV50_DMA_V0_PART_256;
		argc += sizeof(args.nv50);
	} else
	if (drm->client.device.info.chipset < 0xc0) {
		args.nv50.part = NV50_DMA_V0_PART_256;
		args.nv50.kind = kind;
		argc += sizeof(args.nv50);
	} else
	if (drm->client.device.info.chipset < 0xd0) {
		args.gf100.kind = kind;
		argc += sizeof(args.gf100);
	} else {
		args.gf119.page = GF119_DMA_V0_PAGE_LP;
		args.gf119.kind = kind;
		argc += sizeof(args.gf119);
	}

	ret = nvif_object_init(wndw->ctxdma.parent, handle, NV_DMA_IN_MEMORY,
			       &args, argc, &ctxdma->object);
	if (ret) {
		nv50_wndw_ctxdma_del(ctxdma);
		return ERR_PTR(ret);
	}

	return ctxdma;
}

int
nv50_wndw_wait_armed(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nv50_disp *disp = nv50_disp(wndw->plane.dev);
	if (asyw->set.ntfy) {
		return wndw->func->ntfy_wait_begun(disp->sync,
						   asyw->ntfy.offset,
						   wndw->wndw.base.device);
	}
	return 0;
}

void
nv50_wndw_flush_clr(struct nv50_wndw *wndw, u32 *interlock, bool flush,
		    struct nv50_wndw_atom *asyw)
{
	union nv50_wndw_atom_mask clr = {
		.mask = asyw->clr.mask & ~(flush ? 0 : asyw->set.mask),
	};
	if (clr.sema ) wndw->func-> sema_clr(wndw);
	if (clr.ntfy ) wndw->func-> ntfy_clr(wndw);
	if (clr.xlut ) wndw->func-> xlut_clr(wndw);
	if (clr.image) wndw->func->image_clr(wndw);

	interlock[wndw->interlock.type] |= wndw->interlock.data;
}

void
nv50_wndw_flush_set(struct nv50_wndw *wndw, u32 *interlock,
		    struct nv50_wndw_atom *asyw)
{
	if (interlock) {
		asyw->image.mode = 0;
		asyw->image.interval = 1;
	}

	if (asyw->set.sema ) wndw->func->sema_set (wndw, asyw);
	if (asyw->set.ntfy ) wndw->func->ntfy_set (wndw, asyw);
	if (asyw->set.image) wndw->func->image_set(wndw, asyw);

	if (asyw->set.xlut ) {
		if (asyw->ilut) {
			asyw->xlut.i.offset =
				nv50_lut_load(&wndw->ilut,
					      asyw->xlut.i.mode <= 1,
					      asyw->xlut.i.buffer,
					      asyw->ilut);
		}
		wndw->func->xlut_set(wndw, asyw);
	}

	if (asyw->set.scale) wndw->func->scale_set(wndw, asyw);
	if (asyw->set.point) {
		if (asyw->set.point = false, asyw->set.mask)
			interlock[wndw->interlock.type] |= wndw->interlock.data;
		interlock[NV50_DISP_INTERLOCK_WIMM] |= wndw->interlock.wimm;

		wndw->immd->point(wndw, asyw);
		wndw->immd->update(wndw, interlock);
	} else {
		interlock[wndw->interlock.type] |= wndw->interlock.data;
	}
}

void
nv50_wndw_ntfy_enable(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nv50_disp *disp = nv50_disp(wndw->plane.dev);

	asyw->ntfy.handle = wndw->wndw.sync.handle;
	asyw->ntfy.offset = wndw->ntfy;
	asyw->ntfy.awaken = false;
	asyw->set.ntfy = true;

	wndw->func->ntfy_reset(disp->sync, wndw->ntfy);
	wndw->ntfy ^= 0x10;
}

static void
nv50_wndw_atomic_check_release(struct nv50_wndw *wndw,
			       struct nv50_wndw_atom *asyw,
			       struct nv50_head_atom *asyh)
{
	struct nouveau_drm *drm = nouveau_drm(wndw->plane.dev);
	NV_ATOMIC(drm, "%s release\n", wndw->plane.name);
	wndw->func->release(wndw, asyw, asyh);
	asyw->ntfy.handle = 0;
	asyw->sema.handle = 0;
}

static int
nv50_wndw_atomic_check_acquire_yuv(struct nv50_wndw_atom *asyw)
{
	switch (asyw->state.fb->format->format) {
	case DRM_FORMAT_YUYV: asyw->image.format = 0x28; break;
	case DRM_FORMAT_UYVY: asyw->image.format = 0x29; break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}
	asyw->image.colorspace = 1;
	return 0;
}

static int
nv50_wndw_atomic_check_acquire_rgb(struct nv50_wndw_atom *asyw)
{
	switch (asyw->state.fb->format->format) {
	case DRM_FORMAT_C8         : asyw->image.format = 0x1e; break;
	case DRM_FORMAT_XRGB8888   :
	case DRM_FORMAT_ARGB8888   : asyw->image.format = 0xcf; break;
	case DRM_FORMAT_RGB565     : asyw->image.format = 0xe8; break;
	case DRM_FORMAT_XRGB1555   :
	case DRM_FORMAT_ARGB1555   : asyw->image.format = 0xe9; break;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010: asyw->image.format = 0xd1; break;
	case DRM_FORMAT_XBGR8888   :
	case DRM_FORMAT_ABGR8888   : asyw->image.format = 0xd5; break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010: asyw->image.format = 0xdf; break;
	default:
		return -EINVAL;
	}
	asyw->image.colorspace = 0;
	return 0;
}

static int
nv50_wndw_atomic_check_acquire(struct nv50_wndw *wndw, bool modeset,
			       struct nv50_wndw_atom *armw,
			       struct nv50_wndw_atom *asyw,
			       struct nv50_head_atom *asyh)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(asyw->state.fb);
	struct nouveau_drm *drm = nouveau_drm(wndw->plane.dev);
	int ret;

	NV_ATOMIC(drm, "%s acquire\n", wndw->plane.name);

	if (asyw->state.fb != armw->state.fb || !armw->visible || modeset) {
		asyw->image.w = fb->base.width;
		asyw->image.h = fb->base.height;
		asyw->image.kind = fb->nvbo->kind;

		ret = nv50_wndw_atomic_check_acquire_rgb(asyw);
		if (ret) {
			ret = nv50_wndw_atomic_check_acquire_yuv(asyw);
			if (ret)
				return ret;
		}

		if (asyw->image.kind) {
			asyw->image.layout = 0;
			if (drm->client.device.info.chipset >= 0xc0)
				asyw->image.blockh = fb->nvbo->mode >> 4;
			else
				asyw->image.blockh = fb->nvbo->mode;
			asyw->image.blocks[0] = fb->base.pitches[0] / 64;
			asyw->image.pitch[0] = 0;
		} else {
			asyw->image.layout = 1;
			asyw->image.blockh = 0;
			asyw->image.blocks[0] = 0;
			asyw->image.pitch[0] = fb->base.pitches[0];
		}

		if (!(asyh->state.pageflip_flags & DRM_MODE_PAGE_FLIP_ASYNC))
			asyw->image.interval = 1;
		else
			asyw->image.interval = 0;
		asyw->image.mode = asyw->image.interval ? 0 : 1;
		asyw->set.image = wndw->func->image_set != NULL;
	}

	if (wndw->func->scale_set) {
		asyw->scale.sx = asyw->state.src_x >> 16;
		asyw->scale.sy = asyw->state.src_y >> 16;
		asyw->scale.sw = asyw->state.src_w >> 16;
		asyw->scale.sh = asyw->state.src_h >> 16;
		asyw->scale.dw = asyw->state.crtc_w;
		asyw->scale.dh = asyw->state.crtc_h;
		if (memcmp(&armw->scale, &asyw->scale, sizeof(asyw->scale)))
			asyw->set.scale = true;
	}

	if (wndw->immd) {
		asyw->point.x = asyw->state.crtc_x;
		asyw->point.y = asyw->state.crtc_y;
		if (memcmp(&armw->point, &asyw->point, sizeof(asyw->point)))
			asyw->set.point = true;
	}

	return wndw->func->acquire(wndw, asyw, asyh);
}

static void
nv50_wndw_atomic_check_lut(struct nv50_wndw *wndw,
			   struct nv50_wndw_atom *armw,
			   struct nv50_wndw_atom *asyw,
			   struct nv50_head_atom *asyh)
{
	struct drm_property_blob *ilut = asyh->state.degamma_lut;

	/* I8 format without an input LUT makes no sense, and the
	 * HW error-checks for this.
	 *
	 * In order to handle legacy gamma, when there's no input
	 * LUT we need to steal the output LUT and use it instead.
	 */
	if (!ilut && asyw->state.fb->format->format == DRM_FORMAT_C8) {
		/* This should be an error, but there's legacy clients
		 * that do a modeset before providing a gamma table.
		 *
		 * We keep the window disabled to avoid angering HW.
		 */
		if (!(ilut = asyh->state.gamma_lut)) {
			asyw->visible = false;
			return;
		}

		if (wndw->func->ilut)
			asyh->wndw.olut |= BIT(wndw->id);
	} else {
		asyh->wndw.olut &= ~BIT(wndw->id);
	}

	/* Recalculate LUT state. */
	memset(&asyw->xlut, 0x00, sizeof(asyw->xlut));
	if ((asyw->ilut = wndw->func->ilut ? ilut : NULL)) {
		wndw->func->ilut(wndw, asyw);
		asyw->xlut.handle = wndw->wndw.vram.handle;
		asyw->xlut.i.buffer = !asyw->xlut.i.buffer;
		asyw->set.xlut = true;
	}

	/* Handle setting base SET_OUTPUT_LUT_LO_ENABLE_USE_CORE_LUT. */
	if (wndw->func->olut_core &&
	    (!armw->visible || (armw->xlut.handle && !asyw->xlut.handle)))
		asyw->set.xlut = true;

	/* Can't do an immediate flip while changing the LUT. */
	asyh->state.pageflip_flags &= ~DRM_MODE_PAGE_FLIP_ASYNC;
}

static int
nv50_wndw_atomic_check(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct nouveau_drm *drm = nouveau_drm(plane->dev);
	struct nv50_wndw *wndw = nv50_wndw(plane);
	struct nv50_wndw_atom *armw = nv50_wndw_atom(wndw->plane.state);
	struct nv50_wndw_atom *asyw = nv50_wndw_atom(state);
	struct nv50_head_atom *harm = NULL, *asyh = NULL;
	bool modeset = false;
	int ret;

	NV_ATOMIC(drm, "%s atomic_check\n", plane->name);

	/* Fetch the assembly state for the head the window will belong to,
	 * and determine whether the window will be visible.
	 */
	if (asyw->state.crtc) {
		asyh = nv50_head_atom_get(asyw->state.state, asyw->state.crtc);
		if (IS_ERR(asyh))
			return PTR_ERR(asyh);
		modeset = drm_atomic_crtc_needs_modeset(&asyh->state);
		asyw->visible = asyh->state.active;
	} else {
		asyw->visible = false;
	}

	/* Fetch assembly state for the head the window used to belong to. */
	if (armw->state.crtc) {
		harm = nv50_head_atom_get(asyw->state.state, armw->state.crtc);
		if (IS_ERR(harm))
			return PTR_ERR(harm);
	}

	/* LUT configuration can potentially cause the window to be disabled. */
	if (asyw->visible && wndw->func->xlut_set &&
	    (!armw->visible ||
	     asyh->state.color_mgmt_changed ||
	     asyw->state.fb->format->format !=
	     armw->state.fb->format->format))
		nv50_wndw_atomic_check_lut(wndw, armw, asyw, asyh);

	/* Calculate new window state. */
	if (asyw->visible) {
		ret = nv50_wndw_atomic_check_acquire(wndw, modeset,
						     armw, asyw, asyh);
		if (ret)
			return ret;

		asyh->wndw.mask |= BIT(wndw->id);
	} else
	if (armw->visible) {
		nv50_wndw_atomic_check_release(wndw, asyw, harm);
		harm->wndw.mask &= ~BIT(wndw->id);
	} else {
		return 0;
	}

	/* Aside from the obvious case where the window is actively being
	 * disabled, we might also need to temporarily disable the window
	 * when performing certain modeset operations.
	 */
	if (!asyw->visible || modeset) {
		asyw->clr.ntfy = armw->ntfy.handle != 0;
		asyw->clr.sema = armw->sema.handle != 0;
		asyw->clr.xlut = armw->xlut.handle != 0;
		if (asyw->clr.xlut && asyw->visible)
			asyw->set.xlut = asyw->xlut.handle != 0;
		if (wndw->func->image_clr)
			asyw->clr.image = armw->image.handle[0] != 0;
	}

	return 0;
}

static void
nv50_wndw_cleanup_fb(struct drm_plane *plane, struct drm_plane_state *old_state)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(old_state->fb);
	struct nouveau_drm *drm = nouveau_drm(plane->dev);

	NV_ATOMIC(drm, "%s cleanup: %p\n", plane->name, old_state->fb);
	if (!old_state->fb)
		return;

	nouveau_bo_unpin(fb->nvbo);
}

static int
nv50_wndw_prepare_fb(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(state->fb);
	struct nouveau_drm *drm = nouveau_drm(plane->dev);
	struct nv50_wndw *wndw = nv50_wndw(plane);
	struct nv50_wndw_atom *asyw = nv50_wndw_atom(state);
	struct nv50_head_atom *asyh;
	struct nv50_wndw_ctxdma *ctxdma;
	int ret;

	NV_ATOMIC(drm, "%s prepare: %p\n", plane->name, state->fb);
	if (!asyw->state.fb)
		return 0;

	ret = nouveau_bo_pin(fb->nvbo, TTM_PL_FLAG_VRAM, true);
	if (ret)
		return ret;

	if (wndw->ctxdma.parent) {
		ctxdma = nv50_wndw_ctxdma_new(wndw, fb);
		if (IS_ERR(ctxdma)) {
			nouveau_bo_unpin(fb->nvbo);
			return PTR_ERR(ctxdma);
		}

		asyw->image.handle[0] = ctxdma->object.handle;
	}

	asyw->state.fence = reservation_object_get_excl_rcu(fb->nvbo->bo.resv);
	asyw->image.offset[0] = fb->nvbo->bo.offset;

	if (wndw->func->prepare) {
		asyh = nv50_head_atom_get(asyw->state.state, asyw->state.crtc);
		if (IS_ERR(asyh))
			return PTR_ERR(asyh);

		wndw->func->prepare(wndw, asyh, asyw);
	}

	return 0;
}

static const struct drm_plane_helper_funcs
nv50_wndw_helper = {
	.prepare_fb = nv50_wndw_prepare_fb,
	.cleanup_fb = nv50_wndw_cleanup_fb,
	.atomic_check = nv50_wndw_atomic_check,
};

static void
nv50_wndw_atomic_destroy_state(struct drm_plane *plane,
			       struct drm_plane_state *state)
{
	struct nv50_wndw_atom *asyw = nv50_wndw_atom(state);
	__drm_atomic_helper_plane_destroy_state(&asyw->state);
	kfree(asyw);
}

static struct drm_plane_state *
nv50_wndw_atomic_duplicate_state(struct drm_plane *plane)
{
	struct nv50_wndw_atom *armw = nv50_wndw_atom(plane->state);
	struct nv50_wndw_atom *asyw;
	if (!(asyw = kmalloc(sizeof(*asyw), GFP_KERNEL)))
		return NULL;
	__drm_atomic_helper_plane_duplicate_state(plane, &asyw->state);
	asyw->sema = armw->sema;
	asyw->ntfy = armw->ntfy;
	asyw->ilut = NULL;
	asyw->xlut = armw->xlut;
	asyw->image = armw->image;
	asyw->point = armw->point;
	asyw->clr.mask = 0;
	asyw->set.mask = 0;
	return &asyw->state;
}

static void
nv50_wndw_reset(struct drm_plane *plane)
{
	struct nv50_wndw_atom *asyw;

	if (WARN_ON(!(asyw = kzalloc(sizeof(*asyw), GFP_KERNEL))))
		return;

	if (plane->state)
		plane->funcs->atomic_destroy_state(plane, plane->state);
	plane->state = &asyw->state;
	plane->state->plane = plane;
	plane->state->rotation = DRM_MODE_ROTATE_0;
}

static void
nv50_wndw_destroy(struct drm_plane *plane)
{
	struct nv50_wndw *wndw = nv50_wndw(plane);
	struct nv50_wndw_ctxdma *ctxdma, *ctxtmp;

	list_for_each_entry_safe(ctxdma, ctxtmp, &wndw->ctxdma.list, head) {
		nv50_wndw_ctxdma_del(ctxdma);
	}

	nvif_notify_fini(&wndw->notify);
	nv50_dmac_destroy(&wndw->wimm);
	nv50_dmac_destroy(&wndw->wndw);

	nv50_lut_fini(&wndw->ilut);

	drm_plane_cleanup(&wndw->plane);
	kfree(wndw);
}

const struct drm_plane_funcs
nv50_wndw = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = nv50_wndw_destroy,
	.reset = nv50_wndw_reset,
	.atomic_duplicate_state = nv50_wndw_atomic_duplicate_state,
	.atomic_destroy_state = nv50_wndw_atomic_destroy_state,
};

static int
nv50_wndw_notify(struct nvif_notify *notify)
{
	return NVIF_NOTIFY_KEEP;
}

void
nv50_wndw_fini(struct nv50_wndw *wndw)
{
	nvif_notify_put(&wndw->notify);
}

void
nv50_wndw_init(struct nv50_wndw *wndw)
{
	nvif_notify_get(&wndw->notify);
}

int
nv50_wndw_new_(const struct nv50_wndw_func *func, struct drm_device *dev,
	       enum drm_plane_type type, const char *name, int index,
	       const u32 *format, u32 heads,
	       enum nv50_disp_interlock_type interlock_type, u32 interlock_data,
	       struct nv50_wndw **pwndw)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvif_mmu *mmu = &drm->client.mmu;
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_wndw *wndw;
	int nformat;
	int ret;

	if (!(wndw = *pwndw = kzalloc(sizeof(*wndw), GFP_KERNEL)))
		return -ENOMEM;
	wndw->func = func;
	wndw->id = index;
	wndw->interlock.type = interlock_type;
	wndw->interlock.data = interlock_data;

	wndw->ctxdma.parent = &wndw->wndw.base.user;
	INIT_LIST_HEAD(&wndw->ctxdma.list);

	for (nformat = 0; format[nformat]; nformat++);

	ret = drm_universal_plane_init(dev, &wndw->plane, heads, &nv50_wndw,
				       format, nformat, NULL,
				       type, "%s-%d", name, index);
	if (ret) {
		kfree(*pwndw);
		*pwndw = NULL;
		return ret;
	}

	drm_plane_helper_add(&wndw->plane, &nv50_wndw_helper);

	if (wndw->func->ilut) {
		ret = nv50_lut_init(disp, mmu, &wndw->ilut);
		if (ret)
			return ret;
	}

	wndw->notify.func = nv50_wndw_notify;
	return 0;
}

int
nv50_wndw_new(struct nouveau_drm *drm, enum drm_plane_type type, int index,
	      struct nv50_wndw **pwndw)
{
	struct {
		s32 oclass;
		int version;
		int (*new)(struct nouveau_drm *, enum drm_plane_type,
			   int, s32, struct nv50_wndw **);
	} wndws[] = {
		{ GV100_DISP_WINDOW_CHANNEL_DMA, 0, wndwc37e_new },
		{}
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	int cid, ret;

	cid = nvif_mclass(&disp->disp->object, wndws);
	if (cid < 0) {
		NV_ERROR(drm, "No supported window class\n");
		return cid;
	}

	ret = wndws[cid].new(drm, type, index, wndws[cid].oclass, pwndw);
	if (ret)
		return ret;

	return nv50_wimm_init(drm, *pwndw);
}
