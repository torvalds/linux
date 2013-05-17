/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for Exynos5250 SoC.
*/

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk.h"
#include "clk-pll.h"

#define SRC_CPU			0x200
#define DIV_CPU0		0x500
#define SRC_CORE1		0x4204
#define SRC_TOP0		0x10210
#define SRC_TOP2		0x10218
#define SRC_GSCL		0x10220
#define SRC_DISP1_0		0x1022c
#define SRC_MAU			0x10240
#define SRC_FSYS		0x10244
#define SRC_GEN			0x10248
#define SRC_PERIC0		0x10250
#define SRC_PERIC1		0x10254
#define SRC_MASK_GSCL		0x10320
#define SRC_MASK_DISP1_0	0x1032c
#define SRC_MASK_MAU		0x10334
#define SRC_MASK_FSYS		0x10340
#define SRC_MASK_GEN		0x10344
#define SRC_MASK_PERIC0		0x10350
#define SRC_MASK_PERIC1		0x10354
#define DIV_TOP0		0x10510
#define DIV_TOP1		0x10514
#define DIV_GSCL		0x10520
#define DIV_DISP1_0		0x1052c
#define DIV_GEN			0x1053c
#define DIV_MAU			0x10544
#define DIV_FSYS0		0x10548
#define DIV_FSYS1		0x1054c
#define DIV_FSYS2		0x10550
#define DIV_PERIC0		0x10558
#define DIV_PERIC1		0x1055c
#define DIV_PERIC2		0x10560
#define DIV_PERIC3		0x10564
#define DIV_PERIC4		0x10568
#define DIV_PERIC5		0x1056c
#define GATE_IP_GSCL		0x10920
#define GATE_IP_MFC		0x1092c
#define GATE_IP_GEN		0x10934
#define GATE_IP_FSYS		0x10944
#define GATE_IP_PERIC		0x10950
#define GATE_IP_PERIS		0x10960
#define SRC_CDREX		0x20200
#define PLL_DIV2_SEL		0x20a24
#define GATE_IP_DISP1		0x10928

/*
 * Let each supported clock get a unique id. This id is used to lookup the clock
 * for device tree based platforms. The clocks are categorized into three
 * sections: core, sclk gate and bus interface gate clocks.
 *
 * When adding a new clock to this list, it is advised to choose a clock
 * category and add it to the end of that category. That is because the the
 * device tree source file is referring to these ids and any change in the
 * sequence number of existing clocks will require corresponding change in the
 * device tree files. This limitation would go away when pre-processor support
 * for dtc would be available.
 */
enum exynos5250_clks {
	none,

	/* core clocks */
	fin_pll,

	/* gate for special clocks (sclk) */
	sclk_cam_bayer = 128, sclk_cam0, sclk_cam1, sclk_gscl_wa, sclk_gscl_wb,
	sclk_fimd1, sclk_mipi1, sclk_dp, sclk_hdmi, sclk_pixel, sclk_audio0,
	sclk_mmc0, sclk_mmc1, sclk_mmc2, sclk_mmc3, sclk_sata, sclk_usb3,
	sclk_jpeg, sclk_uart0, sclk_uart1, sclk_uart2, sclk_uart3, sclk_pwm,
	sclk_audio1, sclk_audio2, sclk_spdif, sclk_spi0, sclk_spi1, sclk_spi2,

	/* gate clocks */
	gscl0 = 256, gscl1, gscl2, gscl3, gscl_wa, gscl_wb, smmu_gscl0,
	smmu_gscl1, smmu_gscl2, smmu_gscl3, mfc, smmu_mfcl, smmu_mfcr, rotator,
	jpeg, mdma1, smmu_rotator, smmu_jpeg, smmu_mdma1, pdma0, pdma1, sata,
	usbotg, mipi_hsi, sdmmc0, sdmmc1, sdmmc2, sdmmc3, sromc, usb2, usb3,
	sata_phyctrl, sata_phyi2c, uart0, uart1, uart2,	uart3, uart4, i2c0,
	i2c1, i2c2, i2c3, i2c4, i2c5, i2c6, i2c7, i2c_hdmi, adc, spi0, spi1,
	spi2, i2s1, i2s2, pcm1, pcm2, pwm, spdif, ac97, hsi2c0, hsi2c1, hsi2c2,
	hsi2c3, chipid, sysreg, pmu, cmu_top, cmu_core, cmu_mem, tzpc0, tzpc1,
	tzpc2, tzpc3, tzpc4, tzpc5, tzpc6, tzpc7, tzpc8, tzpc9, hdmi_cec, mct,
	wdt, rtc, tmu, fimd1, mie1, dsim0, dp, mixer, hdmi,

