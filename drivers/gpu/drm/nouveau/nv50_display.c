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

#include "nv50_display.h"
#include "nouveau_crtc.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nouveau_ramht.h"
#include "drm_crtc_helper.h"

static inline int
nv50_sor_nr(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->chipset  < 0x90 ||
	    dev_priv->chipset == 0x92 ||
	    dev_priv->chipset == 0xa0)
		return 2;

	return 4;
}

static void
nv50_evo_channel_del(struct nouveau_channel **pchan)
{
	struct nouveau_channel *chan = *pchan;

	if (!chan)
		return;
	*pchan = NULL;

	nouveau_gpuobj_channel_takedown(chan);
	nouveau_bo_unmap(chan->pushbuf_bo);
	nouveau_bo_ref(NULL, &chan->pushbuf_bo);

	if (chan->user)
		iounmap(chan->user);

	kfree(chan);
}

static int
nv50_evo_dmaobj_new(struct nouveau_channel *evo, uint32_t class, uint32_t name,
		    uint32_t tile_flags, uint32_t magic_flags,
		    uint32_t offset, uint32_t limit)
{
	struct drm_nouveau_private *dev_priv = evo->dev->dev_private;
	struct drm_device *dev = evo->dev;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(dev, evo, 6*4, 32, 0, &obj);
	if (ret)
		return ret;
	obj->engine = NVOBJ_ENGINE_DISPLAY;

	nv_wo32(obj,  0, (tile_flags << 22) | (magic_flags << 16) | class);
	nv_wo32(obj,  4, limit);
	nv_wo32(obj,  8, offset);
	nv_wo32(obj, 12, 0x00000000);
	nv_wo32(obj, 16, 0x00000000);
	if (dev_priv->card_type < NV_C0)
		nv_wo32(obj, 20, 0x00010000);
	else
		nv_wo32(obj, 20, 0x00020000);
	dev_priv->engine.instmem.flush(dev);

	ret = nouveau_ramht_insert(evo, name, obj);
	nouveau_gpuobj_ref(NULL, &obj);
	if (ret) {
		return ret;
	}

	return 0;
}

