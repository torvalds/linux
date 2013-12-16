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

#include <subdev/bios.h>
#include <subdev/bios/pll.h>
#include <subdev/clock.h>
#include <subdev/devinit/priv.h>

#include "pll.h"

struct nv04_clock_priv {
	struct nouveau_clock base;
};

int
nv04_clock_pll_calc(struct nouveau_clock *clock, struct nvbios_pll *info,
		    int clk, struct nouveau_pll_vals *pv)
{
	int N1, M1, N2, M2, P;
	int ret = nv04_pll_calc(nv_subdev(clock), info, clk, &N1, &M1, &N2, &M2, &P);
	if (ret) {
		pv->refclk = info->refclk;
		pv->N1 = N1;
		pv->M1 = M1;
		pv->N2 = N2;
		pv->M2 = M2;
		pv->log2P = P;
	}
	return ret;
}

int
nv04_clock_pll_prog(struct nouveau_clock *clk, u32 reg1,
		    struct nouveau_pll_vals *pv)
{
	struct nouveau_devinit *devinit = nouveau_devinit(clk);
	int cv = nouveau_bios(clk)->version.chip;

	if (cv == 0x30 || cv == 0x31 || cv == 0x35 || cv == 0x36 ||
	    cv >= 0x40) {
		if (reg1 > 0x405c)
			setPLL_double_highregs(devinit, reg1, pv);
		else
			setPLL_double_lowregs(devinit, reg1, pv);
	} else
		setPLL_single(devinit, reg1, pv);

	return 0;
}

static int
nv04_clock_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nv04_clock_priv *priv;
	int ret;

	ret = nouveau_clock_create(parent, engine, oclass, NULL, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.pll_calc = nv04_clock_pll_calc;
	priv->base.pll_prog = nv04_clock_pll_prog;
	return 0;
}

struct nouveau_oclass
nv04_clock_oclass = {
	.handle = NV_SUBDEV(CLOCK, 0x04),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_clock_ctor,
		.dtor = _nouveau_clock_dtor,
		.init = _nouveau_clock_init,
		.fini = _nouveau_clock_fini,
	},
};
