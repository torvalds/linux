/*
 * Copyright 2012 Red Hat Inc.
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
 *
 * Authors: Ben Skeggs
 */
#include "mem.h"
#include "vmm.h"

#include <core/option.h>

#include <nvif/class.h>

static void
nv44_mmu_init(struct nvkm_mmu *mmu)
{
	struct nvkm_device *device = mmu->subdev.device;
	struct nvkm_memory *pt = mmu->vmm->pd->pt[0]->memory;
	u32 addr;

	/* calculate vram address of this PRAMIN block, object must be
	 * allocated on 512KiB alignment, and not exceed a total size
	 * of 512KiB for this to work correctly
	 */
	addr  = nvkm_rd32(device, 0x10020c);
	addr -= ((nvkm_memory_addr(pt) >> 19) + 1) << 19;

	nvkm_wr32(device, 0x100850, 0x80000000);
	nvkm_wr32(device, 0x100818, mmu->vmm->null);
	nvkm_wr32(device, 0x100804, (nvkm_memory_size(pt) / 4) * 4096);
	nvkm_wr32(device, 0x100850, 0x00008000);
	nvkm_mask(device, 0x10008c, 0x00000200, 0x00000200);
	nvkm_wr32(device, 0x100820, 0x00000000);
	nvkm_wr32(device, 0x10082c, 0x00000001);
	nvkm_wr32(device, 0x100800, addr | 0x00000010);
}

static const struct nvkm_mmu_func
nv44_mmu = {
	.init = nv44_mmu_init,
	.dma_bits = 39,
	.mmu = {{ -1, -1, NVIF_CLASS_MMU_NV04}},
	.mem = {{ -1, -1, NVIF_CLASS_MEM_NV04}, nv04_mem_new, nv04_mem_map },
	.vmm = {{ -1, -1, NVIF_CLASS_VMM_NV04}, nv44_vmm_new, true },
};

int
nv44_mmu_new(struct nvkm_device *device, int index, struct nvkm_mmu **pmmu)
{
	if (device->type == NVKM_DEVICE_AGP ||
	    !nvkm_boolopt(device->cfgopt, "NvPCIE", true))
		return nv04_mmu_new(device, index, pmmu);

	return nvkm_mmu_new_(&nv44_mmu, device, index, pmmu);
}
