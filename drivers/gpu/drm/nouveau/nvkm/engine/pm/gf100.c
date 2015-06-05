/*
 * Copyright 2013 Red Hat Inc.
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
#include "gf100.h"

static const struct nvkm_specdom
gf100_pm_hub[] = {
	{}
};

static const struct nvkm_specdom
gf100_pm_gpc[] = {
	{}
};

static const struct nvkm_specdom
gf100_pm_part[] = {
	{}
};

static void
gf100_perfctr_init(struct nvkm_pm *ppm, struct nvkm_perfdom *dom,
		   struct nvkm_perfctr *ctr)
{
	struct gf100_pm_priv *priv = (void *)ppm;
	struct gf100_pm_cntr *cntr = (void *)ctr;
	u32 log = ctr->logic_op;
	u32 src = 0x00000000;
	int i;

	for (i = 0; i < 4 && ctr->signal[i]; i++)
		src |= (ctr->signal[i] - dom->signal) << (i * 8);

	nv_wr32(priv, dom->addr + 0x09c, 0x00040002);
	nv_wr32(priv, dom->addr + 0x100, 0x00000000);
	nv_wr32(priv, dom->addr + 0x040 + (cntr->base.slot * 0x08), src);
	nv_wr32(priv, dom->addr + 0x044 + (cntr->base.slot * 0x08), log);
}

static void
gf100_perfctr_read(struct nvkm_pm *ppm, struct nvkm_perfdom *dom,
		   struct nvkm_perfctr *ctr)
{
	struct gf100_pm_priv *priv = (void *)ppm;
	struct gf100_pm_cntr *cntr = (void *)ctr;

	switch (cntr->base.slot) {
	case 0: cntr->base.ctr = nv_rd32(priv, dom->addr + 0x08c); break;
	case 1: cntr->base.ctr = nv_rd32(priv, dom->addr + 0x088); break;
	case 2: cntr->base.ctr = nv_rd32(priv, dom->addr + 0x080); break;
	case 3: cntr->base.ctr = nv_rd32(priv, dom->addr + 0x090); break;
	}
	cntr->base.clk = nv_rd32(priv, dom->addr + 0x070);
}

static void
gf100_perfctr_next(struct nvkm_pm *ppm, struct nvkm_perfdom *dom)
{
	struct gf100_pm_priv *priv = (void *)ppm;
	nv_wr32(priv, dom->addr + 0x06c, dom->signal_nr - 0x40 + 0x27);
	nv_wr32(priv, dom->addr + 0x0ec, 0x00000011);
}

const struct nvkm_funcdom
gf100_perfctr_func = {
	.init = gf100_perfctr_init,
	.read = gf100_perfctr_read,
	.next = gf100_perfctr_next,
};

int
gf100_pm_fini(struct nvkm_object *object, bool suspend)
{
	struct gf100_pm_priv *priv = (void *)object;
	nv_mask(priv, 0x000200, 0x10000000, 0x00000000);
	nv_mask(priv, 0x000200, 0x10000000, 0x10000000);
	return nvkm_pm_fini(&priv->base, suspend);
}

static int
gf100_pm_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct gf100_pm_priv *priv;
	u32 mask;
	int ret;

	ret = nvkm_pm_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nvkm_perfdom_new(&priv->base, "pwr", 0, 0, 0, 0, gf100_pm_pwr);
	if (ret)
		return ret;

	/* HUB */
	ret = nvkm_perfdom_new(&priv->base, "hub", 0, 0x1b0000, 0, 0x200,
			       gf100_pm_hub);
	if (ret)
		return ret;

	/* GPC */
	mask  = (1 << nv_rd32(priv, 0x022430)) - 1;
	mask &= ~nv_rd32(priv, 0x022504);
	mask &= ~nv_rd32(priv, 0x022584);

	ret = nvkm_perfdom_new(&priv->base, "gpc", mask, 0x180000,
			       0x1000, 0x200, gf100_pm_gpc);
	if (ret)
		return ret;

	/* PART */
	mask  = (1 << nv_rd32(priv, 0x022438)) - 1;
	mask &= ~nv_rd32(priv, 0x022548);
	mask &= ~nv_rd32(priv, 0x0225c8);

	ret = nvkm_perfdom_new(&priv->base, "part", mask, 0x1a0000,
			       0x1000, 0x200, gf100_pm_part);
	if (ret)
		return ret;

	nv_engine(priv)->cclass = &nvkm_pm_cclass;
	nv_engine(priv)->sclass =  nvkm_pm_sclass;
	priv->base.last = 7;
	return 0;
}

struct nvkm_oclass
gf100_pm_oclass = {
	.handle = NV_ENGINE(PM, 0xc0),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_pm_ctor,
		.dtor = _nvkm_pm_dtor,
		.init = _nvkm_pm_init,
		.fini = gf100_pm_fini,
	},
};
