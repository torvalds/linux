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

#include "nouveau_drm.h"
#include "nouveau_dma.h"

#include "nv50_display.h"
#include "nouveau_crtc.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"
#include "nouveau_fbcon.h"
#include <drm/drm_crtc_helper.h>
#include "nouveau_fence.h"

#include <core/gpuobj.h>
#include <core/class.h>

#include <subdev/timer.h>

static inline int
nv50_sor_nr(struct drm_device *dev)
{
	struct nouveau_device *device = nouveau_dev(dev);

	if (device->chipset  < 0x90 ||
	    device->chipset == 0x92 ||
	    device->chipset == 0xa0)
		return 2;

	return 4;
}

u32
nv50_display_active_crtcs(struct drm_device *dev)
{
	struct nouveau_device *device = nouveau_dev(dev);
	u32 mask = 0;
	int i;

	if (device->chipset  < 0x90 ||
	    device->chipset == 0x92 ||
	    device->chipset == 0xa0) {
		for (i = 0; i < 2; i++)
			mask |= nv_rd32(device, NV50_PDISPLAY_SOR_MODE_CTRL_C(i));
	} else {
		for (i = 0; i < 4; i++)
			mask |= nv_rd32(device, NV90_PDISPLAY_SOR_MODE_CTRL_C(i));
	}

	for (i = 0; i < 3; i++)
		mask |= nv_rd32(device, NV50_PDISPLAY_DAC_MODE_CTRL_C(i));

	return mask & 3;
}

int
nv50_display_early_init(struct drm_device *dev)
{
	return 0;
}

void
nv50_display_late_takedown(struct drm_device *dev)
{
}

int
nv50_display_sync(struct drm_device *dev)
{
	struct nv50_display *disp = nv50_display(dev);
	struct nouveau_channel *evo = disp->master;
	int ret;

	ret = RING_SPACE(evo, 6);
	if (ret == 0) {
		BEGIN_NV04(evo, 0, 0x0084, 1);
		OUT_RING  (evo, 0x80000000);
		BEGIN_NV04(evo, 0, 0x0080, 1);
		OUT_RING  (evo, 0);
		BEGIN_NV04(evo, 0, 0x0084, 1);
		OUT_RING  (evo, 0x00000000);

		nv_wo32(disp->ramin, 0x2000, 0x00000000);
		FIRE_RING (evo);

		if (nv_wait_ne(disp->ramin, 0x2000, 0xffffffff, 0x00000000))
			return 0;
	}

	return 0;
}

int
nv50_display_init(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_channel *evo;
	int ret, i;

	for (i = 0; i < 3; i++) {
		nv_wr32(device, NV50_PDISPLAY_DAC_DPMS_CTRL(i), 0x00550000 |
			NV50_PDISPLAY_DAC_DPMS_CTRL_PENDING);
		nv_wr32(device, NV50_PDISPLAY_DAC_CLK_CTRL1(i), 0x00000001);
	}

	for (i = 0; i < 2; i++) {
		nv_wr32(device, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i), 0x2000);
		if (!nv_wait(device, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i),
			     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS, 0)) {
			NV_ERROR(drm, "timeout: CURSOR_CTRL2_STATUS == 0\n");
			NV_ERROR(drm, "CURSOR_CTRL2 = 0x%08x\n",
				 nv_rd32(device, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i)));
			return -EBUSY;
		}

		nv_wr32(device, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i),
			NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_ON);
		if (!nv_wait(device, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i),
			     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS,
			     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS_ACTIVE)) {
			NV_ERROR(drm, "timeout: "
				      "CURSOR_CTRL2_STATUS_ACTIVE(%d)\n", i);
			NV_ERROR(drm, "CURSOR_CTRL2(%d) = 0x%08x\n", i,
				 nv_rd32(device, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i)));
			return -EBUSY;
		}
	}

	ret = nv50_evo_init(dev);
	if (ret)
		return ret;
	evo = nv50_display(dev)->master;

	ret = RING_SPACE(evo, 3);
	if (ret)
		return ret;
	BEGIN_NV04(evo, 0, NV50_EVO_UNK84, 2);
	OUT_RING  (evo, NV50_EVO_UNK84_NOTIFY_DISABLED);
	OUT_RING  (evo, NvEvoSync);

	return nv50_display_sync(dev);
}

