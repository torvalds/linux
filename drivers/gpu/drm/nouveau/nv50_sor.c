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

static u32
nv50_sor_dp_lane_map(struct drm_device *dev, struct dcb_entry *dcb, u8 lane)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	static const u8 nvaf[] = { 24, 16, 8, 0 }; /* thanks, apple.. */
	static const u8 nv50[] = { 16, 8, 0, 24 };
	if (dev_priv->chipset == 0xaf)
		return nvaf[lane];
	return nv50[lane];
}

static void
nv50_sor_dp_train_set(struct drm_device *dev, struct dcb_entry *dcb, u8 pattern)
{
	u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	nv_mask(dev, NV50_SOR_DP_CTRL(or, link), 0x0f000000, pattern << 24);
}

static void
nv50_sor_dp_train_adj(struct drm_device *dev, struct dcb_entry *dcb,
		      u8 lane, u8 swing, u8 preem)
{
	u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	u32 shift = nv50_sor_dp_lane_map(dev, dcb, lane);
	u32 mask = 0x000000ff << shift;
	u8 *table, *entry, *config;

	table = nouveau_dp_bios_data(dev, dcb, &entry);
	if (!table || (table[0] != 0x20 && table[0] != 0x21)) {
		NV_ERROR(dev, "PDISP: unsupported DP table for chipset\n");
		return;
	}

	config = entry + table[4];
	while (config[0] != swing || config[1] != preem) {
		config += table[5];
		if (config >= entry + table[4] + entry[4] * table[5])
			return;
	}

	nv_mask(dev, NV50_SOR_DP_UNK118(or, link), mask, config[2] << shift);
	nv_mask(dev, NV50_SOR_DP_UNK120(or, link), mask, config[3] << shift);
	nv_mask(dev, NV50_SOR_DP_UNK130(or, link), 0x0000ff00, config[4] << 8);
}

static void
nv50_sor_dp_link_set(struct drm_device *dev, struct dcb_entry *dcb, int crtc,
		     int link_nr, u32 link_bw, bool enhframe)
{
	u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	u32 dpctrl = nv_rd32(dev, NV50_SOR_DP_CTRL(or, link)) & ~0x001f4000;
	u32 clksor = nv_rd32(dev, 0x614300 + (or * 0x800)) & ~0x000c0000;
	u8 *table, *entry, mask;
	int i;

	table = nouveau_dp_bios_data(dev, dcb, &entry);
	if (!table || (table[0] != 0x20 && table[0] != 0x21)) {
		NV_ERROR(dev, "PDISP: unsupported DP table for chipset\n");
		return;
	}

	entry = ROMPTR(dev, entry[10]);
	if (entry) {
		while (link_bw < ROM16(entry[0]) * 10)
			entry += 4;

		nouveau_bios_run_init_table(dev, ROM16(entry[2]), dcb, crtc);
	}

	dpctrl |= ((1 << link_nr) - 1) << 16;
	if (enhframe)
		dpctrl |= 0x00004000;

	if (link_bw > 162000)
		clksor |= 0x00040000;

	nv_wr32(dev, 0x614300 + (or * 0x800), clksor);
	nv_wr32(dev, NV50_SOR_DP_CTRL(or, link), dpctrl);

	mask = 0;
	for (i = 0; i < link_nr; i++)
		mask |= 1 << (nv50_sor_dp_lane_map(dev, dcb, i) >> 3);
	nv_mask(dev, NV50_SOR_DP_UNK130(or, link), 0x0000000f, mask);
}

static void
nv50_sor_dp_link_get(struct drm_device *dev, u32 or, u32 link, u32 *nr, u32 *bw)
{
	u32 dpctrl = nv_rd32(dev, NV50_SOR_DP_CTRL(or, link)) & 0x000f0000;
	u32 clksor = nv_rd32(dev, 0x614300 + (or * 0x800));
	if (clksor & 0x000c0000)
		*bw = 270000;
	else
		*bw = 162000;

	if      (dpctrl > 0x00030000) *nr = 4;
	else if (dpctrl > 0x00010000) *nr = 2;
	else			      *nr = 1;
}

