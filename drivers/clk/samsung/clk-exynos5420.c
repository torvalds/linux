/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Authors: Thomas Abraham <thomas.ab@samsung.com>
 *	    Chander Kashyap <k.chander@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for Exynos5420 SoC.
*/

#include <dt-bindings/clock/exynos5420.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk.h"

#define APLL_LOCK		0x0
#define APLL_CON0		0x100
#define SRC_CPU			0x200
#define DIV_CPU0		0x500
#define DIV_CPU1		0x504
#define GATE_BUS_CPU		0x700
#define GATE_SCLK_CPU		0x800
#define CPLL_LOCK		0x10020
#define DPLL_LOCK		0x10030
#define EPLL_LOCK		0x10040
#define RPLL_LOCK		0x10050
#define IPLL_LOCK		0x10060
#define SPLL_LOCK		0x10070
#define VPLL_LOCK		0x10070
#define MPLL_LOCK		0x10090
#define CPLL_CON0		0x10120
#define DPLL_CON0		0x10128
#define EPLL_CON0		0x10130
#define RPLL_CON0		0x10140
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
#define SRC_DISP10		0x1022c
#define SRC_MAU			0x10240
#define SRC_FSYS		0x10244
#define SRC_PERIC0		0x10250
#define SRC_PERIC1		0x10254
#define SRC_TOP10		0x10280
#define SRC_TOP11		0x10284
#define SRC_TOP12		0x10288
#define	SRC_MASK_DISP10		0x1032c
#define SRC_MASK_FSYS		0x10340
#define SRC_MASK_PERIC0		0x10350
#define SRC_MASK_PERIC1		0x10354
#define DIV_TOP0		0x10500
#define DIV_TOP1		0x10504
#define DIV_TOP2		0x10508
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
#define GATE_BUS_TOP		0x10700
#define GATE_BUS_FSYS0		0x10740
#define GATE_BUS_PERIC		0x10750
#define GATE_BUS_PERIC1		0x10754
#define GATE_BUS_PERIS0		0x10760
#define GATE_BUS_PERIS1		0x10764
#define GATE_IP_GSCL0		0x10910
#define GATE_IP_GSCL1		0x10920
#define GATE_IP_MFC		0x1092c
#define GATE_IP_DISP1		0x10928
#define GATE_IP_G3D		0x10930
#define GATE_IP_GEN		0x10934
#define GATE_IP_MSCL		0x10970
#define GATE_TOP_SCLK_GSCL	0x10820
#define GATE_TOP_SCLK_DISP1	0x10828
#define GATE_TOP_SCLK_MAU	0x1083c
#define GATE_TOP_SCLK_FSYS	0x10840
#define GATE_TOP_SCLK_PERIC	0x10850
#define BPLL_LOCK		0x20010
#define BPLL_CON0		0x20110
#define SRC_CDREX		0x20200
#define KPLL_LOCK		0x28000
#define KPLL_CON0		0x28100
#define SRC_KFC			0x28200
#define DIV_KFC0		0x28500

/* list of PLLs */
enum exynos5420_plls {
	apll, cpll, dpll, epll, rpll, ipll, spll, vpll, mpll,
	bpll, kpll,
	nr_plls			/* number of PLLs */
};

/*
 * list of controller registers to be saved and restored during a
 * suspend/resume cycle.
 */
static unsigned long exynos5420_clk_regs[] __initdata = {
	SRC_CPU,
	DIV_CPU0,
	DIV_CPU1,
	GATE_BUS_CPU,
	GATE_SCLK_CPU,
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
	SRC_MASK_DISP10,
	SRC_MASK_FSYS,
	SRC_MASK_PERIC0,
	SRC_MASK_PERIC1,
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
	GATE_BUS_TOP,
	GATE_BUS_FSYS0,
	GATE_BUS_PERIC,
	GATE_BUS_PERIC1,
	GATE_BUS_PERIS0,
	GATE_BUS_PERIS1,
	GATE_IP_GSCL0,
	GATE_IP_GSCL1,
	GATE_IP_MFC,
	GATE_IP_DISP1,
	GATE_IP_G3D,
	GATE_IP_GEN,
	GATE_IP_MSCL,
	GATE_TOP_SCLK_GSCL,
	GATE_TOP_SCLK_DISP1,
	GATE_TOP_SCLK_MAU,
	GATE_TOP_SCLK_FSYS,
	GATE_TOP_SCLK_PERIC,
	SRC_CDREX,
	SRC_KFC,
	DIV_KFC0,
};

/* list of all parent clocks */
PNAME(mspll_cpu_p)	= { "sclk_cpll", "sclk_dpll",
				"sclk_mpll", "sclk_spll" };
PNAME(cpu_p)		= { "mout_apll" , "mout_mspll_cpu" };
PNAME(kfc_p)		= { "mout_kpll" , "mout_mspll_kfc" };
PNAME(apll_p)		= { "fin_pll", "fout_apll", };
PNAME(bpll_p)		= { "fin_pll", "fout_bpll", };
PNAME(cpll_p)		= { "fin_pll", "fout_cpll", };
PNAME(dpll_p)		= { "fin_pll", "fout_dpll", };
PNAME(epll_p)		= { "fin_pll", "fout_epll", };
PNAME(ipll_p)		= { "fin_pll", "fout_ipll", };
PNAME(kpll_p)		= { "fin_pll", "fout_kpll", };
PNAME(mpll_p)		= { "fin_pll", "fout_mpll", };
PNAME(rpll_p)		= { "fin_pll", "fout_rpll", };
PNAME(spll_p)		= { "fin_pll", "fout_spll", };
PNAME(vpll_p)		= { "fin_pll", "fout_vpll", };

