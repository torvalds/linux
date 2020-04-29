/*
 * Copyright 2018 Red Hat Inc.
 * Copyright 2019 NVIDIA Corporation.
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
#include "mem.h"
#include "vmm.h"

#include <core/option.h>

#include <nvif/class.h>

const u8 *
tu102_mmu_kind(struct nvkm_mmu *mmu, int *count, u8 *invalid)
{
	static const u8
	kind[16] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 0x00 */
		0x06, 0x06, 0x02, 0x01, 0x03, 0x04, 0x05, 0x07,
	};
	*count = ARRAY_SIZE(kind);
	*invalid = 0x07;
	return kind;
}

static const struct nvkm_mmu_func
tu102_mmu = {
	.dma_bits = 47,
	.mmu = {{ -1, -1, NVIF_CLASS_MMU_GF100}},
	.mem = {{ -1,  0, NVIF_CLASS_MEM_GF100}, gf100_mem_new, gf100_mem_map },
	.vmm = {{ -1,  0, NVIF_CLASS_VMM_GP100}, tu102_vmm_new },
	.kind = tu102_mmu_kind,
	.kind_sys = true,
};

int
tu102_mmu_new(struct nvkm_device *device, int index, struct nvkm_mmu **pmmu)
{
	return nvkm_mmu_new_(&tu102_mmu, device, index, pmmu);
}
