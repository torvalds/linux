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

/* This is actually a lot more complex than it appears here, but hopefully
 * this should be able to deal with what the VBIOS leaves for us..
 *
 * If not, well, I'll jump off that bridge when I come to it.
 */

struct nva3_pm_state {
	struct pll_lims pll;
	int N, M, P;
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
	u32 src0, src1, ctrl, coef;
	struct pll_lims pll;
	int ret, off;
	int P, N, M;

	ret = get_pll_limits(dev, id, &pll);
	if (ret)
		return ret;

	off = nva3_pm_pll_offset(id);
	if (off < 0)
		return off;

	src0 = nv_rd32(dev, 0x4120 + (off * 4));
	src1 = nv_rd32(dev, 0x4160 + (off * 4));
	ctrl = nv_rd32(dev, pll.reg + 0);
	coef = nv_rd32(dev, pll.reg + 4);
	NV_DEBUG(dev, "PLL %02x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
		      id, src0, src1, ctrl, coef);

	if (ctrl & 0x00000008) {
		u32 div = ((src1 & 0x003c0000) >> 18) + 1;
		return (pll.refclk * 2) / div;
	}

	P = (coef & 0x003f0000) >> 16;
	N = (coef & 0x0000ff00) >> 8;
	M = (coef & 0x000000ff);
	return pll.refclk * N / M / P;
}

void *
nva3_pm_clock_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl,
		  u32 id, int khz)
{
	struct nva3_pm_state *state;
	int dummy, ret;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);

	ret = get_pll_limits(dev, id, &state->pll);
	if (ret < 0) {
		kfree(state);
		return (ret == -ENOENT) ? NULL : ERR_PTR(ret);
	}

	ret = nv50_calc_pll2(dev, &state->pll, khz, &state->N, &dummy,
			     &state->M, &state->P);
	if (ret < 0) {
		kfree(state);
		return ERR_PTR(ret);
	}

	return state;
}

void
nva3_pm_clock_set(struct drm_device *dev, void *pre_state)
{
	struct nva3_pm_state *state = pre_state;
	u32 reg = state->pll.reg;

	nv_wr32(dev, reg + 4, (state->P << 16) | (state->N << 8) | state->M);
	kfree(state);
}

