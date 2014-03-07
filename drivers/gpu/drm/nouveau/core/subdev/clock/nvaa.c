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

#include <engine/fifo.h>
#include <subdev/bios.h>
#include <subdev/bios/pll.h>
#include <subdev/timer.h>
#include <subdev/clock.h>

#include "pll.h"

struct nvaa_clock_priv {
	struct nouveau_clock base;
	enum nv_clk_src csrc, ssrc, vsrc;
	u32 cctrl, sctrl;
	u32 ccoef, scoef;
	u32 cpost, spost;
	u32 vdiv;
};

static u32
read_div(struct nouveau_clock *clk)
{
	return nv_rd32(clk, 0x004600);
}

static u32
read_pll(struct nouveau_clock *clk, u32 base)
{
	u32 ctrl = nv_rd32(clk, base + 0);
	u32 coef = nv_rd32(clk, base + 4);
	u32 ref = clk->read(clk, nv_clk_src_href);
	u32 post_div = 0;
	u32 clock = 0;
	int N1, M1;

	switch (base){
	case 0x4020:
		post_div = 1 << ((nv_rd32(clk, 0x4070) & 0x000f0000) >> 16);
		break;
	case 0x4028:
		post_div = (nv_rd32(clk, 0x4040) & 0x000f0000) >> 16;
		break;
	default:
		break;
	}

	N1 = (coef & 0x0000ff00) >> 8;
	M1 = (coef & 0x000000ff);
	if ((ctrl & 0x80000000) && M1) {
		clock = ref * N1 / M1;
		clock = clock / post_div;
	}

	return clock;
}

static int
nvaa_clock_read(struct nouveau_clock *clk, enum nv_clk_src src)
{
	struct nvaa_clock_priv *priv = (void *)clk;
	u32 mast = nv_rd32(clk, 0x00c054);
	u32 P = 0;

	switch (src) {
	case nv_clk_src_crystal:
		return nv_device(priv)->crystal;
	case nv_clk_src_href:
		return 100000; /* PCIE reference clock */
	case nv_clk_src_hclkm4:
		return clk->read(clk, nv_clk_src_href) * 4;
	case nv_clk_src_hclkm2d3:
		return clk->read(clk, nv_clk_src_href) * 2 / 3;
	case nv_clk_src_host:
		switch (mast & 0x000c0000) {
		case 0x00000000: return clk->read(clk, nv_clk_src_hclkm2d3);
		case 0x00040000: break;
		case 0x00080000: return clk->read(clk, nv_clk_src_hclkm4);
		case 0x000c0000: return clk->read(clk, nv_clk_src_cclk);
		}
		break;
	case nv_clk_src_core:
		P = (nv_rd32(clk, 0x004028) & 0x00070000) >> 16;

		switch (mast & 0x00000003) {
		case 0x00000000: return clk->read(clk, nv_clk_src_crystal) >> P;
		case 0x00000001: return 0;
		case 0x00000002: return clk->read(clk, nv_clk_src_hclkm4) >> P;
		case 0x00000003: return read_pll(clk, 0x004028) >> P;
		}
		break;
	case nv_clk_src_cclk:
		if ((mast & 0x03000000) != 0x03000000)
			return clk->read(clk, nv_clk_src_core);

		if ((mast & 0x00000200) == 0x00000000)
			return clk->read(clk, nv_clk_src_core);

		switch (mast & 0x00000c00) {
		case 0x00000000: return clk->read(clk, nv_clk_src_href);
		case 0x00000400: return clk->read(clk, nv_clk_src_hclkm4);
		case 0x00000800: return clk->read(clk, nv_clk_src_hclkm2d3);
		default: return 0;
		}
	case nv_clk_src_shader:
		P = (nv_rd32(clk, 0x004020) & 0x00070000) >> 16;
		switch (mast & 0x00000030) {
		case 0x00000000:
			if (mast & 0x00000040)
				return clk->read(clk, nv_clk_src_href) >> P;
			return clk->read(clk, nv_clk_src_crystal) >> P;
		case 0x00000010: break;
		case 0x00000020: return read_pll(clk, 0x004028) >> P;
		case 0x00000030: return read_pll(clk, 0x004020) >> P;
		}
		break;
	case nv_clk_src_mem:
		return 0;
		break;
	case nv_clk_src_vdec:
		P = (read_div(clk) & 0x00000700) >> 8;

		switch (mast & 0x00400000) {
		case 0x00400000:
			return clk->read(clk, nv_clk_src_core) >> P;
			break;
		default:
			return 500000 >> P;
			break;
		}
		break;
	default:
		break;
	}

	nv_debug(priv, "unknown clock source %d 0x%08x\n", src, mast);
	return 0;
}

