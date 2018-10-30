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
#include "priv.h"
#include "ram.h"

void
nv20_fb_tile_init(struct nvkm_fb *fb, int i, u32 addr, u32 size, u32 pitch,
		  u32 flags, struct nvkm_fb_tile *tile)
{
	tile->addr  = 0x00000001 | addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;
	if (flags & 4) {
		fb->func->tile.comp(fb, i, size, flags, tile);
		tile->addr |= 2;
	}
}

static void
nv20_fb_tile_comp(struct nvkm_fb *fb, int i, u32 size, u32 flags,
		  struct nvkm_fb_tile *tile)
{
	u32 tiles = DIV_ROUND_UP(size, 0x40);
	u32 tags  = round_up(tiles / fb->ram->parts, 0x40);
	if (!nvkm_mm_head(&fb->tags, 0, 1, tags, tags, 1, &tile->tag)) {
		if (!(flags & 2)) tile->zcomp = 0x00000000; /* Z16 */
		else              tile->zcomp = 0x04000000; /* Z24S8 */
		tile->zcomp |= tile->tag->offset;
		tile->zcomp |= 0x80000000; /* enable */
#ifdef __BIG_ENDIAN
		tile->zcomp |= 0x08000000;
#endif
	}
}

void
nv20_fb_tile_fini(struct nvkm_fb *fb, int i, struct nvkm_fb_tile *tile)
{
	tile->addr  = 0;
	tile->limit = 0;
	tile->pitch = 0;
	tile->zcomp = 0;
	nvkm_mm_free(&fb->tags, &tile->tag);
}

void
nv20_fb_tile_prog(struct nvkm_fb *fb, int i, struct nvkm_fb_tile *tile)
{
	struct nvkm_device *device = fb->subdev.device;
	nvkm_wr32(device, 0x100244 + (i * 0x10), tile->limit);
	nvkm_wr32(device, 0x100248 + (i * 0x10), tile->pitch);
	nvkm_wr32(device, 0x100240 + (i * 0x10), tile->addr);
	nvkm_rd32(device, 0x100240 + (i * 0x10));
	nvkm_wr32(device, 0x100300 + (i * 0x04), tile->zcomp);
}

u32
nv20_fb_tags(struct nvkm_fb *fb)
{
	const u32 tags = nvkm_rd32(fb->subdev.device, 0x100320);
	return tags ? tags + 1 : 0;
}

static const struct nvkm_fb_func
nv20_fb = {
	.tags = nv20_fb_tags,
	.tile.regions = 8,
	.tile.init = nv20_fb_tile_init,
	.tile.comp = nv20_fb_tile_comp,
	.tile.fini = nv20_fb_tile_fini,
	.tile.prog = nv20_fb_tile_prog,
	.ram_new = nv20_ram_new,
};

int
nv20_fb_new(struct nvkm_device *device, int index, struct nvkm_fb **pfb)
{
	return nvkm_fb_new_(&nv20_fb, device, index, pfb);
}
