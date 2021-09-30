/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020, Rockchip Electronics Co., Ltd.
 */

#ifndef __ROCKCHIP_DMC_TIMING_H__
#define __ROCKCHIP_DMC_TIMING_H__

/* hope this define can adapt all future platfor */
static const char * const px30_dts_timing[] = {
	"ddr2_speed_bin",
	"ddr3_speed_bin",
	"ddr4_speed_bin",
	"pd_idle",
	"sr_idle",
	"sr_mc_gate_idle",
	"srpd_lite_idle",
	"standby_idle",

	"auto_pd_dis_freq",
	"auto_sr_dis_freq",
	"ddr2_dll_dis_freq",
	"ddr3_dll_dis_freq",
	"ddr4_dll_dis_freq",
	"phy_dll_dis_freq",

	"ddr2_odt_dis_freq",
	"phy_ddr2_odt_dis_freq",
	"ddr2_drv",
	"ddr2_odt",
	"phy_ddr2_ca_drv",
	"phy_ddr2_ck_drv",
	"phy_ddr2_dq_drv",
	"phy_ddr2_odt",

	"ddr3_odt_dis_freq",
	"phy_ddr3_odt_dis_freq",
	"ddr3_drv",
	"ddr3_odt",
	"phy_ddr3_ca_drv",
	"phy_ddr3_ck_drv",
	"phy_ddr3_dq_drv",
	"phy_ddr3_odt",

	"phy_lpddr2_odt_dis_freq",
	"lpddr2_drv",
	"phy_lpddr2_ca_drv",
	"phy_lpddr2_ck_drv",
	"phy_lpddr2_dq_drv",
	"phy_lpddr2_odt",

	"lpddr3_odt_dis_freq",
	"phy_lpddr3_odt_dis_freq",
	"lpddr3_drv",
	"lpddr3_odt",
	"phy_lpddr3_ca_drv",
	"phy_lpddr3_ck_drv",
	"phy_lpddr3_dq_drv",
	"phy_lpddr3_odt",

	"lpddr4_odt_dis_freq",
	"phy_lpddr4_odt_dis_freq",
	"lpddr4_drv",
	"lpddr4_dq_odt",
	"lpddr4_ca_odt",
	"phy_lpddr4_ca_drv",
	"phy_lpddr4_ck_cs_drv",
	"phy_lpddr4_dq_drv",
	"phy_lpddr4_odt",

	"ddr4_odt_dis_freq",
	"phy_ddr4_odt_dis_freq",
	"ddr4_drv",
	"ddr4_odt",
	"phy_ddr4_ca_drv",
	"phy_ddr4_ck_drv",
	"phy_ddr4_dq_drv",
	"phy_ddr4_odt",
};

struct px30_ddr_dts_config_timing {
	unsigned int ddr2_speed_bin;
	unsigned int ddr3_speed_bin;
	unsigned int ddr4_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;

	unsigned int auto_pd_dis_freq;
	unsigned int auto_sr_dis_freq;
	/* for ddr2 only */
	unsigned int ddr2_dll_dis_freq;
	/* for ddr3 only */
	unsigned int ddr3_dll_dis_freq;
	/* for ddr4 only */
	unsigned int ddr4_dll_dis_freq;
	unsigned int phy_dll_dis_freq;

	unsigned int ddr2_odt_dis_freq;
	unsigned int phy_ddr2_odt_dis_freq;
	unsigned int ddr2_drv;
	unsigned int ddr2_odt;
	unsigned int phy_ddr2_ca_drv;
	unsigned int phy_ddr2_ck_drv;
	unsigned int phy_ddr2_dq_drv;
	unsigned int phy_ddr2_odt;

	unsigned int ddr3_odt_dis_freq;
	unsigned int phy_ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_ca_drv;
	unsigned int phy_ddr3_ck_drv;
	unsigned int phy_ddr3_dq_drv;
	unsigned int phy_ddr3_odt;

	unsigned int phy_lpddr2_odt_dis_freq;
	unsigned int lpddr2_drv;
	unsigned int phy_lpddr2_ca_drv;
	unsigned int phy_lpddr2_ck_drv;
	unsigned int phy_lpddr2_dq_drv;
	unsigned int phy_lpddr2_odt;

	unsigned int lpddr3_odt_dis_freq;
	unsigned int phy_lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_ca_drv;
	unsigned int phy_lpddr3_ck_drv;
	unsigned int phy_lpddr3_dq_drv;
	unsigned int phy_lpddr3_odt;

	unsigned int lpddr4_odt_dis_freq;
	unsigned int phy_lpddr4_odt_dis_freq;
	unsigned int lpddr4_drv;
	unsigned int lpddr4_dq_odt;
	unsigned int lpddr4_ca_odt;
	unsigned int phy_lpddr4_ca_drv;
	unsigned int phy_lpddr4_ck_cs_drv;
	unsigned int phy_lpddr4_dq_drv;
	unsigned int phy_lpddr4_odt;

	unsigned int ddr4_odt_dis_freq;
	unsigned int phy_ddr4_odt_dis_freq;
	unsigned int ddr4_drv;
	unsigned int ddr4_odt;
	unsigned int phy_ddr4_ca_drv;
	unsigned int phy_ddr4_ck_drv;
	unsigned int phy_ddr4_dq_drv;
	unsigned int phy_ddr4_odt;

	unsigned int ca_skew[15];
	unsigned int cs0_skew[44];
	unsigned int cs1_skew[44];

	unsigned int available;
};

static const char * const rk1808_dts_ca_timing[] = {
	"a0_ddr3a9_de-skew",
	"a1_ddr3a14_de-skew",
	"a2_ddr3a13_de-skew",
	"a3_ddr3a11_de-skew",
	"a4_ddr3a2_de-skew",
	"a5_ddr3a4_de-skew",
	"a6_ddr3a3_de-skew",
	"a7_ddr3a6_de-skew",
	"a8_ddr3a5_de-skew",
	"a9_ddr3a1_de-skew",
	"a10_ddr3a0_de-skew",
	"a11_ddr3a7_de-skew",
	"a12_ddr3casb_de-skew",
	"a13_ddr3a8_de-skew",
	"a14_ddr3odt0_de-skew",
	"a15_ddr3ba1_de-skew",
	"a16_ddr3rasb_de-skew",
	"a17_ddr3null_de-skew",
	"ba0_ddr3ba2_de-skew",
	"ba1_ddr3a12_de-skew",
	"bg0_ddr3ba0_de-skew",
	"bg1_ddr3web_de-skew",
	"cke_ddr3cke_de-skew",
	"ck_ddr3ck_de-skew",
	"ckb_ddr3ckb_de-skew",
	"csb0_ddr3a10_de-skew",
	"odt0_ddr3a15_de-skew",
	"resetn_ddr3resetn_de-skew",
	"actn_ddr3csb0_de-skew",
	"csb1_ddr3csb1_de-skew",
	"odt1_ddr3odt1_de-skew",
};

