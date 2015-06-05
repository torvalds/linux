/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for Exynos4415 SoC.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/clock/exynos4415.h>

#include "clk.h"
#include "clk-pll.h"

#define SRC_LEFTBUS		0x4200
#define DIV_LEFTBUS		0x4500
#define GATE_IP_LEFTBUS		0x4800
#define GATE_IP_IMAGE		0x4930
#define SRC_RIGHTBUS		0x8200
#define DIV_RIGHTBUS		0x8500
#define GATE_IP_RIGHTBUS	0x8800
#define GATE_IP_PERIR		0x8960
#define EPLL_LOCK		0xc010
#define G3D_PLL_LOCK		0xc020
#define DISP_PLL_LOCK		0xc030
#define ISP_PLL_LOCK		0xc040
#define EPLL_CON0		0xc110
#define EPLL_CON1		0xc114
#define EPLL_CON2		0xc118
#define G3D_PLL_CON0		0xc120
#define G3D_PLL_CON1		0xc124
#define G3D_PLL_CON2		0xc128
#define ISP_PLL_CON0		0xc130
#define ISP_PLL_CON1		0xc134
#define ISP_PLL_CON2		0xc138
#define DISP_PLL_CON0		0xc140
#define DISP_PLL_CON1		0xc144
#define DISP_PLL_CON2		0xc148
#define SRC_TOP0		0xc210
#define SRC_TOP1		0xc214
#define SRC_CAM			0xc220
#define SRC_TV			0xc224
#define SRC_MFC			0xc228
#define SRC_G3D			0xc22c
#define SRC_LCD			0xc234
#define SRC_ISP			0xc238
#define SRC_MAUDIO		0xc23c
#define SRC_FSYS		0xc240
#define SRC_PERIL0		0xc250
#define SRC_PERIL1		0xc254
#define SRC_CAM1		0xc258
#define SRC_TOP_ISP0		0xc25c
#define SRC_TOP_ISP1		0xc260
#define SRC_MASK_TOP		0xc310
#define SRC_MASK_CAM		0xc320
#define SRC_MASK_TV		0xc324
#define SRC_MASK_LCD		0xc334
#define SRC_MASK_ISP		0xc338
#define SRC_MASK_MAUDIO		0xc33c
#define SRC_MASK_FSYS		0xc340
#define SRC_MASK_PERIL0		0xc350
#define SRC_MASK_PERIL1		0xc354
#define DIV_TOP			0xc510
#define DIV_CAM			0xc520
#define DIV_TV			0xc524
#define DIV_MFC			0xc528
#define DIV_G3D			0xc52c
#define DIV_LCD			0xc534
#define DIV_ISP			0xc538
#define DIV_MAUDIO		0xc53c
#define DIV_FSYS0		0xc540
#define DIV_FSYS1		0xc544
#define DIV_FSYS2		0xc548
#define DIV_PERIL0		0xc550
#define DIV_PERIL1		0xc554
#define DIV_PERIL2		0xc558
#define DIV_PERIL3		0xc55c
#define DIV_PERIL4		0xc560
#define DIV_PERIL5		0xc564
#define DIV_CAM1		0xc568
#define DIV_TOP_ISP1		0xc56c
#define DIV_TOP_ISP0		0xc570
#define CLKDIV2_RATIO		0xc580
#define GATE_SCLK_CAM		0xc820
#define GATE_SCLK_TV		0xc824
#define GATE_SCLK_MFC		0xc828
#define GATE_SCLK_G3D		0xc82c
#define GATE_SCLK_LCD		0xc834
#define GATE_SCLK_MAUDIO	0xc83c
#define GATE_SCLK_FSYS		0xc840
#define GATE_SCLK_PERIL		0xc850
#define GATE_IP_CAM		0xc920
#define GATE_IP_TV		0xc924
#define GATE_IP_MFC		0xc928
#define GATE_IP_G3D		0xc92c
#define GATE_IP_LCD		0xc934
#define GATE_IP_FSYS		0xc940
#define GATE_IP_PERIL		0xc950
#define GATE_BLOCK		0xc970
#define APLL_LOCK		0x14000
#define APLL_CON0		0x14100
#define SRC_CPU			0x14200
#define DIV_CPU0		0x14500
#define DIV_CPU1		0x14504

static unsigned long exynos4415_cmu_clk_regs[] __initdata = {
	SRC_LEFTBUS,
	DIV_LEFTBUS,
	GATE_IP_LEFTBUS,
	GATE_IP_IMAGE,
	SRC_RIGHTBUS,
	DIV_RIGHTBUS,
	GATE_IP_RIGHTBUS,
	GATE_IP_PERIR,
	EPLL_LOCK,
	G3D_PLL_LOCK,
	DISP_PLL_LOCK,
	ISP_PLL_LOCK,
	EPLL_CON0,
	EPLL_CON1,
	EPLL_CON2,
	G3D_PLL_CON0,
	G3D_PLL_CON1,
	G3D_PLL_CON2,
	ISP_PLL_CON0,
	ISP_PLL_CON1,
	ISP_PLL_CON2,
	DISP_PLL_CON0,
	DISP_PLL_CON1,
	DISP_PLL_CON2,
	SRC_TOP0,
	SRC_TOP1,
	SRC_CAM,
	SRC_TV,
	SRC_MFC,
	SRC_G3D,
	SRC_LCD,
	SRC_ISP,
	SRC_MAUDIO,
	SRC_FSYS,
	SRC_PERIL0,
	SRC_PERIL1,
	SRC_CAM1,
	SRC_TOP_ISP0,
	SRC_TOP_ISP1,
	SRC_MASK_TOP,
	SRC_MASK_CAM,
	SRC_MASK_TV,
	SRC_MASK_LCD,
	SRC_MASK_ISP,
	SRC_MASK_MAUDIO,
	SRC_MASK_FSYS,
	SRC_MASK_PERIL0,
	SRC_MASK_PERIL1,
	DIV_TOP,
	DIV_CAM,
	DIV_TV,
	DIV_MFC,
	DIV_G3D,
	DIV_LCD,
	DIV_ISP,
	DIV_MAUDIO,
	DIV_FSYS0,
	DIV_FSYS1,
	DIV_FSYS2,
	DIV_PERIL0,
	DIV_PERIL1,
	DIV_PERIL2,
	DIV_PERIL3,
	DIV_PERIL4,
	DIV_PERIL5,
	DIV_CAM1,
	DIV_TOP_ISP1,
	DIV_TOP_ISP0,
	CLKDIV2_RATIO,
	GATE_SCLK_CAM,
	GATE_SCLK_TV,
	GATE_SCLK_MFC,
	GATE_SCLK_G3D,
	GATE_SCLK_LCD,
	GATE_SCLK_MAUDIO,
	GATE_SCLK_FSYS,
	GATE_SCLK_PERIL,
	GATE_IP_CAM,
	GATE_IP_TV,
	GATE_IP_MFC,
	GATE_IP_G3D,
	GATE_IP_LCD,
	GATE_IP_FSYS,
	GATE_IP_PERIL,
	GATE_BLOCK,
	APLL_LOCK,
	APLL_CON0,
	SRC_CPU,
	DIV_CPU0,
	DIV_CPU1,
};

/* list of all parent clock list */
PNAME(mout_g3d_pllsrc_p)	= { "fin_pll", };

PNAME(mout_apll_p)		= { "fin_pll", "fout_apll", };
PNAME(mout_g3d_pll_p)		= { "fin_pll", "fout_g3d_pll", };
PNAME(mout_isp_pll_p)		= { "fin_pll", "fout_isp_pll", };
PNAME(mout_disp_pll_p)		= { "fin_pll", "fout_disp_pll", };

