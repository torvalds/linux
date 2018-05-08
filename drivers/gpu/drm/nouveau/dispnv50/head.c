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

#include <nvif/class.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include "nouveau_connector.h"
#include "nouveau_bo.h"

static void
nv50_head_lut_load(struct drm_property_blob *blob, int mode,
		   struct nouveau_bo *nvbo)
{
	struct drm_color_lut *in = (struct drm_color_lut *)blob->data;
	void __iomem *lut = (u8 *)nvbo_kmap_obj_iovirtual(nvbo);
	const int size = blob->length / sizeof(*in);
	int bits, shift, i;
	u16 zero, r, g, b;

	/* This can't happen.. But it shuts the compiler up. */
	if (WARN_ON(size != 256))
		return;

	switch (mode) {
	case 0: /* LORES. */
	case 1: /* HIRES. */
		bits = 11;
		shift = 3;
		zero = 0x0000;
		break;
	case 7: /* INTERPOLATE_257_UNITY_RANGE. */
		bits = 14;
		shift = 0;
		zero = 0x6000;
		break;
	default:
		WARN_ON(1);
		return;
	}

	for (i = 0; i < size; i++) {
		r = (drm_color_lut_extract(in[i].  red, bits) + zero) << shift;
		g = (drm_color_lut_extract(in[i].green, bits) + zero) << shift;
		b = (drm_color_lut_extract(in[i]. blue, bits) + zero) << shift;
		writew(r, lut + (i * 0x08) + 0);
		writew(g, lut + (i * 0x08) + 2);
		writew(b, lut + (i * 0x08) + 4);
	}

	/* INTERPOLATE modes require a "next" entry to interpolate with,
	 * so we replicate the last entry to deal with this for now.
	 */
	writew(r, lut + (i * 0x08) + 0);
	writew(g, lut + (i * 0x08) + 2);
	writew(b, lut + (i * 0x08) + 4);
}

void
nv50_head_flush_clr(struct nv50_head *head,
		    struct nv50_head_atom *asyh, bool flush)
{
	union nv50_head_atom_mask clr = {
		.mask = asyh->clr.mask & ~(flush ? 0 : asyh->set.mask),
	};
	if (clr.ilut) head->func->ilut_clr(head);
	if (clr.core) head->func->core_clr(head);
	if (clr.curs) head->func->curs_clr(head);
}

void
nv50_head_flush_set(struct nv50_head *head, struct nv50_head_atom *asyh)
{
	if (asyh->set.view   ) head->func->view    (head, asyh);
	if (asyh->set.mode   ) head->func->mode    (head, asyh);
	if (asyh->set.ilut   ) {
		struct nouveau_bo *nvbo = head->ilut.nvbo[head->ilut.next];
		struct drm_property_blob *blob = asyh->state.gamma_lut;
		if (blob)
			nv50_head_lut_load(blob, asyh->ilut.mode, nvbo);
		asyh->ilut.offset = nvbo->bo.offset;
		head->ilut.next ^= 1;
		head->func->ilut_set(head, asyh);
	}
	if (asyh->set.core   ) head->func->core_set(head, asyh);
	if (asyh->set.curs   ) head->func->curs_set(head, asyh);
	if (asyh->set.base   ) head->func->base    (head, asyh);
	if (asyh->set.ovly   ) head->func->ovly    (head, asyh);
	if (asyh->set.dither ) head->func->dither  (head, asyh);
	if (asyh->set.procamp) head->func->procamp (head, asyh);
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
	struct drm_connector *connector = asyc->state.connector;
	u32 mode = 0x00;

	if (asyc->dither.mode == DITHERING_MODE_AUTO) {
		if (asyh->base.depth > connector->display_info.bpc * 3)
			mode = DITHERING_MODE_DYNAMIC2X2;
	} else {
		mode = asyc->dither.mode;
	}

	if (asyc->dither.depth == DITHERING_DEPTH_AUTO) {
		if (connector->display_info.bpc >= 8)
			mode |= DITHERING_DEPTH_8BPC;
	} else {
		mode |= asyc->dither.depth;
	}

	asyh->dither.enable = mode;
	asyh->dither.bits = mode >> 1;
	asyh->dither.mode = mode >> 3;
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
		asyh->view.oW = min((u16)umode->hdisplay, asyh->view.oW);
		asyh->view.oH = min((u16)umode_vdisplay, asyh->view.oH);
		/* fall-through */
	case DRM_MODE_SCALE_ASPECT:
		if (asyh->view.oH < asyh->view.oW) {
			u32 r = (asyh->view.iW << 19) / asyh->view.iH;
			asyh->view.oW = ((asyh->view.oH * r) + (r / 2)) >> 19;
		} else {
			u32 r = (asyh->view.iH << 19) / asyh->view.iW;
			asyh->view.oH = ((asyh->view.oW * r) + (r / 2)) >> 19;
		}
		break;
	default:
		break;
	}

	asyh->set.view = true;
}

static void
nv50_head_atomic_check_lut(struct nv50_head *head,
			   struct nv50_head_atom *armh,
			   struct nv50_head_atom *asyh)
{
	struct nv50_disp *disp = nv50_disp(head->base.base.dev);

	/* An I8 surface without an input LUT makes no sense, and
	 * EVO will throw an error if you try.
	 *
	 * Legacy clients actually cause this due to the order in
	 * which they call ioctls, so we will enable the LUT with
	 * whatever contents the buffer already contains to avoid
	 * triggering the error check.
	 */
	if (!asyh->state.gamma_lut && asyh->base.cpp != 1) {
		asyh->ilut.handle = 0;
		asyh->clr.ilut = armh->ilut.visible;
		return;
	}

