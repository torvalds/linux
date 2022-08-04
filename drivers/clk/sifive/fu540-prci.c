// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2019 SiFive, Inc.
 * Copyright (C) 2018-2019 Wesley Terpstra
 * Copyright (C) 2018-2019 Paul Walmsley
 * Copyright (C) 2020 Zong Li
 *
 * The FU540 PRCI implements clock and reset control for the SiFive
 * FU540-C000 chip.  This driver assumes that it has sole control
 * over all PRCI resources.
 *
 * This driver is based on the PRCI driver written by Wesley Terpstra:
 * https://github.com/riscv/riscv-linux/commit/999529edf517ed75b56659d456d221b2ee56bb60
 *
 * References:
 * - SiFive FU540-C000 manual v1p0, Chapter 7 "Clocking and Reset"
 */

#include <linux/module.h>

#include <dt-bindings/clock/sifive-fu540-prci.h>

#include "fu540-prci.h"
#include "sifive-prci.h"

/* PRCI integration data for each WRPLL instance */

static struct __prci_wrpll_data __prci_corepll_data = {
	.cfg0_offs = PRCI_COREPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_COREPLLCFG1_OFFSET,
	.enable_bypass = sifive_prci_coreclksel_use_hfclk,
	.disable_bypass = sifive_prci_coreclksel_use_corepll,
};

static struct __prci_wrpll_data __prci_ddrpll_data = {
	.cfg0_offs = PRCI_DDRPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_DDRPLLCFG1_OFFSET,
};

static struct __prci_wrpll_data __prci_gemgxlpll_data = {
	.cfg0_offs = PRCI_GEMGXLPLLCFG0_OFFSET,
	.cfg1_offs = PRCI_GEMGXLPLLCFG1_OFFSET,
};

/* Linux clock framework integration */

static const struct clk_ops sifive_fu540_prci_wrpll_clk_ops = {
	.set_rate = sifive_prci_wrpll_set_rate,
	.round_rate = sifive_prci_wrpll_round_rate,
	.recalc_rate = sifive_prci_wrpll_recalc_rate,
	.enable = sifive_prci_clock_enable,
	.disable = sifive_prci_clock_disable,
	.is_enabled = sifive_clk_is_enabled,
};

static const struct clk_ops sifive_fu540_prci_wrpll_ro_clk_ops = {
	.recalc_rate = sifive_prci_wrpll_recalc_rate,
};

static const struct clk_ops sifive_fu540_prci_tlclksel_clk_ops = {
	.recalc_rate = sifive_prci_tlclksel_recalc_rate,
};

/* List of clock controls provided by the PRCI */
struct __prci_clock __prci_init_clocks_fu540[] = {
	[PRCI_CLK_COREPLL] = {
		.name = "corepll",
		.parent_name = "hfclk",
		.ops = &sifive_fu540_prci_wrpll_clk_ops,
		.pwd = &__prci_corepll_data,
	},
	[PRCI_CLK_DDRPLL] = {
		.name = "ddrpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu540_prci_wrpll_ro_clk_ops,
		.pwd = &__prci_ddrpll_data,
	},
	[PRCI_CLK_GEMGXLPLL] = {
		.name = "gemgxlpll",
		.parent_name = "hfclk",
		.ops = &sifive_fu540_prci_wrpll_clk_ops,
		.pwd = &__prci_gemgxlpll_data,
	},
	[PRCI_CLK_TLCLK] = {
		.name = "tlclk",
		.parent_name = "corepll",
		.ops = &sifive_fu540_prci_tlclksel_clk_ops,
	},
};