static int
nv50_evo_channel_new(struct drm_device *dev, struct nouveau_channel **pchan)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramht = NULL;
	struct nouveau_channel *chan;
	int ret;

	chan = kzalloc(sizeof(struct nouveau_channel), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;
	*pchan = chan;

	chan->id = -1;
	chan->dev = dev;
	chan->user_get = 4;
	chan->user_put = 0;

	ret = nouveau_gpuobj_new(dev, NULL, 32768, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC, &chan->ramin);
	if (ret) {
		NV_ERROR(dev, "Error allocating EVO channel memory: %d\n", ret);
		nv50_evo_channel_del(pchan);
		return ret;
	}

	ret = drm_mm_init(&chan->ramin_heap, 0, 32768);
	if (ret) {
		NV_ERROR(dev, "Error initialising EVO PRAMIN heap: %d\n", ret);
		nv50_evo_channel_del(pchan);
		return ret;
	}

	ret = nouveau_gpuobj_new(dev, chan, 4096, 16, 0, &ramht);
	if (ret) {
		NV_ERROR(dev, "Unable to allocate EVO RAMHT: %d\n", ret);
		nv50_evo_channel_del(pchan);
		return ret;
	}

	ret = nouveau_ramht_new(dev, ramht, &chan->ramht);
	nouveau_gpuobj_ref(NULL, &ramht);
	if (ret) {
		nv50_evo_channel_del(pchan);
		return ret;
	}

	if (dev_priv->chipset != 0x50) {
		ret = nv50_evo_dmaobj_new(chan, 0x3d, NvEvoFB16, 0x70, 0x19,
					  0, 0xffffffff);
		if (ret) {
			nv50_evo_channel_del(pchan);
			return ret;
		}


		ret = nv50_evo_dmaobj_new(chan, 0x3d, NvEvoFB32, 0x7a, 0x19,
					  0, 0xffffffff);
		if (ret) {
			nv50_evo_channel_del(pchan);
			return ret;
		}
	}

	ret = nv50_evo_dmaobj_new(chan, 0x3d, NvEvoVRAM, 0, 0x19,
				  0, dev_priv->vram_size);
	if (ret) {
		nv50_evo_channel_del(pchan);
		return ret;
	}

	ret = nouveau_bo_new(dev, NULL, 4096, 0, TTM_PL_FLAG_VRAM, 0, 0,
			     false, true, &chan->pushbuf_bo);
	if (ret == 0)
		ret = nouveau_bo_pin(chan->pushbuf_bo, TTM_PL_FLAG_VRAM);
	if (ret) {
		NV_ERROR(dev, "Error creating EVO DMA push buffer: %d\n", ret);
		nv50_evo_channel_del(pchan);
		return ret;
	}

	ret = nouveau_bo_map(chan->pushbuf_bo);
	if (ret) {
		NV_ERROR(dev, "Error mapping EVO DMA push buffer: %d\n", ret);
		nv50_evo_channel_del(pchan);
		return ret;
	}

	chan->user = ioremap(pci_resource_start(dev->pdev, 0) +
					NV50_PDISPLAY_USER(0), PAGE_SIZE);
	if (!chan->user) {
		NV_ERROR(dev, "Error mapping EVO control regs.\n");
		nv50_evo_channel_del(pchan);
		return -ENOMEM;
	}

	return 0;
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
nv50_display_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct nouveau_channel *evo = dev_priv->evo;
	struct drm_connector *connector;
	uint32_t val, ram_amount;
	uint64_t start;
	int ret, i;

	NV_DEBUG_KMS(dev, "\n");

	nv_wr32(dev, 0x00610184, nv_rd32(dev, 0x00614004));
	/*
	 * I think the 0x006101XX range is some kind of main control area
	 * that enables things.
	 */
	/* CRTC? */
	for (i = 0; i < 2; i++) {
		val = nv_rd32(dev, 0x00616100 + (i * 0x800));
		nv_wr32(dev, 0x00610190 + (i * 0x10), val);
		val = nv_rd32(dev, 0x00616104 + (i * 0x800));
		nv_wr32(dev, 0x00610194 + (i * 0x10), val);
		val = nv_rd32(dev, 0x00616108 + (i * 0x800));
		nv_wr32(dev, 0x00610198 + (i * 0x10), val);
		val = nv_rd32(dev, 0x0061610c + (i * 0x800));
		nv_wr32(dev, 0x0061019c + (i * 0x10), val);
	}
	/* DAC */
	for (i = 0; i < 3; i++) {
		val = nv_rd32(dev, 0x0061a000 + (i * 0x800));
		nv_wr32(dev, 0x006101d0 + (i * 0x04), val);
	}
	/* SOR */
	for (i = 0; i < nv50_sor_nr(dev); i++) {
		val = nv_rd32(dev, 0x0061c000 + (i * 0x800));
		nv_wr32(dev, 0x006101e0 + (i * 0x04), val);
	}
	/* EXT */
	for (i = 0; i < 3; i++) {
		val = nv_rd32(dev, 0x0061e000 + (i * 0x800));
		nv_wr32(dev, 0x006101f0 + (i * 0x04), val);
	}

	for (i = 0; i < 3; i++) {
		nv_wr32(dev, NV50_PDISPLAY_DAC_DPMS_CTRL(i), 0x00550000 |
			NV50_PDISPLAY_DAC_DPMS_CTRL_PENDING);
		nv_wr32(dev, NV50_PDISPLAY_DAC_CLK_CTRL1(i), 0x00000001);
	}

	/* This used to be in crtc unblank, but seems out of place there. */
	nv_wr32(dev, NV50_PDISPLAY_UNK_380, 0);
	/* RAM is clamped to 256 MiB. */
	ram_amount = dev_priv->vram_size;
	NV_DEBUG_KMS(dev, "ram_amount %d\n", ram_amount);
	if (ram_amount > 256*1024*1024)
		ram_amount = 256*1024*1024;
	nv_wr32(dev, NV50_PDISPLAY_RAM_AMOUNT, ram_amount - 1);
	nv_wr32(dev, NV50_PDISPLAY_UNK_388, 0x150000);
	nv_wr32(dev, NV50_PDISPLAY_UNK_38C, 0);

	/* The precise purpose is unknown, i suspect it has something to do
	 * with text mode.
	 */
	if (nv_rd32(dev, NV50_PDISPLAY_INTR_1) & 0x100) {
		nv_wr32(dev, NV50_PDISPLAY_INTR_1, 0x100);
		nv_wr32(dev, 0x006194e8, nv_rd32(dev, 0x006194e8) & ~1);
		if (!nv_wait(dev, 0x006194e8, 2, 0)) {
			NV_ERROR(dev, "timeout: (0x6194e8 & 2) != 0\n");
			NV_ERROR(dev, "0x6194e8 = 0x%08x\n",
						nv_rd32(dev, 0x6194e8));
			return -EBUSY;
		}
	}

	/* taken from nv bug #12637, attempts to un-wedge the hw if it's
	 * stuck in some unspecified state
	 */
	start = ptimer->read(dev);
	nv_wr32(dev, NV50_PDISPLAY_CHANNEL_STAT(0), 0x2b00);
	while ((val = nv_rd32(dev, NV50_PDISPLAY_CHANNEL_STAT(0))) & 0x1e0000) {
		if ((val & 0x9f0000) == 0x20000)
			nv_wr32(dev, NV50_PDISPLAY_CHANNEL_STAT(0),
							val | 0x800000);

		if ((val & 0x3f0000) == 0x30000)
			nv_wr32(dev, NV50_PDISPLAY_CHANNEL_STAT(0),
							val | 0x200000);

		if (ptimer->read(dev) - start > 1000000000ULL) {
			NV_ERROR(dev, "timeout: (0x610200 & 0x1e0000) != 0\n");
			NV_ERROR(dev, "0x610200 = 0x%08x\n", val);
			return -EBUSY;
		}
	}

	nv_wr32(dev, NV50_PDISPLAY_CTRL_STATE, NV50_PDISPLAY_CTRL_STATE_ENABLE);
	nv_wr32(dev, NV50_PDISPLAY_CHANNEL_STAT(0), 0x1000b03);
	if (!nv_wait(dev, NV50_PDISPLAY_CHANNEL_STAT(0),
		     0x40000000, 0x40000000)) {
		NV_ERROR(dev, "timeout: (0x610200 & 0x40000000) == 0x40000000\n");
		NV_ERROR(dev, "0x610200 = 0x%08x\n",
			  nv_rd32(dev, NV50_PDISPLAY_CHANNEL_STAT(0)));
		return -EBUSY;
	}

	for (i = 0; i < 2; i++) {
		nv_wr32(dev, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i), 0x2000);
		if (!nv_wait(dev, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i),
			     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS, 0)) {
			NV_ERROR(dev, "timeout: CURSOR_CTRL2_STATUS == 0\n");
			NV_ERROR(dev, "CURSOR_CTRL2 = 0x%08x\n",
				 nv_rd32(dev, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i)));
			return -EBUSY;
		}

		nv_wr32(dev, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i),
			NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_ON);
		if (!nv_wait(dev, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i),
			     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS,
			     NV50_PDISPLAY_CURSOR_CURSOR_CTRL2_STATUS_ACTIVE)) {
			NV_ERROR(dev, "timeout: "
				      "CURSOR_CTRL2_STATUS_ACTIVE(%d)\n", i);
			NV_ERROR(dev, "CURSOR_CTRL2(%d) = 0x%08x\n", i,
				 nv_rd32(dev, NV50_PDISPLAY_CURSOR_CURSOR_CTRL2(i)));
			return -EBUSY;
		}
	}

	nv_wr32(dev, NV50_PDISPLAY_OBJECTS, (evo->ramin->vinst >> 8) | 9);

	/* initialise fifo */
	nv_wr32(dev, NV50_PDISPLAY_CHANNEL_DMA_CB(0),
		((evo->pushbuf_bo->bo.mem.start << PAGE_SHIFT) >> 8) |
		NV50_PDISPLAY_CHANNEL_DMA_CB_LOCATION_VRAM |
		NV50_PDISPLAY_CHANNEL_DMA_CB_VALID);
	nv_wr32(dev, NV50_PDISPLAY_CHANNEL_UNK2(0), 0x00010000);
	nv_wr32(dev, NV50_PDISPLAY_CHANNEL_UNK3(0), 0x00000002);
	if (!nv_wait(dev, 0x610200, 0x80000000, 0x00000000)) {
		NV_ERROR(dev, "timeout: (0x610200 & 0x80000000) == 0\n");
		NV_ERROR(dev, "0x610200 = 0x%08x\n", nv_rd32(dev, 0x610200));
		return -EBUSY;
	}
	nv_wr32(dev, NV50_PDISPLAY_CHANNEL_STAT(0),
		(nv_rd32(dev, NV50_PDISPLAY_CHANNEL_STAT(0)) & ~0x00000003) |
		 NV50_PDISPLAY_CHANNEL_STAT_DMA_ENABLED);
	nv_wr32(dev, NV50_PDISPLAY_USER_PUT(0), 0);
	nv_wr32(dev, NV50_PDISPLAY_CHANNEL_STAT(0), 0x01000003 |
		NV50_PDISPLAY_CHANNEL_STAT_DMA_ENABLED);
	nv_wr32(dev, 0x610300, nv_rd32(dev, 0x610300) & ~1);

	evo->dma.max = (4096/4) - 2;
	evo->dma.put = 0;
	evo->dma.cur = evo->dma.put;
	evo->dma.free = evo->dma.max - evo->dma.cur;

	ret = RING_SPACE(evo, NOUVEAU_DMA_SKIPS);
	if (ret)
		return ret;

	for (i = 0; i < NOUVEAU_DMA_SKIPS; i++)
		OUT_RING(evo, 0);

	ret = RING_SPACE(evo, 11);
	if (ret)
		return ret;
	BEGIN_RING(evo, 0, NV50_EVO_UNK84, 2);
	OUT_RING(evo, NV50_EVO_UNK84_NOTIFY_DISABLED);
	OUT_RING(evo, NV50_EVO_DMA_NOTIFY_HANDLE_NONE);
	BEGIN_RING(evo, 0, NV50_EVO_CRTC(0, FB_DMA), 1);
	OUT_RING(evo, NV50_EVO_CRTC_FB_DMA_HANDLE_NONE);
	BEGIN_RING(evo, 0, NV50_EVO_CRTC(0, UNK0800), 1);
	OUT_RING(evo, 0);
	BEGIN_RING(evo, 0, NV50_EVO_CRTC(0, DISPLAY_START), 1);
	OUT_RING(evo, 0);
	BEGIN_RING(evo, 0, NV50_EVO_CRTC(0, UNK082C), 1);
	OUT_RING(evo, 0);
	FIRE_RING(evo);
	if (!nv_wait(dev, 0x640004, 0xffffffff, evo->dma.put << 2))
		NV_ERROR(dev, "evo pushbuf stalled\n");

	/* enable clock change interrupts. */
	nv_wr32(dev, 0x610028, 0x00010001);
	nv_wr32(dev, NV50_PDISPLAY_INTR_EN, (NV50_PDISPLAY_INTR_EN_CLK_UNK10 |
					     NV50_PDISPLAY_INTR_EN_CLK_UNK20 |
					     NV50_PDISPLAY_INTR_EN_CLK_UNK40));

	/* enable hotplug interrupts */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct nouveau_connector *conn = nouveau_connector(connector);

		if (conn->dcb->gpio_tag == 0xff)
			continue;

		pgpio->irq_enable(dev, conn->dcb->gpio_tag, true);
	}

	return 0;
}