PNAME(group1_p)		= { "sclk_cpll", "sclk_dpll", "sclk_mpll" };
PNAME(group2_p)		= { "fin_pll", "sclk_cpll", "sclk_dpll", "sclk_mpll",
			  "sclk_spll", "sclk_ipll", "sclk_epll", "sclk_rpll" };
PNAME(group3_p)		= { "sclk_rpll", "sclk_spll" };
PNAME(group4_p)		= { "sclk_ipll", "sclk_dpll", "sclk_mpll" };
PNAME(group5_p)		= { "sclk_vpll", "sclk_dpll" };

PNAME(sw_aclk66_p)	= { "dout_aclk66", "sclk_spll" };
PNAME(aclk66_peric_p)	= { "fin_pll", "mout_sw_aclk66" };

PNAME(sw_aclk200_fsys_p) = { "dout_aclk200_fsys", "sclk_spll"};
PNAME(user_aclk200_fsys_p)	= { "fin_pll", "mout_sw_aclk200_fsys" };

PNAME(sw_aclk200_fsys2_p) = { "dout_aclk200_fsys2", "sclk_spll"};
PNAME(user_aclk200_fsys2_p)	= { "fin_pll", "mout_sw_aclk200_fsys2" };

PNAME(sw_aclk200_p) = { "dout_aclk200", "sclk_spll"};
PNAME(aclk200_disp1_p)	= { "fin_pll", "mout_sw_aclk200" };

PNAME(sw_aclk400_mscl_p) = { "dout_aclk400_mscl", "sclk_spll"};
PNAME(user_aclk400_mscl_p)	= { "fin_pll", "mout_sw_aclk400_mscl" };

PNAME(sw_aclk333_p) = { "dout_aclk333", "sclk_spll"};
PNAME(user_aclk333_p)	= { "fin_pll", "mout_sw_aclk333" };

PNAME(sw_aclk166_p) = { "dout_aclk166", "sclk_spll"};
PNAME(user_aclk166_p)	= { "fin_pll", "mout_sw_aclk166" };

PNAME(sw_aclk266_p) = { "dout_aclk266", "sclk_spll"};
PNAME(user_aclk266_p)	= { "fin_pll", "mout_sw_aclk266" };

PNAME(sw_aclk333_432_gscl_p) = { "dout_aclk333_432_gscl", "sclk_spll"};
PNAME(user_aclk333_432_gscl_p)	= { "fin_pll", "mout_sw_aclk333_432_gscl" };

PNAME(sw_aclk300_gscl_p) = { "dout_aclk300_gscl", "sclk_spll"};
PNAME(user_aclk300_gscl_p)	= { "fin_pll", "mout_sw_aclk300_gscl" };

PNAME(sw_aclk300_disp1_p) = { "dout_aclk300_disp1", "sclk_spll"};
PNAME(user_aclk300_disp1_p)	= { "fin_pll", "mout_sw_aclk300_disp1" };

PNAME(sw_aclk300_jpeg_p) = { "dout_aclk300_jpeg", "sclk_spll"};
PNAME(user_aclk300_jpeg_p)	= { "fin_pll", "mout_sw_aclk300_jpeg" };

PNAME(sw_aclk_g3d_p) = { "dout_aclk_g3d", "sclk_spll"};
PNAME(user_aclk_g3d_p)	= { "fin_pll", "mout_sw_aclk_g3d" };

PNAME(sw_aclk266_g2d_p) = { "dout_aclk266_g2d", "sclk_spll"};
PNAME(user_aclk266_g2d_p)	= { "fin_pll", "mout_sw_aclk266_g2d" };

PNAME(sw_aclk333_g2d_p) = { "dout_aclk333_g2d", "sclk_spll"};
PNAME(user_aclk333_g2d_p)	= { "fin_pll", "mout_sw_aclk333_g2d" };

PNAME(audio0_p)	= { "fin_pll", "cdclk0", "sclk_dpll", "sclk_mpll",
		  "sclk_spll", "sclk_ipll", "sclk_epll", "sclk_rpll" };
PNAME(audio1_p)	= { "fin_pll", "cdclk1", "sclk_dpll", "sclk_mpll",
		  "sclk_spll", "sclk_ipll", "sclk_epll", "sclk_rpll" };
PNAME(audio2_p)	= { "fin_pll", "cdclk2", "sclk_dpll", "sclk_mpll",
		  "sclk_spll", "sclk_ipll", "sclk_epll", "sclk_rpll" };
PNAME(spdif_p)	= { "fin_pll", "dout_audio0", "dout_audio1", "dout_audio2",
		  "spdif_extclk", "sclk_ipll", "sclk_epll", "sclk_rpll" };
PNAME(hdmi_p)	= { "dout_hdmi_pixel", "sclk_hdmiphy" };
PNAME(maudio0_p)	= { "fin_pll", "maudio_clk", "sclk_dpll", "sclk_mpll",
			  "sclk_spll", "sclk_ipll", "sclk_epll", "sclk_rpll" };

/* fixed rate clocks generated outside the soc */
static struct samsung_fixed_rate_clock exynos5420_fixed_rate_ext_clks[] __initdata = {
	FRATE(CLK_FIN_PLL, "fin_pll", NULL, CLK_IS_ROOT, 0),
};