void
nv50_display_fini(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_device *device = nouveau_dev(dev);
	struct nv50_display *disp = nv50_display(dev);
	struct nouveau_channel *evo = disp->master;
	struct drm_crtc *drm_crtc;
	int ret, i;

	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);

		nv50_crtc_blank(crtc, true);
	}

	ret = RING_SPACE(evo, 2);
	if (ret == 0) {
		BEGIN_NV04(evo, 0, NV50_EVO_UPDATE, 1);
		OUT_RING(evo, 0);
	}
	FIRE_RING(evo);

	/* Almost like ack'ing a vblank interrupt, maybe in the spirit of
	 * cleaning up?
	 */
	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);
		uint32_t mask = NV50_PDISPLAY_INTR_1_VBLANK_CRTC_(crtc->index);

		if (!crtc->base.enabled)
			continue;

		nv_wr32(device, NV50_PDISPLAY_INTR_1, mask);
		if (!nv_wait(device, NV50_PDISPLAY_INTR_1, mask, mask)) {
			NV_ERROR(drm, "timeout: (0x610024 & 0x%08x) == "
				      "0x%08x\n", mask, mask);
			NV_ERROR(drm, "0x610024 = 0x%08x\n",
				 nv_rd32(device, NV50_PDISPLAY_INTR_1));
		}
	}

	for (i = 0; i < 2; i++) {
		nv_wr32(device, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i), 0);
		if (!nv_wait(device, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i),
			     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS, 0)) {
			NV_ERROR(drm, "timeout: CURSOR_CTRL2_STATUS == 0\n");
			NV_ERROR(drm, "CURSOR_CTRL2 = 0x%08x\n",
				 nv_rd32(device, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i)));
		}
	}

	nv50_evo_fini(dev);

	for (i = 0; i < 3; i++) {
		if (!nv_wait(device, NV50_PDISPLAY_SOR_DPMS_STATE(i),
			     NV50_PDISPLAY_SOR_DPMS_STATE_WAIT, 0)) {
			NV_ERROR(drm, "timeout: SOR_DPMS_STATE_WAIT(%d) == 0\n", i);
			NV_ERROR(drm, "SOR_DPMS_STATE(%d) = 0x%08x\n", i,
				  nv_rd32(device, NV50_PDISPLAY_SOR_DPMS_STATE(i)));
		}
	}
}

int
nv50_display_create(struct drm_device *dev)
{
	static const u16 oclass[] = {
		NVA3_DISP_CLASS,
		NV94_DISP_CLASS,
		NVA0_DISP_CLASS,
		NV84_DISP_CLASS,
		NV50_DISP_CLASS,
	};
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct dcb_table *dcb = &drm->vbios.dcb;
	struct drm_connector *connector, *ct;
	struct nv50_display *priv;
	int ret, i;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	nouveau_display(dev)->priv = priv;
	nouveau_display(dev)->dtor = nv50_display_destroy;
	nouveau_display(dev)->init = nv50_display_init;
	nouveau_display(dev)->fini = nv50_display_fini;

	/* attempt to allocate a supported evo display class */
	ret = -ENODEV;
	for (i = 0; ret && i < ARRAY_SIZE(oclass); i++) {
		ret = nouveau_object_new(nv_object(drm), NVDRM_DEVICE,
					 0xd1500000, oclass[i], NULL, 0,
					 &priv->core);
	}

	if (ret)
		return ret;

	/* Create CRTC objects */
	for (i = 0; i < 2; i++) {
		ret = nv50_crtc_create(dev, i);
		if (ret)
			return ret;
	}

	/* We setup the encoders from the BIOS table */
	for (i = 0 ; i < dcb->entries; i++) {
		struct dcb_output *entry = &dcb->entry[i];

		if (entry->location != DCB_LOC_ON_CHIP) {
			NV_WARN(drm, "Off-chip encoder %d/%d unsupported\n",
				entry->type, ffs(entry->or) - 1);
			continue;
		}

		connector = nouveau_connector_create(dev, entry->connector);
		if (IS_ERR(connector))
			continue;

		switch (entry->type) {
		case DCB_OUTPUT_TMDS:
		case DCB_OUTPUT_LVDS:
		case DCB_OUTPUT_DP:
			nv50_sor_create(connector, entry);
			break;
		case DCB_OUTPUT_ANALOG:
			nv50_dac_create(connector, entry);
			break;
		default:
			NV_WARN(drm, "DCB encoder %d unknown\n", entry->type);
			continue;
		}
	}

	list_for_each_entry_safe(connector, ct,
				 &dev->mode_config.connector_list, head) {
		if (!connector->encoder_ids[0]) {
			NV_WARN(drm, "%s has no encoders, removing\n",
				drm_get_connector_name(connector));
			connector->funcs->destroy(connector);
		}
	}

	ret = nv50_evo_create(dev);
	if (ret) {
		nv50_display_destroy(dev);
		return ret;
	}

	return 0;
}

void
nv50_display_destroy(struct drm_device *dev)
{
	struct nv50_display *disp = nv50_display(dev);

	nv50_evo_destroy(dev);
	kfree(disp);
}

struct nouveau_bo *
nv50_display_crtc_sema(struct drm_device *dev, int crtc)
{
	return nv50_display(dev)->crtc[crtc].sem.bo;
}

