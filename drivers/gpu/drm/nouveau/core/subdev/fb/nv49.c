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

struct nv49_fb_priv {
	struct nouveau_fb base;
};

static int
nv49_fb_vram_init(struct nouveau_fb *pfb)
{
	u32 pfb914 = nv_rd32(pfb, 0x100914);

	switch (pfb914 & 0x00000003) {
	case 0x00000000: pfb->ram.type = NV_MEM_TYPE_DDR1; break;
	case 0x00000001: pfb->ram.type = NV_MEM_TYPE_DDR2; break;
	case 0x00000002: pfb->ram.type = NV_MEM_TYPE_GDDR3; break;
	case 0x00000003: break;
	}

	pfb->ram.size =   nv_rd32(pfb, 0x10020c) & 0xff000000;
	pfb->ram.parts = (nv_rd32(pfb, 0x100200) & 0x00000003) + 1;
	return nv_rd32(pfb, 0x100320);
}

static int
nv49_fb_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nv49_fb_priv *priv;
	int ret;

	ret = nouveau_fb_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.memtype_valid = nv04_fb_memtype_valid;
	priv->base.ram.init = nv49_fb_vram_init;
	priv->base.tile.regions = 15;
	priv->base.tile.init = nv30_fb_tile_init;
	priv->base.tile.comp = nv40_fb_tile_comp;
	priv->base.tile.fini = nv20_fb_tile_fini;
	priv->base.tile.prog = nv41_fb_tile_prog;

	return nouveau_fb_preinit(&priv->base);
}


struct nouveau_oclass
nv49_fb_oclass = {
	.handle = NV_SUBDEV(FB, 0x49),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv49_fb_ctor,
		.dtor = _nouveau_fb_dtor,
		.init = nv41_fb_init,
		.fini = _nouveau_fb_fini,
	},
};
