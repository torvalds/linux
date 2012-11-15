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

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#define NOUVEAU_DMA_DEBUG (nouveau_reg_debug & NOUVEAU_REG_DEBUG_EVO)
#include "nouveau_reg.h"
#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"
#include "nouveau_crtc.h"
#include "nv50_display.h"

#include <core/class.h>

#include <subdev/timer.h>

static void
nv50_sor_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
	struct drm_device *dev = encoder->dev;
	struct nouveau_channel *evo = nv50_display(dev)->master;
	int ret;

	if (!nv_encoder->crtc)
		return;
	nv50_crtc_blank(nouveau_crtc(nv_encoder->crtc), true);

	NV_DEBUG(drm, "Disconnecting SOR %d\n", nv_encoder->or);

	ret = RING_SPACE(evo, 4);
	if (ret) {
		NV_ERROR(drm, "no space while disconnecting SOR\n");
		return;
	}
	BEGIN_NV04(evo, 0, NV50_EVO_SOR(nv_encoder->or, MODE_CTRL), 1);
	OUT_RING  (evo, 0);
	BEGIN_NV04(evo, 0, NV50_EVO_UPDATE, 1);
	OUT_RING  (evo, 0);

	nouveau_hdmi_mode_set(encoder, NULL);

	nv_encoder->crtc = NULL;
	nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;
}

static void
nv50_sor_dpms(struct drm_encoder *encoder, int mode)
{
	struct nv50_display *priv = nv50_display(encoder->dev);
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
	struct drm_device *dev = encoder->dev;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_encoder *enc;
	int or = nv_encoder->or;

	NV_DEBUG(drm, "or %d type %d mode %d\n", or, nv_encoder->dcb->type, mode);

	nv_encoder->last_dpms = mode;
	list_for_each_entry(enc, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nvenc = nouveau_encoder(enc);

		if (nvenc == nv_encoder ||
		    (nvenc->dcb->type != DCB_OUTPUT_TMDS &&
		     nvenc->dcb->type != DCB_OUTPUT_LVDS &&
		     nvenc->dcb->type != DCB_OUTPUT_DP) ||
		    nvenc->dcb->or != nv_encoder->dcb->or)
			continue;

		if (nvenc->last_dpms == DRM_MODE_DPMS_ON)
			return;
	}

	nv_call(priv->core, NV50_DISP_SOR_PWR + or, (mode == DRM_MODE_DPMS_ON));

	if (nv_encoder->dcb->type == DCB_OUTPUT_DP)
		nouveau_dp_dpms(encoder, mode, nv_encoder->dp.datarate, priv->core);
}

static void
nv50_sor_save(struct drm_encoder *encoder)
{
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
	NV_ERROR(drm, "!!\n");
}

static void
nv50_sor_restore(struct drm_encoder *encoder)
{
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
	NV_ERROR(drm, "!!\n");
}

static bool
nv50_sor_mode_fixup(struct drm_encoder *encoder,
		    const struct drm_display_mode *mode,
		    struct drm_display_mode *adjusted_mode)
{
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *connector;

	NV_DEBUG(drm, "or %d\n", nv_encoder->or);

	connector = nouveau_encoder_connector_get(nv_encoder);
	if (!connector) {
		NV_ERROR(drm, "Encoder has no connector\n");
		return false;
	}

	if (connector->scaling_mode != DRM_MODE_SCALE_NONE &&
	     connector->native_mode)
		drm_mode_copy(adjusted_mode, connector->native_mode);

	return true;
}

static void
nv50_sor_prepare(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	nv50_sor_disconnect(encoder);
	if (nv_encoder->dcb->type == DCB_OUTPUT_DP) {
		/* avoid race between link training and supervisor intr */
		nv50_display_sync(encoder->dev);
	}
}

static void
nv50_sor_commit(struct drm_encoder *encoder)
{
}

static void
nv50_sor_mode_set(struct drm_encoder *encoder, struct drm_display_mode *umode,
		  struct drm_display_mode *mode)
{
	struct nouveau_channel *evo = nv50_display(encoder->dev)->master;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
	struct nouveau_crtc *crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector;
	struct nv50_display *disp = nv50_display(encoder->dev);
	struct nvbios *bios = &drm->vbios;
	uint32_t mode_ctl = 0, script;
	int ret;

	NV_DEBUG(drm, "or %d type %d -> crtc %d\n",
		     nv_encoder->or, nv_encoder->dcb->type, crtc->index);
	nv_encoder->crtc = encoder->crtc;

