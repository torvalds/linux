/*
 * Copyright (c) 2012, 2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/clk/tegra.h>

#include "clk.h"
#include "clk-id.h"

#define OSC_CTRL			0x50
#define OSC_CTRL_OSC_FREQ_SHIFT		28
#define OSC_CTRL_PLL_REF_DIV_SHIFT	26

int __init tegra_osc_clk_init(void __iomem *clk_base, struct tegra_clk *clks,
			      unsigned long *input_freqs, unsigned int num,
			      unsigned int clk_m_div, unsigned long *osc_freq,
			      unsigned long *pll_ref_freq)
{
	struct clk *clk, *osc;
	struct clk **dt_clk;
	u32 val, pll_ref_div;
	unsigned osc_idx;

	val = readl_relaxed(clk_base + OSC_CTRL);
	osc_idx = val >> OSC_CTRL_OSC_FREQ_SHIFT;

	if (osc_idx < num)
		*osc_freq = input_freqs[osc_idx];
	else
		*osc_freq = 0;

	if (!*osc_freq) {
		WARN_ON(1);
		return -EINVAL;
	}

	osc = clk_register_fixed_rate(NULL, "osc", NULL, 0, *osc_freq);

	dt_clk = tegra_lookup_dt_id(tegra_clk_clk_m, clks);
	if (!dt_clk)
		return 0;

	clk = clk_register_fixed_factor(NULL, "clk_m", "osc",
					0, 1, clk_m_div);
	*dt_clk = clk;

	/* pll_ref */
	val = (val >> OSC_CTRL_PLL_REF_DIV_SHIFT) & 3;
	pll_ref_div = 1 << val;
	dt_clk = tegra_lookup_dt_id(tegra_clk_pll_ref, clks);
	if (!dt_clk)
		return 0;

	clk = clk_register_fixed_factor(NULL, "pll_ref", "osc",
					0, 1, pll_ref_div);
	*dt_clk = clk;

	if (pll_ref_freq)
		*pll_ref_freq = *osc_freq / pll_ref_div;

	return 0;
}

void __init tegra_fixed_clk_init(struct tegra_clk *tegra_clks)
{
	struct clk *clk;
	struct clk **dt_clk;

	/* clk_32k */
	dt_clk = tegra_lookup_dt_id(tegra_clk_clk_32k, tegra_clks);
	if (dt_clk) {
		clk = clk_register_fixed_rate(NULL, "clk_32k", NULL, 0, 32768);
		*dt_clk = clk;
	}

	/* clk_m_div2 */
	dt_clk = tegra_lookup_dt_id(tegra_clk_clk_m_div2, tegra_clks);
	if (dt_clk) {
		clk = clk_register_fixed_factor(NULL, "clk_m_div2", "clk_m",
					CLK_SET_RATE_PARENT, 1, 2);
		*dt_clk = clk;
	}

	/* clk_m_div4 */
	dt_clk = tegra_lookup_dt_id(tegra_clk_clk_m_div4, tegra_clks);
	if (dt_clk) {
		clk = clk_register_fixed_factor(NULL, "clk_m_div4", "clk_m",
					CLK_SET_RATE_PARENT, 1, 4);
		*dt_clk = clk;
	}
}
