/*
 * Copyright 2018 Red Hat Inc.
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

#include <subdev/timer.h>

static void
tu102_vmm_flush(struct nvkm_vmm *vmm, int depth)
{
	struct nvkm_device *device = vmm->mmu->subdev.device;
	u32 type = (5 /* CACHE_LEVEL_UP_TO_PDE3 */ - depth) << 24;

	type |= 0x00000001; /* PAGE_ALL */
	if (atomic_read(&vmm->engref[NVKM_SUBDEV_BAR]))
		type |= 0x00000004; /* HUB_ONLY */

	mutex_lock(&vmm->mmu->mutex);

	nvkm_wr32(device, 0xb830a0, vmm->pd->pt[0]->addr >> 8);
	nvkm_wr32(device, 0xb830a4, 0x00000000);
	nvkm_wr32(device, 0x100e68, 0x00000000);
	nvkm_wr32(device, 0xb830b0, 0x80000000 | type);

	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0xb830b0) & 0x80000000))
			break;
	);

	mutex_unlock(&vmm->mmu->mutex);
}

static const struct nvkm_vmm_func
tu102_vmm = {
	.join = gv100_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gp100_vmm_valid,
	.flush = tu102_vmm_flush,
	.mthd = gp100_vmm_mthd,
	.page = {
		{ 47, &gp100_vmm_desc_16[4], NVKM_VMM_PAGE_Sxxx },
		{ 38, &gp100_vmm_desc_16[3], NVKM_VMM_PAGE_Sxxx },
		{ 29, &gp100_vmm_desc_16[2], NVKM_VMM_PAGE_Sxxx },
		{ 21, &gp100_vmm_desc_16[1], NVKM_VMM_PAGE_SVxC },
		{ 16, &gp100_vmm_desc_16[0], NVKM_VMM_PAGE_SVxC },
		{ 12, &gp100_vmm_desc_12[0], NVKM_VMM_PAGE_SVHx },
		{}
	}
};

int
tu102_vmm_new(struct nvkm_mmu *mmu, bool managed, u64 addr, u64 size,
	      void *argv, u32 argc, struct lock_class_key *key,
	      const char *name, struct nvkm_vmm **pvmm)
{
	return gp100_vmm_new_(&tu102_vmm, mmu, managed, addr, size,
			      argv, argc, key, name, pvmm);
}
