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
#include "gf100.h"
#include "ram.h"

#include <core/memory.h>

int
gm200_fb_init_page(struct nvkm_fb *fb)
{
	struct nvkm_device *device = fb->subdev.device;
	switch (fb->page) {
	case 16: nvkm_mask(device, 0x100c80, 0x00001801, 0x00001001); break;
	case 17: nvkm_mask(device, 0x100c80, 0x00001801, 0x00000000); break;
	case  0: nvkm_mask(device, 0x100c80, 0x00001800, 0x00001800); break;
	default:
		return -EINVAL;
	}
	return 0;
}

void
gm200_fb_init(struct nvkm_fb *base)
{
	struct gf100_fb *fb = gf100_fb(base);
	struct nvkm_device *device = fb->base.subdev.device;

	if (fb->r100c10_page)
		nvkm_wr32(device, 0x100c10, fb->r100c10 >> 8);

	nvkm_mask(device, 0x100c80, 0x00000001, 0x00000000); /* 128KiB lpg */

	nvkm_wr32(device, 0x100cc8, nvkm_memory_addr(fb->base.mmu_wr) >> 8);
	nvkm_wr32(device, 0x100ccc, nvkm_memory_addr(fb->base.mmu_rd) >> 8);
	nvkm_mask(device, 0x100cc4, 0x00060000,
		  min(nvkm_memory_size(fb->base.mmu_rd) >> 16, (u64)2) << 17);
}

static const struct nvkm_fb_func
gm200_fb = {
	.dtor = gf100_fb_dtor,
	.oneinit = gf100_fb_oneinit,
	.init = gm200_fb_init,
	.init_page = gm200_fb_init_page,
	.intr = gf100_fb_intr,
	.ram_new = gm200_ram_new,
	.default_bigpage = 0 /* per-instance. */,
};

int
gm200_fb_new(struct nvkm_device *device, int index, struct nvkm_fb **pfb)
{
	return gf100_fb_new_(&gm200_fb, device, index, pfb);
}
