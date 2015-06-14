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

const struct nvkm_specsrc
gf100_pmfb_sources[] = {
	{ 0x140028, (const struct nvkm_specmux[]) {
			{ 0x3fff, 0, "unk0" },
			{ 0x7, 16, "unk16" },
			{ 0x3, 24, "unk24" },
			{ 0x2, 29, "unk29" },
			{}
		}, "pmfb0_pm_unk28" },
	{}
};

static const struct nvkm_specsrc
gf100_l1_sources[] = {
	{ 0x5044a8, (const struct nvkm_specmux[]) {
			{ 0x3f, 0, "sel", true },
			{}
		}, "pgraph_gpc0_tpc0_l1_pm_mux" },
	{}
};

static const struct nvkm_specsrc
gf100_pbfb_sources[] = {
	{ 0x10f100, (const struct nvkm_specmux[]) {
			{ 0x1, 0, "unk0" },
			{ 0xf, 4, "unk4" },
			{ 0x3, 8, "unk8" },
			{}
		}, "pbfb_broadcast_pm_unk100" },
	{}
};

static const struct nvkm_specsrc
gf100_tex_sources[] = {
	{ 0x5042c0, (const struct nvkm_specmux[]) {
			{ 0xf, 0, "sel0", true },
			{ 0x7, 8, "sel1", true },
			{}
		}, "pgraph_gpc0_tpc0_tex_pm_mux_c_d" },
	{ 0x5042c8, (const struct nvkm_specmux[]) {
			{ 0x1f, 0, "sel", true },
			{}
		}, "pgraph_gpc0_tpc0_tex_pm_unkc8" },
	{}
};

static const struct nvkm_specsrc
gf100_unk400_sources[] = {
	{ 0x50440c, (const struct nvkm_specmux[]) {
			{ 0x3f, 0, "sel", true },
			{}
		}, "pgraph_gpc0_tpc0_unk400_pm_mux" },
	{}
};

static const struct nvkm_specdom
gf100_pm_hub[] = {
	{}
};

const struct nvkm_specdom
gf100_pm_gpc[] = {
	{ 0xe0, (const struct nvkm_specsig[]) {
			{ 0x00, "gpc00_l1_00", gf100_l1_sources },
			{ 0x01, "gpc00_l1_01", gf100_l1_sources },
			{ 0x02, "gpc00_l1_02", gf100_l1_sources },
			{ 0x03, "gpc00_l1_03", gf100_l1_sources },
			{ 0x05, "gpc00_l1_04", gf100_l1_sources },
			{ 0x06, "gpc00_l1_05", gf100_l1_sources },
			{ 0x0a, "gpc00_tex_00", gf100_tex_sources },
			{ 0x0b, "gpc00_tex_01", gf100_tex_sources },
			{ 0x0c, "gpc00_tex_02", gf100_tex_sources },
			{ 0x0d, "gpc00_tex_03", gf100_tex_sources },
			{ 0x0e, "gpc00_tex_04", gf100_tex_sources },
			{ 0x0e, "gpc00_tex_05", gf100_tex_sources },
			{ 0x0f, "gpc00_tex_06", gf100_tex_sources },
			{ 0x10, "gpc00_tex_07", gf100_tex_sources },
			{ 0x11, "gpc00_tex_08", gf100_tex_sources },
			{ 0x12, "gpc00_tex_09", gf100_tex_sources },
			{ 0x26, "gpc00_unk400_00", gf100_unk400_sources },
			{}
		}, &gf100_perfctr_func },
	{}
};

const struct nvkm_specdom
gf100_pm_part[] = {
	{ 0xe0, (const struct nvkm_specsig[]) {
			{ 0x0f, "part00_pbfb_00", gf100_pbfb_sources },
			{ 0x10, "part00_pbfb_01", gf100_pbfb_sources },
			{ 0x21, "part00_pmfb_00", gf100_pmfb_sources },
			{ 0x04, "part00_pmfb_01", gf100_pmfb_sources },
			{ 0x00, "part00_pmfb_02", gf100_pmfb_sources },
			{ 0x02, "part00_pmfb_03", gf100_pmfb_sources },
			{ 0x01, "part00_pmfb_04", gf100_pmfb_sources },
			{ 0x2e, "part00_pmfb_05", gf100_pmfb_sources },
			{ 0x2f, "part00_pmfb_06", gf100_pmfb_sources },
			{ 0x1b, "part00_pmfb_07", gf100_pmfb_sources },
			{ 0x1c, "part00_pmfb_08", gf100_pmfb_sources },
			{ 0x1d, "part00_pmfb_09", gf100_pmfb_sources },
			{ 0x1e, "part00_pmfb_0a", gf100_pmfb_sources },
			{ 0x1f, "part00_pmfb_0b", gf100_pmfb_sources },
			{}
		}, &gf100_perfctr_func },
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

	for (i = 0; i < 4; i++)
		src |= ctr->signal[i] << (i * 8);

	nv_wr32(priv, dom->addr + 0x09c, 0x00040002 | (dom->mode << 3));
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
	dom->clk = nv_rd32(priv, dom->addr + 0x070);
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

int
gf100_pm_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct gf100_pm_oclass *mclass = (void *)oclass;
	struct gf100_pm_priv *priv;
	u32 mask;
	int ret;

	ret = nvkm_pm_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	/* HUB */
	ret = nvkm_perfdom_new(&priv->base, "hub", 0, 0x1b0000, 0, 0x200,
			       mclass->doms_hub);
	if (ret)
		return ret;

	/* GPC */
	mask  = (1 << nv_rd32(priv, 0x022430)) - 1;
	mask &= ~nv_rd32(priv, 0x022504);
	mask &= ~nv_rd32(priv, 0x022584);

	ret = nvkm_perfdom_new(&priv->base, "gpc", mask, 0x180000,
			       0x1000, 0x200, mclass->doms_gpc);
	if (ret)
		return ret;

	/* PART */
	mask  = (1 << nv_rd32(priv, 0x022438)) - 1;
	mask &= ~nv_rd32(priv, 0x022548);
	mask &= ~nv_rd32(priv, 0x0225c8);

	ret = nvkm_perfdom_new(&priv->base, "part", mask, 0x1a0000,
			       0x1000, 0x200, mclass->doms_part);
	if (ret)
		return ret;

	nv_engine(priv)->cclass = &nvkm_pm_cclass;
	nv_engine(priv)->sclass =  nvkm_pm_sclass;
	return 0;
}

struct nvkm_oclass *
gf100_pm_oclass = &(struct gf100_pm_oclass) {
	.base.handle = NV_ENGINE(PM, 0xc0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_pm_ctor,
		.dtor = _nvkm_pm_dtor,
		.init = _nvkm_pm_init,
		.fini = gf100_pm_fini,
	},
	.doms_gpc  = gf100_pm_gpc,
	.doms_hub  = gf100_pm_hub,
	.doms_part = gf100_pm_part,
}.base;