static int nv50_display_disable(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_crtc *drm_crtc;
	int ret, i;

	NV_DEBUG_KMS(dev, "\n");

	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);

		nv50_crtc_blank(crtc, true);
	}

	ret = RING_SPACE(dev_priv->evo, 2);
	if (ret == 0) {
		BEGIN_RING(dev_priv->evo, 0, NV50_EVO_UPDATE, 1);
		OUT_RING(dev_priv->evo, 0);
	}
	FIRE_RING(dev_priv->evo);

	/* Almost like ack'ing a vblank interrupt, maybe in the spirit of
	 * cleaning up?
	 */
	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);
		uint32_t mask = NV50_PDISPLAY_INTR_1_VBLANK_CRTC_(crtc->index);

		if (!crtc->base.enabled)
			continue;

		nv_wr32(dev, NV50_PDISPLAY_INTR_1, mask);
		if (!nv_wait(dev, NV50_PDISPLAY_INTR_1, mask, mask)) {
			NV_ERROR(dev, "timeout: (0x610024 & 0x%08x) == "
				      "0x%08x\n", mask, mask);
			NV_ERROR(dev, "0x610024 = 0x%08x\n",
				 nv_rd32(dev, NV50_PDISPLAY_INTR_1));
		}
	}

	nv_wr32(dev, NV50_PDISPLAY_CHANNEL_STAT(0), 0);
	nv_wr32(dev, NV50_PDISPLAY_CTRL_STATE, 0);
	if (!nv_wait(dev, NV50_PDISPLAY_CHANNEL_STAT(0), 0x1e0000, 0)) {
		NV_ERROR(dev, "timeout: (0x610200 & 0x1e0000) == 0\n");
		NV_ERROR(dev, "0x610200 = 0x%08x\n",
			  nv_rd32(dev, NV50_PDISPLAY_CHANNEL_STAT(0)));
	}

	for (i = 0; i < 3; i++) {
		if (!nv_wait(dev, NV50_PDISPLAY_SOR_DPMS_STATE(i),
			     NV50_PDISPLAY_SOR_DPMS_STATE_WAIT, 0)) {
			NV_ERROR(dev, "timeout: SOR_DPMS_STATE_WAIT(%d) == 0\n", i);
			NV_ERROR(dev, "SOR_DPMS_STATE(%d) = 0x%08x\n", i,
				  nv_rd32(dev, NV50_PDISPLAY_SOR_DPMS_STATE(i)));
		}
	}

	/* disable interrupts. */
	nv_wr32(dev, NV50_PDISPLAY_INTR_EN, 0x00000000);

	/* disable hotplug interrupts */
	nv_wr32(dev, 0xe054, 0xffffffff);
	nv_wr32(dev, 0xe050, 0x00000000);
	if (dev_priv->chipset >= 0x90) {
		nv_wr32(dev, 0xe074, 0xffffffff);
		nv_wr32(dev, 0xe070, 0x00000000);
	}
	return 0;
}

