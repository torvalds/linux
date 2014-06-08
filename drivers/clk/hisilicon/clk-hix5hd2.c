/*
 * Copyright (c) 2014 Linaro Ltd.
 * Copyright (c) 2014 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/of_address.h>
#include <dt-bindings/clock/hix5hd2-clock.h>
#include "clk.h"

static struct hisi_fixed_rate_clock hix5hd2_fixed_rate_clks[] __initdata = {
	{ HIX5HD2_FIXED_1200M, "1200m", NULL, CLK_IS_ROOT, 1200000000, },
	{ HIX5HD2_FIXED_400M, "400m", NULL, CLK_IS_ROOT, 400000000, },
	{ HIX5HD2_FIXED_48M, "48m", NULL, CLK_IS_ROOT, 48000000, },
	{ HIX5HD2_FIXED_24M, "24m", NULL, CLK_IS_ROOT, 24000000, },
	{ HIX5HD2_FIXED_600M, "600m", NULL, CLK_IS_ROOT, 600000000, },
	{ HIX5HD2_FIXED_300M, "300m", NULL, CLK_IS_ROOT, 300000000, },
	{ HIX5HD2_FIXED_75M, "75m", NULL, CLK_IS_ROOT, 75000000, },
	{ HIX5HD2_FIXED_200M, "200m", NULL, CLK_IS_ROOT, 200000000, },
	{ HIX5HD2_FIXED_100M, "100m", NULL, CLK_IS_ROOT, 100000000, },
	{ HIX5HD2_FIXED_40M, "40m", NULL, CLK_IS_ROOT, 40000000, },
	{ HIX5HD2_FIXED_150M, "150m", NULL, CLK_IS_ROOT, 150000000, },
	{ HIX5HD2_FIXED_1728M, "1728m", NULL, CLK_IS_ROOT, 1728000000, },
	{ HIX5HD2_FIXED_28P8M, "28p8m", NULL, CLK_IS_ROOT, 28000000, },
	{ HIX5HD2_FIXED_432M, "432m", NULL, CLK_IS_ROOT, 432000000, },
	{ HIX5HD2_FIXED_345P6M, "345p6m", NULL, CLK_IS_ROOT, 345000000, },
	{ HIX5HD2_FIXED_288M, "288m", NULL, CLK_IS_ROOT, 288000000, },
	{ HIX5HD2_FIXED_60M,	"60m", NULL, CLK_IS_ROOT, 60000000, },
	{ HIX5HD2_FIXED_750M, "750m", NULL, CLK_IS_ROOT, 750000000, },
	{ HIX5HD2_FIXED_500M, "500m", NULL, CLK_IS_ROOT, 500000000, },
	{ HIX5HD2_FIXED_54M,	"54m", NULL, CLK_IS_ROOT, 54000000, },
	{ HIX5HD2_FIXED_27M, "27m", NULL, CLK_IS_ROOT, 27000000, },
	{ HIX5HD2_FIXED_1500M, "1500m", NULL, CLK_IS_ROOT, 1500000000, },
	{ HIX5HD2_FIXED_375M, "375m", NULL, CLK_IS_ROOT, 375000000, },
	{ HIX5HD2_FIXED_187M, "187m", NULL, CLK_IS_ROOT, 187000000, },
	{ HIX5HD2_FIXED_250M, "250m", NULL, CLK_IS_ROOT, 250000000, },
	{ HIX5HD2_FIXED_125M, "125m", NULL, CLK_IS_ROOT, 125000000, },
	{ HIX5HD2_FIXED_2P02M, "2m", NULL, CLK_IS_ROOT, 2000000, },
	{ HIX5HD2_FIXED_50M, "50m", NULL, CLK_IS_ROOT, 50000000, },
	{ HIX5HD2_FIXED_25M, "25m", NULL, CLK_IS_ROOT, 25000000, },
	{ HIX5HD2_FIXED_83M, "83m", NULL, CLK_IS_ROOT, 83333333, },
};

static const char *sfc_mux_p[] __initconst = {
		"24m", "150m", "200m", "100m", "75m", };
static u32 sfc_mux_table[] = {0, 4, 5, 6, 7};

static const char *sdio1_mux_p[] __initconst = {
		"75m", "100m", "50m", "15m", };
static u32 sdio1_mux_table[] = {0, 1, 2, 3};

static const char *fephy_mux_p[] __initconst = { "25m", "125m"};
static u32 fephy_mux_table[] = {0, 1};


static struct hisi_mux_clock hix5hd2_mux_clks[] __initdata = {
	{ HIX5HD2_SFC_MUX, "sfc_mux", sfc_mux_p, ARRAY_SIZE(sfc_mux_p),
		CLK_SET_RATE_PARENT, 0x5c, 8, 3, 0, sfc_mux_table, },
	{ HIX5HD2_MMC_MUX, "mmc_mux", sdio1_mux_p, ARRAY_SIZE(sdio1_mux_p),
		CLK_SET_RATE_PARENT, 0xa0, 8, 2, 0, sdio1_mux_table, },
	{ HIX5HD2_FEPHY_MUX, "fephy_mux",
		fephy_mux_p, ARRAY_SIZE(fephy_mux_p),
		CLK_SET_RATE_PARENT, 0x120, 8, 2, 0, fephy_mux_table, },
};

static struct hisi_gate_clock hix5hd2_gate_clks[] __initdata = {
	/*sfc*/
	{ HIX5HD2_SFC_CLK, "clk_sfc", "sfc_mux",
		CLK_SET_RATE_PARENT, 0x5c, 0, 0, },
	{ HIX5HD2_SFC_RST, "rst_sfc", "clk_sfc",
		CLK_SET_RATE_PARENT, 0x5c, 4, CLK_GATE_SET_TO_DISABLE, },
	/*sdio1*/
	{ HIX5HD2_MMC_BIU_CLK, "clk_mmc_biu", "200m",
		CLK_SET_RATE_PARENT, 0xa0, 0, 0, },
	{ HIX5HD2_MMC_CIU_CLK, "clk_mmc_ciu", "mmc_mux",
		CLK_SET_RATE_PARENT, 0xa0, 1, 0, },
	{ HIX5HD2_MMC_CIU_RST, "rst_mmc_ciu", "clk_mmc_ciu",
		CLK_SET_RATE_PARENT, 0xa0, 4, CLK_GATE_SET_TO_DISABLE, },
};

static void __init hix5hd2_clk_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;

	clk_data = hisi_clk_init(np, HIX5HD2_NR_CLKS);
	if (!clk_data)
		return;

	hisi_clk_register_fixed_rate(hix5hd2_fixed_rate_clks,
				     ARRAY_SIZE(hix5hd2_fixed_rate_clks),
				     clk_data);
	hisi_clk_register_mux(hix5hd2_mux_clks, ARRAY_SIZE(hix5hd2_mux_clks),
					clk_data);
	hisi_clk_register_gate(hix5hd2_gate_clks,
			ARRAY_SIZE(hix5hd2_gate_clks), clk_data);
}

CLK_OF_DECLARE(hix5hd2_clk, "hisilicon,hix5hd2-clock", hix5hd2_clk_init);
