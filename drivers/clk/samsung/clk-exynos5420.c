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

enum exynos5420_clks {
	none,

	/* core clocks */
	fin_pll,  fout_apll, fout_cpll, fout_dpll, fout_epll, fout_rpll,
	fout_ipll, fout_spll, fout_vpll, fout_mpll, fout_bpll, fout_kpll,

	/* gate for special clocks (sclk) */
	sclk_uart0 = 128, sclk_uart1, sclk_uart2, sclk_uart3, sclk_mmc0,
	sclk_mmc1, sclk_mmc2, sclk_spi0, sclk_spi1, sclk_spi2, sclk_i2s1,
	sclk_i2s2, sclk_pcm1, sclk_pcm2, sclk_spdif, sclk_hdmi, sclk_pixel,
	sclk_dp1, sclk_mipi1, sclk_fimd1, sclk_maudio0, sclk_maupcm0,
	sclk_usbd300, sclk_usbd301, sclk_usbphy300, sclk_usbphy301, sclk_unipro,
	sclk_pwm, sclk_gscl_wa, sclk_gscl_wb, sclk_hdmiphy,

	/* gate clocks */
	aclk66_peric = 256, uart0, uart1, uart2, uart3, i2c0, i2c1, i2c2, i2c3,
	i2c4, i2c5, i2c6, i2c7, i2c_hdmi, tsadc, spi0, spi1, spi2, keyif, i2s1,
	i2s2, pcm1, pcm2, pwm, spdif, i2c8, i2c9, i2c10, aclk66_psgen = 300,
	chipid, sysreg, tzpc0, tzpc1, tzpc2, tzpc3, tzpc4, tzpc5, tzpc6, tzpc7,
	tzpc8, tzpc9, hdmi_cec, seckey, mct, wdt, rtc, tmu, tmu_gpu,
	pclk66_gpio = 330, aclk200_fsys2 = 350, mmc0, mmc1, mmc2, sromc, ufs,
	aclk200_fsys = 360, tsi, pdma0, pdma1, rtic, usbh20, usbd300, usbd301,
	aclk400_mscl = 380, mscl0, mscl1, mscl2, smmu_mscl0, smmu_mscl1,
	smmu_mscl2, aclk333 = 400, mfc, smmu_mfcl, smmu_mfcr,
	aclk200_disp1 = 410, dsim1, dp1, hdmi, aclk300_disp1 = 420, fimd1,
	smmu_fimd1, aclk166 = 430, mixer, aclk266 = 440, rotator, mdma1,
	smmu_rotator, smmu_mdma1, aclk300_jpeg = 450, jpeg, jpeg2, smmu_jpeg,
	aclk300_gscl = 460, smmu_gscl0, smmu_gscl1, gscl_wa, gscl_wb, gscl0,
	gscl1, clk_3aa, aclk266_g2d = 470, sss, slim_sss, mdma0,
	aclk333_g2d = 480, g2d, aclk333_432_gscl = 490, smmu_3aa, smmu_fimcl0,
	smmu_fimcl1, smmu_fimcl3, fimc_lite3, aclk_g3d = 500, g3d, smmu_mixer,

	nr_clks,
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
PNAME(hdmi_p)	= { "sclk_hdmiphy", "dout_hdmi_pixel" };
PNAME(maudio0_p)	= { "fin_pll", "maudio_clk", "sclk_dpll", "sclk_mpll",
			  "sclk_spll", "sclk_ipll", "sclk_epll", "sclk_rpll" };

/* fixed rate clocks generated outside the soc */
static struct samsung_fixed_rate_clock exynos5420_fixed_rate_ext_clks[] __initdata = {
	FRATE(fin_pll, "fin_pll", NULL, CLK_IS_ROOT, 0),
};

/* fixed rate clocks generated inside the soc */
static struct samsung_fixed_rate_clock exynos5420_fixed_rate_clks[] __initdata = {
	FRATE(sclk_hdmiphy, "sclk_hdmiphy", NULL, CLK_IS_ROOT, 24000000),
	FRATE(none, "sclk_pwi", NULL, CLK_IS_ROOT, 24000000),
	FRATE(none, "sclk_usbh20", NULL, CLK_IS_ROOT, 48000000),
	FRATE(none, "mphy_refclk_ixtal24", NULL, CLK_IS_ROOT, 48000000),
	FRATE(none, "sclk_usbh20_scan_clk", NULL, CLK_IS_ROOT, 480000000),
};

static struct samsung_fixed_factor_clock exynos5420_fixed_factor_clks[] __initdata = {
	FFACTOR(none, "sclk_hsic_12m", "fin_pll", 1, 2, 0),
};

static struct samsung_mux_clock exynos5420_mux_clks[] __initdata = {
	MUX(none, "mout_mspll_kfc", mspll_cpu_p, SRC_TOP7, 8, 2),
	MUX(none, "mout_mspll_cpu", mspll_cpu_p, SRC_TOP7, 12, 2),
	MUX(none, "mout_apll", apll_p, SRC_CPU, 0, 1),
	MUX(none, "mout_cpu", cpu_p, SRC_CPU, 16, 1),
	MUX(none, "mout_kpll", kpll_p, SRC_KFC, 0, 1),
	MUX(none, "mout_cpu_kfc", kfc_p, SRC_KFC, 16, 1),

