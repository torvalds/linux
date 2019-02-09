/*
 * Copyright 2017 Red Hat Inc.
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
 */
#include "vmm.h"

#include <subdev/timer.h>

static void
nv41_vmm_pgt_pte(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		 u32 ptei, u32 ptes, struct nvkm_vmm_map *map, u64 addr)
{
	u32 data = (addr >> 7) | 0x00000001; /* VALID. */
	while (ptes--) {
		VMM_WO032(pt, vmm, ptei++ * 4, data);
		data += 0x00000020;
	}
}

static void
nv41_vmm_pgt_sgl(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		 u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_SGL(vmm, pt, ptei, ptes, map, nv41_vmm_pgt_pte);
}

static void
nv41_vmm_pgt_dma(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		 u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
#if PAGE_SHIFT == 12
	nvkm_kmap(pt->memory);
	while (ptes--) {
		const u32 data = (*map->dma++ >> 7) | 0x00000001;
		VMM_WO032(pt, vmm, ptei++ * 4, data);
	}
	nvkm_done(pt->memory);
#else
	VMM_MAP_ITER_DMA(vmm, pt, ptei, ptes, map, nv41_vmm_pgt_pte);
#endif
}

static void
nv41_vmm_pgt_unmap(struct nvkm_vmm *vmm,
		   struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	VMM_FO032(pt, vmm, ptei * 4, 0, ptes);
}

static const struct nvkm_vmm_desc_func
nv41_vmm_desc_pgt = {
	.unmap = nv41_vmm_pgt_unmap,
	.dma = nv41_vmm_pgt_dma,
	.sgl = nv41_vmm_pgt_sgl,
};

static const struct nvkm_vmm_desc
nv41_vmm_desc_12[] = {
	{ PGT, 17, 4, 0x1000, &nv41_vmm_desc_pgt },
	{}
};

static void
nv41_vmm_flush(struct nvkm_vmm *vmm, int level)
{
	struct nvkm_subdev *subdev = &vmm->mmu->subdev;
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

static const struct nvkm_vmm_func
nv41_vmm = {
	.valid = nv04_vmm_valid,
	.flush = nv41_vmm_flush,
	.page = {
		{ 12, &nv41_vmm_desc_12[0], NVKM_VMM_PAGE_HOST },
		{}
	}
};

int
nv41_vmm_new(struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	     struct lock_class_key *key, const char *name,
	     struct nvkm_vmm **pvmm)
{
	return nv04_vmm_new_(&nv41_vmm, mmu, 0, addr, size,
			     argv, argc, key, name, pvmm);
}
