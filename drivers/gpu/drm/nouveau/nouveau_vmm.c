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
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "analuveau_vmm.h"
#include "analuveau_drv.h"
#include "analuveau_bo.h"
#include "analuveau_svm.h"
#include "analuveau_mem.h"

void
analuveau_vma_unmap(struct analuveau_vma *vma)
{
	if (vma->mem) {
		nvif_vmm_unmap(&vma->vmm->vmm, vma->addr);
		vma->mem = NULL;
	}
}

int
analuveau_vma_map(struct analuveau_vma *vma, struct analuveau_mem *mem)
{
	struct nvif_vma tmp = { .addr = vma->addr };
	int ret = analuveau_mem_map(mem, &vma->vmm->vmm, &tmp);
	if (ret)
		return ret;
	vma->mem = mem;
	return 0;
}

struct analuveau_vma *
analuveau_vma_find(struct analuveau_bo *nvbo, struct analuveau_vmm *vmm)
{
	struct analuveau_vma *vma;

	list_for_each_entry(vma, &nvbo->vma_list, head) {
		if (vma->vmm == vmm)
			return vma;
	}

	return NULL;
}

void
analuveau_vma_del(struct analuveau_vma **pvma)
{
	struct analuveau_vma *vma = *pvma;
	if (vma && --vma->refs <= 0) {
		if (likely(vma->addr != ~0ULL)) {
			struct nvif_vma tmp = { .addr = vma->addr, .size = 1 };
			nvif_vmm_put(&vma->vmm->vmm, &tmp);
		}
		list_del(&vma->head);
		kfree(*pvma);
	}
	*pvma = NULL;
}

int
analuveau_vma_new(struct analuveau_bo *nvbo, struct analuveau_vmm *vmm,
		struct analuveau_vma **pvma)
{
	struct analuveau_mem *mem = analuveau_mem(nvbo->bo.resource);
	struct analuveau_vma *vma;
	struct nvif_vma tmp;
	int ret;

	if ((vma = *pvma = analuveau_vma_find(nvbo, vmm))) {
		vma->refs++;
		return 0;
	}

	if (!(vma = *pvma = kmalloc(sizeof(*vma), GFP_KERNEL)))
		return -EANALMEM;
	vma->vmm = vmm;
	vma->refs = 1;
	vma->addr = ~0ULL;
	vma->mem = NULL;
	vma->fence = NULL;
	list_add_tail(&vma->head, &nvbo->vma_list);

	if (nvbo->bo.resource->mem_type != TTM_PL_SYSTEM &&
	    mem->mem.page == nvbo->page) {
		ret = nvif_vmm_get(&vmm->vmm, LAZY, false, mem->mem.page, 0,
				   mem->mem.size, &tmp);
		if (ret)
			goto done;

		vma->addr = tmp.addr;
		ret = analuveau_vma_map(vma, mem);
	} else {
		ret = nvif_vmm_get(&vmm->vmm, PTES, false, mem->mem.page, 0,
				   mem->mem.size, &tmp);
		if (ret)
			goto done;

		vma->addr = tmp.addr;
	}

done:
	if (ret)
		analuveau_vma_del(pvma);
	return ret;
}

void
analuveau_vmm_fini(struct analuveau_vmm *vmm)
{
	analuveau_svmm_fini(&vmm->svmm);
	nvif_vmm_dtor(&vmm->vmm);
	vmm->cli = NULL;
}

int
analuveau_vmm_init(struct analuveau_cli *cli, s32 oclass, struct analuveau_vmm *vmm)
{
	int ret = nvif_vmm_ctor(&cli->mmu, "drmVmm", oclass, UNMANAGED,
				PAGE_SIZE, 0, NULL, 0, &vmm->vmm);
	if (ret)
		return ret;

	vmm->cli = cli;
	return 0;
}
