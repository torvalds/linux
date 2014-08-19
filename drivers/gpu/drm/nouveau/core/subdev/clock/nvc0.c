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
#include <subdev/timer.h>

#include "pll.h"

struct nvc0_clock_info {
	u32 freq;
	u32 ssel;
	u32 mdiv;
	u32 dsrc;
	u32 ddiv;
	u32 coef;
};

struct nvc0_clock_priv {
	struct nouveau_clock base;
	struct nvc0_clock_info eng[16];
};

static u32 read_div(struct nvc0_clock_priv *, int, u32, u32);

static u32
read_vco(struct nvc0_clock_priv *priv, u32 dsrc)
{
	struct nouveau_clock *clk = &priv->base;
	u32 ssrc = nv_rd32(priv, dsrc);
	if (!(ssrc & 0x00000100))
		return clk->read(clk, nv_clk_src_sppll0);
	return clk->read(clk, nv_clk_src_sppll1);
}

static u32
read_pll(struct nvc0_clock_priv *priv, u32 pll)
{
	struct nouveau_clock *clk = &priv->base;
	u32 ctrl = nv_rd32(priv, pll + 0x00);
	u32 coef = nv_rd32(priv, pll + 0x04);
	u32 P = (coef & 0x003f0000) >> 16;
	u32 N = (coef & 0x0000ff00) >> 8;
	u32 M = (coef & 0x000000ff) >> 0;
	u32 sclk;

	if (!(ctrl & 0x00000001))
		return 0;

	switch (pll) {
	case 0x00e800:
	case 0x00e820:
		sclk = nv_device(priv)->crystal;
		P = 1;
		break;
	case 0x132000:
		sclk = clk->read(clk, nv_clk_src_mpllsrc);
		break;
	case 0x132020:
		sclk = clk->read(clk, nv_clk_src_mpllsrcref);
		break;
	case 0x137000:
	case 0x137020:
	case 0x137040:
	case 0x1370e0:
		sclk = read_div(priv, (pll & 0xff) / 0x20, 0x137120, 0x137140);
		break;
	default:
		return 0;
	}

	return sclk * N / M / P;
}

static u32
read_div(struct nvc0_clock_priv *priv, int doff, u32 dsrc, u32 dctl)
{
	u32 ssrc = nv_rd32(priv, dsrc + (doff * 4));
	u32 sctl = nv_rd32(priv, dctl + (doff * 4));

	switch (ssrc & 0x00000003) {
	case 0:
		if ((ssrc & 0x00030000) != 0x00030000)
			return nv_device(priv)->crystal;
		return 108000;
	case 2:
		return 100000;
	case 3:
		if (sctl & 0x80000000) {
			u32 sclk = read_vco(priv, dsrc + (doff * 4));
			u32 sdiv = (sctl & 0x0000003f) + 2;
			return (sclk * 2) / sdiv;
		}

		return read_vco(priv, dsrc + (doff * 4));
	default:
		return 0;
	}
}

static u32
read_clk(struct nvc0_clock_priv *priv, int clk)
{
	u32 sctl = nv_rd32(priv, 0x137250 + (clk * 4));
	u32 ssel = nv_rd32(priv, 0x137100);
	u32 sclk, sdiv;

	if (ssel & (1 << clk)) {
		if (clk < 7)
			sclk = read_pll(priv, 0x137000 + (clk * 0x20));
		else
			sclk = read_pll(priv, 0x1370e0);
		sdiv = ((sctl & 0x00003f00) >> 8) + 2;
	} else {
		sclk = read_div(priv, clk, 0x137160, 0x1371d0);
		sdiv = ((sctl & 0x0000003f) >> 0) + 2;
	}

	if (sctl & 0x80000000)
		return (sclk * 2) / sdiv;

	return sclk;
}

static int
nvc0_clock_read(struct nouveau_clock *clk, enum nv_clk_src src)
{
	struct nouveau_device *device = nv_device(clk);
	struct nvc0_clock_priv *priv = (void *)clk;

	switch (src) {
	case nv_clk_src_crystal:
		return device->crystal;
	case nv_clk_src_href:
		return 100000;
	case nv_clk_src_sppll0:
		return read_pll(priv, 0x00e800);
	case nv_clk_src_sppll1:
		return read_pll(priv, 0x00e820);

	case nv_clk_src_mpllsrcref:
		return read_div(priv, 0, 0x137320, 0x137330);
	case nv_clk_src_mpllsrc:
		return read_pll(priv, 0x132020);
	case nv_clk_src_mpll:
		return read_pll(priv, 0x132000);
	case nv_clk_src_mdiv:
		return read_div(priv, 0, 0x137300, 0x137310);
	case nv_clk_src_mem:
		if (nv_rd32(priv, 0x1373f0) & 0x00000002)
			return clk->read(clk, nv_clk_src_mpll);
		return clk->read(clk, nv_clk_src_mdiv);

	case nv_clk_src_gpc:
		return read_clk(priv, 0x00);
	case nv_clk_src_rop:
		return read_clk(priv, 0x01);
	case nv_clk_src_hubk07:
		return read_clk(priv, 0x02);
	case nv_clk_src_hubk06:
		return read_clk(priv, 0x07);
	case nv_clk_src_hubk01:
		return read_clk(priv, 0x08);
	case nv_clk_src_copy:
		return read_clk(priv, 0x09);
	case nv_clk_src_daemon:
		return read_clk(priv, 0x0c);
	case nv_clk_src_vdec:
		return read_clk(priv, 0x0e);
	default:
		nv_error(clk, "invalid clock source %d\n", src);
		return -EINVAL;
	}
}

