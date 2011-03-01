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
#include "nouveau_mm.h"

static int types[0x80] = {
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 0, 0,
	0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	1, 0, 2, 0, 1, 0, 2, 0, 1, 1, 2, 2, 1, 1, 0, 0
};

bool
nv50_vram_flags_valid(struct drm_device *dev, u32 tile_flags)
{
	int type = (tile_flags & NOUVEAU_GEM_TILE_LAYOUT_MASK) >> 8;

	if (likely(type < ARRAY_SIZE(types) && types[type]))
		return true;
	return false;
}

void
nv50_vram_del(struct drm_device *dev, struct nouveau_vram **pvram)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct ttm_bo_device *bdev = &dev_priv->ttm.bdev;
	struct ttm_mem_type_manager *man = &bdev->man[TTM_PL_VRAM];
	struct nouveau_mm *mm = man->priv;
	struct nouveau_mm_node *this;
	struct nouveau_vram *vram;

	vram = *pvram;
	*pvram = NULL;
	if (unlikely(vram == NULL))
		return;

	mutex_lock(&mm->mutex);
	while (!list_empty(&vram->regions)) {
		this = list_first_entry(&vram->regions, struct nouveau_mm_node, rl_entry);

		list_del(&this->rl_entry);
		nouveau_mm_put(mm, this);
	}
	mutex_unlock(&mm->mutex);

	kfree(vram);
}

int
nv50_vram_new(struct drm_device *dev, u64 size, u32 align, u32 size_nc,
	      u32 type, struct nouveau_vram **pvram)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct ttm_bo_device *bdev = &dev_priv->ttm.bdev;
	struct ttm_mem_type_manager *man = &bdev->man[TTM_PL_VRAM];
	struct nouveau_mm *mm = man->priv;
	struct nouveau_mm_node *r;
	struct nouveau_vram *vram;
	int ret;

	if (!types[type])
		return -EINVAL;
	size >>= 12;
	align >>= 12;
	size_nc >>= 12;

	vram = kzalloc(sizeof(*vram), GFP_KERNEL);
	if (!vram)
		return -ENOMEM;

	INIT_LIST_HEAD(&vram->regions);
	vram->dev = dev_priv->dev;
	vram->memtype = type;
	vram->size = size;

	mutex_lock(&mm->mutex);
	do {
		ret = nouveau_mm_get(mm, types[type], size, size_nc, align, &r);
		if (ret) {
			mutex_unlock(&mm->mutex);
			nv50_vram_del(dev, &vram);
			return ret;
		}

		list_add_tail(&r->rl_entry, &vram->regions);
		size -= r->length;
	} while (size);
	mutex_unlock(&mm->mutex);

	r = list_first_entry(&vram->regions, struct nouveau_mm_node, rl_entry);
	vram->offset = (u64)r->offset << 12;
	*pvram = vram;
	return 0;
}

static u32
nv50_vram_rblock(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i, parts, colbits, rowbitsa, rowbitsb, banks;
	u64 rowsize, predicted;
	u32 r0, r4, rt, ru, rblock_size;

	r0 = nv_rd32(dev, 0x100200);
	r4 = nv_rd32(dev, 0x100204);
	rt = nv_rd32(dev, 0x100250);
	ru = nv_rd32(dev, 0x001540);
	NV_DEBUG(dev, "memcfg 0x%08x 0x%08x 0x%08x 0x%08x\n", r0, r4, rt, ru);

	for (i = 0, parts = 0; i < 8; i++) {
		if (ru & (0x00010000 << i))
			parts++;
	}

	colbits  =  (r4 & 0x0000f000) >> 12;
	rowbitsa = ((r4 & 0x000f0000) >> 16) + 8;
	rowbitsb = ((r4 & 0x00f00000) >> 20) + 8;
	banks    = ((r4 & 0x01000000) ? 8 : 4);

	rowsize = parts * banks * (1 << colbits) * 8;
	predicted = rowsize << rowbitsa;
	if (r0 & 0x00000004)
		predicted += rowsize << rowbitsb;

	if (predicted != dev_priv->vram_size) {
		NV_WARN(dev, "memory controller reports %dMiB VRAM\n",
			(u32)(dev_priv->vram_size >> 20));
		NV_WARN(dev, "we calculated %dMiB VRAM\n",
			(u32)(predicted >> 20));
	}

	rblock_size = rowsize;
	if (rt & 1)
		rblock_size *= 3;

	NV_DEBUG(dev, "rblock %d bytes\n", rblock_size);
	return rblock_size;
}

int
nv50_vram_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	dev_priv->vram_size  = nv_rd32(dev, 0x10020c);
	dev_priv->vram_size |= (dev_priv->vram_size & 0xff) << 32;
	dev_priv->vram_size &= 0xffffffff00ULL;

	switch (dev_priv->chipset) {
	case 0xaa:
	case 0xac:
	case 0xaf:
		dev_priv->vram_sys_base = (u64)nv_rd32(dev, 0x100e10) << 12;
		dev_priv->vram_rblock_size = 4096;
		break;
	default:
		dev_priv->vram_rblock_size = nv50_vram_rblock(dev);
		break;
	}

	return 0;
}