	if (disp->disp->object.oclass < GF110_DISP) {
		asyh->ilut.mode = (asyh->base.cpp == 1) ? 0 : 1;
		asyh->set.ilut = true;
	} else {
		asyh->ilut.mode = 7;
		asyh->set.ilut = asyh->state.color_mgmt_changed;
	}
	asyh->ilut.handle = disp->core->chan.vram.handle;
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
nv50_head_atomic_check(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	struct nouveau_drm *drm = nouveau_drm(crtc->dev);
	struct nv50_head *head = nv50_head(crtc);
	struct nv50_head_atom *armh = nv50_head_atom(crtc->state);
	struct nv50_head_atom *asyh = nv50_head_atom(state);
	struct nouveau_conn_atom *asyc = NULL;
	struct drm_connector_state *conns;
	struct drm_connector *conn;
	int i;

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

		if (asyh->state.mode_changed)
			nv50_head_atomic_check_mode(head, asyh);

		if (asyh->state.color_mgmt_changed ||
		    asyh->base.cpp != armh->base.cpp)
			nv50_head_atomic_check_lut(head, armh, asyh);
		asyh->ilut.visible = asyh->ilut.handle != 0;

		if (asyc) {
			if (asyc->set.scaler)
				nv50_head_atomic_check_view(armh, asyh, asyc);
			if (asyc->set.dither)
				nv50_head_atomic_check_dither(armh, asyh, asyc);
			if (asyc->set.procamp)
				nv50_head_atomic_check_procamp(armh, asyh, asyc);
		}

		if (head->func->core_calc)
			head->func->core_calc(head, asyh);

		asyh->set.base = armh->base.cpp != asyh->base.cpp;
		asyh->set.ovly = armh->ovly.cpp != asyh->ovly.cpp;
	} else {
		asyh->ilut.visible = false;
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
	} else {
		asyh->clr.ilut = armh->ilut.visible;
		asyh->clr.core = armh->core.visible;
		asyh->clr.curs = armh->curs.visible;
		asyh->set.ilut = asyh->ilut.visible;
		asyh->set.core = asyh->core.visible;
		asyh->set.curs = asyh->curs.visible;
	}

	if (asyh->clr.mask || asyh->set.mask)
		nv50_atom(asyh->state.state)->lock_core = true;
	return 0;
}

static const struct drm_crtc_helper_funcs
nv50_head_help = {
	.atomic_check = nv50_head_atomic_check,
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
	asyh->view = armh->view;
	asyh->mode = armh->mode;
	asyh->ilut = armh->ilut;
	asyh->core = armh->core;
	asyh->curs = armh->curs;
	asyh->base = armh->base;
	asyh->ovly = armh->ovly;
	asyh->dither = armh->dither;
	asyh->procamp = armh->procamp;
	asyh->clr.mask = 0;
	asyh->set.mask = 0;
	return &asyh->state;
}

static void
__drm_atomic_helper_crtc_reset(struct drm_crtc *crtc,
			       struct drm_crtc_state *state)
{
	if (crtc->state)
		crtc->funcs->atomic_destroy_state(crtc, crtc->state);
	crtc->state = state;
	crtc->state->crtc = crtc;
}

static void
nv50_head_reset(struct drm_crtc *crtc)
{
	struct nv50_head_atom *asyh;

	if (WARN_ON(!(asyh = kzalloc(sizeof(*asyh), GFP_KERNEL))))
		return;

	__drm_atomic_helper_crtc_reset(crtc, &asyh->state);
}

static void
nv50_head_destroy(struct drm_crtc *crtc)
{
	struct nv50_head *head = nv50_head(crtc);
	int i;

	for (i = 0; i < ARRAY_SIZE(head->ilut.nvbo); i++)
		nouveau_bo_unmap_unpin_unref(&head->ilut.nvbo[i]);

	drm_crtc_cleanup(crtc);
	kfree(head);
}

static const struct drm_crtc_funcs
nv50_head_func = {
	.reset = nv50_head_reset,
	.gamma_set = drm_atomic_helper_legacy_gamma_set,
	.destroy = nv50_head_destroy,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = nv50_head_atomic_duplicate_state,
	.atomic_destroy_state = nv50_head_atomic_destroy_state,
};

int
nv50_head_create(struct drm_device *dev, int index)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv50_disp *disp = nv50_disp(dev);
	struct nv50_head *head;
	struct nv50_wndw *curs, *wndw;
	struct drm_crtc *crtc;
	int ret, i;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (!head)
		return -ENOMEM;

	head->func = disp->core->func->head;
	head->base.index = index;
	ret = nv50_base_new(drm, head->base.index, &wndw);
	if (ret == 0)
		ret = nv50_curs_new(drm, head->base.index, &curs);
	if (ret) {
		kfree(head);
		return ret;
	}

	crtc = &head->base.base;
	drm_crtc_init_with_planes(dev, crtc, &wndw->plane, &curs->plane,
				  &nv50_head_func, "head-%d", head->base.index);
	drm_crtc_helper_add(crtc, &nv50_head_help);
	drm_mode_crtc_set_gamma_size(crtc, 256);

	for (i = 0; i < ARRAY_SIZE(head->ilut.nvbo); i++) {
		ret = nouveau_bo_new_pin_map(&drm->client, 1025 * 8, 0x100,
					     TTM_PL_FLAG_VRAM,
					     &head->ilut.nvbo[i]);
		if (ret)
			goto out;
	}

	/* allocate overlay resources */
	ret = nv50_ovly_new(drm, head->base.index, &wndw);
out:
	if (ret)
		nv50_head_destroy(crtc);
	return ret;
}