/* fixed rate clocks generated inside the soc */
static struct samsung_fixed_rate_clock exynos5420_fixed_rate_clks[] __initdata = {
	FRATE(CLK_SCLK_HDMIPHY, "sclk_hdmiphy", NULL, CLK_IS_ROOT, 24000000),
	FRATE(0, "sclk_pwi", NULL, CLK_IS_ROOT, 24000000),
	FRATE(0, "sclk_usbh20", NULL, CLK_IS_ROOT, 48000000),
	FRATE(0, "mphy_refclk_ixtal24", NULL, CLK_IS_ROOT, 48000000),
	FRATE(0, "sclk_usbh20_scan_clk", NULL, CLK_IS_ROOT, 480000000),
};

static struct samsung_fixed_factor_clock exynos5420_fixed_factor_clks[] __initdata = {
	FFACTOR(0, "sclk_hsic_12m", "fin_pll", 1, 2, 0),
};

static struct samsung_mux_clock exynos5420_mux_clks[] __initdata = {
	MUX(0, "mout_mspll_kfc", mspll_cpu_p, SRC_TOP7, 8, 2),
	MUX(0, "mout_mspll_cpu", mspll_cpu_p, SRC_TOP7, 12, 2),
	MUX(0, "mout_apll", apll_p, SRC_CPU, 0, 1),
	MUX(0, "mout_cpu", cpu_p, SRC_CPU, 16, 1),
	MUX(0, "mout_kpll", kpll_p, SRC_KFC, 0, 1),
	MUX(0, "mout_cpu_kfc", kfc_p, SRC_KFC, 16, 1),

	MUX(0, "sclk_bpll", bpll_p, SRC_CDREX, 0, 1),

	MUX_A(0, "mout_aclk400_mscl", group1_p,
			SRC_TOP0, 4, 2, "aclk400_mscl"),
	MUX(0, "mout_aclk200", group1_p, SRC_TOP0, 8, 2),
	MUX(0, "mout_aclk200_fsys2", group1_p, SRC_TOP0, 12, 2),
	MUX(0, "mout_aclk200_fsys", group1_p, SRC_TOP0, 28, 2),

	MUX(0, "mout_aclk333_432_gscl", group4_p, SRC_TOP1, 0, 2),
	MUX(0, "mout_aclk66", group1_p, SRC_TOP1, 8, 2),
	MUX(0, "mout_aclk266", group1_p, SRC_TOP1, 20, 2),
	MUX(0, "mout_aclk166", group1_p, SRC_TOP1, 24, 2),
	MUX(0, "mout_aclk333", group1_p, SRC_TOP1, 28, 2),

	MUX(0, "mout_aclk333_g2d", group1_p, SRC_TOP2, 8, 2),
	MUX(0, "mout_aclk266_g2d", group1_p, SRC_TOP2, 12, 2),
	MUX(0, "mout_aclk_g3d", group5_p, SRC_TOP2, 16, 1),
	MUX(0, "mout_aclk300_jpeg", group1_p, SRC_TOP2, 20, 2),
	MUX(0, "mout_aclk300_disp1", group1_p, SRC_TOP2, 24, 2),
	MUX(0, "mout_aclk300_gscl", group1_p, SRC_TOP2, 28, 2),

	MUX(0, "mout_user_aclk400_mscl", user_aclk400_mscl_p,
			SRC_TOP3, 4, 1),
	MUX_A(0, "mout_aclk200_disp1", aclk200_disp1_p,
			SRC_TOP3, 8, 1, "aclk200_disp1"),
	MUX(0, "mout_user_aclk200_fsys2", user_aclk200_fsys2_p,
			SRC_TOP3, 12, 1),
	MUX(0, "mout_user_aclk200_fsys", user_aclk200_fsys_p,
			SRC_TOP3, 28, 1),

	MUX(0, "mout_user_aclk333_432_gscl", user_aclk333_432_gscl_p,
			SRC_TOP4, 0, 1),
	MUX(0, "mout_aclk66_peric", aclk66_peric_p, SRC_TOP4, 8, 1),
	MUX(0, "mout_user_aclk266", user_aclk266_p, SRC_TOP4, 20, 1),
	MUX(0, "mout_user_aclk166", user_aclk166_p, SRC_TOP4, 24, 1),
	MUX(0, "mout_user_aclk333", user_aclk333_p, SRC_TOP4, 28, 1),

	MUX(0, "mout_aclk66_psgen", aclk66_peric_p, SRC_TOP5, 4, 1),
	MUX(0, "mout_user_aclk333_g2d", user_aclk333_g2d_p, SRC_TOP5, 8, 1),
	MUX(0, "mout_user_aclk266_g2d", user_aclk266_g2d_p, SRC_TOP5, 12, 1),
	MUX_A(0, "mout_user_aclk_g3d", user_aclk_g3d_p,
			SRC_TOP5, 16, 1, "aclkg3d"),
	MUX(0, "mout_user_aclk300_jpeg", user_aclk300_jpeg_p,
			SRC_TOP5, 20, 1),
	MUX(0, "mout_user_aclk300_disp1", user_aclk300_disp1_p,
			SRC_TOP5, 24, 1),
	MUX(0, "mout_user_aclk300_gscl", user_aclk300_gscl_p,
			SRC_TOP5, 28, 1),