	nr_clks,
};

/*
 * list of controller registers to be saved and restored during a
 * suspend/resume cycle.
 */
static __initdata unsigned long exynos5250_clk_regs[] = {
	SRC_CPU,
	DIV_CPU0,
	SRC_CORE1,
	SRC_TOP0,
	SRC_TOP2,
	SRC_GSCL,
	SRC_DISP1_0,
	SRC_MAU,
	SRC_FSYS,
	SRC_GEN,
	SRC_PERIC0,
	SRC_PERIC1,
	SRC_MASK_GSCL,
	SRC_MASK_DISP1_0,
	SRC_MASK_MAU,
	SRC_MASK_FSYS,
	SRC_MASK_GEN,
	SRC_MASK_PERIC0,
	SRC_MASK_PERIC1,
	DIV_TOP0,
	DIV_TOP1,
	DIV_GSCL,
	DIV_DISP1_0,
	DIV_GEN,
	DIV_MAU,
	DIV_FSYS0,
	DIV_FSYS1,
	DIV_FSYS2,
	DIV_PERIC0,
	DIV_PERIC1,
	DIV_PERIC2,
	DIV_PERIC3,
	DIV_PERIC4,
	DIV_PERIC5,
	GATE_IP_GSCL,
	GATE_IP_MFC,
	GATE_IP_GEN,
	GATE_IP_FSYS,
	GATE_IP_PERIC,
	GATE_IP_PERIS,
	SRC_CDREX,
	PLL_DIV2_SEL,
	GATE_IP_DISP1,
};

/* list of all parent clock list */
PNAME(mout_apll_p)	= { "fin_pll", "fout_apll", };
PNAME(mout_cpu_p)	= { "mout_apll", "sclk_mpll", };
PNAME(mout_mpll_fout_p)	= { "fout_mplldiv2", "fout_mpll" };
PNAME(mout_mpll_p)	= { "fin_pll", "mout_mpll_fout" };
PNAME(mout_bpll_fout_p)	= { "fout_bplldiv2", "fout_bpll" };
PNAME(mout_bpll_p)	= { "fin_pll", "mout_bpll_fout" };
PNAME(mout_vpllsrc_p)	= { "fin_pll", "sclk_hdmi27m" };
PNAME(mout_vpll_p)	= { "mout_vpllsrc", "fout_vpll" };
PNAME(mout_cpll_p)	= { "fin_pll", "fout_cpll" };
PNAME(mout_epll_p)	= { "fin_pll", "fout_epll" };
PNAME(mout_mpll_user_p)	= { "fin_pll", "sclk_mpll" };
PNAME(mout_bpll_user_p)	= { "fin_pll", "sclk_bpll" };
PNAME(mout_aclk166_p)	= { "sclk_cpll", "sclk_mpll_user" };
PNAME(mout_aclk200_p)	= { "sclk_mpll_user", "sclk_bpll_user" };
PNAME(mout_hdmi_p)	= { "div_hdmi_pixel", "sclk_hdmiphy" };
PNAME(mout_usb3_p)	= { "sclk_mpll_user", "sclk_cpll" };
PNAME(mout_group1_p)	= { "fin_pll", "fin_pll", "sclk_hdmi27m",
				"sclk_dptxphy", "sclk_uhostphy", "sclk_hdmiphy",
				"sclk_mpll_user", "sclk_epll", "sclk_vpll",
				"sclk_cpll" };
PNAME(mout_audio0_p)	= { "cdclk0", "fin_pll", "sclk_hdmi27m", "sclk_dptxphy",
				"sclk_uhostphy", "sclk_hdmiphy",
				"sclk_mpll_user", "sclk_epll", "sclk_vpll",
				"sclk_cpll" };
PNAME(mout_audio1_p)	= { "cdclk1", "fin_pll", "sclk_hdmi27m", "sclk_dptxphy",
				"sclk_uhostphy", "sclk_hdmiphy",
				"sclk_mpll_user", "sclk_epll", "sclk_vpll",
				"sclk_cpll" };
PNAME(mout_audio2_p)	= { "cdclk2", "fin_pll", "sclk_hdmi27m", "sclk_dptxphy",
				"sclk_uhostphy", "sclk_hdmiphy",
				"sclk_mpll_user", "sclk_epll", "sclk_vpll",
				"sclk_cpll" };
