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
#include <subdev/timer.h>

#include "pll.h"

#include "nva3.h"

struct nva3_clock_priv {
	struct nouveau_clock base;
	struct nva3_clock_info eng[nv_clk_src_max];
};

static u32 read_clk(struct nva3_clock_priv *, int, bool);
static u32 read_pll(struct nva3_clock_priv *, int, u32);

static u32
read_vco(struct nva3_clock_priv *priv, int clk)
{
	u32 sctl = nv_rd32(priv, 0x4120 + (clk * 4));
	if ((sctl & 0x00000030) != 0x00000030)
		return read_pll(priv, 0x41, 0x00e820);
	return read_pll(priv, 0x42, 0x00e8a0);
}

static u32
read_clk(struct nva3_clock_priv *priv, int clk, bool ignore_en)
{
	u32 sctl, sdiv, sclk;

	/* refclk for the 0xe8xx plls is a fixed frequency */
	if (clk >= 0x40) {
		if (nv_device(priv)->chipset == 0xaf) {
			/* no joke.. seriously.. sigh.. */
			return nv_rd32(priv, 0x00471c) * 1000;
		}

		return nv_device(priv)->crystal;
	}

	sctl = nv_rd32(priv, 0x4120 + (clk * 4));
	if (!ignore_en && !(sctl & 0x00000100))
		return 0;

	switch (sctl & 0x00003000) {
	case 0x00000000:
		return nv_device(priv)->crystal;
	case 0x00002000:
		if (sctl & 0x00000040)
			return 108000;
		return 100000;
	case 0x00003000:
		sclk = read_vco(priv, clk);
		sdiv = ((sctl & 0x003f0000) >> 16) + 2;
		return (sclk * 2) / sdiv;
	default:
		return 0;
	}
}

static u32
read_pll(struct nva3_clock_priv *priv, int clk, u32 pll)
{
	u32 ctrl = nv_rd32(priv, pll + 0);
	u32 sclk = 0, P = 1, N = 1, M = 1;

	if (!(ctrl & 0x00000008)) {
		if (ctrl & 0x00000001) {
			u32 coef = nv_rd32(priv, pll + 4);
			M = (coef & 0x000000ff) >> 0;
			N = (coef & 0x0000ff00) >> 8;
			P = (coef & 0x003f0000) >> 16;

			/* no post-divider on these.. */
			if ((pll & 0x00ff00) == 0x00e800)
				P = 1;

			sclk = read_clk(priv, 0x00 + clk, false);
		}
	} else {
		sclk = read_clk(priv, 0x10 + clk, false);
	}

	if (M * P)
		return sclk * N / (M * P);
	return 0;
}

static int
nva3_clock_read(struct nouveau_clock *clk, enum nv_clk_src src)
{
	struct nva3_clock_priv *priv = (void *)clk;

	switch (src) {
	case nv_clk_src_crystal:
		return nv_device(priv)->crystal;
	case nv_clk_src_href:
		return 100000;
	case nv_clk_src_core:
		return read_pll(priv, 0x00, 0x4200);
	case nv_clk_src_shader:
		return read_pll(priv, 0x01, 0x4220);
	case nv_clk_src_mem:
		return read_pll(priv, 0x02, 0x4000);
	case nv_clk_src_disp:
		return read_clk(priv, 0x20, false);
	case nv_clk_src_vdec:
		return read_clk(priv, 0x21, false);
	case nv_clk_src_daemon:
		return read_clk(priv, 0x25, false);
	default:
		nv_error(clk, "invalid clock source %d\n", src);
		return -EINVAL;
	}
}

int
nva3_clock_info(struct nouveau_clock *clock, int clk, u32 pll, u32 khz,
		struct nva3_clock_info *info)
{
	struct nouveau_bios *bios = nouveau_bios(clock);
	struct nva3_clock_priv *priv = (void *)clock;
	struct nvbios_pll limits;
	u32 oclk, sclk, sdiv;
	int P, N, M, diff;
	int ret;

	info->pll = 0;
	info->clk = 0;

	switch (khz) {
	case 27000:
		info->clk = 0x00000100;
		return khz;
	case 100000:
		info->clk = 0x00002100;
		return khz;
	case 108000:
		info->clk = 0x00002140;
		return khz;
	default:
		sclk = read_vco(priv, clk);
		sdiv = min((sclk * 2) / (khz - 2999), (u32)65);
		/* if the clock has a PLL attached, and we can get a within
		 * [-2, 3) MHz of a divider, we'll disable the PLL and use
		 * the divider instead.
		 *
		 * divider can go as low as 2, limited here because NVIDIA
		 * and the VBIOS on my NVA8 seem to prefer using the PLL
		 * for 810MHz - is there a good reason?
		 */
		if (sdiv > 4) {
			oclk = (sclk * 2) / sdiv;
			diff = khz - oclk;
			if (!pll || (diff >= -2000 && diff < 3000)) {
				info->clk = (((sdiv - 2) << 16) | 0x00003100);
				return oclk;
			}
		}

		if (!pll)
			return -ERANGE;
		break;
	}

