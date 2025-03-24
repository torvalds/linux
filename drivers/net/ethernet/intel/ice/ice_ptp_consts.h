/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_PTP_CONSTS_H_
#define _ICE_PTP_CONSTS_H_

/* Constant definitions related to the hardware clock used for PTP 1588
 * features and functionality.
 */
/* Constants defined for the PTP 1588 clock hardware. */

const struct ice_phy_reg_info_eth56g eth56g_phy_res[NUM_ETH56G_PHY_RES] = {
	/* ETH56G_PHY_REG_PTP */
	{
		/* base_addr */
		{
			0x092000,
			0x126000,
			0x1BA000,
			0x24E000,
			0x2E2000,
		},
		/* step */
		0x98,
	},
	/* ETH56G_PHY_MEM_PTP */
	{
		/* base_addr */
		{
			0x093000,
			0x127000,
			0x1BB000,
			0x24F000,
			0x2E3000,
		},
		/* step */
		0x200,
	},
	/* ETH56G_PHY_REG_XPCS */
	{
		/* base_addr */
		{
			0x000000,
			0x009400,
			0x128000,
			0x1BC000,
			0x250000,
		},
		/* step */
		0x21000,
	},
	/* ETH56G_PHY_REG_MAC */
	{
		/* base_addr */
		{
			0x085000,
			0x119000,
			0x1AD000,
			0x241000,
			0x2D5000,
		},
		/* step */
		0x1000,
	},
	/* ETH56G_PHY_REG_GPCS */
	{
		/* base_addr */
		{
			0x084000,
			0x118000,
			0x1AC000,
			0x240000,
			0x2D4000,
		},
		/* step */
		0x400,
	},
};

