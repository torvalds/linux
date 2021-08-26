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
#include "head.h"
#include "base.h"
#include "core.h"
#include "curs.h"
#include "ovly.h"
#include "crc.h"

#include <nvif/class.h>
#include <nvif/event.h>
#include <nvif/cl0046.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_vblank.h>
#include "nouveau_connector.h"

void
nv50_head_flush_clr(struct nv50_head *head,
		    struct nv50_head_atom *asyh, bool flush)
{
	union nv50_head_atom_mask clr = {
		.mask = asyh->clr.mask & ~(flush ? 0 : asyh->set.mask),
	};
	if (clr.crc)  nv50_crc_atomic_clr(head);
	if (clr.olut) head->func->olut_clr(head);
	if (clr.core) head->func->core_clr(head);
	if (clr.curs) head->func->curs_clr(head);
}

void
nv50_head_flush_set_wndw(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	if (asyh->set.olut   ) {
		asyh->olut.offset = nv50_lut_load(&head->olut,
						  asyh->olut.buffer,
						  asyh->state.gamma_lut,
						  asyh->olut.load);
		head->func->olut_set(head, asyh);
	}
}

void
nv50_head_flush_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	if (asyh->set.view   ) head->func->view    (head, asyh);
	if (asyh->set.mode   ) head->func->mode    (head, asyh);
	if (asyh->set.core   ) head->func->core_set(head, asyh);
	if (asyh->set.curs   ) head->func->curs_set(head, asyh);
	if (asyh->set.base   ) head->func->base    (head, asyh);
	if (asyh->set.ovly   ) head->func->ovly    (head, asyh);
	if (asyh->set.dither ) head->func->dither  (head, asyh);
	if (asyh->set.procamp) head->func->procamp (head, asyh);
	if (asyh->set.crc    ) nv50_crc_atomic_set (head, asyh);
	if (asyh->set.or     ) head->func->or      (head, asyh);
}

static void
nv50_head_atomic_check_procamp(struct nv50_head_atom *armh,
			       struct nv50_head_atom *asyh,
			       struct nouveau_conn_atom *asyc)
{
	const int vib = asyc->procamp.color_vibrance - 100;
	const int hue = asyc->procamp.vibrant_hue - 90;
	const int adj = (vib > 0) ? 50 : 0;
	asyh->procamp.sat.cos = ((vib * 2047 + adj) / 100) & 0xfff;
	asyh->procamp.sat.sin = ((hue * 2047) / 100) & 0xfff;
	asyh->set.procamp = true;
}

static void
nv50_head_atomic_check_dither(struct nv50_head_atom *armh,
			      struct nv50_head_atom *asyh,
			      struct nouveau_conn_atom *asyc)
{
	u32 mode = 0x00;

	if (asyc->dither.mode) {
		if (asyc->dither.mode == DITHERING_MODE_AUTO) {
			if (asyh->base.depth > asyh->or.bpc * 3)
				mode = DITHERING_MODE_DYNAMIC2X2;
		} else {
			mode = asyc->dither.mode;
		}

		if (asyc->dither.depth == DITHERING_DEPTH_AUTO) {
			if (asyh->or.bpc >= 8)
				mode |= DITHERING_DEPTH_8BPC;
		} else {
			mode |= asyc->dither.depth;
		}
	}

	asyh->dither.enable = NVVAL_GET(mode, NV507D, HEAD_SET_DITHER_CONTROL, ENABLE);
	asyh->dither.bits = NVVAL_GET(mode, NV507D, HEAD_SET_DITHER_CONTROL, BITS);
	asyh->dither.mode = NVVAL_GET(mode, NV507D, HEAD_SET_DITHER_CONTROL, MODE);
	asyh->set.dither = true;
}

static void
nv50_head_atomic_check_view(struct nv50_head_atom *armh,
			    struct nv50_head_atom *asyh,
			    struct nouveau_conn_atom *asyc)
{
	struct drm_connector *connector = asyc->state.connector;
	struct drm_display_mode *omode = &asyh->state.adjusted_mode;
	struct drm_display_mode *umode = &asyh->state.mode;
	int mode = asyc->scaler.mode;
	struct edid *edid;
	int umode_vdisplay, omode_hdisplay, omode_vdisplay;

	if (connector->edid_blob_ptr)
		edid = (struct edid *)connector->edid_blob_ptr->data;
	else
		edid = NULL;

