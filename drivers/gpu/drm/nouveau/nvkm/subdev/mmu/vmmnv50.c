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
#include <subdev/timer.h>
#include <engine/gr.h>

#include <nvif/if500d.h>
#include <nvif/unpack.h>

static inline void
nv50_vmm_pgt_pte(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		 u32 ptei, u32 ptes, struct nvkm_vmm_map *map, u64 addr)
{
	u64 next = addr + map->type, data;
	u32 pten;
	int log2blk;

	map->type += ptes * map->ctag;

	while (ptes) {
		for (log2blk = 7; log2blk >= 0; log2blk--) {
			pten = 1 << log2blk;
			if (ptes >= pten && IS_ALIGNED(ptei, pten))
				break;
		}

		data  = next | (log2blk << 7);
		next += pten * map->next;
		ptes -= pten;

		while (pten--)
			VMM_WO064(pt, vmm, ptei++ * 8, data);
	}
}

static void
nv50_vmm_pgt_sgl(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		 u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_SGL(vmm, pt, ptei, ptes, map, nv50_vmm_pgt_pte);
}

static void
nv50_vmm_pgt_dma(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		 u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	if (map->page->shift == PAGE_SHIFT) {
		VMM_SPAM(vmm, "DMAA %08x %08x PTE(s)", ptei, ptes);
		nvkm_kmap(pt->memory);
		while (ptes--) {
			const u64 data = *map->dma++ + map->type;
			VMM_WO064(pt, vmm, ptei++ * 8, data);
			map->type += map->ctag;
		}
		nvkm_done(pt->memory);
		return;
	}

	VMM_MAP_ITER_DMA(vmm, pt, ptei, ptes, map, nv50_vmm_pgt_pte);
}

static void
nv50_vmm_pgt_mem(struct nvkm_vmm *vmm, struct nvkm_mmu_pt *pt,
		 u32 ptei, u32 ptes, struct nvkm_vmm_map *map)
{
	VMM_MAP_ITER_MEM(vmm, pt, ptei, ptes, map, nv50_vmm_pgt_pte);
}

static void
nv50_vmm_pgt_unmap(struct nvkm_vmm *vmm,
		   struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	VMM_FO064(pt, vmm, ptei * 8, 0ULL, ptes);
}

static const struct nvkm_vmm_desc_func
nv50_vmm_pgt = {
	.unmap = nv50_vmm_pgt_unmap,
	.mem = nv50_vmm_pgt_mem,
	.dma = nv50_vmm_pgt_dma,
	.sgl = nv50_vmm_pgt_sgl,
};

static bool
nv50_vmm_pde(struct nvkm_vmm *vmm, struct nvkm_vmm_pt *pgt, u64 *pdata)
{
	struct nvkm_mmu_pt *pt;
	u64 data = 0xdeadcafe00000000ULL;
	if (pgt && (pt = pgt->pt[0])) {
		switch (pgt->page) {
		case 16: data = 0x00000001; break;
		case 12: data = 0x00000003;
			switch (nvkm_memory_size(pt->memory)) {
			case 0x100000: data |= 0x00000000; break;
			case 0x040000: data |= 0x00000020; break;
			case 0x020000: data |= 0x00000040; break;
			case 0x010000: data |= 0x00000060; break;
			default:
				WARN_ON(1);
				return false;
			}
			break;
		default:
			WARN_ON(1);
			return false;
		}

		switch (nvkm_memory_target(pt->memory)) {
		case NVKM_MEM_TARGET_VRAM: data |= 0x00000000; break;
		case NVKM_MEM_TARGET_HOST: data |= 0x00000008; break;
		case NVKM_MEM_TARGET_NCOH: data |= 0x0000000c; break;
		default:
			WARN_ON(1);
			return false;
		}

		data |= pt->addr;
	}
	*pdata = data;
	return true;
}