int nv50_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct dcb_table *dcb = &dev_priv->vbios.dcb;
	struct drm_connector *connector, *ct;
	int ret, i;

	NV_DEBUG_KMS(dev, "\n");

	/* init basic kernel modesetting */
	drm_mode_config_init(dev);

	/* Initialise some optional connector properties. */
	drm_mode_create_scaling_mode_property(dev);
	drm_mode_create_dithering_property(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = (void *)&nouveau_mode_config_funcs;

	dev->mode_config.max_width = 8192;
	dev->mode_config.max_height = 8192;

	dev->mode_config.fb_base = dev_priv->fb_phys;

	/* Create EVO channel */
	ret = nv50_evo_channel_new(dev, &dev_priv->evo);
	if (ret) {
		NV_ERROR(dev, "Error creating EVO channel: %d\n", ret);
		return ret;
	}

	/* Create CRTC objects */
	for (i = 0; i < 2; i++)
		nv50_crtc_create(dev, i);

	/* We setup the encoders from the BIOS table */
	for (i = 0 ; i < dcb->entries; i++) {
		struct dcb_entry *entry = &dcb->entry[i];

		if (entry->location != DCB_LOC_ON_CHIP) {
			NV_WARN(dev, "Off-chip encoder %d/%d unsupported\n",
				entry->type, ffs(entry->or) - 1);
			continue;
		}

		connector = nouveau_connector_create(dev, entry->connector);
		if (IS_ERR(connector))
			continue;

		switch (entry->type) {
		case OUTPUT_TMDS:
		case OUTPUT_LVDS:
		case OUTPUT_DP:
			nv50_sor_create(connector, entry);
			break;
		case OUTPUT_ANALOG:
			nv50_dac_create(connector, entry);
			break;
		default:
			NV_WARN(dev, "DCB encoder %d unknown\n", entry->type);
			continue;
		}
	}

	list_for_each_entry_safe(connector, ct,
				 &dev->mode_config.connector_list, head) {
		if (!connector->encoder_ids[0]) {
			NV_WARN(dev, "%s has no encoders, removing\n",
				drm_get_connector_name(connector));
			connector->funcs->destroy(connector);
		}
	}

	ret = nv50_display_init(dev);
	if (ret) {
		nv50_display_destroy(dev);
		return ret;
	}

	return 0;
}

void
nv50_display_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	NV_DEBUG_KMS(dev, "\n");

	drm_mode_config_cleanup(dev);

	nv50_display_disable(dev);
	nv50_evo_channel_del(&dev_priv->evo);
}