const
struct ice_eth56g_mac_reg_cfg eth56g_mac_cfg[NUM_ICE_ETH56G_LNK_SPD] = {
	[ICE_ETH56G_LNK_SPD_1G] = {
		.tx_mode = { .def = 6, },
		.rx_mode = { .def = 6, },
		.blks_per_clk = 1,
		.blktime = 0x4000, /* 32 */
		.tx_offset = {
			.serdes = 0x6666, /* 51.2 */
			.no_fec = 0xd066, /* 104.2 */
			.sfd = 0x3000, /* 24 */
			.onestep = 0x30000 /* 384 */
		},
		.rx_offset = {
			.serdes = 0xffffc59a, /* -29.2 */
			.no_fec = 0xffff0a80, /* -122.75 */
			.sfd = 0x2c00, /* 22 */
			.bs_ds = 0x19a /* 0.8 */
			/* Dynamic bitslip 0 equals to 10 */
		}
	},
	[ICE_ETH56G_LNK_SPD_2_5G] = {
		.tx_mode = { .def = 6, },
		.rx_mode = { .def = 6, },
		.blks_per_clk = 1,
		.blktime = 0x199a, /* 12.8 */
		.tx_offset = {
			.serdes = 0x28f6, /* 20.48 */
			.no_fec = 0x53b8, /* 41.86 */
			.sfd = 0x1333, /* 9.6 */
			.onestep = 0x13333 /* 153.6 */
		},
		.rx_offset = {
			.serdes = 0xffffe8a4, /* -11.68 */
			.no_fec = 0xffff9a76, /* -50.77 */
			.sfd = 0xf33, /* 7.6 */
			.bs_ds = 0xa4 /* 0.32 */
		}
	},
	[ICE_ETH56G_LNK_SPD_10G] = {
		.tx_mode = { .def = 1, },
		.rx_mode = { .def = 1, },
		.blks_per_clk = 1,
		.blktime = 0x666, /* 3.2 */
		.tx_offset = {
			.serdes = 0x234c, /* 17.6484848 */
			.no_fec = 0x8e80, /* 71.25 */
			.fc = 0xb4a4, /* 90.32 */
			.sfd = 0x4a4, /* 2.32 */
			.onestep = 0x4ccd /* 38.4 */
		},
		.rx_offset = {
			.serdes = 0xffffeb27, /* -10.42424 */
			.no_fec = 0xffffcccd, /* -25.6 */
			.fc = 0xfffc557b, /* -469.26 */
			.sfd = 0x4a4, /* 2.32 */
			.bs_ds = 0x32 /* 0.0969697 */
		}
	},
	[ICE_ETH56G_LNK_SPD_25G] = {
		.tx_mode = {
			.def = 1,
			.rs = 4
		},
		.tx_mk_dly = 4,
		.tx_cw_dly = {
			.def = 1,
			.onestep = 6
		},
		.rx_mode = {
			.def = 1,
			.rs = 4
		},
		.rx_mk_dly = {
			.def = 1,
			.rs = 1
		},
		.rx_cw_dly = {
			.def = 1,
			.rs = 1
		},
		.blks_per_clk = 1,
		.blktime = 0x28f, /* 1.28 */
		.mktime = 0x147b, /* 10.24, only if RS-FEC enabled */
		.tx_offset = {
			.serdes = 0xe1e, /* 7.0593939 */
			.no_fec = 0x3857, /* 28.17 */
			.fc = 0x48c3, /* 36.38 */
			.rs = 0x8100, /* 64.5 */
			.sfd = 0x1dc, /* 0.93 */
			.onestep = 0x1eb8 /* 15.36 */
		},
		.rx_offset = {
			.serdes = 0xfffff7a9, /* -4.1697 */
			.no_fec = 0xffffe71a, /* -12.45 */
			.fc = 0xfffe894d, /* -187.35 */
			.rs = 0xfffff8cd, /* -3.6 */
			.sfd = 0x1dc, /* 0.93 */
			.bs_ds = 0x14 /* 0.0387879, RS-FEC 0 */
		}
	},
	[ICE_ETH56G_LNK_SPD_40G] = {
		.tx_mode = { .def = 3 },
		.tx_mk_dly = 4,
		.tx_cw_dly = {
			.def = 1,
			.onestep = 6
		},
		.rx_mode = { .def = 4 },
		.rx_mk_dly = { .def = 1 },
		.rx_cw_dly = { .def = 1 },
		.blktime = 0x333, /* 1.6 */
		.mktime = 0xccd, /* 6.4 */
		.tx_offset = {
			.serdes = 0x234c, /* 17.6484848 */
			.no_fec = 0x5a8a, /* 45.27 */
			.fc = 0x81b8, /* 64.86 */
			.sfd = 0x4a4, /* 2.32 */
			.onestep = 0x1333 /* 9.6 */
		},
		.rx_offset = {
			.serdes = 0xffffeb27, /* -10.42424 */
			.no_fec = 0xfffff594, /* -5.21 */
			.fc = 0xfffe3080, /* -231.75 */
			.sfd = 0x4a4, /* 2.32 */
			.bs_ds = 0xccd /* 6.4 */
		}
	},
	[ICE_ETH56G_LNK_SPD_50G] = {
		.tx_mode = { .def = 5 },
		.tx_mk_dly = 4,
		.tx_cw_dly = {
			.def = 1,
			.onestep = 6
		},
		.rx_mode = { .def = 5 },
		.rx_mk_dly = { .def = 1 },
		.rx_cw_dly = { .def = 1 },
		.blktime = 0x28f, /* 1.28 */
		.mktime = 0xa3d, /* 5.12 */
		.tx_offset = {
			.serdes = 0x13ba, /* 9.86353 */
			.rs = 0x5400, /* 42 */
			.sfd = 0xe6, /* 0.45 */
			.onestep = 0xf5c /* 7.68 */
		},
		.rx_offset = {
			.serdes = 0xfffff7e8, /* -4.04706 */
			.rs = 0xfffff994, /* -3.21 */
			.sfd = 0xe6 /* 0.45 */
		}
	},
	[ICE_ETH56G_LNK_SPD_50G2] = {
		.tx_mode = {
			.def = 3,
			.rs = 2
		},
		.tx_mk_dly = 4,
		.tx_cw_dly = {
			.def = 1,
			.onestep = 6
		},
		.rx_mode = {
			.def = 4,
			.rs = 1
		},
		.rx_mk_dly = { .def = 1 },
		.rx_cw_dly = { .def = 1 },
		.blktime = 0x28f, /* 1.28 */
		.mktime = 0xa3d, /* 5.12 */
		.tx_offset = {
			.serdes = 0xe1e, /* 7.0593939 */
			.no_fec = 0x3d33, /* 30.6 */
			.rs = 0x5057, /* 40.17 */
			.sfd = 0x1dc, /* 0.93 */
			.onestep = 0xf5c /* 7.68 */
		},
		.rx_offset = {
			.serdes = 0xfffff7a9, /* -4.1697 */
			.no_fec = 0xfffff8cd, /* -3.6 */
			.rs = 0xfffff21a, /* -6.95 */
			.sfd = 0x1dc, /* 0.93 */
			.bs_ds = 0xa3d /* 5.12, RS-FEC 0x633 (3.1) */
		}
	},
	[ICE_ETH56G_LNK_SPD_100G] = {
		.tx_mode = {
			.def = 3,
			.rs = 2
		},
		.tx_mk_dly = 10,
		.tx_cw_dly = {
			.def = 3,
			.onestep = 6
		},
		.rx_mode = {
			.def = 4,
			.rs = 1
		},
		.rx_mk_dly = { .def = 5 },
		.rx_cw_dly = { .def = 5 },
		.blks_per_clk = 1,
		.blktime = 0x148, /* 0.64 */
		.mktime = 0x199a, /* 12.8 */
		.tx_offset = {
			.serdes = 0xe1e, /* 7.0593939 */
			.no_fec = 0x67ec, /* 51.96 */
			.rs = 0x44fb, /* 34.49 */
			.sfd = 0x1dc, /* 0.93 */
			.onestep = 0xf5c /* 7.68 */
		},
		.rx_offset = {
			.serdes = 0xfffff7a9, /* -4.1697 */
			.no_fec = 0xfffff5a9, /* -5.17 */
			.rs = 0xfffff6e6, /* -4.55 */
			.sfd = 0x1dc, /* 0.93 */
			.bs_ds = 0x199a /* 12.8, RS-FEC 0x31b (1.552) */
		}
	},
	[ICE_ETH56G_LNK_SPD_100G2] = {
		.tx_mode = { .def = 5 },
		.tx_mk_dly = 10,
		.tx_cw_dly = {
			.def = 3,
			.onestep = 6
		},
		.rx_mode = { .def = 5 },
		.rx_mk_dly = { .def = 5 },
		.rx_cw_dly = { .def = 5 },
		.blks_per_clk = 1,
		.blktime = 0x148, /* 0.64 */
		.mktime = 0x199a, /* 12.8 */
		.tx_offset = {
			.serdes = 0x13ba, /* 9.86353 */
			.rs = 0x460a, /* 35.02 */
			.sfd = 0xe6, /* 0.45 */
			.onestep = 0xf5c /* 7.68 */
		},
		.rx_offset = {
			.serdes = 0xfffff7e8, /* -4.04706 */
			.rs = 0xfffff548, /* -5.36 */
			.sfd = 0xe6, /* 0.45 */
			.bs_ds = 0x303 /* 1.506 */
		}
	}
};

