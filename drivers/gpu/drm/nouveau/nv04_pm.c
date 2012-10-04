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

#include <drm/drmP.h>
#include "nouveau_drm.h"
#include "nouveau_reg.h"
#include "nouveau_hw.h"
#include "nouveau_pm.h"

#include <subdev/bios/pll.h>
#include <subdev/clock.h>
#include <subdev/timer.h>

int
nv04_pm_clocks_get(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	int ret;

	ret = nouveau_hw_get_clock(dev, PLL_CORE);
	if (ret < 0)
		return ret;
	perflvl->core = ret;

	ret = nouveau_hw_get_clock(dev, PLL_MEMORY);
	if (ret < 0)
		return ret;
	perflvl->memory = ret;

	return 0;
}

struct nv04_pm_clock {
	struct nvbios_pll pll;
	struct nouveau_pll_vals calc;
};

struct nv04_pm_state {
	struct nv04_pm_clock core;
	struct nv04_pm_clock memory;
};

static int
calc_pll(struct drm_device *dev, u32 id, int khz, struct nv04_pm_clock *clk)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_bios *bios = nouveau_bios(device);
	struct nouveau_clock *pclk = nouveau_clock(device);
	int ret;

	ret = nvbios_pll_parse(bios, id, &clk->pll);
	if (ret)
		return ret;

	ret = pclk->pll_calc(pclk, &clk->pll, khz, &clk->calc);
	if (!ret)
		return -EINVAL;

	return 0;
}

void *
nv04_pm_clocks_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl)
{
	struct nv04_pm_state *info;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	ret = calc_pll(dev, PLL_CORE, perflvl->core, &info->core);
	if (ret)
		goto error;

	if (perflvl->memory) {
		ret = calc_pll(dev, PLL_MEMORY, perflvl->memory, &info->memory);
		if (ret)
			goto error;
	}

	return info;
error:
	kfree(info);
	return ERR_PTR(ret);
}

static void
prog_pll(struct drm_device *dev, struct nv04_pm_clock *clk)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_clock *pclk = nouveau_clock(device);
	u32 reg = clk->pll.reg;

	/* thank the insane nouveau_hw_setpll() interface for this */
	if (device->card_type >= NV_40)
		reg += 4;

	pclk->pll_prog(pclk, reg, &clk->calc);
}

int
nv04_pm_clocks_set(struct drm_device *dev, void *pre_state)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_timer *ptimer = nouveau_timer(device);
	struct nv04_pm_state *state = pre_state;

	prog_pll(dev, &state->core);

	if (state->memory.pll.reg) {
		prog_pll(dev, &state->memory);
		if (device->card_type < NV_30) {
			if (device->card_type == NV_20)
				nv_mask(device, 0x1002c4, 0, 1 << 20);

			/* Reset the DLLs */
			nv_mask(device, 0x1002c0, 0, 1 << 8);
		}
	}

	nv_ofuncs(ptimer)->init(nv_object(ptimer));

	kfree(state);
	return 0;
}