PNAME(mout_spdif_p)	= { "sclk_audio0", "sclk_audio1", "sclk_audio2",
				"spdif_extclk" };

/* fixed rate clocks generated outside the soc */
struct samsung_fixed_rate_clock exynos5250_fixed_rate_ext_clks[] __initdata = {
	FRATE(fin_pll, "fin_pll", NULL, CLK_IS_ROOT, 0),
};

/* fixed rate clocks generated inside the soc */
struct samsung_fixed_rate_clock exynos5250_fixed_rate_clks[] __initdata = {
	FRATE(none, "sclk_hdmiphy", NULL, CLK_IS_ROOT, 24000000),
	FRATE(none, "sclk_hdmi27m", NULL, CLK_IS_ROOT, 27000000),
	FRATE(none, "sclk_dptxphy", NULL, CLK_IS_ROOT, 24000000),
	FRATE(none, "sclk_uhostphy", NULL, CLK_IS_ROOT, 48000000),
};

struct samsung_fixed_factor_clock exynos5250_fixed_factor_clks[] __initdata = {
	FFACTOR(none, "fout_mplldiv2", "fout_mpll", 1, 2, 0),
	FFACTOR(none, "fout_bplldiv2", "fout_bpll", 1, 2, 0),
};

struct samsung_mux_clock exynos5250_mux_clks[] __initdata = {
	MUX_A(none, "mout_apll", mout_apll_p, SRC_CPU, 0, 1, "mout_apll"),
	MUX_A(none, "mout_cpu", mout_cpu_p, SRC_CPU, 16, 1, "mout_cpu"),
	MUX(none, "mout_mpll_fout", mout_mpll_fout_p, PLL_DIV2_SEL, 4, 1),
	MUX_A(none, "sclk_mpll", mout_mpll_p, SRC_CORE1, 8, 1, "mout_mpll"),
	MUX(none, "mout_bpll_fout", mout_bpll_fout_p, PLL_DIV2_SEL, 0, 1),
	MUX(none, "sclk_bpll", mout_bpll_p, SRC_CDREX, 0, 1),
	MUX(none, "mout_vpllsrc", mout_vpllsrc_p, SRC_TOP2, 0, 1),
	MUX(none, "sclk_vpll", mout_vpll_p, SRC_TOP2, 16, 1),
	MUX(none, "sclk_epll", mout_epll_p, SRC_TOP2, 12, 1),
	MUX(none, "sclk_cpll", mout_cpll_p, SRC_TOP2, 8, 1),
	MUX(none, "sclk_mpll_user", mout_mpll_user_p, SRC_TOP2, 20, 1),
	MUX(none, "sclk_bpll_user", mout_bpll_user_p, SRC_TOP2, 24, 1),
	MUX(none, "mout_aclk166", mout_aclk166_p, SRC_TOP0, 8, 1),
	MUX(none, "mout_aclk333", mout_aclk166_p, SRC_TOP0, 16, 1),
	MUX(none, "mout_aclk200", mout_aclk200_p, SRC_TOP0, 12, 1),
	MUX(none, "mout_cam_bayer", mout_group1_p, SRC_GSCL, 12, 4),
	MUX(none, "mout_cam0", mout_group1_p, SRC_GSCL, 16, 4),
	MUX(none, "mout_cam1", mout_group1_p, SRC_GSCL, 20, 4),
	MUX(none, "mout_gscl_wa", mout_group1_p, SRC_GSCL, 24, 4),
	MUX(none, "mout_gscl_wb", mout_group1_p, SRC_GSCL, 28, 4),
	MUX(none, "mout_fimd1", mout_group1_p, SRC_DISP1_0, 0, 4),
	MUX(none, "mout_mipi1", mout_group1_p, SRC_DISP1_0, 12, 4),
	MUX(none, "mout_dp", mout_group1_p, SRC_DISP1_0, 16, 4),
	MUX(none, "mout_hdmi", mout_hdmi_p, SRC_DISP1_0, 20, 1),
	MUX(none, "mout_audio0", mout_audio0_p, SRC_MAU, 0, 4),
	MUX(none, "mout_mmc0", mout_group1_p, SRC_FSYS, 0, 4),
	MUX(none, "mout_mmc1", mout_group1_p, SRC_FSYS, 4, 4),
	MUX(none, "mout_mmc2", mout_group1_p, SRC_FSYS, 8, 4),
	MUX(none, "mout_mmc3", mout_group1_p, SRC_FSYS, 12, 4),
	MUX(none, "mout_sata", mout_aclk200_p, SRC_FSYS, 24, 1),
	MUX(none, "mout_usb3", mout_usb3_p, SRC_FSYS, 28, 1),
	MUX(none, "mout_jpeg", mout_group1_p, SRC_GEN, 0, 4),
	MUX(none, "mout_uart0", mout_group1_p, SRC_PERIC0, 0, 4),
	MUX(none, "mout_uart1", mout_group1_p, SRC_PERIC0, 4, 4),
	MUX(none, "mout_uart2", mout_group1_p, SRC_PERIC0, 8, 4),
	MUX(none, "mout_uart3", mout_group1_p, SRC_PERIC0, 12, 4),
	MUX(none, "mout_pwm", mout_group1_p, SRC_PERIC0, 24, 4),
	MUX(none, "mout_audio1", mout_audio1_p, SRC_PERIC1, 0, 4),
	MUX(none, "mout_audio2", mout_audio2_p, SRC_PERIC1, 4, 4),
	MUX(none, "mout_spdif", mout_spdif_p, SRC_PERIC1, 8, 2),
	MUX(none, "mout_spi0", mout_group1_p, SRC_PERIC1, 16, 4),
	MUX(none, "mout_spi1", mout_group1_p, SRC_PERIC1, 20, 4),
	MUX(none, "mout_spi2", mout_group1_p, SRC_PERIC1, 24, 4),
};