/* struct ice_time_ref_info_e82x
 *
 * E822 hardware can use different sources as the reference for the PTP
 * hardware clock. Each clock has different characteristics such as a slightly
 * different frequency, etc.
 *
 * This lookup table defines several constants that depend on the current time
 * reference. See the struct ice_time_ref_info_e82x for information about the
 * meaning of each constant.
 */
const struct ice_time_ref_info_e82x e82x_time_ref[NUM_ICE_TIME_REF_FREQ] = {
	/* ICE_TIME_REF_FREQ_25_000 -> 25 MHz */
	{
		/* pll_freq */
		823437500, /* 823.4375 MHz PLL */
		/* nominal_incval */
		0x136e44fabULL,
	},

	/* ICE_TIME_REF_FREQ_122_880 -> 122.88 MHz */
	{
		/* pll_freq */
		783360000, /* 783.36 MHz */
		/* nominal_incval */
		0x146cc2177ULL,
	},

	/* ICE_TIME_REF_FREQ_125_000 -> 125 MHz */
	{
		/* pll_freq */
		796875000, /* 796.875 MHz */
		/* nominal_incval */
		0x141414141ULL,
	},

	/* ICE_TIME_REF_FREQ_153_600 -> 153.6 MHz */
	{
		/* pll_freq */
		816000000, /* 816 MHz */
		/* nominal_incval */
		0x139b9b9baULL,
	},

	/* ICE_TIME_REF_FREQ_156_250 -> 156.25 MHz */
	{
		/* pll_freq */
		830078125, /* 830.78125 MHz */
		/* nominal_incval */
		0x134679aceULL,
	},

	/* ICE_TIME_REF_FREQ_245_760 -> 245.76 MHz */
	{
		/* pll_freq */
		783360000, /* 783.36 MHz */
		/* nominal_incval */
		0x146cc2177ULL,
	},
};

