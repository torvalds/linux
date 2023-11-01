/*
 * Copyright 2009 Red Hat Inc.
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
 *
 * Author: Ben Skeggs
 */

#include <drm/drm_crtc_helper.h>

#include "nouveau_drv.h"
#include "nouveau_reg.h"
#include "hw.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"
#include "nouveau_bo.h"
#include "nouveau_gem.h"
#include "nouveau_chan.h"

#include <nvif/if0004.h>

struct nouveau_connector *
nv04_encoder_get_connector(struct nouveau_encoder *encoder)
{
	struct drm_device *dev = to_drm_encoder(encoder)->dev;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct nouveau_connector *nv_connector = NULL;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->encoder == to_drm_encoder(encoder))
			nv_connector = nouveau_connector(connector);
	}
	drm_connector_list_iter_end(&conn_iter);

	return nv_connector;
}

static void
nv04_display_fini(struct drm_device *dev, bool runtime, bool suspend)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv04_display *disp = nv04_display(dev);
	struct drm_crtc *crtc;

	/* Disable flip completion events. */
	nvif_event_block(&disp->flip);

	/* Disable vblank interrupts. */
	NVWriteCRTC(dev, 0, NV_PCRTC_INTR_EN_0, 0);
	if (nv_two_heads(dev))
		NVWriteCRTC(dev, 1, NV_PCRTC_INTR_EN_0, 0);

	if (!runtime)
		cancel_work_sync(&drm->hpd_work);

	if (!suspend)
		return;

	/* Un-pin FB and cursors so they'll be evicted to system memory. */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct drm_framebuffer *fb = crtc->primary->fb;
		struct nouveau_bo *nvbo;

		if (!fb || !fb->obj[0])
			continue;
		nvbo = nouveau_gem_object(fb->obj[0]);
		nouveau_bo_unpin(nvbo);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
		if (nv_crtc->cursor.nvbo) {
			if (nv_crtc->cursor.set_offset)
				nouveau_bo_unmap(nv_crtc->cursor.nvbo);
			nouveau_bo_unpin(nv_crtc->cursor.nvbo);
		}
	}
}

static int
nv04_display_init(struct drm_device *dev, bool resume, bool runtime)
{
	struct nv04_display *disp = nv04_display(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_encoder *encoder;
	struct drm_crtc *crtc;
	int ret;

	/* meh.. modeset apparently doesn't setup all the regs and depends
	 * on pre-existing state, for now load the state of the card *before*
	 * nouveau was loaded, and then do a modeset.
	 *
	 * best thing to do probably is to make save/restore routines not
	 * save/restore "pre-load" state, but more general so we can save
	 * on suspend too.
	 */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
		nv_crtc->save(&nv_crtc->base);
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, base.base.head)
		encoder->enc_save(&encoder->base.base);

	/* Enable flip completion events. */
	nvif_event_allow(&disp->flip);

	if (!resume)
		return 0;

	/* Re-pin FB/cursors. */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct drm_framebuffer *fb = crtc->primary->fb;
		struct nouveau_bo *nvbo;

		if (!fb || !fb->obj[0])
			continue;
		nvbo = nouveau_gem_object(fb->obj[0]);
		ret = nouveau_bo_pin(nvbo, NOUVEAU_GEM_DOMAIN_VRAM, true);
		if (ret)
			NV_ERROR(drm, "Could not pin framebuffer\n");
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
		if (!nv_crtc->cursor.nvbo)
			continue;

		ret = nouveau_bo_pin(nv_crtc->cursor.nvbo,
				     NOUVEAU_GEM_DOMAIN_VRAM, true);
		if (!ret && nv_crtc->cursor.set_offset)
			ret = nouveau_bo_map(nv_crtc->cursor.nvbo);
		if (ret)
			NV_ERROR(drm, "Could not pin/map cursor.\n");
	}

	/* Force CLUT to get re-loaded during modeset. */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

		nv_crtc->lut.depth = 0;
	}

	/* This should ensure we don't hit a locking problem when someone
	 * wakes us up via a connector.  We should never go into suspend
	 * while the display is on anyways.
	 */
	if (runtime)
		return 0;

	/* Restore mode. */
	drm_helper_resume_force_mode(dev);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

		if (!nv_crtc->cursor.nvbo)
			continue;

		if (nv_crtc->cursor.set_offset)
			nv_crtc->cursor.set_offset(nv_crtc,
						   nv_crtc->cursor.nvbo->offset);
		nv_crtc->cursor.set_pos(nv_crtc, nv_crtc->cursor_saved_x,
						 nv_crtc->cursor_saved_y);
	}

	return 0;
}

