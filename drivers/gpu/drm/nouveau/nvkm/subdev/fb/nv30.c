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
nv30_fb_tile_init(struct nvkm_fb *fb, int i, u32 addr, u32 size, u32 pitch,
		  u32 flags, struct nvkm_fb_tile *tile)
{
	/* for performance, select alternate bank offset for zeta */
	if (!(flags & 4)) {
		tile->addr = (0 << 4);
	} else {
		if (fb->func->tile.comp) /* z compression */
			fb->func->tile.comp(fb, i, size, flags, tile);
		tile->addr = (1 << 4);
	}

	tile->addr |= 0x00000001; /* enable */
	tile->addr |= addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;
}

static void
nv30_fb_tile_comp(struct nvkm_fb *fb, int i, u32 size, u32 flags,
		  struct nvkm_fb_tile *tile)
{
	u32 tiles = DIV_ROUND_UP(size, 0x40);
	u32 tags  = round_up(tiles / fb->ram->parts, 0x40);
	if (!nvkm_mm_head(&fb->tags.mm, 0, 1, tags, tags, 1, &tile->tag)) {
		if (flags & 2) tile->zcomp |= 0x01000000; /* Z16 */
		else           tile->zcomp |= 0x02000000; /* Z24S8 */
		tile->zcomp |= ((tile->tag->offset           ) >> 6);
		tile->zcomp |= ((tile->tag->offset + tags - 1) >> 6) << 12;
#ifdef __BIG_ENDIAN
		tile->zcomp |= 0x10000000;
#endif
	}
}

static int
calc_bias(struct nvkm_fb *fb, int k, int i, int j)
{
	struct nvkm_device *device = fb->subdev.device;
	int b = (device->chipset > 0x30 ?
		 nvkm_rd32(device, 0x122c + 0x10 * k + 0x4 * j) >>
			(4 * (i ^ 1)) :
		 0) & 0xf;

	return 2 * (b & 0x8 ? b - 0x10 : b);
}

static int
calc_ref(struct nvkm_fb *fb, int l, int k, int i)
{
	int j, x = 0;

	for (j = 0; j < 4; j++) {
		int m = (l >> (8 * i) & 0xff) + calc_bias(fb, k, i, j);

		x |= (0x80 | clamp(m, 0, 0x1f)) << (8 * j);
	}

	return x;
}

void
nv30_fb_init(struct nvkm_fb *fb)
{
	struct nvkm_device *device = fb->subdev.device;
	int i, j;

	/* Init the memory timing regs at 0x10037c/0x1003ac */
	if (device->chipset == 0x30 ||
	    device->chipset == 0x31 ||
	    device->chipset == 0x35) {
		/* Related to ROP count */
		int n = (device->chipset == 0x31 ? 2 : 4);
		int l = nvkm_rd32(device, 0x1003d0);

		for (i = 0; i < n; i++) {
			for (j = 0; j < 3; j++)
				nvkm_wr32(device, 0x10037c + 0xc * i + 0x4 * j,
					  calc_ref(fb, l, 0, j));

			for (j = 0; j < 2; j++)
				nvkm_wr32(device, 0x1003ac + 0x8 * i + 0x4 * j,
					  calc_ref(fb, l, 1, j));
		}
	}
}

static const struct nvkm_fb_func
nv30_fb = {
	.tags = nv20_fb_tags,
	.init = nv30_fb_init,
	.tile.regions = 8,
	.tile.init = nv30_fb_tile_init,
	.tile.comp = nv30_fb_tile_comp,
	.tile.fini = nv20_fb_tile_fini,
	.tile.prog = nv20_fb_tile_prog,
	.ram_new = nv20_ram_new,
};

int
nv30_fb_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_fb **pfb)
{
	return nvkm_fb_new_(&nv30_fb, device, type, inst, pfb);
}