static const char * const rk1808_dts_cs0_a_timing[] = {
	"cs0_dm0_rx_de-skew",
	"cs0_dm0_tx_de-skew",
	"cs0_dq0_rx_de-skew",
	"cs0_dq0_tx_de-skew",
	"cs0_dq1_rx_de-skew",
	"cs0_dq1_tx_de-skew",
	"cs0_dq2_rx_de-skew",
	"cs0_dq2_tx_de-skew",
	"cs0_dq3_rx_de-skew",
	"cs0_dq3_tx_de-skew",
	"cs0_dq4_rx_de-skew",
	"cs0_dq4_tx_de-skew",
	"cs0_dq5_rx_de-skew",
	"cs0_dq5_tx_de-skew",
	"cs0_dq6_rx_de-skew",
	"cs0_dq6_tx_de-skew",
	"cs0_dq7_rx_de-skew",
	"cs0_dq7_tx_de-skew",
	"cs0_dqs0p_rx_de-skew",
	"cs0_dqs0p_tx_de-skew",
	"cs0_dqs0n_tx_de-skew",
	"cs0_dm1_rx_de-skew",
	"cs0_dm1_tx_de-skew",
	"cs0_dq8_rx_de-skew",
	"cs0_dq8_tx_de-skew",
	"cs0_dq9_rx_de-skew",
	"cs0_dq9_tx_de-skew",
	"cs0_dq10_rx_de-skew",
	"cs0_dq10_tx_de-skew",
	"cs0_dq11_rx_de-skew",
	"cs0_dq11_tx_de-skew",
	"cs0_dq12_rx_de-skew",
	"cs0_dq12_tx_de-skew",
	"cs0_dq13_rx_de-skew",
	"cs0_dq13_tx_de-skew",
	"cs0_dq14_rx_de-skew",
	"cs0_dq14_tx_de-skew",
	"cs0_dq15_rx_de-skew",
	"cs0_dq15_tx_de-skew",
	"cs0_dqs1p_rx_de-skew",
	"cs0_dqs1p_tx_de-skew",
	"cs0_dqs1n_tx_de-skew",
	"cs0_dqs0n_rx_de-skew",
	"cs0_dqs1n_rx_de-skew",
};

static const char * const rk1808_dts_cs0_b_timing[] = {
	"cs0_dm2_rx_de-skew",
	"cs0_dm2_tx_de-skew",
	"cs0_dq16_rx_de-skew",
	"cs0_dq16_tx_de-skew",
	"cs0_dq17_rx_de-skew",
	"cs0_dq17_tx_de-skew",
	"cs0_dq18_rx_de-skew",
	"cs0_dq18_tx_de-skew",
	"cs0_dq19_rx_de-skew",
	"cs0_dq19_tx_de-skew",
	"cs0_dq20_rx_de-skew",
	"cs0_dq20_tx_de-skew",
	"cs0_dq21_rx_de-skew",
	"cs0_dq21_tx_de-skew",
	"cs0_dq22_rx_de-skew",
	"cs0_dq22_tx_de-skew",
	"cs0_dq23_rx_de-skew",
	"cs0_dq23_tx_de-skew",
	"cs0_dqs2p_rx_de-skew",
	"cs0_dqs2p_tx_de-skew",
	"cs0_dqs2n_tx_de-skew",
	"cs0_dm3_rx_de-skew",
	"cs0_dm3_tx_de-skew",
	"cs0_dq24_rx_de-skew",
	"cs0_dq24_tx_de-skew",
	"cs0_dq25_rx_de-skew",
	"cs0_dq25_tx_de-skew",
	"cs0_dq26_rx_de-skew",
	"cs0_dq26_tx_de-skew",
	"cs0_dq27_rx_de-skew",
	"cs0_dq27_tx_de-skew",
	"cs0_dq28_rx_de-skew",
	"cs0_dq28_tx_de-skew",
	"cs0_dq29_rx_de-skew",
	"cs0_dq29_tx_de-skew",
	"cs0_dq30_rx_de-skew",
	"cs0_dq30_tx_de-skew",
	"cs0_dq31_rx_de-skew",
	"cs0_dq31_tx_de-skew",
	"cs0_dqs3p_rx_de-skew",
	"cs0_dqs3p_tx_de-skew",
	"cs0_dqs3n_tx_de-skew",
	"cs0_dqs2n_rx_de-skew",
	"cs0_dqs3n_rx_de-skew",
};

static const char * const rk1808_dts_cs1_a_timing[] = {
	"cs1_dm0_rx_de-skew",
	"cs1_dm0_tx_de-skew",
	"cs1_dq0_rx_de-skew",
	"cs1_dq0_tx_de-skew",
	"cs1_dq1_rx_de-skew",
	"cs1_dq1_tx_de-skew",
	"cs1_dq2_rx_de-skew",
	"cs1_dq2_tx_de-skew",
	"cs1_dq3_rx_de-skew",
	"cs1_dq3_tx_de-skew",
	"cs1_dq4_rx_de-skew",
	"cs1_dq4_tx_de-skew",
	"cs1_dq5_rx_de-skew",
	"cs1_dq5_tx_de-skew",
	"cs1_dq6_rx_de-skew",
	"cs1_dq6_tx_de-skew",
	"cs1_dq7_rx_de-skew",
	"cs1_dq7_tx_de-skew",
	"cs1_dqs0p_rx_de-skew",
	"cs1_dqs0p_tx_de-skew",
	"cs1_dqs0n_tx_de-skew",
	"cs1_dm1_rx_de-skew",
	"cs1_dm1_tx_de-skew",
	"cs1_dq8_rx_de-skew",
	"cs1_dq8_tx_de-skew",
	"cs1_dq9_rx_de-skew",
	"cs1_dq9_tx_de-skew",
	"cs1_dq10_rx_de-skew",
	"cs1_dq10_tx_de-skew",
	"cs1_dq11_rx_de-skew",
	"cs1_dq11_tx_de-skew",
	"cs1_dq12_rx_de-skew",
	"cs1_dq12_tx_de-skew",
	"cs1_dq13_rx_de-skew",
	"cs1_dq13_tx_de-skew",
	"cs1_dq14_rx_de-skew",
	"cs1_dq14_tx_de-skew",
	"cs1_dq15_rx_de-skew",
	"cs1_dq15_tx_de-skew",
	"cs1_dqs1p_rx_de-skew",
	"cs1_dqs1p_tx_de-skew",
	"cs1_dqs1n_tx_de-skew",
	"cs1_dqs0n_rx_de-skew",
	"cs1_dqs1n_rx_de-skew",
};

