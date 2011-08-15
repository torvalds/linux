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

struct nv50_pm_state {
	struct nouveau_pm_level *perflvl;
	struct pll_lims pll;
	enum pll_types type;
	int N, M, P;
};

int
nv50_pm_clock_get(struct drm_device *dev, u32 id)
{
	struct pll_lims pll;
	int P, N, M, ret;
	u32 reg0, reg1;

	ret = get_pll_limits(dev, id, &pll);
	if (ret)
		return ret;

	reg0 = nv_rd32(dev, pll.reg + 0);
	reg1 = nv_rd32(dev, pll.reg + 4);

	if ((reg0 & 0x80000000) == 0) {
		if (id == PLL_SHADER) {
			NV_DEBUG(dev, "Shader PLL is disabled. "
				"Shader clock is twice the core\n");
			ret = nv50_pm_clock_get(dev, PLL_CORE);
			if (ret > 0)
				return ret << 1;
		} else if (id == PLL_MEMORY) {
			NV_DEBUG(dev, "Memory PLL is disabled. "
				"Memory clock is equal to the ref_clk\n");
			return pll.refclk;
		}
	}

	P = (reg0 & 0x00070000) >> 16;
	N = (reg1 & 0x0000ff00) >> 8;
	M = (reg1 & 0x000000ff);

	return ((pll.refclk * N / M) >> P);
}

void *
nv50_pm_clock_pre(struct drm_device *dev, struct nouveau_pm_level *perflvl,
		  u32 id, int khz)
{
	struct nv50_pm_state *state;
	int dummy, ret;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);
	state->type = id;
	state->perflvl = perflvl;

	ret = get_pll_limits(dev, id, &state->pll);
	if (ret < 0) {
		kfree(state);
		return (ret == -ENOENT) ? NULL : ERR_PTR(ret);
	}

	ret = nv50_calc_pll(dev, &state->pll, khz, &state->N, &state->M,
			    &dummy, &dummy, &state->P);
	if (ret < 0) {
		kfree(state);
		return ERR_PTR(ret);
	}

	return state;
}

void
nv50_pm_clock_set(struct drm_device *dev, void *pre_state)
{
	struct nv50_pm_state *state = pre_state;
	struct nouveau_pm_level *perflvl = state->perflvl;
	u32 reg = state->pll.reg, tmp;
	struct bit_entry BIT_M;
	u16 script;
	int N = state->N;
	int M = state->M;
	int P = state->P;

	if (state->type == PLL_MEMORY && perflvl->memscript &&
	    bit_table(dev, 'M', &BIT_M) == 0 &&
	    BIT_M.version == 1 && BIT_M.length >= 0x0b) {
		script = ROM16(BIT_M.data[0x05]);
		if (script)
			nouveau_bios_run_init_table(dev, script, NULL, -1);
		script = ROM16(BIT_M.data[0x07]);
		if (script)
			nouveau_bios_run_init_table(dev, script, NULL, -1);
		script = ROM16(BIT_M.data[0x09]);
		if (script)
			nouveau_bios_run_init_table(dev, script, NULL, -1);

		nouveau_bios_run_init_table(dev, perflvl->memscript, NULL, -1);
	}

	if (state->type == PLL_MEMORY) {
		nv_wr32(dev, 0x100210, 0);
		nv_wr32(dev, 0x1002dc, 1);
	}

	tmp  = nv_rd32(dev, reg + 0) & 0xfff8ffff;
	tmp |= 0x80000000 | (P << 16);
	nv_wr32(dev, reg + 0, tmp);
	nv_wr32(dev, reg + 4, (N << 8) | M);

	if (state->type == PLL_MEMORY) {
		nv_wr32(dev, 0x1002dc, 0);
		nv_wr32(dev, 0x100210, 0x80000000);
	}

	kfree(state);
}

struct pwm_info {
	int id;
	int invert;
	u8  tag;
	u32 ctrl;
	int line;
};

static int
nv50_pm_fanspeed_pwm(struct drm_device *dev, struct pwm_info *pwm)
{
	struct dcb_gpio_entry *gpio;

	gpio = nouveau_bios_gpio_entry(dev, 0x09);
	if (gpio) {
		pwm->tag = gpio->tag;
		pwm->id = (gpio->line == 9) ? 1 : 0;
		pwm->invert = gpio->state[0] & 1;
		pwm->ctrl = (gpio->line < 16) ? 0xe100 : 0xe28c;
		pwm->line = (gpio->line & 0xf);
		return 0;
	}

	return -ENOENT;
}

int
nv50_pm_fanspeed_get(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpio_engine *pgpio = &dev_priv->engine.gpio;
	struct pwm_info pwm;
	int ret;

	ret = nv50_pm_fanspeed_pwm(dev, &pwm);
	if (ret)
		return ret;

	if (nv_rd32(dev, pwm.ctrl) & (0x00000001 << pwm.line)) {
		u32 divs = nv_rd32(dev, 0x00e114 + (pwm.id * 8));
		u32 duty = nv_rd32(dev, 0x00e118 + (pwm.id * 8));
		if (divs) {
			divs = max(divs, duty);
			if (pwm.invert)
				duty = divs - duty;
			return (duty * 100) / divs;
		}

		return 0;
	}

	return pgpio->get(dev, pwm.tag) * 100;
}

int
nv50_pm_fanspeed_set(struct drm_device *dev, int percent)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pm_engine *pm = &dev_priv->engine.pm;
	struct pwm_info pwm;
	u32 divs, duty;
	int ret;

	ret = nv50_pm_fanspeed_pwm(dev, &pwm);
	if (ret)
		return ret;

	divs = pm->pwm_divisor;
	if (pm->fan.pwm_freq) {
		/*XXX: PNVIO clock more than likely... */
		divs = 1350000 / pm->fan.pwm_freq;
		if (dev_priv->chipset < 0xa3)
			divs /= 4;
	}

	duty = ((divs * percent) + 99) / 100;
	if (pwm.invert)
		duty = divs - duty;

	nv_mask(dev, pwm.ctrl, 0x00010001 << pwm.line, 0x00000001 << pwm.line);
	nv_wr32(dev, 0x00e114 + (pwm.id * 8), divs);
	nv_wr32(dev, 0x00e118 + (pwm.id * 8), 0x80000000 | duty);
	return 0;
}
