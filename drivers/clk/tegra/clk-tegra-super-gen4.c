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
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/clk/tegra.h>

#include "clk.h"
#include "clk-id.h"

#define PLLX_BASE 0xe0
#define PLLX_MISC 0xe4
#define PLLX_MISC2 0x514
#define PLLX_MISC3 0x518

#define CCLKG_BURST_POLICY 0x368
#define CCLKLP_BURST_POLICY 0x370
#define SCLK_BURST_POLICY 0x028
#define SYSTEM_CLK_RATE 0x030

static DEFINE_SPINLOCK(sysrate_lock);

static const char *sclk_parents[] = { "clk_m", "pll_c_out1", "pll_p_out4",
			       "pll_p", "pll_p_out2", "unused",
			       "clk_32k", "pll_m_out1" };

static const char *cclk_g_parents[] = { "clk_m", "pll_c", "clk_32k", "pll_m",
					"pll_p", "pll_p_out4", "unused",
					"unused", "pll_x" };

static const char *cclk_lp_parents[] = { "clk_m", "pll_c", "clk_32k", "pll_m",
					 "pll_p", "pll_p_out4", "unused",
					 "unused", "pll_x", "pll_x_out0" };

static void __init tegra_sclk_init(void __iomem *clk_base,
				struct tegra_clk *tegra_clks)
{
	struct clk *clk;
	struct clk **dt_clk;

	/* SCLK */
	dt_clk = tegra_lookup_dt_id(tegra_clk_sclk, tegra_clks);
	if (dt_clk) {
		clk = tegra_clk_register_super_mux("sclk", sclk_parents,
						ARRAY_SIZE(sclk_parents),
						CLK_SET_RATE_PARENT,
						clk_base + SCLK_BURST_POLICY,
						0, 4, 0, 0, NULL);
		*dt_clk = clk;
	}

	/* HCLK */
	dt_clk = tegra_lookup_dt_id(tegra_clk_hclk, tegra_clks);
	if (dt_clk) {
		clk = clk_register_divider(NULL, "hclk_div", "sclk", 0,
				   clk_base + SYSTEM_CLK_RATE, 4, 2, 0,
				   &sysrate_lock);
		clk = clk_register_gate(NULL, "hclk", "hclk_div",
				CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
				clk_base + SYSTEM_CLK_RATE,
				7, CLK_GATE_SET_TO_DISABLE, &sysrate_lock);
		*dt_clk = clk;
	}

	/* PCLK */
	dt_clk = tegra_lookup_dt_id(tegra_clk_pclk, tegra_clks);
	if (!dt_clk)
		return;

	clk = clk_register_divider(NULL, "pclk_div", "hclk", 0,
				   clk_base + SYSTEM_CLK_RATE, 0, 2, 0,
				   &sysrate_lock);
	clk = clk_register_gate(NULL, "pclk", "pclk_div", CLK_SET_RATE_PARENT |
				CLK_IGNORE_UNUSED, clk_base + SYSTEM_CLK_RATE,
				3, CLK_GATE_SET_TO_DISABLE, &sysrate_lock);
	*dt_clk = clk;
}

void __init tegra_super_clk_gen4_init(void __iomem *clk_base,
				void __iomem *pmc_base,
				struct tegra_clk *tegra_clks,
				struct tegra_clk_pll_params *params)
{
	struct clk *clk;
	struct clk **dt_clk;

	/* CCLKG */
	dt_clk = tegra_lookup_dt_id(tegra_clk_cclk_g, tegra_clks);
	if (dt_clk) {
		clk = tegra_clk_register_super_mux("cclk_g", cclk_g_parents,
					ARRAY_SIZE(cclk_g_parents),
					CLK_SET_RATE_PARENT,
					clk_base + CCLKG_BURST_POLICY,
					0, 4, 0, 0, NULL);
		*dt_clk = clk;
	}

	/* CCLKLP */
	dt_clk = tegra_lookup_dt_id(tegra_clk_cclk_lp, tegra_clks);
	if (dt_clk) {
		clk = tegra_clk_register_super_mux("cclk_lp", cclk_lp_parents,
					ARRAY_SIZE(cclk_lp_parents),
					CLK_SET_RATE_PARENT,
					clk_base + CCLKLP_BURST_POLICY,
					TEGRA_DIVIDER_2, 4, 8, 9, NULL);
		*dt_clk = clk;
	}

	tegra_sclk_init(clk_base, tegra_clks);

#if defined(CONFIG_ARCH_TEGRA_114_SOC) || defined(CONFIG_ARCH_TEGRA_124_SOC)
	/* PLLX */
	dt_clk = tegra_lookup_dt_id(tegra_clk_pll_x, tegra_clks);
	if (!dt_clk)
		return;

	clk = tegra_clk_register_pllxc("pll_x", "pll_ref", clk_base,
			pmc_base, CLK_IGNORE_UNUSED, params, NULL);
	*dt_clk = clk;

	/* PLLX_OUT0 */

	dt_clk = tegra_lookup_dt_id(tegra_clk_pll_x_out0, tegra_clks);
	if (!dt_clk)
		return;
	clk = clk_register_fixed_factor(NULL, "pll_x_out0", "pll_x",
					CLK_SET_RATE_PARENT, 1, 2);
	*dt_clk = clk;
#endif
}

