/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

#include <nvhw/drf.h>
#include <nvhw/ref/gb100/dev_hshub_base.h>

static void
gb100_fb_sysmem_flush_page_init(struct nvkm_fb *fb)
{
	const u32 addr_hi = upper_32_bits(fb->sysmem.flush_page_addr);
	const u32 addr_lo = lower_32_bits(fb->sysmem.flush_page_addr);
	const u32 hshub = DRF_LO(NV_PFB_HSHUB0);
	struct nvkm_device *device = fb->subdev.device;

	nvkm_wr32(device, hshub + NV_PFB_HSHUB_PCIE_FLUSH_SYSMEM_ADDR_HI, addr_hi);
	nvkm_wr32(device, hshub + NV_PFB_HSHUB_PCIE_FLUSH_SYSMEM_ADDR_LO, addr_lo);
	nvkm_wr32(device, hshub + NV_PFB_HSHUB_EG_PCIE_FLUSH_SYSMEM_ADDR_HI, addr_hi);
	nvkm_wr32(device, hshub + NV_PFB_HSHUB_EG_PCIE_FLUSH_SYSMEM_ADDR_LO, addr_lo);
}

static const struct nvkm_fb_func
gb100_fb = {
	.sysmem.flush_page_init = gb100_fb_sysmem_flush_page_init,
	.vidmem.size = ga102_fb_vidmem_size,
};

int
gb100_fb_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_fb **pfb)
{
	return r535_fb_new(&gb100_fb, device, type, inst, pfb);
}
