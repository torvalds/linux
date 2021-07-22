/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 SiFive, Inc.
 * Zong Li
 */

#ifndef __SIFIVE_CLK_FU740_PRCI_H
#define __SIFIVE_CLK_FU740_PRCI_H

#include "sifive-prci.h"

#define NUM_CLOCK_FU740	9

extern struct __prci_clock __prci_init_clocks_fu740[NUM_CLOCK_FU740];

static const struct prci_clk_desc prci_clk_fu740 = {
	.clks = __prci_init_clocks_fu740,
	.num_clks = ARRAY_SIZE(__prci_init_clocks_fu740),
};

#endif /* __SIFIVE_CLK_FU740_PRCI_H */
