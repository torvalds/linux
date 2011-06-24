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
nv50_vram_del(struct drm_device *dev, struct nouveau_mem **pmem)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_mm *mm = &dev_priv->engine.vram.mm;
	struct nouveau_mm_node *this;
	struct nouveau_mem *mem;

	mem = *pmem;
	*pmem = NULL;
	if (unlikely(mem == NULL))
		return;

	mutex_lock(&mm->mutex);
	while (!list_empty(&mem->regions)) {
		this = list_first_entry(&mem->regions, struct nouveau_mm_node, rl_entry);

		list_del(&this->rl_entry);
		nouveau_mm_put(mm, this);
	}

	if (mem->tag) {
		drm_mm_put_block(mem->tag);
		mem->tag = NULL;
	}
	mutex_unlock(&mm->mutex);

	kfree(mem);
}

int
nv50_vram_new(struct drm_device *dev, u64 size, u32 align, u32 size_nc,
	      u32 memtype, struct nouveau_mem **pmem)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_mm *mm = &dev_priv->engine.vram.mm;
	struct nouveau_mm_node *r;
	struct nouveau_mem *mem;
	int comp = (memtype & 0x300) >> 8;
	int type = (memtype & 0x07f);
	int ret;

	if (!types[type])
		return -EINVAL;
	size >>= 12;
	align >>= 12;
	size_nc >>= 12;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	mutex_lock(&mm->mutex);
	if (comp) {
		if (align == 16) {
			struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
			int n = (size >> 4) * comp;

			mem->tag = drm_mm_search_free(&pfb->tag_heap, n, 0, 0);
			if (mem->tag)
				mem->tag = drm_mm_get_block(mem->tag, n, 0);
		}

		if (unlikely(!mem->tag))
			comp = 0;
	}

	INIT_LIST_HEAD(&mem->regions);
	mem->dev = dev_priv->dev;
	mem->memtype = (comp << 7) | type;
	mem->size = size;

	do {
		ret = nouveau_mm_get(mm, types[type], size, size_nc, align, &r);
		if (ret) {
			mutex_unlock(&mm->mutex);
			nv50_vram_del(dev, &mem);
			return ret;
		}

		list_add_tail(&r->rl_entry, &mem->regions);
		size -= r->length;
	} while (size);
	mutex_unlock(&mm->mutex);

	r = list_first_entry(&mem->regions, struct nouveau_mm_node, rl_entry);
	mem->offset = (u64)r->offset << 12;
	*pmem = mem;
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
	struct nouveau_vram_engine *vram = &dev_priv->engine.vram;
	const u32 rsvd_head = ( 256 * 1024) >> 12; /* vga memory */
	const u32 rsvd_tail = (1024 * 1024) >> 12; /* vbios etc */
	u32 rblock, length;

	dev_priv->vram_size  = nv_rd32(dev, 0x10020c);
	dev_priv->vram_size |= (dev_priv->vram_size & 0xff) << 32;
	dev_priv->vram_size &= 0xffffffff00ULL;

	/* IGPs, no funky reordering happens here, they don't have VRAM */
	if (dev_priv->chipset == 0xaa ||
	    dev_priv->chipset == 0xac ||
	    dev_priv->chipset == 0xaf) {
		dev_priv->vram_sys_base = (u64)nv_rd32(dev, 0x100e10) << 12;
		rblock = 4096 >> 12;
	} else {
		rblock = nv50_vram_rblock(dev) >> 12;
	}

	length = (dev_priv->vram_size >> 12) - rsvd_head - rsvd_tail;

	return nouveau_mm_init(&vram->mm, rsvd_head, length, rblock);
}

void
nv50_vram_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_vram_engine *vram = &dev_priv->engine.vram;

	nouveau_mm_fini(&vram->mm);
}
