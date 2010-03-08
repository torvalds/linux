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

#include "drmP.h"
#include "drm_crtc_helper.h"

#define NOUVEAU_DMA_DEBUG (nouveau_reg_debug & NOUVEAU_REG_DEBUG_EVO)
#include "nouveau_reg.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"
#include "nouveau_crtc.h"
#include "nv50_display.h"

static void
nv50_sor_disconnect(struct nouveau_encoder *nv_encoder)
{
	struct drm_device *dev = to_drm_encoder(nv_encoder)->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *evo = dev_priv->evo;
	int ret;

	NV_DEBUG_KMS(dev, "Disconnecting SOR %d\n", nv_encoder->or);

	ret = RING_SPACE(evo, 2);
	if (ret) {
		NV_ERROR(dev, "no space while disconnecting SOR\n");
		return;
	}
	BEGIN_RING(evo, 0, NV50_EVO_SOR(nv_encoder->or, MODE_CTRL), 1);
	OUT_RING(evo, 0);
}

static void
nv50_sor_dp_link_train(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct bit_displayport_encoder_table *dpe;
	int dpe_headerlen;

	dpe = nouveau_bios_dp_table(dev, nv_encoder->dcb, &dpe_headerlen);
	if (!dpe) {
		NV_ERROR(dev, "SOR-%d: no DP encoder table!\n", nv_encoder->or);
		return;
	}

	if (dpe->script0) {
		NV_DEBUG_KMS(dev, "SOR-%d: running DP script 0\n", nv_encoder->or);
		nouveau_bios_run_init_table(dev, le16_to_cpu(dpe->script0),
					    nv_encoder->dcb);
	}

	if (!nouveau_dp_link_train(encoder))
		NV_ERROR(dev, "SOR-%d: link training failed\n", nv_encoder->or);

	if (dpe->script1) {
		NV_DEBUG_KMS(dev, "SOR-%d: running DP script 1\n", nv_encoder->or);
		nouveau_bios_run_init_table(dev, le16_to_cpu(dpe->script1),
					    nv_encoder->dcb);
	}
}

static void
nv50_sor_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_encoder *enc;
	uint32_t val;
	int or = nv_encoder->or;

	NV_DEBUG_KMS(dev, "or %d mode %d\n", or, mode);

	nv_encoder->last_dpms = mode;
	list_for_each_entry(enc, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nvenc = nouveau_encoder(enc);

		if (nvenc == nv_encoder ||
		    nvenc->disconnect != nv50_sor_disconnect ||
		    nvenc->dcb->or != nv_encoder->dcb->or)
			continue;

		if (nvenc->last_dpms == DRM_MODE_DPMS_ON)
			return;
	}

	/* wait for it to be done */
	if (!nv_wait(NV50_PDISPLAY_SOR_DPMS_CTRL(or),
		     NV50_PDISPLAY_SOR_DPMS_CTRL_PENDING, 0)) {
		NV_ERROR(dev, "timeout: SOR_DPMS_CTRL_PENDING(%d) == 0\n", or);
		NV_ERROR(dev, "SOR_DPMS_CTRL(%d) = 0x%08x\n", or,
			 nv_rd32(dev, NV50_PDISPLAY_SOR_DPMS_CTRL(or)));
	}

	val = nv_rd32(dev, NV50_PDISPLAY_SOR_DPMS_CTRL(or));

	if (mode == DRM_MODE_DPMS_ON)
		val |= NV50_PDISPLAY_SOR_DPMS_CTRL_ON;
	else
		val &= ~NV50_PDISPLAY_SOR_DPMS_CTRL_ON;

	nv_wr32(dev, NV50_PDISPLAY_SOR_DPMS_CTRL(or), val |
		NV50_PDISPLAY_SOR_DPMS_CTRL_PENDING);
	if (!nv_wait(NV50_PDISPLAY_SOR_DPMS_STATE(or),
		     NV50_PDISPLAY_SOR_DPMS_STATE_WAIT, 0)) {
		NV_ERROR(dev, "timeout: SOR_DPMS_STATE_WAIT(%d) == 0\n", or);
		NV_ERROR(dev, "SOR_DPMS_STATE(%d) = 0x%08x\n", or,
			 nv_rd32(dev, NV50_PDISPLAY_SOR_DPMS_STATE(or)));
	}

	if (nv_encoder->dcb->type == OUTPUT_DP && mode == DRM_MODE_DPMS_ON)
		nv50_sor_dp_link_train(encoder);
}

static void
nv50_sor_save(struct drm_encoder *encoder)
{
	NV_ERROR(encoder->dev, "!!\n");
}

static void
nv50_sor_restore(struct drm_encoder *encoder)
{
	NV_ERROR(encoder->dev, "!!\n");
}

