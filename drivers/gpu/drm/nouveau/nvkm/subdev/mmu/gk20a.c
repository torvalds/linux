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
#include "mem.h"
#include "vmm.h"

#include <nvif/class.h>

static const struct nvkm_mmu_func
gk20a_mmu = {
	.dma_bits = 40,
	.mmu = {{ -1, -1, NVIF_CLASS_MMU_GF100}},
	.mem = {{ -1, -1, NVIF_CLASS_MEM_GF100}, .umap = gf100_mem_map },
	.vmm = {{ -1, -1, NVIF_CLASS_VMM_GF100}, gk20a_vmm_new },
	.kind = gf100_mmu_kind,
	.kind_sys = true,
};

int
gk20a_mmu_new(struct nvkm_device *device, int index, struct nvkm_mmu **pmmu)
{
	return nvkm_mmu_new_(&gk20a_mmu, device, index, pmmu);
}
