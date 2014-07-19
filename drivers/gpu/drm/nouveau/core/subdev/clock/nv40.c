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

struct nv40_clock_priv {
	struct nouveau_clock base;
	u32 ctrl;
	u32 npll_ctrl;
	u32 npll_coef;
	u32 spll;
};

static struct nouveau_clocks
nv40_domain[] = {
	{ nv_clk_src_crystal, 0xff },
	{ nv_clk_src_href   , 0xff },
	{ nv_clk_src_core   , 0xff, 0, "core", 1000 },
	{ nv_clk_src_shader , 0xff, 0, "shader", 1000 },
	{ nv_clk_src_mem    , 0xff, 0, "memory", 1000 },
	{ nv_clk_src_max }
};

static u32
read_pll_1(struct nv40_clock_priv *priv, u32 reg)
{
	u32 ctrl = nv_rd32(priv, reg + 0x00);
	int P = (ctrl & 0x00070000) >> 16;
	int N = (ctrl & 0x0000ff00) >> 8;
	int M = (ctrl & 0x000000ff) >> 0;
	u32 ref = 27000, clk = 0;

	if (ctrl & 0x80000000)
		clk = ref * N / M;

	return clk >> P;
}

static u32
read_pll_2(struct nv40_clock_priv *priv, u32 reg)
{
	u32 ctrl = nv_rd32(priv, reg + 0x00);
	u32 coef = nv_rd32(priv, reg + 0x04);
	int N2 = (coef & 0xff000000) >> 24;
	int M2 = (coef & 0x00ff0000) >> 16;
	int N1 = (coef & 0x0000ff00) >> 8;
	int M1 = (coef & 0x000000ff) >> 0;
	int P = (ctrl & 0x00070000) >> 16;
	u32 ref = 27000, clk = 0;

	if ((ctrl & 0x80000000) && M1) {
		clk = ref * N1 / M1;
		if ((ctrl & 0x40000100) == 0x40000000) {
			if (M2)
				clk = clk * N2 / M2;
			else
				clk = 0;
		}
	}

	return clk >> P;
}

static u32
read_clk(struct nv40_clock_priv *priv, u32 src)
{
	switch (src) {
	case 3:
		return read_pll_2(priv, 0x004000);
	case 2:
		return read_pll_1(priv, 0x004008);
	default:
		break;
	}

	return 0;
}

static int
nv40_clock_read(struct nouveau_clock *clk, enum nv_clk_src src)
{
	struct nv40_clock_priv *priv = (void *)clk;
	u32 mast = nv_rd32(priv, 0x00c040);

	switch (src) {
	case nv_clk_src_crystal:
		return nv_device(priv)->crystal;
	case nv_clk_src_href:
		return 100000; /*XXX: PCIE/AGP differ*/
	case nv_clk_src_core:
		return read_clk(priv, (mast & 0x00000003) >> 0);
	case nv_clk_src_shader:
		return read_clk(priv, (mast & 0x00000030) >> 4);
	case nv_clk_src_mem:
		return read_pll_2(priv, 0x4020);
	default:
		break;
	}

	nv_debug(priv, "unknown clock source %d 0x%08x\n", src, mast);
	return -EINVAL;
}

static int
nv40_clock_calc_pll(struct nv40_clock_priv *priv, u32 reg, u32 clk,
		    int *N1, int *M1, int *N2, int *M2, int *log2P)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_pll pll;
	int ret;

	ret = nvbios_pll_parse(bios, reg, &pll);
	if (ret)
		return ret;

	if (clk < pll.vco1.max_freq)
		pll.vco2.max_freq = 0;

	ret = nv04_pll_calc(nv_subdev(priv), &pll, clk, N1, M1, N2, M2, log2P);
	if (ret == 0)
		return -ERANGE;
	return ret;
}

static int
nv40_clock_calc(struct nouveau_clock *clk, struct nouveau_cstate *cstate)
{
	struct nv40_clock_priv *priv = (void *)clk;
	int gclk = cstate->domain[nv_clk_src_core];
	int sclk = cstate->domain[nv_clk_src_shader];
	int N1, M1, N2, M2, log2P;
	int ret;

	/* core/geometric clock */
	ret = nv40_clock_calc_pll(priv, 0x004000, gclk,
				 &N1, &M1, &N2, &M2, &log2P);
	if (ret < 0)
		return ret;

	if (N2 == M2) {
		priv->npll_ctrl = 0x80000100 | (log2P << 16);
		priv->npll_coef = (N1 << 8) | M1;
	} else {
		priv->npll_ctrl = 0xc0000000 | (log2P << 16);
		priv->npll_coef = (N2 << 24) | (M2 << 16) | (N1 << 8) | M1;
	}

	/* use the second pll for shader/rop clock, if it differs from core */
	if (sclk && sclk != gclk) {
		ret = nv40_clock_calc_pll(priv, 0x004008, sclk,
					 &N1, &M1, NULL, NULL, &log2P);
		if (ret < 0)
			return ret;

		priv->spll = 0xc0000000 | (log2P << 16) | (N1 << 8) | M1;
		priv->ctrl = 0x00000223;
	} else {
		priv->spll = 0x00000000;
		priv->ctrl = 0x00000333;
	}

	return 0;
}

static int
nv40_clock_prog(struct nouveau_clock *clk)
{
	struct nv40_clock_priv *priv = (void *)clk;
	nv_mask(priv, 0x00c040, 0x00000333, 0x00000000);
	nv_wr32(priv, 0x004004, priv->npll_coef);
	nv_mask(priv, 0x004000, 0xc0070100, priv->npll_ctrl);
	nv_mask(priv, 0x004008, 0xc007ffff, priv->spll);
	mdelay(5);
	nv_mask(priv, 0x00c040, 0x00000333, priv->ctrl);
	return 0;
}

static void
nv40_clock_tidy(struct nouveau_clock *clk)
{
}

static int
nv40_clock_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nv40_clock_priv *priv;
	int ret;

	ret = nouveau_clock_create(parent, engine, oclass, nv40_domain, true,
				   &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.pll_calc = nv04_clock_pll_calc;
	priv->base.pll_prog = nv04_clock_pll_prog;
	priv->base.read = nv40_clock_read;
	priv->base.calc = nv40_clock_calc;
	priv->base.prog = nv40_clock_prog;
	priv->base.tidy = nv40_clock_tidy;
	return 0;
}

struct nouveau_oclass
nv40_clock_oclass = {
	.handle = NV_SUBDEV(CLOCK, 0x40),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv40_clock_ctor,
		.dtor = _nouveau_clock_dtor,
		.init = _nouveau_clock_init,
		.fini = _nouveau_clock_fini,
	},
};
