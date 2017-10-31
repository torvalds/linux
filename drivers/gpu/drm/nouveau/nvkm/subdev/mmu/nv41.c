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
#include "vmm.h"

#include <core/option.h>
#include <subdev/timer.h>

#include <nvif/class.h>

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
	struct nvkm_subdev *subdev = &vm->mmu->subdev;
	struct nvkm_device *device = subdev->device;

	mutex_lock(&subdev->mutex);
	nvkm_wr32(device, 0x100810, 0x00000022);
	nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x100810) & 0x00000020)
			break;
	);
	nvkm_wr32(device, 0x100810, 0x00000000);
	mutex_unlock(&subdev->mutex);
}

/*******************************************************************************
 * MMU subdev
 ******************************************************************************/

static int
nv41_mmu_oneinit(struct nvkm_mmu *mmu)
{
	mmu->vmm->pgt[0].mem[0] = mmu->vmm->pd->pt[0]->memory;
	mmu->vmm->pgt[0].refcount[0] = 1;
	return 0;
}

static void
nv41_mmu_init(struct nvkm_mmu *mmu)
{
	struct nvkm_device *device = mmu->subdev.device;
	nvkm_wr32(device, 0x100800, 0x00000002 | mmu->vmm->pd->pt[0]->addr);
	nvkm_mask(device, 0x10008c, 0x00000100, 0x00000100);
	nvkm_wr32(device, 0x100820, 0x00000000);
}

static const struct nvkm_mmu_func
nv41_mmu = {
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
	.vmm = {{ -1, -1, NVIF_CLASS_VMM_NV04}, nv41_vmm_new, true },
};

int
nv41_mmu_new(struct nvkm_device *device, int index, struct nvkm_mmu **pmmu)
{
	if (device->type == NVKM_DEVICE_AGP ||
	    !nvkm_boolopt(device->cfgopt, "NvPCIE", true))
		return nv04_mmu_new(device, index, pmmu);

	return nvkm_mmu_new_(&nv41_mmu, device, index, pmmu);
}