static u32
calc_div(struct nvc0_clock_priv *priv, int clk, u32 ref, u32 freq, u32 *ddiv)
{
	u32 div = min((ref * 2) / freq, (u32)65);
	if (div < 2)
		div = 2;

	*ddiv = div - 2;
	return (ref * 2) / div;
}

static u32
calc_src(struct nvc0_clock_priv *priv, int clk, u32 freq, u32 *dsrc, u32 *ddiv)
{
	u32 sclk;

	/* use one of the fixed frequencies if possible */
	*ddiv = 0x00000000;
	switch (freq) {
	case  27000:
	case 108000:
		*dsrc = 0x00000000;
		if (freq == 108000)
			*dsrc |= 0x00030000;
		return freq;
	case 100000:
		*dsrc = 0x00000002;
		return freq;
	default:
		*dsrc = 0x00000003;
		break;
	}

	/* otherwise, calculate the closest divider */
	sclk = read_vco(priv, 0x137160 + (clk * 4));
	if (clk < 7)
		sclk = calc_div(priv, clk, sclk, freq, ddiv);
	return sclk;
}

static u32
calc_pll(struct nvc0_clock_priv *priv, int clk, u32 freq, u32 *coef)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_pll limits;
	int N, M, P, ret;

	ret = nvbios_pll_parse(bios, 0x137000 + (clk * 0x20), &limits);
	if (ret)
		return 0;

	limits.refclk = read_div(priv, clk, 0x137120, 0x137140);
	if (!limits.refclk)
		return 0;

	ret = nva3_pll_calc(nv_subdev(priv), &limits, freq, &N, NULL, &M, &P);
	if (ret <= 0)
		return 0;

	*coef = (P << 16) | (N << 8) | M;
	return ret;
}

static int
calc_clk(struct nvc0_clock_priv *priv,
	 struct nouveau_cstate *cstate, int clk, int dom)
{
	struct nvc0_clock_info *info = &priv->eng[clk];
	u32 freq = cstate->domain[dom];
	u32 src0, div0, div1D, div1P = 0;
	u32 clk0, clk1 = 0;

	/* invalid clock domain */
	if (!freq)
		return 0;

	/* first possible path, using only dividers */
	clk0 = calc_src(priv, clk, freq, &src0, &div0);
	clk0 = calc_div(priv, clk, clk0, freq, &div1D);

	/* see if we can get any closer using PLLs */
	if (clk0 != freq && (0x00004387 & (1 << clk))) {
		if (clk <= 7)
			clk1 = calc_pll(priv, clk, freq, &info->coef);
		else
			clk1 = cstate->domain[nv_clk_src_hubk06];
		clk1 = calc_div(priv, clk, clk1, freq, &div1P);
	}

	/* select the method which gets closest to target freq */
	if (abs((int)freq - clk0) <= abs((int)freq - clk1)) {
		info->dsrc = src0;
		if (div0) {
			info->ddiv |= 0x80000000;
			info->ddiv |= div0 << 8;
			info->ddiv |= div0;
		}
		if (div1D) {
			info->mdiv |= 0x80000000;
			info->mdiv |= div1D;
		}
		info->ssel = info->coef = 0;
		info->freq = clk0;
	} else {
		if (div1P) {
			info->mdiv |= 0x80000000;
			info->mdiv |= div1P << 8;
		}
		info->ssel = (1 << clk);
		info->freq = clk1;
	}

	return 0;
}

static int
nvc0_clock_calc(struct nouveau_clock *clk, struct nouveau_cstate *cstate)
{
	struct nvc0_clock_priv *priv = (void *)clk;
	int ret;

	if ((ret = calc_clk(priv, cstate, 0x00, nv_clk_src_gpc)) ||
	    (ret = calc_clk(priv, cstate, 0x01, nv_clk_src_rop)) ||
	    (ret = calc_clk(priv, cstate, 0x02, nv_clk_src_hubk07)) ||
	    (ret = calc_clk(priv, cstate, 0x07, nv_clk_src_hubk06)) ||
	    (ret = calc_clk(priv, cstate, 0x08, nv_clk_src_hubk01)) ||
	    (ret = calc_clk(priv, cstate, 0x09, nv_clk_src_copy)) ||
	    (ret = calc_clk(priv, cstate, 0x0c, nv_clk_src_daemon)) ||
	    (ret = calc_clk(priv, cstate, 0x0e, nv_clk_src_vdec)))
		return ret;

	return 0;
}

