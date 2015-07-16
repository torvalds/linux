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

#include <core/device.h>
#include <core/gpuobj.h>
#include <core/option.h>
#include <subdev/timer.h>

#define NV41_GART_SIZE (512 * 1024 * 1024)
#define NV41_GART_PAGE (  4 * 1024)

/*******************************************************************************
 * VM map/unmap callbacks
 ******************************************************************************/

static void
nv41_vm_map_sg(struct nvkm_vma *vma, struct nvkm_gpuobj *pgt,
	       struct nvkm_mem *mem, u32 pte, u32 cnt, dma_addr_t *list)
{
	pte = pte * 4;
	while (cnt) {
		u32 page = PAGE_SIZE / NV41_GART_PAGE;
		u64 phys = (u64)*list++;
		while (cnt && page--) {
			nv_wo32(pgt, pte, (phys >> 7) | 1);
			phys += NV41_GART_PAGE;
			pte += 4;
			cnt -= 1;
		}
	}
}

static void
nv41_vm_unmap(struct nvkm_gpuobj *pgt, u32 pte, u32 cnt)
{
	pte = pte * 4;
	while (cnt--) {
		nv_wo32(pgt, pte, 0x00000000);
		pte += 4;
	}
}

static void
nv41_vm_flush(struct nvkm_vm *vm)
{
	struct nv04_mmu_priv *priv = (void *)vm->mmu;

	mutex_lock(&nv_subdev(priv)->mutex);
	nv_wr32(priv, 0x100810, 0x00000022);
	if (!nv_wait(priv, 0x100810, 0x00000020, 0x00000020)) {
		nv_warn(priv, "flush timeout, 0x%08x\n",
			nv_rd32(priv, 0x100810));
	}
	nv_wr32(priv, 0x100810, 0x00000000);
	mutex_unlock(&nv_subdev(priv)->mutex);
}

/*******************************************************************************
 * MMU subdev
 ******************************************************************************/

static int
nv41_mmu_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct nvkm_device *device = nv_device(parent);
	struct nv04_mmu_priv *priv;
	int ret;

	if (pci_find_capability(device->pdev, PCI_CAP_ID_AGP) ||
	    !nvkm_boolopt(device->cfgopt, "NvPCIE", true)) {
		return nvkm_object_ctor(parent, engine, &nv04_mmu_oclass,
					data, size, pobject);
	}

	ret = nvkm_mmu_create(parent, engine, oclass, "PCIEGART",
			      "pciegart", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.create = nv04_vm_create;
	priv->base.limit = NV41_GART_SIZE;
	priv->base.dma_bits = 39;
	priv->base.pgt_bits = 32 - 12;
	priv->base.spg_shift = 12;
	priv->base.lpg_shift = 12;
	priv->base.map_sg = nv41_vm_map_sg;
	priv->base.unmap = nv41_vm_unmap;
	priv->base.flush = nv41_vm_flush;

	ret = nvkm_vm_create(&priv->base, 0, NV41_GART_SIZE, 0, 4096,
			     &priv->vm);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(priv), NULL,
			      (NV41_GART_SIZE / NV41_GART_PAGE) * 4, 16,
			      NVOBJ_FLAG_ZERO_ALLOC,
			      &priv->vm->pgt[0].obj[0]);
	priv->vm->pgt[0].refcount[0] = 1;
	if (ret)
		return ret;

	return 0;
}

static int
nv41_mmu_init(struct nvkm_object *object)
{
	struct nv04_mmu_priv *priv = (void *)object;
	struct nvkm_gpuobj *dma = priv->vm->pgt[0].obj[0];
	int ret;

	ret = nvkm_mmu_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x100800, dma->addr | 0x00000002);
	nv_mask(priv, 0x10008c, 0x00000100, 0x00000100);
	nv_wr32(priv, 0x100820, 0x00000000);
	return 0;
}

struct nvkm_oclass
nv41_mmu_oclass = {
	.handle = NV_SUBDEV(MMU, 0x41),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv41_mmu_ctor,
		.dtor = nv04_mmu_dtor,
		.init = nv41_mmu_init,
		.fini = _nvkm_mmu_fini,
	},
};
