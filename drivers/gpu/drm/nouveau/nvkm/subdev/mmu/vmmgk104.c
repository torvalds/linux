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

void
gk104_vmm_lpt_invalid(struct nvkm_vmm *vmm,
		      struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	/* VALID_FALSE + PRIV tells the MMU to ignore corresponding SPTEs. */
	VMM_FO064(pt, vmm, ptei * 8, BIT_ULL(1) /* PRIV. */, ptes);
}

static const struct nvkm_vmm_desc_func
gk104_vmm_lpt = {
	.invalid = gk104_vmm_lpt_invalid,
	.unmap = gf100_vmm_pgt_unmap,
	.mem = gf100_vmm_pgt_mem,
};

const struct nvkm_vmm_desc
gk104_vmm_desc_17_12[] = {
	{ SPT, 15, 8, 0x1000, &gf100_vmm_pgt },
	{ PGD, 13, 8, 0x1000, &gf100_vmm_pgd },
	{}
};

const struct nvkm_vmm_desc
gk104_vmm_desc_17_17[] = {
	{ LPT, 10, 8, 0x1000, &gk104_vmm_lpt },
	{ PGD, 13, 8, 0x1000, &gf100_vmm_pgd },
	{}
};

const struct nvkm_vmm_desc
gk104_vmm_desc_16_12[] = {
	{ SPT, 14, 8, 0x1000, &gf100_vmm_pgt },
	{ PGD, 14, 8, 0x1000, &gf100_vmm_pgd },
	{}
};

const struct nvkm_vmm_desc
gk104_vmm_desc_16_16[] = {
	{ LPT, 10, 8, 0x1000, &gk104_vmm_lpt },
	{ PGD, 14, 8, 0x1000, &gf100_vmm_pgd },
	{}
};

static const struct nvkm_vmm_func
gk104_vmm_17 = {
	.join = gf100_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gf100_vmm_valid,
	.flush = gf100_vmm_flush,
	.page = {
		{ 17, &gk104_vmm_desc_17_17[0], NVKM_VMM_PAGE_xVxC },
		{ 12, &gk104_vmm_desc_17_12[0], NVKM_VMM_PAGE_xVHx },
		{}
	}
};

static const struct nvkm_vmm_func
gk104_vmm_16 = {
	.join = gf100_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gf100_vmm_valid,
	.flush = gf100_vmm_flush,
	.page = {
		{ 16, &gk104_vmm_desc_16_16[0], NVKM_VMM_PAGE_xVxC },
		{ 12, &gk104_vmm_desc_16_12[0], NVKM_VMM_PAGE_xVHx },
		{}
	}
};

int
gk104_vmm_new(struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	      struct lock_class_key *key, const char *name,
	      struct nvkm_vmm **pvmm)
{
	return gf100_vmm_new_(&gk104_vmm_16, &gk104_vmm_17, mmu, addr,
			      size, argv, argc, key, name, pvmm);
}
