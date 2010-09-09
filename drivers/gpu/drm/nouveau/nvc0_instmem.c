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

int
nvc0_instmem_populate(struct drm_device *dev, struct nouveau_gpuobj *gpuobj,
		      uint32_t *size)
{
	int ret;

	*size = ALIGN(*size, 4096);
	if (*size == 0)
		return -EINVAL;

	ret = nouveau_bo_new(dev, NULL, *size, 0, TTM_PL_FLAG_VRAM, 0, 0x0000,
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

	gpuobj->im_backing_start = gpuobj->im_backing->bo.mem.mm_node->start;
	gpuobj->im_backing_start <<= PAGE_SHIFT;
	return 0;
}

void
nvc0_instmem_clear(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
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
nvc0_instmem_bind(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t pte, pte_end;
	uint64_t vram;

	if (!gpuobj->im_backing || !gpuobj->im_pramin || gpuobj->im_bound)
		return -EINVAL;

	NV_DEBUG(dev, "st=0x%lx sz=0x%lx\n",
		 gpuobj->im_pramin->start, gpuobj->im_pramin->size);

	pte     = gpuobj->im_pramin->start >> 12;
	pte_end = (gpuobj->im_pramin->size >> 12) + pte;
	vram    = gpuobj->im_backing_start;

	NV_DEBUG(dev, "pramin=0x%lx, pte=%d, pte_end=%d\n",
		 gpuobj->im_pramin->start, pte, pte_end);
	NV_DEBUG(dev, "first vram page: 0x%08x\n", gpuobj->im_backing_start);

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

	gpuobj->im_bound = 1;
	return 0;
}

int
nvc0_instmem_unbind(struct drm_device *dev, struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t pte, pte_end;

	if (gpuobj->im_bound == 0)
		return -EINVAL;

	pte     = gpuobj->im_pramin->start >> 12;
	pte_end = (gpuobj->im_pramin->size >> 12) + pte;
	while (pte < pte_end) {
		nv_wr32(dev, 0x702000 + (pte * 8), 0);
		nv_wr32(dev, 0x702004 + (pte * 8), 0);
		pte++;
	}
	dev_priv->engine.instmem.flush(dev);

	gpuobj->im_bound = 0;
	return 0;
}

void
nvc0_instmem_flush(struct drm_device *dev)
{
	nv_wr32(dev, 0x070000, 1);
	if (!nv_wait(0x070000, 0x00000002, 0x00000000))
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

	/*XXX: incorrect, but needed to make hash func "work" */
	dev_priv->ramht_offset = 0x10000;
	dev_priv->ramht_bits   = 9;
	dev_priv->ramht_size   = (1 << dev_priv->ramht_bits) * 8;
	return 0;
}

void
nvc0_instmem_takedown(struct drm_device *dev)
{
}

