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
#include <subdev/timer.h>

#include <nvif/if900d.h>
#include <nvif/unpack.h>

static inline void
gf100_vmm_pgt_pte(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map, u64 addr)
{
	u64 base = (addr >> 8) | map->type;
	u64 data = base;

	if (map->ctag && !(map->next & (1ULL << 44))) {
		while (ptes--) {
			data = base | ((map->ctag >> 1) << 44);
			if (!(map->ctag++ & 1))
				data |= BIT_ULL(60);

			VMM_WO064(pt, vmm, ptei++ * 8, data);
			base += map->next;
		}
	} else {
		map->type += ptes * map->ctag;

		while (ptes--) {
			VMM_WO064(pt, vmm, ptei++ * 8, data);
			data += map->next;
		}
	}
}

void
gf100_vmm_pgt_sgl(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_SGL(vmm, pt, ptei, ptes, map, gf100_vmm_pgt_pte);
}

void
gf100_vmm_pgt_dma(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	if (map->page->shift == PAGE_SHIFT) {
		VMM_SPAM(vmm, "DMAA %08x %08x PTE(s)", ptei, ptes);
		nvkm_kmap(pt->memory);
		while (ptes--) {
			const u64 data = (*map->dma++ >> 8) | map->type;
			VMM_WO064(pt, vmm, ptei++ * 8, data);
			map->type += map->ctag;
		}
		nvkm_done(pt->memory);
		return;
	}

	VMM_MAP_ITER_DMA(vmm, pt, ptei, ptes, map, gf100_vmm_pgt_pte);
}

void
gf100_vmm_pgt_mem(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		  u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_MEM(vmm, pt, ptei, ptes, map, gf100_vmm_pgt_pte);
}

void
gf100_vmm_pgt_unmap(struct nvkm_vmm *vmm,
		    struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	VMM_FO064(pt, vmm, ptei * 8, 0ULL, ptes);
}

const struct nvkm_vmm_desc_func
gf100_vmm_pgt = {
	.unmap = gf100_vmm_pgt_unmap,
	.mem = gf100_vmm_pgt_mem,
	.dma = gf100_vmm_pgt_dma,
	.sgl = gf100_vmm_pgt_sgl,
};

void
gf100_vmm_pgd_pde(struct nvkm_vmm *vmm, struct nvkm_vmm_pt *pgd, u32 pdei)
{
	struct nvkm_vmm_pt *pgt = pgd->pde[pdei];
	struct nvkm_mmu_pt *pd = pgd->pt[0];
	struct nvkm_mmu_pt *pt;
	u64 data = 0;

	if ((pt = pgt->pt[0])) {
		switch (nvkm_memory_target(pt->memory)) {
		case NVKM_MEM_TARGET_VRAM: data |= 1ULL << 0; break;
		case NVKM_MEM_TARGET_HOST: data |= 2ULL << 0;
			data |= BIT_ULL(35); /* VOL */
			break;
		case NVKM_MEM_TARGET_NCOH: data |= 3ULL << 0; break;
		default:
			WARN_ON(1);
			return;
		}
		data |= pt->addr >> 8;
	}

	if ((pt = pgt->pt[1])) {
		switch (nvkm_memory_target(pt->memory)) {
		case NVKM_MEM_TARGET_VRAM: data |= 1ULL << 32; break;
		case NVKM_MEM_TARGET_HOST: data |= 2ULL << 32;
			data |= BIT_ULL(34); /* VOL */
			break;
		case NVKM_MEM_TARGET_NCOH: data |= 3ULL << 32; break;
		default:
			WARN_ON(1);
			return;
		}
		data |= pt->addr << 24;
	}

	nvkm_kmap(pd->memory);
	VMM_WO064(pd, vmm, pdei * 8, data);
	nvkm_done(pd->memory);
}

const struct nvkm_vmm_desc_func
gf100_vmm_pgd = {
	.unmap = gf100_vmm_pgt_unmap,
	.pde = gf100_vmm_pgd_pde,
};

static const struct nvkm_vmm_desc
gf100_vmm_desc_17_12[] = {
	{ SPT, 15, 8, 0x1000, &gf100_vmm_pgt },
	{ PGD, 13, 8, 0x1000, &gf100_vmm_pgd },
	{}
};

