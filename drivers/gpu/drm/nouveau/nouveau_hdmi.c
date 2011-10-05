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

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"

static bool
hdmi_sor(struct drm_encoder *encoder)
{
	struct drm_nouveau_private *dev_priv = encoder->dev->dev_private;
	if (dev_priv->chipset < 0xa3)
		return false;
	return true;
}

static void
nouveau_audio_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	int or = nv_encoder->or * 0x800;

	if (hdmi_sor(encoder)) {
		nv_mask(dev, 0x61c448 + or, 0x00000003, 0x00000000);
	}
}

static void
nouveau_audio_mode_set(struct drm_encoder *encoder,
		       struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;
	struct drm_device *dev = encoder->dev;
	u32 or = nv_encoder->or * 0x800;
	int i;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_monitor_audio(nv_connector->edid)) {
		nouveau_audio_disconnect(encoder);
		return;
	}

	if (hdmi_sor(encoder)) {
		nv_mask(dev, 0x61c448 + or, 0x00000001, 0x00000001);

		drm_edid_to_eld(&nv_connector->base, nv_connector->edid);
		if (nv_connector->base.eld[0]) {
			u8 *eld = nv_connector->base.eld;
			for (i = 0; i < eld[2] * 4; i++)
				nv_wr32(dev, 0x61c440 + or, (i << 8) | eld[i]);
			for (i = eld[2] * 4; i < 0x60; i++)
				nv_wr32(dev, 0x61c440 + or, (i << 8) | 0x00);
			nv_mask(dev, 0x61c448 + or, 0x00000002, 0x00000002);
		}
	}
}

static void
nouveau_hdmi_disconnect(struct drm_encoder *encoder)
{
	nouveau_audio_disconnect(encoder);
}

void
nouveau_hdmi_mode_set(struct drm_encoder *encoder,
		      struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!mode || !nv_connector || !nv_connector->edid ||
	    !drm_detect_hdmi_monitor(nv_connector->edid)) {
		nouveau_hdmi_disconnect(encoder);
		return;
	}

	nouveau_audio_mode_set(encoder, mode);
}
