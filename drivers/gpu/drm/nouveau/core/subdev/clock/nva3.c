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

struct nva3_clock_priv {
	struct nouveau_clock base;
};

int
nva3_clock_pll_calc(struct nouveau_clock *clock, struct nvbios_pll *info,
		    int clk, struct nouveau_pll_vals *pv)
{
	int ret, N, M, P;

	ret = nva3_pll_calc(nv_subdev(clock), info, clk, &N, NULL, &M, &P);

	if (ret > 0) {
		pv->refclk = info->refclk;
		pv->N1 = N;
		pv->M1 = M;
		pv->log2P = P;
	}
	return ret;
}


static int
nva3_clock_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nva3_clock_priv *priv;
	int ret;

	ret = nouveau_clock_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.pll_calc = nva3_clock_pll_calc;
	return 0;
}

struct nouveau_oclass
nva3_clock_oclass = {
	.handle = NV_SUBDEV(CLOCK, 0xa3),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nva3_clock_ctor,
		.dtor = _nouveau_clock_dtor,
		.init = _nouveau_clock_init,
		.fini = _nouveau_clock_fini,
	},
};