PNAME(mout_mpll_user_p)		= { "fin_pll", "div_mpll_pre", };
PNAME(mout_epll_p)		= { "fin_pll", "fout_epll", };
PNAME(mout_core_p)		= { "mout_apll", "mout_mpll_user_c", };
PNAME(mout_hpm_p)		= { "mout_apll", "mout_mpll_user_c", };

PNAME(mout_ebi_p)		= { "div_aclk_200", "div_aclk_160", };
PNAME(mout_ebi_1_p)		= { "mout_ebi", "mout_g3d_pll", };

PNAME(mout_gdl_p)		= { "mout_mpll_user_l", };
PNAME(mout_gdr_p)		= { "mout_mpll_user_r", };

PNAME(mout_aclk_266_p)		= { "mout_mpll_user_t", "mout_g3d_pll", };

PNAME(group_epll_g3dpll_p)	= { "mout_epll", "mout_g3d_pll" };
PNAME(group_sclk_p)		= { "xxti", "xusbxti",
				    "none", "mout_isp_pll",
				    "none", "none", "div_mpll_pre",
				    "mout_epll", "mout_g3d_pll", };
PNAME(group_spdif_p)		= { "mout_audio0", "mout_audio1",
				    "mout_audio2", "spdif_extclk", };
PNAME(group_sclk_audio2_p)	= { "audiocdclk2", "none",
				    "none", "mout_isp_pll",
				    "mout_disp_pll", "xusbxti",
				    "div_mpll_pre", "mout_epll",
				    "mout_g3d_pll", };
PNAME(group_sclk_audio1_p)	= { "audiocdclk1", "none",
				    "none", "mout_isp_pll",
				    "mout_disp_pll", "xusbxti",
				    "div_mpll_pre", "mout_epll",
				    "mout_g3d_pll", };
PNAME(group_sclk_audio0_p)	= { "audiocdclk0", "none",
				    "none", "mout_isp_pll",
				    "mout_disp_pll", "xusbxti",
				    "div_mpll_pre", "mout_epll",
				    "mout_g3d_pll", };
PNAME(group_fimc_lclk_p)	= { "xxti", "xusbxti",
				    "none", "mout_isp_pll",
				    "none", "mout_disp_pll",
				    "mout_mpll_user_t", "mout_epll",
				    "mout_g3d_pll", };
PNAME(group_sclk_fimd0_p)	= { "xxti", "xusbxti",
				    "m_bitclkhsdiv4_4l", "mout_isp_pll",
				    "mout_disp_pll", "sclk_hdmiphy",
				    "div_mpll_pre", "mout_epll",
				    "mout_g3d_pll", };
PNAME(mout_hdmi_p)		= { "sclk_pixel", "sclk_hdmiphy" };
PNAME(mout_mfc_p)		= { "mout_mfc_0", "mout_mfc_1" };
PNAME(mout_g3d_p)		= { "mout_g3d_0", "mout_g3d_1" };
PNAME(mout_jpeg_p)		= { "mout_jpeg_0", "mout_jpeg_1" };
PNAME(mout_jpeg1_p)		= { "mout_epll", "mout_g3d_pll" };
PNAME(group_aclk_isp0_300_p)	= { "mout_isp_pll", "div_mpll_pre" };
PNAME(group_aclk_isp0_400_user_p) = { "fin_pll", "div_aclk_400_mcuisp" };
PNAME(group_aclk_isp0_300_user_p) = { "fin_pll", "mout_aclk_isp0_300" };
PNAME(group_aclk_isp1_300_user_p) = { "fin_pll", "mout_aclk_isp1_300" };
PNAME(group_mout_mpll_user_t_p)	= { "mout_mpll_user_t" };

static struct samsung_fixed_factor_clock exynos4415_fixed_factor_clks[] __initdata = {
	/* HACK: fin_pll hardcoded to xusbxti until detection is implemented. */
	FFACTOR(CLK_FIN_PLL, "fin_pll", "xusbxti", 1, 1, 0),
};

static struct samsung_fixed_rate_clock exynos4415_fixed_rate_clks[] __initdata = {
	FRATE(CLK_SCLK_HDMIPHY, "sclk_hdmiphy", NULL, CLK_IS_ROOT, 27000000),
};

static struct samsung_mux_clock exynos4415_mux_clks[] __initdata = {
	/*
	 * NOTE: Following table is sorted by register address in ascending
	 * order and then bitfield shift in descending order, as it is done
	 * in the User's Manual. When adding new entries, please make sure
	 * that the order is preserved, to avoid merge conflicts and make
	 * further work with defined data easier.
	 */

	/* SRC_LEFTBUS */
	MUX(CLK_MOUT_MPLL_USER_L, "mout_mpll_user_l", mout_mpll_user_p,
		SRC_LEFTBUS, 4, 1),
	MUX(CLK_MOUT_GDL, "mout_gdl", mout_gdl_p, SRC_LEFTBUS, 0, 1),

	/* SRC_RIGHTBUS */
	MUX(CLK_MOUT_MPLL_USER_R, "mout_mpll_user_r", mout_mpll_user_p,
		SRC_RIGHTBUS, 4, 1),
	MUX(CLK_MOUT_GDR, "mout_gdr", mout_gdr_p, SRC_RIGHTBUS, 0, 1),

	/* SRC_TOP0 */
	MUX(CLK_MOUT_EBI, "mout_ebi", mout_ebi_p, SRC_TOP0, 28, 1),
	MUX(CLK_MOUT_ACLK_200, "mout_aclk_200", group_mout_mpll_user_t_p,
		SRC_TOP0, 24, 1),
	MUX(CLK_MOUT_ACLK_160, "mout_aclk_160", group_mout_mpll_user_t_p,
		SRC_TOP0, 20, 1),
	MUX(CLK_MOUT_ACLK_100, "mout_aclk_100", group_mout_mpll_user_t_p,
		SRC_TOP0, 16, 1),
	MUX(CLK_MOUT_ACLK_266, "mout_aclk_266", mout_aclk_266_p,
		SRC_TOP0, 12, 1),
	MUX(CLK_MOUT_G3D_PLL, "mout_g3d_pll", mout_g3d_pll_p,
		SRC_TOP0, 8, 1),
	MUX(CLK_MOUT_EPLL, "mout_epll", mout_epll_p, SRC_TOP0, 4, 1),
	MUX(CLK_MOUT_EBI_1, "mout_ebi_1", mout_ebi_1_p, SRC_TOP0, 0, 1),

	/* SRC_TOP1 */
	MUX(CLK_MOUT_ISP_PLL, "mout_isp_pll", mout_isp_pll_p,
		SRC_TOP1, 28, 1),
	MUX(CLK_MOUT_DISP_PLL, "mout_disp_pll", mout_disp_pll_p,
		SRC_TOP1, 16, 1),
	MUX(CLK_MOUT_MPLL_USER_T, "mout_mpll_user_t", mout_mpll_user_p,
		SRC_TOP1, 12, 1),
	MUX(CLK_MOUT_ACLK_400_MCUISP, "mout_aclk_400_mcuisp",
		group_mout_mpll_user_t_p, SRC_TOP1, 8, 1),
	MUX(CLK_MOUT_G3D_PLLSRC, "mout_g3d_pllsrc", mout_g3d_pllsrc_p,
		SRC_TOP1, 0, 1),