void
nv50_display_flip_stop(struct drm_crtc *crtc)
{
	struct nv50_display *disp = nv50_display(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv50_display_crtc *dispc = &disp->crtc[nv_crtc->index];
	struct nouveau_channel *evo = dispc->sync;
	int ret;

	ret = RING_SPACE(evo, 8);
	if (ret) {
		WARN_ON(1);
		return;
	}

	BEGIN_NV04(evo, 0, 0x0084, 1);
	OUT_RING  (evo, 0x00000000);
	BEGIN_NV04(evo, 0, 0x0094, 1);
	OUT_RING  (evo, 0x00000000);
	BEGIN_NV04(evo, 0, 0x00c0, 1);
	OUT_RING  (evo, 0x00000000);
	BEGIN_NV04(evo, 0, 0x0080, 1);
	OUT_RING  (evo, 0x00000000);
	FIRE_RING (evo);
}

int
nv50_display_flip_next(struct drm_crtc *crtc, struct drm_framebuffer *fb,
		       struct nouveau_channel *chan)
{
	struct nouveau_drm *drm = nouveau_drm(crtc->dev);
	struct nouveau_framebuffer *nv_fb = nouveau_framebuffer(fb);
	struct nv50_display *disp = nv50_display(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv50_display_crtc *dispc = &disp->crtc[nv_crtc->index];
	struct nouveau_channel *evo = dispc->sync;
	int ret;

	ret = RING_SPACE(evo, chan ? 25 : 27);
	if (unlikely(ret))
		return ret;

	/* synchronise with the rendering channel, if necessary */
	if (likely(chan)) {
		ret = RING_SPACE(chan, 10);
		if (ret) {
			WIND_RING(evo);
			return ret;
		}

		if (nv_device(drm->device)->chipset < 0xc0) {
			BEGIN_NV04(chan, 0, 0x0060, 2);
			OUT_RING  (chan, NvEvoSema0 + nv_crtc->index);
			OUT_RING  (chan, dispc->sem.offset);
			BEGIN_NV04(chan, 0, 0x006c, 1);
			OUT_RING  (chan, 0xf00d0000 | dispc->sem.value);
			BEGIN_NV04(chan, 0, 0x0064, 2);
			OUT_RING  (chan, dispc->sem.offset ^ 0x10);
			OUT_RING  (chan, 0x74b1e000);
			BEGIN_NV04(chan, 0, 0x0060, 1);
			if (nv_device(drm->device)->chipset < 0x84)
				OUT_RING  (chan, NvSema);
			else
				OUT_RING  (chan, chan->vram);
		} else {
			u64 offset = nvc0_fence_crtc(chan, nv_crtc->index);
			offset += dispc->sem.offset;
			BEGIN_NVC0(chan, 0, 0x0010, 4);
			OUT_RING  (chan, upper_32_bits(offset));
			OUT_RING  (chan, lower_32_bits(offset));
			OUT_RING  (chan, 0xf00d0000 | dispc->sem.value);
			OUT_RING  (chan, 0x1002);
			BEGIN_NVC0(chan, 0, 0x0010, 4);
			OUT_RING  (chan, upper_32_bits(offset));
			OUT_RING  (chan, lower_32_bits(offset ^ 0x10));
			OUT_RING  (chan, 0x74b1e000);
			OUT_RING  (chan, 0x1001);
		}
		FIRE_RING (chan);
	} else {
		nouveau_bo_wr32(dispc->sem.bo, dispc->sem.offset / 4,
				0xf00d0000 | dispc->sem.value);
	}

	/* queue the flip on the crtc's "display sync" channel */
	BEGIN_NV04(evo, 0, 0x0100, 1);
	OUT_RING  (evo, 0xfffe0000);
	if (chan) {
		BEGIN_NV04(evo, 0, 0x0084, 1);
		OUT_RING  (evo, 0x00000100);
	} else {
		BEGIN_NV04(evo, 0, 0x0084, 1);
		OUT_RING  (evo, 0x00000010);
		/* allows gamma somehow, PDISP will bitch at you if
		 * you don't wait for vblank before changing this..
		 */
		BEGIN_NV04(evo, 0, 0x00e0, 1);
		OUT_RING  (evo, 0x40000000);
	}
	BEGIN_NV04(evo, 0, 0x0088, 4);
	OUT_RING  (evo, dispc->sem.offset);
	OUT_RING  (evo, 0xf00d0000 | dispc->sem.value);
	OUT_RING  (evo, 0x74b1e000);
	OUT_RING  (evo, NvEvoSync);
	BEGIN_NV04(evo, 0, 0x00a0, 2);
	OUT_RING  (evo, 0x00000000);
	OUT_RING  (evo, 0x00000000);
	BEGIN_NV04(evo, 0, 0x00c0, 1);
	OUT_RING  (evo, nv_fb->r_dma);
	BEGIN_NV04(evo, 0, 0x0110, 2);
	OUT_RING  (evo, 0x00000000);
	OUT_RING  (evo, 0x00000000);
	BEGIN_NV04(evo, 0, 0x0800, 5);
	OUT_RING  (evo, nv_fb->nvbo->bo.offset >> 8);
	OUT_RING  (evo, 0);
	OUT_RING  (evo, (fb->height << 16) | fb->width);
	OUT_RING  (evo, nv_fb->r_pitch);
	OUT_RING  (evo, nv_fb->r_format);
	BEGIN_NV04(evo, 0, 0x0080, 1);
	OUT_RING  (evo, 0x00000000);
	FIRE_RING (evo);

	dispc->sem.offset ^= 0x10;
	dispc->sem.value++;
	return 0;
}