	if (!asyc->scaler.full) {
		if (mode == DRM_MODE_SCALE_NONE)
			omode = umode;
	} else {
		/* Non-EDID LVDS/eDP mode. */
		mode = DRM_MODE_SCALE_FULLSCREEN;
	}

	/* For the user-specified mode, we must ignore doublescan and
	 * the like, but honor frame packing.
	 */
	umode_vdisplay = umode->vdisplay;
	if ((umode->flags & DRM_MODE_FLAG_3D_MASK) == DRM_MODE_FLAG_3D_FRAME_PACKING)
		umode_vdisplay += umode->vtotal;
	asyh->view.iW = umode->hdisplay;
	asyh->view.iH = umode_vdisplay;
	/* For the output mode, we can just use the stock helper. */
	drm_mode_get_hv_timing(omode, &omode_hdisplay, &omode_vdisplay);
	asyh->view.oW = omode_hdisplay;
	asyh->view.oH = omode_vdisplay;

	/* Add overscan compensation if necessary, will keep the aspect
	 * ratio the same as the backend mode unless overridden by the
	 * user setting both hborder and vborder properties.
	 */
	if ((asyc->scaler.underscan.mode == UNDERSCAN_ON ||
	    (asyc->scaler.underscan.mode == UNDERSCAN_AUTO &&
	     drm_detect_hdmi_monitor(edid)))) {
		u32 bX = asyc->scaler.underscan.hborder;
		u32 bY = asyc->scaler.underscan.vborder;
		u32 r = (asyh->view.oH << 19) / asyh->view.oW;

		if (bX) {
			asyh->view.oW -= (bX * 2);
			if (bY) asyh->view.oH -= (bY * 2);
			else    asyh->view.oH  = ((asyh->view.oW * r) + (r / 2)) >> 19;
		} else {
			asyh->view.oW -= (asyh->view.oW >> 4) + 32;
			if (bY) asyh->view.oH -= (bY * 2);
			else    asyh->view.oH  = ((asyh->view.oW * r) + (r / 2)) >> 19;
		}
	}

	/* Handle CENTER/ASPECT scaling, taking into account the areas
	 * removed already for overscan compensation.
	 */
	switch (mode) {
	case DRM_MODE_SCALE_CENTER:
		/* NOTE: This will cause scaling when the input is
		 * larger than the output.
		 */
		asyh->view.oW = min(asyh->view.iW, asyh->view.oW);
		asyh->view.oH = min(asyh->view.iH, asyh->view.oH);
		break;
	case DRM_MODE_SCALE_ASPECT:
		/* Determine whether the scaling should be on width or on
		 * height. This is done by comparing the aspect ratios of the
		 * sizes. If the output AR is larger than input AR, that means
		 * we want to change the width (letterboxed on the
		 * left/right), otherwise on the height (letterboxed on the
		 * top/bottom).
		 *
		 * E.g. 4:3 (1.333) AR image displayed on a 16:10 (1.6) AR
		 * screen will have letterboxes on the left/right. However a
		 * 16:9 (1.777) AR image on that same screen will have
		 * letterboxes on the top/bottom.
		 *
		 * inputAR = iW / iH; outputAR = oW / oH
		 * outputAR > inputAR is equivalent to oW * iH > iW * oH
		 */
		if (asyh->view.oW * asyh->view.iH > asyh->view.iW * asyh->view.oH) {
			/* Recompute output width, i.e. left/right letterbox */
			u32 r = (asyh->view.iW << 19) / asyh->view.iH;
			asyh->view.oW = ((asyh->view.oH * r) + (r / 2)) >> 19;
		} else {
			/* Recompute output height, i.e. top/bottom letterbox */
			u32 r = (asyh->view.iH << 19) / asyh->view.iW;
			asyh->view.oH = ((asyh->view.oW * r) + (r / 2)) >> 19;
		}
		break;
	default:
		break;
	}

	asyh->set.view = true;
}

static int
nv50_head_atomic_check_lut(struct nv50_head *head,
			   struct nv50_head_atom *asyh)
{
	struct nv50_disp *disp = nv50_disp(head->base.base.dev);
	struct drm_property_blob *olut = asyh->state.gamma_lut;
	int size;

