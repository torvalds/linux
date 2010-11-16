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
#include "nouveau_hw.h"
#include "nouveau_pm.h"

struct nv04_pm_state {
	struct pll_lims pll;
	struct nouveau_pll_vals calc;
};

int
nv04_pm_clock_get(struct drm_device *dev, u32 id)
{
	return nouveau_hw_get_clock(dev, id);
}

void *
nv04_pm_clock_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl,
		  u32 id, int khz)
{
	struct nv04_pm_state *state;
	int ret;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);

	ret = get_pll_limits(dev, id, &state->pll);
	if (ret) {
		kfree(state);
		return (ret == -ENOENT) ? NULL : ERR_PTR(ret);
	}

	ret = nouveau_calc_pll_mnp(dev, &state->pll, khz, &state->calc);
	if (!ret) {
		kfree(state);
		return ERR_PTR(-EINVAL);
	}

	return state;
}

void
nv04_pm_clock_set(struct drm_device *dev, void *pre_state)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_pm_state *state = pre_state;
	u32 reg = state->pll.reg;

	/* thank the insane nouveau_hw_setpll() interface for this */
	if (dev_priv->card_type >= NV_40)
		reg += 4;

	nouveau_hw_setpll(dev, reg, &state->calc);
	kfree(state);
}