static const char * const rk1808_dts_cs1_b_timing[] = {
	"cs1_dm2_rx_de-skew",
	"cs1_dm2_tx_de-skew",
	"cs1_dq16_rx_de-skew",
	"cs1_dq16_tx_de-skew",
	"cs1_dq17_rx_de-skew",
	"cs1_dq17_tx_de-skew",
	"cs1_dq18_rx_de-skew",
	"cs1_dq18_tx_de-skew",
	"cs1_dq19_rx_de-skew",
	"cs1_dq19_tx_de-skew",
	"cs1_dq20_rx_de-skew",
	"cs1_dq20_tx_de-skew",
	"cs1_dq21_rx_de-skew",
	"cs1_dq21_tx_de-skew",
	"cs1_dq22_rx_de-skew",
	"cs1_dq22_tx_de-skew",
	"cs1_dq23_rx_de-skew",
	"cs1_dq23_tx_de-skew",
	"cs1_dqs2p_rx_de-skew",
	"cs1_dqs2p_tx_de-skew",
	"cs1_dqs2n_tx_de-skew",
	"cs1_dm3_rx_de-skew",
	"cs1_dm3_tx_de-skew",
	"cs1_dq24_rx_de-skew",
	"cs1_dq24_tx_de-skew",
	"cs1_dq25_rx_de-skew",
	"cs1_dq25_tx_de-skew",
	"cs1_dq26_rx_de-skew",
	"cs1_dq26_tx_de-skew",
	"cs1_dq27_rx_de-skew",
	"cs1_dq27_tx_de-skew",
	"cs1_dq28_rx_de-skew",
	"cs1_dq28_tx_de-skew",
	"cs1_dq29_rx_de-skew",
	"cs1_dq29_tx_de-skew",
	"cs1_dq30_rx_de-skew",
	"cs1_dq30_tx_de-skew",
	"cs1_dq31_rx_de-skew",
	"cs1_dq31_tx_de-skew",
	"cs1_dqs3p_rx_de-skew",
	"cs1_dqs3p_tx_de-skew",
	"cs1_dqs3n_tx_de-skew",
	"cs1_dqs2n_rx_de-skew",
	"cs1_dqs3n_rx_de-skew",
};

struct rk1808_ddr_dts_config_timing {
	unsigned int ddr2_speed_bin;
	unsigned int ddr3_speed_bin;
	unsigned int ddr4_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;

	unsigned int auto_pd_dis_freq;
	unsigned int auto_sr_dis_freq;
	/* for ddr2 only */
	unsigned int ddr2_dll_dis_freq;
	/* for ddr3 only */
	unsigned int ddr3_dll_dis_freq;
	/* for ddr4 only */
	unsigned int ddr4_dll_dis_freq;
	unsigned int phy_dll_dis_freq;

	unsigned int ddr2_odt_dis_freq;
	unsigned int phy_ddr2_odt_dis_freq;
	unsigned int ddr2_drv;
	unsigned int ddr2_odt;
	unsigned int phy_ddr2_ca_drv;
	unsigned int phy_ddr2_ck_drv;
	unsigned int phy_ddr2_dq_drv;
	unsigned int phy_ddr2_odt;

	unsigned int ddr3_odt_dis_freq;
	unsigned int phy_ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_ca_drv;
	unsigned int phy_ddr3_ck_drv;
	unsigned int phy_ddr3_dq_drv;
	unsigned int phy_ddr3_odt;

	unsigned int phy_lpddr2_odt_dis_freq;
	unsigned int lpddr2_drv;
	unsigned int phy_lpddr2_ca_drv;
	unsigned int phy_lpddr2_ck_drv;
	unsigned int phy_lpddr2_dq_drv;
	unsigned int phy_lpddr2_odt;

	unsigned int lpddr3_odt_dis_freq;
	unsigned int phy_lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_ca_drv;
	unsigned int phy_lpddr3_ck_drv;
	unsigned int phy_lpddr3_dq_drv;
	unsigned int phy_lpddr3_odt;

	unsigned int lpddr4_odt_dis_freq;
	unsigned int phy_lpddr4_odt_dis_freq;
	unsigned int lpddr4_drv;
	unsigned int lpddr4_dq_odt;
	unsigned int lpddr4_ca_odt;
	unsigned int phy_lpddr4_ca_drv;
	unsigned int phy_lpddr4_ck_cs_drv;
	unsigned int phy_lpddr4_dq_drv;
	unsigned int phy_lpddr4_odt;

	unsigned int ddr4_odt_dis_freq;
	unsigned int phy_ddr4_odt_dis_freq;
	unsigned int ddr4_drv;
	unsigned int ddr4_odt;
	unsigned int phy_ddr4_ca_drv;
	unsigned int phy_ddr4_ck_drv;
	unsigned int phy_ddr4_dq_drv;
	unsigned int phy_ddr4_odt;

	unsigned int ca_de_skew[31];
	unsigned int cs0_a_de_skew[44];
	unsigned int cs0_b_de_skew[44];
	unsigned int cs1_a_de_skew[44];
	unsigned int cs1_b_de_skew[44];

	unsigned int available;
};

