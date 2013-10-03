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

struct nv10_fb_priv {
	struct nouveau_fb base;
};

void
nv10_fb_tile_init(struct nouveau_fb *pfb, int i, u32 addr, u32 size, u32 pitch,
		  u32 flags, struct nouveau_fb_tile *tile)
{
	tile->addr  = 0x80000000 | addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;
}

void
nv10_fb_tile_fini(struct nouveau_fb *pfb, int i, struct nouveau_fb_tile *tile)
{
	tile->addr  = 0;
	tile->limit = 0;
	tile->pitch = 0;
	tile->zcomp = 0;
}

void
nv10_fb_tile_prog(struct nouveau_fb *pfb, int i, struct nouveau_fb_tile *tile)
{
	nv_wr32(pfb, 0x100244 + (i * 0x10), tile->limit);
	nv_wr32(pfb, 0x100248 + (i * 0x10), tile->pitch);
	nv_wr32(pfb, 0x100240 + (i * 0x10), tile->addr);
	nv_rd32(pfb, 0x100240 + (i * 0x10));
}

static int
nv10_fb_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nv10_fb_priv *priv;
	int ret;

	ret = nouveau_fb_create(parent, engine, oclass, &nv10_ram_oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.memtype_valid = nv04_fb_memtype_valid;
	priv->base.tile.regions = 8;
	priv->base.tile.init = nv10_fb_tile_init;
	priv->base.tile.fini = nv10_fb_tile_fini;
	priv->base.tile.prog = nv10_fb_tile_prog;
	return 0;
}

struct nouveau_oclass
nv10_fb_oclass = {
	.handle = NV_SUBDEV(FB, 0x10),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv10_fb_ctor,
		.dtor = _nouveau_fb_dtor,
		.init = _nouveau_fb_init,
		.fini = _nouveau_fb_fini,
	},
};