	/* Determine whether core output LUT should be enabled. */
	if (olut) {
		/* Check if any window(s) have stolen the core output LUT
		 * to as an input LUT for legacy gamma + I8 colour format.
		 */
		if (asyh->wndw.olut) {
			/* If any window has stolen the core output LUT,
			 * all of them must.
			 */
			if (asyh->wndw.olut != asyh->wndw.mask)
				return -EINVAL;
			olut = NULL;
		}
	}

	if (!olut) {
		if (!head->func->olut_identity) {
			asyh->olut.handle = 0;
			return 0;
		}
		size = 0;
	} else {
		size = drm_color_lut_size(olut);
	}

	if (!head->func->olut(head, asyh, size)) {
		DRM_DEBUG_KMS("Invalid olut\n");
		return -EINVAL;
	}
	asyh->olut.handle = disp->core->chan.vram.handle;
	asyh->olut.buffer = !asyh->olut.buffer;

	return 0;
}

static void
nv50_head_atomic_check_mode(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	struct drm_display_mode *mode = &asyh->state.adjusted_mode;
	struct nv50_head_mode *m = &asyh->mode;
	u32 blankus;

	drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V | CRTC_STEREO_DOUBLE);

	/*
	 * DRM modes are defined in terms of a repeating interval
	 * starting with the active display area.  The hardware modes
	 * are defined in terms of a repeating interval starting one
	 * unit (pixel or line) into the sync pulse.  So, add bias.
	 */

	m->h.active = mode->crtc_htotal;
	m->h.synce  = mode->crtc_hsync_end - mode->crtc_hsync_start - 1;
	m->h.blanke = mode->crtc_hblank_end - mode->crtc_hsync_start - 1;
	m->h.blanks = m->h.blanke + mode->crtc_hdisplay;

	m->v.active = mode->crtc_vtotal;
	m->v.synce  = mode->crtc_vsync_end - mode->crtc_vsync_start - 1;
	m->v.blanke = mode->crtc_vblank_end - mode->crtc_vsync_start - 1;
	m->v.blanks = m->v.blanke + mode->crtc_vdisplay;

	/*XXX: Safe underestimate, even "0" works */
	blankus = (m->v.active - mode->crtc_vdisplay - 2) * m->h.active;
	blankus *= 1000;
	blankus /= mode->crtc_clock;
	m->v.blankus = blankus;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		m->v.blank2e =  m->v.active + m->v.blanke;
		m->v.blank2s =  m->v.blank2e + mode->crtc_vdisplay;
		m->v.active  = (m->v.active * 2) + 1;
		m->interlace = true;
	} else {
		m->v.blank2e = 0;
		m->v.blank2s = 1;
		m->interlace = false;
	}
	m->clock = mode->crtc_clock;

	asyh->or.nhsync = !!(mode->flags & DRM_MODE_FLAG_NHSYNC);
	asyh->or.nvsync = !!(mode->flags & DRM_MODE_FLAG_NVSYNC);
	asyh->set.or = head->func->or != NULL;
	asyh->set.mode = true;
}

