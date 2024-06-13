/*
 * Copyright 2016 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "gf100.h"
#include "ram.h"

#include <core/memory.h>

void
gp100_fb_init_unkn(struct nvkm_fb *base)
{
	struct nvkm_device *device = gf100_fb(base)->base.subdev.device;
	nvkm_wr32(device, 0x1fac80, nvkm_rd32(device, 0x100c80));
	nvkm_wr32(device, 0x1facc4, nvkm_rd32(device, 0x100cc4));
	nvkm_wr32(device, 0x1facc8, nvkm_rd32(device, 0x100cc8));
	nvkm_wr32(device, 0x1faccc, nvkm_rd32(device, 0x100ccc));
}

void
gp100_fb_init_remapper(struct nvkm_fb *fb)
{
	struct nvkm_device *device = fb->subdev.device;
	/* Disable address remapper. */
	nvkm_mask(device, 0x100c14, 0x00040000, 0x00000000);
}

void
gp100_fb_init(struct nvkm_fb *base)
{
	struct gf100_fb *fb = gf100_fb(base);
	struct nvkm_device *device = fb->base.subdev.device;

	if (fb->r100c10_page)
		nvkm_wr32(device, 0x100c10, fb->r100c10 >> 8);

	nvkm_wr32(device, 0x100cc8, nvkm_memory_addr(fb->base.mmu_wr) >> 8);
	nvkm_wr32(device, 0x100ccc, nvkm_memory_addr(fb->base.mmu_rd) >> 8);
	nvkm_mask(device, 0x100cc4, 0x00060000,
		  min(nvkm_memory_size(fb->base.mmu_rd) >> 16, (u64)2) << 17);
}

static const struct nvkm_fb_func
gp100_fb = {
	.dtor = gf100_fb_dtor,
	.oneinit = gf100_fb_oneinit,
	.init = gp100_fb_init,
	.init_remapper = gp100_fb_init_remapper,
	.init_page = gm200_fb_init_page,
	.init_unkn = gp100_fb_init_unkn,
	.ram_new = gp100_ram_new,
};

int
gp100_fb_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_fb **pfb)
{
	return gf100_fb_new_(&gp100_fb, device, type, inst, pfb);
}
