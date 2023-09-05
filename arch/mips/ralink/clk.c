// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/clkdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <asm/mach-ralink/ralink_regs.h>

#include <asm/time.h>

#include "common.h"

static const char *clk_cpu(int *idx)
{
	switch (ralink_soc) {
	case RT2880_SOC:
		*idx = 0;
		return "ralink,rt2880-sysc";
	case RT3883_SOC:
		*idx = 0;
		return "ralink,rt3883-sysc";
	case RT305X_SOC_RT3050:
		*idx = 0;
		return "ralink,rt3050-sysc";
	case RT305X_SOC_RT3052:
		*idx = 0;
		return "ralink,rt3052-sysc";
	case RT305X_SOC_RT3350:
		*idx = 1;
		return "ralink,rt3350-sysc";
	case RT305X_SOC_RT3352:
		*idx = 1;
		return "ralink,rt3352-sysc";
	case RT305X_SOC_RT5350:
		*idx = 1;
		return "ralink,rt5350-sysc";
	case MT762X_SOC_MT7620A:
		*idx = 2;
		return "ralink,mt7620-sysc";
	case MT762X_SOC_MT7620N:
		*idx = 2;
		return "ralink,mt7620-sysc";
	case MT762X_SOC_MT7628AN:
		*idx = 1;
		return "ralink,mt7628-sysc";
	case MT762X_SOC_MT7688:
		*idx = 1;
		return "ralink,mt7688-sysc";
	default:
		*idx = -1;
		return "invalid";
	}
}

void __init plat_time_init(void)
{
	struct of_phandle_args clkspec;
	const char *compatible;
	struct clk *clk;
	int cpu_clk_idx;

	ralink_of_remap();

	compatible = clk_cpu(&cpu_clk_idx);
	if (cpu_clk_idx == -1)
		panic("unable to get CPU clock index");

	of_clk_init(NULL);
	clkspec.np = of_find_compatible_node(NULL, NULL, compatible);
	clkspec.args_count = 1;
	clkspec.args[0] = cpu_clk_idx;
	clk = of_clk_get_from_provider(&clkspec);
	if (IS_ERR(clk))
		panic("unable to get CPU clock, err=%ld", PTR_ERR(clk));
	pr_info("CPU Clock: %ldMHz\n", clk_get_rate(clk) / 1000000);
	mips_hpt_frequency = clk_get_rate(clk) / 2;
	clk_put(clk);
	timer_probe();
}