static int
nv50_head_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state = drm_atomic_get_old_crtc_state(state,
									      crtc);
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	struct nouveau_drm *drm = nouveau_drm(crtc->dev);
	struct nv50_head *head = nv50_head(crtc);
	struct nv50_head_atom *armh = nv50_head_atom(old_crtc_state);
	struct nv50_head_atom *asyh = nv50_head_atom(crtc_state);
	struct nouveau_conn_atom *asyc = NULL;
	struct drm_connector_state *conns;
	struct drm_connector *conn;
	int i, ret;

	NV_ATOMIC(drm, "%s atomic_check %d\n", crtc->name, asyh->state.active);
	if (asyh->state.active) {
		for_each_new_connector_in_state(asyh->state.state, conn, conns, i) {
			if (conns->crtc == crtc) {
				asyc = nouveau_conn_atom(conns);
				break;
			}
		}

		if (armh->state.active) {
			if (asyc) {
				if (asyh->state.mode_changed)
					asyc->set.scaler = true;
				if (armh->base.depth != asyh->base.depth)
					asyc->set.dither = true;
			}
		} else {
			if (asyc)
				asyc->set.mask = ~0;
			asyh->set.mask = ~0;
			asyh->set.or = head->func->or != NULL;
		}

		if (asyh->state.mode_changed || asyh->state.connectors_changed)
			nv50_head_atomic_check_mode(head, asyh);

		if (asyh->state.color_mgmt_changed ||
		    memcmp(&armh->wndw, &asyh->wndw, sizeof(asyh->wndw))) {
			int ret = nv50_head_atomic_check_lut(head, asyh);
			if (ret)
				return ret;

			asyh->olut.visible = asyh->olut.handle != 0;
		}

		if (asyc) {
			if (asyc->set.scaler)
				nv50_head_atomic_check_view(armh, asyh, asyc);
			if (asyc->set.dither)
				nv50_head_atomic_check_dither(armh, asyh, asyc);
			if (asyc->set.procamp)
				nv50_head_atomic_check_procamp(armh, asyh, asyc);
		}

		if (head->func->core_calc) {
			head->func->core_calc(head, asyh);
			if (!asyh->core.visible)
				asyh->olut.visible = false;
		}

		asyh->set.base = armh->base.cpp != asyh->base.cpp;
		asyh->set.ovly = armh->ovly.cpp != asyh->ovly.cpp;
	} else {
		asyh->olut.visible = false;
		asyh->core.visible = false;
		asyh->curs.visible = false;
		asyh->base.cpp = 0;
		asyh->ovly.cpp = 0;
	}

	if (!drm_atomic_crtc_needs_modeset(&asyh->state)) {
		if (asyh->core.visible) {
			if (memcmp(&armh->core, &asyh->core, sizeof(asyh->core)))
				asyh->set.core = true;
		} else
		if (armh->core.visible) {
			asyh->clr.core = true;
		}

		if (asyh->curs.visible) {
			if (memcmp(&armh->curs, &asyh->curs, sizeof(asyh->curs)))
				asyh->set.curs = true;
		} else
		if (armh->curs.visible) {
			asyh->clr.curs = true;
		}

		if (asyh->olut.visible) {
			if (memcmp(&armh->olut, &asyh->olut, sizeof(asyh->olut)))
				asyh->set.olut = true;
		} else
		if (armh->olut.visible) {
			asyh->clr.olut = true;
		}
	} else {
		asyh->clr.olut = armh->olut.visible;
		asyh->clr.core = armh->core.visible;
		asyh->clr.curs = armh->curs.visible;
		asyh->set.olut = asyh->olut.visible;
		asyh->set.core = asyh->core.visible;
		asyh->set.curs = asyh->curs.visible;
	}

	ret = nv50_crc_atomic_check_head(head, asyh, armh);
	if (ret)
		return ret;

	if (asyh->clr.mask || asyh->set.mask)
		nv50_atom(asyh->state.state)->lock_core = true;
	return 0;
}

static const struct drm_crtc_helper_funcs
nv50_head_help = {
	.atomic_check = nv50_head_atomic_check,
	.get_scanout_position = nouveau_display_scanoutpos,
};

static void
nv50_head_atomic_destroy_state(struct drm_crtc *crtc,
			       struct drm_crtc_state *state)
{
	struct nv50_head_atom *asyh = nv50_head_atom(state);
	__drm_atomic_helper_crtc_destroy_state(&asyh->state);
	kfree(asyh);
}

static struct drm_crtc_state *
nv50_head_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct nv50_head_atom *armh = nv50_head_atom(crtc->state);
	struct nv50_head_atom *asyh;
	if (!(asyh = kmalloc(sizeof(*asyh), GFP_KERNEL)))
		return NULL;
	__drm_atomic_helper_crtc_duplicate_state(crtc, &asyh->state);
	asyh->wndw = armh->wndw;
	asyh->view = armh->view;
	asyh->mode = armh->mode;
	asyh->olut = armh->olut;
	asyh->core = armh->core;
	asyh->curs = armh->curs;
	asyh->base = armh->base;
	asyh->ovly = armh->ovly;
	asyh->dither = armh->dither;
	asyh->procamp = armh->procamp;
	asyh->crc = armh->crc;
	asyh->or = armh->or;
	asyh->dp = armh->dp;
	asyh->clr.mask = 0;
	asyh->set.mask = 0;
	return &asyh->state;
}

static void
nv50_head_reset(struct drm_crtc *crtc)
{
	struct nv50_head_atom *asyh;

	if (WARN_ON(!(asyh = kzalloc(sizeof(*asyh), GFP_KERNEL))))
		return;

	if (crtc->state)
		nv50_head_atomic_destroy_state(crtc, crtc->state);

	__drm_atomic_helper_crtc_reset(crtc, &asyh->state);
}

static int
nv50_head_late_register(struct drm_crtc *crtc)
{
	return nv50_head_crc_late_register(nv50_head(crtc));
}

