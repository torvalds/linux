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

#include <subdev/fb.h>
#include <subdev/ltc.h>

#include <nvif/ifc00d.h>
#include <nvif/unpack.h>

int
gv100_vmm_join(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	u64 data[2], mask;
	int ret = gp100_vmm_join(vmm, inst), i;
	if (ret)
		return ret;

	nvkm_kmap(inst);
	data[0] = nvkm_ro32(inst, 0x200);
	data[1] = nvkm_ro32(inst, 0x204);
	mask = BIT_ULL(0);

	nvkm_wo32(inst, 0x21c, 0x00000000);

	for (i = 0; i < 64; i++) {
		if (mask & BIT_ULL(i)) {
			nvkm_wo32(inst, 0x2a4 + (i * 0x10), data[1]);
			nvkm_wo32(inst, 0x2a0 + (i * 0x10), data[0]);
		} else {
			nvkm_wo32(inst, 0x2a4 + (i * 0x10), 0x00000001);
			nvkm_wo32(inst, 0x2a0 + (i * 0x10), 0x00000001);
		}
		nvkm_wo32(inst, 0x2a8 + (i * 0x10), 0x00000000);
	}

	nvkm_wo32(inst, 0x298, lower_32_bits(mask));
	nvkm_wo32(inst, 0x29c, upper_32_bits(mask));
	nvkm_done(inst);
	return 0;
}

static const struct nvkm_vmm_func
gv100_vmm = {
	.join = gv100_vmm_join,
	.part = gf100_vmm_part,
	.aper = gf100_vmm_aper,
	.valid = gp100_vmm_valid,
	.flush = gp100_vmm_flush,
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
gv100_vmm_new(struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	      struct lock_class_key *key, const char *name,
	      struct nvkm_vmm **pvmm)
{
	return nv04_vmm_new_(&gv100_vmm, mmu, 0, addr, size,
			     argv, argc, key, name, pvmm);
}
