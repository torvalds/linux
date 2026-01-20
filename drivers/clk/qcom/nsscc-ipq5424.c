// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/interconnect-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,ipq5424-nsscc.h>
#include <dt-bindings/interconnect/qcom,ipq5424.h>
#include <dt-bindings/reset/qcom,ipq5424-nsscc.h>

#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "reset.h"

/* Need to match the order of clocks in DT binding */
enum {
	DT_CMN_PLL_XO_CLK,
	DT_CMN_PLL_NSS_300M_CLK,
	DT_CMN_PLL_NSS_375M_CLK,
	DT_GCC_GPLL0_OUT_AUX,
	DT_UNIPHY0_NSS_RX_CLK,
	DT_UNIPHY0_NSS_TX_CLK,
	DT_UNIPHY1_NSS_RX_CLK,
	DT_UNIPHY1_NSS_TX_CLK,
	DT_UNIPHY2_NSS_RX_CLK,
	DT_UNIPHY2_NSS_TX_CLK,
};

enum {
	P_CMN_PLL_XO_CLK,
	P_CMN_PLL_NSS_300M_CLK,
	P_CMN_PLL_NSS_375M_CLK,
	P_GCC_GPLL0_OUT_AUX,
	P_UNIPHY0_NSS_RX_CLK,
	P_UNIPHY0_NSS_TX_CLK,
	P_UNIPHY1_NSS_RX_CLK,
	P_UNIPHY1_NSS_TX_CLK,
	P_UNIPHY2_NSS_RX_CLK,
	P_UNIPHY2_NSS_TX_CLK,
};

static const struct parent_map nss_cc_parent_map_0[] = {
	{ P_CMN_PLL_XO_CLK, 0 },
	{ P_GCC_GPLL0_OUT_AUX, 2 },
	{ P_CMN_PLL_NSS_300M_CLK, 5 },
	{ P_CMN_PLL_NSS_375M_CLK, 6 },
};

static const struct clk_parent_data nss_cc_parent_data_0[] = {
	{ .index = DT_CMN_PLL_XO_CLK },
	{ .index = DT_GCC_GPLL0_OUT_AUX },
	{ .index = DT_CMN_PLL_NSS_300M_CLK },
	{ .index = DT_CMN_PLL_NSS_375M_CLK },
};

static const struct parent_map nss_cc_parent_map_1[] = {
	{ P_CMN_PLL_XO_CLK, 0 },
	{ P_GCC_GPLL0_OUT_AUX, 2 },
	{ P_UNIPHY0_NSS_RX_CLK, 3 },
	{ P_UNIPHY0_NSS_TX_CLK, 4 },
	{ P_CMN_PLL_NSS_300M_CLK, 5 },
	{ P_CMN_PLL_NSS_375M_CLK, 6 },
};

static const struct clk_parent_data nss_cc_parent_data_1[] = {
	{ .index = DT_CMN_PLL_XO_CLK },
	{ .index = DT_GCC_GPLL0_OUT_AUX },
	{ .index = DT_UNIPHY0_NSS_RX_CLK },
	{ .index = DT_UNIPHY0_NSS_TX_CLK },
	{ .index = DT_CMN_PLL_NSS_300M_CLK },
	{ .index = DT_CMN_PLL_NSS_375M_CLK },
};

static const struct parent_map nss_cc_parent_map_2[] = {
	{ P_CMN_PLL_XO_CLK, 0 },
	{ P_GCC_GPLL0_OUT_AUX, 2 },
	{ P_UNIPHY1_NSS_RX_CLK, 3 },
	{ P_UNIPHY1_NSS_TX_CLK, 4 },
	{ P_CMN_PLL_NSS_300M_CLK, 5 },
	{ P_CMN_PLL_NSS_375M_CLK, 6 },
};

static const struct clk_parent_data nss_cc_parent_data_2[] = {
	{ .index = DT_CMN_PLL_XO_CLK },
	{ .index = DT_GCC_GPLL0_OUT_AUX },
	{ .index = DT_UNIPHY1_NSS_RX_CLK },
	{ .index = DT_UNIPHY1_NSS_TX_CLK },
	{ .index = DT_CMN_PLL_NSS_300M_CLK },
	{ .index = DT_CMN_PLL_NSS_375M_CLK },
};

