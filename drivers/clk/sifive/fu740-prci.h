/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 SiFive, Inc.
 * Copyright (C) 2020-2021 Zong Li
 */

#ifndef __SIFIVE_CLK_FU740_PRCI_H
#define __SIFIVE_CLK_FU740_PRCI_H

#include <linux/module.h>

#include <dt-bindings/clock/sifive-fu740-prci.h>

#include "sifive-prci.h"

/* PRCI integration data for each WRPLL instance */

static struct __prci_wrpll_data sifive_fu740_prci_corepll_data = {
	.cfg0_offs = PRCI_COREPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_COREPLLCFG1_OFFSET,
	.enable_bypass = sifive_prci_coreclksel_use_hfclk,
	.disable_bypass = sifive_prci_coreclksel_use_final_corepll,
};

static struct __prci_wrpll_data sifive_fu740_prci_ddrpll_data = {
	.cfg0_offs = PRCI_DDRPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_DDRPLLCFG1_OFFSET,
};

static struct __prci_wrpll_data sifive_fu740_prci_gemgxlpll_data = {
	.cfg0_offs = PRCI_GEMGXLPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_GEMGXLPLLCFG1_OFFSET,
};

static struct __prci_wrpll_data sifive_fu740_prci_dvfscorepll_data = {
	.cfg0_offs = PRCI_DVFSCOREPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_DVFSCOREPLLCFG1_OFFSET,
	.enable_bypass = sifive_prci_corepllsel_use_corepll,
	.disable_bypass = sifive_prci_corepllsel_use_dvfscorepll,
};

static struct __prci_wrpll_data sifive_fu740_prci_hfpclkpll_data = {
	.cfg0_offs = PRCI_HFPCLKPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_HFPCLKPLLCFG1_OFFSET,
	.enable_bypass = sifive_prci_hfpclkpllsel_use_hfclk,
	.disable_bypass = sifive_prci_hfpclkpllsel_use_hfpclkpll,
};

static struct __prci_wrpll_data sifive_fu740_prci_cltxpll_data = {
	.cfg0_offs = PRCI_CLTXPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_CLTXPLLCFG1_OFFSET,
};

/* Linux clock framework integration */

static const struct clk_ops sifive_fu740_prci_wrpll_clk_ops = {
	.set_rate = sifive_prci_wrpll_set_rate,
	.determine_rate = sifive_prci_wrpll_determine_rate,
	.recalc_rate = sifive_prci_wrpll_recalc_rate,
	.enable = sifive_prci_clock_enable,
	.disable = sifive_prci_clock_disable,
	.is_enabled = sifive_clk_is_enabled,
};

static const struct clk_ops sifive_fu740_prci_wrpll_ro_clk_ops = {
	.recalc_rate = sifive_prci_wrpll_recalc_rate,
};

static const struct clk_ops sifive_fu740_prci_tlclksel_clk_ops = {
	.recalc_rate = sifive_prci_tlclksel_recalc_rate,
};

static const struct clk_ops sifive_fu740_prci_hfpclkplldiv_clk_ops = {
	.recalc_rate = sifive_prci_hfpclkplldiv_recalc_rate,
};

static const struct clk_ops sifive_fu740_prci_pcie_aux_clk_ops = {
	.enable = sifive_prci_pcie_aux_clock_enable,
	.disable = sifive_prci_pcie_aux_clock_disable,
	.is_enabled = sifive_prci_pcie_aux_clock_is_enabled,
};

/* List of clock controls provided by the PRCI */
static struct __prci_clock __prci_init_clocks_fu740[] = {
	[FU740_PRCI_CLK_COREPLL] = {
		.name = "corepll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_clk_ops,
		.pwd = &sifive_fu740_prci_corepll_data,
	},
	[FU740_PRCI_CLK_DDRPLL] = {
		.name = "ddrpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_ro_clk_ops,
		.pwd = &sifive_fu740_prci_ddrpll_data,
	},
	[FU740_PRCI_CLK_GEMGXLPLL] = {
		.name = "gemgxlpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_clk_ops,
		.pwd = &sifive_fu740_prci_gemgxlpll_data,
	},
	[FU740_PRCI_CLK_DVFSCOREPLL] = {
		.name = "dvfscorepll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_clk_ops,
		.pwd = &sifive_fu740_prci_dvfscorepll_data,
	},
	[FU740_PRCI_CLK_HFPCLKPLL] = {
		.name = "hfpclkpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_clk_ops,
		.pwd = &sifive_fu740_prci_hfpclkpll_data,
	},
	[FU740_PRCI_CLK_CLTXPLL] = {
		.name = "cltxpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_clk_ops,
		.pwd = &sifive_fu740_prci_cltxpll_data,
	},
	[FU740_PRCI_CLK_TLCLK] = {
		.name = "tlclk",
		.parent_name = "corepll",
		.ops = &sifive_fu740_prci_tlclksel_clk_ops,
	},
	[FU740_PRCI_CLK_PCLK] = {
		.name = "pclk",
		.parent_name = "hfpclkpll",
		.ops = &sifive_fu740_prci_hfpclkplldiv_clk_ops,
	},
	[FU740_PRCI_CLK_PCIE_AUX] = {
		.name = "pcie_aux",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_pcie_aux_clk_ops,
	},
};

static const struct prci_clk_desc prci_clk_fu740 = {
	.clks = __prci_init_clocks_fu740,
	.num_clks = ARRAY_SIZE(__prci_init_clocks_fu740),
};

#endif /* __SIFIVE_CLK_FU740_PRCI_H */
