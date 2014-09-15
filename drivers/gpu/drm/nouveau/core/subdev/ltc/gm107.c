/*
 * Copyright 2014 Red Hat Inc.
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

#include <subdev/fb.h>
#include <subdev/timer.h>

#include "priv.h"

static void
gm107_ltc_cbc_clear(struct nvkm_ltc_priv *priv, u32 start, u32 limit)
{
	nv_wr32(priv, 0x17e270, start);
	nv_wr32(priv, 0x17e274, limit);
	nv_wr32(priv, 0x17e26c, 0x00000004);
}

static void
gm107_ltc_cbc_wait(struct nvkm_ltc_priv *priv)
{
	int c, s;
	for (c = 0; c < priv->ltc_nr; c++) {
		for (s = 0; s < priv->lts_nr; s++)
			nv_wait(priv, 0x14046c + c * 0x2000 + s * 0x200, ~0, 0);
	}
}

static void
gm107_ltc_zbc_clear_color(struct nvkm_ltc_priv *priv, int i, const u32 color[4])
{
	nv_mask(priv, 0x17e338, 0x0000000f, i);
	nv_wr32(priv, 0x17e33c, color[0]);
	nv_wr32(priv, 0x17e340, color[1]);
	nv_wr32(priv, 0x17e344, color[2]);
	nv_wr32(priv, 0x17e348, color[3]);
}

static void
gm107_ltc_zbc_clear_depth(struct nvkm_ltc_priv *priv, int i, const u32 depth)
{
	nv_mask(priv, 0x17e338, 0x0000000f, i);
	nv_wr32(priv, 0x17e34c, depth);
}

static void
gm107_ltc_lts_isr(struct nvkm_ltc_priv *priv, int ltc, int lts)
{
	u32 base = 0x140000 + (ltc * 0x2000) + (lts * 0x400);
	u32 stat = nv_rd32(priv, base + 0x00c);

	if (stat) {
		nv_info(priv, "LTC%d_LTS%d: 0x%08x\n", ltc, lts, stat);
		nv_wr32(priv, base + 0x00c, stat);
	}
}

static void
gm107_ltc_intr(struct nouveau_subdev *subdev)
{
	struct nvkm_ltc_priv *priv = (void *)subdev;
	u32 mask;

	mask = nv_rd32(priv, 0x00017c);
	while (mask) {
		u32 lts, ltc = __ffs(mask);
		for (lts = 0; lts < priv->lts_nr; lts++)
			gm107_ltc_lts_isr(priv, ltc, lts);
		mask &= ~(1 << ltc);
	}
}

static int
gm107_ltc_init(struct nouveau_object *object)
{
	struct nvkm_ltc_priv *priv = (void *)object;
	int ret;

	ret = nvkm_ltc_init(priv);
	if (ret)
		return ret;

	nv_wr32(priv, 0x17e27c, priv->ltc_nr);
	nv_wr32(priv, 0x17e278, priv->tag_base);
	return 0;
}

static int
gm107_ltc_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nouveau_fb *pfb = nouveau_fb(parent);
	struct nvkm_ltc_priv *priv;
	u32 parts, mask;
	int ret, i;

	ret = nvkm_ltc_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	parts = nv_rd32(priv, 0x022438);
	mask = nv_rd32(priv, 0x021c14);
	for (i = 0; i < parts; i++) {
		if (!(mask & (1 << i)))
			priv->ltc_nr++;
	}
	priv->lts_nr = nv_rd32(priv, 0x17e280) >> 28;

	ret = gf100_ltc_init_tag_ram(pfb, priv);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass *
gm107_ltc_oclass = &(struct nvkm_ltc_impl) {
	.base.handle = NV_SUBDEV(LTC, 0xff),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = gm107_ltc_ctor,
		.dtor = gf100_ltc_dtor,
		.init = gm107_ltc_init,
		.fini = _nvkm_ltc_fini,
	},
	.intr = gm107_ltc_intr,
	.cbc_clear = gm107_ltc_cbc_clear,
	.cbc_wait = gm107_ltc_cbc_wait,
	.zbc = 16,
	.zbc_clear_color = gm107_ltc_zbc_clear_color,
	.zbc_clear_depth = gm107_ltc_zbc_clear_depth,
}.base;
