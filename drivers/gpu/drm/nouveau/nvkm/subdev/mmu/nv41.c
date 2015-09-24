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

#include <core/gpuobj.h>
#include <core/option.h>
#include <subdev/timer.h>

#define NV41_GART_SIZE (512 * 1024 * 1024)
#define NV41_GART_PAGE (  4 * 1024)

/*******************************************************************************
 * VM map/unmap callbacks
 ******************************************************************************/

static void
nv41_vm_map_sg(struct nvkm_vma *vma, struct nvkm_memory *pgt,
	       struct nvkm_mem *mem, u32 pte, u32 cnt, dma_addr_t *list)
{
	pte = pte * 4;
	nvkm_kmap(pgt);
	while (cnt) {
		u32 page = PAGE_SIZE / NV41_GART_PAGE;
		u64 phys = (u64)*list++;
		while (cnt && page--) {
			nvkm_wo32(pgt, pte, (phys >> 7) | 1);
			phys += NV41_GART_PAGE;
			pte += 4;
			cnt -= 1;
		}
	}
	nvkm_done(pgt);
}

static void
nv41_vm_unmap(struct nvkm_vma *vma, struct nvkm_memory *pgt, u32 pte, u32 cnt)
{
	pte = pte * 4;
	nvkm_kmap(pgt);
	while (cnt--) {
		nvkm_wo32(pgt, pte, 0x00000000);
		pte += 4;
	}
	nvkm_done(pgt);
}

static void
nv41_vm_flush(struct nvkm_vm *vm)
{
	struct nv04_mmu *mmu = nv04_mmu(vm->mmu);
	struct nvkm_device *device = mmu->base.subdev.device;

	mutex_lock(&mmu->base.subdev.mutex);
	nvkm_wr32(device, 0x100810, 0x00000022);
	nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x100810) & 0x00000020)
			break;
	);
	nvkm_wr32(device, 0x100810, 0x00000000);
	mutex_unlock(&mmu->base.subdev.mutex);
}

/*******************************************************************************
 * MMU subdev
 ******************************************************************************/

static int
nv41_mmu_oneinit(struct nvkm_mmu *base)
{
	struct nv04_mmu *mmu = nv04_mmu(base);
	struct nvkm_device *device = mmu->base.subdev.device;
	int ret;

	ret = nvkm_vm_create(&mmu->base, 0, NV41_GART_SIZE, 0, 4096, NULL,
			     &mmu->vm);
	if (ret)
		return ret;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
			      (NV41_GART_SIZE / NV41_GART_PAGE) * 4, 16, true,
			      &mmu->vm->pgt[0].mem[0]);
	mmu->vm->pgt[0].refcount[0] = 1;
	return ret;
}

static void
nv41_mmu_init(struct nvkm_mmu *base)
{
	struct nv04_mmu *mmu = nv04_mmu(base);
	struct nvkm_device *device = mmu->base.subdev.device;
	struct nvkm_memory *dma = mmu->vm->pgt[0].mem[0];
	nvkm_wr32(device, 0x100800, 0x00000002 | nvkm_memory_addr(dma));
	nvkm_mask(device, 0x10008c, 0x00000100, 0x00000100);
	nvkm_wr32(device, 0x100820, 0x00000000);
}

static const struct nvkm_mmu_func
nv41_mmu = {
	.dtor = nv04_mmu_dtor,
	.oneinit = nv41_mmu_oneinit,
	.init = nv41_mmu_init,
	.limit = NV41_GART_SIZE,
	.dma_bits = 39,
	.pgt_bits = 32 - 12,
	.spg_shift = 12,
	.lpg_shift = 12,
	.map_sg = nv41_vm_map_sg,
	.unmap = nv41_vm_unmap,
	.flush = nv41_vm_flush,
};

int
nv41_mmu_new(struct nvkm_device *device, int index, struct nvkm_mmu **pmmu)
{
	if (device->type == NVKM_DEVICE_AGP ||
	    !nvkm_boolopt(device->cfgopt, "NvPCIE", true))
		return nv04_mmu_new(device, index, pmmu);

	return nv04_mmu_new_(&nv41_mmu, device, index, pmmu);
}
