/*
 * Copyright 2013 Red Hat Inc.
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
#define mcp77_ram(p) container_of((p), struct mcp77_ram, base)
#include "ram.h"

struct mcp77_ram {
	struct nvkm_ram base;
	u64 poller_base;
};

static int
mcp77_ram_init(struct nvkm_ram *base)
{
	struct mcp77_ram *ram = mcp77_ram(base);
	struct nvkm_device *device = ram->base.fb->subdev.device;
	u32 dniso  = ((ram->base.size - (ram->poller_base + 0x00)) >> 5) - 1;
	u32 hostnb = ((ram->base.size - (ram->poller_base + 0x20)) >> 5) - 1;
	u32 flush  = ((ram->base.size - (ram->poller_base + 0x40)) >> 5) - 1;

	/* Enable NISO poller for various clients and set their associated
	 * read address, only for MCP77/78 and MCP79/7A. (fd#27501)
	 */
	nvkm_wr32(device, 0x100c18, dniso);
	nvkm_mask(device, 0x100c14, 0x00000000, 0x00000001);
	nvkm_wr32(device, 0x100c1c, hostnb);
	nvkm_mask(device, 0x100c14, 0x00000000, 0x00000002);
	nvkm_wr32(device, 0x100c24, flush);
	nvkm_mask(device, 0x100c14, 0x00000000, 0x00010000);
	return 0;
}

static const struct nvkm_ram_func
mcp77_ram_func = {
	.init = mcp77_ram_init,
	.get = nv50_ram_get,
	.put = nv50_ram_put,
};

int
mcp77_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	struct nvkm_device *device = fb->subdev.device;
	u32 rsvd_head = ( 256 * 1024); /* vga memory */
	u32 rsvd_tail = (1024 * 1024) + 0x1000; /* vbios etc + poller mem */
	u64 base = (u64)nvkm_rd32(device, 0x100e10) << 12;
	u64 size = (u64)nvkm_rd32(device, 0x100e14) << 12;
	struct mcp77_ram *ram;
	int ret;

	if (!(ram = kzalloc(sizeof(*ram), GFP_KERNEL)))
		return -ENOMEM;
	*pram = &ram->base;

	ret = nvkm_ram_ctor(&mcp77_ram_func, fb, NVKM_RAM_TYPE_STOLEN,
			    size, 0, &ram->base);
	if (ret)
		return ret;

	ram->poller_base = size - rsvd_tail;
	ram->base.stolen = base;
	nvkm_mm_fini(&ram->base.vram);

	return nvkm_mm_init(&ram->base.vram, rsvd_head >> NVKM_RAM_MM_SHIFT,
			    (size - rsvd_head - rsvd_tail) >>
			    NVKM_RAM_MM_SHIFT, 1);
}
