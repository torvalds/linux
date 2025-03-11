// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Authors: Thomas Abraham <thomas.ab@samsung.com>
 *	    Chander Kashyap <k.chander@samsung.com>
 *
 * Common Clock Framework support for Exynos5420 SoC.
 */

#include <dt-bindings/clock/exynos5420.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk.h>

#include "clk.h"
#include "clk-cpu.h"
#include "clk-exynos5-subcmu.h"

#define APLL_LOCK		0x0
#define APLL_CON0		0x100
#define SRC_CPU			0x200
#define DIV_CPU0		0x500
#define DIV_CPU1		0x504
#define GATE_BUS_CPU		0x700
#define GATE_SCLK_CPU		0x800
#define CLKOUT_CMU_CPU		0xa00
#define SRC_MASK_CPERI		0x4300
#define GATE_IP_G2D		0x8800
#define CPLL_LOCK		0x10020
#define DPLL_LOCK		0x10030
#define EPLL_LOCK		0x10040
#define RPLL_LOCK		0x10050
#define IPLL_LOCK		0x10060
#define SPLL_LOCK		0x10070
#define VPLL_LOCK		0x10080
#define MPLL_LOCK		0x10090
#define CPLL_CON0		0x10120
#define DPLL_CON0		0x10128
#define EPLL_CON0		0x10130
#define EPLL_CON1		0x10134
#define EPLL_CON2		0x10138
#define RPLL_CON0		0x10140
#define RPLL_CON1		0x10144
#define RPLL_CON2		0x10148
#define IPLL_CON0		0x10150
#define SPLL_CON0		0x10160
#define VPLL_CON0		0x10170
#define MPLL_CON0		0x10180
#define SRC_TOP0		0x10200
#define SRC_TOP1		0x10204
#define SRC_TOP2		0x10208
#define SRC_TOP3		0x1020c
#define SRC_TOP4		0x10210
#define SRC_TOP5		0x10214
#define SRC_TOP6		0x10218
#define SRC_TOP7		0x1021c
#define SRC_TOP8		0x10220 /* 5800 specific */
#define SRC_TOP9		0x10224 /* 5800 specific */
#define SRC_DISP10		0x1022c
#define SRC_MAU			0x10240
#define SRC_FSYS		0x10244
#define SRC_PERIC0		0x10250
#define SRC_PERIC1		0x10254
#define SRC_ISP			0x10270
#define SRC_CAM			0x10274 /* 5800 specific */
#define SRC_TOP10		0x10280
#define SRC_TOP11		0x10284
#define SRC_TOP12		0x10288
#define SRC_TOP13		0x1028c /* 5800 specific */
#define SRC_MASK_TOP0		0x10300
#define SRC_MASK_TOP1		0x10304
#define SRC_MASK_TOP2		0x10308
#define SRC_MASK_TOP7		0x1031c
#define SRC_MASK_DISP10		0x1032c
#define SRC_MASK_MAU		0x10334
#define SRC_MASK_FSYS		0x10340
#define SRC_MASK_PERIC0		0x10350
#define SRC_MASK_PERIC1		0x10354
#define SRC_MASK_ISP		0x10370
#define DIV_TOP0		0x10500
#define DIV_TOP1		0x10504
#define DIV_TOP2		0x10508
#define DIV_TOP8		0x10520 /* 5800 specific */
#define DIV_TOP9		0x10524 /* 5800 specific */
#define DIV_DISP10		0x1052c
#define DIV_MAU			0x10544
#define DIV_FSYS0		0x10548
#define DIV_FSYS1		0x1054c
#define DIV_FSYS2		0x10550
#define DIV_PERIC0		0x10558
#define DIV_PERIC1		0x1055c
#define DIV_PERIC2		0x10560
#define DIV_PERIC3		0x10564
#define DIV_PERIC4		0x10568
#define DIV_CAM			0x10574 /* 5800 specific */
#define SCLK_DIV_ISP0		0x10580
#define SCLK_DIV_ISP1		0x10584
#define DIV2_RATIO0		0x10590
#define DIV4_RATIO		0x105a0
#define GATE_BUS_TOP		0x10700
#define GATE_BUS_DISP1		0x10728
#define GATE_BUS_GEN		0x1073c
#define GATE_BUS_FSYS0		0x10740
#define GATE_BUS_FSYS2		0x10748
#define GATE_BUS_PERIC		0x10750
#define GATE_BUS_PERIC1		0x10754
#define GATE_BUS_PERIS0		0x10760
#define GATE_BUS_PERIS1		0x10764
#define GATE_BUS_NOC		0x10770
#define GATE_TOP_SCLK_ISP	0x10870
#define GATE_IP_GSCL0		0x10910
#define GATE_IP_GSCL1		0x10920
#define GATE_IP_CAM		0x10924 /* 5800 specific */
#define GATE_IP_MFC		0x1092c
#define GATE_IP_DISP1		0x10928
#define GATE_IP_G3D		0x10930
#define GATE_IP_GEN		0x10934
#define GATE_IP_FSYS		0x10944
#define GATE_IP_PERIC		0x10950
#define GATE_IP_PERIS		0x10960
#define GATE_IP_MSCL		0x10970
#define GATE_TOP_SCLK_GSCL	0x10820
#define GATE_TOP_SCLK_DISP1	0x10828
#define GATE_TOP_SCLK_MAU	0x1083c
#define GATE_TOP_SCLK_FSYS	0x10840
#define GATE_TOP_SCLK_PERIC	0x10850
#define TOP_SPARE2		0x10b08
#define BPLL_LOCK		0x20010
#define BPLL_CON0		0x20110
#define SRC_CDREX		0x20200
#define DIV_CDREX0		0x20500
#define DIV_CDREX1		0x20504
#define GATE_BUS_CDREX0		0x20700
#define GATE_BUS_CDREX1		0x20704
#define KPLL_LOCK		0x28000
#define KPLL_CON0		0x28100
#define SRC_KFC			0x28200
#define DIV_KFC0		0x28500

/* NOTE: Must be equal to the last clock ID increased by one */
#define CLKS_NR			(CLK_DOUT_PCLK_DREX1 + 1)

/* Exynos5x SoC type */
enum exynos5x_soc {
	EXYNOS5420,
	EXYNOS5800,
};

/* list of PLLs */
enum exynos5x_plls {
	apll, cpll, dpll, epll, rpll, ipll, spll, vpll, mpll,
	bpll, kpll,
	nr_plls			/* number of PLLs */
};

static void __iomem *reg_base;
static enum exynos5x_soc exynos5x_soc;

/*
 * list of controller registers to be saved and restored during a
 * suspend/resume cycle.
 */
static const unsigned long exynos5x_clk_regs[] __initconst = {
	SRC_CPU,
	DIV_CPU0,
	DIV_CPU1,
	GATE_BUS_CPU,
	GATE_SCLK_CPU,
	CLKOUT_CMU_CPU,
	APLL_CON0,
	KPLL_CON0,
	CPLL_CON0,
	DPLL_CON0,
	EPLL_CON0,
	EPLL_CON1,
	EPLL_CON2,
	RPLL_CON0,
	RPLL_CON1,
	RPLL_CON2,
	IPLL_CON0,
	SPLL_CON0,
	VPLL_CON0,
	MPLL_CON0,
	SRC_TOP0,
	SRC_TOP1,
	SRC_TOP2,
	SRC_TOP3,
	SRC_TOP4,
	SRC_TOP5,
	SRC_TOP6,
	SRC_TOP7,
	SRC_DISP10,
	SRC_MAU,
	SRC_FSYS,
	SRC_PERIC0,
	SRC_PERIC1,
	SRC_TOP10,
	SRC_TOP11,
	SRC_TOP12,
	SRC_MASK_TOP2,
	SRC_MASK_TOP7,
	SRC_MASK_DISP10,
	SRC_MASK_FSYS,
	SRC_MASK_PERIC0,
	SRC_MASK_PERIC1,
	SRC_MASK_TOP0,
	SRC_MASK_TOP1,
	SRC_MASK_MAU,
	SRC_MASK_ISP,
	SRC_ISP,
	DIV_TOP0,
	DIV_TOP1,
	DIV_TOP2,
	DIV_DISP10,
	DIV_MAU,
	DIV_FSYS0,
	DIV_FSYS1,
	DIV_FSYS2,
	DIV_PERIC0,
	DIV_PERIC1,
	DIV_PERIC2,
	DIV_PERIC3,
	DIV_PERIC4,
	SCLK_DIV_ISP0,
	SCLK_DIV_ISP1,
	DIV2_RATIO0,
	DIV4_RATIO,
	GATE_BUS_DISP1,
	GATE_BUS_TOP,
	GATE_BUS_GEN,
	GATE_BUS_FSYS0,
	GATE_BUS_FSYS2,
	GATE_BUS_PERIC,
	GATE_BUS_PERIC1,
	GATE_BUS_PERIS0,
	GATE_BUS_PERIS1,
	GATE_BUS_NOC,
	GATE_TOP_SCLK_ISP,
	GATE_IP_GSCL0,
	GATE_IP_GSCL1,
	GATE_IP_MFC,
	GATE_IP_DISP1,
	GATE_IP_G3D,
	GATE_IP_GEN,
	GATE_IP_FSYS,
	GATE_IP_PERIC,
	GATE_IP_PERIS,
	GATE_IP_MSCL,
	GATE_TOP_SCLK_GSCL,
	GATE_TOP_SCLK_DISP1,
	GATE_TOP_SCLK_MAU,
	GATE_TOP_SCLK_FSYS,
	GATE_TOP_SCLK_PERIC,
	TOP_SPARE2,
	SRC_CDREX,
	DIV_CDREX0,
	DIV_CDREX1,
	SRC_KFC,
	DIV_KFC0,
	GATE_BUS_CDREX0,
	GATE_BUS_CDREX1,
};

static const unsigned long exynos5800_clk_regs[] __initconst = {
	SRC_TOP8,
	SRC_TOP9,
	SRC_CAM,
	SRC_TOP1,
	DIV_TOP8,
	DIV_TOP9,
	DIV_CAM,
	GATE_IP_CAM,
};

static const struct samsung_clk_reg_dump exynos5420_set_clksrc[] = {
	{ .offset = SRC_MASK_CPERI,		.value = 0xffffffff, },
	{ .offset = SRC_MASK_TOP0,		.value = 0x11111111, },
	{ .offset = SRC_MASK_TOP1,		.value = 0x11101111, },
	{ .offset = SRC_MASK_TOP2,		.value = 0x11111110, },
	{ .offset = SRC_MASK_TOP7,		.value = 0x00111100, },
	{ .offset = SRC_MASK_DISP10,		.value = 0x11111110, },
	{ .offset = SRC_MASK_MAU,		.value = 0x10000000, },
	{ .offset = SRC_MASK_FSYS,		.value = 0x11111110, },
	{ .offset = SRC_MASK_PERIC0,		.value = 0x11111110, },
	{ .offset = SRC_MASK_PERIC1,		.value = 0x11111100, },
	{ .offset = SRC_MASK_ISP,		.value = 0x11111000, },
	{ .offset = GATE_BUS_TOP,		.value = 0xffffffff, },
	{ .offset = GATE_BUS_DISP1,		.value = 0xffffffff, },
	{ .offset = GATE_IP_PERIC,		.value = 0xffffffff, },
	{ .offset = GATE_IP_PERIS,		.value = 0xffffffff, },
};

/* list of all parent clocks */
PNAME(mout_mspll_cpu_p) = {"mout_sclk_cpll", "mout_sclk_dpll",
				"mout_sclk_mpll", "mout_sclk_spll"};
PNAME(mout_cpu_p) = {"mout_apll", "mout_mspll_cpu"};
PNAME(mout_kfc_p) = {"mout_kpll", "mout_mspll_kfc"};
PNAME(mout_apll_p) = {"fin_pll", "fout_apll"};
PNAME(mout_bpll_p) = {"fin_pll", "fout_bpll"};
PNAME(mout_cpll_p) = {"fin_pll", "fout_cpll"};
PNAME(mout_dpll_p) = {"fin_pll", "fout_dpll"};
PNAME(mout_epll_p) = {"fin_pll", "fout_epll"};
PNAME(mout_ipll_p) = {"fin_pll", "fout_ipll"};
PNAME(mout_kpll_p) = {"fin_pll", "fout_kpll"};
PNAME(mout_mpll_p) = {"fin_pll", "fout_mpll"};
PNAME(mout_rpll_p) = {"fin_pll", "fout_rpll"};
PNAME(mout_spll_p) = {"fin_pll", "fout_spll"};
PNAME(mout_vpll_p) = {"fin_pll", "fout_vpll"};

PNAME(mout_group1_p) = {"mout_sclk_cpll", "mout_sclk_dpll",
					"mout_sclk_mpll"};
PNAME(mout_group2_p) = {"fin_pll", "mout_sclk_cpll",
			"mout_sclk_dpll", "mout_sclk_mpll", "mout_sclk_spll",
			"mout_sclk_ipll", "mout_sclk_epll", "mout_sclk_rpll"};
PNAME(mout_group3_p) = {"mout_sclk_rpll", "mout_sclk_spll"};
PNAME(mout_group4_p) = {"mout_sclk_ipll", "mout_sclk_dpll", "mout_sclk_mpll"};
PNAME(mout_group5_p) = {"mout_sclk_vpll", "mout_sclk_dpll"};