static const char * const rk3128_dts_timing[] = {
	"ddr3_speed_bin",
	"pd_idle",
	"sr_idle",
	"auto_pd_dis_freq",
	"auto_sr_dis_freq",
	"ddr3_dll_dis_freq",
	"lpddr2_dll_dis_freq",
	"phy_dll_dis_freq",
	"ddr3_odt_dis_freq",
	"phy_ddr3_odt_disb_freq",
	"ddr3_drv",
	"ddr3_odt",
	"phy_ddr3_clk_drv",
	"phy_ddr3_cmd_drv",
	"phy_ddr3_dqs_drv",
	"phy_ddr3_odt",
	"lpddr2_drv",
	"phy_lpddr2_clk_drv",
	"phy_lpddr2_cmd_drv",
	"phy_lpddr2_dqs_drv",
	"ddr_2t",
};

struct rk3128_ddr_dts_config_timing {
	u32 ddr3_speed_bin;
	u32 pd_idle;
	u32 sr_idle;
	u32 auto_pd_dis_freq;
	u32 auto_sr_dis_freq;
	u32 ddr3_dll_dis_freq;
	u32 lpddr2_dll_dis_freq;
	u32 phy_dll_dis_freq;
	u32 ddr3_odt_dis_freq;
	u32 phy_ddr3_odt_disb_freq;
	u32 ddr3_drv;
	u32 ddr3_odt;
	u32 phy_ddr3_clk_drv;
	u32 phy_ddr3_cmd_drv;
	u32 phy_ddr3_dqs_drv;
	u32 phy_ddr3_odt;
	u32 lpddr2_drv;
	u32 phy_lpddr2_clk_drv;
	u32 phy_lpddr2_cmd_drv;
	u32 phy_lpddr2_dqs_drv;
	u32 ddr_2t;
	u32 available;
};

static const char * const rk3228_dts_timing[] = {
	"dram_spd_bin",
	"sr_idle",
	"pd_idle",
	"dram_dll_disb_freq",
	"phy_dll_disb_freq",
	"dram_odt_disb_freq",
	"phy_odt_disb_freq",
	"ddr3_drv",
	"ddr3_odt",
	"lpddr3_drv",
	"lpddr3_odt",
	"lpddr2_drv",
	"phy_ddr3_clk_drv",
	"phy_ddr3_cmd_drv",
	"phy_ddr3_dqs_drv",
	"phy_ddr3_odt",
	"phy_lp23_clk_drv",
	"phy_lp23_cmd_drv",
	"phy_lp23_dqs_drv",
	"phy_lp3_odt"
};

struct rk3228_ddr_dts_config_timing {
	u32 dram_spd_bin;
	u32 sr_idle;
	u32 pd_idle;
	u32 dram_dll_dis_freq;
	u32 phy_dll_dis_freq;
	u32 dram_odt_dis_freq;
	u32 phy_odt_dis_freq;
	u32 ddr3_drv;
	u32 ddr3_odt;
	u32 lpddr3_drv;
	u32 lpddr3_odt;
	u32 lpddr2_drv;
	u32 phy_ddr3_clk_drv;
	u32 phy_ddr3_cmd_drv;
	u32 phy_ddr3_dqs_drv;
	u32 phy_ddr3_odt;
	u32 phy_lp23_clk_drv;
	u32 phy_lp23_cmd_drv;
	u32 phy_lp23_dqs_drv;
	u32 phy_lp3_odt;
};

static const char * const rk3288_dts_timing[] = {
	"ddr3_speed_bin",
	"pd_idle",
	"sr_idle",

	"auto_pd_dis_freq",
	"auto_sr_dis_freq",
	/* for ddr3 only */
	"ddr3_dll_dis_freq",
	"phy_dll_dis_freq",

	"ddr3_odt_dis_freq",
	"phy_ddr3_odt_dis_freq",
	"ddr3_drv",
	"ddr3_odt",
	"phy_ddr3_drv",
	"phy_ddr3_odt",

	"lpddr2_drv",
	"phy_lpddr2_drv",

	"lpddr3_odt_dis_freq",
	"phy_lpddr3_odt_dis_freq",
	"lpddr3_drv",
	"lpddr3_odt",
	"phy_lpddr3_drv",
	"phy_lpddr3_odt"
};

struct rk3288_ddr_dts_config_timing {
	unsigned int ddr3_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;

	unsigned int auto_pd_dis_freq;
	unsigned int auto_sr_dis_freq;
	/* for ddr3 only */
	unsigned int ddr3_dll_dis_freq;
	unsigned int phy_dll_dis_freq;

	unsigned int ddr3_odt_dis_freq;
	unsigned int phy_ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_drv;
	unsigned int phy_ddr3_odt;

	unsigned int lpddr2_drv;
	unsigned int phy_lpddr2_drv;

	unsigned int lpddr3_odt_dis_freq;
	unsigned int phy_lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_drv;
	unsigned int phy_lpddr3_odt;

	unsigned int available;
};

/* hope this define can adapt all future platfor */
static const char * const rk3328_dts_timing[] = {
	"ddr3_speed_bin",
	"ddr4_speed_bin",
	"pd_idle",
	"sr_idle",
	"sr_mc_gate_idle",
	"srpd_lite_idle",
	"standby_idle",

	"auto_pd_dis_freq",
	"auto_sr_dis_freq",
	"ddr3_dll_dis_freq",
	"ddr4_dll_dis_freq",
	"phy_dll_dis_freq",

	"ddr3_odt_dis_freq",
	"phy_ddr3_odt_dis_freq",
	"ddr3_drv",
	"ddr3_odt",
	"phy_ddr3_ca_drv",
	"phy_ddr3_ck_drv",
	"phy_ddr3_dq_drv",
	"phy_ddr3_odt",

	"lpddr3_odt_dis_freq",
	"phy_lpddr3_odt_dis_freq",
	"lpddr3_drv",
	"lpddr3_odt",
	"phy_lpddr3_ca_drv",
	"phy_lpddr3_ck_drv",
	"phy_lpddr3_dq_drv",
	"phy_lpddr3_odt",

	"lpddr4_odt_dis_freq",
	"phy_lpddr4_odt_dis_freq",
	"lpddr4_drv",
	"lpddr4_dq_odt",
	"lpddr4_ca_odt",
	"phy_lpddr4_ca_drv",
	"phy_lpddr4_ck_cs_drv",
	"phy_lpddr4_dq_drv",
	"phy_lpddr4_odt",

	"ddr4_odt_dis_freq",
	"phy_ddr4_odt_dis_freq",
	"ddr4_drv",
	"ddr4_odt",
	"phy_ddr4_ca_drv",
	"phy_ddr4_ck_drv",
	"phy_ddr4_dq_drv",
	"phy_ddr4_odt",
};

