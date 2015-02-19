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

void
nv20_fb_tile_init(struct nvkm_fb *pfb, int i, u32 addr, u32 size, u32 pitch,
		  u32 flags, struct nvkm_fb_tile *tile)
{
	tile->addr  = 0x00000001 | addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;
	if (flags & 4) {
		pfb->tile.comp(pfb, i, size, flags, tile);
		tile->addr |= 2;
	}
}

static void
nv20_fb_tile_comp(struct nvkm_fb *pfb, int i, u32 size, u32 flags,
		  struct nvkm_fb_tile *tile)
{
	u32 tiles = DIV_ROUND_UP(size, 0x40);
	u32 tags  = round_up(tiles / pfb->ram->parts, 0x40);
	if (!nvkm_mm_head(&pfb->tags, 0, 1, tags, tags, 1, &tile->tag)) {
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
nv20_fb_tile_fini(struct nvkm_fb *pfb, int i, struct nvkm_fb_tile *tile)
{
	tile->addr  = 0;
	tile->limit = 0;
	tile->pitch = 0;
	tile->zcomp = 0;
	nvkm_mm_free(&pfb->tags, &tile->tag);
}

void
nv20_fb_tile_prog(struct nvkm_fb *pfb, int i, struct nvkm_fb_tile *tile)
{
	nv_wr32(pfb, 0x100244 + (i * 0x10), tile->limit);
	nv_wr32(pfb, 0x100248 + (i * 0x10), tile->pitch);
	nv_wr32(pfb, 0x100240 + (i * 0x10), tile->addr);
	nv_rd32(pfb, 0x100240 + (i * 0x10));
	nv_wr32(pfb, 0x100300 + (i * 0x04), tile->zcomp);
}

struct nvkm_oclass *
nv20_fb_oclass = &(struct nv04_fb_impl) {
	.base.base.handle = NV_SUBDEV(FB, 0x20),
	.base.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_fb_ctor,
		.dtor = _nvkm_fb_dtor,
		.init = _nvkm_fb_init,
		.fini = _nvkm_fb_fini,
	},
	.base.memtype = nv04_fb_memtype_valid,
	.base.ram = &nv20_ram_oclass,
	.tile.regions = 8,
	.tile.init = nv20_fb_tile_init,
	.tile.comp = nv20_fb_tile_comp,
	.tile.fini = nv20_fb_tile_fini,
	.tile.prog = nv20_fb_tile_prog,
}.base.base;