PNAME(mout_fimd1_final_p) = {"mout_fimd1", "mout_fimd1_opt"};
PNAME(mout_sw_aclk66_p)	= {"dout_aclk66", "mout_sclk_spll"};
PNAME(mout_user_aclk66_peric_p)	= { "fin_pll", "mout_sw_aclk66"};
PNAME(mout_user_pclk66_gpio_p) = {"mout_sw_aclk66", "ff_sw_aclk66"};

PNAME(mout_sw_aclk200_fsys_p) = {"dout_aclk200_fsys", "mout_sclk_spll"};
PNAME(mout_sw_pclk200_fsys_p) = {"dout_pclk200_fsys", "mout_sclk_spll"};
PNAME(mout_user_pclk200_fsys_p)	= {"fin_pll", "mout_sw_pclk200_fsys"};
PNAME(mout_user_aclk200_fsys_p)	= {"fin_pll", "mout_sw_aclk200_fsys"};

PNAME(mout_sw_aclk200_fsys2_p) = {"dout_aclk200_fsys2", "mout_sclk_spll"};
PNAME(mout_user_aclk200_fsys2_p) = {"fin_pll", "mout_sw_aclk200_fsys2"};
PNAME(mout_sw_aclk100_noc_p) = {"dout_aclk100_noc", "mout_sclk_spll"};
PNAME(mout_user_aclk100_noc_p) = {"fin_pll", "mout_sw_aclk100_noc"};

PNAME(mout_sw_aclk400_wcore_p) = {"dout_aclk400_wcore", "mout_sclk_spll"};
PNAME(mout_aclk400_wcore_bpll_p) = {"mout_aclk400_wcore", "sclk_bpll"};
PNAME(mout_user_aclk400_wcore_p) = {"fin_pll", "mout_sw_aclk400_wcore"};

PNAME(mout_sw_aclk400_isp_p) = {"dout_aclk400_isp", "mout_sclk_spll"};
PNAME(mout_user_aclk400_isp_p) = {"fin_pll", "mout_sw_aclk400_isp"};

PNAME(mout_sw_aclk333_432_isp0_p) = {"dout_aclk333_432_isp0",
					"mout_sclk_spll"};
PNAME(mout_user_aclk333_432_isp0_p) = {"fin_pll", "mout_sw_aclk333_432_isp0"};

PNAME(mout_sw_aclk333_432_isp_p) = {"dout_aclk333_432_isp", "mout_sclk_spll"};
PNAME(mout_user_aclk333_432_isp_p) = {"fin_pll", "mout_sw_aclk333_432_isp"};

PNAME(mout_sw_aclk200_p) = {"dout_aclk200", "mout_sclk_spll"};
PNAME(mout_user_aclk200_disp1_p) = {"fin_pll", "mout_sw_aclk200"};

PNAME(mout_sw_aclk400_mscl_p) = {"dout_aclk400_mscl", "mout_sclk_spll"};
PNAME(mout_user_aclk400_mscl_p)	= {"fin_pll", "mout_sw_aclk400_mscl"};

PNAME(mout_sw_aclk333_p) = {"dout_aclk333", "mout_sclk_spll"};
PNAME(mout_user_aclk333_p) = {"fin_pll", "mout_sw_aclk333"};

PNAME(mout_sw_aclk166_p) = {"dout_aclk166", "mout_sclk_spll"};
PNAME(mout_user_aclk166_p) = {"fin_pll", "mout_sw_aclk166"};

PNAME(mout_sw_aclk266_p) = {"dout_aclk266", "mout_sclk_spll"};
PNAME(mout_user_aclk266_p) = {"fin_pll", "mout_sw_aclk266"};
PNAME(mout_user_aclk266_isp_p) = {"fin_pll", "mout_sw_aclk266"};

PNAME(mout_sw_aclk333_432_gscl_p) = {"dout_aclk333_432_gscl", "mout_sclk_spll"};
PNAME(mout_user_aclk333_432_gscl_p) = {"fin_pll", "mout_sw_aclk333_432_gscl"};

PNAME(mout_sw_aclk300_gscl_p) = {"dout_aclk300_gscl", "mout_sclk_spll"};
PNAME(mout_user_aclk300_gscl_p)	= {"fin_pll", "mout_sw_aclk300_gscl"};

PNAME(mout_sw_aclk300_disp1_p) = {"dout_aclk300_disp1", "mout_sclk_spll"};
PNAME(mout_sw_aclk400_disp1_p) = {"dout_aclk400_disp1", "mout_sclk_spll"};
PNAME(mout_user_aclk300_disp1_p) = {"fin_pll", "mout_sw_aclk300_disp1"};
PNAME(mout_user_aclk400_disp1_p) = {"fin_pll", "mout_sw_aclk400_disp1"};

PNAME(mout_sw_aclk300_jpeg_p) = {"dout_aclk300_jpeg", "mout_sclk_spll"};
PNAME(mout_user_aclk300_jpeg_p) = {"fin_pll", "mout_sw_aclk300_jpeg"};

PNAME(mout_sw_aclk_g3d_p) = {"dout_aclk_g3d", "mout_sclk_spll"};
PNAME(mout_user_aclk_g3d_p) = {"fin_pll", "mout_sw_aclk_g3d"};

PNAME(mout_sw_aclk266_g2d_p) = {"dout_aclk266_g2d", "mout_sclk_spll"};
PNAME(mout_user_aclk266_g2d_p) = {"fin_pll", "mout_sw_aclk266_g2d"};

PNAME(mout_sw_aclk333_g2d_p) = {"dout_aclk333_g2d", "mout_sclk_spll"};
PNAME(mout_user_aclk333_g2d_p) = {"fin_pll", "mout_sw_aclk333_g2d"};

PNAME(mout_audio0_p) = {"fin_pll", "cdclk0", "mout_sclk_dpll",
			"mout_sclk_mpll", "mout_sclk_spll", "mout_sclk_ipll",
			"mout_sclk_epll", "mout_sclk_rpll"};
PNAME(mout_audio1_p) = {"fin_pll", "cdclk1", "mout_sclk_dpll",
			"mout_sclk_mpll", "mout_sclk_spll", "mout_sclk_ipll",
			"mout_sclk_epll", "mout_sclk_rpll"};
PNAME(mout_audio2_p) = {"fin_pll", "cdclk2", "mout_sclk_dpll",
			"mout_sclk_mpll", "mout_sclk_spll", "mout_sclk_ipll",
			"mout_sclk_epll", "mout_sclk_rpll"};
PNAME(mout_spdif_p) = {"fin_pll", "dout_audio0", "dout_audio1",
			"dout_audio2", "spdif_extclk", "mout_sclk_ipll",
			"mout_sclk_epll", "mout_sclk_rpll"};
PNAME(mout_hdmi_p) = {"dout_hdmi_pixel", "sclk_hdmiphy"};
PNAME(mout_maudio0_p) = {"fin_pll", "maudio_clk", "mout_sclk_dpll",
			 "mout_sclk_mpll", "mout_sclk_spll", "mout_sclk_ipll",
			 "mout_sclk_epll", "mout_sclk_rpll"};
PNAME(mout_mau_epll_clk_p) = {"mout_sclk_epll", "mout_sclk_dpll",
				"mout_sclk_mpll", "mout_sclk_spll"};
PNAME(mout_mclk_cdrex_p) = {"mout_bpll", "mout_mx_mspll_ccore"};

/* List of parents specific to exynos5800 */
PNAME(mout_epll2_5800_p)	= { "mout_sclk_epll", "ff_dout_epll2" };
PNAME(mout_group1_5800_p)	= { "mout_sclk_cpll", "mout_sclk_dpll",
				"mout_sclk_mpll", "ff_dout_spll2" };
PNAME(mout_group2_5800_p)	= { "mout_sclk_cpll", "mout_sclk_dpll",
					"mout_sclk_mpll", "ff_dout_spll2",
					"mout_epll2", "mout_sclk_ipll" };
PNAME(mout_group3_5800_p)	= { "mout_sclk_cpll", "mout_sclk_dpll",
					"mout_sclk_mpll", "ff_dout_spll2",
					"mout_epll2" };
PNAME(mout_group5_5800_p)	= { "mout_sclk_cpll", "mout_sclk_dpll",
					"mout_sclk_mpll", "mout_sclk_spll" };
PNAME(mout_group6_5800_p)	= { "mout_sclk_ipll", "mout_sclk_dpll",
				"mout_sclk_mpll", "ff_dout_spll2" };
PNAME(mout_group7_5800_p)	= { "mout_sclk_cpll", "mout_sclk_dpll",
					"mout_sclk_mpll", "mout_sclk_spll",
					"mout_epll2", "mout_sclk_ipll" };
PNAME(mout_mx_mspll_ccore_p)	= {"sclk_bpll", "mout_sclk_dpll",
					"mout_sclk_mpll", "ff_dout_spll2",
					"mout_sclk_spll", "mout_sclk_epll"};
PNAME(mout_mau_epll_clk_5800_p)	= { "mout_sclk_epll", "mout_sclk_dpll",
					"mout_sclk_mpll",
					"ff_dout_spll2" };
PNAME(mout_group8_5800_p)	= { "dout_aclk432_scaler", "dout_sclk_sw" };
PNAME(mout_group9_5800_p)	= { "dout_osc_div", "mout_sw_aclk432_scaler" };
PNAME(mout_group10_5800_p)	= { "dout_aclk432_cam", "dout_sclk_sw" };
PNAME(mout_group11_5800_p)	= { "dout_osc_div", "mout_sw_aclk432_cam" };
PNAME(mout_group12_5800_p)	= { "dout_aclkfl1_550_cam", "dout_sclk_sw" };
PNAME(mout_group13_5800_p)	= { "dout_osc_div", "mout_sw_aclkfl1_550_cam" };
PNAME(mout_group14_5800_p)	= { "dout_aclk550_cam", "dout_sclk_sw" };
PNAME(mout_group15_5800_p)	= { "dout_osc_div", "mout_sw_aclk550_cam" };
PNAME(mout_group16_5800_p)	= { "dout_osc_div", "mout_mau_epll_clk" };
PNAME(mout_mx_mspll_ccore_phy_p) = { "sclk_bpll", "mout_sclk_dpll",
					"mout_sclk_mpll", "ff_dout_spll2",
					"mout_sclk_spll", "mout_sclk_epll"};

/* fixed rate clocks generated outside the soc */
static struct samsung_fixed_rate_clock
		exynos5x_fixed_rate_ext_clks[] __initdata = {
	FRATE(CLK_FIN_PLL, "fin_pll", NULL, 0, 0),
};

/* fixed rate clocks generated inside the soc */
static const struct samsung_fixed_rate_clock exynos5x_fixed_rate_clks[] __initconst = {
	FRATE(CLK_SCLK_HDMIPHY, "sclk_hdmiphy", NULL, 0, 24000000),
	FRATE(0, "sclk_pwi", NULL, 0, 24000000),
	FRATE(0, "sclk_usbh20", NULL, 0, 48000000),
	FRATE(0, "mphy_refclk_ixtal24", NULL, 0, 48000000),
	FRATE(0, "sclk_usbh20_scan_clk", NULL, 0, 480000000),
};

static const struct samsung_fixed_factor_clock
		exynos5x_fixed_factor_clks[] __initconst = {
	FFACTOR(0, "ff_hsic_12m", "fin_pll", 1, 2, 0),
	FFACTOR(0, "ff_sw_aclk66", "mout_sw_aclk66", 1, 2, 0),
};

static const struct samsung_fixed_factor_clock
		exynos5800_fixed_factor_clks[] __initconst = {
	FFACTOR(0, "ff_dout_epll2", "mout_sclk_epll", 1, 2, 0),
	FFACTOR(CLK_FF_DOUT_SPLL2, "ff_dout_spll2", "mout_sclk_spll", 1, 2, 0),
};

