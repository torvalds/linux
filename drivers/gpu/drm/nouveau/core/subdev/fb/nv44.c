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

#include <subdev/fb.h>

struct nv44_fb_priv {
	struct nouveau_fb base;
};

int
nv44_fb_vram_init(struct nouveau_fb *pfb)
{
	u32 pfb474 = nv_rd32(pfb, 0x100474);
	if (pfb474 & 0x00000004)
		pfb->ram.type = NV_MEM_TYPE_GDDR3;
	if (pfb474 & 0x00000002)
		pfb->ram.type = NV_MEM_TYPE_DDR2;
	if (pfb474 & 0x00000001)
		pfb->ram.type = NV_MEM_TYPE_DDR1;

	pfb->ram.size = nv_rd32(pfb, 0x10020c) & 0xff000000;
	return 0;
}

static void
nv44_fb_tile_init(struct nouveau_fb *pfb, int i, u32 addr, u32 size, u32 pitch,
		  u32 flags, struct nouveau_fb_tile *tile)
{
	tile->addr  = 0x00000001; /* mode = vram */
	tile->addr |= addr;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;
}

void
nv44_fb_tile_prog(struct nouveau_fb *pfb, int i, struct nouveau_fb_tile *tile)
{
	nv_wr32(pfb, 0x100604 + (i * 0x10), tile->limit);
	nv_wr32(pfb, 0x100608 + (i * 0x10), tile->pitch);
	nv_wr32(pfb, 0x100600 + (i * 0x10), tile->addr);
	nv_rd32(pfb, 0x100600 + (i * 0x10));
}

int
nv44_fb_init(struct nouveau_object *object)
{
	struct nv44_fb_priv *priv = (void *)object;
	int ret;

	ret = nouveau_fb_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x100850, 0x80000000);
	nv_wr32(priv, 0x100800, 0x00000001);
	return 0;
}

static int
nv44_fb_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nv44_fb_priv *priv;
	int ret;

	ret = nouveau_fb_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.memtype_valid = nv04_fb_memtype_valid;
	priv->base.ram.init = nv44_fb_vram_init;
	priv->base.tile.regions = 12;
	priv->base.tile.init = nv44_fb_tile_init;
	priv->base.tile.fini = nv20_fb_tile_fini;
	priv->base.tile.prog = nv44_fb_tile_prog;
	return nouveau_fb_preinit(&priv->base);
}


struct nouveau_oclass
nv44_fb_oclass = {
	.handle = NV_SUBDEV(FB, 0x44),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv44_fb_ctor,
		.dtor = _nouveau_fb_dtor,
		.init = nv44_fb_init,
		.fini = _nouveau_fb_fini,
	},
};
