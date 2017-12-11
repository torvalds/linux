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

#include <nvif/ifb00d.h>
#include <nvif/unpack.h>

static void
gm200_vmm_pgt_sparse(struct nvkm_vmm *vmm,
		     struct nvkm_mmu_pt *pt, u32 ptei, u32 ptes)
{
	/* VALID_FALSE + VOL tells the MMU to treat the PTE as sparse. */
	VMM_FO064(pt, vmm, ptei * 8, BIT_ULL(32) /* VOL. */, ptes);
}

static const struct nvkm_vmm_desc_func
gm200_vmm_spt = {
	.unmap = gf100_vmm_pgt_unmap,
	.sparse = gm200_vmm_pgt_sparse,
	.mem = gf100_vmm_pgt_mem,
	.dma = gf100_vmm_pgt_dma,
	.sgl = gf100_vmm_pgt_sgl,
};

static const struct nvkm_vmm_desc_func
gm200_vmm_lpt = {
	.invalid = gk104_vmm_lpt_invalid,
	.unmap = gf100_vmm_pgt_unmap,
	.sparse = gm200_vmm_pgt_sparse,
	.mem = gf100_vmm_pgt_mem,
};

static void
gm200_vmm_pgd_sparse(struct nvkm_vmm *vmm,
		     struct nvkm_mmu_pt *pt, u32 pdei, u32 pdes)
{
	/* VALID_FALSE + VOL_BIG tells the MMU to treat the PDE as sparse. */
	VMM_FO064(pt, vmm, pdei * 8, BIT_ULL(35) /* VOL_BIG. */, pdes);
}

static const struct nvkm_vmm_desc_func
gm200_vmm_pgd = {
	.unmap = gf100_vmm_pgt_unmap,
	.sparse = gm200_vmm_pgd_sparse,
	.pde = gf100_vmm_pgd_pde,
};

const struct nvkm_vmm_desc
gm200_vmm_desc_17_12[] = {
	{ SPT, 15, 8, 0x1000, &gm200_vmm_spt },
	{ PGD, 13, 8, 0x1000, &gm200_vmm_pgd },
	{}
};

const struct nvkm_vmm_desc
gm200_vmm_desc_17_17[] = {
	{ LPT, 10, 8, 0x1000, &gm200_vmm_lpt },
	{ PGD, 13, 8, 0x1000, &gm200_vmm_pgd },
	{}
};

const struct nvkm_vmm_desc
gm200_vmm_desc_16_12[] = {
	{ SPT, 14, 8, 0x1000, &gm200_vmm_spt },
	{ PGD, 14, 8, 0x1000, &gm200_vmm_pgd },
	{}
};

const struct nvkm_vmm_desc
gm200_vmm_desc_16_16[] = {
	{ LPT, 10, 8, 0x1000, &gm200_vmm_lpt },
	{ PGD, 14, 8, 0x1000, &gm200_vmm_pgd },
	{}
};

int
gm200_vmm_join_(struct nvkm_vmm *vmm, struct nvkm_memory *inst, u64 base)
{
	if (vmm->func->page[1].shift == 16)
		base |= BIT_ULL(11);
	return gf100_vmm_join_(vmm, inst, base);
}

int
gm200_vmm_join(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	return gm200_vmm_join_(vmm, inst, 0);
}

static const struct nvkm_vmm_func
gm200_vmm_17 = {
	.join = gm200_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gf100_vmm_valid,
	.flush = gf100_vmm_flush,
	.page = {
		{ 27, &gm200_vmm_desc_17_17[1], NVKM_VMM_PAGE_Sxxx },
		{ 17, &gm200_vmm_desc_17_17[0], NVKM_VMM_PAGE_SVxC },
		{ 12, &gm200_vmm_desc_17_12[0], NVKM_VMM_PAGE_SVHx },
		{}
	}
};

static const struct nvkm_vmm_func
gm200_vmm_16 = {
	.join = gm200_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gf100_vmm_valid,
	.flush = gf100_vmm_flush,
	.page = {
		{ 27, &gm200_vmm_desc_16_16[1], NVKM_VMM_PAGE_Sxxx },
		{ 16, &gm200_vmm_desc_16_16[0], NVKM_VMM_PAGE_SVxC },
		{ 12, &gm200_vmm_desc_16_12[0], NVKM_VMM_PAGE_SVHx },
		{}
	}
};

int
gm200_vmm_new_(const struct nvkm_vmm_func *func_16,
	       const struct nvkm_vmm_func *func_17,
	       struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	       struct lock_class_key *key, const char *name,
	       struct nvkm_vmm **pvmm)
{
	const struct nvkm_vmm_func *func;
	union {
		struct gm200_vmm_vn vn;
		struct gm200_vmm_v0 v0;
	} *args = argv;
	int ret = -ENOSYS;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		switch (args->v0.bigpage) {
		case 16: func = func_16; break;
		case 17: func = func_17; break;
		default:
			return -EINVAL;
		}
	} else
	if (!(ret = nvif_unvers(ret, &argv, &argc, args->vn))) {
		func = func_17;
	} else
		return ret;

	return nvkm_vmm_new_(func, mmu, 0, addr, size, key, name, pvmm);
}

int
gm200_vmm_new(struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	      struct lock_class_key *key, const char *name,
	      struct nvkm_vmm **pvmm)
{
	return gm200_vmm_new_(&gm200_vmm_16, &gm200_vmm_17, mmu, addr,
			      size, argv, argc, key, name, pvmm);
}

int
gm200_vmm_new_fixed(struct nvkm_mmu *mmu, u64 addr, u64 size,
		    void *argv, u32 argc, struct lock_class_key *key,
		    const char *name, struct nvkm_vmm **pvmm)
{
	return gf100_vmm_new_(&gm200_vmm_16, &gm200_vmm_17, mmu, addr,
			      size, argv, argc, key, name, pvmm);
}
