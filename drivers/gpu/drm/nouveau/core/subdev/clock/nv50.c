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

struct nv50_clock_priv {
	struct nouveau_clock base;
};

static int
nv50_clock_pll_set(struct nouveau_clock *clk, u32 type, u32 freq)
{
	struct nv50_clock_priv *priv = (void *)clk;
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_pll info;
	int N1, M1, N2, M2, P;
	int ret;

	ret = nvbios_pll_parse(bios, type, &info);
	if (ret) {
		nv_error(clk, "failed to retrieve pll data, %d\n", ret);
		return ret;
	}

	ret = nv04_pll_calc(nv_subdev(clk), &info, freq, &N1, &M1, &N2, &M2, &P);
	if (!ret) {
		nv_error(clk, "failed pll calculation\n");
		return ret;
	}

	switch (info.type) {
	case PLL_VPLL0:
	case PLL_VPLL1:
		nv_wr32(priv, info.reg + 0, 0x10000611);
		nv_mask(priv, info.reg + 4, 0x00ff00ff, (M1 << 16) | N1);
		nv_mask(priv, info.reg + 8, 0x7fff00ff, (P  << 28) |
							(M2 << 16) | N2);
		break;
	case PLL_MEMORY:
		nv_mask(priv, info.reg + 0, 0x01ff0000, (P << 22) |
						        (info.bias_p << 19) |
							(P << 16));
		nv_wr32(priv, info.reg + 4, (N1 << 8) | M1);
		break;
	default:
		nv_mask(priv, info.reg + 0, 0x00070000, (P << 16));
		nv_wr32(priv, info.reg + 4, (N1 << 8) | M1);
		break;
	}

	return 0;
}

static int
nv50_clock_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nv50_clock_priv *priv;
	int ret;

	ret = nouveau_clock_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.pll_set = nv50_clock_pll_set;
	priv->base.pll_calc = nv04_clock_pll_calc;
	return 0;
}

struct nouveau_oclass
nv50_clock_oclass = {
	.handle = NV_SUBDEV(CLOCK, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_clock_ctor,
		.dtor = _nouveau_clock_dtor,
		.init = _nouveau_clock_init,
		.fini = _nouveau_clock_fini,
	},
};