struct samsung_div_clock exynos5250_div_clks[] __initdata = {
	DIV(none, "div_arm", "mout_cpu", DIV_CPU0, 0, 3),
	DIV(none, "sclk_apll", "mout_apll", DIV_CPU0, 24, 3),
	DIV(none, "aclk66_pre", "sclk_mpll_user", DIV_TOP1, 24, 3),
	DIV(none, "aclk66", "aclk66_pre", DIV_TOP0, 0, 3),
	DIV(none, "aclk266", "sclk_mpll_user", DIV_TOP0, 16, 3),
	DIV(none, "aclk166", "mout_aclk166", DIV_TOP0, 8, 3),
	DIV(none, "aclk333", "mout_aclk333", DIV_TOP0, 20, 3),
	DIV(none, "aclk200", "mout_aclk200", DIV_TOP0, 12, 3),
	DIV(none, "div_cam_bayer", "mout_cam_bayer", DIV_GSCL, 12, 4),
	DIV(none, "div_cam0", "mout_cam0", DIV_GSCL, 16, 4),
	DIV(none, "div_cam1", "mout_cam1", DIV_GSCL, 20, 4),
	DIV(none, "div_gscl_wa", "mout_gscl_wa", DIV_GSCL, 24, 4),
	DIV(none, "div_gscl_wb", "mout_gscl_wb", DIV_GSCL, 28, 4),
	DIV(none, "div_fimd1", "mout_fimd1", DIV_DISP1_0, 0, 4),
	DIV(none, "div_mipi1", "mout_mipi1", DIV_DISP1_0, 16, 4),
	DIV(none, "div_dp", "mout_dp", DIV_DISP1_0, 24, 4),
	DIV(none, "div_jpeg", "mout_jpeg", DIV_GEN, 4, 4),
	DIV(none, "div_audio0", "mout_audio0", DIV_MAU, 0, 4),
	DIV(none, "div_pcm0", "sclk_audio0", DIV_MAU, 4, 8),
	DIV(none, "div_sata", "mout_sata", DIV_FSYS0, 20, 4),
	DIV(none, "div_usb3", "mout_usb3", DIV_FSYS0, 24, 4),
	DIV(none, "div_mmc0", "mout_mmc0", DIV_FSYS1, 0, 4),
	DIV(none, "div_mmc1", "mout_mmc1", DIV_FSYS1, 16, 4),
	DIV(none, "div_mmc2", "mout_mmc2", DIV_FSYS2, 0, 4),
	DIV(none, "div_mmc3", "mout_mmc3", DIV_FSYS2, 16, 4),
	DIV(none, "div_uart0", "mout_uart0", DIV_PERIC0, 0, 4),
	DIV(none, "div_uart1", "mout_uart1", DIV_PERIC0, 4, 4),
	DIV(none, "div_uart2", "mout_uart2", DIV_PERIC0, 8, 4),
	DIV(none, "div_uart3", "mout_uart3", DIV_PERIC0, 12, 4),
	DIV(none, "div_spi0", "mout_spi0", DIV_PERIC1, 0, 4),
	DIV(none, "div_spi1", "mout_spi1", DIV_PERIC1, 16, 4),
	DIV(none, "div_spi2", "mout_spi2", DIV_PERIC2, 0, 4),
	DIV(none, "div_pwm", "mout_pwm", DIV_PERIC3, 0, 4),
	DIV(none, "div_audio1", "mout_audio1", DIV_PERIC4, 0, 4),
	DIV(none, "div_pcm1", "sclk_audio1", DIV_PERIC4, 4, 8),
	DIV(none, "div_audio2", "mout_audio2", DIV_PERIC4, 16, 4),
	DIV(none, "div_pcm2", "sclk_audio2", DIV_PERIC4, 20, 8),
	DIV(none, "div_i2s1", "sclk_audio1", DIV_PERIC5, 0, 6),
	DIV(none, "div_i2s2", "sclk_audio2", DIV_PERIC5, 8, 6),
	DIV(sclk_pixel, "div_hdmi_pixel", "sclk_vpll", DIV_DISP1_0, 28, 4),
	DIV_A(none, "armclk", "div_arm", DIV_CPU0, 28, 3, "armclk"),
	DIV_F(none, "div_mipi1_pre", "div_mipi1",
			DIV_DISP1_0, 20, 4, CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_mmc_pre0", "div_mmc0",
			DIV_FSYS1, 8, 8, CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_mmc_pre1", "div_mmc1",
			DIV_FSYS1, 24, 8, CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_mmc_pre2", "div_mmc2",
			DIV_FSYS2, 8, 8, CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_mmc_pre3", "div_mmc3",
			DIV_FSYS2, 24, 8, CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_spi_pre0", "div_spi0",
			DIV_PERIC1, 8, 8, CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_spi_pre1", "div_spi1",
			DIV_PERIC1, 24, 8, CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_spi_pre2", "div_spi2",
			DIV_PERIC2, 8, 8, CLK_SET_RATE_PARENT, 0),
};

