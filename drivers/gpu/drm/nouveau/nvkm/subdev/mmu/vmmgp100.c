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

#include <subdev/fb.h>
#include <subdev/ltc.h>

#include <nvif/ifc00d.h>
#include <nvif/unpack.h>

static inline void
gp100_vmm_pgt_pte(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map, u64 addr)
{
	u64 data = (addr >> 4) | map->type;

	map->type += ptes * map->ctag;

	while (ptes--) {
		VMM_WO064(pt, vmm, ptei++ * 8, data);
		data += map->next;
	}
}

static void
gp100_vmm_pgt_sgl(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_SGL(vmm, pt, ptei, ptes, map, gp100_vmm_pgt_pte);
}

static void
gp100_vmm_pgt_dma(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	if (map->page->shift == PAGE_SHIFT) {
		VMM_SPAM(vmm, "DMAA %08x %08x PTE(s)", ptei, ptes);
		nvkm_kmap(pt->memory);
		while (ptes--) {
			const u64 data = (*map->dma++ >> 4) | map->type;
			VMM_WO064(pt, vmm, ptei++ * 8, data);
			map->type += map->ctag;
		}
		nvkm_done(pt->memory);
		return;
	}

	VMM_MAP_ITER_DMA(vmm, pt, ptei, ptes, map, gp100_vmm_pgt_pte);
}

static void
gp100_vmm_pgt_mem(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_MEM(vmm, pt, ptei, ptes, map, gp100_vmm_pgt_pte);
}

static void
gp100_vmm_pgt_sparse(struct nvkm_vmm *vmm,
		     struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	/* VALID_FALSE + VOL tells the MMU to treat the PTE as sparse. */
	VMM_FO064(pt, vmm, ptei * 8, BIT_ULL(3) /* VOL. */, ptes);
}

static const struct nvkm_vmm_desc_func
gp100_vmm_desc_spt = {
	.unmap = gf100_vmm_pgt_unmap,
	.sparse = gp100_vmm_pgt_sparse,
	.mem = gp100_vmm_pgt_mem,
	.dma = gp100_vmm_pgt_dma,
	.sgl = gp100_vmm_pgt_sgl,
};

static void
gp100_vmm_lpt_invalid(struct nvkm_vmm *vmm,
		      struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	/* VALID_FALSE + PRIV tells the MMU to ignore corresponding SPTEs. */
	VMM_FO064(pt, vmm, ptei * 8, BIT_ULL(5) /* PRIV. */, ptes);
}

static const struct nvkm_vmm_desc_func
gp100_vmm_desc_lpt = {
	.invalid = gp100_vmm_lpt_invalid,
	.unmap = gf100_vmm_pgt_unmap,
	.sparse = gp100_vmm_pgt_sparse,
	.mem = gp100_vmm_pgt_mem,
};

static inline void
gp100_vmm_pd0_pte(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map, u64 addr)
{
	u64 data = (addr >> 4) | map->type;

	map->type += ptes * map->ctag;

	while (ptes--) {
		VMM_WO128(pt, vmm, ptei++ * 0x10, data, 0ULL);
		data += map->next;
	}
}

static void
gp100_vmm_pd0_mem(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_MEM(vmm, pt, ptei, ptes, map, gp100_vmm_pd0_pte);
}

static inline bool
gp100_vmm_pde(struct nvkm_mmu_pt *pt, u64 *data)
{
	switch (nvkm_memory_target(pt->memory)) {
	case NVKM_MEM_TARGET_VRAM: *data |= 1ULL << 1; break;
	case NVKM_MEM_TARGET_HOST: *data |= 2ULL << 1;
		*data |= BIT_ULL(3); /* VOL. */
		break;
	case NVKM_MEM_TARGET_NCOH: *data |= 3ULL << 1; break;
	default:
		WARN_ON(1);
		return false;
	}
	*data |= pt->addr >> 4;
	return true;
}