	MUX(none, "sclk_bpll", bpll_p, SRC_CDREX, 0, 1),

	MUX_A(none, "mout_aclk400_mscl", group1_p,
			SRC_TOP0, 4, 2, "aclk400_mscl"),
	MUX(none, "mout_aclk200", group1_p, SRC_TOP0, 8, 2),
	MUX(none, "mout_aclk200_fsys2", group1_p, SRC_TOP0, 12, 2),
	MUX(none, "mout_aclk200_fsys", group1_p, SRC_TOP0, 28, 2),

	MUX(none, "mout_aclk333_432_gscl", group4_p, SRC_TOP1, 0, 2),
	MUX(none, "mout_aclk66", group1_p, SRC_TOP1, 8, 2),
	MUX(none, "mout_aclk266", group1_p, SRC_TOP1, 20, 2),
	MUX(none, "mout_aclk166", group1_p, SRC_TOP1, 24, 2),
	MUX(none, "mout_aclk333", group1_p, SRC_TOP1, 28, 2),

	MUX(none, "mout_aclk333_g2d", group1_p, SRC_TOP2, 8, 2),
	MUX(none, "mout_aclk266_g2d", group1_p, SRC_TOP2, 12, 2),
	MUX(none, "mout_aclk_g3d", group5_p, SRC_TOP2, 16, 1),
	MUX(none, "mout_aclk300_jpeg", group1_p, SRC_TOP2, 20, 2),
	MUX(none, "mout_aclk300_disp1", group1_p, SRC_TOP2, 24, 2),
	MUX(none, "mout_aclk300_gscl", group1_p, SRC_TOP2, 28, 2),

	MUX(none, "mout_user_aclk400_mscl", user_aclk400_mscl_p,
			SRC_TOP3, 4, 1),
	MUX_A(none, "mout_aclk200_disp1", aclk200_disp1_p,
			SRC_TOP3, 8, 1, "aclk200_disp1"),
	MUX(none, "mout_user_aclk200_fsys2", user_aclk200_fsys2_p,
			SRC_TOP3, 12, 1),
	MUX(none, "mout_user_aclk200_fsys", user_aclk200_fsys_p,
			SRC_TOP3, 28, 1),

	MUX(none, "mout_user_aclk333_432_gscl", user_aclk333_432_gscl_p,
			SRC_TOP4, 0, 1),
	MUX(none, "mout_aclk66_peric", aclk66_peric_p, SRC_TOP4, 8, 1),
	MUX(none, "mout_user_aclk266", user_aclk266_p, SRC_TOP4, 20, 1),
	MUX(none, "mout_user_aclk166", user_aclk166_p, SRC_TOP4, 24, 1),
	MUX(none, "mout_user_aclk333", user_aclk333_p, SRC_TOP4, 28, 1),

	MUX(none, "mout_aclk66_psgen", aclk66_peric_p, SRC_TOP5, 4, 1),
	MUX(none, "mout_user_aclk333_g2d", user_aclk333_g2d_p, SRC_TOP5, 8, 1),
	MUX(none, "mout_user_aclk266_g2d", user_aclk266_g2d_p, SRC_TOP5, 12, 1),
	MUX_A(none, "mout_user_aclk_g3d", user_aclk_g3d_p,
			SRC_TOP5, 16, 1, "aclkg3d"),
	MUX(none, "mout_user_aclk300_jpeg", user_aclk300_jpeg_p,
			SRC_TOP5, 20, 1),
	MUX(none, "mout_user_aclk300_disp1", user_aclk300_disp1_p,
			SRC_TOP5, 24, 1),
	MUX(none, "mout_user_aclk300_gscl", user_aclk300_gscl_p,
			SRC_TOP5, 28, 1),

	MUX(none, "sclk_mpll", mpll_p, SRC_TOP6, 0, 1),
	MUX(none, "sclk_vpll", vpll_p, SRC_TOP6, 4, 1),
	MUX(none, "sclk_spll", spll_p, SRC_TOP6, 8, 1),
	MUX(none, "sclk_ipll", ipll_p, SRC_TOP6, 12, 1),
	MUX(none, "sclk_rpll", rpll_p, SRC_TOP6, 16, 1),
	MUX(none, "sclk_epll", epll_p, SRC_TOP6, 20, 1),
	MUX(none, "sclk_dpll", dpll_p, SRC_TOP6, 24, 1),
	MUX(none, "sclk_cpll", cpll_p, SRC_TOP6, 28, 1),

