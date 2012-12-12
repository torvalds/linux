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

#include <drm/drmP.h>

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nv50_display.h"

#include <core/gpuobj.h>

#include <subdev/timer.h>
#include <subdev/fb.h>

static u32
nv50_evo_rd32(struct nouveau_object *object, u32 addr)
{
	void __iomem *iomem = object->oclass->ofuncs->rd08;
	return ioread32_native(iomem + addr);
}

static void
nv50_evo_wr32(struct nouveau_object *object, u32 addr, u32 data)
{
	void __iomem *iomem = object->oclass->ofuncs->rd08;
	iowrite32_native(data, iomem + addr);
}

static void
nv50_evo_channel_del(struct nouveau_channel **pevo)
{
	struct nouveau_channel *evo = *pevo;

	if (!evo)
		return;
	*pevo = NULL;

	nouveau_bo_unmap(evo->push.buffer);
	nouveau_bo_ref(NULL, &evo->push.buffer);

	if (evo->object)
		iounmap(evo->object->oclass->ofuncs);

	kfree(evo);
}

int
nv50_evo_dmaobj_new(struct nouveau_channel *evo, u32 handle, u32 memtype,
		    u64 base, u64 size, struct nouveau_gpuobj **pobj)
{
	struct drm_device *dev = evo->fence;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv50_display *disp = nv50_display(dev);
	u32 dmao = disp->dmao;
	u32 hash = disp->hash;
	u32 flags5;

	if (nv_device(drm->device)->chipset < 0xc0) {
		/* not supported on 0x50, specified in format mthd */
		if (nv_device(drm->device)->chipset == 0x50)
			memtype = 0;
		flags5 = 0x00010000;
	} else {
		if (memtype & 0x80000000)
			flags5 = 0x00000000; /* large pages */
		else
			flags5 = 0x00020000;
	}

	nv_wo32(disp->ramin, dmao + 0x00, 0x0019003d | (memtype << 22));
	nv_wo32(disp->ramin, dmao + 0x04, lower_32_bits(base + size - 1));
	nv_wo32(disp->ramin, dmao + 0x08, lower_32_bits(base));
	nv_wo32(disp->ramin, dmao + 0x0c, upper_32_bits(base + size - 1) << 24 |
					  upper_32_bits(base));
	nv_wo32(disp->ramin, dmao + 0x10, 0x00000000);
	nv_wo32(disp->ramin, dmao + 0x14, flags5);

	nv_wo32(disp->ramin, hash + 0x00, handle);
	nv_wo32(disp->ramin, hash + 0x04, (evo->handle << 28) | (dmao << 10) |
					   evo->handle);

	disp->dmao += 0x20;
	disp->hash += 0x08;
	return 0;
}

static int
nv50_evo_channel_new(struct drm_device *dev, int chid,
		     struct nouveau_channel **pevo)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nv50_display *disp = nv50_display(dev);
	struct nouveau_channel *evo;
	int ret;

	evo = kzalloc(sizeof(struct nouveau_channel), GFP_KERNEL);
	if (!evo)
		return -ENOMEM;
	*pevo = evo;

	evo->drm = drm;
	evo->handle = chid;
	evo->fence = dev;
	evo->user_get = 4;
	evo->user_put = 0;

	ret = nouveau_bo_new(dev, 4096, 0, TTM_PL_FLAG_VRAM, 0, 0, NULL,
			     &evo->push.buffer);
	if (ret == 0)
		ret = nouveau_bo_pin(evo->push.buffer, TTM_PL_FLAG_VRAM);
	if (ret) {
		NV_ERROR(drm, "Error creating EVO DMA push buffer: %d\n", ret);
		nv50_evo_channel_del(pevo);
		return ret;
	}

	ret = nouveau_bo_map(evo->push.buffer);
	if (ret) {
		NV_ERROR(drm, "Error mapping EVO DMA push buffer: %d\n", ret);
		nv50_evo_channel_del(pevo);
		return ret;
	}

	evo->object = kzalloc(sizeof(*evo->object), GFP_KERNEL);