static const struct samsung_mux_clock exynos5800_mux_clks[] __initconst = {
	MUX(0, "mout_aclk400_isp", mout_group3_5800_p, SRC_TOP0, 0, 3),
	MUX(0, "mout_aclk400_mscl", mout_group3_5800_p, SRC_TOP0, 4, 3),
	MUX(0, "mout_aclk400_wcore", mout_group2_5800_p, SRC_TOP0, 16, 3),
	MUX(0, "mout_aclk100_noc", mout_group1_5800_p, SRC_TOP0, 20, 2),

	MUX(0, "mout_aclk333_432_gscl", mout_group6_5800_p, SRC_TOP1, 0, 2),
	MUX(0, "mout_aclk333_432_isp", mout_group6_5800_p, SRC_TOP1, 4, 2),
	MUX(0, "mout_aclk333_432_isp0", mout_group6_5800_p, SRC_TOP1, 12, 2),
	MUX(0, "mout_aclk266", mout_group5_5800_p, SRC_TOP1, 20, 2),
	MUX(0, "mout_aclk333", mout_group1_5800_p, SRC_TOP1, 28, 2),

	MUX(0, "mout_aclk400_disp1", mout_group7_5800_p, SRC_TOP2, 4, 3),
	MUX(0, "mout_aclk333_g2d", mout_group5_5800_p, SRC_TOP2, 8, 2),
	MUX(0, "mout_aclk266_g2d", mout_group5_5800_p, SRC_TOP2, 12, 2),
	MUX(0, "mout_aclk300_jpeg", mout_group5_5800_p, SRC_TOP2, 20, 2),
	MUX(0, "mout_aclk300_disp1", mout_group5_5800_p, SRC_TOP2, 24, 2),
	MUX(0, "mout_aclk300_gscl", mout_group5_5800_p, SRC_TOP2, 28, 2),

	MUX(CLK_MOUT_MX_MSPLL_CCORE_PHY, "mout_mx_mspll_ccore_phy",
		mout_mx_mspll_ccore_phy_p, SRC_TOP7, 0, 3),

	MUX(CLK_MOUT_MX_MSPLL_CCORE, "mout_mx_mspll_ccore",
			mout_mx_mspll_ccore_p, SRC_TOP7, 16, 3),
	MUX_F(CLK_MOUT_MAU_EPLL, "mout_mau_epll_clk", mout_mau_epll_clk_5800_p,
			SRC_TOP7, 20, 2, CLK_SET_RATE_PARENT, 0),
	MUX(CLK_SCLK_BPLL, "sclk_bpll", mout_bpll_p, SRC_TOP7, 24, 1),
	MUX(0, "mout_epll2", mout_epll2_5800_p, SRC_TOP7, 28, 1),

	MUX(0, "mout_aclk550_cam", mout_group3_5800_p, SRC_TOP8, 16, 3),
	MUX(0, "mout_aclkfl1_550_cam", mout_group3_5800_p, SRC_TOP8, 20, 3),
	MUX(0, "mout_aclk432_cam", mout_group6_5800_p, SRC_TOP8, 24, 2),
	MUX(0, "mout_aclk432_scaler", mout_group6_5800_p, SRC_TOP8, 28, 2),

	MUX_F(CLK_MOUT_USER_MAU_EPLL, "mout_user_mau_epll", mout_group16_5800_p,
			SRC_TOP9, 8, 1, CLK_SET_RATE_PARENT, 0),
	MUX(0, "mout_user_aclk550_cam", mout_group15_5800_p,
							SRC_TOP9, 16, 1),
	MUX(0, "mout_user_aclkfl1_550_cam", mout_group13_5800_p,
							SRC_TOP9, 20, 1),
	MUX(0, "mout_user_aclk432_cam", mout_group11_5800_p,
							SRC_TOP9, 24, 1),
	MUX(0, "mout_user_aclk432_scaler", mout_group9_5800_p,
							SRC_TOP9, 28, 1),

	MUX(0, "mout_sw_aclk550_cam", mout_group14_5800_p, SRC_TOP13, 16, 1),
	MUX(0, "mout_sw_aclkfl1_550_cam", mout_group12_5800_p,
							SRC_TOP13, 20, 1),
	MUX(0, "mout_sw_aclk432_cam", mout_group10_5800_p,
							SRC_TOP13, 24, 1),
	MUX(0, "mout_sw_aclk432_scaler", mout_group8_5800_p,
							SRC_TOP13, 28, 1),

	MUX(0, "mout_fimd1", mout_group2_p, SRC_DISP10, 4, 3),
};

static const struct samsung_div_clock exynos5800_div_clks[] __initconst = {
	DIV(CLK_DOUT_ACLK400_WCORE, "dout_aclk400_wcore",
			"mout_aclk400_wcore", DIV_TOP0, 16, 3),
	DIV(0, "dout_aclk550_cam", "mout_aclk550_cam",
				DIV_TOP8, 16, 3),
	DIV(0, "dout_aclkfl1_550_cam", "mout_aclkfl1_550_cam",
				DIV_TOP8, 20, 3),
	DIV(0, "dout_aclk432_cam", "mout_aclk432_cam",
				DIV_TOP8, 24, 3),
	DIV(0, "dout_aclk432_scaler", "mout_aclk432_scaler",
				DIV_TOP8, 28, 3),

	DIV(0, "dout_osc_div", "fin_pll", DIV_TOP9, 20, 3),
	DIV(0, "dout_sclk_sw", "sclk_spll", DIV_TOP9, 24, 6),
};

static const struct samsung_gate_clock exynos5800_gate_clks[] __initconst = {
	GATE(CLK_ACLK550_CAM, "aclk550_cam", "mout_user_aclk550_cam",
				GATE_BUS_TOP, 24, CLK_IS_CRITICAL, 0),
	GATE(CLK_ACLK432_SCALER, "aclk432_scaler", "mout_user_aclk432_scaler",
				GATE_BUS_TOP, 27, CLK_IS_CRITICAL, 0),
};

static const struct samsung_mux_clock exynos5420_mux_clks[] __initconst = {
	MUX(0, "sclk_bpll", mout_bpll_p, TOP_SPARE2, 0, 1),
	MUX(0, "mout_aclk400_wcore_bpll", mout_aclk400_wcore_bpll_p,
				TOP_SPARE2, 4, 1),

	MUX(0, "mout_aclk400_isp", mout_group1_p, SRC_TOP0, 0, 2),
	MUX(0, "mout_aclk400_mscl", mout_group1_p, SRC_TOP0, 4, 2),
	MUX(0, "mout_aclk400_wcore", mout_group1_p, SRC_TOP0, 16, 2),
	MUX(0, "mout_aclk100_noc", mout_group1_p, SRC_TOP0, 20, 2),

	MUX(0, "mout_aclk333_432_gscl", mout_group4_p, SRC_TOP1, 0, 2),
	MUX(0, "mout_aclk333_432_isp", mout_group4_p,
				SRC_TOP1, 4, 2),
	MUX(0, "mout_aclk333_432_isp0", mout_group4_p, SRC_TOP1, 12, 2),
	MUX(0, "mout_aclk266", mout_group1_p, SRC_TOP1, 20, 2),
	MUX(0, "mout_aclk333", mout_group1_p, SRC_TOP1, 28, 2),

	MUX(0, "mout_aclk400_disp1", mout_group1_p, SRC_TOP2, 4, 2),
	MUX(0, "mout_aclk333_g2d", mout_group1_p, SRC_TOP2, 8, 2),
	MUX(0, "mout_aclk266_g2d", mout_group1_p, SRC_TOP2, 12, 2),
	MUX(0, "mout_aclk300_jpeg", mout_group1_p, SRC_TOP2, 20, 2),
	MUX(0, "mout_aclk300_disp1", mout_group1_p, SRC_TOP2, 24, 2),
	MUX(0, "mout_aclk300_gscl", mout_group1_p, SRC_TOP2, 28, 2),

	MUX(CLK_MOUT_MX_MSPLL_CCORE, "mout_mx_mspll_ccore",
			mout_group5_5800_p, SRC_TOP7, 16, 2),
	MUX_F(0, "mout_mau_epll_clk", mout_mau_epll_clk_p, SRC_TOP7, 20, 2,
	      CLK_SET_RATE_PARENT, 0),

	MUX(0, "mout_fimd1", mout_group3_p, SRC_DISP10, 4, 1),
};

static const struct samsung_div_clock exynos5420_div_clks[] __initconst = {
	DIV(CLK_DOUT_ACLK400_WCORE, "dout_aclk400_wcore",
			"mout_aclk400_wcore_bpll", DIV_TOP0, 16, 3),
};