static const struct parent_map nss_cc_parent_map_3[] = {
	{ P_CMN_PLL_XO_CLK, 0 },
	{ P_GCC_GPLL0_OUT_AUX, 2 },
	{ P_UNIPHY2_NSS_RX_CLK, 3 },
	{ P_UNIPHY2_NSS_TX_CLK, 4 },
	{ P_CMN_PLL_NSS_300M_CLK, 5 },
	{ P_CMN_PLL_NSS_375M_CLK, 6 },
};

static const struct clk_parent_data nss_cc_parent_data_3[] = {
	{ .index = DT_CMN_PLL_XO_CLK },
	{ .index = DT_GCC_GPLL0_OUT_AUX },
	{ .index = DT_UNIPHY2_NSS_RX_CLK },
	{ .index = DT_UNIPHY2_NSS_TX_CLK },
	{ .index = DT_CMN_PLL_NSS_300M_CLK },
	{ .index = DT_CMN_PLL_NSS_375M_CLK },
};

static const struct freq_tbl ftbl_nss_cc_ce_clk_src[] = {
	F(24000000, P_CMN_PLL_XO_CLK, 1, 0, 0),
	F(375000000, P_CMN_PLL_NSS_375M_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_ce_clk_src = {
	.cmd_rcgr = 0x5e0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nss_cc_parent_map_0,
	.freq_tbl = ftbl_nss_cc_ce_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "nss_cc_ce_clk_src",
		.parent_data = nss_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(nss_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_nss_cc_cfg_clk_src[] = {
	F(100000000, P_GCC_GPLL0_OUT_AUX, 8, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_cfg_clk_src = {
	.cmd_rcgr = 0x6a8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nss_cc_parent_map_0,
	.freq_tbl = ftbl_nss_cc_cfg_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "nss_cc_cfg_clk_src",
		.parent_data = nss_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(nss_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_nss_cc_eip_bfdcd_clk_src[] = {
	F(300000000, P_CMN_PLL_NSS_300M_CLK, 1, 0, 0),
	F(375000000, P_CMN_PLL_NSS_375M_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_eip_bfdcd_clk_src = {
	.cmd_rcgr = 0x644,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nss_cc_parent_map_0,
	.freq_tbl = ftbl_nss_cc_eip_bfdcd_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "nss_cc_eip_bfdcd_clk_src",
		.parent_data = nss_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(nss_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_conf ftbl_nss_cc_port1_rx_clk_src_25[] = {
	C(P_UNIPHY0_NSS_RX_CLK, 12.5, 0, 0),
	C(P_UNIPHY0_NSS_RX_CLK, 5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_port1_rx_clk_src_125[] = {
	C(P_UNIPHY0_NSS_RX_CLK, 2.5, 0, 0),
	C(P_UNIPHY0_NSS_RX_CLK, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_cc_port1_rx_clk_src[] = {
	FMS(24000000, P_CMN_PLL_XO_CLK, 1, 0, 0),
	FM(25000000, ftbl_nss_cc_port1_rx_clk_src_25),
	FMS(78125000, P_UNIPHY0_NSS_RX_CLK, 4, 0, 0),
	FM(125000000, ftbl_nss_cc_port1_rx_clk_src_125),
	FMS(156250000, P_UNIPHY0_NSS_RX_CLK, 2, 0, 0),
	FMS(312500000, P_UNIPHY0_NSS_RX_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_port1_rx_clk_src = {
	.cmd_rcgr = 0x4b4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nss_cc_parent_map_1,
	.freq_multi_tbl = ftbl_nss_cc_port1_rx_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "nss_cc_port1_rx_clk_src",
		.parent_data = nss_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(nss_cc_parent_data_1),
		.ops = &clk_rcg2_fm_ops,
	},
};

static const struct freq_conf ftbl_nss_cc_port1_tx_clk_src_25[] = {
	C(P_UNIPHY0_NSS_TX_CLK, 12.5, 0, 0),
	C(P_UNIPHY0_NSS_TX_CLK, 5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_port1_tx_clk_src_125[] = {
	C(P_UNIPHY0_NSS_TX_CLK, 2.5, 0, 0),
	C(P_UNIPHY0_NSS_TX_CLK, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_cc_port1_tx_clk_src[] = {
	FMS(24000000, P_CMN_PLL_XO_CLK, 1, 0, 0),
	FM(25000000, ftbl_nss_cc_port1_tx_clk_src_25),
	FMS(78125000, P_UNIPHY0_NSS_TX_CLK, 4, 0, 0),
	FM(125000000, ftbl_nss_cc_port1_tx_clk_src_125),
	FMS(156250000, P_UNIPHY0_NSS_TX_CLK, 2, 0, 0),
	FMS(312500000, P_UNIPHY0_NSS_TX_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_port1_tx_clk_src = {
	.cmd_rcgr = 0x4c0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nss_cc_parent_map_1,
	.freq_multi_tbl = ftbl_nss_cc_port1_tx_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "nss_cc_port1_tx_clk_src",
		.parent_data = nss_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(nss_cc_parent_data_1),
		.ops = &clk_rcg2_fm_ops,
	},
};

static const struct freq_conf ftbl_nss_cc_port2_rx_clk_src_25[] = {
	C(P_UNIPHY1_NSS_RX_CLK, 12.5, 0, 0),
	C(P_UNIPHY1_NSS_RX_CLK, 5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_port2_rx_clk_src_125[] = {
	C(P_UNIPHY1_NSS_RX_CLK, 2.5, 0, 0),
	C(P_UNIPHY1_NSS_RX_CLK, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_cc_port2_rx_clk_src[] = {
	FMS(24000000, P_CMN_PLL_XO_CLK, 1, 0, 0),
	FM(25000000, ftbl_nss_cc_port2_rx_clk_src_25),
	FMS(78125000, P_UNIPHY1_NSS_RX_CLK, 4, 0, 0),
	FM(125000000, ftbl_nss_cc_port2_rx_clk_src_125),
	FMS(156250000, P_UNIPHY1_NSS_RX_CLK, 2, 0, 0),
	FMS(312500000, P_UNIPHY1_NSS_RX_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_port2_rx_clk_src = {
	.cmd_rcgr = 0x4cc,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nss_cc_parent_map_2,
	.freq_multi_tbl = ftbl_nss_cc_port2_rx_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "nss_cc_port2_rx_clk_src",
		.parent_data = nss_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(nss_cc_parent_data_2),
		.ops = &clk_rcg2_fm_ops,
	},
};

static const struct freq_conf ftbl_nss_cc_port2_tx_clk_src_25[] = {
	C(P_UNIPHY1_NSS_TX_CLK, 12.5, 0, 0),
	C(P_UNIPHY1_NSS_TX_CLK, 5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_port2_tx_clk_src_125[] = {
	C(P_UNIPHY1_NSS_TX_CLK, 2.5, 0, 0),
	C(P_UNIPHY1_NSS_TX_CLK, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_cc_port2_tx_clk_src[] = {
	FMS(24000000, P_CMN_PLL_XO_CLK, 1, 0, 0),
	FM(25000000, ftbl_nss_cc_port2_tx_clk_src_25),
	FMS(78125000, P_UNIPHY1_NSS_TX_CLK, 4, 0, 0),
	FM(125000000, ftbl_nss_cc_port2_tx_clk_src_125),
	FMS(156250000, P_UNIPHY1_NSS_TX_CLK, 2, 0, 0),
	FMS(312500000, P_UNIPHY1_NSS_TX_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_port2_tx_clk_src = {
	.cmd_rcgr = 0x4d8,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nss_cc_parent_map_2,
	.freq_multi_tbl = ftbl_nss_cc_port2_tx_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "nss_cc_port2_tx_clk_src",
		.parent_data = nss_cc_parent_data_2,
		.num_parents = ARRAY_SIZE(nss_cc_parent_data_2),
		.ops = &clk_rcg2_fm_ops,
	},
};

static const struct freq_conf ftbl_nss_cc_port3_rx_clk_src_25[] = {
	C(P_UNIPHY2_NSS_RX_CLK, 12.5, 0, 0),
	C(P_UNIPHY2_NSS_RX_CLK, 5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_port3_rx_clk_src_125[] = {
	C(P_UNIPHY2_NSS_RX_CLK, 2.5, 0, 0),
	C(P_UNIPHY2_NSS_RX_CLK, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_cc_port3_rx_clk_src[] = {
	FMS(24000000, P_CMN_PLL_XO_CLK, 1, 0, 0),
	FM(25000000, ftbl_nss_cc_port3_rx_clk_src_25),
	FMS(78125000, P_UNIPHY2_NSS_RX_CLK, 4, 0, 0),
	FM(125000000, ftbl_nss_cc_port3_rx_clk_src_125),
	FMS(156250000, P_UNIPHY2_NSS_RX_CLK, 2, 0, 0),
	FMS(312500000, P_UNIPHY2_NSS_RX_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_port3_rx_clk_src = {
	.cmd_rcgr = 0x4e4,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nss_cc_parent_map_3,
	.freq_multi_tbl = ftbl_nss_cc_port3_rx_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "nss_cc_port3_rx_clk_src",
		.parent_data = nss_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(nss_cc_parent_data_3),
		.ops = &clk_rcg2_fm_ops,
	},
};

static const struct freq_conf ftbl_nss_cc_port3_tx_clk_src_25[] = {
	C(P_UNIPHY2_NSS_TX_CLK, 12.5, 0, 0),
	C(P_UNIPHY2_NSS_TX_CLK, 5, 0, 0),
};

static const struct freq_conf ftbl_nss_cc_port3_tx_clk_src_125[] = {
	C(P_UNIPHY2_NSS_TX_CLK, 2.5, 0, 0),
	C(P_UNIPHY2_NSS_TX_CLK, 1, 0, 0),
};

static const struct freq_multi_tbl ftbl_nss_cc_port3_tx_clk_src[] = {
	FMS(24000000, P_CMN_PLL_XO_CLK, 1, 0, 0),
	FM(25000000, ftbl_nss_cc_port3_tx_clk_src_25),
	FMS(78125000, P_UNIPHY2_NSS_TX_CLK, 4, 0, 0),
	FM(125000000, ftbl_nss_cc_port3_tx_clk_src_125),
	FMS(156250000, P_UNIPHY2_NSS_TX_CLK, 2, 0, 0),
	FMS(312500000, P_UNIPHY2_NSS_TX_CLK, 1, 0, 0),
	{ }
};

static struct clk_rcg2 nss_cc_port3_tx_clk_src = {
	.cmd_rcgr = 0x4f0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nss_cc_parent_map_3,
	.freq_multi_tbl = ftbl_nss_cc_port3_tx_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "nss_cc_port3_tx_clk_src",
		.parent_data = nss_cc_parent_data_3,
		.num_parents = ARRAY_SIZE(nss_cc_parent_data_3),
		.ops = &clk_rcg2_fm_ops,
	},
};

static struct clk_rcg2 nss_cc_ppe_clk_src = {
	.cmd_rcgr = 0x3ec,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = nss_cc_parent_map_0,
	.freq_tbl = ftbl_nss_cc_ce_clk_src,
	.clkr.hw.init = &(const struct clk_init_data){
		.name = "nss_cc_ppe_clk_src",
		.parent_data = nss_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(nss_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_regmap_div nss_cc_port1_rx_div_clk_src = {
	.reg = 0x4bc,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_port1_rx_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&nss_cc_port1_rx_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div nss_cc_port1_tx_div_clk_src = {
	.reg = 0x4c8,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_port1_tx_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&nss_cc_port1_tx_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div nss_cc_port2_rx_div_clk_src = {
	.reg = 0x4d4,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_port2_rx_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&nss_cc_port2_rx_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div nss_cc_port2_tx_div_clk_src = {
	.reg = 0x4e0,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_port2_tx_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&nss_cc_port2_tx_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div nss_cc_port3_rx_div_clk_src = {
	.reg = 0x4ec,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_port3_rx_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&nss_cc_port3_rx_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div nss_cc_port3_tx_div_clk_src = {
	.reg = 0x4f8,
	.shift = 0,
	.width = 9,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_port3_tx_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&nss_cc_port3_tx_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ops,
	},
};

static struct clk_regmap_div nss_cc_xgmac0_ptp_ref_div_clk_src = {
	.reg = 0x3f4,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_xgmac0_ptp_ref_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&nss_cc_ppe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div nss_cc_xgmac1_ptp_ref_div_clk_src = {
	.reg = 0x3f8,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_xgmac1_ptp_ref_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&nss_cc_ppe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_regmap_div nss_cc_xgmac2_ptp_ref_div_clk_src = {
	.reg = 0x3fc,
	.shift = 0,
	.width = 4,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nss_cc_xgmac2_ptp_ref_div_clk_src",
		.parent_hws = (const struct clk_hw*[]){
			&nss_cc_ppe_clk_src.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_regmap_div_ro_ops,
	},
};

static struct clk_branch nss_cc_ce_apb_clk = {
	.halt_reg = 0x5e8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5e8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_ce_apb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ce_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_ce_axi_clk = {
	.halt_reg = 0x5ec,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5ec,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_ce_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ce_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_debug_clk = {
	.halt_reg = 0x70c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x70c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_debug_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_eip_clk = {
	.halt_reg = 0x658,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x658,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_eip_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_eip_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_nss_csr_clk = {
	.halt_reg = 0x6b0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6b0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_nss_csr_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_cfg_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_nssnoc_ce_apb_clk = {
	.halt_reg = 0x5f4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5f4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_nssnoc_ce_apb_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ce_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_nssnoc_ce_axi_clk = {
	.halt_reg = 0x5f8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x5f8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_nssnoc_ce_axi_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ce_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_nssnoc_eip_clk = {
	.halt_reg = 0x660,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x660,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_nssnoc_eip_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_eip_bfdcd_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_nssnoc_nss_csr_clk = {
	.halt_reg = 0x6b4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x6b4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_nssnoc_nss_csr_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_cfg_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_nssnoc_ppe_cfg_clk = {
	.halt_reg = 0x444,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x444,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_nssnoc_ppe_cfg_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_nssnoc_ppe_clk = {
	.halt_reg = 0x440,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x440,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_nssnoc_ppe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_port1_mac_clk = {
	.halt_reg = 0x428,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x428,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_port1_mac_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_port1_rx_clk = {
	.halt_reg = 0x4fc,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x4fc,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_port1_rx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port1_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_port1_tx_clk = {
	.halt_reg = 0x504,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x504,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_port1_tx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port1_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_port2_mac_clk = {
	.halt_reg = 0x430,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x430,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_port2_mac_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_port2_rx_clk = {
	.halt_reg = 0x50c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x50c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_port2_rx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port2_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_port2_tx_clk = {
	.halt_reg = 0x514,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x514,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_port2_tx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port2_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_port3_mac_clk = {
	.halt_reg = 0x438,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x438,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_port3_mac_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_port3_rx_clk = {
	.halt_reg = 0x51c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x51c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_port3_rx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port3_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_port3_tx_clk = {
	.halt_reg = 0x524,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x524,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_port3_tx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port3_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_ppe_edma_cfg_clk = {
	.halt_reg = 0x424,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x424,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_ppe_edma_cfg_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_ppe_edma_clk = {
	.halt_reg = 0x41c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x41c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_ppe_edma_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_ppe_switch_btq_clk = {
	.halt_reg = 0x408,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x408,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_ppe_switch_btq_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_ppe_switch_cfg_clk = {
	.halt_reg = 0x418,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x418,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_ppe_switch_cfg_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_ppe_switch_clk = {
	.halt_reg = 0x410,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x410,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_ppe_switch_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_ppe_switch_ipe_clk = {
	.halt_reg = 0x400,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x400,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_ppe_switch_ipe_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_ppe_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_uniphy_port1_rx_clk = {
	.halt_reg = 0x57c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x57c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_uniphy_port1_rx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port1_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_uniphy_port1_tx_clk = {
	.halt_reg = 0x580,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x580,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_uniphy_port1_tx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port1_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_uniphy_port2_rx_clk = {
	.halt_reg = 0x584,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x584,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_uniphy_port2_rx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port2_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_uniphy_port2_tx_clk = {
	.halt_reg = 0x588,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x588,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_uniphy_port2_tx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port2_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_uniphy_port3_rx_clk = {
	.halt_reg = 0x58c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x58c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_uniphy_port3_rx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port3_rx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_uniphy_port3_tx_clk = {
	.halt_reg = 0x590,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x590,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_uniphy_port3_tx_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_port3_tx_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_xgmac0_ptp_ref_clk = {
	.halt_reg = 0x448,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x448,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_xgmac0_ptp_ref_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_xgmac0_ptp_ref_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_xgmac1_ptp_ref_clk = {
	.halt_reg = 0x44c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x44c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_xgmac1_ptp_ref_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_xgmac1_ptp_ref_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nss_cc_xgmac2_ptp_ref_clk = {
	.halt_reg = 0x450,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x450,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data){
			.name = "nss_cc_xgmac2_ptp_ref_clk",
			.parent_hws = (const struct clk_hw*[]){
				&nss_cc_xgmac2_ptp_ref_div_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *nss_cc_ipq5424_clocks[] = {
	[NSS_CC_CE_APB_CLK] = &nss_cc_ce_apb_clk.clkr,
	[NSS_CC_CE_AXI_CLK] = &nss_cc_ce_axi_clk.clkr,
	[NSS_CC_CE_CLK_SRC] = &nss_cc_ce_clk_src.clkr,
	[NSS_CC_CFG_CLK_SRC] = &nss_cc_cfg_clk_src.clkr,
	[NSS_CC_DEBUG_CLK] = &nss_cc_debug_clk.clkr,
	[NSS_CC_EIP_BFDCD_CLK_SRC] = &nss_cc_eip_bfdcd_clk_src.clkr,
	[NSS_CC_EIP_CLK] = &nss_cc_eip_clk.clkr,
	[NSS_CC_NSS_CSR_CLK] = &nss_cc_nss_csr_clk.clkr,
	[NSS_CC_NSSNOC_CE_APB_CLK] = &nss_cc_nssnoc_ce_apb_clk.clkr,
	[NSS_CC_NSSNOC_CE_AXI_CLK] = &nss_cc_nssnoc_ce_axi_clk.clkr,
	[NSS_CC_NSSNOC_EIP_CLK] = &nss_cc_nssnoc_eip_clk.clkr,
	[NSS_CC_NSSNOC_NSS_CSR_CLK] = &nss_cc_nssnoc_nss_csr_clk.clkr,
	[NSS_CC_NSSNOC_PPE_CFG_CLK] = &nss_cc_nssnoc_ppe_cfg_clk.clkr,
	[NSS_CC_NSSNOC_PPE_CLK] = &nss_cc_nssnoc_ppe_clk.clkr,
	[NSS_CC_PORT1_MAC_CLK] = &nss_cc_port1_mac_clk.clkr,
	[NSS_CC_PORT1_RX_CLK] = &nss_cc_port1_rx_clk.clkr,
	[NSS_CC_PORT1_RX_CLK_SRC] = &nss_cc_port1_rx_clk_src.clkr,
	[NSS_CC_PORT1_RX_DIV_CLK_SRC] = &nss_cc_port1_rx_div_clk_src.clkr,
	[NSS_CC_PORT1_TX_CLK] = &nss_cc_port1_tx_clk.clkr,
	[NSS_CC_PORT1_TX_CLK_SRC] = &nss_cc_port1_tx_clk_src.clkr,
	[NSS_CC_PORT1_TX_DIV_CLK_SRC] = &nss_cc_port1_tx_div_clk_src.clkr,
	[NSS_CC_PORT2_MAC_CLK] = &nss_cc_port2_mac_clk.clkr,
	[NSS_CC_PORT2_RX_CLK] = &nss_cc_port2_rx_clk.clkr,
	[NSS_CC_PORT2_RX_CLK_SRC] = &nss_cc_port2_rx_clk_src.clkr,
	[NSS_CC_PORT2_RX_DIV_CLK_SRC] = &nss_cc_port2_rx_div_clk_src.clkr,
	[NSS_CC_PORT2_TX_CLK] = &nss_cc_port2_tx_clk.clkr,
	[NSS_CC_PORT2_TX_CLK_SRC] = &nss_cc_port2_tx_clk_src.clkr,
	[NSS_CC_PORT2_TX_DIV_CLK_SRC] = &nss_cc_port2_tx_div_clk_src.clkr,
	[NSS_CC_PORT3_MAC_CLK] = &nss_cc_port3_mac_clk.clkr,
	[NSS_CC_PORT3_RX_CLK] = &nss_cc_port3_rx_clk.clkr,
	[NSS_CC_PORT3_RX_CLK_SRC] = &nss_cc_port3_rx_clk_src.clkr,
	[NSS_CC_PORT3_RX_DIV_CLK_SRC] = &nss_cc_port3_rx_div_clk_src.clkr,
	[NSS_CC_PORT3_TX_CLK] = &nss_cc_port3_tx_clk.clkr,
	[NSS_CC_PORT3_TX_CLK_SRC] = &nss_cc_port3_tx_clk_src.clkr,
	[NSS_CC_PORT3_TX_DIV_CLK_SRC] = &nss_cc_port3_tx_div_clk_src.clkr,
	[NSS_CC_PPE_CLK_SRC] = &nss_cc_ppe_clk_src.clkr,
	[NSS_CC_PPE_EDMA_CFG_CLK] = &nss_cc_ppe_edma_cfg_clk.clkr,
	[NSS_CC_PPE_EDMA_CLK] = &nss_cc_ppe_edma_clk.clkr,
	[NSS_CC_PPE_SWITCH_BTQ_CLK] = &nss_cc_ppe_switch_btq_clk.clkr,
	[NSS_CC_PPE_SWITCH_CFG_CLK] = &nss_cc_ppe_switch_cfg_clk.clkr,
	[NSS_CC_PPE_SWITCH_CLK] = &nss_cc_ppe_switch_clk.clkr,
	[NSS_CC_PPE_SWITCH_IPE_CLK] = &nss_cc_ppe_switch_ipe_clk.clkr,
	[NSS_CC_UNIPHY_PORT1_RX_CLK] = &nss_cc_uniphy_port1_rx_clk.clkr,
	[NSS_CC_UNIPHY_PORT1_TX_CLK] = &nss_cc_uniphy_port1_tx_clk.clkr,
	[NSS_CC_UNIPHY_PORT2_RX_CLK] = &nss_cc_uniphy_port2_rx_clk.clkr,
	[NSS_CC_UNIPHY_PORT2_TX_CLK] = &nss_cc_uniphy_port2_tx_clk.clkr,
	[NSS_CC_UNIPHY_PORT3_RX_CLK] = &nss_cc_uniphy_port3_rx_clk.clkr,
	[NSS_CC_UNIPHY_PORT3_TX_CLK] = &nss_cc_uniphy_port3_tx_clk.clkr,
	[NSS_CC_XGMAC0_PTP_REF_CLK] = &nss_cc_xgmac0_ptp_ref_clk.clkr,
	[NSS_CC_XGMAC0_PTP_REF_DIV_CLK_SRC] = &nss_cc_xgmac0_ptp_ref_div_clk_src.clkr,
	[NSS_CC_XGMAC1_PTP_REF_CLK] = &nss_cc_xgmac1_ptp_ref_clk.clkr,
	[NSS_CC_XGMAC1_PTP_REF_DIV_CLK_SRC] = &nss_cc_xgmac1_ptp_ref_div_clk_src.clkr,
	[NSS_CC_XGMAC2_PTP_REF_CLK] = &nss_cc_xgmac2_ptp_ref_clk.clkr,
	[NSS_CC_XGMAC2_PTP_REF_DIV_CLK_SRC] = &nss_cc_xgmac2_ptp_ref_div_clk_src.clkr,
};

static const struct qcom_reset_map nss_cc_ipq5424_resets[] = {
	[NSS_CC_CE_APB_CLK_ARES] = { 0x5e8, 2 },
	[NSS_CC_CE_AXI_CLK_ARES] = { 0x5ec, 2 },
	[NSS_CC_DEBUG_CLK_ARES] = { 0x70c, 2 },
	[NSS_CC_EIP_CLK_ARES] = { 0x658, 2 },
	[NSS_CC_NSS_CSR_CLK_ARES] = { 0x6b0, 2 },
	[NSS_CC_NSSNOC_CE_APB_CLK_ARES] = { 0x5f4, 2 },
	[NSS_CC_NSSNOC_CE_AXI_CLK_ARES] = { 0x5f8, 2 },
	[NSS_CC_NSSNOC_EIP_CLK_ARES] = { 0x660, 2 },
	[NSS_CC_NSSNOC_NSS_CSR_CLK_ARES] = { 0x6b4, 2 },
	[NSS_CC_NSSNOC_PPE_CLK_ARES] = { 0x440, 2 },
	[NSS_CC_NSSNOC_PPE_CFG_CLK_ARES] = { 0x444, 2 },
	[NSS_CC_PORT1_MAC_CLK_ARES] = { 0x428, 2 },
	[NSS_CC_PORT1_RX_CLK_ARES] = { 0x4fc, 2 },
	[NSS_CC_PORT1_TX_CLK_ARES] = { 0x504, 2 },
	[NSS_CC_PORT2_MAC_CLK_ARES] = { 0x430, 2 },
	[NSS_CC_PORT2_RX_CLK_ARES] = { 0x50c, 2 },
	[NSS_CC_PORT2_TX_CLK_ARES] = { 0x514, 2 },
	[NSS_CC_PORT3_MAC_CLK_ARES] = { 0x438, 2 },
	[NSS_CC_PORT3_RX_CLK_ARES] = { 0x51c, 2 },
	[NSS_CC_PORT3_TX_CLK_ARES] = { 0x524, 2 },
	[NSS_CC_PPE_BCR] = { 0x3e8 },
	[NSS_CC_PPE_EDMA_CLK_ARES] = { 0x41c, 2 },
	[NSS_CC_PPE_EDMA_CFG_CLK_ARES] = { 0x424, 2 },
	[NSS_CC_PPE_SWITCH_BTQ_CLK_ARES] = { 0x408, 2 },
	[NSS_CC_PPE_SWITCH_CLK_ARES] = { 0x410, 2 },
	[NSS_CC_PPE_SWITCH_CFG_CLK_ARES] = { 0x418, 2 },
	[NSS_CC_PPE_SWITCH_IPE_CLK_ARES] = { 0x400, 2 },
	[NSS_CC_UNIPHY_PORT1_RX_CLK_ARES] = { 0x57c, 2 },
	[NSS_CC_UNIPHY_PORT1_TX_CLK_ARES] = { 0x580, 2 },
	[NSS_CC_UNIPHY_PORT2_RX_CLK_ARES] = { 0x584, 2 },
	[NSS_CC_UNIPHY_PORT2_TX_CLK_ARES] = { 0x588, 2 },
	[NSS_CC_UNIPHY_PORT3_RX_CLK_ARES] = { 0x58c, 2 },
	[NSS_CC_UNIPHY_PORT3_TX_CLK_ARES] = { 0x590, 2 },
	[NSS_CC_XGMAC0_PTP_REF_CLK_ARES] = { 0x448, 2 },
	[NSS_CC_XGMAC1_PTP_REF_CLK_ARES] = { 0x44c, 2 },
	[NSS_CC_XGMAC2_PTP_REF_CLK_ARES] = { 0x450, 2 },
};

static const struct regmap_config nss_cc_ipq5424_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x800,
	.fast_io = true,
};

static const struct qcom_icc_hws_data icc_ipq5424_nss_hws[] = {
	{ MASTER_NSSNOC_PPE, SLAVE_NSSNOC_PPE, NSS_CC_NSSNOC_PPE_CLK },
	{ MASTER_NSSNOC_PPE_CFG, SLAVE_NSSNOC_PPE_CFG, NSS_CC_NSSNOC_PPE_CFG_CLK },
	{ MASTER_NSSNOC_NSS_CSR, SLAVE_NSSNOC_NSS_CSR, NSS_CC_NSSNOC_NSS_CSR_CLK },
	{ MASTER_NSSNOC_CE_AXI, SLAVE_NSSNOC_CE_AXI, NSS_CC_NSSNOC_CE_AXI_CLK},
	{ MASTER_NSSNOC_CE_APB, SLAVE_NSSNOC_CE_APB, NSS_CC_NSSNOC_CE_APB_CLK},
	{ MASTER_NSSNOC_EIP, SLAVE_NSSNOC_EIP, NSS_CC_NSSNOC_EIP_CLK},
};

#define IPQ_NSSCC_ID	(5424 * 2) /* some unique value */

static const struct qcom_cc_desc nss_cc_ipq5424_desc = {
	.config = &nss_cc_ipq5424_regmap_config,
	.clks = nss_cc_ipq5424_clocks,
	.num_clks = ARRAY_SIZE(nss_cc_ipq5424_clocks),
	.resets = nss_cc_ipq5424_resets,
	.num_resets = ARRAY_SIZE(nss_cc_ipq5424_resets),
	.icc_hws = icc_ipq5424_nss_hws,
	.num_icc_hws = ARRAY_SIZE(icc_ipq5424_nss_hws),
	.icc_first_node_id = IPQ_NSSCC_ID,
};

static const struct dev_pm_ops nss_cc_ipq5424_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static const struct of_device_id nss_cc_ipq5424_match_table[] = {
	{ .compatible = "qcom,ipq5424-nsscc" },
	{ }
};
MODULE_DEVICE_TABLE(of, nss_cc_ipq5424_match_table);

static int nss_cc_ipq5424_probe(struct platform_device *pdev)
{
	int ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Fail to enable runtime PM\n");

	ret = devm_pm_clk_create(&pdev->dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Fail to create PM clock\n");

	ret = pm_clk_add(&pdev->dev, "bus");
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Fail to add bus clock\n");

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Fail to resume\n");

	ret = qcom_cc_probe(pdev, &nss_cc_ipq5424_desc);
	pm_runtime_put(&pdev->dev);

	return ret;
}

static struct platform_driver nss_cc_ipq5424_driver = {
	.probe = nss_cc_ipq5424_probe,
	.driver = {
		.name = "qcom,ipq5424-nsscc",
		.of_match_table = nss_cc_ipq5424_match_table,
		.pm = &nss_cc_ipq5424_pm_ops,
		.sync_state = icc_sync_state,
	},
};
module_platform_driver(nss_cc_ipq5424_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. NSSCC IPQ5424 Driver");
MODULE_LICENSE("GPL");
