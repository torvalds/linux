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

struct nvc0_gpuobj_node {
	struct nouveau_bo *vram;
	struct drm_mm_node *ramin;
	u32 align;
};

int
nvc0_instmem_get(struct nouveau_gpuobj *gpuobj, u32 size, u32 align)
{
	struct drm_device *dev = gpuobj->dev;
	struct nvc0_gpuobj_node *node = NULL;
	int ret;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	node->align = align;

	ret = nouveau_bo_new(dev, NULL, size, align, TTM_PL_FLAG_VRAM,
			     0, 0x0000, true, false, &node->vram);
	if (ret) {
		NV_ERROR(dev, "error getting PRAMIN backing pages: %d\n", ret);
		return ret;
	}

	ret = nouveau_bo_pin(node->vram, TTM_PL_FLAG_VRAM);
	if (ret) {
		NV_ERROR(dev, "error pinning PRAMIN backing VRAM: %d\n", ret);
		nouveau_bo_ref(NULL, &node->vram);
		return ret;
	}

	gpuobj->vinst = node->vram->bo.mem.start << PAGE_SHIFT;
	gpuobj->size  = node->vram->bo.mem.num_pages << PAGE_SHIFT;
	gpuobj->node  = node;
	return 0;
}

void
nvc0_instmem_put(struct nouveau_gpuobj *gpuobj)
{
	struct nvc0_gpuobj_node *node;

	node = gpuobj->node;
	gpuobj->node = NULL;

	nouveau_bo_unpin(node->vram);
	nouveau_bo_ref(NULL, &node->vram);
	kfree(node);
}

int
nvc0_instmem_map(struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;
	struct nvc0_gpuobj_node *node = gpuobj->node;
	struct drm_device *dev = gpuobj->dev;
	struct drm_mm_node *ramin = NULL;
	u32 pte, pte_end;
	u64 vram;

	do {
		if (drm_mm_pre_get(&dev_priv->ramin_heap))
			return -ENOMEM;

		spin_lock(&dev_priv->ramin_lock);
		ramin = drm_mm_search_free(&dev_priv->ramin_heap, gpuobj->size,
					   node->align, 0);
		if (ramin == NULL) {
			spin_unlock(&dev_priv->ramin_lock);
			return -ENOMEM;
		}

		ramin = drm_mm_get_block_atomic(ramin, gpuobj->size, node->align);
		spin_unlock(&dev_priv->ramin_lock);
	} while (ramin == NULL);

	pte     = (ramin->start >> 12) << 1;
	pte_end = ((ramin->size >> 12) << 1) + pte;
	vram    = gpuobj->vinst;

	NV_DEBUG(dev, "pramin=0x%lx, pte=%d, pte_end=%d\n",
		 ramin->start, pte, pte_end);
	NV_DEBUG(dev, "first vram page: 0x%010llx\n", gpuobj->vinst);

	while (pte < pte_end) {
		nv_wr32(dev, 0x702000 + (pte * 8), (vram >> 8) | 1);
		nv_wr32(dev, 0x702004 + (pte * 8), 0);
		vram += 4096;
		pte++;
	}
	dev_priv->engine.instmem.flush(dev);

	if (1) {
		u32 chan = nv_rd32(dev, 0x1700) << 16;
		nv_wr32(dev, 0x100cb8, (chan + 0x1000) >> 8);
		nv_wr32(dev, 0x100cbc, 0x80000005);
	}

	node->ramin   = ramin;
	gpuobj->pinst = ramin->start;
	return 0;
}

