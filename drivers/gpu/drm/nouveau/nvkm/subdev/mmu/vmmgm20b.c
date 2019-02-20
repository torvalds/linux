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

static const struct nvkm_vmm_func
gm20b_vmm_17 = {
	.join = gm200_vmm_join,
	.part = gf100_vmm_part,
	.aper = gk20a_vmm_aper,
	.valid = gf100_vmm_valid,
	.flush = gf100_vmm_flush,
	.invalidate_pdb = gf100_vmm_invalidate_pdb,
	.page = {
		{ 27, &gm200_vmm_desc_17_17[1], NVKM_VMM_PAGE_Sxxx },
		{ 17, &gm200_vmm_desc_17_17[0], NVKM_VMM_PAGE_SxHC },
		{ 12, &gm200_vmm_desc_17_12[0], NVKM_VMM_PAGE_SxHx },
		{}
	}
};

static const struct nvkm_vmm_func
gm20b_vmm_16 = {
	.join = gm200_vmm_join,
	.part = gf100_vmm_part,
	.aper = gk20a_vmm_aper,
	.valid = gf100_vmm_valid,
	.flush = gf100_vmm_flush,
	.invalidate_pdb = gf100_vmm_invalidate_pdb,
	.page = {
		{ 27, &gm200_vmm_desc_16_16[1], NVKM_VMM_PAGE_Sxxx },
		{ 16, &gm200_vmm_desc_16_16[0], NVKM_VMM_PAGE_SxHC },
		{ 12, &gm200_vmm_desc_16_12[0], NVKM_VMM_PAGE_SxHx },
		{}
	}
};

int
gm20b_vmm_new(struct nvkm_mmu *mmu, bool managed, u64 addr, u64 size,
	      void *argv, u32 argc, struct lock_class_key *key,
	      const char *name, struct nvkm_vmm **pvmm)
{
	return gm200_vmm_new_(&gm20b_vmm_16, &gm20b_vmm_17, mmu, managed, addr,
			      size, argv, argc, key, name, pvmm);
}

int
gm20b_vmm_new_fixed(struct nvkm_mmu *mmu, bool managed, u64 addr, u64 size,
		    void *argv, u32 argc, struct lock_class_key *key,
		    const char *name, struct nvkm_vmm **pvmm)
{
	return gf100_vmm_new_(&gm20b_vmm_16, &gm20b_vmm_17, mmu, managed, addr,
			      size, argv, argc, key, name, pvmm);
}