const struct ice_cgu_pll_params_e82x e822_cgu_params[NUM_ICE_TIME_REF_FREQ] = {
	/* ICE_TIME_REF_FREQ_25_000 -> 25 MHz */
	{
		/* refclk_pre_div */
		1,
		/* feedback_div */
		197,
		/* frac_n_div */
		2621440,
		/* post_pll_div */
		6,
	},

	/* ICE_TIME_REF_FREQ_122_880 -> 122.88 MHz */
	{
		/* refclk_pre_div */
		5,
		/* feedback_div */
		223,
		/* frac_n_div */
		524288,
		/* post_pll_div */
		7,
	},

	/* ICE_TIME_REF_FREQ_125_000 -> 125 MHz */
	{
		/* refclk_pre_div */
		5,
		/* feedback_div */
		223,
		/* frac_n_div */
		524288,
		/* post_pll_div */
		7,
	},

	/* ICE_TIME_REF_FREQ_153_600 -> 153.6 MHz */
	{
		/* refclk_pre_div */
		5,
		/* feedback_div */
		159,
		/* frac_n_div */
		1572864,
		/* post_pll_div */
		6,
	},

	/* ICE_TIME_REF_FREQ_156_250 -> 156.25 MHz */
	{
		/* refclk_pre_div */
		5,
		/* feedback_div */
		159,
		/* frac_n_div */
		1572864,
		/* post_pll_div */
		6,
	},

	/* ICE_TIME_REF_FREQ_245_760 -> 245.76 MHz */
	{
		/* refclk_pre_div */
		10,
		/* feedback_div */
		223,
		/* frac_n_div */
		524288,
		/* post_pll_div */
		7,
	},
};

const
struct ice_cgu_pll_params_e825c e825c_cgu_params[NUM_ICE_TIME_REF_FREQ] = {
	/* ICE_TIME_REF_FREQ_25_000 -> 25 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x19,
		/* tspll_ndivratio */
		1,
		/* tspll_fbdiv_intgr */
		320,
		/* tspll_fbdiv_frac */
		0,
		/* ref1588_ck_div */
		0,
	},

	/* ICE_TIME_REF_FREQ_122_880 -> 122.88 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x29,
		/* tspll_ndivratio */
		3,
		/* tspll_fbdiv_intgr */
		195,
		/* tspll_fbdiv_frac */
		1342177280UL,
		/* ref1588_ck_div */
		0,
	},

	/* ICE_TIME_REF_FREQ_125_000 -> 125 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x3E,
		/* tspll_ndivratio */
		2,
		/* tspll_fbdiv_intgr */
		128,
		/* tspll_fbdiv_frac */
		0,
		/* ref1588_ck_div */
		0,
	},

	/* ICE_TIME_REF_FREQ_153_600 -> 153.6 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x33,
		/* tspll_ndivratio */
		3,
		/* tspll_fbdiv_intgr */
		156,
		/* tspll_fbdiv_frac */
		1073741824UL,
		/* ref1588_ck_div */
		0,
	},

	/* ICE_TIME_REF_FREQ_156_250 -> 156.25 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x1F,
		/* tspll_ndivratio */
		5,
		/* tspll_fbdiv_intgr */
		256,
		/* tspll_fbdiv_frac */
		0,
		/* ref1588_ck_div */
		0,
	},

	/* ICE_TIME_REF_FREQ_245_760 -> 245.76 MHz */
	{
		/* tspll_ck_refclkfreq */
		0x52,
		/* tspll_ndivratio */
		3,
		/* tspll_fbdiv_intgr */
		97,
		/* tspll_fbdiv_frac */
		2818572288UL,
		/* ref1588_ck_div */
		0,
	},
};

