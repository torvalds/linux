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

#include <subdev/clock.h>
#include <subdev/bios.h>
#include <subdev/bios/pll.h>

#include "pll.h"

struct nvc0_clock_priv {
	struct nouveau_clock base;
};

static int
nvc0_clock_pll_set(struct nouveau_clock *clk, u32 type, u32 freq)
{
	struct nvc0_clock_priv *priv = (void *)clk;
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_pll info;
	int N, fN, M, P;
	int ret;

	ret = nvbios_pll_parse(bios, type, &info);
	if (ret)
		return ret;

	ret = nva3_pll_calc(nv_subdev(clk), &info, freq, &N, &fN, &M, &P);
	if (ret < 0)
		return ret;

	switch (info.type) {
	case PLL_VPLL0:
	case PLL_VPLL1:
	case PLL_VPLL2:
	case PLL_VPLL3:
		nv_mask(priv, info.reg + 0x0c, 0x00000000, 0x00000100);
		nv_wr32(priv, info.reg + 0x04, (P << 16) | (N << 8) | M);
		nv_wr32(priv, info.reg + 0x10, fN << 16);
		break;
	default:
		nv_warn(priv, "0x%08x/%dKhz unimplemented\n", type, freq);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int
nvc0_clock_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvc0_clock_priv *priv;
	int ret;

	ret = nouveau_clock_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.pll_set = nvc0_clock_pll_set;
	priv->base.pll_calc = nva3_clock_pll_calc;
	return 0;
}

struct nouveau_oclass
nvc0_clock_oclass = {
	.handle = NV_SUBDEV(CLOCK, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_clock_ctor,
		.dtor = _nouveau_clock_dtor,
		.init = _nouveau_clock_init,
		.fini = _nouveau_clock_fini,
	},
};