static void
nv50_vmm_pgd_pde(struct nvkm_vmm *vmm, struct nvkm_vmm_pt *pgd, u32 pdei)
{
	struct nvkm_vmm_join *join;
	u32 pdeo = vmm->mmu->func->vmm.pd_offset + (pdei * 8);
	u64 data;

	if (!nv50_vmm_pde(vmm, pgd->pde[pdei], &data))
		return;

	list_for_each_entry(join, &vmm->join, head) {
		nvkm_kmap(join->inst);
		nvkm_wo64(join->inst, pdeo, data);
		nvkm_done(join->inst);
	}
}

static const struct nvkm_vmm_desc_func
nv50_vmm_pgd = {
	.pde = nv50_vmm_pgd_pde,
};

const struct nvkm_vmm_desc
nv50_vmm_desc_12[] = {
	{ PGT, 17, 8, 0x1000, &nv50_vmm_pgt },
	{ PGD, 11, 0, 0x0000, &nv50_vmm_pgd },
	{}
};

const struct nvkm_vmm_desc
nv50_vmm_desc_16[] = {
	{ PGT, 13, 8, 0x1000, &nv50_vmm_pgt },
	{ PGD, 11, 0, 0x0000, &nv50_vmm_pgd },
	{}
};

void
nv50_vmm_flush(struct nvkm_vmm *vmm, int level)
{
	struct nvkm_subdev *subdev = &vmm->mmu->subdev;
	struct nvkm_device *device = subdev->device;
	int i, id;

	mutex_lock(&vmm->mmu->mutex);
	for (i = 0; i < NVKM_SUBDEV_NR; i++) {
		if (!atomic_read(&vmm->engref[i]))
			continue;

		/* unfortunate hw bug workaround... */
		if (i == NVKM_ENGINE_GR && device->gr) {
			int ret = nvkm_gr_tlb_flush(device->gr);
			if (ret != -ENODEV)
				continue;
		}

		switch (i) {
		case NVKM_ENGINE_GR    : id = 0x00; break;
		case NVKM_ENGINE_VP    :
		case NVKM_ENGINE_MSPDEC: id = 0x01; break;
		case NVKM_SUBDEV_BAR   : id = 0x06; break;
		case NVKM_ENGINE_MSPPP :
		case NVKM_ENGINE_MPEG  : id = 0x08; break;
		case NVKM_ENGINE_BSP   :
		case NVKM_ENGINE_MSVLD : id = 0x09; break;
		case NVKM_ENGINE_CIPHER:
		case NVKM_ENGINE_SEC   : id = 0x0a; break;
		case NVKM_ENGINE_CE    : id = 0x0d; break;
		default:
			continue;
		}

		nvkm_wr32(device, 0x100c80, (id << 16) | 1);
		if (nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x100c80) & 0x00000001))
				break;
		) < 0)
			nvkm_error(subdev, "%s mmu invalidate timeout\n", nvkm_subdev_type[i]);
	}
	mutex_unlock(&vmm->mmu->mutex);
}

int
nv50_vmm_valid(struct nvkm_vmm *vmm, void *argv, u32 argc,
	       struct nvkm_vmm_map *map)
{
	const struct nvkm_vmm_page *page = map->page;
	union {
		struct nv50_vmm_map_vn vn;
		struct nv50_vmm_map_v0 v0;
	} *args = argv;
	struct nvkm_device *device = vmm->mmu->subdev.device;
	struct nvkm_ram *ram = device->fb->ram;
	struct nvkm_memory *memory = map->memory;
	u8  aper, kind, kind_inv, comp, priv, ro;
	int kindn, ret = -ENOSYS;
	const u8 *kindm;

