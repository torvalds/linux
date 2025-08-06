/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

#include <nvhw/ref/gh100/pri_nv_xal_ep.h>

static void
gh100_instmem_set_bar0_window_addr(struct nvkm_device *device, u64 addr)
{
	nvkm_wr32(device, NV_XAL_EP_BAR0_WINDOW, addr >> NV_XAL_EP_BAR0_WINDOW_BASE_SHIFT);
}

static const struct nvkm_instmem_func
gh100_instmem = {
	.fini = nv50_instmem_fini,
	.memory_new = nv50_instobj_new,
	.memory_wrap = nv50_instobj_wrap,
	.set_bar0_window_addr = gh100_instmem_set_bar0_window_addr,
};

int
gh100_instmem_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		  struct nvkm_instmem **pimem)
{
	return r535_instmem_new(&gh100_instmem, device, type, inst, pimem);
}