void
nvc0_instmem_unmap(struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;
	struct nvc0_gpuobj_node *node = gpuobj->node;
	u32 pte, pte_end;

	if (!node->ramin || !dev_priv->ramin_available)
		return;

	pte     = (node->ramin->start >> 12) << 1;
	pte_end = ((node->ramin->size >> 12) << 1) + pte;

	while (pte < pte_end) {
		nv_wr32(gpuobj->dev, 0x702000 + (pte * 8), 0);
		nv_wr32(gpuobj->dev, 0x702004 + (pte * 8), 0);
		pte++;
	}
	dev_priv->engine.instmem.flush(gpuobj->dev);

	spin_lock(&dev_priv->ramin_lock);
	drm_mm_put_block(node->ramin);
	node->ramin = NULL;
	spin_unlock(&dev_priv->ramin_lock);
}

void
nvc0_instmem_flush(struct drm_device *dev)
{
	nv_wr32(dev, 0x070000, 1);
	if (!nv_wait(dev, 0x070000, 0x00000002, 0x00000000))
		NV_ERROR(dev, "PRAMIN flush timeout\n");
}

int
nvc0_instmem_suspend(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 *buf;
	int i;

	dev_priv->susres.ramin_copy = vmalloc(65536);
	if (!dev_priv->susres.ramin_copy)
		return -ENOMEM;
	buf = dev_priv->susres.ramin_copy;

	for (i = 0; i < 65536; i += 4)
		buf[i/4] = nv_rd32(dev, NV04_PRAMIN + i);
	return 0;
}

void
nvc0_instmem_resume(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 *buf = dev_priv->susres.ramin_copy;
	u64 chan;
	int i;

	chan = dev_priv->vram_size - dev_priv->ramin_rsvd_vram;
	nv_wr32(dev, 0x001700, chan >> 16);

	for (i = 0; i < 65536; i += 4)
		nv_wr32(dev, NV04_PRAMIN + i, buf[i/4]);
	vfree(dev_priv->susres.ramin_copy);
	dev_priv->susres.ramin_copy = NULL;

	nv_wr32(dev, 0x001714, 0xc0000000 | (chan >> 12));
}

int
nvc0_instmem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u64 chan, pgt3, imem, lim3 = dev_priv->ramin_size - 1;
	int ret, i;

	dev_priv->ramin_rsvd_vram = 1 * 1024 * 1024;
	chan = dev_priv->vram_size - dev_priv->ramin_rsvd_vram;
	imem = 4096 + 4096 + 32768;

	nv_wr32(dev, 0x001700, chan >> 16);

	/* channel setup */
	nv_wr32(dev, 0x700200, lower_32_bits(chan + 0x1000));
	nv_wr32(dev, 0x700204, upper_32_bits(chan + 0x1000));
	nv_wr32(dev, 0x700208, lower_32_bits(lim3));
	nv_wr32(dev, 0x70020c, upper_32_bits(lim3));

	/* point pgd -> pgt */
	nv_wr32(dev, 0x701000, 0);
	nv_wr32(dev, 0x701004, ((chan + 0x2000) >> 8) | 1);

	/* point pgt -> physical vram for channel */
	pgt3 = 0x2000;
	for (i = 0; i < dev_priv->ramin_rsvd_vram; i += 4096, pgt3 += 8) {
		nv_wr32(dev, 0x700000 + pgt3, ((chan + i) >> 8) | 1);
		nv_wr32(dev, 0x700004 + pgt3, 0);
	}

	/* clear rest of pgt */
	for (; i < dev_priv->ramin_size; i += 4096, pgt3 += 8) {
		nv_wr32(dev, 0x700000 + pgt3, 0);
		nv_wr32(dev, 0x700004 + pgt3, 0);
	}

	/* point bar3 at the channel */
	nv_wr32(dev, 0x001714, 0xc0000000 | (chan >> 12));

	/* Global PRAMIN heap */
	ret = drm_mm_init(&dev_priv->ramin_heap, imem,
			  dev_priv->ramin_size - imem);
	if (ret) {
		NV_ERROR(dev, "Failed to init RAMIN heap\n");
		return -ENOMEM;
	}

	return 0;
}

void
nvc0_instmem_takedown(struct drm_device *dev)
{
}

