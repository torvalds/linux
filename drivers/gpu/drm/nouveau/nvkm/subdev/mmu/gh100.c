/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "mem.h"
#include "vmm.h"

#include <nvif/class.h>

static const struct nvkm_mmu_func
gh100_mmu = {
	.dma_bits = 52,
	.mmu = {{ -1, -1, NVIF_CLASS_MMU_GF100}},
	.mem = {{ -1,  0, NVIF_CLASS_MEM_GF100}, gf100_mem_new, gf100_mem_map },
	.vmm = {{ -1,  0, NVIF_CLASS_VMM_GP100}, gh100_vmm_new },
	.kind = tu102_mmu_kind,
	.kind_sys = true,
};

int
gh100_mmu_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_mmu **pmmu)
{
	return r535_mmu_new(&gh100_mmu, device, type, inst, pmmu);
}