	switch (nv_encoder->dcb->type) {
	case DCB_OUTPUT_TMDS:
		if (nv_encoder->dcb->sorconf.link & 1) {
			if (mode->clock < 165000)
				mode_ctl = 0x0100;
			else
				mode_ctl = 0x0500;
		} else
			mode_ctl = 0x0200;

		nouveau_hdmi_mode_set(encoder, mode);
		break;
	case DCB_OUTPUT_LVDS:
		script = 0x0000;
		if (bios->fp_no_ddc) {
			if (bios->fp.dual_link)
				script |= 0x0100;
			if (bios->fp.if_is_24bit)
				script |= 0x0200;
		} else {
			/* determine number of lvds links */
			nv_connector = nouveau_encoder_connector_get(nv_encoder);
			if (nv_connector && nv_connector->edid &&
			    nv_connector->type == DCB_CONNECTOR_LVDS_SPWG) {
				/* http://www.spwg.org */
				if (((u8 *)nv_connector->edid)[121] == 2)
					script |= 0x0100;
			} else
			if (mode->clock >= bios->fp.duallink_transition_clk) {
				script |= 0x0100;
			}

			/* determine panel depth */
			if (script & 0x0100) {
				if (bios->fp.strapless_is_24bit & 2)
					script |= 0x0200;
			} else {
				if (bios->fp.strapless_is_24bit & 1)
					script |= 0x0200;
			}

			if (nv_connector && nv_connector->edid &&
			    (nv_connector->edid->revision >= 4) &&
			    (nv_connector->edid->input & 0x70) >= 0x20)
				script |= 0x0200;
		}

		nv_call(disp->core, NV50_DISP_SOR_LVDS_SCRIPT + nv_encoder->or, script);
		break;
	case DCB_OUTPUT_DP:
		nv_connector = nouveau_encoder_connector_get(nv_encoder);
		if (nv_connector && nv_connector->base.display_info.bpc == 6) {
			nv_encoder->dp.datarate = mode->clock * 18 / 8;
			mode_ctl |= 0x00020000;
		} else {
			nv_encoder->dp.datarate = mode->clock * 24 / 8;
			mode_ctl |= 0x00050000;
		}

		if (nv_encoder->dcb->sorconf.link & 1)
			mode_ctl |= 0x00000800;
		else
			mode_ctl |= 0x00000900;
		break;
	default:
		break;
	}

	if (crtc->index == 1)
		mode_ctl |= NV50_EVO_SOR_MODE_CTRL_CRTC1;
	else
		mode_ctl |= NV50_EVO_SOR_MODE_CTRL_CRTC0;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		mode_ctl |= NV50_EVO_SOR_MODE_CTRL_NHSYNC;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		mode_ctl |= NV50_EVO_SOR_MODE_CTRL_NVSYNC;

	nv50_sor_dpms(encoder, DRM_MODE_DPMS_ON);

	ret = RING_SPACE(evo, 2);
	if (ret) {
		NV_ERROR(drm, "no space while connecting SOR\n");
		nv_encoder->crtc = NULL;
		return;
	}
	BEGIN_NV04(evo, 0, NV50_EVO_SOR(nv_encoder->or, MODE_CTRL), 1);
	OUT_RING(evo, mode_ctl);
}

static struct drm_crtc *
nv50_sor_crtc_get(struct drm_encoder *encoder)
{
	return nouveau_encoder(encoder)->crtc;
}

static const struct drm_encoder_helper_funcs nv50_sor_helper_funcs = {
	.dpms = nv50_sor_dpms,
	.save = nv50_sor_save,
	.restore = nv50_sor_restore,
	.mode_fixup = nv50_sor_mode_fixup,
	.prepare = nv50_sor_prepare,
	.commit = nv50_sor_commit,
	.mode_set = nv50_sor_mode_set,
	.get_crtc = nv50_sor_crtc_get,
	.detect = NULL,
	.disable = nv50_sor_disconnect
};

static void
nv50_sor_destroy(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);

	drm_encoder_cleanup(encoder);
	kfree(nv_encoder);
}

static const struct drm_encoder_funcs nv50_sor_encoder_funcs = {
	.destroy = nv50_sor_destroy,
};

int
nv50_sor_create(struct drm_connector *connector, struct dcb_output *entry)
{
	struct nouveau_encoder *nv_encoder = NULL;
	struct drm_device *dev = connector->dev;
	struct drm_encoder *encoder;
	int type;

	switch (entry->type) {
	case DCB_OUTPUT_TMDS:
	case DCB_OUTPUT_DP:
		type = DRM_MODE_ENCODER_TMDS;
		break;
	case DCB_OUTPUT_LVDS:
		type = DRM_MODE_ENCODER_LVDS;
		break;
	default:
		return -EINVAL;
	}

	nv_encoder = kzalloc(sizeof(*nv_encoder), GFP_KERNEL);
	if (!nv_encoder)
		return -ENOMEM;
	encoder = to_drm_encoder(nv_encoder);

	nv_encoder->dcb = entry;
	nv_encoder->or = ffs(entry->or) - 1;
	nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;

	drm_encoder_init(dev, encoder, &nv50_sor_encoder_funcs, type);
	drm_encoder_helper_add(encoder, &nv50_sor_helper_funcs);

	encoder->possible_crtcs = entry->heads;
	encoder->possible_clones = 0;

	drm_mode_connector_attach_encoder(connector, encoder);
	return 0;
}