static void
nv04_display_destroy(struct drm_device *dev)
{
	struct nv04_display *disp = nv04_display(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_encoder *encoder;
	struct nouveau_crtc *nv_crtc;

	/* Restore state */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, base.base.head)
		encoder->enc_restore(&encoder->base.base);

	list_for_each_entry(nv_crtc, &dev->mode_config.crtc_list, base.head)
		nv_crtc->restore(&nv_crtc->base);

	nouveau_hw_save_vga_fonts(dev, 0);

	nvif_event_dtor(&disp->flip);

	nouveau_display(dev)->priv = NULL;
	vfree(disp);

	nvif_object_unmap(&drm->client.device.object);
}

int
nv04_display_create(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_i2c *i2c = nvxx_i2c(&drm->client.device);
	struct dcb_table *dcb = &drm->vbios.dcb;
	struct drm_connector *connector, *ct;
	struct drm_encoder *encoder;
	struct nouveau_encoder *nv_encoder;
	struct nouveau_crtc *crtc;
	struct nv04_display *disp;
	int i, ret;

	disp = vzalloc(sizeof(*disp));
	if (!disp)
		return -ENOMEM;

	disp->drm = drm;

	nvif_object_map(&drm->client.device.object, NULL, 0);

	nouveau_display(dev)->priv = disp;
	nouveau_display(dev)->dtor = nv04_display_destroy;
	nouveau_display(dev)->init = nv04_display_init;
	nouveau_display(dev)->fini = nv04_display_fini;

	/* Pre-nv50 doesn't support atomic, so don't expose the ioctls */
	dev->driver_features &= ~DRIVER_ATOMIC;

	/* Request page flip completion event. */
	if (drm->channel) {
		ret = nvif_event_ctor(&drm->channel->nvsw, "kmsFlip", 0, nv04_flip_complete,
				      true, NULL, 0, &disp->flip);
		if (ret)
			return ret;
	}

	nouveau_hw_save_vga_fonts(dev, 1);

	nv04_crtc_create(dev, 0);
	if (nv_two_heads(dev))
		nv04_crtc_create(dev, 1);

	for (i = 0; i < dcb->entries; i++) {
		struct dcb_output *dcbent = &dcb->entry[i];

		connector = nouveau_connector_create(dev, dcbent->connector);
		if (IS_ERR(connector))
			continue;

		switch (dcbent->type) {
		case DCB_OUTPUT_ANALOG:
			ret = nv04_dac_create(connector, dcbent);
			break;
		case DCB_OUTPUT_LVDS:
		case DCB_OUTPUT_TMDS:
			ret = nv04_dfp_create(connector, dcbent);
			break;
		case DCB_OUTPUT_TV:
			if (dcbent->location == DCB_LOC_ON_CHIP)
				ret = nv17_tv_create(connector, dcbent);
			else
				ret = nv04_tv_create(connector, dcbent);
			break;
		default:
			NV_WARN(drm, "DCB type %d not known\n", dcbent->type);
			continue;
		}

		if (ret)
			continue;
	}

	list_for_each_entry_safe(connector, ct,
				 &dev->mode_config.connector_list, head) {
		if (!connector->possible_encoders) {
			NV_WARN(drm, "%s has no encoders, removing\n",
				connector->name);
			connector->funcs->destroy(connector);
		}
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
		struct nvkm_i2c_bus *bus =
			nvkm_i2c_bus_find(i2c, nv_encoder->dcb->i2c_index);
		nv_encoder->i2c = bus ? &bus->i2c : NULL;
	}

	/* Save previous state */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, base.head)
		crtc->save(&crtc->base);

	list_for_each_entry(nv_encoder, &dev->mode_config.encoder_list, base.base.head)
		nv_encoder->enc_save(&nv_encoder->base.base);

	nouveau_overlay_init(dev);

	return 0;
}
