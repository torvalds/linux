/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "vmm.h"

#include <subdev/fb.h>

#include <nvhw/drf.h>
#include <nvhw/ref/gh100/dev_mmu.h>

static inline void
gh100_vmm_pgt_pte(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes,
		  struct nvkm_vmm_map *map, u64 addr)
{
	u64 data = addr | map->type;

	while (ptes--) {
		VMM_WO064(pt, vmm, ptei++ * NV_MMU_VER3_PTE__SIZE, data);
		data += map->next;
	}
}

static void
gh100_vmm_pgt_sgl(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes,
		  struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_SGL(vmm, pt, ptei, ptes, map, gh100_vmm_pgt_pte);
}

static void
gh100_vmm_pgt_dma(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes,
		  struct nvkm_vmm_map *map)
{
	if (map->page->shift == PAGE_SHIFT) {
		VMM_SPAM(vmm, "DMAA %08x %08x PTE(s)", ptei, ptes);

		nvkm_kmap(pt->memory);
		while (ptes--) {
			const u64 data = *map->dma++ | map->type;

			VMM_WO064(pt, vmm, ptei++ * NV_MMU_VER3_PTE__SIZE, data);
		}
		nvkm_done(pt->memory);
		return;
	}

	VMM_MAP_ITER_DMA(vmm, pt, ptei, ptes, map, gh100_vmm_pgt_pte);
}

static void
gh100_vmm_pgt_mem(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes,
		  struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_MEM(vmm, pt, ptei, ptes, map, gh100_vmm_pgt_pte);
}

static void
gh100_vmm_pgt_sparse(struct nvkm_vmm *vmm,
		     struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	const u64 data = NVDEF(NV_MMU, VER3_PTE, PCF, SPARSE);

	VMM_FO064(pt, vmm, ptei * NV_MMU_VER3_PTE__SIZE, data, ptes);
}

static const struct nvkm_vmm_desc_func
gh100_vmm_desc_spt = {
	.unmap = gf100_vmm_pgt_unmap,
	.sparse = gh100_vmm_pgt_sparse,
	.mem = gh100_vmm_pgt_mem,
	.dma = gh100_vmm_pgt_dma,
	.sgl = gh100_vmm_pgt_sgl,
};

static void
gh100_vmm_lpt_invalid(struct nvkm_vmm *vmm,
		      struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	const u64 data = NVDEF(NV_MMU, VER3_PTE, PCF, NO_VALID_4KB_PAGE);

	VMM_FO064(pt, vmm, ptei * NV_MMU_VER3_PTE__SIZE, data, ptes);
}

static const struct nvkm_vmm_desc_func
gh100_vmm_desc_lpt = {
	.invalid = gh100_vmm_lpt_invalid,
	.unmap = gf100_vmm_pgt_unmap,
	.sparse = gh100_vmm_pgt_sparse,
	.mem = gh100_vmm_pgt_mem,
};

static inline void
gh100_vmm_pd0_pte(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map, u64 addr)
{
	u64 data = addr | map->type;

	while (ptes--) {
		VMM_WO128(pt, vmm, ptei++ * NV_MMU_VER3_DUAL_PDE__SIZE, data, 0ULL);
		data += map->next;
	}
}

static void
gh100_vmm_pd0_mem(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_MEM(vmm, pt, ptei, ptes, map, gh100_vmm_pd0_pte);
}

static inline bool
gh100_vmm_pde(struct nvkm_mmu_pt *pt, u64 *data)
{
	switch (nvkm_memory_target(pt->memory)) {
	case NVKM_MEM_TARGET_VRAM:
		*data |= NVDEF(NV_MMU, VER3_PDE, APERTURE, VIDEO_MEMORY);
		*data |= NVDEF(NV_MMU, VER3_PDE, PCF, VALID_CACHED_ATS_NOT_ALLOWED);
		break;
	case NVKM_MEM_TARGET_HOST:
		*data |= NVDEF(NV_MMU, VER3_PDE, APERTURE, SYSTEM_COHERENT_MEMORY);
		*data |= NVDEF(NV_MMU, VER3_PDE, PCF, VALID_UNCACHED_ATS_ALLOWED);
		break;
	case NVKM_MEM_TARGET_NCOH:
		*data |= NVDEF(NV_MMU, VER3_PDE, APERTURE, SYSTEM_NON_COHERENT_MEMORY);
		*data |= NVDEF(NV_MMU, VER3_PDE, PCF, VALID_CACHED_ATS_ALLOWED);
		break;
	default:
		WARN_ON(1);
		return false;
	}

	*data |= pt->addr;
	return true;
}