void
nv50_sor_dp_calc_tu(struct drm_device *dev, int or, int link, u32 clk, u32 bpp)
{
	const u32 symbol = 100000;
	int bestTU = 0, bestVTUi = 0, bestVTUf = 0, bestVTUa = 0;
	int TU, VTUi, VTUf, VTUa;
	u64 link_data_rate, link_ratio, unk;
	u32 best_diff = 64 * symbol;
	u32 link_nr, link_bw, r;

	/* calculate packed data rate for each lane */
	nv50_sor_dp_link_get(dev, or, link, &link_nr, &link_bw);
	link_data_rate = (clk * bpp / 8) / link_nr;

	/* calculate ratio of packed data rate to link symbol rate */
	link_ratio = link_data_rate * symbol;
	r = do_div(link_ratio, link_bw);

	for (TU = 64; TU >= 32; TU--) {
		/* calculate average number of valid symbols in each TU */
		u32 tu_valid = link_ratio * TU;
		u32 calc, diff;

		/* find a hw representation for the fraction.. */
		VTUi = tu_valid / symbol;
		calc = VTUi * symbol;
		diff = tu_valid - calc;
		if (diff) {
			if (diff >= (symbol / 2)) {
				VTUf = symbol / (symbol - diff);
				if (symbol - (VTUf * diff))
					VTUf++;

				if (VTUf <= 15) {
					VTUa  = 1;
					calc += symbol - (symbol / VTUf);
				} else {
					VTUa  = 0;
					VTUf  = 1;
					calc += symbol;
				}
			} else {
				VTUa  = 0;
				VTUf  = min((int)(symbol / diff), 15);
				calc += symbol / VTUf;
			}

			diff = calc - tu_valid;
		} else {
			/* no remainder, but the hw doesn't like the fractional
			 * part to be zero.  decrement the integer part and
			 * have the fraction add a whole symbol back
			 */
			VTUa = 0;
			VTUf = 1;
			VTUi--;
		}

		if (diff < best_diff) {
			best_diff = diff;
			bestTU = TU;
			bestVTUa = VTUa;
			bestVTUf = VTUf;
			bestVTUi = VTUi;
			if (diff == 0)
				break;
		}
	}

	if (!bestTU) {
		NV_ERROR(dev, "DP: unable to find suitable config\n");
		return;
	}

	/* XXX close to vbios numbers, but not right */
	unk  = (symbol - link_ratio) * bestTU;
	unk *= link_ratio;
	r = do_div(unk, symbol);
	r = do_div(unk, symbol);
	unk += 6;

	nv_mask(dev, NV50_SOR_DP_CTRL(or, link), 0x000001fc, bestTU << 2);
	nv_mask(dev, NV50_SOR_DP_SCFG(or, link), 0x010f7f3f, bestVTUa << 24 |
							     bestVTUf << 16 |
							     bestVTUi << 8 |
							     unk);
}
static void
nv50_sor_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct nouveau_channel *evo = nv50_display(dev)->master;
	int ret;

	if (!nv_encoder->crtc)
		return;
	nv50_crtc_blank(nouveau_crtc(nv_encoder->crtc), true);

	NV_DEBUG_KMS(dev, "Disconnecting SOR %d\n", nv_encoder->or);

	ret = RING_SPACE(evo, 4);
	if (ret) {
		NV_ERROR(dev, "no space while disconnecting SOR\n");
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
	struct drm_device *dev = encoder->dev;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_encoder *enc;
	uint32_t val;
	int or = nv_encoder->or;

	NV_DEBUG_KMS(dev, "or %d type %d mode %d\n", or, nv_encoder->dcb->type, mode);

	nv_encoder->last_dpms = mode;
	list_for_each_entry(enc, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nvenc = nouveau_encoder(enc);

		if (nvenc == nv_encoder ||
		    (nvenc->dcb->type != OUTPUT_TMDS &&
		     nvenc->dcb->type != OUTPUT_LVDS &&
		     nvenc->dcb->type != OUTPUT_DP) ||
		    nvenc->dcb->or != nv_encoder->dcb->or)
			continue;

		if (nvenc->last_dpms == DRM_MODE_DPMS_ON)
			return;
	}

	/* wait for it to be done */
	if (!nv_wait(dev, NV50_PDISPLAY_SOR_DPMS_CTRL(or),
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
	if (!nv_wait(dev, NV50_PDISPLAY_SOR_DPMS_STATE(or),
		     NV50_PDISPLAY_SOR_DPMS_STATE_WAIT, 0)) {
		NV_ERROR(dev, "timeout: SOR_DPMS_STATE_WAIT(%d) == 0\n", or);
		NV_ERROR(dev, "SOR_DPMS_STATE(%d) = 0x%08x\n", or,
			 nv_rd32(dev, NV50_PDISPLAY_SOR_DPMS_STATE(or)));
	}

	if (nv_encoder->dcb->type == OUTPUT_DP) {
		struct dp_train_func func = {
			.link_set = nv50_sor_dp_link_set,
			.train_set = nv50_sor_dp_train_set,
			.train_adj = nv50_sor_dp_train_adj
		};

		nouveau_dp_dpms(encoder, mode, nv_encoder->dp.datarate, &func);
	}
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
nv50_sor_mode_fixup(struct drm_encoder *encoder,
		    const struct drm_display_mode *mode,
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
	     connector->native_mode)
		drm_mode_copy(adjusted_mode, connector->native_mode);

	return true;
}

static void
nv50_sor_prepare(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	nv50_sor_disconnect(encoder);
	if (nv_encoder->dcb->type == OUTPUT_DP) {
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
	struct drm_device *dev = encoder->dev;
	struct nouveau_crtc *crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector;
	uint32_t mode_ctl = 0;
	int ret;

	NV_DEBUG_KMS(dev, "or %d type %d -> crtc %d\n",
		     nv_encoder->or, nv_encoder->dcb->type, crtc->index);
	nv_encoder->crtc = encoder->crtc;

	switch (nv_encoder->dcb->type) {
	case OUTPUT_TMDS:
		if (nv_encoder->dcb->sorconf.link & 1) {
			if (mode->clock < 165000)
				mode_ctl = 0x0100;
			else
				mode_ctl = 0x0500;
		} else
			mode_ctl = 0x0200;

		nouveau_hdmi_mode_set(encoder, mode);
		break;
	case OUTPUT_DP:
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
		NV_ERROR(dev, "no space while connecting SOR\n");
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
nv50_sor_create(struct drm_connector *connector, struct dcb_entry *entry)
{
	struct nouveau_encoder *nv_encoder = NULL;
	struct drm_device *dev = connector->dev;
	struct drm_encoder *encoder;
	int type;

	NV_DEBUG_KMS(dev, "\n");

	switch (entry->type) {
	case OUTPUT_TMDS:
	case OUTPUT_DP:
		type = DRM_MODE_ENCODER_TMDS;
		break;
	case OUTPUT_LVDS:
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