static void
nv50_head_destroy(struct drm_crtc *crtc)
{
	struct nv50_head *head = nv50_head(crtc);

	nvif_notify_dtor(&head->base.vblank);
	nv50_lut_fini(&head->olut);
	drm_crtc_cleanup(crtc);
	kfree(head);
}

static const struct drm_crtc_funcs
nv50_head_func = {
	.reset = nv50_head_reset,
	.destroy = nv50_head_destroy,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = nv50_head_atomic_duplicate_state,
	.atomic_destroy_state = nv50_head_atomic_destroy_state,
	.enable_vblank = nouveau_display_vblank_enable,
	.disable_vblank = nouveau_display_vblank_disable,
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp,
	.late_register = nv50_head_late_register,
};

static const struct drm_crtc_funcs
nvd9_head_func = {
	.reset = nv50_head_reset,
	.destroy = nv50_head_destroy,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = nv50_head_atomic_duplicate_state,
	.atomic_destroy_state = nv50_head_atomic_destroy_state,
	.enable_vblank = nouveau_display_vblank_enable,
	.disable_vblank = nouveau_display_vblank_disable,
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp,
	.verify_crc_source = nv50_crc_verify_source,
	.get_crc_sources = nv50_crc_get_sources,
	.set_crc_source = nv50_crc_set_source,
	.late_register = nv50_head_late_register,
};

static int nv50_head_vblank_handler(struct nvif_notify *notify)
{
	struct nouveau_crtc *nv_crtc =
		container_of(notify, struct nouveau_crtc, vblank);

	if (drm_crtc_handle_vblank(&nv_crtc->base))
		nv50_crc_handle_vblank(nv50_head(&nv_crtc->base));

	return NVIF_NOTIFY_KEEP;
}

struct nv50_head *
nv50_head_create(struct drm_device *dev, int index)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_head *head;
	struct nv50_wndw *base, *ovly, *curs;
	struct nouveau_crtc *nv_crtc;
	struct drm_crtc *crtc;
	const struct drm_crtc_funcs *funcs;
	int ret;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (!head)
		return ERR_PTR(-ENOMEM);

	head->func = disp->core->func->head;
	head->base.index = index;

	if (disp->disp->object.oclass < GF110_DISP)
		funcs = &nv50_head_func;
	else
		funcs = &nvd9_head_func;

	if (disp->disp->object.oclass < GV100_DISP) {
		ret = nv50_base_new(drm, head->base.index, &base);
		ret = nv50_ovly_new(drm, head->base.index, &ovly);
	} else {
		ret = nv50_wndw_new(drm, DRM_PLANE_TYPE_PRIMARY,
				    head->base.index * 2 + 0, &base);
		ret = nv50_wndw_new(drm, DRM_PLANE_TYPE_OVERLAY,
				    head->base.index * 2 + 1, &ovly);
	}
	if (ret == 0)
		ret = nv50_curs_new(drm, head->base.index, &curs);
	if (ret) {
		kfree(head);
		return ERR_PTR(ret);
	}

	nv_crtc = &head->base;
	crtc = &nv_crtc->base;
	drm_crtc_init_with_planes(dev, crtc, &base->plane, &curs->plane,
				  funcs, "head-%d", head->base.index);
	drm_crtc_helper_add(crtc, &nv50_head_help);
	/* Keep the legacy gamma size at 256 to avoid compatibility issues */
	drm_mode_crtc_set_gamma_size(crtc, 256);
	drm_crtc_enable_color_mgmt(crtc, base->func->ilut_size,
				   disp->disp->object.oclass >= GF110_DISP,
				   head->func->olut_size);

	if (head->func->olut_set) {
		ret = nv50_lut_init(disp, &drm->client.mmu, &head->olut);
		if (ret) {
			nv50_head_destroy(crtc);
			return ERR_PTR(ret);
		}
	}

	ret = nvif_notify_ctor(&disp->disp->object, "kmsVbl", nv50_head_vblank_handler,
			       false, NV04_DISP_NTFY_VBLANK,
			       &(struct nvif_notify_head_req_v0) {
				    .head = nv_crtc->index,
			       },
			       sizeof(struct nvif_notify_head_req_v0),
			       sizeof(struct nvif_notify_head_rep_v0),
			       &nv_crtc->vblank);
	if (ret)
		return ERR_PTR(ret);

	return head;
}
