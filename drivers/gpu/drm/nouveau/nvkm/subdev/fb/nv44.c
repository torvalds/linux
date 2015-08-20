/*
 * Copyright (C) 2010 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "nv04.h"

static void
nv44_fb_tile_init(struct nvkm_fb *fb, int i, u32 addr, u32 size, u32 pitch,
		  u32 flags, struct nvkm_fb_tile *tile)
{
	tile->addr  = 0x00000001; /* mode = vram */
	tile->addr |= addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;
}

void
nv44_fb_tile_prog(struct nvkm_fb *fb, int i, struct nvkm_fb_tile *tile)
{
	struct nvkm_device *device = fb->subdev.device;
	nvkm_wr32(device, 0x100604 + (i * 0x10), tile->limit);
	nvkm_wr32(device, 0x100608 + (i * 0x10), tile->pitch);
	nvkm_wr32(device, 0x100600 + (i * 0x10), tile->addr);
	nvkm_rd32(device, 0x100600 + (i * 0x10));
}

int
nv44_fb_init(struct nvkm_object *object)
{
	struct nvkm_fb *fb = (void *)object;
	struct nvkm_device *device = fb->subdev.device;
	int ret;

	ret = nvkm_fb_init(fb);
	if (ret)
		return ret;

	nvkm_wr32(device, 0x100850, 0x80000000);
	nvkm_wr32(device, 0x100800, 0x00000001);
	return 0;
}

struct nvkm_oclass *
nv44_fb_oclass = &(struct nv04_fb_impl) {
	.base.base.handle = NV_SUBDEV(FB, 0x44),
	.base.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_fb_ctor,
		.dtor = _nvkm_fb_dtor,
		.init = nv44_fb_init,
		.fini = _nvkm_fb_fini,
	},
	.base.memtype = nv04_fb_memtype_valid,
	.base.ram = &nv44_ram_oclass,
	.tile.regions = 12,
	.tile.init = nv44_fb_tile_init,
	.tile.fini = nv20_fb_tile_fini,
	.tile.prog = nv44_fb_tile_prog,
}.base.base;