static u16
nv50_display_script_select(struct drm_device *dev, struct dcb_entry *dcb,
			   u32 mc, int pxclk)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_connector *nv_connector = NULL;
	struct drm_encoder *encoder;
	struct nvbios *bios = &dev_priv->vbios;
	u32 script = 0, or;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);

		if (nv_encoder->dcb != dcb)
			continue;

		nv_connector = nouveau_encoder_connector_get(nv_encoder);
		break;
	}

	or = ffs(dcb->or) - 1;
	switch (dcb->type) {
	case OUTPUT_LVDS:
		script = (mc >> 8) & 0xf;
		if (bios->fp_no_ddc) {
			if (bios->fp.dual_link)
				script |= 0x0100;
			if (bios->fp.if_is_24bit)
				script |= 0x0200;
		} else {
			if (pxclk >= bios->fp.duallink_transition_clk) {
				script |= 0x0100;
				if (bios->fp.strapless_is_24bit & 2)
					script |= 0x0200;
			} else
			if (bios->fp.strapless_is_24bit & 1)
				script |= 0x0200;

			if (nv_connector && nv_connector->edid &&
			    (nv_connector->edid->revision >= 4) &&
			    (nv_connector->edid->input & 0x70) >= 0x20)
				script |= 0x0200;
		}

		if (nouveau_uscript_lvds >= 0) {
			NV_INFO(dev, "override script 0x%04x with 0x%04x "
				     "for output LVDS-%d\n", script,
				     nouveau_uscript_lvds, or);
			script = nouveau_uscript_lvds;
		}
		break;
	case OUTPUT_TMDS:
		script = (mc >> 8) & 0xf;
		if (pxclk >= 165000)
			script |= 0x0100;

		if (nouveau_uscript_tmds >= 0) {
			NV_INFO(dev, "override script 0x%04x with 0x%04x "
				     "for output TMDS-%d\n", script,
				     nouveau_uscript_tmds, or);
			script = nouveau_uscript_tmds;
		}
		break;
	case OUTPUT_DP:
		script = (mc >> 8) & 0xf;
		break;
	case OUTPUT_ANALOG:
		script = 0xff;
		break;
	default:
		NV_ERROR(dev, "modeset on unsupported output type!\n");
		break;
	}

	return script;
}

static void
nv50_display_vblank_crtc_handler(struct drm_device *dev, int crtc)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan;
	struct list_head *entry, *tmp;

	list_for_each_safe(entry, tmp, &dev_priv->vbl_waiting) {
		chan = list_entry(entry, struct nouveau_channel, nvsw.vbl_wait);

		nouveau_bo_wr32(chan->notifier_bo, chan->nvsw.vblsem_offset,
						chan->nvsw.vblsem_rval);
		list_del(&chan->nvsw.vbl_wait);
	}
}

static void
nv50_display_vblank_handler(struct drm_device *dev, uint32_t intr)
{
	intr &= NV50_PDISPLAY_INTR_1_VBLANK_CRTC;

	if (intr & NV50_PDISPLAY_INTR_1_VBLANK_CRTC_0)
		nv50_display_vblank_crtc_handler(dev, 0);

	if (intr & NV50_PDISPLAY_INTR_1_VBLANK_CRTC_1)
		nv50_display_vblank_crtc_handler(dev, 1);

	nv_wr32(dev, NV50_PDISPLAY_INTR_EN, nv_rd32(dev,
		     NV50_PDISPLAY_INTR_EN) & ~intr);
	nv_wr32(dev, NV50_PDISPLAY_INTR_1, intr);
}

static void
nv50_display_unk10_handler(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 unk30 = nv_rd32(dev, 0x610030), mc;
	int i, crtc, or, type = OUTPUT_ANY;

	NV_DEBUG_KMS(dev, "0x610030: 0x%08x\n", unk30);
	dev_priv->evo_irq.dcb = NULL;

	nv_wr32(dev, 0x619494, nv_rd32(dev, 0x619494) & ~8);

	/* Determine which CRTC we're dealing with, only 1 ever will be
	 * signalled at the same time with the current nouveau code.
	 */
	crtc = ffs((unk30 & 0x00000060) >> 5) - 1;
	if (crtc < 0)
		goto ack;

	/* Nothing needs to be done for the encoder */
	crtc = ffs((unk30 & 0x00000180) >> 7) - 1;
	if (crtc < 0)
		goto ack;

	/* Find which encoder was connected to the CRTC */
	for (i = 0; type == OUTPUT_ANY && i < 3; i++) {
		mc = nv_rd32(dev, NV50_PDISPLAY_DAC_MODE_CTRL_C(i));
		NV_DEBUG_KMS(dev, "DAC-%d mc: 0x%08x\n", i, mc);
		if (!(mc & (1 << crtc)))
			continue;

		switch ((mc & 0x00000f00) >> 8) {
		case 0: type = OUTPUT_ANALOG; break;
		case 1: type = OUTPUT_TV; break;
		default:
			NV_ERROR(dev, "invalid mc, DAC-%d: 0x%08x\n", i, mc);
			goto ack;
		}

		or = i;
	}

	for (i = 0; type == OUTPUT_ANY && i < nv50_sor_nr(dev); i++) {
		if (dev_priv->chipset  < 0x90 ||
		    dev_priv->chipset == 0x92 ||
		    dev_priv->chipset == 0xa0)
			mc = nv_rd32(dev, NV50_PDISPLAY_SOR_MODE_CTRL_C(i));
		else
			mc = nv_rd32(dev, NV90_PDISPLAY_SOR_MODE_CTRL_C(i));

		NV_DEBUG_KMS(dev, "SOR-%d mc: 0x%08x\n", i, mc);
		if (!(mc & (1 << crtc)))
			continue;

		switch ((mc & 0x00000f00) >> 8) {
		case 0: type = OUTPUT_LVDS; break;
		case 1: type = OUTPUT_TMDS; break;
		case 2: type = OUTPUT_TMDS; break;
		case 5: type = OUTPUT_TMDS; break;
		case 8: type = OUTPUT_DP; break;
		case 9: type = OUTPUT_DP; break;
		default:
			NV_ERROR(dev, "invalid mc, SOR-%d: 0x%08x\n", i, mc);
			goto ack;
		}

		or = i;
	}

	/* There was no encoder to disable */
	if (type == OUTPUT_ANY)
		goto ack;

	/* Disable the encoder */
	for (i = 0; i < dev_priv->vbios.dcb.entries; i++) {
		struct dcb_entry *dcb = &dev_priv->vbios.dcb.entry[i];

		if (dcb->type == type && (dcb->or & (1 << or))) {
			nouveau_bios_run_display_table(dev, dcb, 0, -1);
			dev_priv->evo_irq.dcb = dcb;
			goto ack;
		}
	}

	NV_ERROR(dev, "no dcb for %d %d 0x%08x\n", or, type, mc);
ack:
	nv_wr32(dev, NV50_PDISPLAY_INTR_1, NV50_PDISPLAY_INTR_1_CLK_UNK10);
	nv_wr32(dev, 0x610030, 0x80000000);
}