static void
nvc0_clock_prog_0(struct nvc0_clock_priv *priv, int clk)
{
	struct nvc0_clock_info *info = &priv->eng[clk];
	if (clk < 7 && !info->ssel) {
		nv_mask(priv, 0x1371d0 + (clk * 0x04), 0x80003f3f, info->ddiv);
		nv_wr32(priv, 0x137160 + (clk * 0x04), info->dsrc);
	}
}

static void
nvc0_clock_prog_1(struct nvc0_clock_priv *priv, int clk)
{
	nv_mask(priv, 0x137100, (1 << clk), 0x00000000);
	nv_wait(priv, 0x137100, (1 << clk), 0x00000000);
}

static void
nvc0_clock_prog_2(struct nvc0_clock_priv *priv, int clk)
{
	struct nvc0_clock_info *info = &priv->eng[clk];
	const u32 addr = 0x137000 + (clk * 0x20);
	if (clk <= 7) {
		nv_mask(priv, addr + 0x00, 0x00000004, 0x00000000);
		nv_mask(priv, addr + 0x00, 0x00000001, 0x00000000);
		if (info->coef) {
			nv_wr32(priv, addr + 0x04, info->coef);
			nv_mask(priv, addr + 0x00, 0x00000001, 0x00000001);
			nv_wait(priv, addr + 0x00, 0x00020000, 0x00020000);
			nv_mask(priv, addr + 0x00, 0x00020004, 0x00000004);
		}
	}
}

static void
nvc0_clock_prog_3(struct nvc0_clock_priv *priv, int clk)
{
	struct nvc0_clock_info *info = &priv->eng[clk];
	if (info->ssel) {
		nv_mask(priv, 0x137100, (1 << clk), info->ssel);
		nv_wait(priv, 0x137100, (1 << clk), info->ssel);
	}
}

static void
nvc0_clock_prog_4(struct nvc0_clock_priv *priv, int clk)
{
	struct nvc0_clock_info *info = &priv->eng[clk];
	nv_mask(priv, 0x137250 + (clk * 0x04), 0x00003f3f, info->mdiv);
}

static int
nvc0_clock_prog(struct nouveau_clock *clk)
{
	struct nvc0_clock_priv *priv = (void *)clk;
	struct {
		void (*exec)(struct nvc0_clock_priv *, int);
	} stage[] = {
		{ nvc0_clock_prog_0 }, /* div programming */
		{ nvc0_clock_prog_1 }, /* select div mode */
		{ nvc0_clock_prog_2 }, /* (maybe) program pll */
		{ nvc0_clock_prog_3 }, /* (maybe) select pll mode */
		{ nvc0_clock_prog_4 }, /* final divider */
	};
	int i, j;

	for (i = 0; i < ARRAY_SIZE(stage); i++) {
		for (j = 0; j < ARRAY_SIZE(priv->eng); j++) {
			if (!priv->eng[j].freq)
				continue;
			stage[i].exec(priv, j);
		}
	}

	return 0;
}

static void
nvc0_clock_tidy(struct nouveau_clock *clk)
{
	struct nvc0_clock_priv *priv = (void *)clk;
	memset(priv->eng, 0x00, sizeof(priv->eng));
}

static struct nouveau_clocks
nvc0_domain[] = {
	{ nv_clk_src_crystal, 0xff },
	{ nv_clk_src_href   , 0xff },
	{ nv_clk_src_hubk06 , 0x00 },
	{ nv_clk_src_hubk01 , 0x01 },
	{ nv_clk_src_copy   , 0x02 },
	{ nv_clk_src_gpc    , 0x03, 0, "core", 2000 },
	{ nv_clk_src_rop    , 0x04 },
	{ nv_clk_src_mem    , 0x05, 0, "memory", 1000 },
	{ nv_clk_src_vdec   , 0x06 },
	{ nv_clk_src_daemon , 0x0a },
	{ nv_clk_src_hubk07 , 0x0b },
	{ nv_clk_src_max }
};

static int
nvc0_clock_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvc0_clock_priv *priv;
	int ret;

	ret = nouveau_clock_create(parent, engine, oclass, nvc0_domain, NULL, 0,
				   false, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.read = nvc0_clock_read;
	priv->base.calc = nvc0_clock_calc;
	priv->base.prog = nvc0_clock_prog;
	priv->base.tidy = nvc0_clock_tidy;
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
