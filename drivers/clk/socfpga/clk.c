// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2011-2012 Calxeda, Inc.
 *  Copyright (C) 2012-2013 Altera Corporation <www.altera.com>
 *
 * Based from clk-highbank.c
 */
#include <linux/of.h>

#include "clk.h"

CLK_OF_DECLARE(socfpga_pll_clk, "altr,socfpga-pll-clock", socfpga_pll_init);
CLK_OF_DECLARE(socfpga_perip_clk, "altr,socfpga-perip-clk", socfpga_periph_init);
CLK_OF_DECLARE(socfpga_gate_clk, "altr,socfpga-gate-clk", socfpga_gate_init);
CLK_OF_DECLARE(socfpga_a10_pll_clk, "altr,socfpga-a10-pll-clock",
	       socfpga_a10_pll_init);
CLK_OF_DECLARE(socfpga_a10_perip_clk, "altr,socfpga-a10-perip-clk",
	       socfpga_a10_periph_init);
CLK_OF_DECLARE(socfpga_a10_gate_clk, "altr,socfpga-a10-gate-clk",
	       socfpga_a10_gate_init);
