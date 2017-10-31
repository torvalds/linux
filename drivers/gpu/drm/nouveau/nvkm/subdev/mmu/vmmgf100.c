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

const struct nvkm_vmm_desc_func
gf100_vmm_pgt = {
};

const struct nvkm_vmm_desc_func
gf100_vmm_pgd = {
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
	.page = {
		{ 16, &gf100_vmm_desc_16_16[0], NVKM_VMM_PAGE_xVxC },
		{ 12, &gf100_vmm_desc_16_12[0], NVKM_VMM_PAGE_xVHx },
		{}
	}
};

int
gf100_vmm_new_(const struct nvkm_vmm_func *func_16,
	       const struct nvkm_vmm_func *func_17,
	       struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	       struct lock_class_key *key, const char *name,
	       struct nvkm_vmm **pvmm)
{
	switch (mmu->subdev.device->fb->page) {
	case 16: return nv04_vmm_new_(func_16, mmu, 0, addr, size,
				      argv, argc, key, name, pvmm);
	case 17: return nv04_vmm_new_(func_17, mmu, 0, addr, size,
				      argv, argc, key, name, pvmm);
	default:
		WARN_ON(1);
		return -EINVAL;
	}
}

int
gf100_vmm_new(struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	      struct lock_class_key *key, const char *name,
	      struct nvkm_vmm **pvmm)
{
	return gf100_vmm_new_(&gf100_vmm_16, &gf100_vmm_17, mmu, addr,
			      size, argv, argc, key, name, pvmm);
}
