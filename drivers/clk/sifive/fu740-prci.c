// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 SiFive, Inc.
 * Copyright (C) 2020 Zong Li
 */

#include <linux/module.h>

#include <dt-bindings/clock/sifive-fu740-prci.h>

#include "fu540-prci.h"
#include "sifive-prci.h"

/* PRCI integration data for each WRPLL instance */

static struct __prci_wrpll_data __prci_corepll_data = {
	.cfg0_offs = PRCI_COREPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_COREPLLCFG1_OFFSET,
	.enable_bypass = sifive_prci_coreclksel_use_hfclk,
	.disable_bypass = sifive_prci_coreclksel_use_final_corepll,
};

static struct __prci_wrpll_data __prci_ddrpll_data = {
	.cfg0_offs = PRCI_DDRPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_DDRPLLCFG1_OFFSET,
};

static struct __prci_wrpll_data __prci_gemgxlpll_data = {
	.cfg0_offs = PRCI_GEMGXLPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_GEMGXLPLLCFG1_OFFSET,
};

static struct __prci_wrpll_data __prci_dvfscorepll_data = {
	.cfg0_offs = PRCI_DVFSCOREPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_DVFSCOREPLLCFG1_OFFSET,
	.enable_bypass = sifive_prci_corepllsel_use_corepll,
	.disable_bypass = sifive_prci_corepllsel_use_dvfscorepll,
};

static struct __prci_wrpll_data __prci_hfpclkpll_data = {
	.cfg0_offs = PRCI_HFPCLKPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_HFPCLKPLLCFG1_OFFSET,
	.enable_bypass = sifive_prci_hfpclkpllsel_use_hfclk,
	.disable_bypass = sifive_prci_hfpclkpllsel_use_hfpclkpll,
};

static struct __prci_wrpll_data __prci_cltxpll_data = {
	.cfg0_offs = PRCI_CLTXPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_CLTXPLLCFG1_OFFSET,
};

/* Linux clock framework integration */

static const struct clk_ops sifive_fu740_prci_wrpll_clk_ops = {
	.set_rate = sifive_prci_wrpll_set_rate,
	.round_rate = sifive_prci_wrpll_round_rate,
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

/* List of clock controls provided by the PRCI */
struct __prci_clock __prci_init_clocks_fu740[] = {
	[PRCI_CLK_COREPLL] = {
		.name = "corepll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_clk_ops,
		.pwd = &__prci_corepll_data,
	},
	[PRCI_CLK_DDRPLL] = {
		.name = "ddrpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_ro_clk_ops,
		.pwd = &__prci_ddrpll_data,
	},
	[PRCI_CLK_GEMGXLPLL] = {
		.name = "gemgxlpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_clk_ops,
		.pwd = &__prci_gemgxlpll_data,
	},
	[PRCI_CLK_DVFSCOREPLL] = {
		.name = "dvfscorepll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_clk_ops,
		.pwd = &__prci_dvfscorepll_data,
	},
	[PRCI_CLK_HFPCLKPLL] = {
		.name = "hfpclkpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_clk_ops,
		.pwd = &__prci_hfpclkpll_data,
	},
	[PRCI_CLK_CLTXPLL] = {
		.name = "cltxpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu740_prci_wrpll_clk_ops,
		.pwd = &__prci_cltxpll_data,
	},
	[PRCI_CLK_TLCLK] = {
		.name = "tlclk",
		.parent_name = "corepll",
		.ops = &sifive_fu740_prci_tlclksel_clk_ops,
	},
	[PRCI_CLK_PCLK] = {
		.name = "pclk",
		.parent_name = "hfpclkpll",
		.ops = &sifive_fu740_prci_hfpclkplldiv_clk_ops,
	},
};
