/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

#include <nvhw/drf.h>
#include <nvhw/ref/gh100/dev_fb.h>

static void
gh100_fb_sysmem_flush_page_init(struct nvkm_fb *fb)
{
	const u64 addr = fb->sysmem.flush_page_addr >> NV_PFB_NISO_FLUSH_SYSMEM_ADDR_SHIFT;
	struct nvkm_device *device = fb->subdev.device;

	nvkm_wr32(device, NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_HI, upper_32_bits(addr));
	nvkm_wr32(device, NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_LO, lower_32_bits(addr));
}

static const struct nvkm_fb_func
gh100_fb = {
	.sysmem.flush_page_init = gh100_fb_sysmem_flush_page_init,
	.vidmem.size = ga102_fb_vidmem_size,
};

int
gh100_fb_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_fb **pfb)
{
	return r535_fb_new(&gh100_fb, device, type, inst, pfb);
}
