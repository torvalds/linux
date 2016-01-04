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
#define gf100_clk(p) container_of((p), struct gf100_clk, base)
#include "priv.h"
#include "pll.h"

#include <subdev/bios.h>
#include <subdev/bios/pll.h>
#include <subdev/timer.h>

struct gf100_clk_info {
	u32 freq;
	u32 ssel;
	u32 mdiv;
	u32 dsrc;
	u32 ddiv;
	u32 coef;
};

struct gf100_clk {
	struct nvkm_clk base;
	struct gf100_clk_info eng[16];
};

static u32 read_div(struct gf100_clk *, int, u32, u32);

static u32
read_vco(struct gf100_clk *clk, u32 dsrc)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 ssrc = nvkm_rd32(device, dsrc);
	if (!(ssrc & 0x00000100))
		return nvkm_clk_read(&clk->base, nv_clk_src_sppll0);
	return nvkm_clk_read(&clk->base, nv_clk_src_sppll1);
}

static u32
read_pll(struct gf100_clk *clk, u32 pll)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 ctrl = nvkm_rd32(device, pll + 0x00);
	u32 coef = nvkm_rd32(device, pll + 0x04);
	u32 P = (coef & 0x003f0000) >> 16;
	u32 N = (coef & 0x0000ff00) >> 8;
	u32 M = (coef & 0x000000ff) >> 0;
	u32 sclk;

	if (!(ctrl & 0x00000001))
		return 0;

	switch (pll) {
	case 0x00e800:
	case 0x00e820:
		sclk = device->crystal;
		P = 1;
		break;
	case 0x132000:
		sclk = nvkm_clk_read(&clk->base, nv_clk_src_mpllsrc);
		break;
	case 0x132020:
		sclk = nvkm_clk_read(&clk->base, nv_clk_src_mpllsrcref);
		break;
	case 0x137000:
	case 0x137020:
	case 0x137040:
	case 0x1370e0:
		sclk = read_div(clk, (pll & 0xff) / 0x20, 0x137120, 0x137140);
		break;
	default:
		return 0;
	}

	return sclk * N / M / P;
}

static u32
read_div(struct gf100_clk *clk, int doff, u32 dsrc, u32 dctl)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 ssrc = nvkm_rd32(device, dsrc + (doff * 4));
	u32 sctl = nvkm_rd32(device, dctl + (doff * 4));

	switch (ssrc & 0x00000003) {
	case 0:
		if ((ssrc & 0x00030000) != 0x00030000)
			return device->crystal;
		return 108000;
	case 2:
		return 100000;
	case 3:
		if (sctl & 0x80000000) {
			u32 sclk = read_vco(clk, dsrc + (doff * 4));
			u32 sdiv = (sctl & 0x0000003f) + 2;
			return (sclk * 2) / sdiv;
		}

		return read_vco(clk, dsrc + (doff * 4));
	default:
		return 0;
	}
}

static u32
read_clk(struct gf100_clk *clk, int idx)
{
	struct nvkm_device *device = clk->base.subdev.device;
	u32 sctl = nvkm_rd32(device, 0x137250 + (idx * 4));
	u32 ssel = nvkm_rd32(device, 0x137100);
	u32 sclk, sdiv;

	if (ssel & (1 << idx)) {
		if (idx < 7)
			sclk = read_pll(clk, 0x137000 + (idx * 0x20));
		else
			sclk = read_pll(clk, 0x1370e0);
		sdiv = ((sctl & 0x00003f00) >> 8) + 2;
	} else {
		sclk = read_div(clk, idx, 0x137160, 0x1371d0);
		sdiv = ((sctl & 0x0000003f) >> 0) + 2;
	}

	if (sctl & 0x80000000)
		return (sclk * 2) / sdiv;

	return sclk;
}