	MUX(0, "sclk_mpll", mpll_p, SRC_TOP6, 0, 1),
	MUX(0, "sclk_vpll", vpll_p, SRC_TOP6, 4, 1),
	MUX(0, "sclk_spll", spll_p, SRC_TOP6, 8, 1),
	MUX(0, "sclk_ipll", ipll_p, SRC_TOP6, 12, 1),
	MUX(0, "sclk_rpll", rpll_p, SRC_TOP6, 16, 1),
	MUX(0, "sclk_epll", epll_p, SRC_TOP6, 20, 1),
	MUX(0, "sclk_dpll", dpll_p, SRC_TOP6, 24, 1),
	MUX(0, "sclk_cpll", cpll_p, SRC_TOP6, 28, 1),

	MUX(0, "mout_sw_aclk400_mscl", sw_aclk400_mscl_p, SRC_TOP10, 4, 1),
	MUX(0, "mout_sw_aclk200", sw_aclk200_p, SRC_TOP10, 8, 1),
	MUX(0, "mout_sw_aclk200_fsys2", sw_aclk200_fsys2_p,
			SRC_TOP10, 12, 1),
	MUX(0, "mout_sw_aclk200_fsys", sw_aclk200_fsys_p, SRC_TOP10, 28, 1),

	MUX(0, "mout_sw_aclk333_432_gscl", sw_aclk333_432_gscl_p,
			SRC_TOP11, 0, 1),
	MUX(0, "mout_sw_aclk66", sw_aclk66_p, SRC_TOP11, 8, 1),
	MUX(0, "mout_sw_aclk266", sw_aclk266_p, SRC_TOP11, 20, 1),
	MUX(0, "mout_sw_aclk166", sw_aclk166_p, SRC_TOP11, 24, 1),
	MUX(0, "mout_sw_aclk333", sw_aclk333_p, SRC_TOP11, 28, 1),

	MUX(0, "mout_sw_aclk333_g2d", sw_aclk333_g2d_p, SRC_TOP12, 8, 1),
	MUX(0, "mout_sw_aclk266_g2d", sw_aclk266_g2d_p, SRC_TOP12, 12, 1),
	MUX(0, "mout_sw_aclk_g3d", sw_aclk_g3d_p, SRC_TOP12, 16, 1),
	MUX(0, "mout_sw_aclk300_jpeg", sw_aclk300_jpeg_p, SRC_TOP12, 20, 1),
	MUX(0, "mout_sw_aclk300_disp1", sw_aclk300_disp1_p,
			SRC_TOP12, 24, 1),
	MUX(0, "mout_sw_aclk300_gscl", sw_aclk300_gscl_p, SRC_TOP12, 28, 1),

	/* DISP1 Block */
	MUX(0, "mout_fimd1", group3_p, SRC_DISP10, 4, 1),
	MUX(0, "mout_mipi1", group2_p, SRC_DISP10, 16, 3),
	MUX(0, "mout_dp1", group2_p, SRC_DISP10, 20, 3),
	MUX(0, "mout_pixel", group2_p, SRC_DISP10, 24, 3),
	MUX(CLK_MOUT_HDMI, "mout_hdmi", hdmi_p, SRC_DISP10, 28, 1),

	/* MAU Block */
	MUX(0, "mout_maudio0", maudio0_p, SRC_MAU, 28, 3),

	/* FSYS Block */
	MUX(0, "mout_usbd301", group2_p, SRC_FSYS, 4, 3),
	MUX(0, "mout_mmc0", group2_p, SRC_FSYS, 8, 3),
	MUX(0, "mout_mmc1", group2_p, SRC_FSYS, 12, 3),
	MUX(0, "mout_mmc2", group2_p, SRC_FSYS, 16, 3),
	MUX(0, "mout_usbd300", group2_p, SRC_FSYS, 20, 3),
	MUX(0, "mout_unipro", group2_p, SRC_FSYS, 24, 3),

	/* PERIC Block */
	MUX(0, "mout_uart0", group2_p, SRC_PERIC0, 4, 3),
	MUX(0, "mout_uart1", group2_p, SRC_PERIC0, 8, 3),
	MUX(0, "mout_uart2", group2_p, SRC_PERIC0, 12, 3),
	MUX(0, "mout_uart3", group2_p, SRC_PERIC0, 16, 3),
	MUX(0, "mout_pwm", group2_p, SRC_PERIC0, 24, 3),
	MUX(0, "mout_spdif", spdif_p, SRC_PERIC0, 28, 3),
	MUX(0, "mout_audio0", audio0_p, SRC_PERIC1, 8, 3),
	MUX(0, "mout_audio1", audio1_p, SRC_PERIC1, 12, 3),
	MUX(0, "mout_audio2", audio2_p, SRC_PERIC1, 16, 3),
	MUX(0, "mout_spi0", group2_p, SRC_PERIC1, 20, 3),
	MUX(0, "mout_spi1", group2_p, SRC_PERIC1, 24, 3),
	MUX(0, "mout_spi2", group2_p, SRC_PERIC1, 28, 3),
};

static struct samsung_div_clock exynos5420_div_clks[] __initdata = {
	DIV(0, "div_arm", "mout_cpu", DIV_CPU0, 0, 3),
	DIV(0, "sclk_apll", "mout_apll", DIV_CPU0, 24, 3),
	DIV(0, "armclk2", "div_arm", DIV_CPU0, 28, 3),
	DIV(0, "div_kfc", "mout_cpu_kfc", DIV_KFC0, 0, 3),
	DIV(0, "sclk_kpll", "mout_kpll", DIV_KFC0, 24, 3),