	/* SRC_CAM */
	MUX(CLK_MOUT_CSIS1, "mout_csis1", group_fimc_lclk_p, SRC_CAM, 28, 4),
	MUX(CLK_MOUT_CSIS0, "mout_csis0", group_fimc_lclk_p, SRC_CAM, 24, 4),
	MUX(CLK_MOUT_CAM1, "mout_cam1", group_fimc_lclk_p, SRC_CAM, 20, 4),
	MUX(CLK_MOUT_FIMC3_LCLK, "mout_fimc3_lclk", group_fimc_lclk_p, SRC_CAM,
		12, 4),
	MUX(CLK_MOUT_FIMC2_LCLK, "mout_fimc2_lclk", group_fimc_lclk_p, SRC_CAM,
		8, 4),
	MUX(CLK_MOUT_FIMC1_LCLK, "mout_fimc1_lclk", group_fimc_lclk_p, SRC_CAM,
		4, 4),
	MUX(CLK_MOUT_FIMC0_LCLK, "mout_fimc0_lclk", group_fimc_lclk_p, SRC_CAM,
		0, 4),

	/* SRC_TV */
	MUX(CLK_MOUT_HDMI, "mout_hdmi", mout_hdmi_p, SRC_TV, 0, 1),

	/* SRC_MFC */
	MUX(CLK_MOUT_MFC, "mout_mfc", mout_mfc_p, SRC_MFC, 8, 1),
	MUX(CLK_MOUT_MFC_1, "mout_mfc_1", group_epll_g3dpll_p, SRC_MFC, 4, 1),
	MUX(CLK_MOUT_MFC_0, "mout_mfc_0", group_mout_mpll_user_t_p, SRC_MFC, 0,
		1),

	/* SRC_G3D */
	MUX(CLK_MOUT_G3D, "mout_g3d", mout_g3d_p, SRC_G3D, 8, 1),
	MUX(CLK_MOUT_G3D_1, "mout_g3d_1", group_epll_g3dpll_p, SRC_G3D, 4, 1),
	MUX(CLK_MOUT_G3D_0, "mout_g3d_0", group_mout_mpll_user_t_p, SRC_G3D, 0,
		1),

	/* SRC_LCD */
	MUX(CLK_MOUT_MIPI0, "mout_mipi0", group_fimc_lclk_p, SRC_LCD, 12, 4),
	MUX(CLK_MOUT_FIMD0, "mout_fimd0", group_sclk_fimd0_p, SRC_LCD, 0, 4),

	/* SRC_ISP */
	MUX(CLK_MOUT_TSADC_ISP, "mout_tsadc_isp", group_fimc_lclk_p, SRC_ISP,
		16, 4),
	MUX(CLK_MOUT_UART_ISP, "mout_uart_isp", group_fimc_lclk_p, SRC_ISP,
		12, 4),
	MUX(CLK_MOUT_SPI1_ISP, "mout_spi1_isp", group_fimc_lclk_p, SRC_ISP,
		8, 4),
	MUX(CLK_MOUT_SPI0_ISP, "mout_spi0_isp", group_fimc_lclk_p, SRC_ISP,
		4, 4),
	MUX(CLK_MOUT_PWM_ISP, "mout_pwm_isp", group_fimc_lclk_p, SRC_ISP,
		0, 4),

	/* SRC_MAUDIO */
	MUX(CLK_MOUT_AUDIO0, "mout_audio0", group_sclk_audio0_p, SRC_MAUDIO,
		0, 4),

	/* SRC_FSYS */
	MUX(CLK_MOUT_TSADC, "mout_tsadc", group_sclk_p, SRC_FSYS, 28, 4),
	MUX(CLK_MOUT_MMC2, "mout_mmc2", group_sclk_p, SRC_FSYS, 8, 4),
	MUX(CLK_MOUT_MMC1, "mout_mmc1", group_sclk_p, SRC_FSYS, 4, 4),
	MUX(CLK_MOUT_MMC0, "mout_mmc0", group_sclk_p, SRC_FSYS, 0, 4),

	/* SRC_PERIL0 */
	MUX(CLK_MOUT_UART3, "mout_uart3", group_sclk_p, SRC_PERIL0, 12, 4),
	MUX(CLK_MOUT_UART2, "mout_uart2", group_sclk_p, SRC_PERIL0, 8, 4),
	MUX(CLK_MOUT_UART1, "mout_uart1", group_sclk_p, SRC_PERIL0, 4, 4),
	MUX(CLK_MOUT_UART0, "mout_uart0", group_sclk_p, SRC_PERIL0, 0, 4),

	/* SRC_PERIL1 */
	MUX(CLK_MOUT_SPI2, "mout_spi2", group_sclk_p, SRC_PERIL1, 24, 4),
	MUX(CLK_MOUT_SPI1, "mout_spi1", group_sclk_p, SRC_PERIL1, 20, 4),
	MUX(CLK_MOUT_SPI0, "mout_spi0", group_sclk_p, SRC_PERIL1, 16, 4),
	MUX(CLK_MOUT_SPDIF, "mout_spdif", group_spdif_p, SRC_PERIL1, 8, 4),
	MUX(CLK_MOUT_AUDIO2, "mout_audio2", group_sclk_audio2_p, SRC_PERIL1,
		4, 4),
	MUX(CLK_MOUT_AUDIO1, "mout_audio1", group_sclk_audio1_p, SRC_PERIL1,
		0, 4),

	/* SRC_CPU */
	MUX(CLK_MOUT_MPLL_USER_C, "mout_mpll_user_c", mout_mpll_user_p,
		SRC_CPU, 24, 1),
	MUX(CLK_MOUT_HPM, "mout_hpm", mout_hpm_p, SRC_CPU, 20, 1),
	MUX_F(CLK_MOUT_CORE, "mout_core", mout_core_p, SRC_CPU, 16, 1, 0,
		CLK_MUX_READ_ONLY),
	MUX_F(CLK_MOUT_APLL, "mout_apll", mout_apll_p, SRC_CPU, 0, 1,
		CLK_SET_RATE_PARENT, 0),

	/* SRC_CAM1 */
	MUX(CLK_MOUT_PXLASYNC_CSIS1_FIMC, "mout_pxlasync_csis1",
		group_fimc_lclk_p, SRC_CAM1, 20, 1),
	MUX(CLK_MOUT_PXLASYNC_CSIS0_FIMC, "mout_pxlasync_csis0",
		group_fimc_lclk_p, SRC_CAM1, 16, 1),
	MUX(CLK_MOUT_JPEG, "mout_jpeg", mout_jpeg_p, SRC_CAM1, 8, 1),
	MUX(CLK_MOUT_JPEG1, "mout_jpeg_1", mout_jpeg1_p, SRC_CAM1, 4, 1),
	MUX(CLK_MOUT_JPEG0, "mout_jpeg_0", group_mout_mpll_user_t_p, SRC_CAM1,
		0, 1),

	/* SRC_TOP_ISP0 */
	MUX(CLK_MOUT_ACLK_ISP0_300, "mout_aclk_isp0_300",
		group_aclk_isp0_300_p, SRC_TOP_ISP0, 8, 1),
	MUX(CLK_MOUT_ACLK_ISP0_400, "mout_aclk_isp0_400_user",
		group_aclk_isp0_400_user_p, SRC_TOP_ISP0, 4, 1),
	MUX(CLK_MOUT_ACLK_ISP0_300_USER, "mout_aclk_isp0_300_user",
		group_aclk_isp0_300_user_p, SRC_TOP_ISP0, 0, 1),

	/* SRC_TOP_ISP1 */
	MUX(CLK_MOUT_ACLK_ISP1_300, "mout_aclk_isp1_300",
		group_aclk_isp0_300_p, SRC_TOP_ISP1, 4, 1),
	MUX(CLK_MOUT_ACLK_ISP1_300_USER, "mout_aclk_isp1_300_user",
		group_aclk_isp1_300_user_p, SRC_TOP_ISP1, 0, 1),
};

