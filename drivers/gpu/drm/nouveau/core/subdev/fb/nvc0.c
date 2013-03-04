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

#include "nvc0.h"

extern const u8 nvc0_pte_storage_type_map[256];

bool
nvc0_fb_memtype_valid(struct nouveau_fb *pfb, u32 tile_flags)
{
	u8 memtype = (tile_flags & 0x0000ff00) >> 8;
	return likely((nvc0_pte_storage_type_map[memtype] != 0xff));
}

int
nvc0_fb_init(struct nouveau_object *object)
{
	struct nvc0_fb_priv *priv = (void *)object;
	int ret;

	ret = nouveau_fb_init(&priv->base);
	if (ret)
		return ret;

	if (priv->r100c10_page)
		nv_wr32(priv, 0x100c10, priv->r100c10 >> 8);
	return 0;
}

void
nvc0_fb_dtor(struct nouveau_object *object)
{
	struct nouveau_device *device = nv_device(object);
	struct nvc0_fb_priv *priv = (void *)object;

	if (priv->r100c10_page) {
		pci_unmap_page(device->pdev, priv->r100c10, PAGE_SIZE,
			       PCI_DMA_BIDIRECTIONAL);
		__free_page(priv->r100c10_page);
	}

	nouveau_fb_destroy(&priv->base);
}

int
nvc0_fb_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nvc0_fb_priv *priv;
	int ret;

	ret = nouveau_fb_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->r100c10_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (priv->r100c10_page) {
		priv->r100c10 = pci_map_page(device->pdev, priv->r100c10_page,
					     0, PAGE_SIZE,
					     PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(device->pdev, priv->r100c10))
			return -EFAULT;
	}

	return 0;
}

struct nouveau_oclass *
nvc0_fb_oclass = &(struct nouveau_fb_impl) {
	.base.handle = NV_SUBDEV(FB, 0xc0),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_fb_ctor,
		.dtor = nvc0_fb_dtor,
		.init = nvc0_fb_init,
		.fini = _nouveau_fb_fini,
	},
	.memtype = nvc0_fb_memtype_valid,
	.ram = &nvc0_ram_oclass,
}.base;