	DIV(0, "dout_aclk400_mscl", "mout_aclk400_mscl", DIV_TOP0, 4, 3),
	DIV(0, "dout_aclk200", "mout_aclk200", DIV_TOP0, 8, 3),
	DIV(0, "dout_aclk200_fsys2", "mout_aclk200_fsys2", DIV_TOP0, 12, 3),
	DIV(0, "dout_pclk200_fsys", "mout_pclk200_fsys", DIV_TOP0, 24, 3),
	DIV(0, "dout_aclk200_fsys", "mout_aclk200_fsys", DIV_TOP0, 28, 3),

	DIV(0, "dout_aclk333_432_gscl", "mout_aclk333_432_gscl",
			DIV_TOP1, 0, 3),
	DIV(0, "dout_aclk66", "mout_aclk66", DIV_TOP1, 8, 6),
	DIV(0, "dout_aclk266", "mout_aclk266", DIV_TOP1, 20, 3),
	DIV(0, "dout_aclk166", "mout_aclk166", DIV_TOP1, 24, 3),
	DIV(0, "dout_aclk333", "mout_aclk333", DIV_TOP1, 28, 3),

	DIV(0, "dout_aclk333_g2d", "mout_aclk333_g2d", DIV_TOP2, 8, 3),
	DIV(0, "dout_aclk266_g2d", "mout_aclk266_g2d", DIV_TOP2, 12, 3),
	DIV(0, "dout_aclk_g3d", "mout_aclk_g3d", DIV_TOP2, 16, 3),
	DIV(0, "dout_aclk300_jpeg", "mout_aclk300_jpeg", DIV_TOP2, 20, 3),
	DIV_A(0, "dout_aclk300_disp1", "mout_aclk300_disp1",
			DIV_TOP2, 24, 3, "aclk300_disp1"),
	DIV(0, "dout_aclk300_gscl", "mout_aclk300_gscl", DIV_TOP2, 28, 3),

	/* DISP1 Block */
	DIV(0, "dout_fimd1", "mout_fimd1", DIV_DISP10, 0, 4),
	DIV(0, "dout_mipi1", "mout_mipi1", DIV_DISP10, 16, 8),
	DIV(0, "dout_dp1", "mout_dp1", DIV_DISP10, 24, 4),
	DIV(CLK_DOUT_PIXEL, "dout_hdmi_pixel", "mout_pixel", DIV_DISP10, 28, 4),

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
	DIV(0, "dout_pre_spi0", "dout_spi0", DIV_PERIC4, 8, 8),
	DIV(0, "dout_pre_spi1", "dout_spi1", DIV_PERIC4, 16, 8),
	DIV(0, "dout_pre_spi2", "dout_spi2", DIV_PERIC4, 24, 8),
};

static struct samsung_gate_clock exynos5420_gate_clks[] __initdata = {
	/* TODO: Re-verify the CG bits for all the gate clocks */
	GATE_A(CLK_MCT, "pclk_st", "aclk66_psgen", GATE_BUS_PERIS1, 2, 0, 0,
		"mct"),

	GATE(0, "aclk200_fsys", "mout_user_aclk200_fsys",
			GATE_BUS_FSYS0, 9, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk200_fsys2", "mout_user_aclk200_fsys2",
			GATE_BUS_FSYS0, 10, CLK_IGNORE_UNUSED, 0),

	GATE(0, "aclk333_g2d", "mout_user_aclk333_g2d",
			GATE_BUS_TOP, 0, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk266_g2d", "mout_user_aclk266_g2d",
			GATE_BUS_TOP, 1, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk300_jpeg", "mout_user_aclk300_jpeg",
			GATE_BUS_TOP, 4, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk300_gscl", "mout_user_aclk300_gscl",
			GATE_BUS_TOP, 6, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk333_432_gscl", "mout_user_aclk333_432_gscl",
			GATE_BUS_TOP, 7, CLK_IGNORE_UNUSED, 0),
	GATE(0, "pclk66_gpio", "mout_sw_aclk66",
			GATE_BUS_TOP, 9, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk66_psgen", "mout_aclk66_psgen",
			GATE_BUS_TOP, 10, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk66_peric", "mout_aclk66_peric",
			GATE_BUS_TOP, 11, 0, 0),
	GATE(0, "aclk166", "mout_user_aclk166",
			GATE_BUS_TOP, 14, CLK_IGNORE_UNUSED, 0),
	GATE(0, "aclk333", "mout_aclk333",
			GATE_BUS_TOP, 15, CLK_IGNORE_UNUSED, 0),

