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
#include "nv50_display.h"

static void
nv50_evo_channel_del(struct nouveau_channel **pevo)
{
	struct nouveau_channel *evo = *pevo;

	if (!evo)
		return;
	*pevo = NULL;

	nouveau_gpuobj_channel_takedown(evo);
	nouveau_bo_unmap(evo->pushbuf_bo);
	nouveau_bo_ref(NULL, &evo->pushbuf_bo);

	if (evo->user)
		iounmap(evo->user);

	kfree(evo);
}

void
nv50_evo_dmaobj_init(struct nouveau_gpuobj *obj, u32 memtype, u64 base, u64 size)
{
	struct drm_nouveau_private *dev_priv = obj->dev->dev_private;
	u32 flags5;

	if (dev_priv->chipset < 0xc0) {
		/* not supported on 0x50, specified in format mthd */
		if (dev_priv->chipset == 0x50)
			memtype = 0;
		flags5 = 0x00010000;
	} else {
		if (memtype & 0x80000000)
			flags5 = 0x00000000; /* large pages */
		else
			flags5 = 0x00020000;
	}

	nv50_gpuobj_dma_init(obj, 0, 0x3d, base, size, NV_MEM_TARGET_VRAM,
			     NV_MEM_ACCESS_RW, (memtype >> 8) & 0xff, 0);
	nv_wo32(obj, 0x14, flags5);
	dev_priv->engine.instmem.flush(obj->dev);
}

int
nv50_evo_dmaobj_new(struct nouveau_channel *evo, u32 handle, u32 memtype,
		    u64 base, u64 size, struct nouveau_gpuobj **pobj)
{
	struct nv50_display *disp = nv50_display(evo->dev);
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(evo->dev, disp->master, 6*4, 32, 0, &obj);
	if (ret)
		return ret;
	obj->engine = NVOBJ_ENGINE_DISPLAY;

	nv50_evo_dmaobj_init(obj, memtype, base, size);

	ret = nouveau_ramht_insert(evo, handle, obj);
	if (ret)
		goto out;

	if (pobj)
		nouveau_gpuobj_ref(obj, pobj);
out:
	nouveau_gpuobj_ref(NULL, &obj);
	return ret;
}

static int
nv50_evo_channel_new(struct drm_device *dev, int chid,
		     struct nouveau_channel **pevo)
{
	struct nv50_display *disp = nv50_display(dev);
	struct nouveau_channel *evo;
	int ret;

	evo = kzalloc(sizeof(struct nouveau_channel), GFP_KERNEL);
	if (!evo)
		return -ENOMEM;
	*pevo = evo;

	evo->id = chid;
	evo->dev = dev;
	evo->user_get = 4;
	evo->user_put = 0;

	ret = nouveau_bo_new(dev, NULL, 4096, 0, TTM_PL_FLAG_VRAM, 0, 0,
			     &evo->pushbuf_bo);
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
	if (disp->master && evo != disp->master)
		nouveau_ramht_ref(disp->master->ramht, &evo->ramht, NULL);

	return 0;
}

