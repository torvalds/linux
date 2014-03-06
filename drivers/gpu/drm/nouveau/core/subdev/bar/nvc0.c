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

#include <core/gpuobj.h>

#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/vm.h>

#include "priv.h"

struct nvc0_bar_priv {
	struct nouveau_bar base;
	spinlock_t lock;
	struct {
		struct nouveau_gpuobj *mem;
		struct nouveau_gpuobj *pgd;
		struct nouveau_vm *vm;
	} bar[2];
};

static int
nvc0_bar_kmap(struct nouveau_bar *bar, struct nouveau_mem *mem,
	      u32 flags, struct nouveau_vma *vma)
{
	struct nvc0_bar_priv *priv = (void *)bar;
	int ret;

	ret = nouveau_vm_get(priv->bar[0].vm, mem->size << 12, 12, flags, vma);
	if (ret)
		return ret;

	nouveau_vm_map(vma, mem);
	return 0;
}

static int
nvc0_bar_umap(struct nouveau_bar *bar, struct nouveau_mem *mem,
	      u32 flags, struct nouveau_vma *vma)
{
	struct nvc0_bar_priv *priv = (void *)bar;
	int ret;

	ret = nouveau_vm_get(priv->bar[1].vm, mem->size << 12,
			     mem->page_shift, flags, vma);
	if (ret)
		return ret;

	nouveau_vm_map(vma, mem);
	return 0;
}

static void
nvc0_bar_unmap(struct nouveau_bar *bar, struct nouveau_vma *vma)
{
	nouveau_vm_unmap(vma);
	nouveau_vm_put(vma);
}

static int
nvc0_bar_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 size,
	      struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct pci_dev *pdev = device->pdev;
	struct nvc0_bar_priv *priv;
	struct nouveau_gpuobj *mem;
	struct nouveau_vm *vm;
	int ret;

	ret = nouveau_bar_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	/* BAR3 */
	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x1000, 0, 0,
				&priv->bar[0].mem);
	mem = priv->bar[0].mem;
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x8000, 0, 0,
				&priv->bar[0].pgd);
	if (ret)
		return ret;

	ret = nouveau_vm_new(device, 0, pci_resource_len(pdev, 3), 0, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVDEV_SUBDEV_BAR]);

	ret = nouveau_gpuobj_new(nv_object(priv), NULL,
				 (pci_resource_len(pdev, 3) >> 12) * 8,
				 0x1000, NVOBJ_FLAG_ZERO_ALLOC,
				 &vm->pgt[0].obj[0]);
	vm->pgt[0].refcount[0] = 1;
	if (ret)
		return ret;

	ret = nouveau_vm_ref(vm, &priv->bar[0].vm, priv->bar[0].pgd);
	nouveau_vm_ref(NULL, &vm, NULL);
	if (ret)
		return ret;

	nv_wo32(mem, 0x0200, lower_32_bits(priv->bar[0].pgd->addr));
	nv_wo32(mem, 0x0204, upper_32_bits(priv->bar[0].pgd->addr));
	nv_wo32(mem, 0x0208, lower_32_bits(pci_resource_len(pdev, 3) - 1));
	nv_wo32(mem, 0x020c, upper_32_bits(pci_resource_len(pdev, 3) - 1));

	/* BAR1 */
	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x1000, 0, 0,
				&priv->bar[1].mem);
	mem = priv->bar[1].mem;
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x8000, 0, 0,
				&priv->bar[1].pgd);
	if (ret)
		return ret;

	ret = nouveau_vm_new(device, 0, pci_resource_len(pdev, 1), 0, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVDEV_SUBDEV_BAR]);

	ret = nouveau_vm_ref(vm, &priv->bar[1].vm, priv->bar[1].pgd);
	nouveau_vm_ref(NULL, &vm, NULL);
	if (ret)
		return ret;

	nv_wo32(mem, 0x0200, lower_32_bits(priv->bar[1].pgd->addr));
	nv_wo32(mem, 0x0204, upper_32_bits(priv->bar[1].pgd->addr));
	nv_wo32(mem, 0x0208, lower_32_bits(pci_resource_len(pdev, 1) - 1));
	nv_wo32(mem, 0x020c, upper_32_bits(pci_resource_len(pdev, 1) - 1));

	priv->base.alloc = nouveau_bar_alloc;
	priv->base.kmap = nvc0_bar_kmap;
	priv->base.umap = nvc0_bar_umap;
	priv->base.unmap = nvc0_bar_unmap;
	priv->base.flush = nv84_bar_flush;
	spin_lock_init(&priv->lock);
	return 0;
}

static void
nvc0_bar_dtor(struct nouveau_object *object)
{
	struct nvc0_bar_priv *priv = (void *)object;

	nouveau_vm_ref(NULL, &priv->bar[1].vm, priv->bar[1].pgd);
	nouveau_gpuobj_ref(NULL, &priv->bar[1].pgd);
	nouveau_gpuobj_ref(NULL, &priv->bar[1].mem);

	if (priv->bar[0].vm) {
		nouveau_gpuobj_ref(NULL, &priv->bar[0].vm->pgt[0].obj[0]);
		nouveau_vm_ref(NULL, &priv->bar[0].vm, priv->bar[0].pgd);
	}
	nouveau_gpuobj_ref(NULL, &priv->bar[0].pgd);
	nouveau_gpuobj_ref(NULL, &priv->bar[0].mem);

	nouveau_bar_destroy(&priv->base);
}

static int
nvc0_bar_init(struct nouveau_object *object)
{
	struct nvc0_bar_priv *priv = (void *)object;
	int ret;

	ret = nouveau_bar_init(&priv->base);
	if (ret)
		return ret;

	nv_mask(priv, 0x000200, 0x00000100, 0x00000000);
	nv_mask(priv, 0x000200, 0x00000100, 0x00000100);
	nv_mask(priv, 0x100c80, 0x00000001, 0x00000000);

	nv_wr32(priv, 0x001704, 0x80000000 | priv->bar[1].mem->addr >> 12);
	nv_wr32(priv, 0x001714, 0xc0000000 | priv->bar[0].mem->addr >> 12);
	return 0;
}

struct nouveau_oclass
nvc0_bar_oclass = {
	.handle = NV_SUBDEV(BAR, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_bar_ctor,
		.dtor = nvc0_bar_dtor,
		.init = nvc0_bar_init,
		.fini = _nouveau_bar_fini,
	},
};