static void
gp100_vmm_pd0_pde(struct nvkm_vmm *vmm, struct nvkm_vmm_pt *pgd, u32 pdei)
{
	struct nvkm_vmm_pt *pgt = pgd->pde[pdei];
	struct nvkm_mmu_pt *pd = pgd->pt[0];
	u64 data[2] = {};

	if (pgt->pt[0] && !gp100_vmm_pde(pgt->pt[0], &data[0]))
		return;
	if (pgt->pt[1] && !gp100_vmm_pde(pgt->pt[1], &data[1]))
		return;

	nvkm_kmap(pd->memory);
	VMM_WO128(pd, vmm, pdei * 0x10, data[0], data[1]);
	nvkm_done(pd->memory);
}

static void
gp100_vmm_pd0_sparse(struct nvkm_vmm *vmm,
		     struct nvkm_mmu_pt *pt, u32 pdei, u32 pdes)
{
	/* VALID_FALSE + VOL_BIG tells the MMU to treat the PDE as sparse. */
	VMM_FO128(pt, vmm, pdei * 0x10, BIT_ULL(3) /* VOL_BIG. */, 0ULL, pdes);
}

static void
gp100_vmm_pd0_unmap(struct nvkm_vmm *vmm,
		    struct nvkm_mmu_pt *pt, u32 pdei, u32 pdes)
{
	VMM_FO128(pt, vmm, pdei * 0x10, 0ULL, 0ULL, pdes);
}

static const struct nvkm_vmm_desc_func
gp100_vmm_desc_pd0 = {
	.unmap = gp100_vmm_pd0_unmap,
	.sparse = gp100_vmm_pd0_sparse,
	.pde = gp100_vmm_pd0_pde,
	.mem = gp100_vmm_pd0_mem,
};

static void
gp100_vmm_pd1_pde(struct nvkm_vmm *vmm, struct nvkm_vmm_pt *pgd, u32 pdei)
{
	struct nvkm_vmm_pt *pgt = pgd->pde[pdei];
	struct nvkm_mmu_pt *pd = pgd->pt[0];
	u64 data = 0;

	if (!gp100_vmm_pde(pgt->pt[0], &data))
		return;

	nvkm_kmap(pd->memory);
	VMM_WO064(pd, vmm, pdei * 8, data);
	nvkm_done(pd->memory);
}

static const struct nvkm_vmm_desc_func
gp100_vmm_desc_pd1 = {
	.unmap = gf100_vmm_pgt_unmap,
	.sparse = gp100_vmm_pgt_sparse,
	.pde = gp100_vmm_pd1_pde,
};

const struct nvkm_vmm_desc
gp100_vmm_desc_16[] = {
	{ LPT, 5,  8, 0x0100, &gp100_vmm_desc_lpt },
	{ PGD, 8, 16, 0x1000, &gp100_vmm_desc_pd0 },
	{ PGD, 9,  8, 0x1000, &gp100_vmm_desc_pd1 },
	{ PGD, 9,  8, 0x1000, &gp100_vmm_desc_pd1 },
	{ PGD, 2,  8, 0x1000, &gp100_vmm_desc_pd1 },
	{}
};

const struct nvkm_vmm_desc
gp100_vmm_desc_12[] = {
	{ SPT, 9,  8, 0x1000, &gp100_vmm_desc_spt },
	{ PGD, 8, 16, 0x1000, &gp100_vmm_desc_pd0 },
	{ PGD, 9,  8, 0x1000, &gp100_vmm_desc_pd1 },
	{ PGD, 9,  8, 0x1000, &gp100_vmm_desc_pd1 },
	{ PGD, 2,  8, 0x1000, &gp100_vmm_desc_pd1 },
	{}
};

