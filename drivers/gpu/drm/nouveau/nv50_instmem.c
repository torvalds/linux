/*
 * Copyright (C) 2007 Ben Skeggs.
 *
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
#include "drm.h"
#include "nouveau_drv.h"

struct nv50_instmem_priv {
	uint32_t save1700[5]; /* 0x1700->0x1710 */

	struct nouveau_gpuobj *pramin_pt;
	struct nouveau_gpuobj *pramin_bar;
	struct nouveau_gpuobj *fb_bar;
};

static void
nv50_channel_del(struct nouveau_channel **pchan)
{
	struct nouveau_channel *chan;

	chan = *pchan;
	*pchan = NULL;
	if (!chan)
		return;

	nouveau_gpuobj_ref(NULL, &chan->ramfc);
	nouveau_gpuobj_ref(NULL, &chan->vm_pd);
	if (chan->ramin_heap.free_stack.next)
		drm_mm_takedown(&chan->ramin_heap);
	nouveau_gpuobj_ref(NULL, &chan->ramin);
	kfree(chan);
}

static int
nv50_channel_new(struct drm_device *dev, u32 size,
		 struct nouveau_channel **pchan)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 pgd = (dev_priv->chipset == 0x50) ? 0x1400 : 0x0200;
	u32  fc = (dev_priv->chipset == 0x50) ? 0x0000 : 0x4200;
	struct nouveau_channel *chan;
	int ret;

	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;
	chan->dev = dev;

	ret = nouveau_gpuobj_new(dev, NULL, size, 0x1000, 0, &chan->ramin);
	if (ret) {
		nv50_channel_del(&chan);
		return ret;
	}

	ret = drm_mm_init(&chan->ramin_heap, 0x6000, chan->ramin->size);
	if (ret) {
		nv50_channel_del(&chan);
		return ret;
	}

	ret = nouveau_gpuobj_new_fake(dev, chan->ramin->pinst == ~0 ? ~0 :
				      chan->ramin->pinst + pgd,
				      chan->ramin->vinst + pgd,
				      0x4000, NVOBJ_FLAG_ZERO_ALLOC,
				      &chan->vm_pd);
	if (ret) {
		nv50_channel_del(&chan);
		return ret;
	}

	ret = nouveau_gpuobj_new_fake(dev, chan->ramin->pinst == ~0 ? ~0 :
				      chan->ramin->pinst + fc,
				      chan->ramin->vinst + fc, 0x100,
				      NVOBJ_FLAG_ZERO_ALLOC, &chan->ramfc);
	if (ret) {
		nv50_channel_del(&chan);
		return ret;
	}

	*pchan = chan;
	return 0;
}