static const char * const rk3328_dts_ca_timing[] = {
	"ddr3a1_ddr4a9_de-skew",
	"ddr3a0_ddr4a10_de-skew",
	"ddr3a3_ddr4a6_de-skew",
	"ddr3a2_ddr4a4_de-skew",
	"ddr3a5_ddr4a8_de-skew",
	"ddr3a4_ddr4a5_de-skew",
	"ddr3a7_ddr4a11_de-skew",
	"ddr3a6_ddr4a7_de-skew",
	"ddr3a9_ddr4a0_de-skew",
	"ddr3a8_ddr4a13_de-skew",
	"ddr3a11_ddr4a3_de-skew",
	"ddr3a10_ddr4cs0_de-skew",
	"ddr3a13_ddr4a2_de-skew",
	"ddr3a12_ddr4ba1_de-skew",
	"ddr3a15_ddr4odt0_de-skew",
	"ddr3a14_ddr4a1_de-skew",
	"ddr3ba1_ddr4a15_de-skew",
	"ddr3ba0_ddr4bg0_de-skew",
	"ddr3ras_ddr4cke_de-skew",
	"ddr3ba2_ddr4ba0_de-skew",
	"ddr3we_ddr4bg1_de-skew",
	"ddr3cas_ddr4a12_de-skew",
	"ddr3ckn_ddr4ckn_de-skew",
	"ddr3ckp_ddr4ckp_de-skew",
	"ddr3cke_ddr4a16_de-skew",
	"ddr3odt0_ddr4a14_de-skew",
	"ddr3cs0_ddr4act_de-skew",
	"ddr3reset_ddr4reset_de-skew",
	"ddr3cs1_ddr4cs1_de-skew",
	"ddr3odt1_ddr4odt1_de-skew",
};

static const char * const rk3328_dts_cs0_timing[] = {
	"cs0_dm0_rx_de-skew",
	"cs0_dm0_tx_de-skew",
	"cs0_dq0_rx_de-skew",
	"cs0_dq0_tx_de-skew",
	"cs0_dq1_rx_de-skew",
	"cs0_dq1_tx_de-skew",
	"cs0_dq2_rx_de-skew",
	"cs0_dq2_tx_de-skew",
	"cs0_dq3_rx_de-skew",
	"cs0_dq3_tx_de-skew",
	"cs0_dq4_rx_de-skew",
	"cs0_dq4_tx_de-skew",
	"cs0_dq5_rx_de-skew",
	"cs0_dq5_tx_de-skew",
	"cs0_dq6_rx_de-skew",
	"cs0_dq6_tx_de-skew",
	"cs0_dq7_rx_de-skew",
	"cs0_dq7_tx_de-skew",
	"cs0_dqs0_rx_de-skew",
	"cs0_dqs0p_tx_de-skew",
	"cs0_dqs0n_tx_de-skew",

	"cs0_dm1_rx_de-skew",
	"cs0_dm1_tx_de-skew",
	"cs0_dq8_rx_de-skew",
	"cs0_dq8_tx_de-skew",
	"cs0_dq9_rx_de-skew",
	"cs0_dq9_tx_de-skew",
	"cs0_dq10_rx_de-skew",
	"cs0_dq10_tx_de-skew",
	"cs0_dq11_rx_de-skew",
	"cs0_dq11_tx_de-skew",
	"cs0_dq12_rx_de-skew",
	"cs0_dq12_tx_de-skew",
	"cs0_dq13_rx_de-skew",
	"cs0_dq13_tx_de-skew",
	"cs0_dq14_rx_de-skew",
	"cs0_dq14_tx_de-skew",
	"cs0_dq15_rx_de-skew",
	"cs0_dq15_tx_de-skew",
	"cs0_dqs1_rx_de-skew",
	"cs0_dqs1p_tx_de-skew",
	"cs0_dqs1n_tx_de-skew",

	"cs0_dm2_rx_de-skew",
	"cs0_dm2_tx_de-skew",
	"cs0_dq16_rx_de-skew",
	"cs0_dq16_tx_de-skew",
	"cs0_dq17_rx_de-skew",
	"cs0_dq17_tx_de-skew",
	"cs0_dq18_rx_de-skew",
	"cs0_dq18_tx_de-skew",
	"cs0_dq19_rx_de-skew",
	"cs0_dq19_tx_de-skew",
	"cs0_dq20_rx_de-skew",
	"cs0_dq20_tx_de-skew",
	"cs0_dq21_rx_de-skew",
	"cs0_dq21_tx_de-skew",
	"cs0_dq22_rx_de-skew",
	"cs0_dq22_tx_de-skew",
	"cs0_dq23_rx_de-skew",
	"cs0_dq23_tx_de-skew",
	"cs0_dqs2_rx_de-skew",
	"cs0_dqs2p_tx_de-skew",
	"cs0_dqs2n_tx_de-skew",

	"cs0_dm3_rx_de-skew",
	"cs0_dm3_tx_de-skew",
	"cs0_dq24_rx_de-skew",
	"cs0_dq24_tx_de-skew",
	"cs0_dq25_rx_de-skew",
	"cs0_dq25_tx_de-skew",
	"cs0_dq26_rx_de-skew",
	"cs0_dq26_tx_de-skew",
	"cs0_dq27_rx_de-skew",
	"cs0_dq27_tx_de-skew",
	"cs0_dq28_rx_de-skew",
	"cs0_dq28_tx_de-skew",
	"cs0_dq29_rx_de-skew",
	"cs0_dq29_tx_de-skew",
	"cs0_dq30_rx_de-skew",
	"cs0_dq30_tx_de-skew",
	"cs0_dq31_rx_de-skew",
	"cs0_dq31_tx_de-skew",
	"cs0_dqs3_rx_de-skew",
	"cs0_dqs3p_tx_de-skew",
	"cs0_dqs3n_tx_de-skew",
};