	MUX(none, "mout_sw_aclk400_mscl", sw_aclk400_mscl_p, SRC_TOP10, 4, 1),
	MUX(none, "mout_sw_aclk200", sw_aclk200_p, SRC_TOP10, 8, 1),
	MUX(none, "mout_sw_aclk200_fsys2", sw_aclk200_fsys2_p,
			SRC_TOP10, 12, 1),
	MUX(none, "mout_sw_aclk200_fsys", sw_aclk200_fsys_p, SRC_TOP10, 28, 1),

	MUX(none, "mout_sw_aclk333_432_gscl", sw_aclk333_432_gscl_p,
			SRC_TOP11, 0, 1),
	MUX(none, "mout_sw_aclk66", sw_aclk66_p, SRC_TOP11, 8, 1),
	MUX(none, "mout_sw_aclk266", sw_aclk266_p, SRC_TOP11, 20, 1),
	MUX(none, "mout_sw_aclk166", sw_aclk166_p, SRC_TOP11, 24, 1),
	MUX(none, "mout_sw_aclk333", sw_aclk333_p, SRC_TOP11, 28, 1),

	MUX(none, "mout_sw_aclk333_g2d", sw_aclk333_g2d_p, SRC_TOP12, 8, 1),
	MUX(none, "mout_sw_aclk266_g2d", sw_aclk266_g2d_p, SRC_TOP12, 12, 1),
	MUX(none, "mout_sw_aclk_g3d", sw_aclk_g3d_p, SRC_TOP12, 16, 1),
	MUX(none, "mout_sw_aclk300_jpeg", sw_aclk300_jpeg_p, SRC_TOP12, 20, 1),
	MUX(none, "mout_sw_aclk300_disp1", sw_aclk300_disp1_p,
			SRC_TOP12, 24, 1),
	MUX(none, "mout_sw_aclk300_gscl", sw_aclk300_gscl_p, SRC_TOP12, 28, 1),

	/* DISP1 Block */
	MUX(none, "mout_fimd1", group3_p, SRC_DISP10, 4, 1),
	MUX(none, "mout_mipi1", group2_p, SRC_DISP10, 16, 3),
	MUX(none, "mout_dp1", group2_p, SRC_DISP10, 20, 3),
	MUX(none, "mout_pixel", group2_p, SRC_DISP10, 24, 3),
	MUX(none, "mout_hdmi", hdmi_p, SRC_DISP10, 28, 1),

	/* MAU Block */
	MUX(none, "mout_maudio0", maudio0_p, SRC_MAU, 28, 3),

	/* FSYS Block */
	MUX(none, "mout_usbd301", group2_p, SRC_FSYS, 4, 3),
	MUX(none, "mout_mmc0", group2_p, SRC_FSYS, 8, 3),
	MUX(none, "mout_mmc1", group2_p, SRC_FSYS, 12, 3),
	MUX(none, "mout_mmc2", group2_p, SRC_FSYS, 16, 3),
	MUX(none, "mout_usbd300", group2_p, SRC_FSYS, 20, 3),
	MUX(none, "mout_unipro", group2_p, SRC_FSYS, 24, 3),

	/* PERIC Block */
	MUX(none, "mout_uart0", group2_p, SRC_PERIC0, 4, 3),
	MUX(none, "mout_uart1", group2_p, SRC_PERIC0, 8, 3),
	MUX(none, "mout_uart2", group2_p, SRC_PERIC0, 12, 3),
	MUX(none, "mout_uart3", group2_p, SRC_PERIC0, 16, 3),
	MUX(none, "mout_pwm", group2_p, SRC_PERIC0, 24, 3),
	MUX(none, "mout_spdif", spdif_p, SRC_PERIC0, 28, 3),
	MUX(none, "mout_audio0", audio0_p, SRC_PERIC1, 8, 3),
	MUX(none, "mout_audio1", audio1_p, SRC_PERIC1, 12, 3),
	MUX(none, "mout_audio2", audio2_p, SRC_PERIC1, 16, 3),
	MUX(none, "mout_spi0", group2_p, SRC_PERIC1, 20, 3),
	MUX(none, "mout_spi1", group2_p, SRC_PERIC1, 24, 3),
	MUX(none, "mout_spi2", group2_p, SRC_PERIC1, 28, 3),
};

static struct samsung_div_clock exynos5420_div_clks[] __initdata = {
	DIV(none, "div_arm", "mout_cpu", DIV_CPU0, 0, 3),
	DIV(none, "sclk_apll", "mout_apll", DIV_CPU0, 24, 3),
	DIV(none, "armclk2", "div_arm", DIV_CPU0, 28, 3),
	DIV(none, "div_kfc", "mout_cpu_kfc", DIV_KFC0, 0, 3),
	DIV(none, "sclk_kpll", "mout_kpll", DIV_KFC0, 24, 3),