/* struct ice_vernier_info_e82x
 *
 * E822 hardware calibrates the delay of the timestamp indication from the
 * actual packet transmission or reception during the initialization of the
 * PHY. To do this, the hardware mechanism uses some conversions between the
 * various clocks within the PHY block. This table defines constants used to
 * calculate the correct conversion ratios in the PHY registers.
 *
 * Many of the values relate to the PAR/PCS clock conversion registers. For
 * these registers, a value of 0 means that the associated register is not
 * used by this link speed, and that the register should be cleared by writing
 * 0. Other values specify the clock frequency in Hz.
 */
const struct ice_vernier_info_e82x e822_vernier[NUM_ICE_PTP_LNK_SPD] = {
	/* ICE_PTP_LNK_SPD_1G */
	{
		/* tx_par_clk */
		31250000, /* 31.25 MHz */
		/* rx_par_clk */
		31250000, /* 31.25 MHz */
		/* tx_pcs_clk */
		125000000, /* 125 MHz */
		/* rx_pcs_clk */
		125000000, /* 125 MHz */
		/* tx_desk_rsgb_par */
		0, /* unused */
		/* rx_desk_rsgb_par */
		0, /* unused */
		/* tx_desk_rsgb_pcs */
		0, /* unused */
		/* rx_desk_rsgb_pcs */
		0, /* unused */
		/* tx_fixed_delay */
		25140,
		/* pmd_adj_divisor */
		10000000,
		/* rx_fixed_delay */
		17372,
	},
	/* ICE_PTP_LNK_SPD_10G */
	{
		/* tx_par_clk */
		257812500, /* 257.8125 MHz */
		/* rx_par_clk */
		257812500, /* 257.8125 MHz */
		/* tx_pcs_clk */
		156250000, /* 156.25 MHz */
		/* rx_pcs_clk */
		156250000, /* 156.25 MHz */
		/* tx_desk_rsgb_par */
		0, /* unused */
		/* rx_desk_rsgb_par */
		0, /* unused */
		/* tx_desk_rsgb_pcs */
		0, /* unused */
		/* rx_desk_rsgb_pcs */
		0, /* unused */
		/* tx_fixed_delay */
		6938,
		/* pmd_adj_divisor */
		82500000,
		/* rx_fixed_delay */
		6212,
	},
	/* ICE_PTP_LNK_SPD_25G */
	{
		/* tx_par_clk */
		644531250, /* 644.53125 MHZ */
		/* rx_par_clk */
		644531250, /* 644.53125 MHz */
		/* tx_pcs_clk */
		390625000, /* 390.625 MHz */
		/* rx_pcs_clk */
		390625000, /* 390.625 MHz */
		/* tx_desk_rsgb_par */
		0, /* unused */
		/* rx_desk_rsgb_par */
		0, /* unused */
		/* tx_desk_rsgb_pcs */
		0, /* unused */
		/* rx_desk_rsgb_pcs */
		0, /* unused */
		/* tx_fixed_delay */
		2778,
		/* pmd_adj_divisor */
		206250000,
		/* rx_fixed_delay */
		2491,
	},
	/* ICE_PTP_LNK_SPD_25G_RS */
	{
		/* tx_par_clk */
		0, /* unused */
		/* rx_par_clk */
		0, /* unused */
		/* tx_pcs_clk */
		0, /* unused */
		/* rx_pcs_clk */
		0, /* unused */
		/* tx_desk_rsgb_par */
		161132812, /* 162.1328125 MHz Reed Solomon gearbox */
		/* rx_desk_rsgb_par */
		161132812, /* 162.1328125 MHz Reed Solomon gearbox */
		/* tx_desk_rsgb_pcs */
		97656250, /* 97.62625 MHz Reed Solomon gearbox */
		/* rx_desk_rsgb_pcs */
		97656250, /* 97.62625 MHz Reed Solomon gearbox */
		/* tx_fixed_delay */
		3928,
		/* pmd_adj_divisor */
		206250000,
		/* rx_fixed_delay */
		29535,
	},
	/* ICE_PTP_LNK_SPD_40G */
	{
		/* tx_par_clk */
		257812500,
		/* rx_par_clk */
		257812500,
		/* tx_pcs_clk */
		156250000, /* 156.25 MHz */
		/* rx_pcs_clk */
		156250000, /* 156.25 MHz */
		/* tx_desk_rsgb_par */
		0, /* unused */
		/* rx_desk_rsgb_par */
		156250000, /* 156.25 MHz deskew clock */
		/* tx_desk_rsgb_pcs */
		0, /* unused */
		/* rx_desk_rsgb_pcs */
		156250000, /* 156.25 MHz deskew clock */
		/* tx_fixed_delay */
		5666,
		/* pmd_adj_divisor */
		82500000,
		/* rx_fixed_delay */
		4244,
	},
	/* ICE_PTP_LNK_SPD_50G */
	{
		/* tx_par_clk */
		644531250, /* 644.53125 MHZ */
		/* rx_par_clk */
		644531250, /* 644.53125 MHZ */
		/* tx_pcs_clk */
		390625000, /* 390.625 MHz */
		/* rx_pcs_clk */
		390625000, /* 390.625 MHz */
		/* tx_desk_rsgb_par */
		0, /* unused */
		/* rx_desk_rsgb_par */
		195312500, /* 193.3125 MHz deskew clock */
		/* tx_desk_rsgb_pcs */
		0, /* unused */
		/* rx_desk_rsgb_pcs */
		195312500, /* 193.3125 MHz deskew clock */
		/* tx_fixed_delay */
		2778,
		/* pmd_adj_divisor */
		206250000,
		/* rx_fixed_delay */
		2868,
	},
	/* ICE_PTP_LNK_SPD_50G_RS */
	{
		/* tx_par_clk */
		0, /* unused */
		/* rx_par_clk */
		644531250, /* 644.53125 MHz */
		/* tx_pcs_clk */
		0, /* unused */
		/* rx_pcs_clk */
		644531250, /* 644.53125 MHz */
		/* tx_desk_rsgb_par */
		322265625, /* 322.265625 MHz Reed Solomon gearbox */
		/* rx_desk_rsgb_par */
		322265625, /* 322.265625 MHz Reed Solomon gearbox */
		/* tx_desk_rsgb_pcs */
		644531250, /* 644.53125 MHz Reed Solomon gearbox */
		/* rx_desk_rsgb_pcs */
		644531250, /* 644.53125 MHz Reed Solomon gearbox */
		/* tx_fixed_delay */
		2095,
		/* pmd_adj_divisor */
		206250000,
		/* rx_fixed_delay */
		14524,
	},
	/* ICE_PTP_LNK_SPD_100G_RS */
	{
		/* tx_par_clk */
		0, /* unused */
		/* rx_par_clk */
		644531250, /* 644.53125 MHz */
		/* tx_pcs_clk */
		0, /* unused */
		/* rx_pcs_clk */
		644531250, /* 644.53125 MHz */
		/* tx_desk_rsgb_par */
		644531250, /* 644.53125 MHz Reed Solomon gearbox */
		/* rx_desk_rsgb_par */
		644531250, /* 644.53125 MHz Reed Solomon gearbox */
		/* tx_desk_rsgb_pcs */
		390625000, /* 390.625 MHz Reed Solomon gearbox */
		/* rx_desk_rsgb_pcs */
		390625000, /* 390.625 MHz Reed Solomon gearbox */
		/* tx_fixed_delay */
		1620,
		/* pmd_adj_divisor */
		206250000,
		/* rx_fixed_delay */
		7775,
	},
};

#endif /* _ICE_PTP_CONSTS_H_ */