struct samsung_gate_clock exynos5250_gate_clks[] __initdata = {
	GATE(gscl0, "gscl0", "none", GATE_IP_GSCL, 0, 0, 0),
	GATE(gscl1, "gscl1", "none", GATE_IP_GSCL, 1, 0, 0),
	GATE(gscl2, "gscl2", "aclk266", GATE_IP_GSCL, 2, 0, 0),
	GATE(gscl3, "gscl3", "aclk266", GATE_IP_GSCL, 3, 0, 0),
	GATE(gscl_wa, "gscl_wa", "div_gscl_wa", GATE_IP_GSCL, 5, 0, 0),
	GATE(gscl_wb, "gscl_wb", "div_gscl_wb", GATE_IP_GSCL, 6, 0, 0),
	GATE(smmu_gscl0, "smmu_gscl0", "aclk266", GATE_IP_GSCL, 7, 0, 0),
	GATE(smmu_gscl1, "smmu_gscl1", "aclk266", GATE_IP_GSCL, 8, 0, 0),
	GATE(smmu_gscl2, "smmu_gscl2", "aclk266", GATE_IP_GSCL, 9, 0, 0),
	GATE(smmu_gscl3, "smmu_gscl3", "aclk266", GATE_IP_GSCL, 10, 0, 0),
	GATE(mfc, "mfc", "aclk333", GATE_IP_MFC, 0, 0, 0),
	GATE(smmu_mfcl, "smmu_mfcl", "aclk333", GATE_IP_MFC, 1, 0, 0),
	GATE(smmu_mfcr, "smmu_mfcr", "aclk333", GATE_IP_MFC, 2, 0, 0),
	GATE(rotator, "rotator", "aclk266", GATE_IP_GEN, 1, 0, 0),
	GATE(jpeg, "jpeg", "aclk166", GATE_IP_GEN, 2, 0, 0),
	GATE(mdma1, "mdma1", "aclk266", GATE_IP_GEN, 4, 0, 0),
	GATE(smmu_rotator, "smmu_rotator", "aclk266", GATE_IP_GEN, 6, 0, 0),
	GATE(smmu_jpeg, "smmu_jpeg", "aclk166", GATE_IP_GEN, 7, 0, 0),
	GATE(smmu_mdma1, "smmu_mdma1", "aclk266", GATE_IP_GEN, 9, 0, 0),
	GATE(pdma0, "pdma0", "aclk200", GATE_IP_FSYS, 1, 0, 0),
	GATE(pdma1, "pdma1", "aclk200", GATE_IP_FSYS, 2, 0, 0),
	GATE(sata, "sata", "aclk200", GATE_IP_FSYS, 6, 0, 0),
	GATE(usbotg, "usbotg", "aclk200", GATE_IP_FSYS, 7, 0, 0),
	GATE(mipi_hsi, "mipi_hsi", "aclk200", GATE_IP_FSYS, 8, 0, 0),
	GATE(sdmmc0, "sdmmc0", "aclk200", GATE_IP_FSYS, 12, 0, 0),
	GATE(sdmmc1, "sdmmc1", "aclk200", GATE_IP_FSYS, 13, 0, 0),
	GATE(sdmmc2, "sdmmc2", "aclk200", GATE_IP_FSYS, 14, 0, 0),
	GATE(sdmmc3, "sdmmc3", "aclk200", GATE_IP_FSYS, 15, 0, 0),
	GATE(sromc, "sromc", "aclk200", GATE_IP_FSYS, 17, 0, 0),
	GATE(usb2, "usb2", "aclk200", GATE_IP_FSYS, 18, 0, 0),
	GATE(usb3, "usb3", "aclk200", GATE_IP_FSYS, 19, 0, 0),
	GATE(sata_phyctrl, "sata_phyctrl", "aclk200", GATE_IP_FSYS, 24, 0, 0),
	GATE(sata_phyi2c, "sata_phyi2c", "aclk200", GATE_IP_FSYS, 25, 0, 0),
	GATE(uart0, "uart0", "aclk66", GATE_IP_PERIC, 0, 0, 0),
	GATE(uart1, "uart1", "aclk66", GATE_IP_PERIC, 1, 0, 0),
	GATE(uart2, "uart2", "aclk66", GATE_IP_PERIC, 2, 0, 0),
	GATE(uart3, "uart3", "aclk66", GATE_IP_PERIC, 3, 0, 0),
	GATE(uart4, "uart4", "aclk66", GATE_IP_PERIC, 4, 0, 0),
	GATE(i2c0, "i2c0", "aclk66", GATE_IP_PERIC, 6, 0, 0),
	GATE(i2c1, "i2c1", "aclk66", GATE_IP_PERIC, 7, 0, 0),
	GATE(i2c2, "i2c2", "aclk66", GATE_IP_PERIC, 8, 0, 0),
	GATE(i2c3, "i2c3", "aclk66", GATE_IP_PERIC, 9, 0, 0),
	GATE(i2c4, "i2c4", "aclk66", GATE_IP_PERIC, 10, 0, 0),
	GATE(i2c5, "i2c5", "aclk66", GATE_IP_PERIC, 11, 0, 0),
	GATE(i2c6, "i2c6", "aclk66", GATE_IP_PERIC, 12, 0, 0),
	GATE(i2c7, "i2c7", "aclk66", GATE_IP_PERIC, 13, 0, 0),
	GATE(i2c_hdmi, "i2c_hdmi", "aclk66", GATE_IP_PERIC, 14, 0, 0),
	GATE(adc, "adc", "aclk66", GATE_IP_PERIC, 15, 0, 0),
	GATE(spi0, "spi0", "aclk66", GATE_IP_PERIC, 16, 0, 0),
	GATE(spi1, "spi1", "aclk66", GATE_IP_PERIC, 17, 0, 0),
	GATE(spi2, "spi2", "aclk66", GATE_IP_PERIC, 18, 0, 0),
	GATE(i2s1, "i2s1", "aclk66", GATE_IP_PERIC, 20, 0, 0),
	GATE(i2s2, "i2s2", "aclk66", GATE_IP_PERIC, 21, 0, 0),
	GATE(pcm1, "pcm1", "aclk66", GATE_IP_PERIC, 22, 0, 0),
	GATE(pcm2, "pcm2", "aclk66", GATE_IP_PERIC, 23, 0, 0),
	GATE(pwm, "pwm", "aclk66", GATE_IP_PERIC, 24, 0, 0),
	GATE(spdif, "spdif", "aclk66", GATE_IP_PERIC, 26, 0, 0),
	GATE(ac97, "ac97", "aclk66", GATE_IP_PERIC, 27, 0, 0),
	GATE(hsi2c0, "hsi2c0", "aclk66", GATE_IP_PERIC, 28, 0, 0),
	GATE(hsi2c1, "hsi2c1", "aclk66", GATE_IP_PERIC, 29, 0, 0),
	GATE(hsi2c2, "hsi2c2", "aclk66", GATE_IP_PERIC, 30, 0, 0),
	GATE(hsi2c3, "hsi2c3", "aclk66", GATE_IP_PERIC, 31, 0, 0),
	GATE(chipid, "chipid", "aclk66", GATE_IP_PERIS, 0, 0, 0),
	GATE(sysreg, "sysreg", "aclk66", GATE_IP_PERIS, 1, 0, 0),
	GATE(pmu, "pmu", "aclk66", GATE_IP_PERIS, 2, 0, 0),
	GATE(tzpc0, "tzpc0", "aclk66", GATE_IP_PERIS, 6, 0, 0),
	GATE(tzpc1, "tzpc1", "aclk66", GATE_IP_PERIS, 7, 0, 0),
	GATE(tzpc2, "tzpc2", "aclk66", GATE_IP_PERIS, 8, 0, 0),
	GATE(tzpc3, "tzpc3", "aclk66", GATE_IP_PERIS, 9, 0, 0),
	GATE(tzpc4, "tzpc4", "aclk66", GATE_IP_PERIS, 10, 0, 0),
	GATE(tzpc5, "tzpc5", "aclk66", GATE_IP_PERIS, 11, 0, 0),
	GATE(tzpc6, "tzpc6", "aclk66", GATE_IP_PERIS, 12, 0, 0),
	GATE(tzpc7, "tzpc7", "aclk66", GATE_IP_PERIS, 13, 0, 0),
	GATE(tzpc8, "tzpc8", "aclk66", GATE_IP_PERIS, 14, 0, 0),
	GATE(tzpc9, "tzpc9", "aclk66", GATE_IP_PERIS, 15, 0, 0),
	GATE(hdmi_cec, "hdmi_cec", "aclk66", GATE_IP_PERIS, 16, 0, 0),
	GATE(mct, "mct", "aclk66", GATE_IP_PERIS, 18, 0, 0),
	GATE(wdt, "wdt", "aclk66", GATE_IP_PERIS, 19, 0, 0),
	GATE(rtc, "rtc", "aclk66", GATE_IP_PERIS, 20, 0, 0),
	GATE(tmu, "tmu", "aclk66", GATE_IP_PERIS, 21, 0, 0),
	GATE(cmu_top, "cmu_top", "aclk66",
			GATE_IP_PERIS, 3, CLK_IGNORE_UNUSED, 0),
	GATE(cmu_core, "cmu_core", "aclk66",
			GATE_IP_PERIS, 4, CLK_IGNORE_UNUSED, 0),
	GATE(cmu_mem, "cmu_mem", "aclk66",
			GATE_IP_PERIS, 5, CLK_IGNORE_UNUSED, 0),
	GATE(sclk_cam_bayer, "sclk_cam_bayer", "div_cam_bayer",
			SRC_MASK_GSCL, 12, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_cam0, "sclk_cam0", "div_cam0",
			SRC_MASK_GSCL, 16, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_cam1, "sclk_cam1", "div_cam1",
			SRC_MASK_GSCL, 20, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_gscl_wa, "sclk_gscl_wa", "div_gscl_wa",
			SRC_MASK_GSCL, 24, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_gscl_wb, "sclk_gscl_wb", "div_gscl_wb",
			SRC_MASK_GSCL, 28, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_fimd1, "sclk_fimd1", "div_fimd1",
			SRC_MASK_DISP1_0, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mipi1, "sclk_mipi1", "div_mipi1",
			SRC_MASK_DISP1_0, 12, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_dp, "sclk_dp", "div_dp",
			SRC_MASK_DISP1_0, 16, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_hdmi, "sclk_hdmi", "mout_hdmi",
			SRC_MASK_DISP1_0, 20, 0, 0),
	GATE(sclk_audio0, "sclk_audio0", "div_audio0",
			SRC_MASK_MAU, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc0, "sclk_mmc0", "div_mmc_pre0",
			SRC_MASK_FSYS, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc1, "sclk_mmc1", "div_mmc_pre1",
			SRC_MASK_FSYS, 4, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc2, "sclk_mmc2", "div_mmc_pre2",
			SRC_MASK_FSYS, 8, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc3, "sclk_mmc3", "div_mmc_pre3",
			SRC_MASK_FSYS, 12, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_sata, "sclk_sata", "div_sata",
			SRC_MASK_FSYS, 24, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_usb3, "sclk_usb3", "div_usb3",
			SRC_MASK_FSYS, 28, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_jpeg, "sclk_jpeg", "div_jpeg",
			SRC_MASK_GEN, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart0, "sclk_uart0", "div_uart0",
			SRC_MASK_PERIC0, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart1, "sclk_uart1", "div_uart1",
			SRC_MASK_PERIC0, 4, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart2, "sclk_uart2", "div_uart2",
			SRC_MASK_PERIC0, 8, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart3, "sclk_uart3", "div_uart3",
			SRC_MASK_PERIC0, 12, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_pwm, "sclk_pwm", "div_pwm",
			SRC_MASK_PERIC0, 24, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_audio1, "sclk_audio1", "div_audio1",
			SRC_MASK_PERIC1, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_audio2, "sclk_audio2", "div_audio2",
			SRC_MASK_PERIC1, 4, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spdif, "sclk_spdif", "mout_spdif",
			SRC_MASK_PERIC1, 4, 0, 0),
	GATE(sclk_spi0, "sclk_spi0", "div_spi_pre0",
			SRC_MASK_PERIC1, 16, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spi1, "sclk_spi1", "div_spi_pre1",
			SRC_MASK_PERIC1, 20, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spi2, "sclk_spi2", "div_spi_pre2",
			SRC_MASK_PERIC1, 24, CLK_SET_RATE_PARENT, 0),
	GATE(fimd1, "fimd1", "aclk200", GATE_IP_DISP1, 0, 0, 0),
	GATE(mie1, "mie1", "aclk200", GATE_IP_DISP1, 1, 0, 0),
	GATE(dsim0, "dsim0", "aclk200", GATE_IP_DISP1, 3, 0, 0),
	GATE(dp, "dp", "aclk200", GATE_IP_DISP1, 4, 0, 0),
	GATE(mixer, "mixer", "aclk200", GATE_IP_DISP1, 5, 0, 0),
	GATE(hdmi, "hdmi", "aclk200", GATE_IP_DISP1, 6, 0, 0),
};

