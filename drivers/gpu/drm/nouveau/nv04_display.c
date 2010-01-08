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

#include "drmP.h"
#include "drm.h"
#include "drm_crtc_helper.h"

#include "nouveau_drv.h"
#include "nouveau_fb.h"
#include "nouveau_hw.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"

#define MULTIPLE_ENCODERS(e) (e & (e - 1))

static void
nv04_display_store_initial_head_owner(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->chipset != 0x11) {
		dev_priv->crtc_owner = NVReadVgaCrtc(dev, 0, NV_CIO_CRE_44);
		goto ownerknown;
	}

	/* reading CR44 is broken on nv11, so we attempt to infer it */
	if (nvReadMC(dev, NV_PBUS_DEBUG_1) & (1 << 28))	/* heads tied, restore both */
		dev_priv->crtc_owner = 0x4;
	else {
		uint8_t slaved_on_A, slaved_on_B;
		bool tvA = false;
		bool tvB = false;

		NVLockVgaCrtcs(dev, false);

		slaved_on_B = NVReadVgaCrtc(dev, 1, NV_CIO_CRE_PIXEL_INDEX) &
									0x80;
		if (slaved_on_B)
			tvB = !(NVReadVgaCrtc(dev, 1, NV_CIO_CRE_LCD__INDEX) &
					MASK(NV_CIO_CRE_LCD_LCD_SELECT));

		slaved_on_A = NVReadVgaCrtc(dev, 0, NV_CIO_CRE_PIXEL_INDEX) &
									0x80;
		if (slaved_on_A)
			tvA = !(NVReadVgaCrtc(dev, 0, NV_CIO_CRE_LCD__INDEX) &
					MASK(NV_CIO_CRE_LCD_LCD_SELECT));

		NVLockVgaCrtcs(dev, true);

		if (slaved_on_A && !tvA)
			dev_priv->crtc_owner = 0x0;
		else if (slaved_on_B && !tvB)
			dev_priv->crtc_owner = 0x3;
		else if (slaved_on_A)
			dev_priv->crtc_owner = 0x0;
		else if (slaved_on_B)
			dev_priv->crtc_owner = 0x3;
		else
			dev_priv->crtc_owner = 0x0;
	}

ownerknown:
	NV_INFO(dev, "Initial CRTC_OWNER is %d\n", dev_priv->crtc_owner);

	/* we need to ensure the heads are not tied henceforth, or reading any
	 * 8 bit reg on head B will fail
	 * setting a single arbitrary head solves that */
	NVSetOwner(dev, 0);
}

int
nv04_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct parsed_dcb *dcb = dev_priv->vbios->dcb;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;
	uint16_t connector[16] = { 0 };
	int i, ret;

	NV_DEBUG_KMS(dev, "\n");

	if (nv_two_heads(dev))
		nv04_display_store_initial_head_owner(dev);
	nouveau_hw_save_vga_fonts(dev, 1);

	drm_mode_config_init(dev);
	drm_mode_create_scaling_mode_property(dev);
	drm_mode_create_dithering_property(dev);

	dev->mode_config.funcs = (void *)&nouveau_mode_config_funcs;

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	switch (dev_priv->card_type) {
	case NV_04:
		dev->mode_config.max_width = 2048;
		dev->mode_config.max_height = 2048;
		break;
	default:
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
		break;
	}

	dev->mode_config.fb_base = dev_priv->fb_phys;

	nv04_crtc_create(dev, 0);
	if (nv_two_heads(dev))
		nv04_crtc_create(dev, 1);

	for (i = 0; i < dcb->entries; i++) {
		struct dcb_entry *dcbent = &dcb->entry[i];

		switch (dcbent->type) {
		case OUTPUT_ANALOG:
			ret = nv04_dac_create(dev, dcbent);
			break;
		case OUTPUT_LVDS:
		case OUTPUT_TMDS:
			ret = nv04_dfp_create(dev, dcbent);
			break;
		case OUTPUT_TV:
			if (dcbent->location == DCB_LOC_ON_CHIP)
				ret = nv17_tv_create(dev, dcbent);
			else
				ret = nv04_tv_create(dev, dcbent);
			break;
		default:
			NV_WARN(dev, "DCB type %d not known\n", dcbent->type);
			continue;
		}

		if (ret)
			continue;

		connector[dcbent->connector] |= (1 << dcbent->type);
	}

	for (i = 0; i < dcb->entries; i++) {
		struct dcb_entry *dcbent = &dcb->entry[i];
		uint16_t encoders;
		int type;

		encoders = connector[dcbent->connector];
		if (!(encoders & (1 << dcbent->type)))
			continue;
		connector[dcbent->connector] = 0;

		switch (dcbent->type) {
		case OUTPUT_ANALOG:
			if (!MULTIPLE_ENCODERS(encoders))
				type = DRM_MODE_CONNECTOR_VGA;
			else
				type = DRM_MODE_CONNECTOR_DVII;
			break;
		case OUTPUT_TMDS:
			if (!MULTIPLE_ENCODERS(encoders))
				type = DRM_MODE_CONNECTOR_DVID;
			else
				type = DRM_MODE_CONNECTOR_DVII;
			break;
		case OUTPUT_LVDS:
			type = DRM_MODE_CONNECTOR_LVDS;
#if 0
			/* don't create i2c adapter when lvds ddc not allowed */
			if (dcbent->lvdsconf.use_straps_for_mode ||
			    dev_priv->vbios->fp_no_ddc)
				i2c_index = 0xf;
#endif
			break;
		case OUTPUT_TV:
			type = DRM_MODE_CONNECTOR_TV;
			break;
		default:
			type = DRM_MODE_CONNECTOR_Unknown;
			continue;
		}

		nouveau_connector_create(dev, dcbent->connector, type);
	}

	/* Save previous state */
	NVLockVgaCrtcs(dev, false);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		crtc->funcs->save(crtc);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct drm_encoder_helper_funcs *func = encoder->helper_private;

		func->save(encoder);
	}

	return 0;
}

void
nv04_display_destroy(struct drm_device *dev)
{
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;

	NV_DEBUG_KMS(dev, "\n");

	/* Turn every CRTC off. */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct drm_mode_set modeset = {
			.crtc = crtc,
		};

		crtc->funcs->set_config(&modeset);
	}

	/* Restore state */
	NVLockVgaCrtcs(dev, false);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct drm_encoder_helper_funcs *func = encoder->helper_private;

		func->restore(encoder);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		crtc->funcs->restore(crtc);

	drm_mode_config_cleanup(dev);

	nouveau_hw_save_vga_fonts(dev, 0);
}

void
nv04_display_restore(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;

	NVLockVgaCrtcs(dev, false);

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

	if (nv_two_heads(dev)) {
		NV_INFO(dev, "Restoring CRTC_OWNER to %d.\n",
			dev_priv->crtc_owner);
		NVSetOwner(dev, dev_priv->crtc_owner);
	}

	NVLockVgaCrtcs(dev, true);
}