static int
nv50_evo_channel_init(struct nouveau_channel *evo)
{
	struct drm_device *dev = evo->dev;
	int id = evo->id, ret, i;
	u64 pushbuf = evo->pushbuf_bo->bo.mem.start << PAGE_SHIFT;
	u32 tmp;

	tmp = nv_rd32(dev, NV50_PDISPLAY_EVO_CTRL(id));
	if ((tmp & 0x009f0000) == 0x00020000)
		nv_wr32(dev, NV50_PDISPLAY_EVO_CTRL(id), tmp | 0x00800000);

	tmp = nv_rd32(dev, NV50_PDISPLAY_EVO_CTRL(id));
	if ((tmp & 0x003f0000) == 0x00030000)
		nv_wr32(dev, NV50_PDISPLAY_EVO_CTRL(id), tmp | 0x00600000);

	/* initialise fifo */
	nv_wr32(dev, NV50_PDISPLAY_EVO_DMA_CB(id), pushbuf >> 8 |
		     NV50_PDISPLAY_EVO_DMA_CB_LOCATION_VRAM |
		     NV50_PDISPLAY_EVO_DMA_CB_VALID);
	nv_wr32(dev, NV50_PDISPLAY_EVO_UNK2(id), 0x00010000);
	nv_wr32(dev, NV50_PDISPLAY_EVO_HASH_TAG(id), id);
	nv_mask(dev, NV50_PDISPLAY_EVO_CTRL(id), NV50_PDISPLAY_EVO_CTRL_DMA,
		     NV50_PDISPLAY_EVO_CTRL_DMA_ENABLED);

	nv_wr32(dev, NV50_PDISPLAY_USER_PUT(id), 0x00000000);
	nv_wr32(dev, NV50_PDISPLAY_EVO_CTRL(id), 0x01000003 |
		     NV50_PDISPLAY_EVO_CTRL_DMA_ENABLED);
	if (!nv_wait(dev, NV50_PDISPLAY_EVO_CTRL(id), 0x80000000, 0x00000000)) {
		NV_ERROR(dev, "EvoCh %d init timeout: 0x%08x\n", id,
			 nv_rd32(dev, NV50_PDISPLAY_EVO_CTRL(id)));
		return -EBUSY;
	}

	/* enable error reporting on the channel */
	nv_mask(dev, 0x610028, 0x00000000, 0x00010001 << id);

	evo->dma.max = (4096/4) - 2;
	evo->dma.max &= ~7;
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
	int id = evo->id;

	nv_mask(dev, 0x610028, 0x00010001 << id, 0x00000000);
	nv_mask(dev, NV50_PDISPLAY_EVO_CTRL(id), 0x00001010, 0x00001000);
	nv_wr32(dev, NV50_PDISPLAY_INTR_0, (1 << id));
	nv_mask(dev, NV50_PDISPLAY_EVO_CTRL(id), 0x00000003, 0x00000000);
	if (!nv_wait(dev, NV50_PDISPLAY_EVO_CTRL(id), 0x001e0000, 0x00000000)) {
		NV_ERROR(dev, "EvoCh %d takedown timeout: 0x%08x\n", id,
			 nv_rd32(dev, NV50_PDISPLAY_EVO_CTRL(id)));
	}
}

static void
nv50_evo_destroy(struct drm_device *dev)
{
	struct nv50_display *disp = nv50_display(dev);
	int i;

	for (i = 0; i < 2; i++) {
		if (disp->crtc[i].sem.bo) {
			nouveau_bo_unmap(disp->crtc[i].sem.bo);
			nouveau_bo_ref(NULL, &disp->crtc[i].sem.bo);
		}
		nv50_evo_channel_del(&disp->crtc[i].sync);
	}
	nouveau_gpuobj_ref(NULL, &disp->ntfy);
	nv50_evo_channel_del(&disp->master);
}