	DIV(none, "dout_aclk400_mscl", "mout_aclk400_mscl", DIV_TOP0, 4, 3),
	DIV(none, "dout_aclk200", "mout_aclk200", DIV_TOP0, 8, 3),
	DIV(none, "dout_aclk200_fsys2", "mout_aclk200_fsys2", DIV_TOP0, 12, 3),
	DIV(none, "dout_pclk200_fsys", "mout_pclk200_fsys", DIV_TOP0, 24, 3),
	DIV(none, "dout_aclk200_fsys", "mout_aclk200_fsys", DIV_TOP0, 28, 3),

	DIV(none, "dout_aclk333_432_gscl", "mout_aclk333_432_gscl",
			DIV_TOP1, 0, 3),
	DIV(none, "dout_aclk66", "mout_aclk66", DIV_TOP1, 8, 6),
	DIV(none, "dout_aclk266", "mout_aclk266", DIV_TOP1, 20, 3),
	DIV(none, "dout_aclk166", "mout_aclk166", DIV_TOP1, 24, 3),
	DIV(none, "dout_aclk333", "mout_aclk333", DIV_TOP1, 28, 3),

	DIV(none, "dout_aclk333_g2d", "mout_aclk333_g2d", DIV_TOP2, 8, 3),
	DIV(none, "dout_aclk266_g2d", "mout_aclk266_g2d", DIV_TOP2, 12, 3),
	DIV(none, "dout_aclk_g3d", "mout_aclk_g3d", DIV_TOP2, 16, 3),
	DIV(none, "dout_aclk300_jpeg", "mout_aclk300_jpeg", DIV_TOP2, 20, 3),
	DIV_A(none, "dout_aclk300_disp1", "mout_aclk300_disp1",
			DIV_TOP2, 24, 3, "aclk300_disp1"),
	DIV(none, "dout_aclk300_gscl", "mout_aclk300_gscl", DIV_TOP2, 28, 3),

	/* DISP1 Block */
	DIV(none, "dout_fimd1", "mout_fimd1", DIV_DISP10, 0, 4),
	DIV(none, "dout_mipi1", "mout_mipi1", DIV_DISP10, 16, 8),
	DIV(none, "dout_dp1", "mout_dp1", DIV_DISP10, 24, 4),
	DIV(none, "dout_hdmi_pixel", "mout_pixel", DIV_DISP10, 28, 4),

	/* Audio Block */
	DIV(none, "dout_maudio0", "mout_maudio0", DIV_MAU, 20, 4),
	DIV(none, "dout_maupcm0", "dout_maudio0", DIV_MAU, 24, 8),

	/* USB3.0 */
	DIV(none, "dout_usbphy301", "mout_usbd301", DIV_FSYS0, 12, 4),
	DIV(none, "dout_usbphy300", "mout_usbd300", DIV_FSYS0, 16, 4),
	DIV(none, "dout_usbd301", "mout_usbd301", DIV_FSYS0, 20, 4),
	DIV(none, "dout_usbd300", "mout_usbd300", DIV_FSYS0, 24, 4),

	/* MMC */
	DIV(none, "dout_mmc0", "mout_mmc0", DIV_FSYS1, 0, 10),
	DIV(none, "dout_mmc1", "mout_mmc1", DIV_FSYS1, 10, 10),
	DIV(none, "dout_mmc2", "mout_mmc2", DIV_FSYS1, 20, 10),

	DIV(none, "dout_unipro", "mout_unipro", DIV_FSYS2, 24, 8),

	/* UART and PWM */
	DIV(none, "dout_uart0", "mout_uart0", DIV_PERIC0, 8, 4),
	DIV(none, "dout_uart1", "mout_uart1", DIV_PERIC0, 12, 4),
	DIV(none, "dout_uart2", "mout_uart2", DIV_PERIC0, 16, 4),
	DIV(none, "dout_uart3", "mout_uart3", DIV_PERIC0, 20, 4),
	DIV(none, "dout_pwm", "mout_pwm", DIV_PERIC0, 28, 4),

	/* SPI */
	DIV(none, "dout_spi0", "mout_spi0", DIV_PERIC1, 20, 4),
	DIV(none, "dout_spi1", "mout_spi1", DIV_PERIC1, 24, 4),
	DIV(none, "dout_spi2", "mout_spi2", DIV_PERIC1, 28, 4),

	/* PCM */
	DIV(none, "dout_pcm1", "dout_audio1", DIV_PERIC2, 16, 8),
	DIV(none, "dout_pcm2", "dout_audio2", DIV_PERIC2, 24, 8),

