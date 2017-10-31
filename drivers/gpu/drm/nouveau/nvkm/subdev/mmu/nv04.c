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
#include "vmm.h"

#include <nvif/class.h>

#define NV04_PDMA_SIZE (128 * 1024 * 1024)
#define NV04_PDMA_PAGE (  4 * 1024)

/*******************************************************************************
 * VM map/unmap callbacks
 ******************************************************************************/

static void
nv04_vm_map_sg(struct nvkm_vma *vma, struct nvkm_memory *pgt,
	       struct nvkm_mem *mem, u32 pte, u32 cnt, dma_addr_t *list)
{
	pte = 0x00008 + (pte * 4);
	nvkm_kmap(pgt);
	while (cnt) {
		u32 page = PAGE_SIZE / NV04_PDMA_PAGE;
		u32 phys = (u32)*list++;
		while (cnt && page--) {
			nvkm_wo32(pgt, pte, phys | 3);
			phys += NV04_PDMA_PAGE;
			pte += 4;
			cnt -= 1;
		}
	}
	nvkm_done(pgt);
}

static void
nv04_vm_unmap(struct nvkm_vma *vma, struct nvkm_memory *pgt, u32 pte, u32 cnt)
{
	pte = 0x00008 + (pte * 4);
	nvkm_kmap(pgt);
	while (cnt--) {
		nvkm_wo32(pgt, pte, 0x00000000);
		pte += 4;
	}
	nvkm_done(pgt);
}

static void
nv04_vm_flush(struct nvkm_vm *vm)
{
}

/*******************************************************************************
 * MMU subdev
 ******************************************************************************/

static int
nv04_mmu_oneinit(struct nvkm_mmu *mmu)
{
	mmu->vmm->pgt[0].mem[0] = mmu->vmm->pd->pt[0]->memory;
	mmu->vmm->pgt[0].refcount[0] = 1;
	return 0;
}

void *
nv04_mmu_dtor(struct nvkm_mmu *base)
{
	struct nv04_mmu *mmu = nv04_mmu(base);
	struct nvkm_device *device = mmu->base.subdev.device;
	if (mmu->base.vmm)
		nvkm_memory_unref(&mmu->base.vmm->pgt[0].mem[0]);
	if (mmu->nullp) {
		dma_free_coherent(device->dev, 16 * 1024,
				  mmu->nullp, mmu->null);
	}
	return mmu;
}

int
nv04_mmu_new_(const struct nvkm_mmu_func *func, struct nvkm_device *device,
	      int index, struct nvkm_mmu **pmmu)
{
	struct nv04_mmu *mmu;
	if (!(mmu = kzalloc(sizeof(*mmu), GFP_KERNEL)))
		return -ENOMEM;
	*pmmu = &mmu->base;
	nvkm_mmu_ctor(func, device, index, &mmu->base);
	return 0;
}

const struct nvkm_mmu_func
nv04_mmu = {
	.oneinit = nv04_mmu_oneinit,
	.limit = NV04_PDMA_SIZE,
	.dma_bits = 32,
	.pgt_bits = 32 - 12,
	.spg_shift = 12,
	.lpg_shift = 12,
	.map_sg = nv04_vm_map_sg,
	.unmap = nv04_vm_unmap,
	.flush = nv04_vm_flush,
	.vmm = {{ -1, -1, NVIF_CLASS_VMM_NV04}, nv04_vmm_new, true },
};

int
nv04_mmu_new(struct nvkm_device *device, int index, struct nvkm_mmu **pmmu)
{
	return nvkm_mmu_new_(&nv04_mmu, device, index, pmmu);
}
