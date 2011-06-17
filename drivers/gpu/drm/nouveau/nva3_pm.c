/*
 * Copyright 2010 Red Hat Inc.
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

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_bios.h"
#include "nouveau_pm.h"

static u32 read_clk(struct drm_device *, int, bool);
static u32 read_pll(struct drm_device *, u32, int);

static u32
read_vco(struct drm_device *dev, int clk)
{
	u32 sctl = nv_rd32(dev, 0x4120 + (clk * 4));
	if ((sctl & 0x00000030) != 0x00000030)
		return read_pll(dev, 0x00e820, 0x41);
	return read_pll(dev, 0x00e8a0, 0x42);
}

static u32
read_clk(struct drm_device *dev, int clk, bool ignore_en)
{
	u32 sctl, sdiv, sclk;

	if (clk >= 0x40)
		return 27000;

	sctl = nv_rd32(dev, 0x4120 + (clk * 4));
	if (!ignore_en && !(sctl & 0x00000100))
		return 0;

	switch (sctl & 0x00003000) {
	case 0x00000000:
		return 27000;
	case 0x00002000:
		if (sctl & 0x00000040)
			return 108000;
		return 100000;
	case 0x00003000:
		sclk = read_vco(dev, clk);
		sdiv = ((sctl & 0x003f0000) >> 16) + 2;
		return (sclk * 2) / sdiv;
	default:
		return 0;
	}
}

static u32
read_pll(struct drm_device *dev, u32 pll, int clk)
{
	u32 ctrl = nv_rd32(dev, pll + 0);
	u32 sclk, P = 1, N = 1, M = 1;

	if (!(ctrl & 0x00000008)) {
		u32 coef = nv_rd32(dev, pll + 4);
		M = (coef & 0x000000ff) >> 0;
		N = (coef & 0x0000ff00) >> 8;
		P = (coef & 0x003f0000) >> 16;
		if ((pll & 0x00ff00) == 0x00e800)
			P = 1;

		sclk = read_clk(dev, 0x00 + clk, false);
	} else {
		sclk = read_clk(dev, 0x10 + clk, false);
	}

	return sclk * N / (M * P);
}

struct creg {
	u32 clk;
	u32 pll;
};

static int
calc_clk(struct drm_device *dev, u32 pll, int clk, u32 khz, struct creg *reg)
{
	struct pll_lims limits;
	u32 oclk, sclk, sdiv;
	int P, N, M, diff;
	int ret;

	reg->pll = 0;
	reg->clk = 0;

	switch (khz) {
	case 27000:
		reg->clk = 0x00000100;
		return khz;
	case 100000:
		reg->clk = 0x00002100;
		return khz;
	case 108000:
		reg->clk = 0x00002140;
		return khz;
	default:
		sclk = read_vco(dev, clk);
		sdiv = min((sclk * 2) / (khz - 2999), (u32)65);
		if (sdiv > 4) {
			oclk = (sclk * 2) / sdiv;
			diff = khz - oclk;
			if (!pll || (diff >= -2000 && diff < 3000)) {
				reg->clk = (((sdiv - 2) << 16) | 0x00003100);
				return oclk;
			}
		}
		break;
	}

	ret = get_pll_limits(dev, pll, &limits);
	if (ret)
		return ret;

	limits.refclk = read_clk(dev, clk - 0x10, true);
	if (!limits.refclk)
		return -EINVAL;

	ret = nva3_calc_pll(dev, &limits, khz, &N, NULL, &M, &P);
	if (ret >= 0) {
		reg->clk = nv_rd32(dev, 0x4120 + (clk * 4));
		reg->pll = (P << 16) | (N << 8) | M;
	}
	return ret;
}

int
nva3_pm_clocks_get(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	perflvl->core   = read_pll(dev, 0x4200, 0);
	perflvl->shader = read_pll(dev, 0x4220, 1);
	perflvl->memory = read_pll(dev, 0x4000, 2);
	perflvl->unka0  = read_clk(dev, 0x20, false);
	perflvl->vdec   = read_clk(dev, 0x21, false);
	return 0;
}

struct nva3_pm_state {
	struct creg nclk;
	struct creg sclk;
	struct creg mclk;
	struct creg vdec;
	struct creg unka0;
};

void *
nva3_pm_clocks_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct nva3_pm_state *info;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	ret = calc_clk(dev, 0x4200, 0x10, perflvl->core, &info->nclk);
	if (ret < 0)
		goto out;

	ret = calc_clk(dev, 0x4220, 0x11, perflvl->shader, &info->sclk);
	if (ret < 0)
		goto out;

	ret = calc_clk(dev, 0x4000, 0x12, perflvl->memory, &info->mclk);
	if (ret < 0)
		goto out;

	ret = calc_clk(dev, 0x0000, 0x20, perflvl->unka0, &info->unka0);
	if (ret < 0)
		goto out;

	ret = calc_clk(dev, 0x0000, 0x21, perflvl->vdec, &info->vdec);
	if (ret < 0)
		goto out;

out:
	if (ret < 0) {
		kfree(info);
		info = ERR_PTR(ret);
	}
	return info;
}

static void
prog_pll(struct drm_device *dev, u32 pll, int clk, struct creg *reg)
{
	const u32 src0 = 0x004120 + (clk * 4);
	const u32 src1 = 0x004160 + (clk * 4);
	const u32 ctrl = pll + 0;
	const u32 coef = pll + 4;
	u32 cntl;

	cntl = nv_rd32(dev, ctrl) & 0xfffffff2;
	if (reg->pll) {
		nv_mask(dev, src0, 0x00000101, 0x00000101);
		nv_wr32(dev, coef, reg->pll);
		nv_wr32(dev, ctrl, cntl | 0x00000015);
		nv_mask(dev, src1, 0x00000100, 0x00000000);
		nv_mask(dev, src1, 0x00000001, 0x00000000);
	} else {
		nv_mask(dev, src1, 0x003f3141, 0x00000101 | reg->clk);
		nv_wr32(dev, ctrl, cntl | 0x0000001d);
		nv_mask(dev, ctrl, 0x00000001, 0x00000000);
		nv_mask(dev, src0, 0x00000100, 0x00000000);
		nv_mask(dev, src0, 0x00000001, 0x00000000);
	}
}

static void
prog_clk(struct drm_device *dev, int clk, struct creg *reg)
{
	nv_mask(dev, 0x004120 + (clk * 4), 0x003f3141, 0x00000101 | reg->clk);
}

void
nva3_pm_clocks_set(struct drm_device *dev, void *pre_state)
{
	struct nva3_pm_state *info = pre_state;

	prog_pll(dev, 0x004200, 0, &info->nclk);
	prog_pll(dev, 0x004220, 1, &info->sclk);
	prog_clk(dev, 0x20, &info->unka0);
	prog_clk(dev, 0x21, &info->vdec);

	nv_wr32(dev, 0x100210, 0);
	nv_wr32(dev, 0x1002dc, 1);
	nv_wr32(dev, 0x004018, 0x00001000);
	prog_pll(dev, 0x004000, 2, &info->mclk);
	if (nv_rd32(dev, 0x4000) & 0x00000008)
		nv_wr32(dev, 0x004018, 0x1000d000);
	else
		nv_wr32(dev, 0x004018, 0x10005000);
	nv_wr32(dev, 0x1002dc, 0);
	nv_wr32(dev, 0x100210, 0x80000000);

	kfree(info);
}
