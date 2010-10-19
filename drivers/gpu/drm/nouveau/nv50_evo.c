/*
 * Copyright 2010 Red Hat Inc.
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
#include "nouveau_dma.h"
#include "nouveau_ramht.h"

static void
nv50_evo_channel_del(struct nouveau_channel **pevo)
{
	struct drm_nouveau_private *dev_priv;
	struct nouveau_channel *evo = *pevo;

	if (!evo)
		return;
	*pevo = NULL;

	dev_priv = evo->dev->dev_private;
	dev_priv->evo_alloc &= ~(1 << evo->id);

	nouveau_gpuobj_channel_takedown(evo);
	nouveau_bo_unmap(evo->pushbuf_bo);
	nouveau_bo_ref(NULL, &evo->pushbuf_bo);

	if (evo->user)
		iounmap(evo->user);

	kfree(evo);
}

int
nv50_evo_dmaobj_new(struct nouveau_channel *evo, u32 class, u32 name,
		    u32 tile_flags, u32 magic_flags, u32 offset, u32 limit)
{
	struct drm_nouveau_private *dev_priv = evo->dev->dev_private;
	struct drm_device *dev = evo->dev;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(dev, dev_priv->evo, 6*4, 32, 0, &obj);
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
nv50_evo_channel_new(struct drm_device *dev, struct nouveau_channel **pevo)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *evo;
	int ret;

	evo = kzalloc(sizeof(struct nouveau_channel), GFP_KERNEL);
	if (!evo)
		return -ENOMEM;
	*pevo = evo;

	for (evo->id = 0; evo->id < 5; evo->id++) {
		if (dev_priv->evo_alloc & (1 << evo->id))
			continue;

		dev_priv->evo_alloc |= (1 << evo->id);
		break;
	}

	if (evo->id == 5) {
		kfree(evo);
		return -ENODEV;
	}

	evo->dev = dev;
	evo->user_get = 4;
	evo->user_put = 0;

	ret = nouveau_bo_new(dev, NULL, 4096, 0, TTM_PL_FLAG_VRAM, 0, 0,
			     false, true, &evo->pushbuf_bo);
	if (ret == 0)
		ret = nouveau_bo_pin(evo->pushbuf_bo, TTM_PL_FLAG_VRAM);
	if (ret) {
		NV_ERROR(dev, "Error creating EVO DMA push buffer: %d\n", ret);
		nv50_evo_channel_del(pevo);
		return ret;
	}

	ret = nouveau_bo_map(evo->pushbuf_bo);
	if (ret) {
		NV_ERROR(dev, "Error mapping EVO DMA push buffer: %d\n", ret);
		nv50_evo_channel_del(pevo);
		return ret;
	}

	evo->user = ioremap(pci_resource_start(dev->pdev, 0) +
			    NV50_PDISPLAY_USER(evo->id), PAGE_SIZE);
	if (!evo->user) {
		NV_ERROR(dev, "Error mapping EVO control regs.\n");
		nv50_evo_channel_del(pevo);
		return -ENOMEM;
	}

	/* bind primary evo channel's ramht to the channel */
	if (dev_priv->evo && evo != dev_priv->evo)
		nouveau_ramht_ref(dev_priv->evo->ramht, &evo->ramht, NULL);

	return 0;
}

static int
nv50_evo_channel_init(struct nouveau_channel *evo)
{
	struct drm_nouveau_private *dev_priv = evo->dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	struct drm_device *dev = evo->dev;
	int id = evo->id, ret, i;
	u64 start;
	u32 tmp;

	/* taken from nv bug #12637, attempts to un-wedge the hw if it's
	 * stuck in some unspecified state
	 */
	start = ptimer->read(dev);
	nv_wr32(dev, NV50_PDISPLAY_EVO_CTRL(id), 0x2b00);
	while ((tmp = nv_rd32(dev, NV50_PDISPLAY_EVO_CTRL(id))) & 0x1e0000) {
		if ((tmp & 0x9f0000) == 0x20000)
			nv_wr32(dev, NV50_PDISPLAY_EVO_CTRL(id), tmp | 0x800000);

		if ((tmp & 0x3f0000) == 0x30000)
			nv_wr32(dev, NV50_PDISPLAY_EVO_CTRL(id), tmp | 0x200000);

		if (ptimer->read(dev) - start > 1000000000ULL) {
			NV_ERROR(dev, "timeout: (0x610200 & 0x1e0000) != 0\n");
			NV_ERROR(dev, "0x610200 = 0x%08x\n", tmp);
			return -EBUSY;
		}
	}

	nv_wr32(dev, NV50_PDISPLAY_EVO_CTRL(id), 0x1000b03);
	if (!nv_wait(dev, NV50_PDISPLAY_EVO_CTRL(id),
		     0x40000000, 0x40000000)) {
		NV_ERROR(dev, "timeout: (0x610200 & 0x40000000) == 0x40000000\n");
		NV_ERROR(dev, "0x610200 = 0x%08x\n",
			  nv_rd32(dev, NV50_PDISPLAY_EVO_CTRL(id)));
		return -EBUSY;
	}

	/* initialise fifo */
	nv_wr32(dev, NV50_PDISPLAY_EVO_DMA_CB(id),
		((evo->pushbuf_bo->bo.mem.start << PAGE_SHIFT) >> 8) |
		NV50_PDISPLAY_EVO_DMA_CB_LOCATION_VRAM |
		NV50_PDISPLAY_EVO_DMA_CB_VALID);
	nv_wr32(dev, NV50_PDISPLAY_EVO_UNK2(id), 0x00010000);
	nv_wr32(dev, NV50_PDISPLAY_EVO_HASH_TAG(id), id);
	if (!nv_wait(dev, NV50_PDISPLAY_EVO_CTRL(id), 0x80000000, 0x00000000)) {
		NV_ERROR(dev, "timeout: (0x610200 & 0x80000000) == 0\n");
		NV_ERROR(dev, "0x610200 = 0x%08x\n",
			 nv_rd32(dev, NV50_PDISPLAY_EVO_CTRL(id)));
		return -EBUSY;
	}
	nv_wr32(dev, NV50_PDISPLAY_EVO_CTRL(id),
		(nv_rd32(dev, NV50_PDISPLAY_EVO_CTRL(id)) & ~0x00000003) |
		 NV50_PDISPLAY_EVO_CTRL_DMA_ENABLED);
	nv_wr32(dev, NV50_PDISPLAY_USER_PUT(id), 0);
	nv_wr32(dev, NV50_PDISPLAY_EVO_CTRL(id), 0x01000003 |
		     NV50_PDISPLAY_EVO_CTRL_DMA_ENABLED);

	/* enable error reporting on the channel */
	nv_mask(dev, 0x610028, 0x00000000, 0x00010001 << id);

	evo->dma.max = (4096/4) - 2;
	evo->dma.put = 0;
	evo->dma.cur = evo->dma.put;
	evo->dma.free = evo->dma.max - evo->dma.cur;

	ret = RING_SPACE(evo, NOUVEAU_DMA_SKIPS);
	if (ret)
		return ret;

	for (i = 0; i < NOUVEAU_DMA_SKIPS; i++)
		OUT_RING(evo, 0);

	return 0;
}