	/* sclk */
	GATE(CLK_SCLK_UART0, "sclk_uart0", "dout_uart0",
		GATE_TOP_SCLK_PERIC, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART1, "sclk_uart1", "dout_uart1",
		GATE_TOP_SCLK_PERIC, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART2, "sclk_uart2", "dout_uart2",
		GATE_TOP_SCLK_PERIC, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART3, "sclk_uart3", "dout_uart3",
		GATE_TOP_SCLK_PERIC, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0, "sclk_spi0", "dout_pre_spi0",
		GATE_TOP_SCLK_PERIC, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI1, "sclk_spi1", "dout_pre_spi1",
		GATE_TOP_SCLK_PERIC, 7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI2, "sclk_spi2", "dout_pre_spi2",
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

	GATE(CLK_SCLK_USBD301, "sclk_unipro", "dout_unipro",
		SRC_MASK_FSYS, 24, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_SCLK_GSCL_WA, "sclk_gscl_wa", "aclK333_432_gscl",
		GATE_TOP_SCLK_GSCL, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_GSCL_WB, "sclk_gscl_wb", "aclk333_432_gscl",
		GATE_TOP_SCLK_GSCL, 7, CLK_SET_RATE_PARENT, 0),

	/* Display */
	GATE(CLK_SCLK_FIMD1, "sclk_fimd1", "dout_fimd1",
		GATE_TOP_SCLK_DISP1, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MIPI1, "sclk_mipi1", "dout_mipi1",
		GATE_TOP_SCLK_DISP1, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_HDMI, "sclk_hdmi", "mout_hdmi",
		GATE_TOP_SCLK_DISP1, 9, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PIXEL, "sclk_pixel", "dout_hdmi_pixel",
		GATE_TOP_SCLK_DISP1, 10, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_DP1, "sclk_dp1", "dout_dp1",
		GATE_TOP_SCLK_DISP1, 20, CLK_SET_RATE_PARENT, 0),

	/* Maudio Block */
	GATE(CLK_SCLK_MAUDIO0, "sclk_maudio0", "dout_maudio0",
		GATE_TOP_SCLK_MAU, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MAUPCM0, "sclk_maupcm0", "dout_maupcm0",
		GATE_TOP_SCLK_MAU, 1, CLK_SET_RATE_PARENT, 0),
	/* FSYS */
	GATE(CLK_TSI, "tsi", "aclk200_fsys", GATE_BUS_FSYS0, 0, 0, 0),
	GATE(CLK_PDMA0, "pdma0", "aclk200_fsys", GATE_BUS_FSYS0, 1, 0, 0),
	GATE(CLK_PDMA1, "pdma1", "aclk200_fsys", GATE_BUS_FSYS0, 2, 0, 0),
	GATE(CLK_UFS, "ufs", "aclk200_fsys2", GATE_BUS_FSYS0, 3, 0, 0),
	GATE(CLK_RTIC, "rtic", "aclk200_fsys", GATE_BUS_FSYS0, 5, 0, 0),
	GATE(CLK_MMC0, "mmc0", "aclk200_fsys2", GATE_BUS_FSYS0, 12, 0, 0),
	GATE(CLK_MMC1, "mmc1", "aclk200_fsys2", GATE_BUS_FSYS0, 13, 0, 0),
	GATE(CLK_MMC2, "mmc2", "aclk200_fsys2", GATE_BUS_FSYS0, 14, 0, 0),
	GATE(CLK_SROMC, "sromc", "aclk200_fsys2",
			GATE_BUS_FSYS0, 19, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_USBH20, "usbh20", "aclk200_fsys", GATE_BUS_FSYS0, 20, 0, 0),
	GATE(CLK_USBD300, "usbd300", "aclk200_fsys", GATE_BUS_FSYS0, 21, 0, 0),
	GATE(CLK_USBD301, "usbd301", "aclk200_fsys", GATE_BUS_FSYS0, 28, 0, 0),

	/* UART */
	GATE(CLK_UART0, "uart0", "aclk66_peric", GATE_BUS_PERIC, 4, 0, 0),
	GATE(CLK_UART1, "uart1", "aclk66_peric", GATE_BUS_PERIC, 5, 0, 0),
	GATE_A(CLK_UART2, "uart2", "aclk66_peric",
		GATE_BUS_PERIC, 6, CLK_IGNORE_UNUSED, 0, "uart2"),
	GATE(CLK_UART3, "uart3", "aclk66_peric", GATE_BUS_PERIC, 7, 0, 0),
	/* I2C */
	GATE(CLK_I2C0, "i2c0", "aclk66_peric", GATE_BUS_PERIC, 9, 0, 0),
	GATE(CLK_I2C1, "i2c1", "aclk66_peric", GATE_BUS_PERIC, 10, 0, 0),
	GATE(CLK_I2C2, "i2c2", "aclk66_peric", GATE_BUS_PERIC, 11, 0, 0),
	GATE(CLK_I2C3, "i2c3", "aclk66_peric", GATE_BUS_PERIC, 12, 0, 0),
	GATE(CLK_I2C4, "i2c4", "aclk66_peric", GATE_BUS_PERIC, 13, 0, 0),
	GATE(CLK_I2C5, "i2c5", "aclk66_peric", GATE_BUS_PERIC, 14, 0, 0),
	GATE(CLK_I2C6, "i2c6", "aclk66_peric", GATE_BUS_PERIC, 15, 0, 0),
	GATE(CLK_I2C7, "i2c7", "aclk66_peric", GATE_BUS_PERIC, 16, 0, 0),
	GATE(CLK_I2C_HDMI, "i2c_hdmi", "aclk66_peric", GATE_BUS_PERIC, 17, 0,
		0),
	GATE(CLK_TSADC, "tsadc", "aclk66_peric", GATE_BUS_PERIC, 18, 0, 0),
	/* SPI */
	GATE(CLK_SPI0, "spi0", "aclk66_peric", GATE_BUS_PERIC, 19, 0, 0),
	GATE(CLK_SPI1, "spi1", "aclk66_peric", GATE_BUS_PERIC, 20, 0, 0),
	GATE(CLK_SPI2, "spi2", "aclk66_peric", GATE_BUS_PERIC, 21, 0, 0),
	GATE(CLK_KEYIF, "keyif", "aclk66_peric", GATE_BUS_PERIC, 22, 0, 0),
	/* I2S */
	GATE(CLK_I2S1, "i2s1", "aclk66_peric", GATE_BUS_PERIC, 23, 0, 0),
	GATE(CLK_I2S2, "i2s2", "aclk66_peric", GATE_BUS_PERIC, 24, 0, 0),
	/* PCM */
	GATE(CLK_PCM1, "pcm1", "aclk66_peric", GATE_BUS_PERIC, 25, 0, 0),
	GATE(CLK_PCM2, "pcm2", "aclk66_peric", GATE_BUS_PERIC, 26, 0, 0),
	/* PWM */
	GATE(CLK_PWM, "pwm", "aclk66_peric", GATE_BUS_PERIC, 27, 0, 0),
	/* SPDIF */
	GATE(CLK_SPDIF, "spdif", "aclk66_peric", GATE_BUS_PERIC, 29, 0, 0),

	GATE(CLK_I2C8, "i2c8", "aclk66_peric", GATE_BUS_PERIC1, 0, 0, 0),
	GATE(CLK_I2C9, "i2c9", "aclk66_peric", GATE_BUS_PERIC1, 1, 0, 0),
	GATE(CLK_I2C10, "i2c10", "aclk66_peric", GATE_BUS_PERIC1, 2, 0, 0),

	GATE(CLK_CHIPID, "chipid", "aclk66_psgen",
			GATE_BUS_PERIS0, 12, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SYSREG, "sysreg", "aclk66_psgen",
			GATE_BUS_PERIS0, 13, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_TZPC0, "tzpc0", "aclk66_psgen", GATE_BUS_PERIS0, 18, 0, 0),
	GATE(CLK_TZPC1, "tzpc1", "aclk66_psgen", GATE_BUS_PERIS0, 19, 0, 0),
	GATE(CLK_TZPC2, "tzpc2", "aclk66_psgen", GATE_BUS_PERIS0, 20, 0, 0),
	GATE(CLK_TZPC3, "tzpc3", "aclk66_psgen", GATE_BUS_PERIS0, 21, 0, 0),
	GATE(CLK_TZPC4, "tzpc4", "aclk66_psgen", GATE_BUS_PERIS0, 22, 0, 0),
	GATE(CLK_TZPC5, "tzpc5", "aclk66_psgen", GATE_BUS_PERIS0, 23, 0, 0),
	GATE(CLK_TZPC6, "tzpc6", "aclk66_psgen", GATE_BUS_PERIS0, 24, 0, 0),
	GATE(CLK_TZPC7, "tzpc7", "aclk66_psgen", GATE_BUS_PERIS0, 25, 0, 0),
	GATE(CLK_TZPC8, "tzpc8", "aclk66_psgen", GATE_BUS_PERIS0, 26, 0, 0),
	GATE(CLK_TZPC9, "tzpc9", "aclk66_psgen", GATE_BUS_PERIS0, 27, 0, 0),

	GATE(CLK_HDMI_CEC, "hdmi_cec", "aclk66_psgen", GATE_BUS_PERIS1, 0, 0,
		0),
	GATE(CLK_SECKEY, "seckey", "aclk66_psgen", GATE_BUS_PERIS1, 1, 0, 0),
	GATE(CLK_WDT, "wdt", "aclk66_psgen", GATE_BUS_PERIS1, 3, 0, 0),
	GATE(CLK_RTC, "rtc", "aclk66_psgen", GATE_BUS_PERIS1, 4, 0, 0),
	GATE(CLK_TMU, "tmu", "aclk66_psgen", GATE_BUS_PERIS1, 5, 0, 0),
	GATE(CLK_TMU_GPU, "tmu_gpu", "aclk66_psgen", GATE_BUS_PERIS1, 6, 0, 0),

	GATE(CLK_GSCL0, "gscl0", "aclk300_gscl", GATE_IP_GSCL0, 0, 0, 0),
	GATE(CLK_GSCL1, "gscl1", "aclk300_gscl", GATE_IP_GSCL0, 1, 0, 0),
	GATE(CLK_CLK_3AA, "clk_3aa", "aclk300_gscl", GATE_IP_GSCL0, 4, 0, 0),

	GATE(CLK_SMMU_3AA, "smmu_3aa", "aclk333_432_gscl", GATE_IP_GSCL1, 2, 0,
		0),
	GATE(CLK_SMMU_FIMCL0, "smmu_fimcl0", "aclk333_432_gscl",
			GATE_IP_GSCL1, 3, 0, 0),
	GATE(CLK_SMMU_FIMCL1, "smmu_fimcl1", "aclk333_432_gscl",
			GATE_IP_GSCL1, 4, 0, 0),
	GATE(CLK_SMMU_GSCL0, "smmu_gscl0", "aclk300_gscl", GATE_IP_GSCL1, 6, 0,
		0),
	GATE(CLK_SMMU_GSCL1, "smmu_gscl1", "aclk300_gscl", GATE_IP_GSCL1, 7, 0,
		0),
	GATE(CLK_GSCL_WA, "gscl_wa", "aclk300_gscl", GATE_IP_GSCL1, 12, 0, 0),
	GATE(CLK_GSCL_WB, "gscl_wb", "aclk300_gscl", GATE_IP_GSCL1, 13, 0, 0),
	GATE(CLK_SMMU_FIMCL3, "smmu_fimcl3,", "aclk333_432_gscl",
			GATE_IP_GSCL1, 16, 0, 0),
	GATE(CLK_FIMC_LITE3, "fimc_lite3", "aclk333_432_gscl",
			GATE_IP_GSCL1, 17, 0, 0),

	GATE(CLK_FIMD1, "fimd1", "aclk300_disp1", GATE_IP_DISP1, 0, 0, 0),
	GATE(CLK_DSIM1, "dsim1", "aclk200_disp1", GATE_IP_DISP1, 3, 0, 0),
	GATE(CLK_DP1, "dp1", "aclk200_disp1", GATE_IP_DISP1, 4, 0, 0),
	GATE(CLK_MIXER, "mixer", "aclk166", GATE_IP_DISP1, 5, 0, 0),
	GATE(CLK_HDMI, "hdmi", "aclk200_disp1", GATE_IP_DISP1, 6, 0, 0),
	GATE(CLK_SMMU_FIMD1, "smmu_fimd1", "aclk300_disp1", GATE_IP_DISP1, 8, 0,
		0),

	GATE(CLK_MFC, "mfc", "aclk333", GATE_IP_MFC, 0, 0, 0),
	GATE(CLK_SMMU_MFCL, "smmu_mfcl", "aclk333", GATE_IP_MFC, 1, 0, 0),
	GATE(CLK_SMMU_MFCR, "smmu_mfcr", "aclk333", GATE_IP_MFC, 2, 0, 0),

	GATE(CLK_G3D, "g3d", "aclkg3d", GATE_IP_G3D, 9, 0, 0),

	GATE(CLK_ROTATOR, "rotator", "aclk266", GATE_IP_GEN, 1, 0, 0),
	GATE(CLK_JPEG, "jpeg", "aclk300_jpeg", GATE_IP_GEN, 2, 0, 0),
	GATE(CLK_JPEG2, "jpeg2", "aclk300_jpeg", GATE_IP_GEN, 3, 0, 0),
	GATE(CLK_MDMA1, "mdma1", "aclk266", GATE_IP_GEN, 4, 0, 0),
	GATE(CLK_SMMU_ROTATOR, "smmu_rotator", "aclk266", GATE_IP_GEN, 6, 0, 0),
	GATE(CLK_SMMU_JPEG, "smmu_jpeg", "aclk300_jpeg", GATE_IP_GEN, 7, 0, 0),
	GATE(CLK_SMMU_MDMA1, "smmu_mdma1", "aclk266", GATE_IP_GEN, 9, 0, 0),

	GATE(CLK_MSCL0, "mscl0", "aclk400_mscl", GATE_IP_MSCL, 0, 0, 0),
	GATE(CLK_MSCL1, "mscl1", "aclk400_mscl", GATE_IP_MSCL, 1, 0, 0),
	GATE(CLK_MSCL2, "mscl2", "aclk400_mscl", GATE_IP_MSCL, 2, 0, 0),
	GATE(CLK_SMMU_MSCL0, "smmu_mscl0", "aclk400_mscl", GATE_IP_MSCL, 8, 0,
		0),
	GATE(CLK_SMMU_MSCL1, "smmu_mscl1", "aclk400_mscl", GATE_IP_MSCL, 9, 0,
		0),
	GATE(CLK_SMMU_MSCL2, "smmu_mscl2", "aclk400_mscl", GATE_IP_MSCL, 10, 0,
		0),
	GATE(CLK_SMMU_MIXER, "smmu_mixer", "aclk200_disp1", GATE_IP_DISP1, 9, 0,
		0),
};

static struct samsung_pll_clock exynos5420_plls[nr_plls] __initdata = {
	[apll] = PLL(pll_2550, CLK_FOUT_APLL, "fout_apll", "fin_pll", APLL_LOCK,
		APLL_CON0, NULL),
	[cpll] = PLL(pll_2550, CLK_FOUT_CPLL, "fout_cpll", "fin_pll", CPLL_LOCK,
		CPLL_CON0, NULL),
	[dpll] = PLL(pll_2550, CLK_FOUT_DPLL, "fout_dpll", "fin_pll", DPLL_LOCK,
		DPLL_CON0, NULL),
	[epll] = PLL(pll_2650, CLK_FOUT_EPLL, "fout_epll", "fin_pll", EPLL_LOCK,
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

static struct of_device_id ext_clk_match[] __initdata = {
	{ .compatible = "samsung,exynos5420-oscclk", .data = (void *)0, },
	{ },
};

/* register exynos5420 clocks */
static void __init exynos5420_clk_init(struct device_node *np)
{
	void __iomem *reg_base;

	if (np) {
		reg_base = of_iomap(np, 0);
		if (!reg_base)
			panic("%s: failed to map registers\n", __func__);
	} else {
		panic("%s: unable to determine soc\n", __func__);
	}

	samsung_clk_init(np, reg_base, CLK_NR_CLKS,
			exynos5420_clk_regs, ARRAY_SIZE(exynos5420_clk_regs),
			NULL, 0);
	samsung_clk_of_register_fixed_ext(exynos5420_fixed_rate_ext_clks,
			ARRAY_SIZE(exynos5420_fixed_rate_ext_clks),
			ext_clk_match);
	samsung_clk_register_pll(exynos5420_plls, ARRAY_SIZE(exynos5420_plls),
					reg_base);
	samsung_clk_register_fixed_rate(exynos5420_fixed_rate_clks,
			ARRAY_SIZE(exynos5420_fixed_rate_clks));
	samsung_clk_register_fixed_factor(exynos5420_fixed_factor_clks,
			ARRAY_SIZE(exynos5420_fixed_factor_clks));
	samsung_clk_register_mux(exynos5420_mux_clks,
			ARRAY_SIZE(exynos5420_mux_clks));
	samsung_clk_register_div(exynos5420_div_clks,
			ARRAY_SIZE(exynos5420_div_clks));
	samsung_clk_register_gate(exynos5420_gate_clks,
			ARRAY_SIZE(exynos5420_gate_clks));
}
CLK_OF_DECLARE(exynos5420_clk, "samsung,exynos5420-clock", exynos5420_clk_init);