static void
nv50_display_unk20_dp_hack(struct drm_device *dev, struct dcb_entry *dcb)
{
	int or = ffs(dcb->or) - 1, link = !(dcb->dpconf.sor.link & 1);
	struct drm_encoder *encoder;
	uint32_t tmp, unk0 = 0, unk1 = 0;

	if (dcb->type != OUTPUT_DP)
		return;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);

		if (nv_encoder->dcb == dcb) {
			unk0 = nv_encoder->dp.unk0;
			unk1 = nv_encoder->dp.unk1;
			break;
		}
	}

	if (unk0 || unk1) {
		tmp  = nv_rd32(dev, NV50_SOR_DP_CTRL(or, link));
		tmp &= 0xfffffe03;
		nv_wr32(dev, NV50_SOR_DP_CTRL(or, link), tmp | unk0);

		tmp  = nv_rd32(dev, NV50_SOR_DP_UNK128(or, link));
		tmp &= 0xfef080c0;
		nv_wr32(dev, NV50_SOR_DP_UNK128(or, link), tmp | unk1);
	}
}

static void
nv50_display_unk20_handler(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 unk30 = nv_rd32(dev, 0x610030), tmp, pclk, script, mc;
	struct dcb_entry *dcb;
	int i, crtc, or, type = OUTPUT_ANY;

	NV_DEBUG_KMS(dev, "0x610030: 0x%08x\n", unk30);
	dcb = dev_priv->evo_irq.dcb;
	if (dcb) {
		nouveau_bios_run_display_table(dev, dcb, 0, -2);
		dev_priv->evo_irq.dcb = NULL;
	}

	/* CRTC clock change requested? */
	crtc = ffs((unk30 & 0x00000600) >> 9) - 1;
	if (crtc >= 0) {
		pclk  = nv_rd32(dev, NV50_PDISPLAY_CRTC_P(crtc, CLOCK));
		pclk &= 0x003fffff;

		nv50_crtc_set_clock(dev, crtc, pclk);

		tmp = nv_rd32(dev, NV50_PDISPLAY_CRTC_CLK_CTRL2(crtc));
		tmp &= ~0x000000f;
		nv_wr32(dev, NV50_PDISPLAY_CRTC_CLK_CTRL2(crtc), tmp);
	}

	/* Nothing needs to be done for the encoder */
	crtc = ffs((unk30 & 0x00000180) >> 7) - 1;
	if (crtc < 0)
		goto ack;
	pclk  = nv_rd32(dev, NV50_PDISPLAY_CRTC_P(crtc, CLOCK)) & 0x003fffff;

	/* Find which encoder is connected to the CRTC */
	for (i = 0; type == OUTPUT_ANY && i < 3; i++) {
		mc = nv_rd32(dev, NV50_PDISPLAY_DAC_MODE_CTRL_P(i));
		NV_DEBUG_KMS(dev, "DAC-%d mc: 0x%08x\n", i, mc);
		if (!(mc & (1 << crtc)))
			continue;

		switch ((mc & 0x00000f00) >> 8) {
		case 0: type = OUTPUT_ANALOG; break;
		case 1: type = OUTPUT_TV; break;
		default:
			NV_ERROR(dev, "invalid mc, DAC-%d: 0x%08x\n", i, mc);
			goto ack;
		}

		or = i;
	}

	for (i = 0; type == OUTPUT_ANY && i < nv50_sor_nr(dev); i++) {
		if (dev_priv->chipset  < 0x90 ||
		    dev_priv->chipset == 0x92 ||
		    dev_priv->chipset == 0xa0)
			mc = nv_rd32(dev, NV50_PDISPLAY_SOR_MODE_CTRL_P(i));
		else
			mc = nv_rd32(dev, NV90_PDISPLAY_SOR_MODE_CTRL_P(i));

		NV_DEBUG_KMS(dev, "SOR-%d mc: 0x%08x\n", i, mc);
		if (!(mc & (1 << crtc)))
			continue;

		switch ((mc & 0x00000f00) >> 8) {
		case 0: type = OUTPUT_LVDS; break;
		case 1: type = OUTPUT_TMDS; break;
		case 2: type = OUTPUT_TMDS; break;
		case 5: type = OUTPUT_TMDS; break;
		case 8: type = OUTPUT_DP; break;
		case 9: type = OUTPUT_DP; break;
		default:
			NV_ERROR(dev, "invalid mc, SOR-%d: 0x%08x\n", i, mc);
			goto ack;
		}

		or = i;
	}

	if (type == OUTPUT_ANY)
		goto ack;

	/* Enable the encoder */
	for (i = 0; i < dev_priv->vbios.dcb.entries; i++) {
		dcb = &dev_priv->vbios.dcb.entry[i];
		if (dcb->type == type && (dcb->or & (1 << or)))
			break;
	}

	if (i == dev_priv->vbios.dcb.entries) {
		NV_ERROR(dev, "no dcb for %d %d 0x%08x\n", or, type, mc);
		goto ack;
	}

	script = nv50_display_script_select(dev, dcb, mc, pclk);
	nouveau_bios_run_display_table(dev, dcb, script, pclk);

	nv50_display_unk20_dp_hack(dev, dcb);

	if (dcb->type != OUTPUT_ANALOG) {
		tmp = nv_rd32(dev, NV50_PDISPLAY_SOR_CLK_CTRL2(or));
		tmp &= ~0x00000f0f;
		if (script & 0x0100)
			tmp |= 0x00000101;
		nv_wr32(dev, NV50_PDISPLAY_SOR_CLK_CTRL2(or), tmp);
	} else {
		nv_wr32(dev, NV50_PDISPLAY_DAC_CLK_CTRL2(or), 0);
	}

	dev_priv->evo_irq.dcb = dcb;
	dev_priv->evo_irq.pclk = pclk;
	dev_priv->evo_irq.script = script;

ack:
	nv_wr32(dev, NV50_PDISPLAY_INTR_1, NV50_PDISPLAY_INTR_1_CLK_UNK20);
	nv_wr32(dev, 0x610030, 0x80000000);
}