static u32
calc_pll(struct nvaa_clock_priv *priv, u32 reg,
	 u32 clock, int *N, int *M, int *P)
{
	struct nouveau_bios *bios = nouveau_bios(priv);
	struct nvbios_pll pll;
	struct nouveau_clock *clk = &priv->base;
	int ret;

	ret = nvbios_pll_parse(bios, reg, &pll);
	if (ret)
		return 0;

	pll.vco2.max_freq = 0;
	pll.refclk = clk->read(clk, nv_clk_src_href);
	if (!pll.refclk)
		return 0;

	return nv04_pll_calc(nv_subdev(priv), &pll, clock, N, M, NULL, NULL, P);
}

static inline u32
calc_P(u32 src, u32 target, int *div)
{
	u32 clk0 = src, clk1 = src;
	for (*div = 0; *div <= 7; (*div)++) {
		if (clk0 <= target) {
			clk1 = clk0 << (*div ? 1 : 0);
			break;
		}
		clk0 >>= 1;
	}

	if (target - clk0 <= clk1 - target)
		return clk0;
	(*div)--;
	return clk1;
}

static int
nvaa_clock_calc(struct nouveau_clock *clk, struct nouveau_cstate *cstate)
{
	struct nvaa_clock_priv *priv = (void *)clk;
	const int shader = cstate->domain[nv_clk_src_shader];
	const int core = cstate->domain[nv_clk_src_core];
	const int vdec = cstate->domain[nv_clk_src_vdec];
	u32 out = 0, clock = 0;
	int N, M, P1, P2 = 0;
	int divs = 0;

	/* cclk: find suitable source, disable PLL if we can */
	if (core < clk->read(clk, nv_clk_src_hclkm4))
		out = calc_P(clk->read(clk, nv_clk_src_hclkm4), core, &divs);

	/* Calculate clock * 2, so shader clock can use it too */
	clock = calc_pll(priv, 0x4028, (core << 1), &N, &M, &P1);

	if (abs(core - out) <=
	    abs(core - (clock >> 1))) {
		priv->csrc = nv_clk_src_hclkm4;
		priv->cctrl = divs << 16;
	} else {
		/* NVCTRL is actually used _after_ NVPOST, and after what we
		 * call NVPLL. To make matters worse, NVPOST is an integer
		 * divider instead of a right-shift number. */
		if(P1 > 2) {
			P2 = P1 - 2;
			P1 = 2;
		}

		priv->csrc = nv_clk_src_core;
		priv->ccoef = (N << 8) | M;

		priv->cctrl = (P2 + 1) << 16;
		priv->cpost = (1 << P1) << 16;
	}

	/* sclk: nvpll + divisor, href or spll */
	out = 0;
	if (shader == clk->read(clk, nv_clk_src_href)) {
		priv->ssrc = nv_clk_src_href;
	} else {
		clock = calc_pll(priv, 0x4020, shader, &N, &M, &P1);
		if (priv->csrc == nv_clk_src_core) {
			out = calc_P((core << 1), shader, &divs);
		}

		if (abs(shader - out) <=
		    abs(shader - clock) &&
		   (divs + P2) <= 7) {
			priv->ssrc = nv_clk_src_core;
			priv->sctrl = (divs + P2) << 16;
		} else {
			priv->ssrc = nv_clk_src_shader;
			priv->scoef = (N << 8) | M;
			priv->sctrl = P1 << 16;
		}
	}

	/* vclk */
	out = calc_P(core, vdec, &divs);
	clock = calc_P(500000, vdec, &P1);
	if(abs(vdec - out) <=
	   abs(vdec - clock)) {
		priv->vsrc = nv_clk_src_cclk;
		priv->vdiv = divs << 16;
	} else {
		priv->vsrc = nv_clk_src_vdec;
		priv->vdiv = P1 << 16;
	}

	/* Print strategy! */
	nv_debug(priv, "nvpll: %08x %08x %08x\n",
			priv->ccoef, priv->cpost, priv->cctrl);
	nv_debug(priv, " spll: %08x %08x %08x\n",
			priv->scoef, priv->spost, priv->sctrl);
	nv_debug(priv, " vdiv: %08x\n", priv->vdiv);
	if (priv->csrc == nv_clk_src_hclkm4)
		nv_debug(priv, "core: hrefm4\n");
	else
		nv_debug(priv, "core: nvpll\n");

	if (priv->ssrc == nv_clk_src_hclkm4)
		nv_debug(priv, "shader: hrefm4\n");
	else if (priv->ssrc == nv_clk_src_core)
		nv_debug(priv, "shader: nvpll\n");
	else
		nv_debug(priv, "shader: spll\n");

	if (priv->vsrc == nv_clk_src_hclkm4)
		nv_debug(priv, "vdec: 500MHz\n");
	else
		nv_debug(priv, "vdec: core\n");

	return 0;
}