int
nv50_instmem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv;
	struct nouveau_channel *chan;
	int ret, i;
	u32 tmp;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_priv->engine.instmem.priv = priv;

	/* Save state, will restore at takedown. */
	for (i = 0x1700; i <= 0x1710; i += 4)
		priv->save1700[(i-0x1700)/4] = nv_rd32(dev, i);

	/* Global PRAMIN heap */
	ret = drm_mm_init(&dev_priv->ramin_heap, 0, dev_priv->ramin_size);
	if (ret) {
		NV_ERROR(dev, "Failed to init RAMIN heap\n");
		return -ENOMEM;
	}

	/* we need a channel to plug into the hw to control the BARs */
	ret = nv50_channel_new(dev, 128*1024, &dev_priv->fifos[0]);
	if (ret)
		return ret;
	chan = dev_priv->fifos[127] = dev_priv->fifos[0];

	/* allocate page table for PRAMIN BAR */
	ret = nouveau_gpuobj_new(dev, chan, (dev_priv->ramin_size >> 12) * 8,
				 0x1000, NVOBJ_FLAG_ZERO_ALLOC,
				 &priv->pramin_pt);
	if (ret)
		return ret;

	nv_wo32(chan->vm_pd, 0x0000, priv->pramin_pt->vinst | 0x63);
	nv_wo32(chan->vm_pd, 0x0004, 0);

	/* DMA object for PRAMIN BAR */
	ret = nouveau_gpuobj_new(dev, chan, 6*4, 16, 0, &priv->pramin_bar);
	if (ret)
		return ret;
	nv_wo32(priv->pramin_bar, 0x00, 0x7fc00000);
	nv_wo32(priv->pramin_bar, 0x04, dev_priv->ramin_size - 1);
	nv_wo32(priv->pramin_bar, 0x08, 0x00000000);
	nv_wo32(priv->pramin_bar, 0x0c, 0x00000000);
	nv_wo32(priv->pramin_bar, 0x10, 0x00000000);
	nv_wo32(priv->pramin_bar, 0x14, 0x00000000);

	/* map channel into PRAMIN, gpuobj didn't do it for us */
	ret = nv50_instmem_bind(dev, chan->ramin);
	if (ret)
		return ret;

	/* poke regs... */
	nv_wr32(dev, 0x001704, 0x00000000 | (chan->ramin->vinst >> 12));
	nv_wr32(dev, 0x001704, 0x40000000 | (chan->ramin->vinst >> 12));
	nv_wr32(dev, 0x00170c, 0x80000000 | (priv->pramin_bar->cinst >> 4));

	tmp = nv_ri32(dev, 0);
	nv_wi32(dev, 0, ~tmp);
	if (nv_ri32(dev, 0) != ~tmp) {
		NV_ERROR(dev, "PRAMIN readback failed\n");
		return -EIO;
	}
	nv_wi32(dev, 0, tmp);

	dev_priv->ramin_available = true;

	/* Determine VM layout */
	dev_priv->vm_gart_base = roundup(NV50_VM_BLOCK, NV50_VM_BLOCK);
	dev_priv->vm_gart_size = NV50_VM_BLOCK;

	dev_priv->vm_vram_base = dev_priv->vm_gart_base + dev_priv->vm_gart_size;
	dev_priv->vm_vram_size = dev_priv->vram_size;
	if (dev_priv->vm_vram_size > NV50_VM_MAX_VRAM)
		dev_priv->vm_vram_size = NV50_VM_MAX_VRAM;
	dev_priv->vm_vram_size = roundup(dev_priv->vm_vram_size, NV50_VM_BLOCK);
	dev_priv->vm_vram_pt_nr = dev_priv->vm_vram_size / NV50_VM_BLOCK;

	dev_priv->vm_end = dev_priv->vm_vram_base + dev_priv->vm_vram_size;

	NV_DEBUG(dev, "NV50VM: GART 0x%016llx-0x%016llx\n",
		 dev_priv->vm_gart_base,
		 dev_priv->vm_gart_base + dev_priv->vm_gart_size - 1);
	NV_DEBUG(dev, "NV50VM: VRAM 0x%016llx-0x%016llx\n",
		 dev_priv->vm_vram_base,
		 dev_priv->vm_vram_base + dev_priv->vm_vram_size - 1);

	/* VRAM page table(s), mapped into VM at +1GiB  */
	for (i = 0; i < dev_priv->vm_vram_pt_nr; i++) {
		ret = nouveau_gpuobj_new(dev, NULL, NV50_VM_BLOCK / 0x10000 * 8,
					 0, NVOBJ_FLAG_ZERO_ALLOC,
					 &chan->vm_vram_pt[i]);
		if (ret) {
			NV_ERROR(dev, "Error creating VRAM PGT: %d\n", ret);
			dev_priv->vm_vram_pt_nr = i;
			return ret;
		}
		dev_priv->vm_vram_pt[i] = chan->vm_vram_pt[i];

		nv_wo32(chan->vm_pd, 0x10 + (i*8),
			chan->vm_vram_pt[i]->vinst | 0x61);
		nv_wo32(chan->vm_pd, 0x14 + (i*8), 0);
	}

	/* DMA object for FB BAR */
	ret = nouveau_gpuobj_new(dev, chan, 6*4, 16, 0, &priv->fb_bar);
	if (ret)
		return ret;
	nv_wo32(priv->fb_bar, 0x00, 0x7fc00000);
	nv_wo32(priv->fb_bar, 0x04, 0x40000000 +
				    pci_resource_len(dev->pdev, 1) - 1);
	nv_wo32(priv->fb_bar, 0x08, 0x40000000);
	nv_wo32(priv->fb_bar, 0x0c, 0x00000000);
	nv_wo32(priv->fb_bar, 0x10, 0x00000000);
	nv_wo32(priv->fb_bar, 0x14, 0x00000000);

	dev_priv->engine.instmem.flush(dev);

	nv_wr32(dev, 0x001708, 0x80000000 | (priv->fb_bar->cinst >> 4));
	for (i = 0; i < 8; i++)
		nv_wr32(dev, 0x1900 + (i*4), 0);

	return 0;
}