/* If programming a TMDS output on a SOR that can also be configured for
 * DisplayPort, make sure NV50_SOR_DP_CTRL_ENABLE is forced off.
 *
 * It looks like the VBIOS TMDS scripts make an attempt at this, however,
 * the VBIOS scripts on at least one board I have only switch it off on
 * link 0, causing a blank display if the output has previously been
 * programmed for DisplayPort.
 */
static void
nv50_display_unk40_dp_set_tmds(struct drm_device *dev, struct dcb_entry *dcb)
{
	int or = ffs(dcb->or) - 1, link = !(dcb->dpconf.sor.link & 1);
	struct drm_encoder *encoder;
	u32 tmp;

	if (dcb->type != OUTPUT_TMDS)
		return;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);

		if (nv_encoder->dcb->type == OUTPUT_DP &&
		    nv_encoder->dcb->or & (1 << or)) {
			tmp  = nv_rd32(dev, NV50_SOR_DP_CTRL(or, link));
			tmp &= ~NV50_SOR_DP_CTRL_ENABLED;
			nv_wr32(dev, NV50_SOR_DP_CTRL(or, link), tmp);
			break;
		}
	}
}

static void
nv50_display_unk40_handler(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct dcb_entry *dcb = dev_priv->evo_irq.dcb;
	u16 script = dev_priv->evo_irq.script;
	u32 unk30 = nv_rd32(dev, 0x610030), pclk = dev_priv->evo_irq.pclk;

	NV_DEBUG_KMS(dev, "0x610030: 0x%08x\n", unk30);
	dev_priv->evo_irq.dcb = NULL;
	if (!dcb)
		goto ack;

	nouveau_bios_run_display_table(dev, dcb, script, -pclk);
	nv50_display_unk40_dp_set_tmds(dev, dcb);

ack:
	nv_wr32(dev, NV50_PDISPLAY_INTR_1, NV50_PDISPLAY_INTR_1_CLK_UNK40);
	nv_wr32(dev, 0x610030, 0x80000000);
	nv_wr32(dev, 0x619494, nv_rd32(dev, 0x619494) | 8);
}

void
nv50_display_irq_handler_bh(struct work_struct *work)
{
	struct drm_nouveau_private *dev_priv =
		container_of(work, struct drm_nouveau_private, irq_work);
	struct drm_device *dev = dev_priv->dev;

	for (;;) {
		uint32_t intr0 = nv_rd32(dev, NV50_PDISPLAY_INTR_0);
		uint32_t intr1 = nv_rd32(dev, NV50_PDISPLAY_INTR_1);

		NV_DEBUG_KMS(dev, "PDISPLAY_INTR_BH 0x%08x 0x%08x\n", intr0, intr1);

		if (intr1 & NV50_PDISPLAY_INTR_1_CLK_UNK10)
			nv50_display_unk10_handler(dev);
		else
		if (intr1 & NV50_PDISPLAY_INTR_1_CLK_UNK20)
			nv50_display_unk20_handler(dev);
		else
		if (intr1 & NV50_PDISPLAY_INTR_1_CLK_UNK40)
			nv50_display_unk40_handler(dev);
		else
			break;
	}

	nv_wr32(dev, NV03_PMC_INTR_EN_0, 1);
}

