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
#define NVKM_VMM_LEVELS_MAX 5
#include "vmm.h"

static void
nvkm_vmm_pt_del(struct nvkm_vmm_pt **ppgt)
{
	struct nvkm_vmm_pt *pgt = *ppgt;
	if (pgt) {
		kvfree(pgt->pde);
		kfree(pgt);
		*ppgt = NULL;
	}
}


static struct nvkm_vmm_pt *
nvkm_vmm_pt_new(const struct nvkm_vmm_desc *desc, bool sparse,
		const struct nvkm_vmm_page *page)
{
	const u32 pten = 1 << desc->bits;
	struct nvkm_vmm_pt *pgt;
	u32 lpte = 0;

	if (desc->type > PGT) {
		if (desc->type == SPT) {
			const struct nvkm_vmm_desc *pair = page[-1].desc;
			lpte = pten >> (desc->bits - pair->bits);
		} else {
			lpte = pten;
		}
	}

	if (!(pgt = kzalloc(sizeof(*pgt) + lpte, GFP_KERNEL)))
		return NULL;
	pgt->page = page ? page->shift : 0;
	pgt->sparse = sparse;

	if (desc->type == PGD) {
		pgt->pde = kvzalloc(sizeof(*pgt->pde) * pten, GFP_KERNEL);
		if (!pgt->pde) {
			kfree(pgt);
			return NULL;
		}
	}

	return pgt;
}

void
nvkm_vmm_dtor(struct nvkm_vmm *vmm)
{
	if (vmm->nullp) {
		dma_free_coherent(vmm->mmu->subdev.device->dev, 16 * 1024,
				  vmm->nullp, vmm->null);
	}

	if (vmm->pd) {
		nvkm_mmu_ptc_put(vmm->mmu, true, &vmm->pd->pt[0]);
		nvkm_vmm_pt_del(&vmm->pd);
	}
}

int
nvkm_vmm_ctor(const struct nvkm_vmm_func *func, struct nvkm_mmu *mmu,
	      u32 pd_header, u64 addr, u64 size, struct lock_class_key *key,
	      const char *name, struct nvkm_vmm *vmm)
{
	static struct lock_class_key _key;
	const struct nvkm_vmm_page *page = func->page;
	const struct nvkm_vmm_desc *desc;
	int levels, bits = 0;

	vmm->func = func;
	vmm->mmu = mmu;
	vmm->name = name;
	kref_init(&vmm->kref);

	__mutex_init(&vmm->mutex, "&vmm->mutex", key ? key : &_key);

	/* Locate the smallest page size supported by the backend, it will
	 * have the the deepest nesting of page tables.
	 */
	while (page[1].shift)
		page++;

	/* Locate the structure that describes the layout of the top-level
	 * page table, and determine the number of valid bits in a virtual
	 * address.
	 */
	for (levels = 0, desc = page->desc; desc->bits; desc++, levels++)
		bits += desc->bits;
	bits += page->shift;
	desc--;

	if (WARN_ON(levels > NVKM_VMM_LEVELS_MAX))
		return -EINVAL;

	vmm->start = addr;
	vmm->limit = size ? (addr + size) : (1ULL << bits);
	if (vmm->start > vmm->limit || vmm->limit > (1ULL << bits))
		return -EINVAL;

	/* Allocate top-level page table. */
	vmm->pd = nvkm_vmm_pt_new(desc, false, NULL);
	if (!vmm->pd)
		return -ENOMEM;
	vmm->pd->refs[0] = 1;
	INIT_LIST_HEAD(&vmm->join);

	/* ... and the GPU storage for it, except on Tesla-class GPUs that
	 * have the PD embedded in the instance structure.
	 */
	if (desc->size && mmu->func->vmm.global) {
		const u32 size = pd_header + desc->size * (1 << desc->bits);
		vmm->pd->pt[0] = nvkm_mmu_ptc_get(mmu, size, desc->align, true);
		if (!vmm->pd->pt[0])
			return -ENOMEM;
	}

	return 0;
}

int
nvkm_vmm_new_(const struct nvkm_vmm_func *func, struct nvkm_mmu *mmu,
	      u32 hdr, u64 addr, u64 size, struct lock_class_key *key,
	      const char *name, struct nvkm_vmm **pvmm)
{
	if (!(*pvmm = kzalloc(sizeof(**pvmm), GFP_KERNEL)))
		return -ENOMEM;
	return nvkm_vmm_ctor(func, mmu, hdr, addr, size, key, name, *pvmm);
}