static const char * const rk3328_dts_cs1_timing[] = {
	"cs1_dm0_rx_de-skew",
	"cs1_dm0_tx_de-skew",
	"cs1_dq0_rx_de-skew",
	"cs1_dq0_tx_de-skew",
	"cs1_dq1_rx_de-skew",
	"cs1_dq1_tx_de-skew",
	"cs1_dq2_rx_de-skew",
	"cs1_dq2_tx_de-skew",
	"cs1_dq3_rx_de-skew",
	"cs1_dq3_tx_de-skew",
	"cs1_dq4_rx_de-skew",
	"cs1_dq4_tx_de-skew",
	"cs1_dq5_rx_de-skew",
	"cs1_dq5_tx_de-skew",
	"cs1_dq6_rx_de-skew",
	"cs1_dq6_tx_de-skew",
	"cs1_dq7_rx_de-skew",
	"cs1_dq7_tx_de-skew",
	"cs1_dqs0_rx_de-skew",
	"cs1_dqs0p_tx_de-skew",
	"cs1_dqs0n_tx_de-skew",

	"cs1_dm1_rx_de-skew",
	"cs1_dm1_tx_de-skew",
	"cs1_dq8_rx_de-skew",
	"cs1_dq8_tx_de-skew",
	"cs1_dq9_rx_de-skew",
	"cs1_dq9_tx_de-skew",
	"cs1_dq10_rx_de-skew",
	"cs1_dq10_tx_de-skew",
	"cs1_dq11_rx_de-skew",
	"cs1_dq11_tx_de-skew",
	"cs1_dq12_rx_de-skew",
	"cs1_dq12_tx_de-skew",
	"cs1_dq13_rx_de-skew",
	"cs1_dq13_tx_de-skew",
	"cs1_dq14_rx_de-skew",
	"cs1_dq14_tx_de-skew",
	"cs1_dq15_rx_de-skew",
	"cs1_dq15_tx_de-skew",
	"cs1_dqs1_rx_de-skew",
	"cs1_dqs1p_tx_de-skew",
	"cs1_dqs1n_tx_de-skew",

	"cs1_dm2_rx_de-skew",
	"cs1_dm2_tx_de-skew",
	"cs1_dq16_rx_de-skew",
	"cs1_dq16_tx_de-skew",
	"cs1_dq17_rx_de-skew",
	"cs1_dq17_tx_de-skew",
	"cs1_dq18_rx_de-skew",
	"cs1_dq18_tx_de-skew",
	"cs1_dq19_rx_de-skew",
	"cs1_dq19_tx_de-skew",
	"cs1_dq20_rx_de-skew",
	"cs1_dq20_tx_de-skew",
	"cs1_dq21_rx_de-skew",
	"cs1_dq21_tx_de-skew",
	"cs1_dq22_rx_de-skew",
	"cs1_dq22_tx_de-skew",
	"cs1_dq23_rx_de-skew",
	"cs1_dq23_tx_de-skew",
	"cs1_dqs2_rx_de-skew",
	"cs1_dqs2p_tx_de-skew",
	"cs1_dqs2n_tx_de-skew",

	"cs1_dm3_rx_de-skew",
	"cs1_dm3_tx_de-skew",
	"cs1_dq24_rx_de-skew",
	"cs1_dq24_tx_de-skew",
	"cs1_dq25_rx_de-skew",
	"cs1_dq25_tx_de-skew",
	"cs1_dq26_rx_de-skew",
	"cs1_dq26_tx_de-skew",
	"cs1_dq27_rx_de-skew",
	"cs1_dq27_tx_de-skew",
	"cs1_dq28_rx_de-skew",
	"cs1_dq28_tx_de-skew",
	"cs1_dq29_rx_de-skew",
	"cs1_dq29_tx_de-skew",
	"cs1_dq30_rx_de-skew",
	"cs1_dq30_tx_de-skew",
	"cs1_dq31_rx_de-skew",
	"cs1_dq31_tx_de-skew",
	"cs1_dqs3_rx_de-skew",
	"cs1_dqs3p_tx_de-skew",
	"cs1_dqs3n_tx_de-skew",
};

struct rk3328_ddr_dts_config_timing {
	unsigned int ddr3_speed_bin;
	unsigned int ddr4_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;

	unsigned int auto_pd_dis_freq;
	unsigned int auto_sr_dis_freq;
	/* for ddr3 only */
	unsigned int ddr3_dll_dis_freq;
	/* for ddr4 only */
	unsigned int ddr4_dll_dis_freq;
	unsigned int phy_dll_dis_freq;

	unsigned int ddr3_odt_dis_freq;
	unsigned int phy_ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_ca_drv;
	unsigned int phy_ddr3_ck_drv;
	unsigned int phy_ddr3_dq_drv;
	unsigned int phy_ddr3_odt;

	unsigned int lpddr3_odt_dis_freq;
	unsigned int phy_lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_ca_drv;
	unsigned int phy_lpddr3_ck_drv;
	unsigned int phy_lpddr3_dq_drv;
	unsigned int phy_lpddr3_odt;

	unsigned int lpddr4_odt_dis_freq;
	unsigned int phy_lpddr4_odt_dis_freq;
	unsigned int lpddr4_drv;
	unsigned int lpddr4_dq_odt;
	unsigned int lpddr4_ca_odt;
	unsigned int phy_lpddr4_ca_drv;
	unsigned int phy_lpddr4_ck_cs_drv;
	unsigned int phy_lpddr4_dq_drv;
	unsigned int phy_lpddr4_odt;

	unsigned int ddr4_odt_dis_freq;
	unsigned int phy_ddr4_odt_dis_freq;
	unsigned int ddr4_drv;
	unsigned int ddr4_odt;
	unsigned int phy_ddr4_ca_drv;
	unsigned int phy_ddr4_ck_drv;
	unsigned int phy_ddr4_dq_drv;
	unsigned int phy_ddr4_odt;

	unsigned int ca_skew[15];
	unsigned int cs0_skew[44];
	unsigned int cs1_skew[44];

	unsigned int available;
};

struct rk3328_ddr_de_skew_setting {
	unsigned int ca_de_skew[30];
	unsigned int cs0_de_skew[84];
	unsigned int cs1_de_skew[84];
};