#ifdef NOUVEAU_OBJECT_MAGIC
	evo->object->_magic = NOUVEAU_OBJECT_MAGIC;
#endif
	evo->object->parent = nv_object(disp->ramin)->parent;
	evo->object->engine = nv_object(disp->ramin)->engine;
	evo->object->oclass =
		kzalloc(sizeof(*evo->object->oclass), GFP_KERNEL);
	evo->object->oclass->ofuncs =
		kzalloc(sizeof(*evo->object->oclass->ofuncs), GFP_KERNEL);
	evo->object->oclass->ofuncs->rd32 = nv50_evo_rd32;
	evo->object->oclass->ofuncs->wr32 = nv50_evo_wr32;
	evo->object->oclass->ofuncs->rd08 =
		ioremap(pci_resource_start(dev->pdev, 0) +
			NV50_PDISPLAY_USER(evo->handle), PAGE_SIZE);
	return 0;
}

static int
nv50_evo_channel_init(struct nouveau_channel *evo)
{
	struct nouveau_drm *drm = evo->drm;
	struct nouveau_device *device = nv_device(drm->device);
	int id = evo->handle, ret, i;
	u64 pushbuf = evo->push.buffer->bo.offset;
	u32 tmp;

	tmp = nv_rd32(device, NV50_PDISPLAY_EVO_CTRL(id));
	if ((tmp & 0x009f0000) == 0x00020000)
		nv_wr32(device, NV50_PDISPLAY_EVO_CTRL(id), tmp | 0x00800000);

	tmp = nv_rd32(device, NV50_PDISPLAY_EVO_CTRL(id));
	if ((tmp & 0x003f0000) == 0x00030000)
		nv_wr32(device, NV50_PDISPLAY_EVO_CTRL(id), tmp | 0x00600000);

	/* initialise fifo */
	nv_wr32(device, NV50_PDISPLAY_EVO_DMA_CB(id), pushbuf >> 8 |
		     NV50_PDISPLAY_EVO_DMA_CB_LOCATION_VRAM |
		     NV50_PDISPLAY_EVO_DMA_CB_VALID);
	nv_wr32(device, NV50_PDISPLAY_EVO_UNK2(id), 0x00010000);
	nv_wr32(device, NV50_PDISPLAY_EVO_HASH_TAG(id), id);
	nv_mask(device, NV50_PDISPLAY_EVO_CTRL(id), NV50_PDISPLAY_EVO_CTRL_DMA,
		     NV50_PDISPLAY_EVO_CTRL_DMA_ENABLED);

	nv_wr32(device, NV50_PDISPLAY_USER_PUT(id), 0x00000000);
	nv_wr32(device, NV50_PDISPLAY_EVO_CTRL(id), 0x01000003 |
		     NV50_PDISPLAY_EVO_CTRL_DMA_ENABLED);
	if (!nv_wait(device, NV50_PDISPLAY_EVO_CTRL(id), 0x80000000, 0x00000000)) {
		NV_ERROR(drm, "EvoCh %d init timeout: 0x%08x\n", id,
			 nv_rd32(device, NV50_PDISPLAY_EVO_CTRL(id)));
		return -EBUSY;
	}

	/* enable error reporting on the channel */
	nv_mask(device, 0x610028, 0x00000000, 0x00010001 << id);

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
	struct nouveau_drm *drm = evo->drm;
	struct nouveau_device *device = nv_device(drm->device);
	int id = evo->handle;

	nv_mask(device, 0x610028, 0x00010001 << id, 0x00000000);
	nv_mask(device, NV50_PDISPLAY_EVO_CTRL(id), 0x00001010, 0x00001000);
	nv_wr32(device, NV50_PDISPLAY_INTR_0, (1 << id));
	nv_mask(device, NV50_PDISPLAY_EVO_CTRL(id), 0x00000003, 0x00000000);
	if (!nv_wait(device, NV50_PDISPLAY_EVO_CTRL(id), 0x001e0000, 0x00000000)) {
		NV_ERROR(drm, "EvoCh %d takedown timeout: 0x%08x\n", id,
			 nv_rd32(device, NV50_PDISPLAY_EVO_CTRL(id)));
	}
}