static const struct nvkm_vmm_desc
gf100_vmm_desc_17_17[] = {
	{ LPT, 10, 8, 0x1000, &gf100_vmm_pgt },
	{ PGD, 13, 8, 0x1000, &gf100_vmm_pgd },
	{}
};

static const struct nvkm_vmm_desc
gf100_vmm_desc_16_12[] = {
	{ SPT, 14, 8, 0x1000, &gf100_vmm_pgt },
	{ PGD, 14, 8, 0x1000, &gf100_vmm_pgd },
	{}
};

static const struct nvkm_vmm_desc
gf100_vmm_desc_16_16[] = {
	{ LPT, 10, 8, 0x1000, &gf100_vmm_pgt },
	{ PGD, 14, 8, 0x1000, &gf100_vmm_pgd },
	{}
};

void
gf100_vmm_invalidate_pdb(struct nvkm_vmm *vmm, u64 addr)
{
	struct nvkm_device *device = vmm->mmu->subdev.device;
	nvkm_wr32(device, 0x100cb8, addr);
}

void
gf100_vmm_invalidate(struct nvkm_vmm *vmm, u32 type)
{
	struct nvkm_device *device = vmm->mmu->subdev.device;
	struct nvkm_mmu_pt *pd = vmm->pd->pt[0];
	u64 addr = 0;

	mutex_lock(&vmm->mmu->mutex);
	/* Looks like maybe a "free flush slots" counter, the
	 * faster you write to 0x100cbc to more it decreases.
	 */
	nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x100c80) & 0x00ff0000)
			break;
	);

	if (!(type & 0x00000002) /* ALL_PDB. */) {
		switch (nvkm_memory_target(pd->memory)) {
		case NVKM_MEM_TARGET_VRAM: addr |= 0x00000000; break;
		case NVKM_MEM_TARGET_HOST: addr |= 0x00000002; break;
		case NVKM_MEM_TARGET_NCOH: addr |= 0x00000003; break;
		default:
			WARN_ON(1);
			break;
		}
		addr |= (vmm->pd->pt[0]->addr >> 12) << 4;

		vmm->func->invalidate_pdb(vmm, addr);
	}

	nvkm_wr32(device, 0x100cbc, 0x80000000 | type);

	/* Wait for flush to be queued? */
	nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x100c80) & 0x00008000)
			break;
	);
	mutex_unlock(&vmm->mmu->mutex);
}

void
gf100_vmm_flush(struct nvkm_vmm *vmm, int depth)
{
	u32 type = 0x00000001; /* PAGE_ALL */
	if (atomic_read(&vmm->engref[NVKM_SUBDEV_BAR]))
		type |= 0x00000004; /* HUB_ONLY */
	gf100_vmm_invalidate(vmm, type);
}

int
gf100_vmm_valid(struct nvkm_vmm *vmm, void *argv, u32 argc,
		struct nvkm_vmm_map *map)
{
	const enum nvkm_memory_target target = nvkm_memory_target(map->memory);
	const struct nvkm_vmm_page *page = map->page;
	const bool gm20x = page->desc->func->sparse != NULL;
	union {
		struct gf100_vmm_map_vn vn;
		struct gf100_vmm_map_v0 v0;
	} *args = argv;
	struct nvkm_device *device = vmm->mmu->subdev.device;
	struct nvkm_memory *memory = map->memory;
	u8  kind, kind_inv, priv, ro, vol;
	int kindn, aper, ret = -ENOSYS;
	const u8 *kindm;

	map->next = (1 << page->shift) >> 8;
	map->type = map->ctag = 0;

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
		u32 comp = (page->shift == 16 && !gm20x) ? 16 : 17;
		u32 tags = ALIGN(nvkm_memory_size(memory), 1 << 17) >> comp;
		if (aper != 0 || !(page->type & NVKM_VMM_PAGE_COMP)) {
			VMM_DEBUG(vmm, "comp %d %02x", aper, page->type);
			return -EINVAL;
		}

		if (!map->no_comp) {
			ret = nvkm_memory_tags_get(memory, device, tags,
						   nvkm_ltc_tags_clear,
						   &map->tags);
			if (ret) {
				VMM_DEBUG(vmm, "comp %d", ret);
				return ret;
			}
		}

