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
nv25_fb_tile_comp(struct nouveau_fb *pfb, int i, u32 size, u32 flags,
		  struct nouveau_fb_tile *tile)
{
	u32 tiles = DIV_ROUND_UP(size, 0x40);
	u32 tags  = round_up(tiles / pfb->ram->parts, 0x40);
	if (!nouveau_mm_head(&pfb->tags, 1, tags, tags, 1, &tile->tag)) {
		if (!(flags & 2)) tile->zcomp = 0x00100000; /* Z16 */
		else              tile->zcomp = 0x00200000; /* Z24S8 */
		tile->zcomp |= tile->tag->offset;
#ifdef __BIG_ENDIAN
		tile->zcomp |= 0x01000000;
#endif
	}
}

struct nouveau_oclass *
nv25_fb_oclass = &(struct nv04_fb_impl) {
	.base.base.handle = NV_SUBDEV(FB, 0x25),
	.base.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_fb_ctor,
		.dtor = _nouveau_fb_dtor,
		.init = _nouveau_fb_init,
		.fini = _nouveau_fb_fini,
	},
	.base.memtype = nv04_fb_memtype_valid,
	.base.ram = &nv20_ram_oclass,
	.tile.regions = 8,
	.tile.init = nv20_fb_tile_init,
	.tile.comp = nv25_fb_tile_comp,
	.tile.fini = nv20_fb_tile_fini,
	.tile.prog = nv20_fb_tile_prog,
}.base.base;