void
nv50_instmem_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;
	struct nouveau_channel *chan = dev_priv->fifos[0];
	int i;

	NV_DEBUG(dev, "\n");

	if (!priv)
		return;

	dev_priv->ramin_available = false;

	/* Restore state from before init */
	for (i = 0x1700; i <= 0x1710; i += 4)
		nv_wr32(dev, i, priv->save1700[(i - 0x1700) / 4]);

	nouveau_gpuobj_ref(NULL, &priv->fb_bar);
	nouveau_gpuobj_ref(NULL, &priv->pramin_bar);
	nouveau_gpuobj_ref(NULL, &priv->pramin_pt);

	/* Destroy dummy channel */
	if (chan) {
		for (i = 0; i < dev_priv->vm_vram_pt_nr; i++)
			nouveau_gpuobj_ref(NULL, &chan->vm_vram_pt[i]);
		dev_priv->vm_vram_pt_nr = 0;

		nv50_channel_del(&dev_priv->fifos[0]);
		dev_priv->fifos[127] = NULL;
	}

	dev_priv->engine.instmem.priv = NULL;
	kfree(priv);
}

int
nv50_instmem_suspend(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->fifos[0];
	struct nouveau_gpuobj *ramin = chan->ramin;
	int i;

	ramin->im_backing_suspend = vmalloc(ramin->size);
	if (!ramin->im_backing_suspend)
		return -ENOMEM;

	for (i = 0; i < ramin->size; i += 4)
		ramin->im_backing_suspend[i/4] = nv_ri32(dev, i);
	return 0;
}

void
nv50_instmem_resume(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;
	struct nouveau_channel *chan = dev_priv->fifos[0];
	struct nouveau_gpuobj *ramin = chan->ramin;
	int i;

	dev_priv->ramin_available = false;
	dev_priv->ramin_base = ~0;
	for (i = 0; i < ramin->size; i += 4)
		nv_wo32(ramin, i, ramin->im_backing_suspend[i/4]);
	dev_priv->ramin_available = true;
	vfree(ramin->im_backing_suspend);
	ramin->im_backing_suspend = NULL;

	/* Poke the relevant regs, and pray it works :) */
	nv_wr32(dev, NV50_PUNK_BAR_CFG_BASE, (chan->ramin->vinst >> 12));
	nv_wr32(dev, NV50_PUNK_UNK1710, 0);
	nv_wr32(dev, NV50_PUNK_BAR_CFG_BASE, (chan->ramin->vinst >> 12) |
					 NV50_PUNK_BAR_CFG_BASE_VALID);
	nv_wr32(dev, NV50_PUNK_BAR1_CTXDMA, (priv->fb_bar->cinst >> 4) |
					NV50_PUNK_BAR1_CTXDMA_VALID);
	nv_wr32(dev, NV50_PUNK_BAR3_CTXDMA, (priv->pramin_bar->cinst >> 4) |
					NV50_PUNK_BAR3_CTXDMA_VALID);

	for (i = 0; i < 8; i++)
		nv_wr32(dev, 0x1900 + (i*4), 0);
}