void
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
	nv50_evo_channel_del(&disp->master);
	nouveau_gpuobj_ref(NULL, &disp->ramin);
}

int
nv50_evo_create(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	struct nv50_display *disp = nv50_display(dev);
	struct nouveau_channel *evo;
	int ret, i, j;

	/* setup object management on it, any other evo channel will
	 * use this also as there's no per-channel support on the
	 * hardware
	 */
	ret = nouveau_gpuobj_new(drm->device, NULL, 32768, 65536,
				 NVOBJ_FLAG_ZERO_ALLOC, &disp->ramin);
	if (ret) {
		NV_ERROR(drm, "Error allocating EVO channel memory: %d\n", ret);
		goto err;
	}

	disp->hash = 0x0000;
	disp->dmao = 0x1000;

	/* create primary evo channel, the one we use for modesetting
	 * purporses
	 */
	ret = nv50_evo_channel_new(dev, 0, &disp->master);
	if (ret)
		return ret;
	evo = disp->master;

	ret = nv50_evo_dmaobj_new(disp->master, NvEvoSync, 0x0000,
				  disp->ramin->addr + 0x2000, 0x1000, NULL);
	if (ret)
		goto err;

	/* create some default objects for the scanout memtypes we support */
	ret = nv50_evo_dmaobj_new(disp->master, NvEvoVRAM, 0x0000,
				  0, pfb->ram.size, NULL);
	if (ret)
		goto err;

	ret = nv50_evo_dmaobj_new(disp->master, NvEvoVRAM_LP, 0x80000000,
				  0, pfb->ram.size, NULL);
	if (ret)
		goto err;

	ret = nv50_evo_dmaobj_new(disp->master, NvEvoFB32, 0x80000000 |
				  (nv_device(drm->device)->chipset < 0xc0 ? 0x7a : 0xfe),
				  0, pfb->ram.size, NULL);
	if (ret)
		goto err;

	ret = nv50_evo_dmaobj_new(disp->master, NvEvoFB16, 0x80000000 |
				  (nv_device(drm->device)->chipset < 0xc0 ? 0x70 : 0xfe),
				  0, pfb->ram.size, NULL);
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

		ret = nouveau_bo_new(dev, 4096, 0x1000, TTM_PL_FLAG_VRAM,
				     0, 0x0000, NULL, &dispc->sem.bo);
		if (!ret) {
			ret = nouveau_bo_pin(dispc->sem.bo, TTM_PL_FLAG_VRAM);
			if (!ret)
				ret = nouveau_bo_map(dispc->sem.bo);
			if (ret)
				nouveau_bo_ref(NULL, &dispc->sem.bo);
			offset = dispc->sem.bo->bo.offset;
		}

		if (ret)
			goto err;

		ret = nv50_evo_dmaobj_new(dispc->sync, NvEvoSync, 0x0000,
					  offset, 4096, NULL);
		if (ret)
			goto err;

		ret = nv50_evo_dmaobj_new(dispc->sync, NvEvoVRAM_LP, 0x80000000,
					  0, pfb->ram.size, NULL);
		if (ret)
			goto err;

		ret = nv50_evo_dmaobj_new(dispc->sync, NvEvoFB32, 0x80000000 |
					  (nv_device(drm->device)->chipset < 0xc0 ?
					  0x7a : 0xfe),
					  0, pfb->ram.size, NULL);
		if (ret)
			goto err;

		ret = nv50_evo_dmaobj_new(dispc->sync, NvEvoFB16, 0x80000000 |
					  (nv_device(drm->device)->chipset < 0xc0 ?
					  0x70 : 0xfe),
					  0, pfb->ram.size, NULL);
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
}
