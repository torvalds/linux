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

static inline u32
hdmi_base(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(nv_encoder->crtc);
	if (!hdmi_sor(encoder))
		return 0x616500 + (nv_crtc->index * 0x800);
	return 0x61c500 + (nv_encoder->or * 0x800);
}

static void
hdmi_wr32(struct drm_encoder *encoder, u32 reg, u32 val)
{
	struct nouveau_device *device = nouveau_dev(encoder->dev);
	nv_wr32(device, hdmi_base(encoder) + reg, val);
}

static u32
hdmi_rd32(struct drm_encoder *encoder, u32 reg)
{
	struct nouveau_device *device = nouveau_dev(encoder->dev);
	return nv_rd32(device, hdmi_base(encoder) + reg);
}

static u32
hdmi_mask(struct drm_encoder *encoder, u32 reg, u32 mask, u32 val)
{
	u32 tmp = hdmi_rd32(encoder, reg);
	hdmi_wr32(encoder, reg, (tmp & ~mask) | val);
	return tmp;
}

static void
nouveau_audio_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_device *device = nouveau_dev(encoder->dev);
	u32 or = nv_encoder->or * 0x800;

	if (hdmi_sor(encoder))
		nv_mask(device, 0x61c448 + or, 0x00000003, 0x00000000);
}

static void
nouveau_audio_mode_set(struct drm_encoder *encoder,
		       struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_device *device = nouveau_dev(encoder->dev);
	struct nouveau_connector *nv_connector;
	u32 or = nv_encoder->or * 0x800;
	int i;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_monitor_audio(nv_connector->edid)) {
		nouveau_audio_disconnect(encoder);
		return;
	}

	if (hdmi_sor(encoder)) {
		nv_mask(device, 0x61c448 + or, 0x00000001, 0x00000001);

		drm_edid_to_eld(&nv_connector->base, nv_connector->edid);
		if (nv_connector->base.eld[0]) {
			u8 *eld = nv_connector->base.eld;
			for (i = 0; i < eld[2] * 4; i++)
				nv_wr32(device, 0x61c440 + or, (i << 8) | eld[i]);
			for (i = eld[2] * 4; i < 0x60; i++)
				nv_wr32(device, 0x61c440 + or, (i << 8) | 0x00);
			nv_mask(device, 0x61c448 + or, 0x00000002, 0x00000002);
		}
	}
}

static void
nouveau_hdmi_infoframe(struct drm_encoder *encoder, u32 ctrl, u8 *frame)
{
	/* calculate checksum for the infoframe */
	u8 sum = 0, i;
	for (i = 0; i < frame[2]; i++)
		sum += frame[i];
	frame[3] = 256 - sum;

	/* disable infoframe, and write header */
	hdmi_mask(encoder, ctrl + 0x00, 0x00000001, 0x00000000);
	hdmi_wr32(encoder, ctrl + 0x08, *(u32 *)frame & 0xffffff);

	/* register scans tell me the audio infoframe has only one set of
	 * subpack regs, according to tegra (gee nvidia, it'd be nice if we
	 * could get those docs too!), the hdmi block pads out the rest of
	 * the packet on its own.
	 */
	if (ctrl == 0x020)
		frame[2] = 6;

	/* write out checksum and data, weird weird 7 byte register pairs */
	for (i = 0; i < frame[2] + 1; i += 7) {
		u32 rsubpack = ctrl + 0x0c + ((i / 7) * 8);
		u32 *subpack = (u32 *)&frame[3 + i];
		hdmi_wr32(encoder, rsubpack + 0, subpack[0]);
		hdmi_wr32(encoder, rsubpack + 4, subpack[1] & 0xffffff);
	}

	/* enable the infoframe */
	hdmi_mask(encoder, ctrl, 0x00000001, 0x00000001);
}

static void
nouveau_hdmi_video_infoframe(struct drm_encoder *encoder,
			     struct drm_display_mode *mode)
{
	const u8 Y = 0, A = 0, B = 0, S = 0, C = 0, M = 0, R = 0;
	const u8 ITC = 0, EC = 0, Q = 0, SC = 0, VIC = 0, PR = 0;
	const u8 bar_top = 0, bar_bottom = 0, bar_left = 0, bar_right = 0;
	u8 frame[20];