static struct samsung_div_clock exynos4415_div_clks[] __initdata = {
	/*
	 * NOTE: Following table is sorted by register address in ascending
	 * order and then bitfield shift in descending order, as it is done
	 * in the User's Manual. When adding new entries, please make sure
	 * that the order is preserved, to avoid merge conflicts and make
	 * further work with defined data easier.
	 */

	/* DIV_LEFTBUS */
	DIV(CLK_DIV_GPL, "div_gpl", "div_gdl", DIV_LEFTBUS, 4, 3),
	DIV(CLK_DIV_GDL, "div_gdl", "mout_gdl", DIV_LEFTBUS, 0, 4),

	/* DIV_RIGHTBUS */
	DIV(CLK_DIV_GPR, "div_gpr", "div_gdr", DIV_RIGHTBUS, 4, 3),
	DIV(CLK_DIV_GDR, "div_gdr", "mout_gdr", DIV_RIGHTBUS, 0, 4),

	/* DIV_TOP */
	DIV(CLK_DIV_ACLK_400_MCUISP, "div_aclk_400_mcuisp",
		"mout_aclk_400_mcuisp", DIV_TOP, 24, 3),
	DIV(CLK_DIV_EBI, "div_ebi", "mout_ebi_1", DIV_TOP, 16, 3),
	DIV(CLK_DIV_ACLK_200, "div_aclk_200", "mout_aclk_200", DIV_TOP, 12, 3),
	DIV(CLK_DIV_ACLK_160, "div_aclk_160", "mout_aclk_160", DIV_TOP, 8, 3),
	DIV(CLK_DIV_ACLK_100, "div_aclk_100", "mout_aclk_100", DIV_TOP, 4, 4),
	DIV(CLK_DIV_ACLK_266, "div_aclk_266", "mout_aclk_266", DIV_TOP, 0, 3),

	/* DIV_CAM */
	DIV(CLK_DIV_CSIS1, "div_csis1", "mout_csis1", DIV_CAM, 28, 4),
	DIV(CLK_DIV_CSIS0, "div_csis0", "mout_csis0", DIV_CAM, 24, 4),
	DIV(CLK_DIV_CAM1, "div_cam1", "mout_cam1", DIV_CAM, 20, 4),
	DIV(CLK_DIV_FIMC3_LCLK, "div_fimc3_lclk", "mout_fimc3_lclk", DIV_CAM,
		12, 4),
	DIV(CLK_DIV_FIMC2_LCLK, "div_fimc2_lclk", "mout_fimc2_lclk", DIV_CAM,
		8, 4),
	DIV(CLK_DIV_FIMC1_LCLK, "div_fimc1_lclk", "mout_fimc1_lclk", DIV_CAM,
		4, 4),
	DIV(CLK_DIV_FIMC0_LCLK, "div_fimc0_lclk", "mout_fimc0_lclk", DIV_CAM,
		0, 4),

	/* DIV_TV */
	DIV(CLK_DIV_TV_BLK, "div_tv_blk", "mout_g3d_pll", DIV_TV, 0, 4),

	/* DIV_MFC */
	DIV(CLK_DIV_MFC, "div_mfc", "mout_mfc", DIV_MFC, 0, 4),

	/* DIV_G3D */
	DIV(CLK_DIV_G3D, "div_g3d", "mout_g3d", DIV_G3D, 0, 4),

	/* DIV_LCD */
	DIV_F(CLK_DIV_MIPI0_PRE, "div_mipi0_pre", "div_mipi0", DIV_LCD, 20, 4,
		CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DIV_MIPI0, "div_mipi0", "mout_mipi0", DIV_LCD, 16, 4),
	DIV(CLK_DIV_FIMD0, "div_fimd0", "mout_fimd0", DIV_LCD, 0, 4),

	/* DIV_ISP */
	DIV(CLK_DIV_UART_ISP, "div_uart_isp", "mout_uart_isp", DIV_ISP, 28, 4),
	DIV_F(CLK_DIV_SPI1_ISP_PRE, "div_spi1_isp_pre", "div_spi1_isp",
		DIV_ISP, 20, 8, CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DIV_SPI1_ISP, "div_spi1_isp", "mout_spi1_isp", DIV_ISP, 16, 4),
	DIV_F(CLK_DIV_SPI0_ISP_PRE, "div_spi0_isp_pre", "div_spi0_isp",
		DIV_ISP, 8, 8, CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DIV_SPI0_ISP, "div_spi0_isp", "mout_spi0_isp", DIV_ISP, 4, 4),
	DIV(CLK_DIV_PWM_ISP, "div_pwm_isp", "mout_pwm_isp", DIV_ISP, 0, 4),

	/* DIV_MAUDIO */
	DIV(CLK_DIV_PCM0, "div_pcm0", "div_audio0", DIV_MAUDIO, 4, 8),
	DIV(CLK_DIV_AUDIO0, "div_audio0", "mout_audio0", DIV_MAUDIO, 0, 4),

	/* DIV_FSYS0 */
	DIV_F(CLK_DIV_TSADC_PRE, "div_tsadc_pre", "div_tsadc", DIV_FSYS0, 8, 8,
		CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DIV_TSADC, "div_tsadc", "mout_tsadc", DIV_FSYS0, 0, 4),

	/* DIV_FSYS1 */
	DIV_F(CLK_DIV_MMC1_PRE, "div_mmc1_pre", "div_mmc1", DIV_FSYS1, 24, 8,
		CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DIV_MMC1, "div_mmc1", "mout_mmc1", DIV_FSYS1, 16, 4),
	DIV_F(CLK_DIV_MMC0_PRE, "div_mmc0_pre", "div_mmc0", DIV_FSYS1, 8, 8,
		CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DIV_MMC0, "div_mmc0", "mout_mmc0", DIV_FSYS1, 0, 4),

	/* DIV_FSYS2 */
	DIV_F(CLK_DIV_MMC2_PRE, "div_mmc2_pre", "div_mmc2", DIV_FSYS2, 8, 8,
		CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DIV_MMC2_PRE, "div_mmc2", "mout_mmc2", DIV_FSYS2, 0, 4,
		CLK_SET_RATE_PARENT, 0),

	/* DIV_PERIL0 */
	DIV(CLK_DIV_UART3, "div_uart3", "mout_uart3", DIV_PERIL0, 12, 4),
	DIV(CLK_DIV_UART2, "div_uart2", "mout_uart2", DIV_PERIL0, 8, 4),
	DIV(CLK_DIV_UART1, "div_uart1", "mout_uart1", DIV_PERIL0, 4, 4),
	DIV(CLK_DIV_UART0, "div_uart0", "mout_uart0", DIV_PERIL0, 0, 4),

	/* DIV_PERIL1 */
	DIV_F(CLK_DIV_SPI1_PRE, "div_spi1_pre", "div_spi1", DIV_PERIL1, 24, 8,
		CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DIV_SPI1, "div_spi1", "mout_spi1", DIV_PERIL1, 16, 4),
	DIV_F(CLK_DIV_SPI0_PRE, "div_spi0_pre", "div_spi0", DIV_PERIL1, 8, 8,
		CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DIV_SPI0, "div_spi0", "mout_spi0", DIV_PERIL1, 0, 4),

	/* DIV_PERIL2 */
	DIV_F(CLK_DIV_SPI2_PRE, "div_spi2_pre", "div_spi2", DIV_PERIL2, 8, 8,
		CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DIV_SPI2, "div_spi2", "mout_spi2", DIV_PERIL2, 0, 4),

	/* DIV_PERIL4 */
	DIV(CLK_DIV_PCM2, "div_pcm2", "div_audio2", DIV_PERIL4, 20, 8),
	DIV(CLK_DIV_AUDIO2, "div_audio2", "mout_audio2", DIV_PERIL4, 16, 4),
	DIV(CLK_DIV_PCM1, "div_pcm1", "div_audio1", DIV_PERIL4, 20, 8),
	DIV(CLK_DIV_AUDIO1, "div_audio1", "mout_audio1", DIV_PERIL4, 0, 4),