static int
nv50_evo_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_display *disp = nv50_display(dev);
	struct nouveau_gpuobj *ramht = NULL;
	struct nouveau_channel *evo;
	int ret, i, j;

	/* create primary evo channel, the one we use for modesetting
	 * purporses
	 */
	ret = nv50_evo_channel_new(dev, 0, &disp->master);
	if (ret)
		return ret;
	evo = disp->master;

	/* setup object management on it, any other evo channel will
	 * use this also as there's no per-channel support on the
	 * hardware
	 */
	ret = nouveau_gpuobj_new(dev, NULL, 32768, 65536,
				 NVOBJ_FLAG_ZERO_ALLOC, &evo->ramin);
	if (ret) {
		NV_ERROR(dev, "Error allocating EVO channel memory: %d\n", ret);
		goto err;
	}

	ret = drm_mm_init(&evo->ramin_heap, 0, 32768);
	if (ret) {
		NV_ERROR(dev, "Error initialising EVO PRAMIN heap: %d\n", ret);
		goto err;
	}

	ret = nouveau_gpuobj_new(dev, evo, 4096, 16, 0, &ramht);
	if (ret) {
		NV_ERROR(dev, "Unable to allocate EVO RAMHT: %d\n", ret);
		goto err;
	}

	ret = nouveau_ramht_new(dev, ramht, &evo->ramht);
	nouveau_gpuobj_ref(NULL, &ramht);
	if (ret)
		goto err;

	/* not sure exactly what this is..
	 *
	 * the first dword of the structure is used by nvidia to wait on
	 * full completion of an EVO "update" command.
	 *
	 * method 0x8c on the master evo channel will fill a lot more of
	 * this structure with some undefined info
	 */
	ret = nouveau_gpuobj_new(dev, disp->master, 0x1000, 0,
				 NVOBJ_FLAG_ZERO_ALLOC, &disp->ntfy);
	if (ret)
		goto err;

	ret = nv50_evo_dmaobj_new(disp->master, NvEvoSync, 0x0000,
				  disp->ntfy->vinst, disp->ntfy->size, NULL);
	if (ret)
		goto err;

	/* create some default objects for the scanout memtypes we support */
	ret = nv50_evo_dmaobj_new(disp->master, NvEvoVRAM, 0x0000,
				  0, dev_priv->vram_size, NULL);
	if (ret)
		goto err;

	ret = nv50_evo_dmaobj_new(disp->master, NvEvoVRAM_LP, 0x80000000,
				  0, dev_priv->vram_size, NULL);
	if (ret)
		goto err;

	ret = nv50_evo_dmaobj_new(disp->master, NvEvoFB32, 0x80000000 |
				  (dev_priv->chipset < 0xc0 ? 0x7a00 : 0xfe00),
				  0, dev_priv->vram_size, NULL);
	if (ret)
		goto err;

	ret = nv50_evo_dmaobj_new(disp->master, NvEvoFB16, 0x80000000 |
				  (dev_priv->chipset < 0xc0 ? 0x7000 : 0xfe00),
				  0, dev_priv->vram_size, NULL);
	if (ret)
		goto err;

	/* create "display sync" channels and other structures we need
	 * to implement page flipping
	 */
	for (i = 0; i < 2; i++) {
		struct nv50_display_crtc *dispc = &disp->crtc[i];
		u64 offset;

		ret = nv50_evo_channel_new(dev, 1 + i, &dispc->sync);
		if (ret)
			goto err;

		ret = nouveau_bo_new(dev, NULL, 4096, 0x1000, TTM_PL_FLAG_VRAM,
				     0, 0x0000, &dispc->sem.bo);
		if (!ret) {
			offset = dispc->sem.bo->bo.mem.start << PAGE_SHIFT;

			ret = nouveau_bo_pin(dispc->sem.bo, TTM_PL_FLAG_VRAM);
			if (!ret)
				ret = nouveau_bo_map(dispc->sem.bo);
			if (ret)
				nouveau_bo_ref(NULL, &dispc->sem.bo);
		}

		if (ret)
			goto err;

		ret = nv50_evo_dmaobj_new(dispc->sync, NvEvoSync, 0x0000,
					  offset, 4096, NULL);
		if (ret)
			goto err;

		ret = nv50_evo_dmaobj_new(dispc->sync, NvEvoVRAM_LP, 0x80000000,
					  0, dev_priv->vram_size, NULL);
		if (ret)
			goto err;

		ret = nv50_evo_dmaobj_new(dispc->sync, NvEvoFB32, 0x80000000 |
					  (dev_priv->chipset < 0xc0 ?
					  0x7a00 : 0xfe00),
					  0, dev_priv->vram_size, NULL);
		if (ret)
			goto err;

		ret = nv50_evo_dmaobj_new(dispc->sync, NvEvoFB16, 0x80000000 |
					  (dev_priv->chipset < 0xc0 ?
					  0x7000 : 0xfe00),
					  0, dev_priv->vram_size, NULL);
		if (ret)
			goto err;

		for (j = 0; j < 4096; j += 4)
			nouveau_bo_wr32(dispc->sem.bo, j / 4, 0x74b1e000);
		dispc->sem.offset = 0;
	}

	return 0;

err:
	nv50_evo_destroy(dev);
	return ret;
}

int
nv50_evo_init(struct drm_device *dev)
{
	struct nv50_display *disp = nv50_display(dev);
	int ret, i;

	if (!disp->master) {
		ret = nv50_evo_create(dev);
		if (ret)
			return ret;
	}

	ret = nv50_evo_channel_init(disp->master);
	if (ret)
		return ret;

	for (i = 0; i < 2; i++) {
		ret = nv50_evo_channel_init(disp->crtc[i].sync);
		if (ret)
			return ret;
	}

	return 0;
}

void
nv50_evo_fini(struct drm_device *dev)
{
	struct nv50_display *disp = nv50_display(dev);
	int i;

	for (i = 0; i < 2; i++) {
		if (disp->crtc[i].sync)
			nv50_evo_channel_fini(disp->crtc[i].sync);
	}

	if (disp->master)
		nv50_evo_channel_fini(disp->master);

	nv50_evo_destroy(dev);
}
