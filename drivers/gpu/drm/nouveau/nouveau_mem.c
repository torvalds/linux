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
#include "nouveau_mem.h"
#include "nouveau_drv.h"
#include "nouveau_bo.h"

#include <drm/ttm/ttm_bo_driver.h>

#include <nvif/class.h>
#include <nvif/if000a.h>
#include <nvif/if500b.h>
#include <nvif/if500d.h>
#include <nvif/if900b.h>
#include <nvif/if900d.h>

int
nouveau_mem_map(struct nouveau_mem *mem,
		struct nvif_vmm *vmm, struct nvif_vma *vma)
{
	union {
		struct nv50_vmm_map_v0 nv50;
		struct gf100_vmm_map_v0 gf100;
	} args;
	u32 argc = 0;

	switch (vmm->object.oclass) {
	case NVIF_CLASS_VMM_NV04:
		break;
	case NVIF_CLASS_VMM_NV50:
		args.nv50.version = 0;
		args.nv50.ro = 0;
		args.nv50.priv = 0;
		args.nv50.kind = mem->kind;
		args.nv50.comp = mem->comp;
		argc = sizeof(args.nv50);
		break;
	case NVIF_CLASS_VMM_GF100:
	case NVIF_CLASS_VMM_GM200:
	case NVIF_CLASS_VMM_GP100:
		args.gf100.version = 0;
		if (mem->mem.type & NVIF_MEM_VRAM)
			args.gf100.vol = 0;
		else
			args.gf100.vol = 1;
		args.gf100.ro = 0;
		args.gf100.priv = 0;
		args.gf100.kind = mem->kind;
		argc = sizeof(args.gf100);
		break;
	default:
		WARN_ON(1);
		return -ENOSYS;
	}

	return nvif_vmm_map(vmm, vma->addr, mem->mem.size, &args, argc, &mem->mem, 0);
}

void
nouveau_mem_fini(struct nouveau_mem *mem)
{
	nvif_vmm_put(&mem->cli->drm->client.vmm.vmm, &mem->vma[1]);
	nvif_vmm_put(&mem->cli->drm->client.vmm.vmm, &mem->vma[0]);
	mutex_lock(&mem->cli->drm->master.lock);
	nvif_mem_dtor(&mem->mem);
	mutex_unlock(&mem->cli->drm->master.lock);
}

int
nouveau_mem_host(struct ttm_resource *reg, struct ttm_tt *tt)
{
	struct nouveau_mem *mem = nouveau_mem(reg);
	struct nouveau_cli *cli = mem->cli;
	struct nouveau_drm *drm = cli->drm;
	struct nvif_mmu *mmu = &cli->mmu;
	struct nvif_mem_ram_v0 args = {};
	u8 type;
	int ret;

	if (!nouveau_drm_use_coherent_gpu_mapping(drm))
		type = drm->ttm.type_ncoh[!!mem->kind];
	else
		type = drm->ttm.type_host[0];

	if (mem->kind && !(mmu->type[type].type & NVIF_MEM_KIND))
		mem->comp = mem->kind = 0;
	if (mem->comp && !(mmu->type[type].type & NVIF_MEM_COMP)) {
		if (mmu->object.oclass >= NVIF_CLASS_MMU_GF100)
			mem->kind = mmu->kind[mem->kind];
		mem->comp = 0;
	}

	if (tt->sg)
		args.sgl = tt->sg->sgl;
	else
		args.dma = tt->dma_address;

	mutex_lock(&drm->master.lock);
	ret = nvif_mem_ctor_type(mmu, "ttmHostMem", cli->mem->oclass, type, PAGE_SHIFT,
				 reg->size,
				 &args, sizeof(args), &mem->mem);
	mutex_unlock(&drm->master.lock);
	return ret;
}

int
nouveau_mem_vram(struct ttm_resource *reg, bool contig, u8 page)
{
	struct nouveau_mem *mem = nouveau_mem(reg);
	struct nouveau_cli *cli = mem->cli;
	struct nouveau_drm *drm = cli->drm;
	struct nvif_mmu *mmu = &cli->mmu;
	u64 size = ALIGN(reg->size, 1 << page);
	int ret;

	mutex_lock(&drm->master.lock);
	switch (cli->mem->oclass) {
	case NVIF_CLASS_MEM_GF100:
		ret = nvif_mem_ctor_type(mmu, "ttmVram", cli->mem->oclass,
					 drm->ttm.type_vram, page, size,
					 &(struct gf100_mem_v0) {
						.contig = contig,
					 }, sizeof(struct gf100_mem_v0),
					 &mem->mem);
		break;
	case NVIF_CLASS_MEM_NV50:
		ret = nvif_mem_ctor_type(mmu, "ttmVram", cli->mem->oclass,
					 drm->ttm.type_vram, page, size,
					 &(struct nv50_mem_v0) {
						.bankswz = mmu->kind[mem->kind] == 2,
						.contig = contig,
					 }, sizeof(struct nv50_mem_v0),
					 &mem->mem);
		break;
	default:
		ret = -ENOSYS;
		WARN_ON(1);
		break;
	}
	mutex_unlock(&drm->master.lock);

	reg->start = mem->mem.addr >> PAGE_SHIFT;
	return ret;
}

void
nouveau_mem_del(struct ttm_resource_manager *man, struct ttm_resource *reg)
{
	struct nouveau_mem *mem = nouveau_mem(reg);

	nouveau_mem_fini(mem);
	ttm_resource_fini(man, reg);
	kfree(mem);
}

int
nouveau_mem_new(struct nouveau_cli *cli, u8 kind, u8 comp,
		struct ttm_resource **res)
{
	struct nouveau_mem *mem;

	if (!(mem = kzalloc(sizeof(*mem), GFP_KERNEL)))
		return -ENOMEM;

	mem->cli = cli;
	mem->kind = kind;
	mem->comp = comp;

	*res = &mem->base;
	return 0;
}

bool
nouveau_mem_intersects(struct ttm_resource *res,
		       const struct ttm_place *place,
		       size_t size)
{
	u32 num_pages = PFN_UP(size);

	/* Don't evict BOs outside of the requested placement range */
	if (place->fpfn >= (res->start + num_pages) ||
	    (place->lpfn && place->lpfn <= res->start))
		return false;

	return true;
}

bool
nouveau_mem_compatible(struct ttm_resource *res,
		       const struct ttm_place *place,
		       size_t size)
{
	u32 num_pages = PFN_UP(size);

	if (res->start < place->fpfn ||
	    (place->lpfn && (res->start + num_pages) > place->lpfn))
		return false;

	return true;
}