	/* DIV_PERIL5 */
	DIV(CLK_DIV_I2S1, "div_i2s1", "div_audio1", DIV_PERIL5, 0, 6),

	/* DIV_CAM1 */
	DIV(CLK_DIV_PXLASYNC_CSIS1_FIMC, "div_pxlasync_csis1_fimc",
		"mout_pxlasync_csis1", DIV_CAM1, 24, 4),
	DIV(CLK_DIV_PXLASYNC_CSIS0_FIMC, "div_pxlasync_csis0_fimc",
		"mout_pxlasync_csis0", DIV_CAM1, 20, 4),
	DIV(CLK_DIV_JPEG, "div_jpeg", "mout_jpeg", DIV_CAM1, 0, 4),

	/* DIV_CPU0 */
	DIV(CLK_DIV_CORE2, "div_core2", "div_core", DIV_CPU0, 28, 3),
	DIV_F(CLK_DIV_APLL, "div_apll", "mout_apll", DIV_CPU0, 24, 3,
			CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV(CLK_DIV_PCLK_DBG, "div_pclk_dbg", "div_core2", DIV_CPU0, 20, 3),
	DIV(CLK_DIV_ATB, "div_atb", "div_core2", DIV_CPU0, 16, 3),
	DIV(CLK_DIV_PERIPH, "div_periph", "div_core2", DIV_CPU0, 12, 3),
	DIV(CLK_DIV_COREM1, "div_corem1", "div_core2", DIV_CPU0, 8, 3),
	DIV(CLK_DIV_COREM0, "div_corem0", "div_core2", DIV_CPU0, 4, 3),
	DIV_F(CLK_DIV_CORE, "div_core", "mout_core", DIV_CPU0, 0, 3,
		CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),

	/* DIV_CPU1 */
	DIV(CLK_DIV_HPM, "div_hpm", "div_copy", DIV_CPU1, 4, 3),
	DIV(CLK_DIV_COPY, "div_copy", "mout_hpm", DIV_CPU1, 0, 3),
};

static struct samsung_gate_clock exynos4415_gate_clks[] __initdata = {
	/*
	 * NOTE: Following table is sorted by register address in ascending
	 * order and then bitfield shift in descending order, as it is done
	 * in the User's Manual. When adding new entries, please make sure
	 * that the order is preserved, to avoid merge conflicts and make
	 * further work with defined data easier.
	 */

	/* GATE_IP_LEFTBUS */
	GATE(CLK_ASYNC_G3D, "async_g3d", "div_aclk_100", GATE_IP_LEFTBUS, 6,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNC_MFCL, "async_mfcl", "div_aclk_100", GATE_IP_LEFTBUS, 4,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNC_TVX, "async_tvx", "div_aclk_100", GATE_IP_LEFTBUS, 3,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMULEFT, "ppmuleft", "div_aclk_100", GATE_IP_LEFTBUS, 1,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GPIO_LEFT, "gpio_left", "div_aclk_100", GATE_IP_LEFTBUS, 0,
		CLK_IGNORE_UNUSED, 0),

	/* GATE_IP_IMAGE */
	GATE(CLK_PPMUIMAGE, "ppmuimage", "div_aclk_100", GATE_IP_IMAGE,
		9, 0, 0),
	GATE(CLK_QEMDMA2, "qe_mdma2", "div_aclk_100", GATE_IP_IMAGE,
		8, 0, 0),
	GATE(CLK_QEROTATOR, "qe_rotator", "div_aclk_100", GATE_IP_IMAGE,
		7, 0, 0),
	GATE(CLK_SMMUMDMA2, "smmu_mdam2", "div_aclk_100", GATE_IP_IMAGE,
		5, 0, 0),
	GATE(CLK_SMMUROTATOR, "smmu_rotator", "div_aclk_100", GATE_IP_IMAGE,
		4, 0, 0),
	GATE(CLK_MDMA2, "mdma2", "div_aclk_100", GATE_IP_IMAGE, 2, 0, 0),
	GATE(CLK_ROTATOR, "rotator", "div_aclk_100", GATE_IP_IMAGE, 1, 0, 0),

	/* GATE_IP_RIGHTBUS */
	GATE(CLK_ASYNC_ISPMX, "async_ispmx", "div_aclk_100",
		GATE_IP_RIGHTBUS, 9, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNC_MAUDIOX, "async_maudiox", "div_aclk_100",
		GATE_IP_RIGHTBUS, 7, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNC_MFCR, "async_mfcr", "div_aclk_100",
		GATE_IP_RIGHTBUS, 6, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNC_FSYSD, "async_fsysd", "div_aclk_100",
		GATE_IP_RIGHTBUS, 5, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNC_LCD0X, "async_lcd0x", "div_aclk_100",
		GATE_IP_RIGHTBUS, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNC_CAMX, "async_camx", "div_aclk_100",
		GATE_IP_RIGHTBUS, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMURIGHT, "ppmuright", "div_aclk_100",
		GATE_IP_RIGHTBUS, 1, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GPIO_RIGHT, "gpio_right", "div_aclk_100",
		GATE_IP_RIGHTBUS, 0, CLK_IGNORE_UNUSED, 0),

	/* GATE_IP_PERIR */
	GATE(CLK_ANTIRBK_APBIF, "antirbk_apbif", "div_aclk_100",
		GATE_IP_PERIR, 24, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_EFUSE_WRITER_APBIF, "efuse_writer_apbif", "div_aclk_100",
		GATE_IP_PERIR, 23, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_MONOCNT, "monocnt", "div_aclk_100", GATE_IP_PERIR, 22,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_TZPC6, "tzpc6", "div_aclk_100", GATE_IP_PERIR, 21,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PROVISIONKEY1, "provisionkey1", "div_aclk_100",
		GATE_IP_PERIR, 20, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PROVISIONKEY0, "provisionkey0", "div_aclk_100",
		GATE_IP_PERIR, 19, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_CMU_ISPPART, "cmu_isppart", "div_aclk_100", GATE_IP_PERIR, 18,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_TMU_APBIF, "tmu_apbif", "div_aclk_100",
		GATE_IP_PERIR, 17, 0, 0),
	GATE(CLK_KEYIF, "keyif", "div_aclk_100", GATE_IP_PERIR, 16, 0, 0),
	GATE(CLK_RTC, "rtc", "div_aclk_100", GATE_IP_PERIR, 15, 0, 0),
	GATE(CLK_WDT, "wdt", "div_aclk_100", GATE_IP_PERIR, 14, 0, 0),
	GATE(CLK_MCT, "mct", "div_aclk_100", GATE_IP_PERIR, 13, 0, 0),
	GATE(CLK_SECKEY, "seckey", "div_aclk_100", GATE_IP_PERIR, 12,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_HDMI_CEC, "hdmi_cec", "div_aclk_100", GATE_IP_PERIR, 11,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_TZPC5, "tzpc5", "div_aclk_100", GATE_IP_PERIR, 10,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_TZPC4, "tzpc4", "div_aclk_100", GATE_IP_PERIR, 9,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_TZPC3, "tzpc3", "div_aclk_100", GATE_IP_PERIR, 8,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_TZPC2, "tzpc2", "div_aclk_100", GATE_IP_PERIR, 7,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_TZPC1, "tzpc1", "div_aclk_100", GATE_IP_PERIR, 6,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_TZPC0, "tzpc0", "div_aclk_100", GATE_IP_PERIR, 5,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_CMU_COREPART, "cmu_corepart", "div_aclk_100", GATE_IP_PERIR, 4,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_CMU_TOPPART, "cmu_toppart", "div_aclk_100", GATE_IP_PERIR, 3,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PMU_APBIF, "pmu_apbif", "div_aclk_100", GATE_IP_PERIR, 2,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SYSREG, "sysreg", "div_aclk_100", GATE_IP_PERIR, 1,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_CHIP_ID, "chip_id", "div_aclk_100", GATE_IP_PERIR, 0,
		CLK_IGNORE_UNUSED, 0),

