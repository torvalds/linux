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
nv30_fb_tile_init(struct nouveau_fb *pfb, int i, u32 addr, u32 size, u32 pitch,
		  u32 flags, struct nouveau_fb_tile *tile)
{
	/* for performance, select alternate bank offset for zeta */
	if (!(flags & 4)) {
		tile->addr = (0 << 4);
	} else {
		if (pfb->tile.comp) /* z compression */
			pfb->tile.comp(pfb, i, size, flags, tile);
		tile->addr = (1 << 4);
	}

	tile->addr |= 0x00000001; /* enable */
	tile->addr |= addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;
}

static void
nv30_fb_tile_comp(struct nouveau_fb *pfb, int i, u32 size, u32 flags,
		  struct nouveau_fb_tile *tile)
{
	u32 tiles = DIV_ROUND_UP(size, 0x40);
	u32 tags  = round_up(tiles / pfb->ram->parts, 0x40);
	if (!nouveau_mm_head(&pfb->tags, 1, tags, tags, 1, &tile->tag)) {
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
calc_bias(struct nv04_fb_priv *priv, int k, int i, int j)
{
	struct nouveau_device *device = nv_device(priv);
	int b = (device->chipset > 0x30 ?
		 nv_rd32(priv, 0x122c + 0x10 * k + 0x4 * j) >> (4 * (i ^ 1)) :
		 0) & 0xf;

	return 2 * (b & 0x8 ? b - 0x10 : b);
}

static int
calc_ref(struct nv04_fb_priv *priv, int l, int k, int i)
{
	int j, x = 0;

	for (j = 0; j < 4; j++) {
		int m = (l >> (8 * i) & 0xff) + calc_bias(priv, k, i, j);

		x |= (0x80 | clamp(m, 0, 0x1f)) << (8 * j);
	}

	return x;
}

int
nv30_fb_init(struct nouveau_object *object)
{
	struct nouveau_device *device = nv_device(object);
	struct nv04_fb_priv *priv = (void *)object;
	int ret, i, j;

	ret = nouveau_fb_init(&priv->base);
	if (ret)
		return ret;

	/* Init the memory timing regs at 0x10037c/0x1003ac */
	if (device->chipset == 0x30 ||
	    device->chipset == 0x31 ||
	    device->chipset == 0x35) {
		/* Related to ROP count */
		int n = (device->chipset == 0x31 ? 2 : 4);
		int l = nv_rd32(priv, 0x1003d0);

		for (i = 0; i < n; i++) {
			for (j = 0; j < 3; j++)
				nv_wr32(priv, 0x10037c + 0xc * i + 0x4 * j,
					calc_ref(priv, l, 0, j));

			for (j = 0; j < 2; j++)
				nv_wr32(priv, 0x1003ac + 0x8 * i + 0x4 * j,
					calc_ref(priv, l, 1, j));
		}
	}

	return 0;
}

struct nouveau_oclass *
nv30_fb_oclass = &(struct nv04_fb_impl) {
	.base.base.handle = NV_SUBDEV(FB, 0x30),
	.base.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_fb_ctor,
		.dtor = _nouveau_fb_dtor,
		.init = nv30_fb_init,
		.fini = _nouveau_fb_fini,
	},
	.base.memtype = nv04_fb_memtype_valid,
	.base.ram = &nv20_ram_oclass,
	.tile.regions = 8,
	.tile.init = nv30_fb_tile_init,
	.tile.comp = nv30_fb_tile_comp,
	.tile.fini = nv20_fb_tile_fini,
	.tile.prog = nv20_fb_tile_prog,
}.base.base;
