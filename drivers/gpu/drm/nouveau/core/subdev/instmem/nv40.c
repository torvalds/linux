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

#include "nv04.h"

static inline int
nv44_graph_class(struct nv04_instmem_priv *priv)
{
	if ((nv_device(priv)->chipset & 0xf0) == 0x60)
		return 1;
	return !(0x0baf & (1 << (nv_device(priv)->chipset & 0x0f)));
}

static int
nv40_instmem_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct pci_dev *pdev = device->pdev;
	struct nv04_instmem_priv *priv;
	int ret, bar, vs;

	ret = nouveau_instmem_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	/* map bar */
	if (pci_resource_len(pdev, 2))
		bar = 2;
	else
		bar = 3;

	priv->iomem = ioremap(pci_resource_start(pdev, bar),
			      pci_resource_len(pdev, bar));
	if (!priv->iomem) {
		nv_error(priv, "unable to map PRAMIN BAR\n");
		return -EFAULT;
	}

	/* PRAMIN aperture maps over the end of vram, reserve enough space
	 * to fit graphics contexts for every channel, the magics come
	 * from engine/graph/nv40.c
	 */
	vs = hweight8((nv_rd32(priv, 0x001540) & 0x0000ff00) >> 8);
	if      (device->chipset == 0x40) priv->base.reserved = 0x6aa0 * vs;
	else if (device->chipset  < 0x43) priv->base.reserved = 0x4f00 * vs;
	else if (nv44_graph_class(priv))  priv->base.reserved = 0x4980 * vs;
	else				  priv->base.reserved = 0x4a40 * vs;
	priv->base.reserved += 16 * 1024;
	priv->base.reserved *= 32;		/* per-channel */
	priv->base.reserved += 512 * 1024;	/* pci(e)gart table */
	priv->base.reserved += 512 * 1024;	/* object storage */

	priv->base.reserved = round_up(priv->base.reserved, 4096);
	priv->base.alloc    = nv04_instmem_alloc;

	ret = nouveau_mm_init(&priv->heap, 0, priv->base.reserved, 1);
	if (ret)
		return ret;

	/* 0x00000-0x10000: reserve for probable vbios image */
	ret = nouveau_gpuobj_new(parent, NULL, 0x10000, 0, 0, &priv->vbios);
	if (ret)
		return ret;

	/* 0x10000-0x18000: reserve for RAMHT */
	ret = nouveau_ramht_new(parent, NULL, 0x08000, 0, &priv->ramht);
	if (ret)
		return ret;

	/* 0x18000-0x18200: reserve for RAMRO
	 * 0x18200-0x20000: padding
	 */
	ret = nouveau_gpuobj_new(parent, NULL, 0x08000, 0, 0, &priv->ramro);
	if (ret)
		return ret;

	/* 0x20000-0x21000: reserve for RAMFC
	 * 0x21000-0x40000: padding and some unknown crap
	 */
	ret = nouveau_gpuobj_new(parent, NULL, 0x20000, 0,
				 NVOBJ_FLAG_ZERO_ALLOC, &priv->ramfc);
	if (ret)
		return ret;

	priv->created = true;
	return 0;
}

static u32
nv40_instmem_rd32(struct nouveau_object *object, u32 addr)
{
	struct nv04_instmem_priv *priv = (void *)object;
	return ioread32_native(priv->iomem + addr);
}

static void
nv40_instmem_wr32(struct nouveau_object *object, u32 addr, u32 data)
{
	struct nv04_instmem_priv *priv = (void *)object;
	iowrite32_native(data, priv->iomem + addr);
}

struct nouveau_oclass
nv40_instmem_oclass = {
	.handle = NV_SUBDEV(INSTMEM, 0x40),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv40_instmem_ctor,
		.dtor = nv04_instmem_dtor,
		.init = _nouveau_instmem_init,
		.fini = _nouveau_instmem_fini,
		.rd32 = nv40_instmem_rd32,
		.wr32 = nv40_instmem_wr32,
	},
};
