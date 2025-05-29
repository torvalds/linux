/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

#include <nvhw/drf.h>
#include <nvhw/ref/gb10b/dev_fbhub.h>

static void
gb202_fb_sysmem_flush_page_init(struct nvkm_fb *fb)
{
	struct nvkm_device *device = fb->subdev.device;
	const u64 addr = fb->sysmem.flush_page_addr;

	nvkm_wr32(device, NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_HI, upper_32_bits(addr));
	nvkm_wr32(device, NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_LO, lower_32_bits(addr));
}

static const struct nvkm_fb_func
gb202_fb = {
	.sysmem.flush_page_init = gb202_fb_sysmem_flush_page_init,
	.vidmem.size = ga102_fb_vidmem_size,
};

int
gb202_fb_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_fb **pfb)
{
	return r535_fb_new(&gb202_fb, device, type, inst, pfb);
}
