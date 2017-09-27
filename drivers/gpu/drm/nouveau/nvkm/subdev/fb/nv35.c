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

static void
nv35_fb_tile_comp(struct nvkm_fb *fb, int i, u32 size, u32 flags,
		  struct nvkm_fb_tile *tile)
{
	u32 tiles = DIV_ROUND_UP(size, 0x40);
	u32 tags  = round_up(tiles / fb->ram->parts, 0x40);
	if (!nvkm_mm_head(&fb->ram->tags, 0, 1, tags, tags, 1, &tile->tag)) {
		if (flags & 2) tile->zcomp |= 0x04000000; /* Z16 */
		else           tile->zcomp |= 0x08000000; /* Z24S8 */
		tile->zcomp |= ((tile->tag->offset           ) >> 6);
		tile->zcomp |= ((tile->tag->offset + tags - 1) >> 6) << 13;
#ifdef __BIG_ENDIAN
		tile->zcomp |= 0x40000000;
#endif
	}
}

static const struct nvkm_fb_func
nv35_fb = {
	.init = nv30_fb_init,
	.tile.regions = 8,
	.tile.init = nv30_fb_tile_init,
	.tile.comp = nv35_fb_tile_comp,
	.tile.fini = nv20_fb_tile_fini,
	.tile.prog = nv20_fb_tile_prog,
	.ram_new = nv20_ram_new,
	.memtype_valid = nv04_fb_memtype_valid,
};

int
nv35_fb_new(struct nvkm_device *device, int index, struct nvkm_fb **pfb)
{
	return nvkm_fb_new_(&nv35_fb, device, index, pfb);
}
