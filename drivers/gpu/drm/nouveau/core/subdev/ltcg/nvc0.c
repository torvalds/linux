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

#include <subdev/ltcg.h>

struct nvc0_ltcg_priv {
	struct nouveau_ltcg base;
	u32 subp_nr;
};

static void
nvc0_ltcg_subp_isr(struct nvc0_ltcg_priv *priv, int unit, int subp)
{
	u32 subp_base = 0x141000 + (unit * 0x2000) + (subp * 0x400);
	u32 stat = nv_rd32(priv, subp_base + 0x020);

	if (stat) {
		nv_info(priv, "LTC%d_LTS%d: 0x%08x\n", unit, subp, stat);
		nv_wr32(priv, subp_base + 0x020, stat);
	}
}

static void
nvc0_ltcg_intr(struct nouveau_subdev *subdev)
{
	struct nvc0_ltcg_priv *priv = (void *)subdev;
	u32 units;

	units = nv_rd32(priv, 0x00017c);
	while (units) {
		u32 subp, unit = ffs(units) - 1;
		for (subp = 0; subp < priv->subp_nr; subp++)
			nvc0_ltcg_subp_isr(priv, unit, subp);
		units &= ~(1 << unit);
	}

	/* we do something horribly wrong and upset PMFB a lot, so mask off
	 * interrupts from it after the first one until it's fixed
	 */
	nv_mask(priv, 0x000640, 0x02000000, 0x00000000);
}

static int
nvc0_ltcg_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nvc0_ltcg_priv *priv;
	int ret;

	ret = nouveau_ltcg_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->subp_nr = nv_rd32(priv, 0x17e8dc) >> 24;
	nv_mask(priv, 0x17e820, 0x00100000, 0x00000000); /* INTR_EN &= ~0x10 */

	nv_subdev(priv)->intr = nvc0_ltcg_intr;
	return 0;
}

struct nouveau_oclass
nvc0_ltcg_oclass = {
	.handle = NV_SUBDEV(LTCG, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_ltcg_ctor,
		.dtor = _nouveau_ltcg_dtor,
		.init = _nouveau_ltcg_init,
		.fini = _nouveau_ltcg_fini,
	},
};