struct rk3368_dram_timing {
	u32 dram_spd_bin;
	u32 sr_idle;
	u32 pd_idle;
	u32 dram_dll_dis_freq;
	u32 phy_dll_dis_freq;
	u32 dram_odt_dis_freq;
	u32 phy_odt_dis_freq;
	u32 ddr3_drv;
	u32 ddr3_odt;
	u32 lpddr3_drv;
	u32 lpddr3_odt;
	u32 lpddr2_drv;
	u32 phy_clk_drv;
	u32 phy_cmd_drv;
	u32 phy_dqs_drv;
	u32 phy_odt;
	u32 ddr_2t;
};

struct rk3399_dram_timing {
	unsigned int ddr3_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;
	unsigned int auto_lp_dis_freq;
	unsigned int ddr3_dll_dis_freq;
	unsigned int phy_dll_dis_freq;
	unsigned int ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_ca_drv;
	unsigned int phy_ddr3_dq_drv;
	unsigned int phy_ddr3_odt;
	unsigned int lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_ca_drv;
	unsigned int phy_lpddr3_dq_drv;
	unsigned int phy_lpddr3_odt;
	unsigned int lpddr4_odt_dis_freq;
	unsigned int lpddr4_drv;
	unsigned int lpddr4_dq_odt;
	unsigned int lpddr4_ca_odt;
	unsigned int phy_lpddr4_ca_drv;
	unsigned int phy_lpddr4_ck_cs_drv;
	unsigned int phy_lpddr4_dq_drv;
	unsigned int phy_lpddr4_odt;
};

/* name rule: ddr4(pad_name)_ddr3_lpddr3_lpddr4_de-skew */
static const char * const rv1126_dts_ca_timing[] = {
	"a0_a3_a3_cke1-a_de-skew",
	"a1_ba1_null_cke0-b_de-skew",
	"a2_a9_a9_a4-a_de-skew",
	"a3_a15_null_a5-b_de-skew",
	"a4_a6_a6_ck-a_de-skew",
	"a5_a12_null_odt0-b_de-skew",
	"a6_ba2_null_a0-a_de-skew",
	"a7_a4_a4_odt0-a_de-skew",
	"a8_a1_a1_cke0-a_de-skew",
	"a9_a5_a5_a5-a_de-skew",
	"a10_a8_a8_clkb-a_de-skew",
	"a11_a7_a7_ca2-a_de-skew",
	"a12_rasn_null_ca1-a_de-skew",
	"a13_a13_null_ca3-a_de-skew",
	"a14_a14_null_csb1-b_de-skew",
	"a15_a10_null_ca0-b_de-skew",
	"a16_a11_null_csb0-b_de-skew",
	"a17_null_null_null_de-skew",
	"ba0_csb1_csb1_csb0-a_de-skew",
	"ba1_wen_null_cke1-b_de-skew",
	"bg0_odt1_odt1_csb1-a_de-skew",
	"bg1_a2_a2_odt1-a_de-skew",
	"cke0_casb_null_ca1-b_de-skew",
	"ck_ck_ck_ck-b_de-skew",
	"ckb_ckb_ckb_ckb-b_de-skew",
	"csb0_odt0_odt0_ca2-b_de-skew",
	"odt0_csb0_csb0_ca4-b_de-skew",
	"resetn_resetn_null-resetn_de-skew",
	"actn_cke_cke_ca3-b_de-skew",
	"cke1_null_null_null_de-skew",
	"csb1_ba0_null_null_de-skew",
	"odt1_a0_a0_odt1-b_de-skew",
};

static const char * const rv1126_dts_cs0_a_timing[] = {
	"cs0_dm0_rx_de-skew",
	"cs0_dq0_rx_de-skew",
	"cs0_dq1_rx_de-skew",
	"cs0_dq2_rx_de-skew",
	"cs0_dq3_rx_de-skew",
	"cs0_dq4_rx_de-skew",
	"cs0_dq5_rx_de-skew",
	"cs0_dq6_rx_de-skew",
	"cs0_dq7_rx_de-skew",
	"cs0_dqs0p_rx_de-skew",
	"cs0_dqs0n_rx_de-skew",
	"cs0_dm1_rx_de-skew",
	"cs0_dq8_rx_de-skew",
	"cs0_dq9_rx_de-skew",
	"cs0_dq10_rx_de-skew",
	"cs0_dq11_rx_de-skew",
	"cs0_dq12_rx_de-skew",
	"cs0_dq13_rx_de-skew",
	"cs0_dq14_rx_de-skew",
	"cs0_dq15_rx_de-skew",
	"cs0_dqs1p_rx_de-skew",
	"cs0_dqs1n_rx_de-skew",
	"cs0_dm0_tx_de-skew",
	"cs0_dq0_tx_de-skew",
	"cs0_dq1_tx_de-skew",
	"cs0_dq2_tx_de-skew",
	"cs0_dq3_tx_de-skew",
	"cs0_dq4_tx_de-skew",
	"cs0_dq5_tx_de-skew",
	"cs0_dq6_tx_de-skew",
	"cs0_dq7_tx_de-skew",
	"cs0_dqs0p_tx_de-skew",
	"cs0_dqs0n_tx_de-skew",
	"cs0_dm1_tx_de-skew",
	"cs0_dq8_tx_de-skew",
	"cs0_dq9_tx_de-skew",
	"cs0_dq10_tx_de-skew",
	"cs0_dq11_tx_de-skew",
	"cs0_dq12_tx_de-skew",
	"cs0_dq13_tx_de-skew",
	"cs0_dq14_tx_de-skew",
	"cs0_dq15_tx_de-skew",
	"cs0_dqs1p_tx_de-skew",
	"cs0_dqs1n_tx_de-skew",
};