	/* Audio - I2S */
	DIV(none, "dout_i2s1", "dout_audio1", DIV_PERIC3, 6, 6),
	DIV(none, "dout_i2s2", "dout_audio2", DIV_PERIC3, 12, 6),
	DIV(none, "dout_audio0", "mout_audio0", DIV_PERIC3, 20, 4),
	DIV(none, "dout_audio1", "mout_audio1", DIV_PERIC3, 24, 4),
	DIV(none, "dout_audio2", "mout_audio2", DIV_PERIC3, 28, 4),

	/* SPI Pre-Ratio */
	DIV(none, "dout_pre_spi0", "dout_spi0", DIV_PERIC4, 8, 8),
	DIV(none, "dout_pre_spi1", "dout_spi1", DIV_PERIC4, 16, 8),
	DIV(none, "dout_pre_spi2", "dout_spi2", DIV_PERIC4, 24, 8),
};

static struct samsung_gate_clock exynos5420_gate_clks[] __initdata = {
	/* TODO: Re-verify the CG bits for all the gate clocks */
	GATE_A(mct, "pclk_st", "aclk66_psgen", GATE_BUS_PERIS1, 2, 0, 0, "mct"),

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
	GATE(sclk_uart0, "sclk_uart0", "dout_uart0",
		GATE_TOP_SCLK_PERIC, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart1, "sclk_uart1", "dout_uart1",
		GATE_TOP_SCLK_PERIC, 1, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart2, "sclk_uart2", "dout_uart2",
		GATE_TOP_SCLK_PERIC, 2, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart3, "sclk_uart3", "dout_uart3",
		GATE_TOP_SCLK_PERIC, 3, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spi0, "sclk_spi0", "dout_pre_spi0",
		GATE_TOP_SCLK_PERIC, 6, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spi1, "sclk_spi1", "dout_pre_spi1",
		GATE_TOP_SCLK_PERIC, 7, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spi2, "sclk_spi2", "dout_pre_spi2",
		GATE_TOP_SCLK_PERIC, 8, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spdif, "sclk_spdif", "mout_spdif",
		GATE_TOP_SCLK_PERIC, 9, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_pwm, "sclk_pwm", "dout_pwm",
		GATE_TOP_SCLK_PERIC, 11, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_pcm1, "sclk_pcm1", "dout_pcm1",
		GATE_TOP_SCLK_PERIC, 15, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_pcm2, "sclk_pcm2", "dout_pcm2",
		GATE_TOP_SCLK_PERIC, 16, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_i2s1, "sclk_i2s1", "dout_i2s1",
		GATE_TOP_SCLK_PERIC, 17, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_i2s2, "sclk_i2s2", "dout_i2s2",
		GATE_TOP_SCLK_PERIC, 18, CLK_SET_RATE_PARENT, 0),

	GATE(sclk_mmc0, "sclk_mmc0", "dout_mmc0",
		GATE_TOP_SCLK_FSYS, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc1, "sclk_mmc1", "dout_mmc1",
		GATE_TOP_SCLK_FSYS, 1, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc2, "sclk_mmc2", "dout_mmc2",
		GATE_TOP_SCLK_FSYS, 2, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_usbphy301, "sclk_usbphy301", "dout_usbphy301",
		GATE_TOP_SCLK_FSYS, 7, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_usbphy300, "sclk_usbphy300", "dout_usbphy300",
		GATE_TOP_SCLK_FSYS, 8, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_usbd300, "sclk_usbd300", "dout_usbd300",
		GATE_TOP_SCLK_FSYS, 9, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_usbd301, "sclk_usbd301", "dout_usbd301",
		GATE_TOP_SCLK_FSYS, 10, CLK_SET_RATE_PARENT, 0),

	GATE(sclk_usbd301, "sclk_unipro", "dout_unipro",
		SRC_MASK_FSYS, 24, CLK_SET_RATE_PARENT, 0),

	GATE(sclk_gscl_wa, "sclk_gscl_wa", "aclK333_432_gscl",
		GATE_TOP_SCLK_GSCL, 6, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_gscl_wb, "sclk_gscl_wb", "aclk333_432_gscl",
		GATE_TOP_SCLK_GSCL, 7, CLK_SET_RATE_PARENT, 0),

	/* Display */
	GATE(sclk_fimd1, "sclk_fimd1", "dout_fimd1",
		GATE_TOP_SCLK_DISP1, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mipi1, "sclk_mipi1", "dout_mipi1",
		GATE_TOP_SCLK_DISP1, 3, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_hdmi, "sclk_hdmi", "mout_hdmi",
		GATE_TOP_SCLK_DISP1, 9, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_pixel, "sclk_pixel", "dout_hdmi_pixel",
		GATE_TOP_SCLK_DISP1, 10, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_dp1, "sclk_dp1", "dout_dp1",
		GATE_TOP_SCLK_DISP1, 20, CLK_SET_RATE_PARENT, 0),