static const struct samsung_gate_clock exynos5420_gate_clks[] __initconst = {
	GATE(CLK_SECKEY, "seckey", "aclk66_psgen", GATE_BUS_PERIS1, 1, 0, 0),
	/* Maudio Block */
	GATE(CLK_MAU_EPLL, "mau_epll", "mout_mau_epll_clk",
			SRC_MASK_TOP7, 20, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MAUDIO0, "sclk_maudio0", "dout_maudio0",
		GATE_TOP_SCLK_MAU, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MAUPCM0, "sclk_maupcm0", "dout_maupcm0",
		GATE_TOP_SCLK_MAU, 1, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_mux_clock exynos5x_mux_clks[] __initconst = {
	MUX(0, "mout_user_pclk66_gpio", mout_user_pclk66_gpio_p,
			SRC_TOP7, 4, 1),
	MUX(CLK_MOUT_MSPLL_KFC, "mout_mspll_kfc", mout_mspll_cpu_p,
	    SRC_TOP7, 8, 2),
	MUX(CLK_MOUT_MSPLL_CPU, "mout_mspll_cpu", mout_mspll_cpu_p,
	    SRC_TOP7, 12, 2),
	MUX_F(CLK_MOUT_APLL, "mout_apll", mout_apll_p, SRC_CPU, 0, 1,
	      CLK_SET_RATE_PARENT | CLK_RECALC_NEW_RATES, 0),
	MUX(0, "mout_cpu", mout_cpu_p, SRC_CPU, 16, 1),
	MUX_F(CLK_MOUT_KPLL, "mout_kpll", mout_kpll_p, SRC_KFC, 0, 1,
	      CLK_SET_RATE_PARENT | CLK_RECALC_NEW_RATES, 0),
	MUX(0, "mout_kfc", mout_kfc_p, SRC_KFC, 16, 1),

	MUX(0, "mout_aclk200", mout_group1_p, SRC_TOP0, 8, 2),
	MUX(0, "mout_aclk200_fsys2", mout_group1_p, SRC_TOP0, 12, 2),
	MUX(0, "mout_pclk200_fsys", mout_group1_p, SRC_TOP0, 24, 2),
	MUX(0, "mout_aclk200_fsys", mout_group1_p, SRC_TOP0, 28, 2),

	MUX(0, "mout_aclk66", mout_group1_p, SRC_TOP1, 8, 2),
	MUX(0, "mout_aclk166", mout_group1_p, SRC_TOP1, 24, 2),

	MUX_F(0, "mout_aclk_g3d", mout_group5_p, SRC_TOP2, 16, 1,
	      CLK_SET_RATE_PARENT, 0),

	MUX(0, "mout_user_aclk400_isp", mout_user_aclk400_isp_p,
			SRC_TOP3, 0, 1),
	MUX(0, "mout_user_aclk400_mscl", mout_user_aclk400_mscl_p,
			SRC_TOP3, 4, 1),
	MUX(CLK_MOUT_USER_ACLK200_DISP1, "mout_user_aclk200_disp1",
			mout_user_aclk200_disp1_p, SRC_TOP3, 8, 1),
	MUX(0, "mout_user_aclk200_fsys2", mout_user_aclk200_fsys2_p,
			SRC_TOP3, 12, 1),
	MUX(0, "mout_user_aclk400_wcore", mout_user_aclk400_wcore_p,
			SRC_TOP3, 16, 1),
	MUX(0, "mout_user_aclk100_noc", mout_user_aclk100_noc_p,
			SRC_TOP3, 20, 1),
	MUX(0, "mout_user_pclk200_fsys", mout_user_pclk200_fsys_p,
			SRC_TOP3, 24, 1),
	MUX(0, "mout_user_aclk200_fsys", mout_user_aclk200_fsys_p,
			SRC_TOP3, 28, 1),

	MUX(0, "mout_user_aclk333_432_gscl", mout_user_aclk333_432_gscl_p,
			SRC_TOP4, 0, 1),
	MUX(0, "mout_user_aclk333_432_isp", mout_user_aclk333_432_isp_p,
			SRC_TOP4, 4, 1),
	MUX(0, "mout_user_aclk66_peric", mout_user_aclk66_peric_p,
			SRC_TOP4, 8, 1),
	MUX(0, "mout_user_aclk333_432_isp0", mout_user_aclk333_432_isp0_p,
			SRC_TOP4, 12, 1),
	MUX(0, "mout_user_aclk266_isp", mout_user_aclk266_isp_p,
			SRC_TOP4, 16, 1),
	MUX(0, "mout_user_aclk266", mout_user_aclk266_p, SRC_TOP4, 20, 1),
	MUX(0, "mout_user_aclk166", mout_user_aclk166_p, SRC_TOP4, 24, 1),
	MUX(CLK_MOUT_USER_ACLK333, "mout_user_aclk333", mout_user_aclk333_p,
			SRC_TOP4, 28, 1),

	MUX(CLK_MOUT_USER_ACLK400_DISP1, "mout_user_aclk400_disp1",
			mout_user_aclk400_disp1_p, SRC_TOP5, 0, 1),
	MUX(0, "mout_user_aclk66_psgen", mout_user_aclk66_peric_p,
			SRC_TOP5, 4, 1),
	MUX(0, "mout_user_aclk333_g2d", mout_user_aclk333_g2d_p,
			SRC_TOP5, 8, 1),
	MUX(0, "mout_user_aclk266_g2d", mout_user_aclk266_g2d_p,
			SRC_TOP5, 12, 1),
	MUX_F(CLK_MOUT_G3D, "mout_user_aclk_g3d", mout_user_aclk_g3d_p,
			SRC_TOP5, 16, 1, CLK_SET_RATE_PARENT, 0),
	MUX(0, "mout_user_aclk300_jpeg", mout_user_aclk300_jpeg_p,
			SRC_TOP5, 20, 1),
	MUX(CLK_MOUT_USER_ACLK300_DISP1, "mout_user_aclk300_disp1",
			mout_user_aclk300_disp1_p, SRC_TOP5, 24, 1),
	MUX(CLK_MOUT_USER_ACLK300_GSCL, "mout_user_aclk300_gscl",
			mout_user_aclk300_gscl_p, SRC_TOP5, 28, 1),

	MUX(0, "mout_sclk_mpll", mout_mpll_p, SRC_TOP6, 0, 1),
	MUX_F(CLK_MOUT_VPLL, "mout_sclk_vpll", mout_vpll_p, SRC_TOP6, 4, 1,
	      CLK_SET_RATE_PARENT, 0),
	MUX(CLK_MOUT_SCLK_SPLL, "mout_sclk_spll", mout_spll_p, SRC_TOP6, 8, 1),
	MUX(0, "mout_sclk_ipll", mout_ipll_p, SRC_TOP6, 12, 1),
	MUX(0, "mout_sclk_rpll", mout_rpll_p, SRC_TOP6, 16, 1),
	MUX_F(CLK_MOUT_EPLL, "mout_sclk_epll", mout_epll_p, SRC_TOP6, 20, 1,
			CLK_SET_RATE_PARENT, 0),
	MUX(0, "mout_sclk_dpll", mout_dpll_p, SRC_TOP6, 24, 1),
	MUX(0, "mout_sclk_cpll", mout_cpll_p, SRC_TOP6, 28, 1),

	MUX(0, "mout_sw_aclk400_isp", mout_sw_aclk400_isp_p,
			SRC_TOP10, 0, 1),
	MUX(0, "mout_sw_aclk400_mscl", mout_sw_aclk400_mscl_p,
			SRC_TOP10, 4, 1),
	MUX(CLK_MOUT_SW_ACLK200, "mout_sw_aclk200", mout_sw_aclk200_p,
			SRC_TOP10, 8, 1),
	MUX(0, "mout_sw_aclk200_fsys2", mout_sw_aclk200_fsys2_p,
			SRC_TOP10, 12, 1),
	MUX(0, "mout_sw_aclk400_wcore", mout_sw_aclk400_wcore_p,
			SRC_TOP10, 16, 1),
	MUX(0, "mout_sw_aclk100_noc", mout_sw_aclk100_noc_p,
			SRC_TOP10, 20, 1),
	MUX(0, "mout_sw_pclk200_fsys", mout_sw_pclk200_fsys_p,
			SRC_TOP10, 24, 1),
	MUX(0, "mout_sw_aclk200_fsys", mout_sw_aclk200_fsys_p,
			SRC_TOP10, 28, 1),

	MUX(0, "mout_sw_aclk333_432_gscl", mout_sw_aclk333_432_gscl_p,
			SRC_TOP11, 0, 1),
	MUX(0, "mout_sw_aclk333_432_isp", mout_sw_aclk333_432_isp_p,
			SRC_TOP11, 4, 1),
	MUX(0, "mout_sw_aclk66", mout_sw_aclk66_p, SRC_TOP11, 8, 1),
	MUX(0, "mout_sw_aclk333_432_isp0", mout_sw_aclk333_432_isp0_p,
			SRC_TOP11, 12, 1),
	MUX(0, "mout_sw_aclk266", mout_sw_aclk266_p, SRC_TOP11, 20, 1),
	MUX(0, "mout_sw_aclk166", mout_sw_aclk166_p, SRC_TOP11, 24, 1),
	MUX(CLK_MOUT_SW_ACLK333, "mout_sw_aclk333", mout_sw_aclk333_p,
			SRC_TOP11, 28, 1),

	MUX(CLK_MOUT_SW_ACLK400, "mout_sw_aclk400_disp1",
			mout_sw_aclk400_disp1_p, SRC_TOP12, 4, 1),
	MUX(0, "mout_sw_aclk333_g2d", mout_sw_aclk333_g2d_p,
			SRC_TOP12, 8, 1),
	MUX(0, "mout_sw_aclk266_g2d", mout_sw_aclk266_g2d_p,
			SRC_TOP12, 12, 1),
	MUX_F(CLK_MOUT_SW_ACLK_G3D, "mout_sw_aclk_g3d", mout_sw_aclk_g3d_p,
			SRC_TOP12, 16, 1, CLK_SET_RATE_PARENT, 0),
	MUX(0, "mout_sw_aclk300_jpeg", mout_sw_aclk300_jpeg_p,
			SRC_TOP12, 20, 1),
	MUX(CLK_MOUT_SW_ACLK300, "mout_sw_aclk300_disp1",
			mout_sw_aclk300_disp1_p, SRC_TOP12, 24, 1),
	MUX(CLK_MOUT_SW_ACLK300_GSCL, "mout_sw_aclk300_gscl",
			mout_sw_aclk300_gscl_p, SRC_TOP12, 28, 1),

	/* DISP1 Block */
	MUX(0, "mout_mipi1", mout_group2_p, SRC_DISP10, 16, 3),
	MUX(0, "mout_dp1", mout_group2_p, SRC_DISP10, 20, 3),
	MUX(0, "mout_pixel", mout_group2_p, SRC_DISP10, 24, 3),
	MUX(CLK_MOUT_HDMI, "mout_hdmi", mout_hdmi_p, SRC_DISP10, 28, 1),
	MUX(0, "mout_fimd1_opt", mout_group2_p, SRC_DISP10, 8, 3),

	MUX(0, "mout_fimd1_final", mout_fimd1_final_p, TOP_SPARE2, 8, 1),

	/* CDREX block */
	MUX_F(CLK_MOUT_MCLK_CDREX, "mout_mclk_cdrex", mout_mclk_cdrex_p,
			SRC_CDREX, 4, 1, CLK_SET_RATE_PARENT, 0),
	MUX_F(CLK_MOUT_BPLL, "mout_bpll", mout_bpll_p, SRC_CDREX, 0, 1,
			CLK_SET_RATE_PARENT, 0),

	/* MAU Block */
	MUX(CLK_MOUT_MAUDIO0, "mout_maudio0", mout_maudio0_p, SRC_MAU, 28, 3),

	/* FSYS Block */
	MUX(0, "mout_usbd301", mout_group2_p, SRC_FSYS, 4, 3),
	MUX(0, "mout_mmc0", mout_group2_p, SRC_FSYS, 8, 3),
	MUX(0, "mout_mmc1", mout_group2_p, SRC_FSYS, 12, 3),
	MUX(0, "mout_mmc2", mout_group2_p, SRC_FSYS, 16, 3),
	MUX(0, "mout_usbd300", mout_group2_p, SRC_FSYS, 20, 3),
	MUX(0, "mout_unipro", mout_group2_p, SRC_FSYS, 24, 3),
	MUX(0, "mout_mphy_refclk", mout_group2_p, SRC_FSYS, 28, 3),

	/* PERIC Block */
	MUX(0, "mout_uart0", mout_group2_p, SRC_PERIC0, 4, 3),
	MUX(0, "mout_uart1", mout_group2_p, SRC_PERIC0, 8, 3),
	MUX(0, "mout_uart2", mout_group2_p, SRC_PERIC0, 12, 3),
	MUX(0, "mout_uart3", mout_group2_p, SRC_PERIC0, 16, 3),
	MUX(0, "mout_pwm", mout_group2_p, SRC_PERIC0, 24, 3),
	MUX(0, "mout_spdif", mout_spdif_p, SRC_PERIC0, 28, 3),
	MUX(0, "mout_audio0", mout_audio0_p, SRC_PERIC1, 8, 3),
	MUX(0, "mout_audio1", mout_audio1_p, SRC_PERIC1, 12, 3),
	MUX(0, "mout_audio2", mout_audio2_p, SRC_PERIC1, 16, 3),
	MUX(0, "mout_spi0", mout_group2_p, SRC_PERIC1, 20, 3),
	MUX(0, "mout_spi1", mout_group2_p, SRC_PERIC1, 24, 3),
	MUX(0, "mout_spi2", mout_group2_p, SRC_PERIC1, 28, 3),

	/* ISP Block */
	MUX(0, "mout_pwm_isp", mout_group2_p, SRC_ISP, 24, 3),
	MUX(0, "mout_uart_isp", mout_group2_p, SRC_ISP, 20, 3),
	MUX(0, "mout_spi0_isp", mout_group2_p, SRC_ISP, 12, 3),
	MUX(0, "mout_spi1_isp", mout_group2_p, SRC_ISP, 16, 3),
	MUX(0, "mout_isp_sensor", mout_group2_p, SRC_ISP, 28, 3),
};

static const struct samsung_div_clock exynos5x_div_clks[] __initconst = {
	DIV(0, "div_arm", "mout_cpu", DIV_CPU0, 0, 3),
	DIV(0, "sclk_apll", "mout_apll", DIV_CPU0, 24, 3),
	DIV(0, "armclk2", "div_arm", DIV_CPU0, 28, 3),
	DIV(0, "div_kfc", "mout_kfc", DIV_KFC0, 0, 3),
	DIV(0, "sclk_kpll", "mout_kpll", DIV_KFC0, 24, 3),

	DIV(CLK_DOUT_ACLK400_ISP, "dout_aclk400_isp", "mout_aclk400_isp",
			DIV_TOP0, 0, 3),
	DIV(CLK_DOUT_ACLK400_MSCL, "dout_aclk400_mscl", "mout_aclk400_mscl",
			DIV_TOP0, 4, 3),
	DIV(CLK_DOUT_ACLK200, "dout_aclk200", "mout_aclk200",
			DIV_TOP0, 8, 3),
	DIV(CLK_DOUT_ACLK200_FSYS2, "dout_aclk200_fsys2", "mout_aclk200_fsys2",
			DIV_TOP0, 12, 3),
	DIV(CLK_DOUT_ACLK100_NOC, "dout_aclk100_noc", "mout_aclk100_noc",
			DIV_TOP0, 20, 3),
	DIV(CLK_DOUT_PCLK200_FSYS, "dout_pclk200_fsys", "mout_pclk200_fsys",
			DIV_TOP0, 24, 3),
	DIV(CLK_DOUT_ACLK200_FSYS, "dout_aclk200_fsys", "mout_aclk200_fsys",
			DIV_TOP0, 28, 3),
	DIV(CLK_DOUT_ACLK333_432_GSCL, "dout_aclk333_432_gscl",
			"mout_aclk333_432_gscl", DIV_TOP1, 0, 3),
	DIV(CLK_DOUT_ACLK333_432_ISP, "dout_aclk333_432_isp",
			"mout_aclk333_432_isp", DIV_TOP1, 4, 3),
	DIV(CLK_DOUT_ACLK66, "dout_aclk66", "mout_aclk66",
			DIV_TOP1, 8, 6),
	DIV(CLK_DOUT_ACLK333_432_ISP0, "dout_aclk333_432_isp0",
			"mout_aclk333_432_isp0", DIV_TOP1, 16, 3),
	DIV(CLK_DOUT_ACLK266, "dout_aclk266", "mout_aclk266",
			DIV_TOP1, 20, 3),
	DIV(CLK_DOUT_ACLK166, "dout_aclk166", "mout_aclk166",
			DIV_TOP1, 24, 3),
	DIV(CLK_DOUT_ACLK333, "dout_aclk333", "mout_aclk333",
			DIV_TOP1, 28, 3),

	DIV(CLK_DOUT_ACLK333_G2D, "dout_aclk333_g2d", "mout_aclk333_g2d",
			DIV_TOP2, 8, 3),
	DIV(CLK_DOUT_ACLK266_G2D, "dout_aclk266_g2d", "mout_aclk266_g2d",
			DIV_TOP2, 12, 3),
	DIV_F(CLK_DOUT_ACLK_G3D, "dout_aclk_g3d", "mout_aclk_g3d", DIV_TOP2,
			16, 3, CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DOUT_ACLK300_JPEG, "dout_aclk300_jpeg", "mout_aclk300_jpeg",
			DIV_TOP2, 20, 3),
	DIV(CLK_DOUT_ACLK300_DISP1, "dout_aclk300_disp1",
			"mout_aclk300_disp1", DIV_TOP2, 24, 3),
	DIV(CLK_DOUT_ACLK300_GSCL, "dout_aclk300_gscl", "mout_aclk300_gscl",
			DIV_TOP2, 28, 3),

	/* DISP1 Block */
	DIV(0, "dout_fimd1", "mout_fimd1_final", DIV_DISP10, 0, 4),
	DIV(0, "dout_mipi1", "mout_mipi1", DIV_DISP10, 16, 8),
	DIV(0, "dout_dp1", "mout_dp1", DIV_DISP10, 24, 4),
	DIV(CLK_DOUT_PIXEL, "dout_hdmi_pixel", "mout_pixel", DIV_DISP10, 28, 4),
	DIV(CLK_DOUT_ACLK400_DISP1, "dout_aclk400_disp1",
			"mout_aclk400_disp1", DIV_TOP2, 4, 3),

	/* CDREX Block */
	/*
	 * The three clocks below are controlled using the same register and
	 * bits. They are put into one because there is a need of
	 * synchronization between the BUS and DREXs (two external memory
	 * interfaces).
	 * They are put here to show this HW assumption and for clock
	 * information summary completeness.
	 */
	DIV_F(CLK_DOUT_PCLK_CDREX, "dout_pclk_cdrex", "dout_aclk_cdrex1",
			DIV_CDREX0, 28, 3, CLK_GET_RATE_NOCACHE, 0),
	DIV_F(CLK_DOUT_PCLK_DREX0, "dout_pclk_drex0", "dout_cclk_drex0",
			DIV_CDREX0, 28, 3, CLK_GET_RATE_NOCACHE, 0),
	DIV_F(CLK_DOUT_PCLK_DREX1, "dout_pclk_drex1", "dout_cclk_drex0",
			DIV_CDREX0, 28, 3, CLK_GET_RATE_NOCACHE, 0),

	DIV_F(CLK_DOUT_SCLK_CDREX, "dout_sclk_cdrex", "mout_mclk_cdrex",
			DIV_CDREX0, 24, 3, CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DOUT_ACLK_CDREX1, "dout_aclk_cdrex1", "dout_clk2x_phy0",
			DIV_CDREX0, 16, 3),
	DIV(CLK_DOUT_CCLK_DREX0, "dout_cclk_drex0", "dout_clk2x_phy0",
			DIV_CDREX0, 8, 3),
	DIV(CLK_DOUT_CLK2X_PHY0, "dout_clk2x_phy0", "dout_sclk_cdrex",
			DIV_CDREX0, 3, 5),

	DIV(CLK_DOUT_PCLK_CORE_MEM, "dout_pclk_core_mem", "mout_mclk_cdrex",
			DIV_CDREX1, 8, 3),

	/* Audio Block */
	DIV(0, "dout_maudio0", "mout_maudio0", DIV_MAU, 20, 4),
	DIV(0, "dout_maupcm0", "dout_maudio0", DIV_MAU, 24, 8),

	/* USB3.0 */
	DIV(0, "dout_usbphy301", "mout_usbd301", DIV_FSYS0, 12, 4),
	DIV(0, "dout_usbphy300", "mout_usbd300", DIV_FSYS0, 16, 4),
	DIV(0, "dout_usbd301", "mout_usbd301", DIV_FSYS0, 20, 4),
	DIV(0, "dout_usbd300", "mout_usbd300", DIV_FSYS0, 24, 4),

	/* MMC */
	DIV(0, "dout_mmc0", "mout_mmc0", DIV_FSYS1, 0, 10),
	DIV(0, "dout_mmc1", "mout_mmc1", DIV_FSYS1, 10, 10),
	DIV(0, "dout_mmc2", "mout_mmc2", DIV_FSYS1, 20, 10),

	DIV(0, "dout_unipro", "mout_unipro", DIV_FSYS2, 24, 8),
	DIV(0, "dout_mphy_refclk", "mout_mphy_refclk", DIV_FSYS2, 16, 8),

	/* UART and PWM */
	DIV(0, "dout_uart0", "mout_uart0", DIV_PERIC0, 8, 4),
	DIV(0, "dout_uart1", "mout_uart1", DIV_PERIC0, 12, 4),
	DIV(0, "dout_uart2", "mout_uart2", DIV_PERIC0, 16, 4),
	DIV(0, "dout_uart3", "mout_uart3", DIV_PERIC0, 20, 4),
	DIV(0, "dout_pwm", "mout_pwm", DIV_PERIC0, 28, 4),

	/* SPI */
	DIV(0, "dout_spi0", "mout_spi0", DIV_PERIC1, 20, 4),
	DIV(0, "dout_spi1", "mout_spi1", DIV_PERIC1, 24, 4),
	DIV(0, "dout_spi2", "mout_spi2", DIV_PERIC1, 28, 4),


	/* PCM */
	DIV(0, "dout_pcm1", "dout_audio1", DIV_PERIC2, 16, 8),
	DIV(0, "dout_pcm2", "dout_audio2", DIV_PERIC2, 24, 8),

	/* Audio - I2S */
	DIV(0, "dout_i2s1", "dout_audio1", DIV_PERIC3, 6, 6),
	DIV(0, "dout_i2s2", "dout_audio2", DIV_PERIC3, 12, 6),
	DIV(0, "dout_audio0", "mout_audio0", DIV_PERIC3, 20, 4),
	DIV(0, "dout_audio1", "mout_audio1", DIV_PERIC3, 24, 4),
	DIV(0, "dout_audio2", "mout_audio2", DIV_PERIC3, 28, 4),

	/* SPI Pre-Ratio */
	DIV(0, "dout_spi0_pre", "dout_spi0", DIV_PERIC4, 8, 8),
	DIV(0, "dout_spi1_pre", "dout_spi1", DIV_PERIC4, 16, 8),
	DIV(0, "dout_spi2_pre", "dout_spi2", DIV_PERIC4, 24, 8),

	/* GSCL Block */
	DIV(0, "dout_gscl_blk_333", "aclk333_432_gscl", DIV2_RATIO0, 6, 2),

	/* PSGEN */
	DIV(0, "dout_gen_blk", "mout_user_aclk266", DIV2_RATIO0, 8, 1),
	DIV(0, "dout_jpg_blk", "aclk166", DIV2_RATIO0, 20, 1),

	/* ISP Block */
	DIV(0, "dout_isp_sensor0", "mout_isp_sensor", SCLK_DIV_ISP0, 8, 8),
	DIV(0, "dout_isp_sensor1", "mout_isp_sensor", SCLK_DIV_ISP0, 16, 8),
	DIV(0, "dout_isp_sensor2", "mout_isp_sensor", SCLK_DIV_ISP0, 24, 8),
	DIV(0, "dout_pwm_isp", "mout_pwm_isp", SCLK_DIV_ISP1, 28, 4),
	DIV(0, "dout_uart_isp", "mout_uart_isp", SCLK_DIV_ISP1, 24, 4),
	DIV(0, "dout_spi0_isp", "mout_spi0_isp", SCLK_DIV_ISP1, 16, 4),
	DIV(0, "dout_spi1_isp", "mout_spi1_isp", SCLK_DIV_ISP1, 20, 4),
	DIV_F(0, "dout_spi0_isp_pre", "dout_spi0_isp", SCLK_DIV_ISP1, 0, 8,
			CLK_SET_RATE_PARENT, 0),
	DIV_F(0, "dout_spi1_isp_pre", "dout_spi1_isp", SCLK_DIV_ISP1, 8, 8,
			CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_gate_clock exynos5x_gate_clks[] __initconst = {
	/* G2D */
	GATE(CLK_MDMA0, "mdma0", "aclk266_g2d", GATE_IP_G2D, 1, 0, 0),
	GATE(CLK_SSS, "sss", "aclk266_g2d", GATE_IP_G2D, 2, 0, 0),
	GATE(CLK_G2D, "g2d", "aclk333_g2d", GATE_IP_G2D, 3, 0, 0),
	GATE(CLK_SMMU_MDMA0, "smmu_mdma0", "aclk266_g2d", GATE_IP_G2D, 5, 0, 0),
	GATE(CLK_SMMU_G2D, "smmu_g2d", "aclk333_g2d", GATE_IP_G2D, 7, 0, 0),

	GATE(0, "aclk200_fsys", "mout_user_aclk200_fsys",
			GATE_BUS_FSYS0, 9, CLK_IS_CRITICAL, 0),
	GATE(0, "aclk200_fsys2", "mout_user_aclk200_fsys2",
			GATE_BUS_FSYS0, 10, CLK_IGNORE_UNUSED, 0),

	GATE(0, "aclk333_g2d", "mout_user_aclk333_g2d",
			GATE_BUS_TOP, 0, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk266_g2d", "mout_user_aclk266_g2d",
			GATE_BUS_TOP, 1, CLK_IS_CRITICAL, 0),
	GATE(0, "aclk300_jpeg", "mout_user_aclk300_jpeg",
			GATE_BUS_TOP, 4, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk333_432_isp0", "mout_user_aclk333_432_isp0",
			GATE_BUS_TOP, 5, CLK_IS_CRITICAL, 0),
	GATE(0, "aclk300_gscl", "mout_user_aclk300_gscl",
			GATE_BUS_TOP, 6, CLK_IS_CRITICAL, 0),
	GATE(0, "aclk333_432_gscl", "mout_user_aclk333_432_gscl",
			GATE_BUS_TOP, 7, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk333_432_isp", "mout_user_aclk333_432_isp",
			GATE_BUS_TOP, 8, CLK_IS_CRITICAL, 0),
	GATE(CLK_PCLK66_GPIO, "pclk66_gpio", "mout_user_pclk66_gpio",
			GATE_BUS_TOP, 9, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk66_psgen", "mout_user_aclk66_psgen",
			GATE_BUS_TOP, 10, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk266_isp", "mout_user_aclk266_isp",
			GATE_BUS_TOP, 13, CLK_IS_CRITICAL, 0),
	GATE(0, "aclk166", "mout_user_aclk166",
			GATE_BUS_TOP, 14, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK333, "aclk333", "mout_user_aclk333",
			GATE_BUS_TOP, 15, CLK_IS_CRITICAL, 0),
	GATE(0, "aclk400_isp", "mout_user_aclk400_isp",
			GATE_BUS_TOP, 16, CLK_IS_CRITICAL, 0),
	GATE(0, "aclk400_mscl", "mout_user_aclk400_mscl",
			GATE_BUS_TOP, 17, CLK_IS_CRITICAL, 0),
	GATE(0, "aclk200_disp1", "mout_user_aclk200_disp1",
			GATE_BUS_TOP, 18, CLK_IS_CRITICAL, 0),
	GATE(CLK_SCLK_MPHY_IXTAL24, "sclk_mphy_ixtal24", "mphy_refclk_ixtal24",
			GATE_BUS_TOP, 28, 0, 0),
	GATE(CLK_SCLK_HSIC_12M, "sclk_hsic_12m", "ff_hsic_12m",
			GATE_BUS_TOP, 29, 0, 0),

	GATE(0, "aclk300_disp1", "mout_user_aclk300_disp1",
			SRC_MASK_TOP2, 24, CLK_IS_CRITICAL, 0),

	/* sclk */
	GATE(CLK_SCLK_UART0, "sclk_uart0", "dout_uart0",
		GATE_TOP_SCLK_PERIC, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART1, "sclk_uart1", "dout_uart1",
		GATE_TOP_SCLK_PERIC, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART2, "sclk_uart2", "dout_uart2",
		GATE_TOP_SCLK_PERIC, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART3, "sclk_uart3", "dout_uart3",
		GATE_TOP_SCLK_PERIC, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0, "sclk_spi0", "dout_spi0_pre",
		GATE_TOP_SCLK_PERIC, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI1, "sclk_spi1", "dout_spi1_pre",
		GATE_TOP_SCLK_PERIC, 7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI2, "sclk_spi2", "dout_spi2_pre",
		GATE_TOP_SCLK_PERIC, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPDIF, "sclk_spdif", "mout_spdif",
		GATE_TOP_SCLK_PERIC, 9, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PWM, "sclk_pwm", "dout_pwm",
		GATE_TOP_SCLK_PERIC, 11, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PCM1, "sclk_pcm1", "dout_pcm1",
		GATE_TOP_SCLK_PERIC, 15, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PCM2, "sclk_pcm2", "dout_pcm2",
		GATE_TOP_SCLK_PERIC, 16, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_I2S1, "sclk_i2s1", "dout_i2s1",
		GATE_TOP_SCLK_PERIC, 17, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_I2S2, "sclk_i2s2", "dout_i2s2",
		GATE_TOP_SCLK_PERIC, 18, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_SCLK_MMC0, "sclk_mmc0", "dout_mmc0",
		GATE_TOP_SCLK_FSYS, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC1, "sclk_mmc1", "dout_mmc1",
		GATE_TOP_SCLK_FSYS, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC2, "sclk_mmc2", "dout_mmc2",
		GATE_TOP_SCLK_FSYS, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_USBPHY301, "sclk_usbphy301", "dout_usbphy301",
		GATE_TOP_SCLK_FSYS, 7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_USBPHY300, "sclk_usbphy300", "dout_usbphy300",
		GATE_TOP_SCLK_FSYS, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_USBD300, "sclk_usbd300", "dout_usbd300",
		GATE_TOP_SCLK_FSYS, 9, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_USBD301, "sclk_usbd301", "dout_usbd301",
		GATE_TOP_SCLK_FSYS, 10, CLK_SET_RATE_PARENT, 0),

	/* Display */
	GATE(CLK_SCLK_FIMD1, "sclk_fimd1", "dout_fimd1",
			GATE_TOP_SCLK_DISP1, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MIPI1, "sclk_mipi1", "dout_mipi1",
			GATE_TOP_SCLK_DISP1, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_HDMI, "sclk_hdmi", "mout_hdmi",
			GATE_TOP_SCLK_DISP1, 9, 0, 0),
	GATE(CLK_SCLK_PIXEL, "sclk_pixel", "dout_hdmi_pixel",
			GATE_TOP_SCLK_DISP1, 10, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_DP1, "sclk_dp1", "dout_dp1",
			GATE_TOP_SCLK_DISP1, 20, CLK_SET_RATE_PARENT, 0),

	/* FSYS Block */
	GATE(CLK_TSI, "tsi", "aclk200_fsys", GATE_BUS_FSYS0, 0, 0, 0),
	GATE(CLK_PDMA0, "pdma0", "aclk200_fsys", GATE_BUS_FSYS0, 1, 0, 0),
	GATE(CLK_PDMA1, "pdma1", "aclk200_fsys", GATE_BUS_FSYS0, 2, 0, 0),
	GATE(CLK_UFS, "ufs", "aclk200_fsys2", GATE_BUS_FSYS0, 3, 0, 0),
	GATE(CLK_RTIC, "rtic", "aclk200_fsys", GATE_IP_FSYS, 9, 0, 0),
	GATE(CLK_MMC0, "mmc0", "aclk200_fsys2", GATE_IP_FSYS, 12, 0, 0),
	GATE(CLK_MMC1, "mmc1", "aclk200_fsys2", GATE_IP_FSYS, 13, 0, 0),
	GATE(CLK_MMC2, "mmc2", "aclk200_fsys2", GATE_IP_FSYS, 14, 0, 0),
	GATE(CLK_SROMC, "sromc", "aclk200_fsys2",
			GATE_IP_FSYS, 17, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_USBH20, "usbh20", "aclk200_fsys", GATE_IP_FSYS, 18, 0, 0),
	GATE(CLK_USBD300, "usbd300", "aclk200_fsys", GATE_IP_FSYS, 19, 0, 0),
	GATE(CLK_USBD301, "usbd301", "aclk200_fsys", GATE_IP_FSYS, 20, 0, 0),
	GATE(CLK_SCLK_UNIPRO, "sclk_unipro", "dout_unipro",
			SRC_MASK_FSYS, 24, CLK_SET_RATE_PARENT, 0),

	/* PERIC Block */
	GATE(CLK_UART0, "uart0", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 0, 0, 0),
	GATE(CLK_UART1, "uart1", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 1, 0, 0),
	GATE(CLK_UART2, "uart2", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 2, 0, 0),
	GATE(CLK_UART3, "uart3", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 3, 0, 0),
	GATE(CLK_I2C0, "i2c0", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 6, 0, 0),
	GATE(CLK_I2C1, "i2c1", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 7, 0, 0),
	GATE(CLK_I2C2, "i2c2", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 8, 0, 0),
	GATE(CLK_I2C3, "i2c3", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 9, 0, 0),
	GATE(CLK_USI0, "usi0", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 10, 0, 0),
	GATE(CLK_USI1, "usi1", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 11, 0, 0),
	GATE(CLK_USI2, "usi2", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 12, 0, 0),
	GATE(CLK_USI3, "usi3", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 13, 0, 0),
	GATE(CLK_I2C_HDMI, "i2c_hdmi", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 14, 0, 0),
	GATE(CLK_TSADC, "tsadc", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 15, 0, 0),
	GATE(CLK_SPI0, "spi0", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 16, 0, 0),
	GATE(CLK_SPI1, "spi1", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 17, 0, 0),
	GATE(CLK_SPI2, "spi2", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 18, 0, 0),
	GATE(CLK_I2S1, "i2s1", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 20, 0, 0),
	GATE(CLK_I2S2, "i2s2", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 21, 0, 0),
	GATE(CLK_PCM1, "pcm1", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 22, 0, 0),
	GATE(CLK_PCM2, "pcm2", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 23, 0, 0),
	GATE(CLK_PWM, "pwm", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 24, 0, 0),
	GATE(CLK_SPDIF, "spdif", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 26, 0, 0),
	GATE(CLK_USI4, "usi4", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 28, 0, 0),
	GATE(CLK_USI5, "usi5", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 30, 0, 0),
	GATE(CLK_USI6, "usi6", "mout_user_aclk66_peric",
			GATE_IP_PERIC, 31, 0, 0),

	GATE(CLK_KEYIF, "keyif", "mout_user_aclk66_peric",
			GATE_BUS_PERIC, 22, 0, 0),

	/* PERIS Block */
	GATE(CLK_CHIPID, "chipid", "aclk66_psgen",
			GATE_IP_PERIS, 0, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SYSREG, "sysreg", "aclk66_psgen",
			GATE_IP_PERIS, 1, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_TZPC0, "tzpc0", "aclk66_psgen", GATE_IP_PERIS, 6, 0, 0),
	GATE(CLK_TZPC1, "tzpc1", "aclk66_psgen", GATE_IP_PERIS, 7, 0, 0),
	GATE(CLK_TZPC2, "tzpc2", "aclk66_psgen", GATE_IP_PERIS, 8, 0, 0),
	GATE(CLK_TZPC3, "tzpc3", "aclk66_psgen", GATE_IP_PERIS, 9, 0, 0),
	GATE(CLK_TZPC4, "tzpc4", "aclk66_psgen", GATE_IP_PERIS, 10, 0, 0),
	GATE(CLK_TZPC5, "tzpc5", "aclk66_psgen", GATE_IP_PERIS, 11, 0, 0),
	GATE(CLK_TZPC6, "tzpc6", "aclk66_psgen", GATE_IP_PERIS, 12, 0, 0),
	GATE(CLK_TZPC7, "tzpc7", "aclk66_psgen", GATE_IP_PERIS, 13, 0, 0),
	GATE(CLK_TZPC8, "tzpc8", "aclk66_psgen", GATE_IP_PERIS, 14, 0, 0),
	GATE(CLK_TZPC9, "tzpc9", "aclk66_psgen", GATE_IP_PERIS, 15, 0, 0),
	GATE(CLK_HDMI_CEC, "hdmi_cec", "aclk66_psgen", GATE_IP_PERIS, 16, 0, 0),
	GATE(CLK_MCT, "mct", "aclk66_psgen", GATE_IP_PERIS, 18, 0, 0),
	GATE(CLK_WDT, "wdt", "aclk66_psgen", GATE_IP_PERIS, 19, 0, 0),
	GATE(CLK_RTC, "rtc", "aclk66_psgen", GATE_IP_PERIS, 20, 0, 0),
	GATE(CLK_TMU, "tmu", "aclk66_psgen", GATE_IP_PERIS, 21, 0, 0),
	GATE(CLK_TMU_GPU, "tmu_gpu", "aclk66_psgen", GATE_IP_PERIS, 22, 0, 0),

	/* GEN Block */
	GATE(CLK_ROTATOR, "rotator", "mout_user_aclk266", GATE_IP_GEN, 1, 0, 0),
	GATE(CLK_JPEG, "jpeg", "aclk300_jpeg", GATE_IP_GEN, 2, 0, 0),
	GATE(CLK_JPEG2, "jpeg2", "aclk300_jpeg", GATE_IP_GEN, 3, 0, 0),
	GATE(CLK_MDMA1, "mdma1", "mout_user_aclk266", GATE_IP_GEN, 4, 0, 0),
	GATE(CLK_TOP_RTC, "top_rtc", "aclk66_psgen", GATE_IP_GEN, 5, 0, 0),
	GATE(CLK_SMMU_ROTATOR, "smmu_rotator", "dout_gen_blk",
			GATE_IP_GEN, 6, 0, 0),
	GATE(CLK_SMMU_JPEG, "smmu_jpeg", "dout_jpg_blk", GATE_IP_GEN, 7, 0, 0),
	GATE(CLK_SMMU_MDMA1, "smmu_mdma1", "dout_gen_blk",
			GATE_IP_GEN, 9, 0, 0),

	/* GATE_IP_GEN doesn't list gates for smmu_jpeg2 and mc */
	GATE(CLK_SMMU_JPEG2, "smmu_jpeg2", "dout_jpg_blk",
			GATE_BUS_GEN, 28, 0, 0),
	GATE(CLK_MC, "mc", "aclk66_psgen", GATE_BUS_GEN, 12, 0, 0),

	/* GSCL Block */
	GATE(CLK_SCLK_GSCL_WA, "sclk_gscl_wa", "mout_user_aclk333_432_gscl",
			GATE_TOP_SCLK_GSCL, 6, 0, 0),
	GATE(CLK_SCLK_GSCL_WB, "sclk_gscl_wb", "mout_user_aclk333_432_gscl",
			GATE_TOP_SCLK_GSCL, 7, 0, 0),

	GATE(CLK_FIMC_3AA, "fimc_3aa", "aclk333_432_gscl",
			GATE_IP_GSCL0, 4, 0, 0),
	GATE(CLK_FIMC_LITE0, "fimc_lite0", "aclk333_432_gscl",
			GATE_IP_GSCL0, 5, 0, 0),
	GATE(CLK_FIMC_LITE1, "fimc_lite1", "aclk333_432_gscl",
			GATE_IP_GSCL0, 6, 0, 0),

	GATE(CLK_SMMU_3AA, "smmu_3aa", "dout_gscl_blk_333",
			GATE_IP_GSCL1, 2, 0, 0),
	GATE(CLK_SMMU_FIMCL0, "smmu_fimcl0", "dout_gscl_blk_333",
			GATE_IP_GSCL1, 3, 0, 0),
	GATE(CLK_SMMU_FIMCL1, "smmu_fimcl1", "dout_gscl_blk_333",
			GATE_IP_GSCL1, 4, 0, 0),
	GATE(CLK_GSCL_WA, "gscl_wa", "sclk_gscl_wa", GATE_IP_GSCL1, 12,
			CLK_IS_CRITICAL, 0),
	GATE(CLK_GSCL_WB, "gscl_wb", "sclk_gscl_wb", GATE_IP_GSCL1, 13,
			CLK_IS_CRITICAL, 0),
	GATE(CLK_SMMU_FIMCL3, "smmu_fimcl3", "dout_gscl_blk_333",
			GATE_IP_GSCL1, 16, 0, 0),
	GATE(CLK_FIMC_LITE3, "fimc_lite3", "aclk333_432_gscl",
			GATE_IP_GSCL1, 17, 0, 0),

	/* ISP */
	GATE(CLK_SCLK_UART_ISP, "sclk_uart_isp", "dout_uart_isp",
			GATE_TOP_SCLK_ISP, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0_ISP, "sclk_spi0_isp", "dout_spi0_isp_pre",
			GATE_TOP_SCLK_ISP, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI1_ISP, "sclk_spi1_isp", "dout_spi1_isp_pre",
			GATE_TOP_SCLK_ISP, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PWM_ISP, "sclk_pwm_isp", "dout_pwm_isp",
			GATE_TOP_SCLK_ISP, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_ISP_SENSOR0, "sclk_isp_sensor0", "dout_isp_sensor0",
			GATE_TOP_SCLK_ISP, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_ISP_SENSOR1, "sclk_isp_sensor1", "dout_isp_sensor1",
			GATE_TOP_SCLK_ISP, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_ISP_SENSOR2, "sclk_isp_sensor2", "dout_isp_sensor2",
			GATE_TOP_SCLK_ISP, 12, CLK_SET_RATE_PARENT, 0),

	/* CDREX */
	GATE(CLK_CLKM_PHY0, "clkm_phy0", "dout_sclk_cdrex",
			GATE_BUS_CDREX0, 0, 0, 0),
	GATE(CLK_CLKM_PHY1, "clkm_phy1", "dout_sclk_cdrex",
			GATE_BUS_CDREX0, 1, 0, 0),
	GATE(0, "mx_mspll_ccore_phy", "mout_mx_mspll_ccore_phy",
			SRC_MASK_TOP7, 0, CLK_IGNORE_UNUSED, 0),

	GATE(CLK_ACLK_PPMU_DREX1_1, "aclk_ppmu_drex1_1", "dout_aclk_cdrex1",
			GATE_BUS_CDREX1, 12, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PPMU_DREX1_0, "aclk_ppmu_drex1_0", "dout_aclk_cdrex1",
			GATE_BUS_CDREX1, 13, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PPMU_DREX0_1, "aclk_ppmu_drex0_1", "dout_aclk_cdrex1",
			GATE_BUS_CDREX1, 14, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PPMU_DREX0_0, "aclk_ppmu_drex0_0", "dout_aclk_cdrex1",
			GATE_BUS_CDREX1, 15, CLK_IGNORE_UNUSED, 0),

	GATE(CLK_PCLK_PPMU_DREX1_1, "pclk_ppmu_drex1_1", "dout_pclk_cdrex",
			GATE_BUS_CDREX1, 26, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PPMU_DREX1_0, "pclk_ppmu_drex1_0", "dout_pclk_cdrex",
			GATE_BUS_CDREX1, 27, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PPMU_DREX0_1, "pclk_ppmu_drex0_1", "dout_pclk_cdrex",
			GATE_BUS_CDREX1, 28, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PPMU_DREX0_0, "pclk_ppmu_drex0_0", "dout_pclk_cdrex",
			GATE_BUS_CDREX1, 29, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_div_clock exynos5x_disp_div_clks[] __initconst = {
	DIV(0, "dout_disp1_blk", "aclk200_disp1", DIV2_RATIO0, 16, 2),
};

static const struct samsung_gate_clock exynos5x_disp_gate_clks[] __initconst = {
	GATE(CLK_FIMD1, "fimd1", "aclk300_disp1", GATE_IP_DISP1, 0, 0, 0),
	GATE(CLK_DSIM1, "dsim1", "aclk200_disp1", GATE_IP_DISP1, 3, 0, 0),
	GATE(CLK_DP1, "dp1", "aclk200_disp1", GATE_IP_DISP1, 4, 0, 0),
	GATE(CLK_MIXER, "mixer", "aclk200_disp1", GATE_IP_DISP1, 5, 0, 0),
	GATE(CLK_HDMI, "hdmi", "aclk200_disp1", GATE_IP_DISP1, 6, 0, 0),
	GATE(CLK_SMMU_FIMD1M0, "smmu_fimd1m0", "dout_disp1_blk",
			GATE_IP_DISP1, 7, 0, 0),
	GATE(CLK_SMMU_FIMD1M1, "smmu_fimd1m1", "dout_disp1_blk",
			GATE_IP_DISP1, 8, 0, 0),
	GATE(CLK_SMMU_MIXER, "smmu_mixer", "aclk200_disp1",
			GATE_IP_DISP1, 9, 0, 0),
};

static struct exynos5_subcmu_reg_dump exynos5x_disp_suspend_regs[] = {
	{ GATE_IP_DISP1, 0xffffffff, 0xffffffff }, /* DISP1 gates */
	{ SRC_TOP5, 0, BIT(0) },	/* MUX mout_user_aclk400_disp1 */
	{ SRC_TOP5, 0, BIT(24) },	/* MUX mout_user_aclk300_disp1 */
	{ SRC_TOP3, 0, BIT(8) },	/* MUX mout_user_aclk200_disp1 */
	{ DIV2_RATIO0, 0, 0x30000 },		/* DIV dout_disp1_blk */
};

static const struct samsung_div_clock exynos5x_gsc_div_clks[] __initconst = {
	DIV(0, "dout_gscl_blk_300", "mout_user_aclk300_gscl",
			DIV2_RATIO0, 4, 2),
};

static const struct samsung_gate_clock exynos5x_gsc_gate_clks[] __initconst = {
	GATE(CLK_GSCL0, "gscl0", "aclk300_gscl", GATE_IP_GSCL0, 0, 0, 0),
	GATE(CLK_GSCL1, "gscl1", "aclk300_gscl", GATE_IP_GSCL0, 1, 0, 0),
	GATE(CLK_SMMU_GSCL0, "smmu_gscl0", "dout_gscl_blk_300",
			GATE_IP_GSCL1, 6, 0, 0),
	GATE(CLK_SMMU_GSCL1, "smmu_gscl1", "dout_gscl_blk_300",
			GATE_IP_GSCL1, 7, 0, 0),
};

static struct exynos5_subcmu_reg_dump exynos5x_gsc_suspend_regs[] = {
	{ GATE_IP_GSCL0, 0x3, 0x3 },	/* GSC gates */
	{ GATE_IP_GSCL1, 0xc0, 0xc0 },	/* GSC gates */
	{ SRC_TOP5, 0, BIT(28) },	/* MUX mout_user_aclk300_gscl */
	{ DIV2_RATIO0, 0, 0x30 },	/* DIV dout_gscl_blk_300 */
};

static const struct samsung_gate_clock exynos5x_g3d_gate_clks[] __initconst = {
	GATE(CLK_G3D, "g3d", "mout_user_aclk_g3d", GATE_IP_G3D, 9,
	     CLK_SET_RATE_PARENT, 0),
};

static struct exynos5_subcmu_reg_dump exynos5x_g3d_suspend_regs[] = {
	{ GATE_IP_G3D, 0x3ff, 0x3ff },	/* G3D gates */
	{ SRC_TOP5, 0, BIT(16) },	/* MUX mout_user_aclk_g3d */
};

static const struct samsung_div_clock exynos5x_mfc_div_clks[] __initconst = {
	DIV(0, "dout_mfc_blk", "mout_user_aclk333", DIV4_RATIO, 0, 2),
};

static const struct samsung_gate_clock exynos5x_mfc_gate_clks[] __initconst = {
	GATE(CLK_MFC, "mfc", "aclk333", GATE_IP_MFC, 0, 0, 0),
	GATE(CLK_SMMU_MFCL, "smmu_mfcl", "dout_mfc_blk", GATE_IP_MFC, 1, 0, 0),
	GATE(CLK_SMMU_MFCR, "smmu_mfcr", "dout_mfc_blk", GATE_IP_MFC, 2, 0, 0),
};

static struct exynos5_subcmu_reg_dump exynos5x_mfc_suspend_regs[] = {
	{ GATE_IP_MFC, 0xffffffff, 0xffffffff }, /* MFC gates */
	{ SRC_TOP4, 0, BIT(28) },		/* MUX mout_user_aclk333 */
	{ DIV4_RATIO, 0, 0x3 },			/* DIV dout_mfc_blk */
};

static const struct samsung_gate_clock exynos5x_mscl_gate_clks[] __initconst = {
	/* MSCL Block */
	GATE(CLK_MSCL0, "mscl0", "aclk400_mscl", GATE_IP_MSCL, 0, 0, 0),
	GATE(CLK_MSCL1, "mscl1", "aclk400_mscl", GATE_IP_MSCL, 1, 0, 0),
	GATE(CLK_MSCL2, "mscl2", "aclk400_mscl", GATE_IP_MSCL, 2, 0, 0),
	GATE(CLK_SMMU_MSCL0, "smmu_mscl0", "dout_mscl_blk",
			GATE_IP_MSCL, 8, 0, 0),
	GATE(CLK_SMMU_MSCL1, "smmu_mscl1", "dout_mscl_blk",
			GATE_IP_MSCL, 9, 0, 0),
	GATE(CLK_SMMU_MSCL2, "smmu_mscl2", "dout_mscl_blk",
			GATE_IP_MSCL, 10, 0, 0),
};

static const struct samsung_div_clock exynos5x_mscl_div_clks[] __initconst = {
	DIV(0, "dout_mscl_blk", "aclk400_mscl", DIV2_RATIO0, 28, 2),
};

static struct exynos5_subcmu_reg_dump exynos5x_mscl_suspend_regs[] = {
	{ GATE_IP_MSCL, 0xffffffff, 0xffffffff }, /* MSCL gates */
	{ SRC_TOP3, 0, BIT(4) },		/* MUX mout_user_aclk400_mscl */
	{ DIV2_RATIO0, 0, 0x30000000 },		/* DIV dout_mscl_blk */
};

static const struct samsung_gate_clock exynos5800_mau_gate_clks[] __initconst = {
	GATE(CLK_MAU_EPLL, "mau_epll", "mout_user_mau_epll",
			SRC_MASK_TOP7, 20, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MAUDIO0, "sclk_maudio0", "dout_maudio0",
		GATE_TOP_SCLK_MAU, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MAUPCM0, "sclk_maupcm0", "dout_maupcm0",
		GATE_TOP_SCLK_MAU, 1, CLK_SET_RATE_PARENT, 0),
};

static struct exynos5_subcmu_reg_dump exynos5800_mau_suspend_regs[] = {
	{ SRC_TOP9, 0, BIT(8) },	/* MUX mout_user_mau_epll */
};

static const struct exynos5_subcmu_info exynos5x_disp_subcmu = {
	.div_clks	= exynos5x_disp_div_clks,
	.nr_div_clks	= ARRAY_SIZE(exynos5x_disp_div_clks),
	.gate_clks	= exynos5x_disp_gate_clks,
	.nr_gate_clks	= ARRAY_SIZE(exynos5x_disp_gate_clks),
	.suspend_regs	= exynos5x_disp_suspend_regs,
	.nr_suspend_regs = ARRAY_SIZE(exynos5x_disp_suspend_regs),
	.pd_name	= "DISP",
};

static const struct exynos5_subcmu_info exynos5x_gsc_subcmu = {
	.div_clks	= exynos5x_gsc_div_clks,
	.nr_div_clks	= ARRAY_SIZE(exynos5x_gsc_div_clks),
	.gate_clks	= exynos5x_gsc_gate_clks,
	.nr_gate_clks	= ARRAY_SIZE(exynos5x_gsc_gate_clks),
	.suspend_regs	= exynos5x_gsc_suspend_regs,
	.nr_suspend_regs = ARRAY_SIZE(exynos5x_gsc_suspend_regs),
	.pd_name	= "GSC",
};

static const struct exynos5_subcmu_info exynos5x_g3d_subcmu = {
	.gate_clks	= exynos5x_g3d_gate_clks,
	.nr_gate_clks	= ARRAY_SIZE(exynos5x_g3d_gate_clks),
	.suspend_regs	= exynos5x_g3d_suspend_regs,
	.nr_suspend_regs = ARRAY_SIZE(exynos5x_g3d_suspend_regs),
	.pd_name	= "G3D",
};

static const struct exynos5_subcmu_info exynos5x_mfc_subcmu = {
	.div_clks	= exynos5x_mfc_div_clks,
	.nr_div_clks	= ARRAY_SIZE(exynos5x_mfc_div_clks),
	.gate_clks	= exynos5x_mfc_gate_clks,
	.nr_gate_clks	= ARRAY_SIZE(exynos5x_mfc_gate_clks),
	.suspend_regs	= exynos5x_mfc_suspend_regs,
	.nr_suspend_regs = ARRAY_SIZE(exynos5x_mfc_suspend_regs),
	.pd_name	= "MFC",
};

static const struct exynos5_subcmu_info exynos5x_mscl_subcmu = {
	.div_clks	= exynos5x_mscl_div_clks,
	.nr_div_clks	= ARRAY_SIZE(exynos5x_mscl_div_clks),
	.gate_clks	= exynos5x_mscl_gate_clks,
	.nr_gate_clks	= ARRAY_SIZE(exynos5x_mscl_gate_clks),
	.suspend_regs	= exynos5x_mscl_suspend_regs,
	.nr_suspend_regs = ARRAY_SIZE(exynos5x_mscl_suspend_regs),
	.pd_name	= "MSC",
};

static const struct exynos5_subcmu_info exynos5800_mau_subcmu = {
	.gate_clks	= exynos5800_mau_gate_clks,
	.nr_gate_clks	= ARRAY_SIZE(exynos5800_mau_gate_clks),
	.suspend_regs	= exynos5800_mau_suspend_regs,
	.nr_suspend_regs = ARRAY_SIZE(exynos5800_mau_suspend_regs),
	.pd_name	= "MAU",
};

static const struct exynos5_subcmu_info *exynos5x_subcmus[] = {
	&exynos5x_disp_subcmu,
	&exynos5x_gsc_subcmu,
	&exynos5x_g3d_subcmu,
	&exynos5x_mfc_subcmu,
	&exynos5x_mscl_subcmu,
};

static const struct exynos5_subcmu_info *exynos5800_subcmus[] = {
	&exynos5x_disp_subcmu,
	&exynos5x_gsc_subcmu,
	&exynos5x_g3d_subcmu,
	&exynos5x_mfc_subcmu,
	&exynos5x_mscl_subcmu,
	&exynos5800_mau_subcmu,
};

static const struct samsung_pll_rate_table exynos5420_pll2550x_24mhz_tbl[] __initconst = {
	PLL_35XX_RATE(24 * MHZ, 2000000000, 250, 3, 0),
	PLL_35XX_RATE(24 * MHZ, 1900000000, 475, 6, 0),
	PLL_35XX_RATE(24 * MHZ, 1800000000, 225, 3, 0),
	PLL_35XX_RATE(24 * MHZ, 1700000000, 425, 6, 0),
	PLL_35XX_RATE(24 * MHZ, 1600000000, 200, 3, 0),
	PLL_35XX_RATE(24 * MHZ, 1500000000, 250, 4, 0),
	PLL_35XX_RATE(24 * MHZ, 1400000000, 175, 3, 0),
	PLL_35XX_RATE(24 * MHZ, 1300000000, 325, 6, 0),
	PLL_35XX_RATE(24 * MHZ, 1200000000, 200, 2, 1),
	PLL_35XX_RATE(24 * MHZ, 1100000000, 275, 3, 1),
	PLL_35XX_RATE(24 * MHZ, 1000000000, 250, 3, 1),
	PLL_35XX_RATE(24 * MHZ, 900000000,  150, 2, 1),
	PLL_35XX_RATE(24 * MHZ, 800000000,  200, 3, 1),
	PLL_35XX_RATE(24 * MHZ, 700000000,  175, 3, 1),
	PLL_35XX_RATE(24 * MHZ, 600000000,  200, 2, 2),
	PLL_35XX_RATE(24 * MHZ, 500000000,  250, 3, 2),
	PLL_35XX_RATE(24 * MHZ, 400000000,  200, 3, 2),
	PLL_35XX_RATE(24 * MHZ, 300000000,  200, 2, 3),
	PLL_35XX_RATE(24 * MHZ, 200000000,  200, 3, 3),
};

static const struct samsung_pll_rate_table exynos5422_bpll_rate_table[] = {
	PLL_35XX_RATE(24 * MHZ, 825000000, 275, 4, 1),
	PLL_35XX_RATE(24 * MHZ, 728000000, 182, 3, 1),
	PLL_35XX_RATE(24 * MHZ, 633000000, 211, 4, 1),
	PLL_35XX_RATE(24 * MHZ, 543000000, 181, 2, 2),
	PLL_35XX_RATE(24 * MHZ, 413000000, 413, 6, 2),
	PLL_35XX_RATE(24 * MHZ, 275000000, 275, 3, 3),
	PLL_35XX_RATE(24 * MHZ, 206000000, 206, 3, 3),
	PLL_35XX_RATE(24 * MHZ, 165000000, 110, 2, 3),
};

static const struct samsung_pll_rate_table exynos5420_epll_24mhz_tbl[] = {
	PLL_36XX_RATE(24 * MHZ, 600000000U, 100, 2, 1, 0),
	PLL_36XX_RATE(24 * MHZ, 400000000U, 200, 3, 2, 0),
	PLL_36XX_RATE(24 * MHZ, 393216003U, 197, 3, 2, -25690),
	PLL_36XX_RATE(24 * MHZ, 361267218U, 301, 5, 2, 3671),
	PLL_36XX_RATE(24 * MHZ, 200000000U, 200, 3, 3, 0),
	PLL_36XX_RATE(24 * MHZ, 196608001U, 197, 3, 3, -25690),
	PLL_36XX_RATE(24 * MHZ, 180633609U, 301, 5, 3, 3671),
	PLL_36XX_RATE(24 * MHZ, 131072006U, 131, 3, 3, 4719),
	PLL_36XX_RATE(24 * MHZ, 100000000U, 200, 3, 4, 0),
	PLL_36XX_RATE(24 * MHZ,  73728000U, 98, 2, 4, 19923),
	PLL_36XX_RATE(24 * MHZ,  67737602U, 90, 2, 4, 20762),
	PLL_36XX_RATE(24 * MHZ,  65536003U, 131, 3, 4, 4719),
	PLL_36XX_RATE(24 * MHZ,  49152000U, 197, 3, 5, -25690),
	PLL_36XX_RATE(24 * MHZ,  45158401U, 90, 3, 4, 20762),
	PLL_36XX_RATE(24 * MHZ,  32768001U, 131, 3, 5, 4719),
};

static const struct samsung_pll_rate_table exynos5420_vpll_24mhz_tbl[] = {
	PLL_35XX_RATE(24 * MHZ, 600000000U,  200, 2, 2),
	PLL_35XX_RATE(24 * MHZ, 543000000U,  181, 2, 2),
	PLL_35XX_RATE(24 * MHZ, 480000000U,  160, 2, 2),
	PLL_35XX_RATE(24 * MHZ, 420000000U,  140, 2, 2),
	PLL_35XX_RATE(24 * MHZ, 350000000U,  175, 3, 2),
	PLL_35XX_RATE(24 * MHZ, 266000000U,  266, 3, 3),
	PLL_35XX_RATE(24 * MHZ, 177000000U,  118, 2, 3),
	PLL_35XX_RATE(24 * MHZ, 100000000U,  200, 3, 4),
};

static struct samsung_pll_clock exynos5x_plls[nr_plls] __initdata = {
	[apll] = PLL(pll_2550, CLK_FOUT_APLL, "fout_apll", "fin_pll", APLL_LOCK,
		APLL_CON0, NULL),
	[cpll] = PLL(pll_2550, CLK_FOUT_CPLL, "fout_cpll", "fin_pll", CPLL_LOCK,
		CPLL_CON0, NULL),
	[dpll] = PLL(pll_2550, CLK_FOUT_DPLL, "fout_dpll", "fin_pll", DPLL_LOCK,
		DPLL_CON0, NULL),
	[epll] = PLL(pll_36xx, CLK_FOUT_EPLL, "fout_epll", "fin_pll", EPLL_LOCK,
		EPLL_CON0, NULL),
	[rpll] = PLL(pll_2650, CLK_FOUT_RPLL, "fout_rpll", "fin_pll", RPLL_LOCK,
		RPLL_CON0, NULL),
	[ipll] = PLL(pll_2550, CLK_FOUT_IPLL, "fout_ipll", "fin_pll", IPLL_LOCK,
		IPLL_CON0, NULL),
	[spll] = PLL(pll_2550, CLK_FOUT_SPLL, "fout_spll", "fin_pll", SPLL_LOCK,
		SPLL_CON0, NULL),
	[vpll] = PLL(pll_2550, CLK_FOUT_VPLL, "fout_vpll", "fin_pll", VPLL_LOCK,
		VPLL_CON0, NULL),
	[mpll] = PLL(pll_2550, CLK_FOUT_MPLL, "fout_mpll", "fin_pll", MPLL_LOCK,
		MPLL_CON0, NULL),
	[bpll] = PLL(pll_2550, CLK_FOUT_BPLL, "fout_bpll", "fin_pll", BPLL_LOCK,
		BPLL_CON0, NULL),
	[kpll] = PLL(pll_2550, CLK_FOUT_KPLL, "fout_kpll", "fin_pll", KPLL_LOCK,
		KPLL_CON0, NULL),
};

#define E5420_EGL_DIV0(apll, pclk_dbg, atb, cpud)			\
		((((apll) << 24) | ((pclk_dbg) << 20) | ((atb) << 16) |	\
		 ((cpud) << 4)))

static const struct exynos_cpuclk_cfg_data exynos5420_eglclk_d[] __initconst = {
	{ 1800000, E5420_EGL_DIV0(3, 7, 7, 4), },
	{ 1700000, E5420_EGL_DIV0(3, 7, 7, 3), },
	{ 1600000, E5420_EGL_DIV0(3, 7, 7, 3), },
	{ 1500000, E5420_EGL_DIV0(3, 7, 7, 3), },
	{ 1400000, E5420_EGL_DIV0(3, 7, 7, 3), },
	{ 1300000, E5420_EGL_DIV0(3, 7, 7, 2), },
	{ 1200000, E5420_EGL_DIV0(3, 7, 7, 2), },
	{ 1100000, E5420_EGL_DIV0(3, 7, 7, 2), },
	{ 1000000, E5420_EGL_DIV0(3, 6, 6, 2), },
	{  900000, E5420_EGL_DIV0(3, 6, 6, 2), },
	{  800000, E5420_EGL_DIV0(3, 5, 5, 2), },
	{  700000, E5420_EGL_DIV0(3, 5, 5, 2), },
	{  600000, E5420_EGL_DIV0(3, 4, 4, 2), },
	{  500000, E5420_EGL_DIV0(3, 3, 3, 2), },
	{  400000, E5420_EGL_DIV0(3, 3, 3, 2), },
	{  300000, E5420_EGL_DIV0(3, 3, 3, 2), },
	{  200000, E5420_EGL_DIV0(3, 3, 3, 2), },
	{  0 },
};

static const struct exynos_cpuclk_cfg_data exynos5800_eglclk_d[] __initconst = {
	{ 2000000, E5420_EGL_DIV0(3, 7, 7, 4), },
	{ 1900000, E5420_EGL_DIV0(3, 7, 7, 4), },
	{ 1800000, E5420_EGL_DIV0(3, 7, 7, 4), },
	{ 1700000, E5420_EGL_DIV0(3, 7, 7, 3), },
	{ 1600000, E5420_EGL_DIV0(3, 7, 7, 3), },
	{ 1500000, E5420_EGL_DIV0(3, 7, 7, 3), },
	{ 1400000, E5420_EGL_DIV0(3, 7, 7, 3), },
	{ 1300000, E5420_EGL_DIV0(3, 7, 7, 2), },
	{ 1200000, E5420_EGL_DIV0(3, 7, 7, 2), },
	{ 1100000, E5420_EGL_DIV0(3, 7, 7, 2), },
	{ 1000000, E5420_EGL_DIV0(3, 7, 6, 2), },
	{  900000, E5420_EGL_DIV0(3, 7, 6, 2), },
	{  800000, E5420_EGL_DIV0(3, 7, 5, 2), },
	{  700000, E5420_EGL_DIV0(3, 7, 5, 2), },
	{  600000, E5420_EGL_DIV0(3, 7, 4, 2), },
	{  500000, E5420_EGL_DIV0(3, 7, 3, 2), },
	{  400000, E5420_EGL_DIV0(3, 7, 3, 2), },
	{  300000, E5420_EGL_DIV0(3, 7, 3, 2), },
	{  200000, E5420_EGL_DIV0(3, 7, 3, 2), },
	{  0 },
};

#define E5420_KFC_DIV(kpll, pclk, aclk)					\
		((((kpll) << 24) | ((pclk) << 20) | ((aclk) << 4)))

static const struct exynos_cpuclk_cfg_data exynos5420_kfcclk_d[] __initconst = {
	{ 1400000, E5420_KFC_DIV(3, 5, 3), }, /* for Exynos5800 */
	{ 1300000, E5420_KFC_DIV(3, 5, 2), },
	{ 1200000, E5420_KFC_DIV(3, 5, 2), },
	{ 1100000, E5420_KFC_DIV(3, 5, 2), },
	{ 1000000, E5420_KFC_DIV(3, 5, 2), },
	{  900000, E5420_KFC_DIV(3, 5, 2), },
	{  800000, E5420_KFC_DIV(3, 5, 2), },
	{  700000, E5420_KFC_DIV(3, 4, 2), },
	{  600000, E5420_KFC_DIV(3, 4, 2), },
	{  500000, E5420_KFC_DIV(3, 4, 2), },
	{  400000, E5420_KFC_DIV(3, 3, 2), },
	{  300000, E5420_KFC_DIV(3, 3, 2), },
	{  200000, E5420_KFC_DIV(3, 3, 2), },
	{  0 },
};

static const struct samsung_cpu_clock exynos5420_cpu_clks[] __initconst = {
	CPU_CLK(CLK_ARM_CLK, "armclk", CLK_MOUT_APLL, CLK_MOUT_MSPLL_CPU, 0,
		0x0, CPUCLK_LAYOUT_E4210, exynos5420_eglclk_d),
	CPU_CLK(CLK_KFC_CLK, "kfcclk", CLK_MOUT_KPLL, CLK_MOUT_MSPLL_KFC, 0,
		0x28000, CPUCLK_LAYOUT_E4210, exynos5420_kfcclk_d),
};

static const struct samsung_cpu_clock exynos5800_cpu_clks[] __initconst = {
	CPU_CLK(CLK_ARM_CLK, "armclk", CLK_MOUT_APLL, CLK_MOUT_MSPLL_CPU, 0,
		0x0, CPUCLK_LAYOUT_E4210, exynos5800_eglclk_d),
	CPU_CLK(CLK_KFC_CLK, "kfcclk", CLK_MOUT_KPLL, CLK_MOUT_MSPLL_KFC, 0,
		0x28000, CPUCLK_LAYOUT_E4210, exynos5420_kfcclk_d),
};

static const struct of_device_id ext_clk_match[] __initconst = {
	{ .compatible = "samsung,exynos5420-oscclk", .data = (void *)0, },
	{ },
};

/* register exynos5420 clocks */
static void __init exynos5x_clk_init(struct device_node *np,
		enum exynos5x_soc soc)
{
	struct samsung_clk_provider *ctx;
	struct clk_hw **hws;

	if (np) {
		reg_base = of_iomap(np, 0);
		if (!reg_base)
			panic("%s: failed to map registers\n", __func__);
	} else {
		panic("%s: unable to determine soc\n", __func__);
	}

	exynos5x_soc = soc;

	ctx = samsung_clk_init(NULL, reg_base, CLKS_NR);
	hws = ctx->clk_data.hws;

	samsung_clk_of_register_fixed_ext(ctx, exynos5x_fixed_rate_ext_clks,
			ARRAY_SIZE(exynos5x_fixed_rate_ext_clks),
			ext_clk_match);

	if (clk_hw_get_rate(hws[CLK_FIN_PLL]) == 24 * MHZ) {
		exynos5x_plls[apll].rate_table = exynos5420_pll2550x_24mhz_tbl;
		exynos5x_plls[epll].rate_table = exynos5420_epll_24mhz_tbl;
		exynos5x_plls[kpll].rate_table = exynos5420_pll2550x_24mhz_tbl;
		exynos5x_plls[vpll].rate_table = exynos5420_vpll_24mhz_tbl;
	}

	if (soc == EXYNOS5420)
		exynos5x_plls[bpll].rate_table = exynos5420_pll2550x_24mhz_tbl;
	else
		exynos5x_plls[bpll].rate_table = exynos5422_bpll_rate_table;

	samsung_clk_register_pll(ctx, exynos5x_plls, ARRAY_SIZE(exynos5x_plls));
	samsung_clk_register_fixed_rate(ctx, exynos5x_fixed_rate_clks,
			ARRAY_SIZE(exynos5x_fixed_rate_clks));
	samsung_clk_register_fixed_factor(ctx, exynos5x_fixed_factor_clks,
			ARRAY_SIZE(exynos5x_fixed_factor_clks));
	samsung_clk_register_mux(ctx, exynos5x_mux_clks,
			ARRAY_SIZE(exynos5x_mux_clks));
	samsung_clk_register_div(ctx, exynos5x_div_clks,
			ARRAY_SIZE(exynos5x_div_clks));
	samsung_clk_register_gate(ctx, exynos5x_gate_clks,
			ARRAY_SIZE(exynos5x_gate_clks));

	if (soc == EXYNOS5420) {
		samsung_clk_register_mux(ctx, exynos5420_mux_clks,
				ARRAY_SIZE(exynos5420_mux_clks));
		samsung_clk_register_div(ctx, exynos5420_div_clks,
				ARRAY_SIZE(exynos5420_div_clks));
		samsung_clk_register_gate(ctx, exynos5420_gate_clks,
				ARRAY_SIZE(exynos5420_gate_clks));
	} else {
		samsung_clk_register_fixed_factor(
				ctx, exynos5800_fixed_factor_clks,
				ARRAY_SIZE(exynos5800_fixed_factor_clks));
		samsung_clk_register_mux(ctx, exynos5800_mux_clks,
				ARRAY_SIZE(exynos5800_mux_clks));
		samsung_clk_register_div(ctx, exynos5800_div_clks,
				ARRAY_SIZE(exynos5800_div_clks));
		samsung_clk_register_gate(ctx, exynos5800_gate_clks,
				ARRAY_SIZE(exynos5800_gate_clks));
	}

	if (soc == EXYNOS5420) {
		samsung_clk_register_cpu(ctx, exynos5420_cpu_clks,
				ARRAY_SIZE(exynos5420_cpu_clks));
	} else {
		samsung_clk_register_cpu(ctx, exynos5800_cpu_clks,
				ARRAY_SIZE(exynos5800_cpu_clks));
	}

	samsung_clk_extended_sleep_init(reg_base,
		exynos5x_clk_regs, ARRAY_SIZE(exynos5x_clk_regs),
		exynos5420_set_clksrc, ARRAY_SIZE(exynos5420_set_clksrc));

	if (soc == EXYNOS5800) {
		samsung_clk_sleep_init(reg_base, exynos5800_clk_regs,
				       ARRAY_SIZE(exynos5800_clk_regs));

		exynos5_subcmus_init(ctx, ARRAY_SIZE(exynos5800_subcmus),
				     exynos5800_subcmus);
	} else {
		exynos5_subcmus_init(ctx, ARRAY_SIZE(exynos5x_subcmus),
				     exynos5x_subcmus);
	}

	/*
	 * Keep top part of G3D clock path enabled permanently to ensure
	 * that the internal busses get their clock regardless of the
	 * main G3D clock enablement status.
	 */
	clk_prepare_enable(hws[CLK_MOUT_SW_ACLK_G3D]->clk);
	/*
	 * Keep top BPLL mux enabled permanently to ensure that DRAM operates
	 * properly.
	 */
	clk_prepare_enable(hws[CLK_MOUT_BPLL]->clk);

	samsung_clk_of_add_provider(np, ctx);
}

static void __init exynos5420_clk_init(struct device_node *np)
{
	exynos5x_clk_init(np, EXYNOS5420);
}
CLK_OF_DECLARE_DRIVER(exynos5420_clk, "samsung,exynos5420-clock",
		      exynos5420_clk_init);

static void __init exynos5800_clk_init(struct device_node *np)
{
	exynos5x_clk_init(np, EXYNOS5800);
}
CLK_OF_DECLARE_DRIVER(exynos5800_clk, "samsung,exynos5800-clock",
		      exynos5800_clk_init);