static int
gf100_clk_read(struct nvkm_clk *base, enum nv_clk_src src)
{
	struct gf100_clk *clk = gf100_clk(base);
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_device *device = subdev->device;

	switch (src) {
	case nv_clk_src_crystal:
		return device->crystal;
	case nv_clk_src_href:
		return 100000;
	case nv_clk_src_sppll0:
		return read_pll(clk, 0x00e800);
	case nv_clk_src_sppll1:
		return read_pll(clk, 0x00e820);

	case nv_clk_src_mpllsrcref:
		return read_div(clk, 0, 0x137320, 0x137330);
	case nv_clk_src_mpllsrc:
		return read_pll(clk, 0x132020);
	case nv_clk_src_mpll:
		return read_pll(clk, 0x132000);
	case nv_clk_src_mdiv:
		return read_div(clk, 0, 0x137300, 0x137310);
	case nv_clk_src_mem:
		if (nvkm_rd32(device, 0x1373f0) & 0x00000002)
			return nvkm_clk_read(&clk->base, nv_clk_src_mpll);
		return nvkm_clk_read(&clk->base, nv_clk_src_mdiv);

	case nv_clk_src_gpc:
		return read_clk(clk, 0x00);
	case nv_clk_src_rop:
		return read_clk(clk, 0x01);
	case nv_clk_src_hubk07:
		return read_clk(clk, 0x02);
	case nv_clk_src_hubk06:
		return read_clk(clk, 0x07);
	case nv_clk_src_hubk01:
		return read_clk(clk, 0x08);
	case nv_clk_src_copy:
		return read_clk(clk, 0x09);
	case nv_clk_src_pmu:
		return read_clk(clk, 0x0c);
	case nv_clk_src_vdec:
		return read_clk(clk, 0x0e);
	default:
		nvkm_error(subdev, "invalid clock source %d\n", src);
		return -EINVAL;
	}
}

static u32
calc_div(struct gf100_clk *clk, int idx, u32 ref, u32 freq, u32 *ddiv)
{
	u32 div = min((ref * 2) / freq, (u32)65);
	if (div < 2)
		div = 2;

	*ddiv = div - 2;
	return (ref * 2) / div;
}

static u32
calc_src(struct gf100_clk *clk, int idx, u32 freq, u32 *dsrc, u32 *ddiv)
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
	sclk = read_vco(clk, 0x137160 + (idx * 4));
	if (idx < 7)
		sclk = calc_div(clk, idx, sclk, freq, ddiv);
	return sclk;
}

static u32
calc_pll(struct gf100_clk *clk, int idx, u32 freq, u32 *coef)
{
	struct nvkm_subdev *subdev = &clk->base.subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct nvbios_pll limits;
	int N, M, P, ret;

	ret = nvbios_pll_parse(bios, 0x137000 + (idx * 0x20), &limits);
	if (ret)
		return 0;

	limits.refclk = read_div(clk, idx, 0x137120, 0x137140);
	if (!limits.refclk)
		return 0;

	ret = gt215_pll_calc(subdev, &limits, freq, &N, NULL, &M, &P);
	if (ret <= 0)
		return 0;

	*coef = (P << 16) | (N << 8) | M;
	return ret;
}

static int
calc_clk(struct gf100_clk *clk, struct nvkm_cstate *cstate, int idx, int dom)
{
	struct gf100_clk_info *info = &clk->eng[idx];
	u32 freq = cstate->domain[dom];
	u32 src0, div0, div1D, div1P = 0;
	u32 clk0, clk1 = 0;

	/* invalid clock domain */
	if (!freq)
		return 0;

	/* first possible path, using only dividers */
	clk0 = calc_src(clk, idx, freq, &src0, &div0);
	clk0 = calc_div(clk, idx, clk0, freq, &div1D);

	/* see if we can get any closer using PLLs */
	if (clk0 != freq && (0x00004387 & (1 << idx))) {
		if (idx <= 7)
			clk1 = calc_pll(clk, idx, freq, &info->coef);
		else
			clk1 = cstate->domain[nv_clk_src_hubk06];
		clk1 = calc_div(clk, idx, clk1, freq, &div1P);
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
		info->ssel = (1 << idx);
		info->freq = clk1;
	}

	return 0;
}

static int
gf100_clk_calc(struct nvkm_clk *base, struct nvkm_cstate *cstate)
{
	struct gf100_clk *clk = gf100_clk(base);
	int ret;

	if ((ret = calc_clk(clk, cstate, 0x00, nv_clk_src_gpc)) ||
	    (ret = calc_clk(clk, cstate, 0x01, nv_clk_src_rop)) ||
	    (ret = calc_clk(clk, cstate, 0x02, nv_clk_src_hubk07)) ||
	    (ret = calc_clk(clk, cstate, 0x07, nv_clk_src_hubk06)) ||
	    (ret = calc_clk(clk, cstate, 0x08, nv_clk_src_hubk01)) ||
	    (ret = calc_clk(clk, cstate, 0x09, nv_clk_src_copy)) ||
	    (ret = calc_clk(clk, cstate, 0x0c, nv_clk_src_pmu)) ||
	    (ret = calc_clk(clk, cstate, 0x0e, nv_clk_src_vdec)))
		return ret;

	return 0;
}

static void
gf100_clk_prog_0(struct gf100_clk *clk, int idx)
{
	struct gf100_clk_info *info = &clk->eng[idx];
	struct nvkm_device *device = clk->base.subdev.device;
	if (idx < 7 && !info->ssel) {
		nvkm_mask(device, 0x1371d0 + (idx * 0x04), 0x80003f3f, info->ddiv);
		nvkm_wr32(device, 0x137160 + (idx * 0x04), info->dsrc);
	}
}