static bool
nv50_sor_mode_fixup(struct drm_encoder *encoder, struct drm_display_mode *mode,
		    struct drm_display_mode *adjusted_mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *connector;

	NV_DEBUG_KMS(encoder->dev, "or %d\n", nv_encoder->or);

	connector = nouveau_encoder_connector_get(nv_encoder);
	if (!connector) {
		NV_ERROR(encoder->dev, "Encoder has no connector\n");
		return false;
	}

	if (connector->scaling_mode != DRM_MODE_SCALE_NONE &&
	     connector->native_mode) {
		int id = adjusted_mode->base.id;
		*adjusted_mode = *connector->native_mode;
		adjusted_mode->base.id = id;
	}

	return true;
}

static void
nv50_sor_prepare(struct drm_encoder *encoder)
{
}

static void
nv50_sor_commit(struct drm_encoder *encoder)
{
}

static void
nv50_sor_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		  struct drm_display_mode *adjusted_mode)
{
	struct drm_nouveau_private *dev_priv = encoder->dev->dev_private;
	struct nouveau_channel *evo = dev_priv->evo;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct nouveau_crtc *crtc = nouveau_crtc(encoder->crtc);
	uint32_t mode_ctl = 0;
	int ret;

	NV_DEBUG_KMS(dev, "or %d\n", nv_encoder->or);

	nv50_sor_dpms(encoder, DRM_MODE_DPMS_ON);

	switch (nv_encoder->dcb->type) {
	case OUTPUT_TMDS:
		if (nv_encoder->dcb->sorconf.link & 1) {
			if (adjusted_mode->clock < 165000)
				mode_ctl = 0x0100;
			else
				mode_ctl = 0x0500;
		} else
			mode_ctl = 0x0200;
		break;
	case OUTPUT_DP:
		mode_ctl |= 0x00050000;
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

	if (adjusted_mode->flags & DRM_MODE_FLAG_NHSYNC)
		mode_ctl |= NV50_EVO_SOR_MODE_CTRL_NHSYNC;

	if (adjusted_mode->flags & DRM_MODE_FLAG_NVSYNC)
		mode_ctl |= NV50_EVO_SOR_MODE_CTRL_NVSYNC;

	ret = RING_SPACE(evo, 2);
	if (ret) {
		NV_ERROR(dev, "no space while connecting SOR\n");
		return;
	}
	BEGIN_RING(evo, 0, NV50_EVO_SOR(nv_encoder->or, MODE_CTRL), 1);
	OUT_RING(evo, mode_ctl);
}

static const struct drm_encoder_helper_funcs nv50_sor_helper_funcs = {
	.dpms = nv50_sor_dpms,
	.save = nv50_sor_save,
	.restore = nv50_sor_restore,
	.mode_fixup = nv50_sor_mode_fixup,
	.prepare = nv50_sor_prepare,
	.commit = nv50_sor_commit,
	.mode_set = nv50_sor_mode_set,
	.detect = NULL
};

static void
nv50_sor_destroy(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);

	if (!encoder)
		return;

	NV_DEBUG_KMS(encoder->dev, "\n");

	drm_encoder_cleanup(encoder);

	kfree(nv_encoder);
}

static const struct drm_encoder_funcs nv50_sor_encoder_funcs = {
	.destroy = nv50_sor_destroy,
};

int
nv50_sor_create(struct drm_device *dev, struct dcb_entry *entry)
{
	struct nouveau_encoder *nv_encoder = NULL;
	struct drm_encoder *encoder;
	bool dum;
	int type;

	NV_DEBUG_KMS(dev, "\n");

	switch (entry->type) {
	case OUTPUT_TMDS:
		NV_INFO(dev, "Detected a TMDS output\n");
		type = DRM_MODE_ENCODER_TMDS;
		break;
	case OUTPUT_LVDS:
		NV_INFO(dev, "Detected a LVDS output\n");
		type = DRM_MODE_ENCODER_LVDS;

		if (nouveau_bios_parse_lvds_table(dev, 0, &dum, &dum)) {
			NV_ERROR(dev, "Failed parsing LVDS table\n");
			return -EINVAL;
		}
		break;
	case OUTPUT_DP:
		NV_INFO(dev, "Detected a DP output\n");
		type = DRM_MODE_ENCODER_TMDS;
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

	nv_encoder->disconnect = nv50_sor_disconnect;

	drm_encoder_init(dev, encoder, &nv50_sor_encoder_funcs, type);
	drm_encoder_helper_add(encoder, &nv50_sor_helper_funcs);

	encoder->possible_crtcs = entry->heads;
	encoder->possible_clones = 0;

	return 0;
}