	/* Maudio Block */
	GATE(sclk_maudio0, "sclk_maudio0", "dout_maudio0",
		GATE_TOP_SCLK_MAU, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_maupcm0, "sclk_maupcm0", "dout_maupcm0",
		GATE_TOP_SCLK_MAU, 1, CLK_SET_RATE_PARENT, 0),
	/* FSYS */
	GATE(tsi, "tsi", "aclk200_fsys", GATE_BUS_FSYS0, 0, 0, 0),
	GATE(pdma0, "pdma0", "aclk200_fsys", GATE_BUS_FSYS0, 1, 0, 0),
	GATE(pdma1, "pdma1", "aclk200_fsys", GATE_BUS_FSYS0, 2, 0, 0),
	GATE(ufs, "ufs", "aclk200_fsys2", GATE_BUS_FSYS0, 3, 0, 0),
	GATE(rtic, "rtic", "aclk200_fsys", GATE_BUS_FSYS0, 5, 0, 0),
	GATE(mmc0, "mmc0", "aclk200_fsys2", GATE_BUS_FSYS0, 12, 0, 0),
	GATE(mmc1, "mmc1", "aclk200_fsys2", GATE_BUS_FSYS0, 13, 0, 0),
	GATE(mmc2, "mmc2", "aclk200_fsys2", GATE_BUS_FSYS0, 14, 0, 0),
	GATE(sromc, "sromc", "aclk200_fsys2",
			GATE_BUS_FSYS0, 19, CLK_IGNORE_UNUSED, 0),
	GATE(usbh20, "usbh20", "aclk200_fsys", GATE_BUS_FSYS0, 20, 0, 0),
	GATE(usbd300, "usbd300", "aclk200_fsys", GATE_BUS_FSYS0, 21, 0, 0),
	GATE(usbd301, "usbd301", "aclk200_fsys", GATE_BUS_FSYS0, 28, 0, 0),

	/* UART */
	GATE(uart0, "uart0", "aclk66_peric", GATE_BUS_PERIC, 4, 0, 0),
	GATE(uart1, "uart1", "aclk66_peric", GATE_BUS_PERIC, 5, 0, 0),
	GATE_A(uart2, "uart2", "aclk66_peric",
		GATE_BUS_PERIC, 6, CLK_IGNORE_UNUSED, 0, "uart2"),
	GATE(uart3, "uart3", "aclk66_peric", GATE_BUS_PERIC, 7, 0, 0),
	/* I2C */
	GATE(i2c0, "i2c0", "aclk66_peric", GATE_BUS_PERIC, 9, 0, 0),
	GATE(i2c1, "i2c1", "aclk66_peric", GATE_BUS_PERIC, 10, 0, 0),
	GATE(i2c2, "i2c2", "aclk66_peric", GATE_BUS_PERIC, 11, 0, 0),
	GATE(i2c3, "i2c3", "aclk66_peric", GATE_BUS_PERIC, 12, 0, 0),
	GATE(i2c4, "i2c4", "aclk66_peric", GATE_BUS_PERIC, 13, 0, 0),
	GATE(i2c5, "i2c5", "aclk66_peric", GATE_BUS_PERIC, 14, 0, 0),
	GATE(i2c6, "i2c6", "aclk66_peric", GATE_BUS_PERIC, 15, 0, 0),
	GATE(i2c7, "i2c7", "aclk66_peric", GATE_BUS_PERIC, 16, 0, 0),
	GATE(i2c_hdmi, "i2c_hdmi", "aclk66_peric", GATE_BUS_PERIC, 17, 0, 0),
	GATE(tsadc, "tsadc", "aclk66_peric", GATE_BUS_PERIC, 18, 0, 0),
	/* SPI */
	GATE(spi0, "spi0", "aclk66_peric", GATE_BUS_PERIC, 19, 0, 0),
	GATE(spi1, "spi1", "aclk66_peric", GATE_BUS_PERIC, 20, 0, 0),
	GATE(spi2, "spi2", "aclk66_peric", GATE_BUS_PERIC, 21, 0, 0),
	GATE(keyif, "keyif", "aclk66_peric", GATE_BUS_PERIC, 22, 0, 0),
	/* I2S */
	GATE(i2s1, "i2s1", "aclk66_peric", GATE_BUS_PERIC, 23, 0, 0),
	GATE(i2s2, "i2s2", "aclk66_peric", GATE_BUS_PERIC, 24, 0, 0),
	/* PCM */
	GATE(pcm1, "pcm1", "aclk66_peric", GATE_BUS_PERIC, 25, 0, 0),
	GATE(pcm2, "pcm2", "aclk66_peric", GATE_BUS_PERIC, 26, 0, 0),
	/* PWM */
	GATE(pwm, "pwm", "aclk66_peric", GATE_BUS_PERIC, 27, 0, 0),
	/* SPDIF */
	GATE(spdif, "spdif", "aclk66_peric", GATE_BUS_PERIC, 29, 0, 0),