int
nv50_instmem_populate(struct drm_device *dev, struct nouveau_gpuobj *gpuobj,
		      uint32_t *sz)
{
	int ret;

	if (gpuobj->im_backing)
		return -EINVAL;

	*sz = ALIGN(*sz, 4096);
	if (*sz == 0)
		return -EINVAL;

	ret = nouveau_bo_new(dev, NULL, *sz, 0, TTM_PL_FLAG_VRAM, 0, 0x0000,
			     true, false, &gpuobj->im_backing);
	if (ret) {
		NV_ERROR(dev, "error getting PRAMIN backing pages: %d\n", ret);
		return ret;
	}

	ret = nouveau_bo_pin(gpuobj->im_backing, TTM_PL_FLAG_VRAM);
	if (ret) {
		NV_ERROR(dev, "error pinning PRAMIN backing VRAM: %d\n", ret);
		nouveau_bo_ref(NULL, &gpuobj->im_backing);
		return ret;
	}

	gpuobj->vinst = gpuobj->im_backing->bo.mem.start << PAGE_SHIFT;
	return 0;
}

void
nv50_instmem_clear(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (gpuobj && gpuobj->im_backing) {
		if (gpuobj->im_bound)
			dev_priv->engine.instmem.unbind(dev, gpuobj);
		nouveau_bo_unpin(gpuobj->im_backing);
		nouveau_bo_ref(NULL, &gpuobj->im_backing);
		gpuobj->im_backing = NULL;
	}
}

int
nv50_instmem_bind(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;
	struct nouveau_gpuobj *pramin_pt = priv->pramin_pt;
	uint32_t pte, pte_end;
	uint64_t vram;

	if (!gpuobj->im_backing || !gpuobj->im_pramin || gpuobj->im_bound)
		return -EINVAL;

	NV_DEBUG(dev, "st=0x%lx sz=0x%lx\n",
		 gpuobj->im_pramin->start, gpuobj->im_pramin->size);

	pte     = (gpuobj->im_pramin->start >> 12) << 1;
	pte_end = ((gpuobj->im_pramin->size >> 12) << 1) + pte;
	vram    = gpuobj->vinst;

	NV_DEBUG(dev, "pramin=0x%lx, pte=%d, pte_end=%d\n",
		 gpuobj->im_pramin->start, pte, pte_end);
	NV_DEBUG(dev, "first vram page: 0x%010llx\n", gpuobj->vinst);

	vram |= 1;
	if (dev_priv->vram_sys_base) {
		vram += dev_priv->vram_sys_base;
		vram |= 0x30;
	}

	while (pte < pte_end) {
		nv_wo32(pramin_pt, (pte * 4) + 0, lower_32_bits(vram));
		nv_wo32(pramin_pt, (pte * 4) + 4, upper_32_bits(vram));
		vram += 0x1000;
		pte += 2;
	}
	dev_priv->engine.instmem.flush(dev);

	nv50_vm_flush(dev, 6);

	gpuobj->im_bound = 1;
	return 0;
}

int
nv50_instmem_unbind(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;
	uint32_t pte, pte_end;

	if (gpuobj->im_bound == 0)
		return -EINVAL;

	/* can happen during late takedown */
	if (unlikely(!dev_priv->ramin_available))
		return 0;

	pte     = (gpuobj->im_pramin->start >> 12) << 1;
	pte_end = ((gpuobj->im_pramin->size >> 12) << 1) + pte;

	while (pte < pte_end) {
		nv_wo32(priv->pramin_pt, (pte * 4) + 0, 0x00000000);
		nv_wo32(priv->pramin_pt, (pte * 4) + 4, 0x00000000);
		pte += 2;
	}
	dev_priv->engine.instmem.flush(dev);

	gpuobj->im_bound = 0;
	return 0;
}

void
nv50_instmem_flush(struct drm_device *dev)
{
	nv_wr32(dev, 0x00330c, 0x00000001);
	if (!nv_wait(dev, 0x00330c, 0x00000002, 0x00000000))
		NV_ERROR(dev, "PRAMIN flush timeout\n");
}

void
nv84_instmem_flush(struct drm_device *dev)
{
	nv_wr32(dev, 0x070000, 0x00000001);
	if (!nv_wait(dev, 0x070000, 0x00000002, 0x00000000))
		NV_ERROR(dev, "PRAMIN flush timeout\n");
}

void
nv50_vm_flush(struct drm_device *dev, int engine)
{
	nv_wr32(dev, 0x100c80, (engine << 16) | 1);
	if (!nv_wait(dev, 0x100c80, 0x00000001, 0x00000000))
		NV_ERROR(dev, "vm flush timeout: engine %d\n", engine);
}

