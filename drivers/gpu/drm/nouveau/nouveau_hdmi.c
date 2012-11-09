/*
 * Copyright 2011 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */

#include <drm/drmP.h>
#include "nouveau_drm.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"

#include <core/class.h>

#include "nv50_display.h"

static bool
hdmi_sor(struct drm_encoder *encoder)
{
	struct nouveau_drm *drm = nouveau_drm(encoder->dev);
	if (nv_device(drm->device)->chipset <  0xa3 ||
	    nv_device(drm->device)->chipset == 0xaa ||
	    nv_device(drm->device)->chipset == 0xac)
		return false;
	return true;
}

static void
nouveau_audio_disconnect(struct drm_encoder *encoder)
{
	struct nv50_display *priv = nv50_display(encoder->dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	u32 or = nv_encoder->or;

	if (hdmi_sor(encoder))
		nv_exec(priv->core, NVA3_DISP_SOR_HDA_ELD + or, NULL, 0);
}

static void
nouveau_audio_mode_set(struct drm_encoder *encoder,
		       struct drm_display_mode *mode)
{
	struct nv50_display *priv = nv50_display(encoder->dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_monitor_audio(nv_connector->edid)) {
		nouveau_audio_disconnect(encoder);
		return;
	}

	if (hdmi_sor(encoder)) {
		drm_edid_to_eld(&nv_connector->base, nv_connector->edid);
		nv_exec(priv->core, NVA3_DISP_SOR_HDA_ELD + nv_encoder->or,
				    nv_connector->base.eld,
				    nv_connector->base.eld[2] * 4);
	}
}

static void
nouveau_hdmi_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(nv_encoder->crtc);
	struct nv50_display *priv = nv50_display(encoder->dev);
	const u32 moff = (nv_crtc->index << 3) | nv_encoder->or;

	nouveau_audio_disconnect(encoder);

	nv_call(priv->core, NV84_DISP_SOR_HDMI_PWR + moff, 0x00000000);
}

void
nouveau_hdmi_mode_set(struct drm_encoder *encoder,
		      struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(nv_encoder->crtc);
	struct nv50_display *priv = nv50_display(encoder->dev);
	const u32 moff = (nv_crtc->index << 3) | nv_encoder->or;
	struct nouveau_connector *nv_connector;
	u32 max_ac_packet, rekey;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!mode || !nv_connector || !nv_connector->edid ||
	    !drm_detect_hdmi_monitor(nv_connector->edid)) {
		nouveau_hdmi_disconnect(encoder);
		return;
	}

	/* value matches nvidia binary driver, and tegra constant */
	rekey = 56;

	max_ac_packet  = mode->htotal - mode->hdisplay;
	max_ac_packet -= rekey;
	max_ac_packet -= 18; /* constant from tegra */
	max_ac_packet /= 32;

	nv_call(priv->core, NV84_DISP_SOR_HDMI_PWR + moff,
			    NV84_DISP_SOR_HDMI_PWR_STATE_ON |
			    (max_ac_packet << 16) | rekey);

	nouveau_audio_mode_set(encoder, mode);
}