	GATE(i2c8, "i2c8", "aclk66_peric", GATE_BUS_PERIC1, 0, 0, 0),
	GATE(i2c9, "i2c9", "aclk66_peric", GATE_BUS_PERIC1, 1, 0, 0),
	GATE(i2c10, "i2c10", "aclk66_peric", GATE_BUS_PERIC1, 2, 0, 0),

	GATE(chipid, "chipid", "aclk66_psgen",
			GATE_BUS_PERIS0, 12, CLK_IGNORE_UNUSED, 0),
	GATE(sysreg, "sysreg", "aclk66_psgen",
			GATE_BUS_PERIS0, 13, CLK_IGNORE_UNUSED, 0),
	GATE(tzpc0, "tzpc0", "aclk66_psgen", GATE_BUS_PERIS0, 18, 0, 0),
	GATE(tzpc1, "tzpc1", "aclk66_psgen", GATE_BUS_PERIS0, 19, 0, 0),
	GATE(tzpc2, "tzpc2", "aclk66_psgen", GATE_BUS_PERIS0, 20, 0, 0),
	GATE(tzpc3, "tzpc3", "aclk66_psgen", GATE_BUS_PERIS0, 21, 0, 0),
	GATE(tzpc4, "tzpc4", "aclk66_psgen", GATE_BUS_PERIS0, 22, 0, 0),
	GATE(tzpc5, "tzpc5", "aclk66_psgen", GATE_BUS_PERIS0, 23, 0, 0),
	GATE(tzpc6, "tzpc6", "aclk66_psgen", GATE_BUS_PERIS0, 24, 0, 0),
	GATE(tzpc7, "tzpc7", "aclk66_psgen", GATE_BUS_PERIS0, 25, 0, 0),
	GATE(tzpc8, "tzpc8", "aclk66_psgen", GATE_BUS_PERIS0, 26, 0, 0),
	GATE(tzpc9, "tzpc9", "aclk66_psgen", GATE_BUS_PERIS0, 27, 0, 0),

	GATE(hdmi_cec, "hdmi_cec", "aclk66_psgen", GATE_BUS_PERIS1, 0, 0, 0),
	GATE(seckey, "seckey", "aclk66_psgen", GATE_BUS_PERIS1, 1, 0, 0),
	GATE(wdt, "wdt", "aclk66_psgen", GATE_BUS_PERIS1, 3, 0, 0),
	GATE(rtc, "rtc", "aclk66_psgen", GATE_BUS_PERIS1, 4, 0, 0),
	GATE(tmu, "tmu", "aclk66_psgen", GATE_BUS_PERIS1, 5, 0, 0),
	GATE(tmu_gpu, "tmu_gpu", "aclk66_psgen", GATE_BUS_PERIS1, 6, 0, 0),

	GATE(gscl0, "gscl0", "aclk300_gscl", GATE_IP_GSCL0, 0, 0, 0),
	GATE(gscl1, "gscl1", "aclk300_gscl", GATE_IP_GSCL0, 1, 0, 0),
	GATE(clk_3aa, "clk_3aa", "aclk300_gscl", GATE_IP_GSCL0, 4, 0, 0),

	GATE(smmu_3aa, "smmu_3aa", "aclk333_432_gscl", GATE_IP_GSCL1, 2, 0, 0),
	GATE(smmu_fimcl0, "smmu_fimcl0", "aclk333_432_gscl",
			GATE_IP_GSCL1, 3, 0, 0),
	GATE(smmu_fimcl1, "smmu_fimcl1", "aclk333_432_gscl",
			GATE_IP_GSCL1, 4, 0, 0),
	GATE(smmu_gscl0, "smmu_gscl0", "aclk300_gscl", GATE_IP_GSCL1, 6, 0, 0),
	GATE(smmu_gscl1, "smmu_gscl1", "aclk300_gscl", GATE_IP_GSCL1, 7, 0, 0),
	GATE(gscl_wa, "gscl_wa", "aclk300_gscl", GATE_IP_GSCL1, 12, 0, 0),
	GATE(gscl_wb, "gscl_wb", "aclk300_gscl", GATE_IP_GSCL1, 13, 0, 0),
	GATE(smmu_fimcl3, "smmu_fimcl3,", "aclk333_432_gscl",
			GATE_IP_GSCL1, 16, 0, 0),
	GATE(fimc_lite3, "fimc_lite3", "aclk333_432_gscl",
			GATE_IP_GSCL1, 17, 0, 0),

