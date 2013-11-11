/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <subdev/ibus.h>

struct nve0_ibus_priv {
	struct nouveau_ibus base;
};

static void
nve0_ibus_intr_hub(struct nve0_ibus_priv *priv, int i)
{
	u32 addr = nv_rd32(priv, 0x122120 + (i * 0x0800));
	u32 data = nv_rd32(priv, 0x122124 + (i * 0x0800));
	u32 stat = nv_rd32(priv, 0x122128 + (i * 0x0800));
	nv_error(priv, "HUB%d: 0x%06x 0x%08x (0x%08x)\n", i, addr, data, stat);
	nv_mask(priv, 0x122128 + (i * 0x0800), 0x00000200, 0x00000000);
}

static void
nve0_ibus_intr_rop(struct nve0_ibus_priv *priv, int i)
{
	u32 addr = nv_rd32(priv, 0x124120 + (i * 0x0800));
	u32 data = nv_rd32(priv, 0x124124 + (i * 0x0800));
	u32 stat = nv_rd32(priv, 0x124128 + (i * 0x0800));
	nv_error(priv, "ROP%d: 0x%06x 0x%08x (0x%08x)\n", i, addr, data, stat);
	nv_mask(priv, 0x124128 + (i * 0x0800), 0x00000200, 0x00000000);
}

static void
nve0_ibus_intr_gpc(struct nve0_ibus_priv *priv, int i)
{
	u32 addr = nv_rd32(priv, 0x128120 + (i * 0x0800));
	u32 data = nv_rd32(priv, 0x128124 + (i * 0x0800));
	u32 stat = nv_rd32(priv, 0x128128 + (i * 0x0800));
	nv_error(priv, "GPC%d: 0x%06x 0x%08x (0x%08x)\n", i, addr, data, stat);
	nv_mask(priv, 0x128128 + (i * 0x0800), 0x00000200, 0x00000000);
}

static void
nve0_ibus_intr(struct nouveau_subdev *subdev)
{
	struct nve0_ibus_priv *priv = (void *)subdev;
	u32 intr0 = nv_rd32(priv, 0x120058);
	u32 intr1 = nv_rd32(priv, 0x12005c);
	u32 hubnr = nv_rd32(priv, 0x120070);
	u32 ropnr = nv_rd32(priv, 0x120074);
	u32 gpcnr = nv_rd32(priv, 0x120078);
	u32 i;

	for (i = 0; (intr0 & 0x0000ff00) && i < hubnr; i++) {
		u32 stat = 0x00000100 << i;
		if (intr0 & stat) {
			nve0_ibus_intr_hub(priv, i);
			intr0 &= ~stat;
		}
	}

	for (i = 0; (intr0 & 0xffff0000) && i < ropnr; i++) {
		u32 stat = 0x00010000 << i;
		if (intr0 & stat) {
			nve0_ibus_intr_rop(priv, i);
			intr0 &= ~stat;
		}
	}

	for (i = 0; intr1 && i < gpcnr; i++) {
		u32 stat = 0x00000001 << i;
		if (intr1 & stat) {
			nve0_ibus_intr_gpc(priv, i);
			intr1 &= ~stat;
		}
	}
}

static int
nve0_ibus_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nve0_ibus_priv *priv;
	int ret;

	ret = nouveau_ibus_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->intr = nve0_ibus_intr;
	return 0;
}

struct nouveau_oclass
nve0_ibus_oclass = {
	.handle = NV_SUBDEV(IBUS, 0xe0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nve0_ibus_ctor,
		.dtor = _nouveau_ibus_dtor,
		.init = _nouveau_ibus_init,
		.fini = _nouveau_ibus_fini,
	},
};