static void
nv50_evo_channel_fini(struct nouveau_channel *evo)
{
	struct drm_device *dev = evo->dev;

	nv_wr32(dev, NV50_PDISPLAY_EVO_CTRL(evo->id), 0);
	if (!nv_wait(dev, NV50_PDISPLAY_EVO_CTRL(evo->id), 0x1e0000, 0)) {
		NV_ERROR(dev, "timeout: (0x610200 & 0x1e0000) == 0\n");
		NV_ERROR(dev, "0x610200 = 0x%08x\n",
			  nv_rd32(dev, NV50_PDISPLAY_EVO_CTRL(evo->id)));
	}
}

static int
nv50_evo_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramht = NULL;
	struct nouveau_channel *evo;
	int ret;

	/* create primary evo channel, the one we use for modesetting
	 * purporses
	 */
	ret = nv50_evo_channel_new(dev, &dev_priv->evo);
	if (ret)
		return ret;
	evo = dev_priv->evo;

	/* setup object management on it, any other evo channel will
	 * use this also as there's no per-channel support on the
	 * hardware
	 */
	ret = nouveau_gpuobj_new(dev, NULL, 32768, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC, &evo->ramin);
	if (ret) {
		NV_ERROR(dev, "Error allocating EVO channel memory: %d\n", ret);
		nv50_evo_channel_del(&dev_priv->evo);
		return ret;
	}

	ret = drm_mm_init(&evo->ramin_heap, 0, 32768);
	if (ret) {
		NV_ERROR(dev, "Error initialising EVO PRAMIN heap: %d\n", ret);
		nv50_evo_channel_del(&dev_priv->evo);
		return ret;
	}

	ret = nouveau_gpuobj_new(dev, evo, 4096, 16, 0, &ramht);
	if (ret) {
		NV_ERROR(dev, "Unable to allocate EVO RAMHT: %d\n", ret);
		nv50_evo_channel_del(&dev_priv->evo);
		return ret;
	}

	ret = nouveau_ramht_new(dev, ramht, &evo->ramht);
	nouveau_gpuobj_ref(NULL, &ramht);
	if (ret) {
		nv50_evo_channel_del(&dev_priv->evo);
		return ret;
	}

	/* create some default objects for the scanout memtypes we support */
	if (dev_priv->chipset != 0x50) {
		ret = nv50_evo_dmaobj_new(evo, 0x3d, NvEvoFB16, 0x70, 0x19,
					  0, 0xffffffff);
		if (ret) {
			nv50_evo_channel_del(&dev_priv->evo);
			return ret;
		}


		ret = nv50_evo_dmaobj_new(evo, 0x3d, NvEvoFB32, 0x7a, 0x19,
					  0, 0xffffffff);
		if (ret) {
			nv50_evo_channel_del(&dev_priv->evo);
			return ret;
		}
	}

	ret = nv50_evo_dmaobj_new(evo, 0x3d, NvEvoVRAM, 0, 0x19,
				  0, dev_priv->vram_size);
	if (ret) {
		nv50_evo_channel_del(&dev_priv->evo);
		return ret;
	}

	return 0;
}

int
nv50_evo_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	if (!dev_priv->evo) {
		ret = nv50_evo_create(dev);
		if (ret)
			return ret;
	}

	return nv50_evo_channel_init(dev_priv->evo);
}

void
nv50_evo_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->evo) {
		nv50_evo_channel_fini(dev_priv->evo);
		nv50_evo_channel_del(&dev_priv->evo);
	}
}