	frame[0x00] = 0x82; /* AVI infoframe */
	frame[0x01] = 0x02; /* version */
	frame[0x02] = 0x0d; /* length */
	frame[0x03] = 0x00;
	frame[0x04] = (Y << 5) | (A << 4) | (B << 2) | S;
	frame[0x05] = (C << 6) | (M << 4) | R;
	frame[0x06] = (ITC << 7) | (EC << 4) | (Q << 2) | SC;
	frame[0x07] = VIC;
	frame[0x08] = PR;
	frame[0x09] = bar_top & 0xff;
	frame[0x0a] = bar_top >> 8;
	frame[0x0b] = bar_bottom & 0xff;
	frame[0x0c] = bar_bottom >> 8;
	frame[0x0d] = bar_left & 0xff;
	frame[0x0e] = bar_left >> 8;
	frame[0x0f] = bar_right & 0xff;
	frame[0x10] = bar_right >> 8;
	frame[0x11] = 0x00;
	frame[0x12] = 0x00;
	frame[0x13] = 0x00;

	nouveau_hdmi_infoframe(encoder, 0x020, frame);
}

static void
nouveau_hdmi_audio_infoframe(struct drm_encoder *encoder,
			     struct drm_display_mode *mode)
{
	const u8 CT = 0x00, CC = 0x01, ceaSS = 0x00, SF = 0x00, FMT = 0x00;
	const u8 CA = 0x00, DM_INH = 0, LSV = 0x00;
	u8 frame[12];

	frame[0x00] = 0x84;	/* Audio infoframe */
	frame[0x01] = 0x01;	/* version */
	frame[0x02] = 0x0a;	/* length */
	frame[0x03] = 0x00;
	frame[0x04] = (CT << 4) | CC;
	frame[0x05] = (SF << 2) | ceaSS;
	frame[0x06] = FMT;
	frame[0x07] = CA;
	frame[0x08] = (DM_INH << 7) | (LSV << 3);
	frame[0x09] = 0x00;
	frame[0x0a] = 0x00;
	frame[0x0b] = 0x00;

	nouveau_hdmi_infoframe(encoder, 0x000, frame);
}

static void
nouveau_hdmi_disconnect(struct drm_encoder *encoder)
{
	nouveau_audio_disconnect(encoder);

	/* disable audio and avi infoframes */
	hdmi_mask(encoder, 0x000, 0x00000001, 0x00000000);
	hdmi_mask(encoder, 0x020, 0x00000001, 0x00000000);

	/* disable hdmi */
	hdmi_mask(encoder, 0x0a4, 0x40000000, 0x00000000);
}

void
nouveau_hdmi_mode_set(struct drm_encoder *encoder,
		      struct drm_display_mode *mode)
{
	struct nouveau_device *device = nouveau_dev(encoder->dev);
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;
	u32 max_ac_packet, rekey;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!mode || !nv_connector || !nv_connector->edid ||
	    !drm_detect_hdmi_monitor(nv_connector->edid)) {
		nouveau_hdmi_disconnect(encoder);
		return;
	}

	nouveau_hdmi_video_infoframe(encoder, mode);
	nouveau_hdmi_audio_infoframe(encoder, mode);

	hdmi_mask(encoder, 0x0d0, 0x00070001, 0x00010001); /* SPARE, HW_CTS */
	hdmi_mask(encoder, 0x068, 0x00010101, 0x00000000); /* ACR_CTRL, ?? */
	hdmi_mask(encoder, 0x078, 0x80000000, 0x80000000); /* ACR_0441_ENABLE */

	nv_mask(device, 0x61733c, 0x00100000, 0x00100000); /* RESETF */
	nv_mask(device, 0x61733c, 0x10000000, 0x10000000); /* LOOKUP_EN */
	nv_mask(device, 0x61733c, 0x00100000, 0x00000000); /* !RESETF */

	/* value matches nvidia binary driver, and tegra constant */
	rekey = 56;

	max_ac_packet  = mode->htotal - mode->hdisplay;
	max_ac_packet -= rekey;
	max_ac_packet -= 18; /* constant from tegra */
	max_ac_packet /= 32;

	/* enable hdmi */
	hdmi_mask(encoder, 0x0a4, 0x5f1f003f, 0x40000000 | /* enable */
					      0x1f000000 | /* unknown */
					      max_ac_packet << 16 |
					      rekey);

	nouveau_audio_mode_set(encoder, mode);
}
