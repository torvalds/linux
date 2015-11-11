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
#include "priv.h"

#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/timer.h>
#include <engine/gr.h>

static void
nv50_vm_map_pgt(struct nvkm_gpuobj *pgd, u32 pde, struct nvkm_memory *pgt[2])
{
	u64 phys = 0xdeadcafe00000000ULL;
	u32 coverage = 0;

	if (pgt[0]) {
		/* present, 4KiB pages */
		phys = 0x00000003 | nvkm_memory_addr(pgt[0]);
		coverage = (nvkm_memory_size(pgt[0]) >> 3) << 12;
	} else
	if (pgt[1]) {
		/* present, 64KiB pages  */
		phys = 0x00000001 | nvkm_memory_addr(pgt[1]);
		coverage = (nvkm_memory_size(pgt[1]) >> 3) << 16;
	}

	if (phys & 1) {
		if (coverage <= 32 * 1024 * 1024)
			phys |= 0x60;
		else if (coverage <= 64 * 1024 * 1024)
			phys |= 0x40;
		else if (coverage <= 128 * 1024 * 1024)
			phys |= 0x20;
	}

	nvkm_kmap(pgd);
	nvkm_wo32(pgd, (pde * 8) + 0, lower_32_bits(phys));
	nvkm_wo32(pgd, (pde * 8) + 4, upper_32_bits(phys));
	nvkm_done(pgd);
}

static inline u64
vm_addr(struct nvkm_vma *vma, u64 phys, u32 memtype, u32 target)
{
	phys |= 1; /* present */
	phys |= (u64)memtype << 40;
	phys |= target << 4;
	if (vma->access & NV_MEM_ACCESS_SYS)
		phys |= (1 << 6);
	if (!(vma->access & NV_MEM_ACCESS_WO))
		phys |= (1 << 3);
	return phys;
}

static void
nv50_vm_map(struct nvkm_vma *vma, struct nvkm_memory *pgt,
	    struct nvkm_mem *mem, u32 pte, u32 cnt, u64 phys, u64 delta)
{
	struct nvkm_ram *ram = vma->vm->mmu->subdev.device->fb->ram;
	u32 comp = (mem->memtype & 0x180) >> 7;
	u32 block, target;
	int i;

	/* IGPs don't have real VRAM, re-target to stolen system memory */
	target = 0;
	if (ram->stolen) {
		phys += ram->stolen;
		target = 3;
	}

	phys  = vm_addr(vma, phys, mem->memtype, target);
	pte <<= 3;
	cnt <<= 3;

	nvkm_kmap(pgt);
	while (cnt) {
		u32 offset_h = upper_32_bits(phys);
		u32 offset_l = lower_32_bits(phys);

		for (i = 7; i >= 0; i--) {
			block = 1 << (i + 3);
			if (cnt >= block && !(pte & (block - 1)))
				break;
		}
		offset_l |= (i << 7);

		phys += block << (vma->node->type - 3);
		cnt  -= block;
		if (comp) {
			u32 tag = mem->tag->offset + ((delta >> 16) * comp);
			offset_h |= (tag << 17);
			delta    += block << (vma->node->type - 3);
		}

		while (block) {
			nvkm_wo32(pgt, pte + 0, offset_l);
			nvkm_wo32(pgt, pte + 4, offset_h);
			pte += 8;
			block -= 8;
		}
	}
	nvkm_done(pgt);
}

static void
nv50_vm_map_sg(struct nvkm_vma *vma, struct nvkm_memory *pgt,
	       struct nvkm_mem *mem, u32 pte, u32 cnt, dma_addr_t *list)
{
	u32 target = (vma->access & NV_MEM_ACCESS_NOSNOOP) ? 3 : 2;
	pte <<= 3;
	nvkm_kmap(pgt);
	while (cnt--) {
		u64 phys = vm_addr(vma, (u64)*list++, mem->memtype, target);
		nvkm_wo32(pgt, pte + 0, lower_32_bits(phys));
		nvkm_wo32(pgt, pte + 4, upper_32_bits(phys));
		pte += 8;
	}
	nvkm_done(pgt);
}

static void
nv50_vm_unmap(struct nvkm_vma *vma, struct nvkm_memory *pgt, u32 pte, u32 cnt)
{
	pte <<= 3;
	nvkm_kmap(pgt);
	while (cnt--) {
		nvkm_wo32(pgt, pte + 0, 0x00000000);
		nvkm_wo32(pgt, pte + 4, 0x00000000);
		pte += 8;
	}
	nvkm_done(pgt);
}

static void
nv50_vm_flush(struct nvkm_vm *vm)
{
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_subdev *subdev = &mmu->subdev;
	struct nvkm_device *device = subdev->device;
	int i, vme;

	mutex_lock(&subdev->mutex);
	for (i = 0; i < NVKM_SUBDEV_NR; i++) {
		if (!atomic_read(&vm->engref[i]))
			continue;

		/* unfortunate hw bug workaround... */
		if (i == NVKM_ENGINE_GR && device->gr) {
			int ret = nvkm_gr_tlb_flush(device->gr);
			if (ret != -ENODEV)
				continue;
		}

		switch (i) {
		case NVKM_ENGINE_GR    : vme = 0x00; break;
		case NVKM_ENGINE_VP    :
		case NVKM_ENGINE_MSPDEC: vme = 0x01; break;
		case NVKM_SUBDEV_BAR   : vme = 0x06; break;
		case NVKM_ENGINE_MSPPP :
		case NVKM_ENGINE_MPEG  : vme = 0x08; break;
		case NVKM_ENGINE_BSP   :
		case NVKM_ENGINE_MSVLD : vme = 0x09; break;
		case NVKM_ENGINE_CIPHER:
		case NVKM_ENGINE_SEC   : vme = 0x0a; break;
		case NVKM_ENGINE_CE0   : vme = 0x0d; break;
		default:
			continue;
		}

		nvkm_wr32(device, 0x100c80, (vme << 16) | 1);
		if (nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x100c80) & 0x00000001))
				break;
		) < 0)
			nvkm_error(subdev, "vm flush timeout: engine %d\n", vme);
	}
	mutex_unlock(&subdev->mutex);
}

static int
nv50_vm_create(struct nvkm_mmu *mmu, u64 offset, u64 length, u64 mm_offset,
	       struct lock_class_key *key, struct nvkm_vm **pvm)
{
	u32 block = (1 << (mmu->func->pgt_bits + 12));
	if (block > length)
		block = length;

	return nvkm_vm_create(mmu, offset, length, mm_offset, block, key, pvm);
}

static const struct nvkm_mmu_func
nv50_mmu = {
	.limit = (1ULL << 40),
	.dma_bits = 40,
	.pgt_bits  = 29 - 12,
	.spg_shift = 12,
	.lpg_shift = 16,
	.create = nv50_vm_create,
	.map_pgt = nv50_vm_map_pgt,
	.map = nv50_vm_map,
	.map_sg = nv50_vm_map_sg,
	.unmap = nv50_vm_unmap,
	.flush = nv50_vm_flush,
};

int
nv50_mmu_new(struct nvkm_device *device, int index, struct nvkm_mmu **pmmu)
{
	return nvkm_mmu_new_(&nv50_mmu, device, index, pmmu);
}
