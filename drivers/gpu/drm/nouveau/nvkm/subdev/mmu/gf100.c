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

#include <subdev/fb.h>
#include <subdev/ltc.h>
#include <subdev/timer.h>

#include <core/gpuobj.h>

/* Map from compressed to corresponding uncompressed storage type.
 * The value 0xff represents an invalid storage type.
 */
const u8 gf100_pte_storage_type_map[256] =
{
	0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0xff, 0x01, /* 0x00 */
	0x01, 0x01, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x11, 0xff, 0xff, 0xff, 0xff, 0xff, 0x11, /* 0x10 */
	0x11, 0x11, 0x11, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x26, 0x27, /* 0x20 */
	0x28, 0x29, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0x30 */
	0xff, 0xff, 0x26, 0x27, 0x28, 0x29, 0x26, 0x27,
	0x28, 0x29, 0xff, 0xff, 0xff, 0xff, 0x46, 0xff, /* 0x40 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x46, 0x46, 0x46, 0x46, 0xff, 0xff, 0xff, /* 0x50 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0x60 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0x70 */
	0xff, 0xff, 0xff, 0x7b, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7b, 0x7b, /* 0x80 */
	0x7b, 0x7b, 0xff, 0x8b, 0x8c, 0x8d, 0x8e, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0x90 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x8b, 0x8c, 0x8d, 0x8e, 0xa7, /* 0xa0 */
	0xa8, 0xa9, 0xaa, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0xb0 */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xa7,
	0xa8, 0xa9, 0xaa, 0xc3, 0xff, 0xff, 0xff, 0xff, /* 0xc0 */
	0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xc3, 0xc3,
	0xc3, 0xc3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 0xd0 */
	0xfe, 0xff, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
	0xfe, 0xff, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xff, /* 0xe0 */
	0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xfe, 0xff,
	0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, /* 0xf0 */
	0xfe, 0xfe, 0xfe, 0xfe, 0xff, 0xfd, 0xfe, 0xff
};


static void
gf100_vm_map_pgt(struct nvkm_gpuobj *pgd, u32 index, struct nvkm_memory *pgt[2])
{
	u32 pde[2] = { 0, 0 };

	if (pgt[0])
		pde[1] = 0x00000001 | (nvkm_memory_addr(pgt[0]) >> 8);
	if (pgt[1])
		pde[0] = 0x00000001 | (nvkm_memory_addr(pgt[1]) >> 8);

	nvkm_kmap(pgd);
	nvkm_wo32(pgd, (index * 8) + 0, pde[0]);
	nvkm_wo32(pgd, (index * 8) + 4, pde[1]);
	nvkm_done(pgd);
}

static inline u64
gf100_vm_addr(struct nvkm_vma *vma, u64 phys, u32 memtype, u32 target)
{
	phys >>= 8;

	phys |= 0x00000001; /* present */
	if (vma->access & NV_MEM_ACCESS_SYS)
		phys |= 0x00000002;

	phys |= ((u64)target  << 32);
	phys |= ((u64)memtype << 36);
	return phys;
}

static void
gf100_vm_map(struct nvkm_vma *vma, struct nvkm_memory *pgt,
	     struct nvkm_mem *mem, u32 pte, u32 cnt, u64 phys, u64 delta)
{
	u64 next = 1 << (vma->node->type - 8);

	phys  = gf100_vm_addr(vma, phys, mem->memtype, 0);
	pte <<= 3;

	if (mem->tag) {
		struct nvkm_ltc *ltc = vma->vm->mmu->subdev.device->ltc;
		u32 tag = mem->tag->offset + (delta >> 17);
		phys |= (u64)tag << (32 + 12);
		next |= (u64)1   << (32 + 12);
		nvkm_ltc_tags_clear(ltc, tag, cnt);
	}

	nvkm_kmap(pgt);
	while (cnt--) {
		nvkm_wo32(pgt, pte + 0, lower_32_bits(phys));
		nvkm_wo32(pgt, pte + 4, upper_32_bits(phys));
		phys += next;
		pte  += 8;
	}
	nvkm_done(pgt);
}

static void
gf100_vm_map_sg(struct nvkm_vma *vma, struct nvkm_memory *pgt,
		struct nvkm_mem *mem, u32 pte, u32 cnt, dma_addr_t *list)
{
	u32 target = (vma->access & NV_MEM_ACCESS_NOSNOOP) ? 7 : 5;
	/* compressed storage types are invalid for system memory */
	u32 memtype = gf100_pte_storage_type_map[mem->memtype & 0xff];

	nvkm_kmap(pgt);
	pte <<= 3;
	while (cnt--) {
		u64 phys = gf100_vm_addr(vma, *list++, memtype, target);
		nvkm_wo32(pgt, pte + 0, lower_32_bits(phys));
		nvkm_wo32(pgt, pte + 4, upper_32_bits(phys));
		pte += 8;
	}
	nvkm_done(pgt);
}

static void
gf100_vm_unmap(struct nvkm_vma *vma, struct nvkm_memory *pgt, u32 pte, u32 cnt)
{
	nvkm_kmap(pgt);
	pte <<= 3;
	while (cnt--) {
		nvkm_wo32(pgt, pte + 0, 0x00000000);
		nvkm_wo32(pgt, pte + 4, 0x00000000);
		pte += 8;
	}
	nvkm_done(pgt);
}

static void
gf100_vm_flush(struct nvkm_vm *vm)
{
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_device *device = mmu->subdev.device;
	struct nvkm_vm_pgd *vpgd;
	u32 type;

	type = 0x00000001; /* PAGE_ALL */
	if (atomic_read(&vm->engref[NVKM_SUBDEV_BAR]))
		type |= 0x00000004; /* HUB_ONLY */

	mutex_lock(&mmu->subdev.mutex);
	list_for_each_entry(vpgd, &vm->pgd_list, head) {
		/* looks like maybe a "free flush slots" counter, the
		 * faster you write to 0x100cbc to more it decreases
		 */
		nvkm_msec(device, 2000,
			if (nvkm_rd32(device, 0x100c80) & 0x00ff0000)
				break;
		);

		nvkm_wr32(device, 0x100cb8, vpgd->obj->addr >> 8);
		nvkm_wr32(device, 0x100cbc, 0x80000000 | type);

		/* wait for flush to be queued? */
		nvkm_msec(device, 2000,
			if (nvkm_rd32(device, 0x100c80) & 0x00008000)
				break;
		);
	}
	mutex_unlock(&mmu->subdev.mutex);
}

static int
gf100_vm_create(struct nvkm_mmu *mmu, u64 offset, u64 length, u64 mm_offset,
		struct lock_class_key *key, struct nvkm_vm **pvm)
{
	return nvkm_vm_create(mmu, offset, length, mm_offset, 4096, key, pvm);
}

static const struct nvkm_mmu_func
gf100_mmu = {
	.limit = (1ULL << 40),
	.dma_bits = 40,
	.pgt_bits  = 27 - 12,
	.spg_shift = 12,
	.lpg_shift = 17,
	.create = gf100_vm_create,
	.map_pgt = gf100_vm_map_pgt,
	.map = gf100_vm_map,
	.map_sg = gf100_vm_map_sg,
	.unmap = gf100_vm_unmap,
	.flush = gf100_vm_flush,
};

int
gf100_mmu_new(struct nvkm_device *device, int index, struct nvkm_mmu **pmmu)
{
	return nvkm_mmu_new_(&gf100_mmu, device, index, pmmu);
}