static void
gh100_vmm_pd0_pde(struct nvkm_vmm *vmm, struct nvkm_vmm_pt *pgd, u32 pdei)
{
	struct nvkm_vmm_pt *pgt = pgd->pde[pdei];
	struct nvkm_mmu_pt *pd = pgd->pt[0];
	u64 data[2] = {};

	if (pgt->pt[0] && !gh100_vmm_pde(pgt->pt[0], &data[0]))
		return;
	if (pgt->pt[1] && !gh100_vmm_pde(pgt->pt[1], &data[1]))
		return;

	nvkm_kmap(pd->memory);
	VMM_WO128(pd, vmm, pdei * NV_MMU_VER3_DUAL_PDE__SIZE, data[0], data[1]);
	nvkm_done(pd->memory);
}

static void
gh100_vmm_pd0_sparse(struct nvkm_vmm *vmm,
		     struct nvkm_mmu_pt *pt, u32 pdei, u32 pdes)
{
	const u64 data = NVDEF(NV_MMU, VER3_DUAL_PDE, PCF_BIG, SPARSE_ATS_ALLOWED);

	VMM_FO128(pt, vmm, pdei * NV_MMU_VER3_DUAL_PDE__SIZE, data, 0ULL, pdes);
}

static void
gh100_vmm_pd0_unmap(struct nvkm_vmm *vmm,
		    struct nvkm_mmu_pt *pt, u32 pdei, u32 pdes)
{
	VMM_FO128(pt, vmm, pdei * NV_MMU_VER3_DUAL_PDE__SIZE, 0ULL, 0ULL, pdes);
}

static const struct nvkm_vmm_desc_func
gh100_vmm_desc_pd0 = {
	.unmap = gh100_vmm_pd0_unmap,
	.sparse = gh100_vmm_pd0_sparse,
	.pde = gh100_vmm_pd0_pde,
	.mem = gh100_vmm_pd0_mem,
};

static void
gh100_vmm_pd1_pde(struct nvkm_vmm *vmm, struct nvkm_vmm_pt *pgd, u32 pdei)
{
	struct nvkm_vmm_pt *pgt = pgd->pde[pdei];
	struct nvkm_mmu_pt *pd = pgd->pt[0];
	u64 data = 0;

	if (!gh100_vmm_pde(pgt->pt[0], &data))
		return;

	nvkm_kmap(pd->memory);
	VMM_WO064(pd, vmm, pdei * NV_MMU_VER3_PDE__SIZE, data);
	nvkm_done(pd->memory);
}

static const struct nvkm_vmm_desc_func
gh100_vmm_desc_pd1 = {
	.unmap = gf100_vmm_pgt_unmap,
	.sparse = gh100_vmm_pgt_sparse,
	.pde = gh100_vmm_pd1_pde,
};

static const struct nvkm_vmm_desc
gh100_vmm_desc_16[] = {
	{ LPT, 5,  8, 0x0100, &gh100_vmm_desc_lpt },
	{ PGD, 8, 16, 0x1000, &gh100_vmm_desc_pd0 },
	{ PGD, 9,  8, 0x1000, &gh100_vmm_desc_pd1 },
	{ PGD, 9,  8, 0x1000, &gh100_vmm_desc_pd1 },
	{ PGD, 9,  8, 0x1000, &gh100_vmm_desc_pd1 },
	{ PGD, 1,  8, 0x1000, &gh100_vmm_desc_pd1 },
	{}
};

static const struct nvkm_vmm_desc
gh100_vmm_desc_12[] = {
	{ SPT, 9,  8, 0x1000, &gh100_vmm_desc_spt },
	{ PGD, 8, 16, 0x1000, &gh100_vmm_desc_pd0 },
	{ PGD, 9,  8, 0x1000, &gh100_vmm_desc_pd1 },
	{ PGD, 9,  8, 0x1000, &gh100_vmm_desc_pd1 },
	{ PGD, 9,  8, 0x1000, &gh100_vmm_desc_pd1 },
	{ PGD, 1,  8, 0x1000, &gh100_vmm_desc_pd1 },
	{}
};

