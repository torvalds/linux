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

#include <core/object.h>
#include <core/class.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "nouveau_drm.h"
#include "nouveau_reg.h"
#include "hw.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"

#include <subdev/i2c.h>

int
nv04_display_early_init(struct drm_device *dev)
{
	/* ensure vblank interrupts are off, they can't be enabled until
	 * drm_vblank has been initialised
	 */
	NVWriteCRTC(dev, 0, NV_PCRTC_INTR_EN_0, 0);
	if (nv_two_heads(dev))
		NVWriteCRTC(dev, 1, NV_PCRTC_INTR_EN_0, 0);

	return 0;
}

void
nv04_display_late_takedown(struct drm_device *dev)
{
}

int
nv04_display_create(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_i2c *i2c = nouveau_i2c(drm->device);
	struct dcb_table *dcb = &drm->vbios.dcb;
	struct drm_connector *connector, *ct;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;
	struct nv04_display *disp;
	int i, ret;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;

	nouveau_display(dev)->priv = disp;
	nouveau_display(dev)->dtor = nv04_display_destroy;
	nouveau_display(dev)->init = nv04_display_init;
	nouveau_display(dev)->fini = nv04_display_fini;

	nouveau_hw_save_vga_fonts(dev, 1);

	ret = nouveau_object_new(nv_object(drm), NVDRM_DEVICE, 0xd1500000,
				 NV04_DISP_CLASS, NULL, 0, &disp->core);
	if (ret)
		return ret;

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
		if (!connector->encoder_ids[0]) {
			NV_WARN(drm, "%s has no encoders, removing\n",
				drm_get_connector_name(connector));
			connector->funcs->destroy(connector);
		}
	}

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
		nv_encoder->i2c = i2c->find(i2c, nv_encoder->dcb->i2c_index);
	}

	/* Save previous state */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		crtc->funcs->save(crtc);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct drm_encoder_helper_funcs *func = encoder->helper_private;

		func->save(encoder);
	}

	nouveau_overlay_init(dev);

	return 0;
}

void
nv04_display_destroy(struct drm_device *dev)
{
	struct nv04_display *disp = nv04_display(dev);
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;

	/* Turn every CRTC off. */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct drm_mode_set modeset = {
			.crtc = crtc,
		};

		drm_mode_set_config_internal(&modeset);
	}

	/* Restore state */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct drm_encoder_helper_funcs *func = encoder->helper_private;

		func->restore(encoder);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		crtc->funcs->restore(crtc);

	nouveau_hw_save_vga_fonts(dev, 0);

	nouveau_display(dev)->priv = NULL;
	kfree(disp);
}

int
nv04_display_init(struct drm_device *dev)
{
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;

	/* meh.. modeset apparently doesn't setup all the regs and depends
	 * on pre-existing state, for now load the state of the card *before*
	 * nouveau was loaded, and then do a modeset.
	 *
	 * best thing to do probably is to make save/restore routines not
	 * save/restore "pre-load" state, but more general so we can save
	 * on suspend too.
	 */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct drm_encoder_helper_funcs *func = encoder->helper_private;

		func->restore(encoder);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		crtc->funcs->restore(crtc);

	return 0;
}

void
nv04_display_fini(struct drm_device *dev)
{
	/* disable vblank interrupts */
	NVWriteCRTC(dev, 0, NV_PCRTC_INTR_EN_0, 0);
	if (nv_two_heads(dev))
		NVWriteCRTC(dev, 1, NV_PCRTC_INTR_EN_0, 0);
}