	map->type = map->ctag = 0;
	map->next = 1 << page->shift;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		ro   = !!args->v0.ro;
		priv = !!args->v0.priv;
		kind = args->v0.kind & 0x7f;
		comp = args->v0.comp & 0x03;
	} else
	if (!(ret = nvif_unvers(ret, &argv, &argc, args->vn))) {
		ro   = 0;
		priv = 0;
		kind = 0x00;
		comp = 0;
	} else {
		VMM_DEBUG(vmm, "args");
		return ret;
	}

	switch (nvkm_memory_target(memory)) {
	case NVKM_MEM_TARGET_VRAM:
		if (ram->stolen) {
			map->type |= ram->stolen;
			aper = 3;
		} else {
			aper = 0;
		}
		break;
	case NVKM_MEM_TARGET_HOST:
		aper = 2;
		break;
	case NVKM_MEM_TARGET_NCOH:
		aper = 3;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	kindm = vmm->mmu->func->kind(vmm->mmu, &kindn, &kind_inv);
	if (kind >= kindn || kindm[kind] == kind_inv) {
		VMM_DEBUG(vmm, "kind %02x", kind);
		return -EINVAL;
	}

	if (map->mem && map->mem->type != kindm[kind]) {
		VMM_DEBUG(vmm, "kind %02x bankswz: %d %d", kind,
			  kindm[kind], map->mem->type);
		return -EINVAL;
	}

	if (comp) {
		u32 tags = (nvkm_memory_size(memory) >> 16) * comp;
		if (aper != 0 || !(page->type & NVKM_VMM_PAGE_COMP)) {
			VMM_DEBUG(vmm, "comp %d %02x", aper, page->type);
			return -EINVAL;
		}

		if (!map->no_comp) {
			ret = nvkm_memory_tags_get(memory, device, tags, NULL,
						   &map->tags);
			if (ret) {
				VMM_DEBUG(vmm, "comp %d", ret);
				return ret;
			}

			if (map->tags->mn) {
				u32 tags = map->tags->mn->offset +
					   (map->offset >> 16);
				map->ctag |= (u64)comp << 49;
				map->type |= (u64)comp << 47;
				map->type |= (u64)tags << 49;
				map->next |= map->ctag;
			}
		}
	}

	map->type |= BIT(0); /* Valid. */
	map->type |= (u64)ro << 3;
	map->type |= (u64)aper << 4;
	map->type |= (u64)priv << 6;
	map->type |= (u64)kind << 40;
	return 0;
}

void
nv50_vmm_part(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	struct nvkm_vmm_join *join;

	list_for_each_entry(join, &vmm->join, head) {
		if (join->inst == inst) {
			list_del(&join->head);
			kfree(join);
			break;
		}
	}
}

int
nv50_vmm_join(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	const u32 pd_offset = vmm->mmu->func->vmm.pd_offset;
	struct nvkm_vmm_join *join;
	int ret = 0;
	u64 data;
	u32 pdei;

	if (!(join = kmalloc(sizeof(*join), GFP_KERNEL)))
		return -ENOMEM;
	join->inst = inst;
	list_add_tail(&join->head, &vmm->join);

	nvkm_kmap(join->inst);
	for (pdei = vmm->start >> 29; pdei <= (vmm->limit - 1) >> 29; pdei++) {
		if (!nv50_vmm_pde(vmm, vmm->pd->pde[pdei], &data)) {
			ret = -EINVAL;
			break;
		}
		nvkm_wo64(join->inst, pd_offset + (pdei * 8), data);
	}
	nvkm_done(join->inst);
	return ret;
}

static const struct nvkm_vmm_func
nv50_vmm = {
	.join = nv50_vmm_join,
	.part = nv50_vmm_part,
	.valid = nv50_vmm_valid,
	.flush = nv50_vmm_flush,
	.page_block = 1 << 29,
	.page = {
		{ 16, &nv50_vmm_desc_16[0], NVKM_VMM_PAGE_xVxC },
		{ 12, &nv50_vmm_desc_12[0], NVKM_VMM_PAGE_xVHx },
		{}
	}
};

int
nv50_vmm_new(struct nvkm_mmu *mmu, bool managed, u64 addr, u64 size,
	     void *argv, u32 argc, struct lock_class_key *key, const char *name,
	     struct nvkm_vmm **pvmm)
{
	return nv04_vmm_new_(&nv50_vmm, mmu, 0, managed, addr, size,
			     argv, argc, key, name, pvmm);
}