static int
gh100_vmm_valid(struct nvkm_vmm *vmm, bool ro, bool priv, u8 kind, u8 comp,
		struct nvkm_vmm_map *map)
{
	const enum nvkm_memory_target target = nvkm_memory_target(map->memory);
	const bool vol = target == NVKM_MEM_TARGET_HOST;
	const struct nvkm_vmm_page *page = map->page;
	u8 kind_inv, pcf;
	int kindn, aper;
	const u8 *kindm;

	map->next = 1ULL << page->shift;
	map->type = 0;

	aper = vmm->func->aper(target);
	if (WARN_ON(aper < 0))
		return aper;

	kindm = vmm->mmu->func->kind(vmm->mmu, &kindn, &kind_inv);
	if (kind >= kindn || kindm[kind] == kind_inv) {
		VMM_DEBUG(vmm, "kind %02x", kind);
		return -EINVAL;
	}

	if (priv) {
		if (ro) {
			if (vol)
				pcf = NV_MMU_VER3_PTE_PCF_PRIVILEGE_RO_ATOMIC_UNCACHED_ACD;
			else
				pcf = NV_MMU_VER3_PTE_PCF_PRIVILEGE_RO_ATOMIC_CACHED_ACD;
		} else {
			if (vol)
				pcf = NV_MMU_VER3_PTE_PCF_PRIVILEGE_RW_ATOMIC_UNCACHED_ACD;
			else
				pcf = NV_MMU_VER3_PTE_PCF_PRIVILEGE_RW_ATOMIC_CACHED_ACD;
		}
	} else {
		if (ro) {
			if (vol)
				pcf = NV_MMU_VER3_PTE_PCF_REGULAR_RO_ATOMIC_UNCACHED_ACD;
			else
				pcf = NV_MMU_VER3_PTE_PCF_REGULAR_RO_ATOMIC_CACHED_ACD;
		} else {
			if (vol)
				pcf = NV_MMU_VER3_PTE_PCF_REGULAR_RW_ATOMIC_UNCACHED_ACD;
			else
				pcf = NV_MMU_VER3_PTE_PCF_REGULAR_RW_ATOMIC_CACHED_ACD;
		}
	}

	map->type |= NVDEF(NV_MMU, VER3_PTE, VALID, TRUE);
	map->type |= NVVAL(NV_MMU, VER3_PTE, APERTURE, aper);
	map->type |= NVVAL(NV_MMU, VER3_PTE, PCF, pcf);
	map->type |= NVVAL(NV_MMU, VER3_PTE, KIND, kind);
	return 0;
}

static const struct nvkm_vmm_func
gh100_vmm = {
	.join = gv100_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gp100_vmm_valid,
	.valid2 = gh100_vmm_valid,
	.flush = tu102_vmm_flush,
	.page = {
		{ 56, &gh100_vmm_desc_16[5], NVKM_VMM_PAGE_Sxxx },
		{ 47, &gh100_vmm_desc_16[4], NVKM_VMM_PAGE_Sxxx },
		{ 38, &gh100_vmm_desc_16[3], NVKM_VMM_PAGE_Sxxx },
		{ 29, &gh100_vmm_desc_16[2], NVKM_VMM_PAGE_SVxC },
		{ 21, &gh100_vmm_desc_16[1], NVKM_VMM_PAGE_SVxC },
		{ 16, &gh100_vmm_desc_16[0], NVKM_VMM_PAGE_SVxC },
		{ 12, &gh100_vmm_desc_12[0], NVKM_VMM_PAGE_SVHx },
		{}
	}
};

int
gh100_vmm_new(struct nvkm_mmu *mmu, bool managed, u64 addr, u64 size,
	      void *argv, u32 argc, struct lock_class_key *key,
	      const char *name, struct nvkm_vmm **pvmm)
{
	return gp100_vmm_new_(&gh100_vmm, mmu, managed, addr, size,
			      argv, argc, key, name, pvmm);
}
