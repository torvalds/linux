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

static u32 read_pll(struct drm_device *dev, u32 pll, int clk);
static u32 read_clk(struct drm_device *dev, int clk);

static u32
read_clk(struct drm_device *dev, int clk)
{
	u32 sctl, sdiv, sclk;

	if (clk >= 0x40)
		return 27000;

	sctl = nv_rd32(dev, 0x4120 + (clk * 4));
	switch (sctl & 0x00003100) {
	case 0x00000100:
		return 27000;
	case 0x00002100:
		if (sctl & 0x00000040)
			return 108000;
		return 100000;
	case 0x00003100:
		sdiv = ((sctl & 0x003f0000) >> 16) + 2;
		if ((sctl & 0x00000030) != 0x00000030)
			sclk = read_pll(dev, 0x00e820, 0x41);
		else
			sclk = read_pll(dev, 0x00e8a0, 0x42);

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

		sclk = read_clk(dev, 0x00 + clk);
	} else {
		sclk = read_clk(dev, 0x10 + clk);
	}

	return sclk * N / (M * P);
}

struct nva3_pm_state {
	enum pll_types type;
	u32 src0;
	u32 src1;
	u32 ctrl;
	u32 coef;
	u32 old_pnm;
	u32 new_pnm;
	u32 new_div;
};

static int
nva3_pm_pll_offset(u32 id)
{
	static const u32 pll_map[] = {
		0x00, PLL_CORE,
		0x01, PLL_SHADER,
		0x02, PLL_MEMORY,
		0x00, 0x00
	};
	const u32 *map = pll_map;

	while (map[1]) {
		if (id == map[1])
			return map[0];
		map += 2;
	}

	return -ENOENT;
}

int
nva3_pm_clock_get(struct drm_device *dev, u32 id)
{
	switch (id) {
	case PLL_CORE:
		return read_pll(dev, 0x4200, 0);
	case PLL_SHADER:
		return read_pll(dev, 0x4220, 1);
	case PLL_MEMORY:
		return read_pll(dev, 0x4000, 2);
	default:
		return -ENOENT;
	}
}

void *
nva3_pm_clock_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl,
		  u32 id, int khz)
{
	struct nva3_pm_state *pll;
	struct pll_lims limits;
	int N, M, P, diff;
	int ret, off;

	ret = get_pll_limits(dev, id, &limits);
	if (ret < 0)
		return (ret == -ENOENT) ? NULL : ERR_PTR(ret);

	off = nva3_pm_pll_offset(id);
	if (id < 0)
		return ERR_PTR(-EINVAL);


	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);
	pll->type = id;
	pll->src0 = 0x004120 + (off * 4);
	pll->src1 = 0x004160 + (off * 4);
	pll->ctrl = limits.reg + 0;
	pll->coef = limits.reg + 4;

	/* If target clock is within [-2, 3) MHz of a divisor, we'll
	 * use that instead of calculating MNP values
	 */
	pll->new_div = min((limits.refclk * 2) / (khz - 2999), 16);
	if (pll->new_div) {
		diff = khz - ((limits.refclk * 2) / pll->new_div);
		if (diff < -2000 || diff >= 3000)
			pll->new_div = 0;
	}

	if (!pll->new_div) {
		ret = nva3_calc_pll(dev, &limits, khz, &N, NULL, &M, &P);
		if (ret < 0)
			return ERR_PTR(ret);

		pll->new_pnm = (P << 16) | (N << 8) | M;
		pll->new_div = 2 - 1;
	} else {
		pll->new_pnm = 0;
		pll->new_div--;
	}

	if ((nv_rd32(dev, pll->src1) & 0x00000101) != 0x00000101)
		pll->old_pnm = nv_rd32(dev, pll->coef);
	return pll;
}

void
nva3_pm_clock_set(struct drm_device *dev, void *pre_state)
{
	struct nva3_pm_state *pll = pre_state;
	u32 ctrl = 0;

	/* For the memory clock, NVIDIA will build a "script" describing
	 * the reclocking process and ask PDAEMON to execute it.
	 */
	if (pll->type == PLL_MEMORY) {
		nv_wr32(dev, 0x100210, 0);
		nv_wr32(dev, 0x1002dc, 1);
		nv_wr32(dev, 0x004018, 0x00001000);
		ctrl = 0x18000100;
	}

	if (pll->old_pnm || !pll->new_pnm) {
		nv_mask(dev, pll->src1, 0x003c0101, 0x00000101 |
						    (pll->new_div << 18));
		nv_wr32(dev, pll->ctrl, 0x0001001d | ctrl);
		nv_mask(dev, pll->ctrl, 0x00000001, 0x00000000);
	}

	if (pll->new_pnm) {
		nv_mask(dev, pll->src0, 0x00000101, 0x00000101);
		nv_wr32(dev, pll->coef, pll->new_pnm);
		nv_wr32(dev, pll->ctrl, 0x0001001d | ctrl);
		nv_mask(dev, pll->ctrl, 0x00000010, 0x00000000);
		nv_mask(dev, pll->ctrl, 0x00020010, 0x00020010);
		nv_wr32(dev, pll->ctrl, 0x00010015 | ctrl);
		nv_mask(dev, pll->src1, 0x00000100, 0x00000000);
		nv_mask(dev, pll->src1, 0x00000001, 0x00000000);
		if (pll->type == PLL_MEMORY)
			nv_wr32(dev, 0x4018, 0x10005000);
	} else {
		nv_mask(dev, pll->ctrl, 0x00000001, 0x00000000);
		nv_mask(dev, pll->src0, 0x00000100, 0x00000000);
		nv_mask(dev, pll->src0, 0x00000001, 0x00000000);
		if (pll->type == PLL_MEMORY)
			nv_wr32(dev, 0x4018, 0x1000d000);
	}

	if (pll->type == PLL_MEMORY) {
		nv_wr32(dev, 0x1002dc, 0);
		nv_wr32(dev, 0x100210, 0x80000000);
	}

	kfree(pll);
}