	GATE(fimd1, "fimd1", "aclk300_disp1", GATE_IP_DISP1, 0, 0, 0),
	GATE(dsim1, "dsim1", "aclk200_disp1", GATE_IP_DISP1, 3, 0, 0),
	GATE(dp1, "dp1", "aclk200_disp1", GATE_IP_DISP1, 4, 0, 0),
	GATE(mixer, "mixer", "aclk166", GATE_IP_DISP1, 5, 0, 0),
	GATE(hdmi, "hdmi", "aclk200_disp1", GATE_IP_DISP1, 6, 0, 0),
	GATE(smmu_fimd1, "smmu_fimd1", "aclk300_disp1", GATE_IP_DISP1, 8, 0, 0),

	GATE(mfc, "mfc", "aclk333", GATE_IP_MFC, 0, 0, 0),
	GATE(smmu_mfcl, "smmu_mfcl", "aclk333", GATE_IP_MFC, 1, 0, 0),
	GATE(smmu_mfcr, "smmu_mfcr", "aclk333", GATE_IP_MFC, 2, 0, 0),

	GATE(g3d, "g3d", "aclkg3d", GATE_IP_G3D, 9, 0, 0),

	GATE(rotator, "rotator", "aclk266", GATE_IP_GEN, 1, 0, 0),
	GATE(jpeg, "jpeg", "aclk300_jpeg", GATE_IP_GEN, 2, 0, 0),
	GATE(jpeg2, "jpeg2", "aclk300_jpeg", GATE_IP_GEN, 3, 0, 0),
	GATE(mdma1, "mdma1", "aclk266", GATE_IP_GEN, 4, 0, 0),
	GATE(smmu_rotator, "smmu_rotator", "aclk266", GATE_IP_GEN, 6, 0, 0),
	GATE(smmu_jpeg, "smmu_jpeg", "aclk300_jpeg", GATE_IP_GEN, 7, 0, 0),
	GATE(smmu_mdma1, "smmu_mdma1", "aclk266", GATE_IP_GEN, 9, 0, 0),

	GATE(mscl0, "mscl0", "aclk400_mscl", GATE_IP_MSCL, 0, 0, 0),
	GATE(mscl1, "mscl1", "aclk400_mscl", GATE_IP_MSCL, 1, 0, 0),
	GATE(mscl2, "mscl2", "aclk400_mscl", GATE_IP_MSCL, 2, 0, 0),
	GATE(smmu_mscl0, "smmu_mscl0", "aclk400_mscl", GATE_IP_MSCL, 8, 0, 0),
	GATE(smmu_mscl1, "smmu_mscl1", "aclk400_mscl", GATE_IP_MSCL, 9, 0, 0),
	GATE(smmu_mscl2, "smmu_mscl2", "aclk400_mscl", GATE_IP_MSCL, 10, 0, 0),
	GATE(smmu_mixer, "smmu_mixer", "aclk200_disp1", GATE_IP_DISP1, 9, 0, 0),
};

static struct samsung_pll_clock exynos5420_plls[nr_plls] __initdata = {
	[apll] = PLL(pll_2550, fout_apll, "fout_apll", "fin_pll", APLL_LOCK,
		APLL_CON0, NULL),
	[cpll] = PLL(pll_2550, fout_mpll, "fout_mpll", "fin_pll", MPLL_LOCK,
		MPLL_CON0, NULL),
	[dpll] = PLL(pll_2550, fout_dpll, "fout_dpll", "fin_pll", DPLL_LOCK,
		DPLL_CON0, NULL),
	[epll] = PLL(pll_2650, fout_epll, "fout_epll", "fin_pll", EPLL_LOCK,
		EPLL_CON0, NULL),
	[rpll] = PLL(pll_2650, fout_rpll, "fout_rpll", "fin_pll", RPLL_LOCK,
		RPLL_CON0, NULL),
	[ipll] = PLL(pll_2550, fout_ipll, "fout_ipll", "fin_pll", IPLL_LOCK,
		IPLL_CON0, NULL),
	[spll] = PLL(pll_2550, fout_spll, "fout_spll", "fin_pll", SPLL_LOCK,
		SPLL_CON0, NULL),
	[vpll] = PLL(pll_2550, fout_vpll, "fout_vpll", "fin_pll", VPLL_LOCK,
		VPLL_CON0, NULL),
	[mpll] = PLL(pll_2550, fout_mpll, "fout_mpll", "fin_pll", MPLL_LOCK,
		MPLL_CON0, NULL),
	[bpll] = PLL(pll_2550, fout_bpll, "fout_bpll", "fin_pll", BPLL_LOCK,
		BPLL_CON0, NULL),
	[kpll] = PLL(pll_2550, fout_kpll, "fout_kpll", "fin_pll", KPLL_LOCK,
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

	samsung_clk_init(np, reg_base, nr_clks,
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