		if (!map->no_comp && map->tags->mn) {
			u64 tags = map->tags->mn->offset + (map->offset >> 17);
			if (page->shift == 17 || !gm20x) {
				map->type |= tags << 44;
				map->ctag |= 1ULL << 44;
				map->next |= 1ULL << 44;
			} else {
				map->ctag |= tags << 1 | 1;
			}
		} else {
			kind = kindm[kind];
		}
	}

	map->type |= BIT(0);
	map->type |= (u64)priv << 1;
	map->type |= (u64)  ro << 2;
	map->type |= (u64) vol << 32;
	map->type |= (u64)aper << 33;
	map->type |= (u64)kind << 36;
	return 0;
}

int
gf100_vmm_aper(enum nvkm_memory_target target)
{
	switch (target) {
	case NVKM_MEM_TARGET_VRAM: return 0;
	case NVKM_MEM_TARGET_HOST: return 2;
	case NVKM_MEM_TARGET_NCOH: return 3;
	default:
		return -EINVAL;
	}
}

void
gf100_vmm_part(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	nvkm_fo64(inst, 0x0200, 0x00000000, 2);
}

int
gf100_vmm_join_(struct nvkm_vmm *vmm, struct nvkm_memory *inst, u64 base)
{
	struct nvkm_mmu_pt *pd = vmm->pd->pt[0];

	switch (nvkm_memory_target(pd->memory)) {
	case NVKM_MEM_TARGET_VRAM: base |= 0ULL << 0; break;
	case NVKM_MEM_TARGET_HOST: base |= 2ULL << 0;
		base |= BIT_ULL(2) /* VOL. */;
		break;
	case NVKM_MEM_TARGET_NCOH: base |= 3ULL << 0; break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}
	base |= pd->addr;

	nvkm_kmap(inst);
	nvkm_wo64(inst, 0x0200, base);
	nvkm_wo64(inst, 0x0208, vmm->limit - 1);
	nvkm_done(inst);
	return 0;
}

int
gf100_vmm_join(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	return gf100_vmm_join_(vmm, inst, 0);
}

static const struct nvkm_vmm_func
gf100_vmm_17 = {
	.join = gf100_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gf100_vmm_valid,
	.flush = gf100_vmm_flush,
	.invalidate_pdb = gf100_vmm_invalidate_pdb,
	.page = {
		{ 17, &gf100_vmm_desc_17_17[0], NVKM_VMM_PAGE_xVxC },
		{ 12, &gf100_vmm_desc_17_12[0], NVKM_VMM_PAGE_xVHx },
		{}
	}
};

static const struct nvkm_vmm_func
gf100_vmm_16 = {
	.join = gf100_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gf100_vmm_valid,
	.flush = gf100_vmm_flush,
	.invalidate_pdb = gf100_vmm_invalidate_pdb,
	.page = {
		{ 16, &gf100_vmm_desc_16_16[0], NVKM_VMM_PAGE_xVxC },
		{ 12, &gf100_vmm_desc_16_12[0], NVKM_VMM_PAGE_xVHx },
		{}
	}
};

int
gf100_vmm_new_(const struct nvkm_vmm_func *func_16,
	       const struct nvkm_vmm_func *func_17,
	       struct nvkm_mmu *mmu, bool managed, u64 addr, u64 size,
	       void *argv, u32 argc, struct lock_class_key *key,
	       const char *name, struct nvkm_vmm **pvmm)
{
	switch (mmu->subdev.device->fb->page) {
	case 16: return nv04_vmm_new_(func_16, mmu, 0, managed, addr, size,
				      argv, argc, key, name, pvmm);
	case 17: return nv04_vmm_new_(func_17, mmu, 0, managed, addr, size,
				      argv, argc, key, name, pvmm);
	default:
		WARN_ON(1);
		return -EINVAL;
	}
}

int
gf100_vmm_new(struct nvkm_mmu *mmu, bool managed, u64 addr, u64 size,
	      void *argv, u32 argc, struct lock_class_key *key,
	      const char *name, struct nvkm_vmm **pvmm)
{
	return gf100_vmm_new_(&gf100_vmm_16, &gf100_vmm_17, mmu, managed, addr,
			      size, argv, argc, key, name, pvmm);
}
