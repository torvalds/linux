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
#include "priv.h"

#include <core/device.h>
#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>

struct gf100_bar_priv_vm {
	struct nvkm_gpuobj *mem;
	struct nvkm_gpuobj *pgd;
	struct nvkm_vm *vm;
};

struct gf100_bar_priv {
	struct nvkm_bar base;
	spinlock_t lock;
	struct gf100_bar_priv_vm bar[2];
};

static int
gf100_bar_kmap(struct nvkm_bar *bar, struct nvkm_mem *mem, u32 flags,
	       struct nvkm_vma *vma)
{
	struct gf100_bar_priv *priv = (void *)bar;
	int ret;

	ret = nvkm_vm_get(priv->bar[0].vm, mem->size << 12, 12, flags, vma);
	if (ret)
		return ret;

	nvkm_vm_map(vma, mem);
	return 0;
}

static int
gf100_bar_umap(struct nvkm_bar *bar, struct nvkm_mem *mem, u32 flags,
	       struct nvkm_vma *vma)
{
	struct gf100_bar_priv *priv = (void *)bar;
	int ret;

	ret = nvkm_vm_get(priv->bar[1].vm, mem->size << 12,
			  mem->page_shift, flags, vma);
	if (ret)
		return ret;

	nvkm_vm_map(vma, mem);
	return 0;
}

static void
gf100_bar_unmap(struct nvkm_bar *bar, struct nvkm_vma *vma)
{
	nvkm_vm_unmap(vma);
	nvkm_vm_put(vma);
}

static int
gf100_bar_ctor_vm(struct gf100_bar_priv *priv, struct gf100_bar_priv_vm *bar_vm,
		  int bar_nr)
{
	struct nvkm_device *device = nv_device(&priv->base);
	struct nvkm_vm *vm;
	resource_size_t bar_len;
	int ret;

	ret = nvkm_gpuobj_new(nv_object(priv), NULL, 0x1000, 0, 0,
			      &bar_vm->mem);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(priv), NULL, 0x8000, 0, 0,
			      &bar_vm->pgd);
	if (ret)
		return ret;

	bar_len = nv_device_resource_len(device, bar_nr);

	ret = nvkm_vm_new(device, 0, bar_len, 0, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVDEV_SUBDEV_BAR]);

	/*
	 * Bootstrap page table lookup.
	 */
	if (bar_nr == 3) {
		ret = nvkm_gpuobj_new(nv_object(priv), NULL,
				      (bar_len >> 12) * 8, 0x1000,
				      NVOBJ_FLAG_ZERO_ALLOC,
				      &vm->pgt[0].obj[0]);
		vm->pgt[0].refcount[0] = 1;
		if (ret)
			return ret;
	}

	ret = nvkm_vm_ref(vm, &bar_vm->vm, bar_vm->pgd);
	nvkm_vm_ref(NULL, &vm, NULL);
	if (ret)
		return ret;

	nv_wo32(bar_vm->mem, 0x0200, lower_32_bits(bar_vm->pgd->addr));
	nv_wo32(bar_vm->mem, 0x0204, upper_32_bits(bar_vm->pgd->addr));
	nv_wo32(bar_vm->mem, 0x0208, lower_32_bits(bar_len - 1));
	nv_wo32(bar_vm->mem, 0x020c, upper_32_bits(bar_len - 1));
	return 0;
}

int
gf100_bar_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_device *device = nv_device(parent);
	struct gf100_bar_priv *priv;
	bool has_bar3 = nv_device_resource_len(device, 3) != 0;
	int ret;

	ret = nvkm_bar_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	/* BAR3 */
	if (has_bar3) {
		ret = gf100_bar_ctor_vm(priv, &priv->bar[0], 3);
		if (ret)
			return ret;
	}

	/* BAR1 */
	ret = gf100_bar_ctor_vm(priv, &priv->bar[1], 1);
	if (ret)
		return ret;

	if (has_bar3) {
		priv->base.alloc = nvkm_bar_alloc;
		priv->base.kmap = gf100_bar_kmap;
	}
	priv->base.umap = gf100_bar_umap;
	priv->base.unmap = gf100_bar_unmap;
	priv->base.flush = g84_bar_flush;
	spin_lock_init(&priv->lock);
	return 0;
}

void
gf100_bar_dtor(struct nvkm_object *object)
{
	struct gf100_bar_priv *priv = (void *)object;

	nvkm_vm_ref(NULL, &priv->bar[1].vm, priv->bar[1].pgd);
	nvkm_gpuobj_ref(NULL, &priv->bar[1].pgd);
	nvkm_gpuobj_ref(NULL, &priv->bar[1].mem);

	if (priv->bar[0].vm) {
		nvkm_gpuobj_ref(NULL, &priv->bar[0].vm->pgt[0].obj[0]);
		nvkm_vm_ref(NULL, &priv->bar[0].vm, priv->bar[0].pgd);
	}
	nvkm_gpuobj_ref(NULL, &priv->bar[0].pgd);
	nvkm_gpuobj_ref(NULL, &priv->bar[0].mem);

	nvkm_bar_destroy(&priv->base);
}

int
gf100_bar_init(struct nvkm_object *object)
{
	struct gf100_bar_priv *priv = (void *)object;
	int ret;

	ret = nvkm_bar_init(&priv->base);
	if (ret)
		return ret;

	nv_mask(priv, 0x000200, 0x00000100, 0x00000000);
	nv_mask(priv, 0x000200, 0x00000100, 0x00000100);

	nv_wr32(priv, 0x001704, 0x80000000 | priv->bar[1].mem->addr >> 12);
	if (priv->bar[0].mem)
		nv_wr32(priv, 0x001714,
			0xc0000000 | priv->bar[0].mem->addr >> 12);
	return 0;
}

struct nvkm_oclass
gf100_bar_oclass = {
	.handle = NV_SUBDEV(BAR, 0xc0),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_bar_ctor,
		.dtor = gf100_bar_dtor,
		.init = gf100_bar_init,
		.fini = _nvkm_bar_fini,
	},
};
