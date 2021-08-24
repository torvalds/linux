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

#include <core/client.h>
#include <subdev/fb.h>
#include <subdev/ltc.h>
#include <subdev/timer.h>
#include <engine/gr.h>

#include <nvif/ifc00d.h>
#include <nvif/unpack.h>

static void
gp100_vmm_pfn_unmap(struct nvkm_vmm *vmm,
		    struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	struct device *dev = vmm->mmu->subdev.device->dev;
	dma_addr_t addr;

	nvkm_kmap(pt->memory);
	while (ptes--) {
		u32 datalo = nvkm_ro32(pt->memory, pt->base + ptei * 8 + 0);
		u32 datahi = nvkm_ro32(pt->memory, pt->base + ptei * 8 + 4);
		u64 data   = (u64)datahi << 32 | datalo;
		if ((data & (3ULL << 1)) != 0) {
			addr = (data >> 8) << 12;
			dma_unmap_page(dev, addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
		}
		ptei++;
	}
	nvkm_done(pt->memory);
}

static bool
gp100_vmm_pfn_clear(struct nvkm_vmm *vmm,
		    struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	bool dma = false;
	nvkm_kmap(pt->memory);
	while (ptes--) {
		u32 datalo = nvkm_ro32(pt->memory, pt->base + ptei * 8 + 0);
		u32 datahi = nvkm_ro32(pt->memory, pt->base + ptei * 8 + 4);
		u64 data   = (u64)datahi << 32 | datalo;
		if ((data & BIT_ULL(0)) && (data & (3ULL << 1)) != 0) {
			VMM_WO064(pt, vmm, ptei * 8, data & ~BIT_ULL(0));
			dma = true;
		}
		ptei++;
	}
	nvkm_done(pt->memory);
	return dma;
}

static void
gp100_vmm_pgt_pfn(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	struct device *dev = vmm->mmu->subdev.device->dev;
	dma_addr_t addr;

	nvkm_kmap(pt->memory);
	for (; ptes; ptes--, map->pfn++) {
		u64 data = 0;

		if (!(*map->pfn & NVKM_VMM_PFN_V))
			continue;

		if (!(*map->pfn & NVKM_VMM_PFN_W))
			data |= BIT_ULL(6); /* RO. */

		if (!(*map->pfn & NVKM_VMM_PFN_A))
			data |= BIT_ULL(7); /* Atomic disable. */

		if (!(*map->pfn & NVKM_VMM_PFN_VRAM)) {
			addr = *map->pfn >> NVKM_VMM_PFN_ADDR_SHIFT;
			addr = dma_map_page(dev, pfn_to_page(addr), 0,
					    PAGE_SIZE, DMA_BIDIRECTIONAL);
			if (!WARN_ON(dma_mapping_error(dev, addr))) {
				data |= addr >> 4;
				data |= 2ULL << 1; /* SYSTEM_COHERENT_MEMORY. */
				data |= BIT_ULL(3); /* VOL. */
				data |= BIT_ULL(0); /* VALID. */
			}
		} else {
			data |= (*map->pfn & NVKM_VMM_PFN_ADDR) >> 4;
			data |= BIT_ULL(0); /* VALID. */
		}

		VMM_WO064(pt, vmm, ptei++ * 8, data);
	}
	nvkm_done(pt->memory);
}

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
	.pfn = gp100_vmm_pgt_pfn,
	.pfn_clear = gp100_vmm_pfn_clear,
	.pfn_unmap = gp100_vmm_pfn_unmap,
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

static void
gp100_vmm_pd0_pfn_unmap(struct nvkm_vmm *vmm,
			struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	struct device *dev = vmm->mmu->subdev.device->dev;
	dma_addr_t addr;

	nvkm_kmap(pt->memory);
	while (ptes--) {
		u32 datalo = nvkm_ro32(pt->memory, pt->base + ptei * 16 + 0);
		u32 datahi = nvkm_ro32(pt->memory, pt->base + ptei * 16 + 4);
		u64 data   = (u64)datahi << 32 | datalo;

		if ((data & (3ULL << 1)) != 0) {
			addr = (data >> 8) << 12;
			dma_unmap_page(dev, addr, 1UL << 21, DMA_BIDIRECTIONAL);
		}
		ptei++;
	}
	nvkm_done(pt->memory);
}

static bool
gp100_vmm_pd0_pfn_clear(struct nvkm_vmm *vmm,
			struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	bool dma = false;

	nvkm_kmap(pt->memory);
	while (ptes--) {
		u32 datalo = nvkm_ro32(pt->memory, pt->base + ptei * 16 + 0);
		u32 datahi = nvkm_ro32(pt->memory, pt->base + ptei * 16 + 4);
		u64 data   = (u64)datahi << 32 | datalo;

		if ((data & BIT_ULL(0)) && (data & (3ULL << 1)) != 0) {
			VMM_WO064(pt, vmm, ptei * 16, data & ~BIT_ULL(0));
			dma = true;
		}
		ptei++;
	}
	nvkm_done(pt->memory);
	return dma;
}

static void
gp100_vmm_pd0_pfn(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	struct device *dev = vmm->mmu->subdev.device->dev;
	dma_addr_t addr;

	nvkm_kmap(pt->memory);
	for (; ptes; ptes--, map->pfn++) {
		u64 data = 0;

		if (!(*map->pfn & NVKM_VMM_PFN_V))
			continue;

		if (!(*map->pfn & NVKM_VMM_PFN_W))
			data |= BIT_ULL(6); /* RO. */

		if (!(*map->pfn & NVKM_VMM_PFN_A))
			data |= BIT_ULL(7); /* Atomic disable. */

		if (!(*map->pfn & NVKM_VMM_PFN_VRAM)) {
			addr = *map->pfn >> NVKM_VMM_PFN_ADDR_SHIFT;
			addr = dma_map_page(dev, pfn_to_page(addr), 0,
					    1UL << 21, DMA_BIDIRECTIONAL);
			if (!WARN_ON(dma_mapping_error(dev, addr))) {
				data |= addr >> 4;
				data |= 2ULL << 1; /* SYSTEM_COHERENT_MEMORY. */
				data |= BIT_ULL(3); /* VOL. */
				data |= BIT_ULL(0); /* VALID. */
			}
		} else {
			data |= (*map->pfn & NVKM_VMM_PFN_ADDR) >> 4;
			data |= BIT_ULL(0); /* VALID. */
		}

		VMM_WO064(pt, vmm, ptei++ * 16, data);
	}
	nvkm_done(pt->memory);
}

static const struct nvkm_vmm_desc_func
gp100_vmm_desc_pd0 = {
	.unmap = gp100_vmm_pd0_unmap,
	.sparse = gp100_vmm_pd0_sparse,
	.pde = gp100_vmm_pd0_pde,
	.mem = gp100_vmm_pd0_mem,
	.pfn = gp100_vmm_pd0_pfn,
	.pfn_clear = gp100_vmm_pd0_pfn_clear,
	.pfn_unmap = gp100_vmm_pd0_pfn_unmap,
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
	u8  kind, kind_inv, priv, ro, vol;
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

	kindm = vmm->mmu->func->kind(vmm->mmu, &kindn, &kind_inv);
	if (kind >= kindn || kindm[kind] == kind_inv) {
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

static int
gp100_vmm_fault_cancel(struct nvkm_vmm *vmm, void *argv, u32 argc)
{
	struct nvkm_device *device = vmm->mmu->subdev.device;
	union {
		struct gp100_vmm_fault_cancel_v0 v0;
	} *args = argv;
	int ret = -ENOSYS;
	u32 inst, aper;

	if ((ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false)))
		return ret;

	/* Translate MaxwellFaultBufferA instance pointer to the same
	 * format as the NV_GR_FECS_CURRENT_CTX register.
	 */
	aper = (args->v0.inst >> 8) & 3;
	args->v0.inst >>= 12;
	args->v0.inst |= aper << 28;
	args->v0.inst |= 0x80000000;

	if (!WARN_ON(nvkm_gr_ctxsw_pause(device))) {
		if ((inst = nvkm_gr_ctxsw_inst(device)) == args->v0.inst) {
			gf100_vmm_invalidate(vmm, 0x0000001b
					     /* CANCEL_TARGETED. */ |
					     (args->v0.hub    << 20) |
					     (args->v0.gpc    << 15) |
					     (args->v0.client << 9));
		}
		WARN_ON(nvkm_gr_ctxsw_resume(device));
	}

	return 0;
}

static int
gp100_vmm_fault_replay(struct nvkm_vmm *vmm, void *argv, u32 argc)
{
	union {
		struct gp100_vmm_fault_replay_vn vn;
	} *args = argv;
	int ret = -ENOSYS;

	if (!(ret = nvif_unvers(ret, &argv, &argc, args->vn))) {
		gf100_vmm_invalidate(vmm, 0x0000000b); /* REPLAY_GLOBAL. */
	}

	return ret;
}

int
gp100_vmm_mthd(struct nvkm_vmm *vmm,
	       struct nvkm_client *client, u32 mthd, void *argv, u32 argc)
{
	switch (mthd) {
	case GP100_VMM_VN_FAULT_REPLAY:
		return gp100_vmm_fault_replay(vmm, argv, argc);
	case GP100_VMM_VN_FAULT_CANCEL:
		return gp100_vmm_fault_cancel(vmm, argv, argc);
	default:
		break;
	}
	return -EINVAL;
}

void
gp100_vmm_invalidate_pdb(struct nvkm_vmm *vmm, u64 addr)
{
	struct nvkm_device *device = vmm->mmu->subdev.device;
	nvkm_wr32(device, 0x100cb8, lower_32_bits(addr));
	nvkm_wr32(device, 0x100cec, upper_32_bits(addr));
}

void
gp100_vmm_flush(struct nvkm_vmm *vmm, int depth)
{
	u32 type = (5 /* CACHE_LEVEL_UP_TO_PDE3 */ - depth) << 24;
	if (atomic_read(&vmm->engref[NVKM_SUBDEV_BAR]))
		type |= 0x00000004; /* HUB_ONLY */
	type |= 0x00000001; /* PAGE_ALL */
	gf100_vmm_invalidate(vmm, type);
}

int
gp100_vmm_join(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	u64 base = BIT_ULL(10) /* VER2 */ | BIT_ULL(11) /* 64KiB */;
	if (vmm->replay) {
		base |= BIT_ULL(4); /* FAULT_REPLAY_TEX */
		base |= BIT_ULL(5); /* FAULT_REPLAY_GCC */
	}
	return gf100_vmm_join_(vmm, inst, base);
}

static const struct nvkm_vmm_func
gp100_vmm = {
	.join = gp100_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gp100_vmm_valid,
	.flush = gp100_vmm_flush,
	.mthd = gp100_vmm_mthd,
	.invalidate_pdb = gp100_vmm_invalidate_pdb,
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
gp100_vmm_new_(const struct nvkm_vmm_func *func,
	       struct nvkm_mmu *mmu, bool managed, u64 addr, u64 size,
	       void *argv, u32 argc, struct lock_class_key *key,
	       const char *name, struct nvkm_vmm **pvmm)
{
	union {
		struct gp100_vmm_vn vn;
		struct gp100_vmm_v0 v0;
	} *args = argv;
	int ret = -ENOSYS;
	bool replay;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		replay = args->v0.fault_replay != 0;
	} else
	if (!(ret = nvif_unvers(ret, &argv, &argc, args->vn))) {
		replay = false;
	} else
		return ret;

	ret = nvkm_vmm_new_(func, mmu, 0, managed, addr, size, key, name, pvmm);
	if (ret)
		return ret;

	(*pvmm)->replay = replay;
	return 0;
}

int
gp100_vmm_new(struct nvkm_mmu *mmu, bool managed, u64 addr, u64 size,
	      void *argv, u32 argc, struct lock_class_key *key,
	      const char *name, struct nvkm_vmm **pvmm)
{
	return gp100_vmm_new_(&gp100_vmm, mmu, managed, addr, size,
			      argv, argc, key, name, pvmm);
}