static int
nvaa_clock_prog(struct nouveau_clock *clk)
{
	struct nvaa_clock_priv *priv = (void *)clk;
	struct nouveau_fifo *pfifo = nouveau_fifo(clk);
	unsigned long flags;
	u32 pllmask = 0, mast, ptherm_gate;
	int ret = -EBUSY;

	/* halt and idle execution engines */
	ptherm_gate = nv_mask(clk, 0x020060, 0x00070000, 0x00000000);
	nv_mask(clk, 0x002504, 0x00000001, 0x00000001);
	/* Wait until the interrupt handler is finished */
	if (!nv_wait(clk, 0x000100, 0xffffffff, 0x00000000))
		goto resume;

	if (pfifo)
		pfifo->pause(pfifo, &flags);

	if (!nv_wait(clk, 0x002504, 0x00000010, 0x00000010))
		goto resume;
	if (!nv_wait(clk, 0x00251c, 0x0000003f, 0x0000003f))
		goto resume;

	/* First switch to safe clocks: href */
	mast = nv_mask(clk, 0xc054, 0x03400e70, 0x03400640);
	mast &= ~0x00400e73;
	mast |= 0x03000000;

	switch (priv->csrc) {
	case nv_clk_src_hclkm4:
		nv_mask(clk, 0x4028, 0x00070000, priv->cctrl);
		mast |= 0x00000002;
		break;
	case nv_clk_src_core:
		nv_wr32(clk, 0x402c, priv->ccoef);
		nv_wr32(clk, 0x4028, 0x80000000 | priv->cctrl);
		nv_wr32(clk, 0x4040, priv->cpost);
		pllmask |= (0x3 << 8);
		mast |= 0x00000003;
		break;
	default:
		nv_warn(priv,"Reclocking failed: unknown core clock\n");
		goto resume;
	}

	switch (priv->ssrc) {
	case nv_clk_src_href:
		nv_mask(clk, 0x4020, 0x00070000, 0x00000000);
		/* mast |= 0x00000000; */
		break;
	case nv_clk_src_core:
		nv_mask(clk, 0x4020, 0x00070000, priv->sctrl);
		mast |= 0x00000020;
		break;
	case nv_clk_src_shader:
		nv_wr32(clk, 0x4024, priv->scoef);
		nv_wr32(clk, 0x4020, 0x80000000 | priv->sctrl);
		nv_wr32(clk, 0x4070, priv->spost);
		pllmask |= (0x3 << 12);
		mast |= 0x00000030;
		break;
	default:
		nv_warn(priv,"Reclocking failed: unknown sclk clock\n");
		goto resume;
	}

	if (!nv_wait(clk, 0x004080, pllmask, pllmask)) {
		nv_warn(priv,"Reclocking failed: unstable PLLs\n");
		goto resume;
	}

	switch (priv->vsrc) {
	case nv_clk_src_cclk:
		mast |= 0x00400000;
	default:
		nv_wr32(clk, 0x4600, priv->vdiv);
	}

	nv_wr32(clk, 0xc054, mast);
	ret = 0;

resume:
	if (pfifo)
		pfifo->start(pfifo, &flags);

	nv_mask(clk, 0x002504, 0x00000001, 0x00000000);
	nv_wr32(clk, 0x020060, ptherm_gate);

	/* Disable some PLLs and dividers when unused */
	if (priv->csrc != nv_clk_src_core) {
		nv_wr32(clk, 0x4040, 0x00000000);
		nv_mask(clk, 0x4028, 0x80000000, 0x00000000);
	}

	if (priv->ssrc != nv_clk_src_shader) {
		nv_wr32(clk, 0x4070, 0x00000000);
		nv_mask(clk, 0x4020, 0x80000000, 0x00000000);
	}

	return ret;
}

static void
nvaa_clock_tidy(struct nouveau_clock *clk)
{
}

static struct nouveau_clocks
nvaa_domains[] = {
	{ nv_clk_src_crystal, 0xff },
	{ nv_clk_src_href   , 0xff },
	{ nv_clk_src_core   , 0xff, 0, "core", 1000 },
	{ nv_clk_src_shader , 0xff, 0, "shader", 1000 },
	{ nv_clk_src_vdec   , 0xff, 0, "vdec", 1000 },
	{ nv_clk_src_max }
};

static int
nvaa_clock_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvaa_clock_priv *priv;
	int ret;

	ret = nouveau_clock_create(parent, engine, oclass, nvaa_domains, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.read = nvaa_clock_read;
	priv->base.calc = nvaa_clock_calc;
	priv->base.prog = nvaa_clock_prog;
	priv->base.tidy = nvaa_clock_tidy;
	return 0;
}

struct nouveau_oclass *
nvaa_clock_oclass = &(struct nouveau_oclass) {
	.handle = NV_SUBDEV(CLOCK, 0xaa),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvaa_clock_ctor,
		.dtor = _nouveau_clock_dtor,
		.init = _nouveau_clock_init,
		.fini = _nouveau_clock_fini,
	},
};