static void
gf100_clk_prog_1(struct gf100_clk *clk, int idx)
{
	struct nvkm_device *device = clk->base.subdev.device;
	nvkm_mask(device, 0x137100, (1 << idx), 0x00000000);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x137100) & (1 << idx)))
			break;
	);
}

static void
gf100_clk_prog_2(struct gf100_clk *clk, int idx)
{
	struct gf100_clk_info *info = &clk->eng[idx];
	struct nvkm_device *device = clk->base.subdev.device;
	const u32 addr = 0x137000 + (idx * 0x20);
	if (idx <= 7) {
		nvkm_mask(device, addr + 0x00, 0x00000004, 0x00000000);
		nvkm_mask(device, addr + 0x00, 0x00000001, 0x00000000);
		if (info->coef) {
			nvkm_wr32(device, addr + 0x04, info->coef);
			nvkm_mask(device, addr + 0x00, 0x00000001, 0x00000001);
			nvkm_msec(device, 2000,
				if (nvkm_rd32(device, addr + 0x00) & 0x00020000)
					break;
			);
			nvkm_mask(device, addr + 0x00, 0x00020004, 0x00000004);
		}
	}
}

static void
gf100_clk_prog_3(struct gf100_clk *clk, int idx)
{
	struct gf100_clk_info *info = &clk->eng[idx];
	struct nvkm_device *device = clk->base.subdev.device;
	if (info->ssel) {
		nvkm_mask(device, 0x137100, (1 << idx), info->ssel);
		nvkm_msec(device, 2000,
			u32 tmp = nvkm_rd32(device, 0x137100) & (1 << idx);
			if (tmp == info->ssel)
				break;
		);
	}
}

static void
gf100_clk_prog_4(struct gf100_clk *clk, int idx)
{
	struct gf100_clk_info *info = &clk->eng[idx];
	struct nvkm_device *device = clk->base.subdev.device;
	nvkm_mask(device, 0x137250 + (idx * 0x04), 0x00003f3f, info->mdiv);
}

static int
gf100_clk_prog(struct nvkm_clk *base)
{
	struct gf100_clk *clk = gf100_clk(base);
	struct {
		void (*exec)(struct gf100_clk *, int);
	} stage[] = {
		{ gf100_clk_prog_0 }, /* div programming */
		{ gf100_clk_prog_1 }, /* select div mode */
		{ gf100_clk_prog_2 }, /* (maybe) program pll */
		{ gf100_clk_prog_3 }, /* (maybe) select pll mode */
		{ gf100_clk_prog_4 }, /* final divider */
	};
	int i, j;

	for (i = 0; i < ARRAY_SIZE(stage); i++) {
		for (j = 0; j < ARRAY_SIZE(clk->eng); j++) {
			if (!clk->eng[j].freq)
				continue;
			stage[i].exec(clk, j);
		}
	}

	return 0;
}

static void
gf100_clk_tidy(struct nvkm_clk *base)
{
	struct gf100_clk *clk = gf100_clk(base);
	memset(clk->eng, 0x00, sizeof(clk->eng));
}

static const struct nvkm_clk_func
gf100_clk = {
	.read = gf100_clk_read,
	.calc = gf100_clk_calc,
	.prog = gf100_clk_prog,
	.tidy = gf100_clk_tidy,
	.domains = {
		{ nv_clk_src_crystal, 0xff },
		{ nv_clk_src_href   , 0xff },
		{ nv_clk_src_hubk06 , 0x00 },
		{ nv_clk_src_hubk01 , 0x01 },
		{ nv_clk_src_copy   , 0x02 },
		{ nv_clk_src_gpc    , 0x03, 0, "core", 2000 },
		{ nv_clk_src_rop    , 0x04 },
		{ nv_clk_src_mem    , 0x05, 0, "memory", 1000 },
		{ nv_clk_src_vdec   , 0x06 },
		{ nv_clk_src_pmu    , 0x0a },
		{ nv_clk_src_hubk07 , 0x0b },
		{ nv_clk_src_max }
	}
};

int
gf100_clk_new(struct nvkm_device *device, int index, struct nvkm_clk **pclk)
{
	struct gf100_clk *clk;

	if (!(clk = kzalloc(sizeof(*clk), GFP_KERNEL)))
		return -ENOMEM;
	*pclk = &clk->base;

	return nvkm_clk_ctor(&gf100_clk, device, index, false, &clk->base);
}