	ret = nvbios_pll_parse(bios, pll, &limits);
	if (ret)
		return ret;

	limits.refclk = read_clk(priv, clk - 0x10, true);
	if (!limits.refclk)
		return -EINVAL;

	ret = nva3_pll_calc(nv_subdev(priv), &limits, khz, &N, NULL, &M, &P);
	if (ret >= 0) {
		info->clk = nv_rd32(priv, 0x4120 + (clk * 4));
		info->pll = (P << 16) | (N << 8) | M;
	}

	return ret ? ret : -ERANGE;
}

static int
calc_clk(struct nva3_clock_priv *priv, struct nouveau_cstate *cstate,
	 int clk, u32 pll, int idx)
{
	int ret = nva3_clock_info(&priv->base, clk, pll, cstate->domain[idx],
				  &priv->eng[idx]);
	if (ret >= 0)
		return 0;
	return ret;
}

static void
prog_pll(struct nva3_clock_priv *priv, int clk, u32 pll, int idx)
{
	struct nva3_clock_info *info = &priv->eng[idx];
	const u32 src0 = 0x004120 + (clk * 4);
	const u32 src1 = 0x004160 + (clk * 4);
	const u32 ctrl = pll + 0;
	const u32 coef = pll + 4;

	if (info->pll) {
		nv_mask(priv, src0, 0x00000101, 0x00000101);
		nv_wr32(priv, coef, info->pll);
		nv_mask(priv, ctrl, 0x00000015, 0x00000015);
		nv_mask(priv, ctrl, 0x00000010, 0x00000000);
		nv_wait(priv, ctrl, 0x00020000, 0x00020000);
		nv_mask(priv, ctrl, 0x00000010, 0x00000010);
		nv_mask(priv, ctrl, 0x00000008, 0x00000000);
		nv_mask(priv, src1, 0x00000100, 0x00000000);
		nv_mask(priv, src1, 0x00000001, 0x00000000);
	} else {
		nv_mask(priv, src1, 0x003f3141, 0x00000101 | info->clk);
		nv_mask(priv, ctrl, 0x00000018, 0x00000018);
		udelay(20);
		nv_mask(priv, ctrl, 0x00000001, 0x00000000);
		nv_mask(priv, src0, 0x00000100, 0x00000000);
		nv_mask(priv, src0, 0x00000001, 0x00000000);
	}
}

static void
prog_clk(struct nva3_clock_priv *priv, int clk, int idx)
{
	struct nva3_clock_info *info = &priv->eng[idx];
	nv_mask(priv, 0x004120 + (clk * 4), 0x003f3141, 0x00000101 | info->clk);
}

static int
nva3_clock_calc(struct nouveau_clock *clk, struct nouveau_cstate *cstate)
{
	struct nva3_clock_priv *priv = (void *)clk;
	int ret;

	if ((ret = calc_clk(priv, cstate, 0x10, 0x4200, nv_clk_src_core)) ||
	    (ret = calc_clk(priv, cstate, 0x11, 0x4220, nv_clk_src_shader)) ||
	    (ret = calc_clk(priv, cstate, 0x20, 0x0000, nv_clk_src_disp)) ||
	    (ret = calc_clk(priv, cstate, 0x21, 0x0000, nv_clk_src_vdec)))
		return ret;

	return 0;
}

static int
nva3_clock_prog(struct nouveau_clock *clk)
{
	struct nva3_clock_priv *priv = (void *)clk;
	prog_pll(priv, 0x00, 0x004200, nv_clk_src_core);
	prog_pll(priv, 0x01, 0x004220, nv_clk_src_shader);
	prog_clk(priv, 0x20, nv_clk_src_disp);
	prog_clk(priv, 0x21, nv_clk_src_vdec);
	return 0;
}

static void
nva3_clock_tidy(struct nouveau_clock *clk)
{
}

static struct nouveau_clocks
nva3_domain[] = {
	{ nv_clk_src_crystal, 0xff },
	{ nv_clk_src_href   , 0xff },
	{ nv_clk_src_core   , 0x00, 0, "core", 1000 },
	{ nv_clk_src_shader , 0x01, 0, "shader", 1000 },
	{ nv_clk_src_mem    , 0x02, 0, "memory", 1000 },
	{ nv_clk_src_vdec   , 0x03 },
	{ nv_clk_src_disp   , 0x04 },
	{ nv_clk_src_max }
};

static int
nva3_clock_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nva3_clock_priv *priv;
	int ret;

	ret = nouveau_clock_create(parent, engine, oclass, nva3_domain, NULL, 0,
				   false, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.read = nva3_clock_read;
	priv->base.calc = nva3_clock_calc;
	priv->base.prog = nva3_clock_prog;
	priv->base.tidy = nva3_clock_tidy;
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
