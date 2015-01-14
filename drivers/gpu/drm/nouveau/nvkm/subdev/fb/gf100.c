/*
 * Copyright 2012 Red Hat Inc.
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
#include "gf100.h"

#include <core/device.h>

extern const u8 gf100_pte_storage_type_map[256];

bool
gf100_fb_memtype_valid(struct nvkm_fb *pfb, u32 tile_flags)
{
	u8 memtype = (tile_flags & 0x0000ff00) >> 8;
	return likely((gf100_pte_storage_type_map[memtype] != 0xff));
}

static void
gf100_fb_intr(struct nvkm_subdev *subdev)
{
	struct gf100_fb_priv *priv = (void *)subdev;
	u32 intr = nv_rd32(priv, 0x000100);
	if (intr & 0x08000000) {
		nv_debug(priv, "PFFB intr\n");
		intr &= ~0x08000000;
	}
	if (intr & 0x00002000) {
		nv_debug(priv, "PBFB intr\n");
		intr &= ~0x00002000;
	}
}

int
gf100_fb_init(struct nvkm_object *object)
{
	struct gf100_fb_priv *priv = (void *)object;
	int ret;

	ret = nvkm_fb_init(&priv->base);
	if (ret)
		return ret;

	if (priv->r100c10_page)
		nv_wr32(priv, 0x100c10, priv->r100c10 >> 8);

	nv_mask(priv, 0x100c80, 0x00000001, 0x00000000); /* 128KiB lpg */
	return 0;
}

void
gf100_fb_dtor(struct nvkm_object *object)
{
	struct nvkm_device *device = nv_device(object);
	struct gf100_fb_priv *priv = (void *)object;

	if (priv->r100c10_page) {
		dma_unmap_page(nv_device_base(device), priv->r100c10, PAGE_SIZE,
			       DMA_BIDIRECTIONAL);
		__free_page(priv->r100c10_page);
	}

	nvkm_fb_destroy(&priv->base);
}

int
gf100_fb_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct nvkm_device *device = nv_device(parent);
	struct gf100_fb_priv *priv;
	int ret;

	ret = nvkm_fb_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->r100c10_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (priv->r100c10_page) {
		priv->r100c10 = dma_map_page(nv_device_base(device),
					     priv->r100c10_page, 0, PAGE_SIZE,
					     DMA_BIDIRECTIONAL);
		if (dma_mapping_error(nv_device_base(device), priv->r100c10))
			return -EFAULT;
	}

	nv_subdev(priv)->intr = gf100_fb_intr;
	return 0;
}

struct nvkm_oclass *
gf100_fb_oclass = &(struct nvkm_fb_impl) {
	.base.handle = NV_SUBDEV(FB, 0xc0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_fb_ctor,
		.dtor = gf100_fb_dtor,
		.init = gf100_fb_init,
		.fini = _nvkm_fb_fini,
	},
	.memtype = gf100_fb_memtype_valid,
	.ram = &gf100_ram_oclass,
}.base;