static const char * const rv1126_dts_cs0_b_timing[] = {
	"cs0_dm2_rx_de-skew",
	"cs0_dq16_rx_de-skew",
	"cs0_dq17_rx_de-skew",
	"cs0_dq18_rx_de-skew",
	"cs0_dq19_rx_de-skew",
	"cs0_dq20_rx_de-skew",
	"cs0_dq21_rx_de-skew",
	"cs0_dq22_rx_de-skew",
	"cs0_dq23_rx_de-skew",
	"cs0_dqs2p_rx_de-skew",
	"cs0_dqs2n_rx_de-skew",
	"cs0_dm3_rx_de-skew",
	"cs0_dq24_rx_de-skew",
	"cs0_dq25_rx_de-skew",
	"cs0_dq26_rx_de-skew",
	"cs0_dq27_rx_de-skew",
	"cs0_dq28_rx_de-skew",
	"cs0_dq29_rx_de-skew",
	"cs0_dq30_rx_de-skew",
	"cs0_dq31_rx_de-skew",
	"cs0_dqs3p_rx_de-skew",
	"cs0_dqs3n_rx_de-skew",
	"cs0_dm2_tx_de-skew",
	"cs0_dq16_tx_de-skew",
	"cs0_dq17_tx_de-skew",
	"cs0_dq18_tx_de-skew",
	"cs0_dq19_tx_de-skew",
	"cs0_dq20_tx_de-skew",
	"cs0_dq21_tx_de-skew",
	"cs0_dq22_tx_de-skew",
	"cs0_dq23_tx_de-skew",
	"cs0_dqs2p_tx_de-skew",
	"cs0_dqs2n_tx_de-skew",
	"cs0_dm3_tx_de-skew",
	"cs0_dq24_tx_de-skew",
	"cs0_dq25_tx_de-skew",
	"cs0_dq26_tx_de-skew",
	"cs0_dq27_tx_de-skew",
	"cs0_dq28_tx_de-skew",
	"cs0_dq29_tx_de-skew",
	"cs0_dq30_tx_de-skew",
	"cs0_dq31_tx_de-skew",
	"cs0_dqs3p_tx_de-skew",
	"cs0_dqs3n_tx_de-skew",
};

static const char * const rv1126_dts_cs1_a_timing[] = {
	"cs1_dm0_rx_de-skew",
	"cs1_dq0_rx_de-skew",
	"cs1_dq1_rx_de-skew",
	"cs1_dq2_rx_de-skew",
	"cs1_dq3_rx_de-skew",
	"cs1_dq4_rx_de-skew",
	"cs1_dq5_rx_de-skew",
	"cs1_dq6_rx_de-skew",
	"cs1_dq7_rx_de-skew",
	"cs1_dqs0p_rx_de-skew",
	"cs1_dqs0n_rx_de-skew",
	"cs1_dm1_rx_de-skew",
	"cs1_dq8_rx_de-skew",
	"cs1_dq9_rx_de-skew",
	"cs1_dq10_rx_de-skew",
	"cs1_dq11_rx_de-skew",
	"cs1_dq12_rx_de-skew",
	"cs1_dq13_rx_de-skew",
	"cs1_dq14_rx_de-skew",
	"cs1_dq15_rx_de-skew",
	"cs1_dqs1p_rx_de-skew",
	"cs1_dqs1n_rx_de-skew",
	"cs1_dm0_tx_de-skew",
	"cs1_dq0_tx_de-skew",
	"cs1_dq1_tx_de-skew",
	"cs1_dq2_tx_de-skew",
	"cs1_dq3_tx_de-skew",
	"cs1_dq4_tx_de-skew",
	"cs1_dq5_tx_de-skew",
	"cs1_dq6_tx_de-skew",
	"cs1_dq7_tx_de-skew",
	"cs1_dqs0p_tx_de-skew",
	"cs1_dqs0n_tx_de-skew",
	"cs1_dm1_tx_de-skew",
	"cs1_dq8_tx_de-skew",
	"cs1_dq9_tx_de-skew",
	"cs1_dq10_tx_de-skew",
	"cs1_dq11_tx_de-skew",
	"cs1_dq12_tx_de-skew",
	"cs1_dq13_tx_de-skew",
	"cs1_dq14_tx_de-skew",
	"cs1_dq15_tx_de-skew",
	"cs1_dqs1p_tx_de-skew",
	"cs1_dqs1n_tx_de-skew",
};

static const char * const rv1126_dts_cs1_b_timing[] = {
	"cs1_dm2_rx_de-skew",
	"cs1_dq16_rx_de-skew",
	"cs1_dq17_rx_de-skew",
	"cs1_dq18_rx_de-skew",
	"cs1_dq19_rx_de-skew",
	"cs1_dq20_rx_de-skew",
	"cs1_dq21_rx_de-skew",
	"cs1_dq22_rx_de-skew",
	"cs1_dq23_rx_de-skew",
	"cs1_dqs2p_rx_de-skew",
	"cs1_dqs2n_rx_de-skew",
	"cs1_dm3_rx_de-skew",
	"cs1_dq24_rx_de-skew",
	"cs1_dq25_rx_de-skew",
	"cs1_dq26_rx_de-skew",
	"cs1_dq27_rx_de-skew",
	"cs1_dq28_rx_de-skew",
	"cs1_dq29_rx_de-skew",
	"cs1_dq30_rx_de-skew",
	"cs1_dq31_rx_de-skew",
	"cs1_dqs3p_rx_de-skew",
	"cs1_dqs3n_rx_de-skew",
	"cs1_dm2_tx_de-skew",
	"cs1_dq16_tx_de-skew",
	"cs1_dq17_tx_de-skew",
	"cs1_dq18_tx_de-skew",
	"cs1_dq19_tx_de-skew",
	"cs1_dq20_tx_de-skew",
	"cs1_dq21_tx_de-skew",
	"cs1_dq22_tx_de-skew",
	"cs1_dq23_tx_de-skew",
	"cs1_dqs2p_tx_de-skew",
	"cs1_dqs2n_tx_de-skew",
	"cs1_dm3_tx_de-skew",
	"cs1_dq24_tx_de-skew",
	"cs1_dq25_tx_de-skew",
	"cs1_dq26_tx_de-skew",
	"cs1_dq27_tx_de-skew",
	"cs1_dq28_tx_de-skew",
	"cs1_dq29_tx_de-skew",
	"cs1_dq30_tx_de-skew",
	"cs1_dq31_tx_de-skew",
	"cs1_dqs3p_tx_de-skew",
	"cs1_dqs3n_tx_de-skew",
};

#endif /* __ROCKCHIP_DMC_TIMING_H__ */