static void
nv50_display_error_handler(struct drm_device *dev)
{
	uint32_t addr, data;

	nv_wr32(dev, NV50_PDISPLAY_INTR_0, 0x00010000);
	addr = nv_rd32(dev, NV50_PDISPLAY_TRAPPED_ADDR);
	data = nv_rd32(dev, NV50_PDISPLAY_TRAPPED_DATA);

	NV_ERROR(dev, "EvoCh %d Mthd 0x%04x Data 0x%08x (0x%04x 0x%02x)\n",
		 0, addr & 0xffc, data, addr >> 16, (addr >> 12) & 0xf);

	nv_wr32(dev, NV50_PDISPLAY_TRAPPED_ADDR, 0x90000000);
}

void
nv50_display_irq_hotplug_bh(struct work_struct *work)
{
	struct drm_nouveau_private *dev_priv =
		container_of(work, struct drm_nouveau_private, hpd_work);
	struct drm_device *dev = dev_priv->dev;
	struct drm_connector *connector;
	const uint32_t gpio_reg[4] = { 0xe104, 0xe108, 0xe280, 0xe284 };
	uint32_t unplug_mask, plug_mask, change_mask;
	uint32_t hpd0, hpd1 = 0;

	hpd0 = nv_rd32(dev, 0xe054) & nv_rd32(dev, 0xe050);
	if (dev_priv->chipset >= 0x90)
		hpd1 = nv_rd32(dev, 0xe074) & nv_rd32(dev, 0xe070);

	plug_mask   = (hpd0 & 0x0000ffff) | (hpd1 << 16);
	unplug_mask = (hpd0 >> 16) | (hpd1 & 0xffff0000);
	change_mask = plug_mask | unplug_mask;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct drm_encoder_helper_funcs *helper;
		struct nouveau_connector *nv_connector =
			nouveau_connector(connector);
		struct nouveau_encoder *nv_encoder;
		struct dcb_gpio_entry *gpio;
		uint32_t reg;
		bool plugged;

		if (!nv_connector->dcb)
			continue;

		gpio = nouveau_bios_gpio_entry(dev, nv_connector->dcb->gpio_tag);
		if (!gpio || !(change_mask & (1 << gpio->line)))
			continue;

		reg = nv_rd32(dev, gpio_reg[gpio->line >> 3]);
		plugged = !!(reg & (4 << ((gpio->line & 7) << 2)));
		NV_INFO(dev, "%splugged %s\n", plugged ? "" : "un",
			drm_get_connector_name(connector)) ;

		if (!connector->encoder || !connector->encoder->crtc ||
		    !connector->encoder->crtc->enabled)
			continue;
		nv_encoder = nouveau_encoder(connector->encoder);
		helper = connector->encoder->helper_private;

		if (nv_encoder->dcb->type != OUTPUT_DP)
			continue;

		if (plugged)
			helper->dpms(connector->encoder, DRM_MODE_DPMS_ON);
		else
			helper->dpms(connector->encoder, DRM_MODE_DPMS_OFF);
	}

	nv_wr32(dev, 0xe054, nv_rd32(dev, 0xe054));
	if (dev_priv->chipset >= 0x90)
		nv_wr32(dev, 0xe074, nv_rd32(dev, 0xe074));

	drm_helper_hpd_irq_event(dev);
}

void
nv50_display_irq_handler(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t delayed = 0;

	if (nv_rd32(dev, NV50_PMC_INTR_0) & NV50_PMC_INTR_0_HOTPLUG) {
		if (!work_pending(&dev_priv->hpd_work))
			queue_work(dev_priv->wq, &dev_priv->hpd_work);
	}

	while (nv_rd32(dev, NV50_PMC_INTR_0) & NV50_PMC_INTR_0_DISPLAY) {
		uint32_t intr0 = nv_rd32(dev, NV50_PDISPLAY_INTR_0);
		uint32_t intr1 = nv_rd32(dev, NV50_PDISPLAY_INTR_1);
		uint32_t clock;

		NV_DEBUG_KMS(dev, "PDISPLAY_INTR 0x%08x 0x%08x\n", intr0, intr1);

		if (!intr0 && !(intr1 & ~delayed))
			break;

		if (intr0 & 0x00010000) {
			nv50_display_error_handler(dev);
			intr0 &= ~0x00010000;
		}

		if (intr1 & NV50_PDISPLAY_INTR_1_VBLANK_CRTC) {
			nv50_display_vblank_handler(dev, intr1);
			intr1 &= ~NV50_PDISPLAY_INTR_1_VBLANK_CRTC;
		}

		clock = (intr1 & (NV50_PDISPLAY_INTR_1_CLK_UNK10 |
				  NV50_PDISPLAY_INTR_1_CLK_UNK20 |
				  NV50_PDISPLAY_INTR_1_CLK_UNK40));
		if (clock) {
			nv_wr32(dev, NV03_PMC_INTR_EN_0, 0);
			if (!work_pending(&dev_priv->irq_work))
				queue_work(dev_priv->wq, &dev_priv->irq_work);
			delayed |= clock;
			intr1 &= ~clock;
		}

		if (intr0) {
			NV_ERROR(dev, "unknown PDISPLAY_INTR_0: 0x%08x\n", intr0);
			nv_wr32(dev, NV50_PDISPLAY_INTR_0, intr0);
		}

		if (intr1) {
			NV_ERROR(dev,
				 "unknown PDISPLAY_INTR_1: 0x%08x\n", intr1);
			nv_wr32(dev, NV50_PDISPLAY_INTR_1, intr1);
		}
	}
}