static __initdata struct of_device_id ext_clk_match[] = {
	{ .compatible = "samsung,clock-xxti", .data = (void *)0, },
	{ },
};

/* register exynox5250 clocks */
void __init exynos5250_clk_init(struct device_node *np)
{
	void __iomem *reg_base;
	struct clk *apll, *mpll, *epll, *vpll, *bpll, *gpll, *cpll;

	if (np) {
		reg_base = of_iomap(np, 0);
		if (!reg_base)
			panic("%s: failed to map registers\n", __func__);
	} else {
		panic("%s: unable to determine soc\n", __func__);
	}

	samsung_clk_init(np, reg_base, nr_clks,
			exynos5250_clk_regs, ARRAY_SIZE(exynos5250_clk_regs),
			NULL, 0);
	samsung_clk_of_register_fixed_ext(exynos5250_fixed_rate_ext_clks,
			ARRAY_SIZE(exynos5250_fixed_rate_ext_clks),
			ext_clk_match);

	apll = samsung_clk_register_pll35xx("fout_apll", "fin_pll",
			reg_base + 0x100);
	mpll = samsung_clk_register_pll35xx("fout_mpll", "fin_pll",
			reg_base + 0x4100);
	bpll = samsung_clk_register_pll35xx("fout_bpll", "fin_pll",
			reg_base + 0x20110);
	gpll = samsung_clk_register_pll35xx("fout_gpll", "fin_pll",
			reg_base + 0x10150);
	cpll = samsung_clk_register_pll35xx("fout_cpll", "fin_pll",
			reg_base + 0x10120);
	epll = samsung_clk_register_pll36xx("fout_epll", "fin_pll",
			reg_base + 0x10130);
	vpll = samsung_clk_register_pll36xx("fout_vpll", "mout_vpllsrc",
			reg_base + 0x10140);

	samsung_clk_register_fixed_rate(exynos5250_fixed_rate_clks,
			ARRAY_SIZE(exynos5250_fixed_rate_clks));
	samsung_clk_register_fixed_factor(exynos5250_fixed_factor_clks,
			ARRAY_SIZE(exynos5250_fixed_factor_clks));
	samsung_clk_register_mux(exynos5250_mux_clks,
			ARRAY_SIZE(exynos5250_mux_clks));
	samsung_clk_register_div(exynos5250_div_clks,
			ARRAY_SIZE(exynos5250_div_clks));
	samsung_clk_register_gate(exynos5250_gate_clks,
			ARRAY_SIZE(exynos5250_gate_clks));

	pr_info("Exynos5250: clock setup completed, armclk=%ld\n",
			_get_rate("armclk"));
}
CLK_OF_DECLARE(exynos5250_clk, "samsung,exynos5250-clock", exynos5250_clk_init);
