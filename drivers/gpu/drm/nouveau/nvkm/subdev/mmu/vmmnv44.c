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

static const struct nvkm_vmm_desc_func
nv44_vmm_desc_pgt = {
};

static const struct nvkm_vmm_desc
nv44_vmm_desc_12[] = {
	{ PGT, 17, 4, 0x80000, &nv44_vmm_desc_pgt },
	{}
};

static const struct nvkm_vmm_func
nv44_vmm = {
	.page = {
		{ 12, &nv44_vmm_desc_12[0], NVKM_VMM_PAGE_HOST },
		{}
	}
};

int
nv44_vmm_new(struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	     struct lock_class_key *key, const char *name,
	     struct nvkm_vmm **pvmm)
{
	struct nvkm_subdev *subdev = &mmu->subdev;
	struct nvkm_vmm *vmm;
	int ret;

	ret = nv04_vmm_new_(&nv44_vmm, mmu, 0, addr, size,
			    argv, argc, key, name, &vmm);
	*pvmm = vmm;
	if (ret)
		return ret;

	vmm->nullp = dma_alloc_coherent(subdev->device->dev, 16 * 1024,
					&vmm->null, GFP_KERNEL);
	if (!vmm->nullp) {
		nvkm_warn(subdev, "unable to allocate dummy pages\n");
		vmm->null = 0;
	}

	return 0;
}