int
gp100_vmm_valid(struct nvkm_vmm *vmm, void *argv, u32 argc,
		struct nvkm_vmm_map *map)
{
	const enum nvkm_memory_target target = nvkm_memory_target(map->memory);
	const struct nvkm_vmm_page *page = map->page;
	union {
		struct gp100_vmm_map_vn vn;
		struct gp100_vmm_map_v0 v0;
	} *args = argv;
	struct nvkm_device *device = vmm->mmu->subdev.device;
	struct nvkm_memory *memory = map->memory;
	u8  kind, priv, ro, vol;
	int kindn, aper, ret = -ENOSYS;
	const u8 *kindm;

	map->next = (1ULL << page->shift) >> 4;
	map->type = 0;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		vol  = !!args->v0.vol;
		ro   = !!args->v0.ro;
		priv = !!args->v0.priv;
		kind =   args->v0.kind;
	} else
	if (!(ret = nvif_unvers(ret, &argv, &argc, args->vn))) {
		vol  = target == NVKM_MEM_TARGET_HOST;
		ro   = 0;
		priv = 0;
		kind = 0x00;
	} else {
		VMM_DEBUG(vmm, "args");
		return ret;
	}

	aper = vmm->func->aper(target);
	if (WARN_ON(aper < 0))
		return aper;

	kindm = vmm->mmu->func->kind(vmm->mmu, &kindn);
	if (kind >= kindn || kindm[kind] == 0xff) {
		VMM_DEBUG(vmm, "kind %02x", kind);
		return -EINVAL;
	}

	if (kindm[kind] != kind) {
		u64 tags = nvkm_memory_size(memory) >> 16;
		if (aper != 0 || !(page->type & NVKM_VMM_PAGE_COMP)) {
			VMM_DEBUG(vmm, "comp %d %02x", aper, page->type);
			return -EINVAL;
		}

		ret = nvkm_memory_tags_get(memory, device, tags,
					   nvkm_ltc_tags_clear,
					   &map->tags);
		if (ret) {
			VMM_DEBUG(vmm, "comp %d", ret);
			return ret;
		}

		if (map->tags->mn) {
			tags = map->tags->mn->offset + (map->offset >> 16);
			map->ctag |= ((1ULL << page->shift) >> 16) << 36;
			map->type |= tags << 36;
			map->next |= map->ctag;
		} else {
			kind = kindm[kind];
		}
	}

	map->type |= BIT(0);
	map->type |= (u64)aper << 1;
	map->type |= (u64) vol << 3;
	map->type |= (u64)priv << 5;
	map->type |= (u64)  ro << 6;
	map->type |= (u64)kind << 56;
	return 0;
}

void
gp100_vmm_flush(struct nvkm_vmm *vmm, int depth)
{
	gf100_vmm_flush_(vmm, 5 /* CACHE_LEVEL_UP_TO_PDE3 */ - depth);
}

int
gp100_vmm_join(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	const u64 base = BIT_ULL(10) /* VER2 */ | BIT_ULL(11); /* 64KiB */
	return gf100_vmm_join_(vmm, inst, base);
}

static const struct nvkm_vmm_func
gp100_vmm = {
	.join = gp100_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gp100_vmm_valid,
	.flush = gp100_vmm_flush,
	.page = {
		{ 47, &gp100_vmm_desc_16[4], NVKM_VMM_PAGE_Sxxx },
		{ 38, &gp100_vmm_desc_16[3], NVKM_VMM_PAGE_Sxxx },
		{ 29, &gp100_vmm_desc_16[2], NVKM_VMM_PAGE_Sxxx },
		{ 21, &gp100_vmm_desc_16[1], NVKM_VMM_PAGE_SVxC },
		{ 16, &gp100_vmm_desc_16[0], NVKM_VMM_PAGE_SVxC },
		{ 12, &gp100_vmm_desc_12[0], NVKM_VMM_PAGE_SVHx },
		{}
	}
};

int
gp100_vmm_new(struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	      struct lock_class_key *key, const char *name,
	      struct nvkm_vmm **pvmm)
{
	return nv04_vmm_new_(&gp100_vmm, mmu, 0, addr, size,
			     argv, argc, key, name, pvmm);
}
