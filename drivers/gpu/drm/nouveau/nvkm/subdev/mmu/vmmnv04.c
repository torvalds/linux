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

#include <nvif/if000d.h>
#include <nvif/unpack.h>

static const struct nvkm_vmm_desc_func
nv04_vmm_desc_pgt = {
};

static const struct nvkm_vmm_desc
nv04_vmm_desc_12[] = {
	{ PGT, 15, 4, 0x1000, &nv04_vmm_desc_pgt },
	{}
};

static const struct nvkm_vmm_func
nv04_vmm = {
	.page = {
		{ 12, &nv04_vmm_desc_12[0], NVKM_VMM_PAGE_HOST },
		{}
	}
};

int
nv04_vmm_new_(const struct nvkm_vmm_func *func, struct nvkm_mmu *mmu,
	      u32 pd_header, u64 addr, u64 size, void *argv, u32 argc,
	      struct lock_class_key *key, const char *name,
	      struct nvkm_vmm **pvmm)
{
	union {
		struct nv04_vmm_vn vn;
	} *args = argv;
	int ret;

	ret = nvkm_vmm_new_(func, mmu, pd_header, addr, size, key, name, pvmm);
	if (ret)
		return ret;

	return nvif_unvers(-ENOSYS, &argv, &argc, args->vn);
}

int
nv04_vmm_new(struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	     struct lock_class_key *key, const char *name,
	     struct nvkm_vmm **pvmm)
{
	struct nvkm_vmm *vmm;
	struct nvkm_memory *mem;
	int ret;

	ret = nv04_vmm_new_(&nv04_vmm, mmu, 8, addr, size,
			    argv, argc, key, name, &vmm);
	*pvmm = vmm;
	if (ret)
		return ret;

	mem = vmm->pd->pt[0]->memory;
	nvkm_kmap(mem);
	nvkm_wo32(mem, 0x00000, 0x0002103d); /* PCI, RW, PT, !LN */
	nvkm_wo32(mem, 0x00004, vmm->limit - 1);
	nvkm_done(mem);
	return 0;
}
