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

bool
nvc0_vram_flags_valid(struct drm_device *dev, u32 tile_flags)
{
	switch (tile_flags & NOUVEAU_GEM_TILE_LAYOUT_MASK) {
	case 0x0000:
	case 0xfe00:
	case 0xdb00:
	case 0x1100:
		return true;
	default:
		break;
	}

	return false;
}

int
nvc0_vram_new(struct drm_device *dev, u64 size, u32 align, u32 ncmin,
	      u32 type, struct nouveau_vram **pvram)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct ttm_bo_device *bdev = &dev_priv->ttm.bdev;
	struct ttm_mem_type_manager *man = &bdev->man[TTM_PL_VRAM];
	struct nouveau_mm *mm = man->priv;
	struct nouveau_mm_node *r;
	struct nouveau_vram *vram;
	int ret;

	size  >>= 12;
	align >>= 12;
	ncmin >>= 12;

	vram = kzalloc(sizeof(*vram), GFP_KERNEL);
	if (!vram)
		return -ENOMEM;

	INIT_LIST_HEAD(&vram->regions);
	vram->dev = dev_priv->dev;
	vram->memtype = type;
	vram->size = size;

	mutex_lock(&mm->mutex);
	do {
		ret = nouveau_mm_get(mm, 1, size, ncmin, align, &r);
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

int
nvc0_vram_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	dev_priv->vram_size  = nv_rd32(dev, 0x10f20c) << 20;
	dev_priv->vram_size *= nv_rd32(dev, 0x121c74);
	dev_priv->vram_rblock_size = 4096;
	return 0;
}
