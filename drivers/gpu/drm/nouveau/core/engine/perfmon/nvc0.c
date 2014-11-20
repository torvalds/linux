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

#include "nvc0.h"

/*******************************************************************************
 * Perfmon object classes
 ******************************************************************************/

/*******************************************************************************
 * PPM context
 ******************************************************************************/

/*******************************************************************************
 * PPM engine/subdev functions
 ******************************************************************************/

static const struct nouveau_specdom
nvc0_perfmon_hub[] = {
	{}
};

static const struct nouveau_specdom
nvc0_perfmon_gpc[] = {
	{}
};

static const struct nouveau_specdom
nvc0_perfmon_part[] = {
	{}
};

static void
nvc0_perfctr_init(struct nouveau_perfmon *ppm, struct nouveau_perfdom *dom,
		  struct nouveau_perfctr *ctr)
{
	struct nvc0_perfmon_priv *priv = (void *)ppm;
	struct nvc0_perfmon_cntr *cntr = (void *)ctr;
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
nvc0_perfctr_read(struct nouveau_perfmon *ppm, struct nouveau_perfdom *dom,
		  struct nouveau_perfctr *ctr)
{
	struct nvc0_perfmon_priv *priv = (void *)ppm;
	struct nvc0_perfmon_cntr *cntr = (void *)ctr;

	switch (cntr->base.slot) {
	case 0: cntr->base.ctr = nv_rd32(priv, dom->addr + 0x08c); break;
	case 1: cntr->base.ctr = nv_rd32(priv, dom->addr + 0x088); break;
	case 2: cntr->base.ctr = nv_rd32(priv, dom->addr + 0x080); break;
	case 3: cntr->base.ctr = nv_rd32(priv, dom->addr + 0x090); break;
	}
	cntr->base.clk = nv_rd32(priv, dom->addr + 0x070);
}

static void
nvc0_perfctr_next(struct nouveau_perfmon *ppm, struct nouveau_perfdom *dom)
{
	struct nvc0_perfmon_priv *priv = (void *)ppm;
	nv_wr32(priv, dom->addr + 0x06c, dom->signal_nr - 0x40 + 0x27);
	nv_wr32(priv, dom->addr + 0x0ec, 0x00000011);
}

const struct nouveau_funcdom
nvc0_perfctr_func = {
	.init = nvc0_perfctr_init,
	.read = nvc0_perfctr_read,
	.next = nvc0_perfctr_next,
};

int
nvc0_perfmon_fini(struct nouveau_object *object, bool suspend)
{
	struct nvc0_perfmon_priv *priv = (void *)object;
	nv_mask(priv, 0x000200, 0x10000000, 0x00000000);
	nv_mask(priv, 0x000200, 0x10000000, 0x10000000);
	return nouveau_perfmon_fini(&priv->base, suspend);
}

static int
nvc0_perfmon_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nvc0_perfmon_priv *priv;
	u32 mask;
	int ret;

	ret = nouveau_perfmon_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nouveau_perfdom_new(&priv->base, "pwr", 0, 0, 0, 0,
				   nvc0_perfmon_pwr);
	if (ret)
		return ret;

	/* HUB */
	ret = nouveau_perfdom_new(&priv->base, "hub", 0, 0x1b0000, 0, 0x200,
				   nvc0_perfmon_hub);
	if (ret)
		return ret;

	/* GPC */
	mask  = (1 << nv_rd32(priv, 0x022430)) - 1;
	mask &= ~nv_rd32(priv, 0x022504);
	mask &= ~nv_rd32(priv, 0x022584);

	ret = nouveau_perfdom_new(&priv->base, "gpc", mask, 0x180000,
				  0x1000, 0x200, nvc0_perfmon_gpc);
	if (ret)
		return ret;

	/* PART */
	mask  = (1 << nv_rd32(priv, 0x022438)) - 1;
	mask &= ~nv_rd32(priv, 0x022548);
	mask &= ~nv_rd32(priv, 0x0225c8);

	ret = nouveau_perfdom_new(&priv->base, "part", mask, 0x1a0000,
				  0x1000, 0x200, nvc0_perfmon_part);
	if (ret)
		return ret;

	nv_engine(priv)->cclass = &nouveau_perfmon_cclass;
	nv_engine(priv)->sclass =  nouveau_perfmon_sclass;
	priv->base.last = 7;
	return 0;
}

struct nouveau_oclass
nvc0_perfmon_oclass = {
	.handle = NV_ENGINE(PERFMON, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_perfmon_ctor,
		.dtor = _nouveau_perfmon_dtor,
		.init = _nouveau_perfmon_init,
		.fini = nvc0_perfmon_fini,
	},
};