	/* GATE_SCLK_CAM - non-completed */
	GATE(CLK_SCLK_PXLAYSNC_CSIS1_FIMC, "sclk_pxlasync_csis1_fimc",
		"div_pxlasync_csis1_fimc", GATE_SCLK_CAM, 11,
		CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PXLAYSNC_CSIS0_FIMC, "sclk_pxlasync_csis0_fimc",
		"div_pxlasync_csis0_fimc", GATE_SCLK_CAM,
		10, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_JPEG, "sclk_jpeg", "div_jpeg",
		GATE_SCLK_CAM, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_CSIS1, "sclk_csis1", "div_csis1",
		GATE_SCLK_CAM, 7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_CSIS0, "sclk_csis0", "div_csis0",
		GATE_SCLK_CAM, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_CAM1, "sclk_cam1", "div_cam1",
		GATE_SCLK_CAM, 5, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_FIMC3_LCLK, "sclk_fimc3_lclk", "div_fimc3_lclk",
		GATE_SCLK_CAM, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_FIMC2_LCLK, "sclk_fimc2_lclk", "div_fimc2_lclk",
		GATE_SCLK_CAM, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_FIMC1_LCLK, "sclk_fimc1_lclk", "div_fimc1_lclk",
		GATE_SCLK_CAM, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_FIMC0_LCLK, "sclk_fimc0_lclk", "div_fimc0_lclk",
		GATE_SCLK_CAM, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_TV */
	GATE(CLK_SCLK_PIXEL, "sclk_pixel", "div_tv_blk",
		GATE_SCLK_TV, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_HDMI, "sclk_hdmi", "mout_hdmi",
		GATE_SCLK_TV, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MIXER, "sclk_mixer", "div_tv_blk",
		GATE_SCLK_TV, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_MFC */
	GATE(CLK_SCLK_MFC, "sclk_mfc", "div_mfc",
		GATE_SCLK_MFC, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_G3D */
	GATE(CLK_SCLK_G3D, "sclk_g3d", "div_g3d",
		GATE_SCLK_G3D, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_LCD */
	GATE(CLK_SCLK_MIPIDPHY4L, "sclk_mipidphy4l", "div_mipi0",
		GATE_SCLK_LCD, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MIPI0, "sclk_mipi0", "div_mipi0_pre",
		GATE_SCLK_LCD, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MDNIE0, "sclk_mdnie0", "div_fimd0",
		GATE_SCLK_LCD, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_FIMD0, "sclk_fimd0", "div_fimd0",
		GATE_SCLK_LCD, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_MAUDIO */
	GATE(CLK_SCLK_PCM0, "sclk_pcm0", "div_pcm0",
		GATE_SCLK_MAUDIO, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_AUDIO0, "sclk_audio0", "div_audio0",
		GATE_SCLK_MAUDIO, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_FSYS */
	GATE(CLK_SCLK_TSADC, "sclk_tsadc", "div_tsadc_pre",
		GATE_SCLK_FSYS, 9, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_EBI, "sclk_ebi", "div_ebi",
		GATE_SCLK_FSYS, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC2, "sclk_mmc2", "div_mmc2_pre",
		GATE_SCLK_FSYS, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC1, "sclk_mmc1", "div_mmc1_pre",
		GATE_SCLK_FSYS, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC0, "sclk_mmc0", "div_mmc0_pre",
		GATE_SCLK_FSYS, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_PERIL */
	GATE(CLK_SCLK_I2S, "sclk_i2s1", "div_i2s1",
		GATE_SCLK_PERIL, 18, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PCM2, "sclk_pcm2", "div_pcm2",
		GATE_SCLK_PERIL, 16, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PCM1, "sclk_pcm1", "div_pcm1",
		GATE_SCLK_PERIL, 15, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_AUDIO2, "sclk_audio2", "div_audio2",
		GATE_SCLK_PERIL, 14, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_AUDIO1, "sclk_audio1", "div_audio1",
		GATE_SCLK_PERIL, 13, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPDIF, "sclk_spdif", "mout_spdif",
		GATE_SCLK_PERIL, 10, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI2, "sclk_spi2", "div_spi2_pre",
		GATE_SCLK_PERIL, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI1, "sclk_spi1", "div_spi1_pre",
		GATE_SCLK_PERIL, 7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0, "sclk_spi0", "div_spi0_pre",
		GATE_SCLK_PERIL, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART3, "sclk_uart3", "div_uart3",
		GATE_SCLK_PERIL, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART2, "sclk_uart2", "div_uart2",
		GATE_SCLK_PERIL, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART1, "sclk_uart1", "div_uart1",
		GATE_SCLK_PERIL, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART0, "sclk_uart0", "div_uart0",
		GATE_SCLK_PERIL, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_IP_CAM */
	GATE(CLK_SMMUFIMC_LITE2, "smmufimc_lite2", "div_aclk_160", GATE_IP_CAM,
		22, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_FIMC_LITE2, "fimc_lite2", "div_aclk_160", GATE_IP_CAM,
		20, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PIXELASYNCM1, "pixelasyncm1", "div_aclk_160", GATE_IP_CAM,
		18, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PIXELASYNCM0, "pixelasyncm0", "div_aclk_160", GATE_IP_CAM,
		17, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMUCAMIF, "ppmucamif", "div_aclk_160", GATE_IP_CAM,
		16, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMUJPEG, "smmujpeg", "div_aclk_160", GATE_IP_CAM, 11, 0, 0),
	GATE(CLK_SMMUFIMC3, "smmufimc3", "div_aclk_160", GATE_IP_CAM, 10, 0, 0),
	GATE(CLK_SMMUFIMC2, "smmufimc2", "div_aclk_160", GATE_IP_CAM, 9, 0, 0),
	GATE(CLK_SMMUFIMC1, "smmufimc1", "div_aclk_160", GATE_IP_CAM, 8, 0, 0),
	GATE(CLK_SMMUFIMC0, "smmufimc0", "div_aclk_160", GATE_IP_CAM, 7, 0, 0),
	GATE(CLK_JPEG, "jpeg", "div_aclk_160", GATE_IP_CAM, 6, 0, 0),
	GATE(CLK_CSIS1, "csis1", "div_aclk_160", GATE_IP_CAM, 5, 0, 0),
	GATE(CLK_CSIS0, "csis0", "div_aclk_160", GATE_IP_CAM, 4, 0, 0),
	GATE(CLK_FIMC3, "fimc3", "div_aclk_160", GATE_IP_CAM, 3, 0, 0),
	GATE(CLK_FIMC2, "fimc2", "div_aclk_160", GATE_IP_CAM, 2, 0, 0),
	GATE(CLK_FIMC1, "fimc1", "div_aclk_160", GATE_IP_CAM, 1, 0, 0),
	GATE(CLK_FIMC0, "fimc0", "div_aclk_160", GATE_IP_CAM, 0, 0, 0),

	/* GATE_IP_TV */
	GATE(CLK_PPMUTV, "ppmutv", "div_aclk_100", GATE_IP_TV, 5, 0, 0),
	GATE(CLK_SMMUTV, "smmutv", "div_aclk_100", GATE_IP_TV, 4, 0, 0),
	GATE(CLK_HDMI, "hdmi", "div_aclk_100", GATE_IP_TV, 3, 0, 0),
	GATE(CLK_MIXER, "mixer", "div_aclk_100", GATE_IP_TV, 1, 0, 0),
	GATE(CLK_VP, "vp", "div_aclk_100", GATE_IP_TV, 0, 0, 0),

	/* GATE_IP_MFC */
	GATE(CLK_PPMUMFC_R, "ppmumfc_r", "div_aclk_200", GATE_IP_MFC, 4,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMUMFC_L, "ppmumfc_l", "div_aclk_200", GATE_IP_MFC, 3,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMUMFC_R, "smmumfc_r", "div_aclk_200", GATE_IP_MFC, 2, 0, 0),
	GATE(CLK_SMMUMFC_L, "smmumfc_l", "div_aclk_200", GATE_IP_MFC, 1, 0, 0),
	GATE(CLK_MFC, "mfc", "div_aclk_200", GATE_IP_MFC, 0, 0, 0),

	/* GATE_IP_G3D */
	GATE(CLK_PPMUG3D, "ppmug3d", "div_aclk_200", GATE_IP_G3D, 1,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_G3D, "g3d", "div_aclk_200", GATE_IP_G3D, 0, 0, 0),

	/* GATE_IP_LCD */
	GATE(CLK_PPMULCD0, "ppmulcd0", "div_aclk_160", GATE_IP_LCD, 5,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMUFIMD0, "smmufimd0", "div_aclk_160", GATE_IP_LCD, 4, 0, 0),
	GATE(CLK_DSIM0, "dsim0", "div_aclk_160", GATE_IP_LCD, 3, 0, 0),
	GATE(CLK_SMIES, "smies", "div_aclk_160", GATE_IP_LCD, 2, 0, 0),
	GATE(CLK_MIE0, "mie0", "div_aclk_160", GATE_IP_LCD, 1, 0, 0),
	GATE(CLK_FIMD0, "fimd0", "div_aclk_160", GATE_IP_LCD, 0, 0, 0),

	/* GATE_IP_FSYS */
	GATE(CLK_TSADC, "tsadc", "div_aclk_200", GATE_IP_FSYS, 20, 0, 0),
	GATE(CLK_PPMUFILE, "ppmufile", "div_aclk_200", GATE_IP_FSYS, 17,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_NFCON, "nfcon", "div_aclk_200", GATE_IP_FSYS, 16, 0, 0),
	GATE(CLK_USBDEVICE, "usbdevice", "div_aclk_200", GATE_IP_FSYS, 13,
		0, 0),
	GATE(CLK_USBHOST, "usbhost", "div_aclk_200", GATE_IP_FSYS, 12, 0, 0),
	GATE(CLK_SROMC, "sromc", "div_aclk_200", GATE_IP_FSYS, 11, 0, 0),
	GATE(CLK_SDMMC2, "sdmmc2", "div_aclk_200", GATE_IP_FSYS, 7, 0, 0),
	GATE(CLK_SDMMC1, "sdmmc1", "div_aclk_200", GATE_IP_FSYS, 6, 0, 0),
	GATE(CLK_SDMMC0, "sdmmc0", "div_aclk_200", GATE_IP_FSYS, 5, 0, 0),
	GATE(CLK_PDMA1, "pdma1", "div_aclk_200", GATE_IP_FSYS, 1, 0, 0),
	GATE(CLK_PDMA0, "pdma0", "div_aclk_200", GATE_IP_FSYS, 0, 0, 0),

	/* GATE_IP_PERIL */
	GATE(CLK_SPDIF, "spdif", "div_aclk_100", GATE_IP_PERIL, 26, 0, 0),
	GATE(CLK_PWM, "pwm", "div_aclk_100", GATE_IP_PERIL, 24, 0, 0),
	GATE(CLK_PCM2, "pcm2", "div_aclk_100", GATE_IP_PERIL, 23, 0, 0),
	GATE(CLK_PCM1, "pcm1", "div_aclk_100", GATE_IP_PERIL, 22, 0, 0),
	GATE(CLK_I2S1, "i2s1", "div_aclk_100", GATE_IP_PERIL, 20, 0, 0),
	GATE(CLK_SPI2, "spi2", "div_aclk_100", GATE_IP_PERIL, 18, 0, 0),
	GATE(CLK_SPI1, "spi1", "div_aclk_100", GATE_IP_PERIL, 17, 0, 0),
	GATE(CLK_SPI0, "spi0", "div_aclk_100", GATE_IP_PERIL, 16, 0, 0),
	GATE(CLK_I2CHDMI, "i2chdmi", "div_aclk_100", GATE_IP_PERIL, 14, 0, 0),
	GATE(CLK_I2C7, "i2c7", "div_aclk_100", GATE_IP_PERIL, 13, 0, 0),
	GATE(CLK_I2C6, "i2c6", "div_aclk_100", GATE_IP_PERIL, 12, 0, 0),
	GATE(CLK_I2C5, "i2c5", "div_aclk_100", GATE_IP_PERIL, 11, 0, 0),
	GATE(CLK_I2C4, "i2c4", "div_aclk_100", GATE_IP_PERIL, 10, 0, 0),
	GATE(CLK_I2C3, "i2c3", "div_aclk_100", GATE_IP_PERIL, 9, 0, 0),
	GATE(CLK_I2C2, "i2c2", "div_aclk_100", GATE_IP_PERIL, 8, 0, 0),
	GATE(CLK_I2C1, "i2c1", "div_aclk_100", GATE_IP_PERIL, 7, 0, 0),
	GATE(CLK_I2C0, "i2c0", "div_aclk_100", GATE_IP_PERIL, 6, 0, 0),
	GATE(CLK_UART3, "uart3", "div_aclk_100", GATE_IP_PERIL, 3, 0, 0),
	GATE(CLK_UART2, "uart2", "div_aclk_100", GATE_IP_PERIL, 2, 0, 0),
	GATE(CLK_UART1, "uart1", "div_aclk_100", GATE_IP_PERIL, 1, 0, 0),
	GATE(CLK_UART0, "uart0", "div_aclk_100", GATE_IP_PERIL, 0, 0, 0),
};

/*
 * APLL & MPLL & BPLL & ISP_PLL & DISP_PLL & G3D_PLL
 */
static struct samsung_pll_rate_table exynos4415_pll_rates[] = {
	PLL_35XX_RATE(1600000000, 400, 3,  1),
	PLL_35XX_RATE(1500000000, 250, 2,  1),
	PLL_35XX_RATE(1400000000, 175, 3,  0),
	PLL_35XX_RATE(1300000000, 325, 3,  1),
	PLL_35XX_RATE(1200000000, 400, 4,  1),
	PLL_35XX_RATE(1100000000, 275, 3,  1),
	PLL_35XX_RATE(1066000000, 533, 6,  1),
	PLL_35XX_RATE(1000000000, 250, 3,  1),
	PLL_35XX_RATE(960000000,  320, 4,  1),
	PLL_35XX_RATE(900000000,  300, 4,  1),
	PLL_35XX_RATE(850000000,  425, 6,  1),
	PLL_35XX_RATE(800000000,  200, 3,  1),
	PLL_35XX_RATE(700000000,  175, 3,  1),
	PLL_35XX_RATE(667000000,  667, 12, 1),
	PLL_35XX_RATE(600000000,  400, 4,  2),
	PLL_35XX_RATE(550000000,  275, 3,  2),
	PLL_35XX_RATE(533000000,  533, 6,  2),
	PLL_35XX_RATE(520000000,  260, 3,  2),
	PLL_35XX_RATE(500000000,  250, 3,  2),
	PLL_35XX_RATE(440000000,  220, 3,  2),
	PLL_35XX_RATE(400000000,  200, 3,  2),
	PLL_35XX_RATE(350000000,  175, 3,  2),
	PLL_35XX_RATE(300000000,  300, 3,  3),
	PLL_35XX_RATE(266000000,  266, 3,  3),
	PLL_35XX_RATE(200000000,  200, 3,  3),
	PLL_35XX_RATE(160000000,  160, 3,  3),
	PLL_35XX_RATE(100000000,  200, 3,  4),
	{ /* sentinel */ }
};

/* EPLL */
static struct samsung_pll_rate_table exynos4415_epll_rates[] = {
	PLL_36XX_RATE(800000000, 200, 3, 1,     0),
	PLL_36XX_RATE(288000000,  96, 2, 2,     0),
	PLL_36XX_RATE(192000000, 128, 2, 3,     0),
	PLL_36XX_RATE(144000000,  96, 2, 3,     0),
	PLL_36XX_RATE(96000000,  128, 2, 4,     0),
	PLL_36XX_RATE(84000000,  112, 2, 4,     0),
	PLL_36XX_RATE(80750011,  107, 2, 4, 43691),
	PLL_36XX_RATE(73728004,   98, 2, 4, 19923),
	PLL_36XX_RATE(67987602,  271, 3, 5, 62285),
	PLL_36XX_RATE(65911004,  175, 2, 5, 49982),
	PLL_36XX_RATE(50000000,  200, 3, 5,     0),
	PLL_36XX_RATE(49152003,  131, 2, 5,  4719),
	PLL_36XX_RATE(48000000,  128, 2, 5,     0),
	PLL_36XX_RATE(45250000,  181, 3, 5,     0),
	{ /* sentinel */ }
};

static struct samsung_pll_clock exynos4415_plls[] __initdata = {
	PLL(pll_35xx, CLK_FOUT_APLL, "fout_apll", "fin_pll",
		APLL_LOCK, APLL_CON0, exynos4415_pll_rates),
	PLL(pll_36xx, CLK_FOUT_EPLL, "fout_epll", "fin_pll",
		EPLL_LOCK, EPLL_CON0, exynos4415_epll_rates),
	PLL(pll_35xx, CLK_FOUT_G3D_PLL, "fout_g3d_pll", "mout_g3d_pllsrc",
		G3D_PLL_LOCK, G3D_PLL_CON0, exynos4415_pll_rates),
	PLL(pll_35xx, CLK_FOUT_ISP_PLL, "fout_isp_pll", "fin_pll",
		ISP_PLL_LOCK, ISP_PLL_CON0, exynos4415_pll_rates),
	PLL(pll_35xx, CLK_FOUT_DISP_PLL, "fout_disp_pll",
		"fin_pll", DISP_PLL_LOCK, DISP_PLL_CON0, exynos4415_pll_rates),
};

static struct samsung_cmu_info cmu_info __initdata = {
	.pll_clks		= exynos4415_plls,
	.nr_pll_clks		= ARRAY_SIZE(exynos4415_plls),
	.mux_clks		= exynos4415_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(exynos4415_mux_clks),
	.div_clks		= exynos4415_div_clks,
	.nr_div_clks		= ARRAY_SIZE(exynos4415_div_clks),
	.gate_clks		= exynos4415_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(exynos4415_gate_clks),
	.fixed_clks		= exynos4415_fixed_rate_clks,
	.nr_fixed_clks		= ARRAY_SIZE(exynos4415_fixed_rate_clks),
	.fixed_factor_clks	= exynos4415_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(exynos4415_fixed_factor_clks),
	.nr_clk_ids		= CLK_NR_CLKS,
	.clk_regs		= exynos4415_cmu_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(exynos4415_cmu_clk_regs),
};

static void __init exynos4415_cmu_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &cmu_info);
}
CLK_OF_DECLARE(exynos4415_cmu, "samsung,exynos4415-cmu", exynos4415_cmu_init);

/*
 * CMU DMC
 */

#define MPLL_LOCK		0x008
#define MPLL_CON0		0x108
#define MPLL_CON1		0x10c
#define MPLL_CON2		0x110
#define BPLL_LOCK		0x118
#define BPLL_CON0		0x218
#define BPLL_CON1		0x21c
#define BPLL_CON2		0x220
#define SRC_DMC			0x300
#define DIV_DMC1		0x504

static unsigned long exynos4415_cmu_dmc_clk_regs[] __initdata = {
	MPLL_LOCK,
	MPLL_CON0,
	MPLL_CON1,
	MPLL_CON2,
	BPLL_LOCK,
	BPLL_CON0,
	BPLL_CON1,
	BPLL_CON2,
	SRC_DMC,
	DIV_DMC1,
};

PNAME(mout_mpll_p)		= { "fin_pll", "fout_mpll", };
PNAME(mout_bpll_p)		= { "fin_pll", "fout_bpll", };
PNAME(mbpll_p)			= { "mout_mpll", "mout_bpll", };

static struct samsung_mux_clock exynos4415_dmc_mux_clks[] __initdata = {
	MUX(CLK_DMC_MOUT_MPLL, "mout_mpll", mout_mpll_p, SRC_DMC, 12, 1),
	MUX(CLK_DMC_MOUT_BPLL, "mout_bpll", mout_bpll_p, SRC_DMC, 10, 1),
	MUX(CLK_DMC_MOUT_DPHY, "mout_dphy", mbpll_p, SRC_DMC, 8, 1),
	MUX(CLK_DMC_MOUT_DMC_BUS, "mout_dmc_bus", mbpll_p, SRC_DMC, 4, 1),
};

static struct samsung_div_clock exynos4415_dmc_div_clks[] __initdata = {
	DIV(CLK_DMC_DIV_DMC, "div_dmc", "div_dmc_pre", DIV_DMC1, 27, 3),
	DIV(CLK_DMC_DIV_DPHY, "div_dphy", "mout_dphy", DIV_DMC1, 23, 3),
	DIV(CLK_DMC_DIV_DMC_PRE, "div_dmc_pre", "mout_dmc_bus",
		DIV_DMC1, 19, 2),
	DIV(CLK_DMC_DIV_DMCP, "div_dmcp", "div_dmcd", DIV_DMC1, 15, 3),
	DIV(CLK_DMC_DIV_DMCD, "div_dmcd", "div_dmc", DIV_DMC1, 11, 3),
	DIV(CLK_DMC_DIV_MPLL_PRE, "div_mpll_pre", "mout_mpll", DIV_DMC1, 8, 2),
};

static struct samsung_pll_clock exynos4415_dmc_plls[] __initdata = {
	PLL(pll_35xx, CLK_DMC_FOUT_MPLL, "fout_mpll", "fin_pll",
		MPLL_LOCK, MPLL_CON0, exynos4415_pll_rates),
	PLL(pll_35xx, CLK_DMC_FOUT_BPLL, "fout_bpll", "fin_pll",
		BPLL_LOCK, BPLL_CON0, exynos4415_pll_rates),
};

static struct samsung_cmu_info cmu_dmc_info __initdata = {
	.pll_clks		= exynos4415_dmc_plls,
	.nr_pll_clks		= ARRAY_SIZE(exynos4415_dmc_plls),
	.mux_clks		= exynos4415_dmc_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(exynos4415_dmc_mux_clks),
	.div_clks		= exynos4415_dmc_div_clks,
	.nr_div_clks		= ARRAY_SIZE(exynos4415_dmc_div_clks),
	.nr_clk_ids		= NR_CLKS_DMC,
	.clk_regs		= exynos4415_cmu_dmc_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(exynos4415_cmu_dmc_clk_regs),
};

static void __init exynos4415_cmu_dmc_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &cmu_dmc_info);
}
CLK_OF_DECLARE(exynos4415_cmu_dmc, "samsung,exynos4415-cmu-dmc",
		exynos4415_cmu_dmc_init);
