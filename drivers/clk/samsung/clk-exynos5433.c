/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for Exynos5443 SoC.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>

#include <dt-bindings/clock/exynos5433.h>

#include "clk.h"
#include "clk-pll.h"

/*
 * Register offset definitions for CMU_TOP
 */
#define ISP_PLL_LOCK			0x0000
#define AUD_PLL_LOCK			0x0004
#define ISP_PLL_CON0			0x0100
#define ISP_PLL_CON1			0x0104
#define ISP_PLL_FREQ_DET		0x0108
#define AUD_PLL_CON0			0x0110
#define AUD_PLL_CON1			0x0114
#define AUD_PLL_CON2			0x0118
#define AUD_PLL_FREQ_DET		0x011c
#define MUX_SEL_TOP0			0x0200
#define MUX_SEL_TOP1			0x0204
#define MUX_SEL_TOP2			0x0208
#define MUX_SEL_TOP3			0x020c
#define MUX_SEL_TOP4			0x0210
#define MUX_SEL_TOP_MSCL		0x0220
#define MUX_SEL_TOP_CAM1		0x0224
#define MUX_SEL_TOP_DISP		0x0228
#define MUX_SEL_TOP_FSYS0		0x0230
#define MUX_SEL_TOP_FSYS1		0x0234
#define MUX_SEL_TOP_PERIC0		0x0238
#define MUX_SEL_TOP_PERIC1		0x023c
#define MUX_ENABLE_TOP0			0x0300
#define MUX_ENABLE_TOP1			0x0304
#define MUX_ENABLE_TOP2			0x0308
#define MUX_ENABLE_TOP3			0x030c
#define MUX_ENABLE_TOP4			0x0310
#define MUX_ENABLE_TOP_MSCL		0x0320
#define MUX_ENABLE_TOP_CAM1		0x0324
#define MUX_ENABLE_TOP_DISP		0x0328
#define MUX_ENABLE_TOP_FSYS0		0x0330
#define MUX_ENABLE_TOP_FSYS1		0x0334
#define MUX_ENABLE_TOP_PERIC0		0x0338
#define MUX_ENABLE_TOP_PERIC1		0x033c
#define MUX_STAT_TOP0			0x0400
#define MUX_STAT_TOP1			0x0404
#define MUX_STAT_TOP2			0x0408
#define MUX_STAT_TOP3			0x040c
#define MUX_STAT_TOP4			0x0410
#define MUX_STAT_TOP_MSCL		0x0420
#define MUX_STAT_TOP_CAM1		0x0424
#define MUX_STAT_TOP_FSYS0		0x0430
#define MUX_STAT_TOP_FSYS1		0x0434
#define MUX_STAT_TOP_PERIC0		0x0438
#define MUX_STAT_TOP_PERIC1		0x043c
#define DIV_TOP0			0x0600
#define DIV_TOP1			0x0604
#define DIV_TOP2			0x0608
#define DIV_TOP3			0x060c
#define DIV_TOP4			0x0610
#define DIV_TOP_MSCL			0x0618
#define DIV_TOP_CAM10			0x061c
#define DIV_TOP_CAM11			0x0620
#define DIV_TOP_FSYS0			0x062c
#define DIV_TOP_FSYS1			0x0630
#define DIV_TOP_FSYS2			0x0634
#define DIV_TOP_PERIC0			0x0638
#define DIV_TOP_PERIC1			0x063c
#define DIV_TOP_PERIC2			0x0640
#define DIV_TOP_PERIC3			0x0644
#define DIV_TOP_PERIC4			0x0648
#define DIV_TOP_PLL_FREQ_DET		0x064c
#define DIV_STAT_TOP0			0x0700
#define DIV_STAT_TOP1			0x0704
#define DIV_STAT_TOP2			0x0708
#define DIV_STAT_TOP3			0x070c
#define DIV_STAT_TOP4			0x0710
#define DIV_STAT_TOP_MSCL		0x0718
#define DIV_STAT_TOP_CAM10		0x071c
#define DIV_STAT_TOP_CAM11		0x0720
#define DIV_STAT_TOP_FSYS0		0x072c
#define DIV_STAT_TOP_FSYS1		0x0730
#define DIV_STAT_TOP_FSYS2		0x0734
#define DIV_STAT_TOP_PERIC0		0x0738
#define DIV_STAT_TOP_PERIC1		0x073c
#define DIV_STAT_TOP_PERIC2		0x0740
#define DIV_STAT_TOP_PERIC3		0x0744
#define DIV_STAT_TOP_PLL_FREQ_DET	0x074c
#define ENABLE_ACLK_TOP			0x0800
#define ENABLE_SCLK_TOP			0x0a00
#define ENABLE_SCLK_TOP_MSCL		0x0a04
#define ENABLE_SCLK_TOP_CAM1		0x0a08
#define ENABLE_SCLK_TOP_DISP		0x0a0c
#define ENABLE_SCLK_TOP_FSYS		0x0a10
#define ENABLE_SCLK_TOP_PERIC		0x0a14
#define ENABLE_IP_TOP			0x0b00
#define ENABLE_CMU_TOP			0x0c00
#define ENABLE_CMU_TOP_DIV_STAT		0x0c04

static unsigned long top_clk_regs[] __initdata = {
	ISP_PLL_LOCK,
	AUD_PLL_LOCK,
	ISP_PLL_CON0,
	ISP_PLL_CON1,
	ISP_PLL_FREQ_DET,
	AUD_PLL_CON0,
	AUD_PLL_CON1,
	AUD_PLL_CON2,
	AUD_PLL_FREQ_DET,
	MUX_SEL_TOP0,
	MUX_SEL_TOP1,
	MUX_SEL_TOP2,
	MUX_SEL_TOP3,
	MUX_SEL_TOP4,
	MUX_SEL_TOP_MSCL,
	MUX_SEL_TOP_CAM1,
	MUX_SEL_TOP_DISP,
	MUX_SEL_TOP_FSYS0,
	MUX_SEL_TOP_FSYS1,
	MUX_SEL_TOP_PERIC0,
	MUX_SEL_TOP_PERIC1,
	MUX_ENABLE_TOP0,
	MUX_ENABLE_TOP1,
	MUX_ENABLE_TOP2,
	MUX_ENABLE_TOP3,
	MUX_ENABLE_TOP4,
	MUX_ENABLE_TOP_MSCL,
	MUX_ENABLE_TOP_CAM1,
	MUX_ENABLE_TOP_DISP,
	MUX_ENABLE_TOP_FSYS0,
	MUX_ENABLE_TOP_FSYS1,
	MUX_ENABLE_TOP_PERIC0,
	MUX_ENABLE_TOP_PERIC1,
	MUX_STAT_TOP0,
	MUX_STAT_TOP1,
	MUX_STAT_TOP2,
	MUX_STAT_TOP3,
	MUX_STAT_TOP4,
	MUX_STAT_TOP_MSCL,
	MUX_STAT_TOP_CAM1,
	MUX_STAT_TOP_FSYS0,
	MUX_STAT_TOP_FSYS1,
	MUX_STAT_TOP_PERIC0,
	MUX_STAT_TOP_PERIC1,
	DIV_TOP0,
	DIV_TOP1,
	DIV_TOP2,
	DIV_TOP3,
	DIV_TOP4,
	DIV_TOP_MSCL,
	DIV_TOP_CAM10,
	DIV_TOP_CAM11,
	DIV_TOP_FSYS0,
	DIV_TOP_FSYS1,
	DIV_TOP_FSYS2,
	DIV_TOP_PERIC0,
	DIV_TOP_PERIC1,
	DIV_TOP_PERIC2,
	DIV_TOP_PERIC3,
	DIV_TOP_PERIC4,
	DIV_TOP_PLL_FREQ_DET,
	DIV_STAT_TOP0,
	DIV_STAT_TOP1,
	DIV_STAT_TOP2,
	DIV_STAT_TOP3,
	DIV_STAT_TOP4,
	DIV_STAT_TOP_MSCL,
	DIV_STAT_TOP_CAM10,
	DIV_STAT_TOP_CAM11,
	DIV_STAT_TOP_FSYS0,
	DIV_STAT_TOP_FSYS1,
	DIV_STAT_TOP_FSYS2,
	DIV_STAT_TOP_PERIC0,
	DIV_STAT_TOP_PERIC1,
	DIV_STAT_TOP_PERIC2,
	DIV_STAT_TOP_PERIC3,
	DIV_STAT_TOP_PLL_FREQ_DET,
	ENABLE_ACLK_TOP,
	ENABLE_SCLK_TOP,
	ENABLE_SCLK_TOP_MSCL,
	ENABLE_SCLK_TOP_CAM1,
	ENABLE_SCLK_TOP_DISP,
	ENABLE_SCLK_TOP_FSYS,
	ENABLE_SCLK_TOP_PERIC,
	ENABLE_IP_TOP,
	ENABLE_CMU_TOP,
	ENABLE_CMU_TOP_DIV_STAT,
};

/* list of all parent clock list */
PNAME(mout_aud_pll_p)		= { "oscclk", "fout_aud_pll", };
PNAME(mout_isp_pll_p)		= { "oscclk", "fout_isp_pll", };
PNAME(mout_aud_pll_user_p)	= { "oscclk", "mout_aud_pll", };
PNAME(mout_mphy_pll_user_p)	= { "oscclk", "sclk_mphy_pll", };
PNAME(mout_mfc_pll_user_p)	= { "oscclk", "sclk_mfc_pll", };
PNAME(mout_bus_pll_user_p)	= { "oscclk", "sclk_bus_pll", };
PNAME(mout_bus_pll_user_t_p)	= { "oscclk", "mout_bus_pll_user", };
PNAME(mout_mphy_pll_user_t_p)	= { "oscclk", "mout_mphy_pll_user", };

PNAME(mout_bus_mfc_pll_user_p)	= { "mout_bus_pll_user", "mout_mfc_pll_user",};
PNAME(mout_mfc_bus_pll_user_p)	= { "mout_mfc_pll_user", "mout_bus_pll_user",};
PNAME(mout_aclk_cam1_552_b_p)	= { "mout_aclk_cam1_552_a",
				    "mout_mfc_pll_user", };
PNAME(mout_aclk_cam1_552_a_p)	= { "mout_isp_pll", "mout_bus_pll_user", };

PNAME(mout_aclk_mfc_400_c_p)	= { "mout_aclk_mfc_400_b",
				    "mout_mphy_pll_user", };
PNAME(mout_aclk_mfc_400_b_p)	= { "mout_aclk_mfc_400_a",
				    "mout_bus_pll_user", };
PNAME(mout_aclk_mfc_400_a_p)	= { "mout_mfc_pll_user", "mout_isp_pll", };

PNAME(mout_bus_mphy_pll_user_p)	= { "mout_bus_pll_user",
				    "mout_mphy_pll_user", };
PNAME(mout_aclk_mscl_b_p)	= { "mout_aclk_mscl_400_a",
				    "mout_mphy_pll_user", };
PNAME(mout_aclk_g2d_400_b_p)	= { "mout_aclk_g2d_400_a",
				    "mout_mphy_pll_user", };

PNAME(mout_sclk_jpeg_c_p)	= { "mout_sclk_jpeg_b", "mout_mphy_pll_user",};
PNAME(mout_sclk_jpeg_b_p)	= { "mout_sclk_jpeg_a", "mout_mfc_pll_user", };

PNAME(mout_sclk_mmc2_b_p)	= { "mout_sclk_mmc2_a", "mout_mfc_pll_user",};
PNAME(mout_sclk_mmc1_b_p)	= { "mout_sclk_mmc1_a", "mout_mfc_pll_user",};
PNAME(mout_sclk_mmc0_d_p)	= { "mout_sclk_mmc0_c", "mout_isp_pll", };
PNAME(mout_sclk_mmc0_c_p)	= { "mout_sclk_mmc0_b", "mout_mphy_pll_user",};
PNAME(mout_sclk_mmc0_b_p)	= { "mout_sclk_mmc0_a", "mout_mfc_pll_user", };

PNAME(mout_sclk_spdif_p)	= { "sclk_audio0", "sclk_audio1",
				    "oscclk", "ioclk_spdif_extclk", };
PNAME(mout_sclk_audio1_p)	= { "ioclk_audiocdclk1", "oscclk",
				    "mout_aud_pll_user_t",};
PNAME(mout_sclk_audio0_p)	= { "ioclk_audiocdclk0", "oscclk",
				    "mout_aud_pll_user_t",};

static struct samsung_fixed_factor_clock top_fixed_factor_clks[] __initdata = {
	FFACTOR(0, "oscclk_efuse_common", "oscclk", 1, 1, 0),
};

static struct samsung_fixed_rate_clock top_fixed_clks[] __initdata = {
	/* Xi2s{0|1}CDCLK input clock for I2S/PCM */
	FRATE(0, "ioclk_audiocdclk1", NULL, CLK_IS_ROOT, 100000000),
	FRATE(0, "ioclk_audiocdclk0", NULL, CLK_IS_ROOT, 100000000),
	/* Xi2s1SDI input clock for SPDIF */
	FRATE(0, "ioclk_spdif_extclk", NULL, CLK_IS_ROOT, 100000000),
	/* XspiCLK[4:0] input clock for SPI */
	FRATE(0, "ioclk_spi4_clk_in", NULL, CLK_IS_ROOT, 50000000),
	FRATE(0, "ioclk_spi3_clk_in", NULL, CLK_IS_ROOT, 50000000),
	FRATE(0, "ioclk_spi2_clk_in", NULL, CLK_IS_ROOT, 50000000),
	FRATE(0, "ioclk_spi1_clk_in", NULL, CLK_IS_ROOT, 50000000),
	FRATE(0, "ioclk_spi0_clk_in", NULL, CLK_IS_ROOT, 50000000),
	/* Xi2s1SCLK input clock for I2S1_BCLK */
	FRATE(0, "ioclk_i2s1_bclk_in", NULL, CLK_IS_ROOT, 12288000),
};

static struct samsung_mux_clock top_mux_clks[] __initdata = {
	/* MUX_SEL_TOP0 */
	MUX(CLK_MOUT_AUD_PLL, "mout_aud_pll", mout_aud_pll_p, MUX_SEL_TOP0,
			4, 1),
	MUX(CLK_MOUT_ISP_PLL, "mout_isp_pll", mout_isp_pll_p, MUX_SEL_TOP0,
			0, 1),

	/* MUX_SEL_TOP1 */
	MUX(CLK_MOUT_AUD_PLL_USER_T, "mout_aud_pll_user_t",
			mout_aud_pll_user_p, MUX_SEL_TOP1, 12, 1),
	MUX(CLK_MOUT_MPHY_PLL_USER, "mout_mphy_pll_user", mout_mphy_pll_user_p,
			MUX_SEL_TOP1, 8, 1),
	MUX(CLK_MOUT_MFC_PLL_USER, "mout_mfc_pll_user", mout_mfc_pll_user_p,
			MUX_SEL_TOP1, 4, 1),
	MUX(CLK_MOUT_BUS_PLL_USER, "mout_bus_pll_user", mout_bus_pll_user_p,
			MUX_SEL_TOP1, 0, 1),

	/* MUX_SEL_TOP2 */
	MUX(CLK_MOUT_ACLK_HEVC_400, "mout_aclk_hevc_400",
			mout_bus_mfc_pll_user_p, MUX_SEL_TOP2, 28, 1),
	MUX(CLK_MOUT_ACLK_CAM1_333, "mout_aclk_cam1_333",
			mout_mfc_bus_pll_user_p, MUX_SEL_TOP2, 16, 1),
	MUX(CLK_MOUT_ACLK_CAM1_552_B, "mout_aclk_cam1_552_b",
			mout_aclk_cam1_552_b_p, MUX_SEL_TOP2, 12, 1),
	MUX(CLK_MOUT_ACLK_CAM1_552_A, "mout_aclk_cam1_552_a",
			mout_aclk_cam1_552_a_p, MUX_SEL_TOP2, 8, 1),
	MUX(CLK_MOUT_ACLK_ISP_DIS_400, "mout_aclk_isp_dis_400",
			mout_bus_mfc_pll_user_p, MUX_SEL_TOP2, 4, 1),
	MUX(CLK_MOUT_ACLK_ISP_400, "mout_aclk_isp_400",
			mout_bus_mfc_pll_user_p, MUX_SEL_TOP2, 0, 1),

	/* MUX_SEL_TOP3 */
	MUX(CLK_MOUT_ACLK_BUS0_400, "mout_aclk_bus0_400",
			mout_bus_mphy_pll_user_p, MUX_SEL_TOP3, 20, 1),
	MUX(CLK_MOUT_ACLK_MSCL_400_B, "mout_aclk_mscl_400_b",
			mout_aclk_mscl_b_p, MUX_SEL_TOP3, 16, 1),
	MUX(CLK_MOUT_ACLK_MSCL_400_A, "mout_aclk_mscl_400_a",
			mout_bus_mfc_pll_user_p, MUX_SEL_TOP3, 12, 1),
	MUX(CLK_MOUT_ACLK_GSCL_333, "mout_aclk_gscl_333",
			mout_mfc_bus_pll_user_p, MUX_SEL_TOP3, 8, 1),
	MUX(CLK_MOUT_ACLK_G2D_400_B, "mout_aclk_g2d_400_b",
			mout_aclk_g2d_400_b_p, MUX_SEL_TOP3, 4, 1),
	MUX(CLK_MOUT_ACLK_G2D_400_A, "mout_aclk_g2d_400_a",
			mout_bus_mfc_pll_user_p, MUX_SEL_TOP3, 0, 1),

	/* MUX_SEL_TOP4 */
	MUX(CLK_MOUT_ACLK_MFC_400_C, "mout_aclk_mfc_400_c",
			mout_aclk_mfc_400_c_p, MUX_SEL_TOP4, 8, 1),
	MUX(CLK_MOUT_ACLK_MFC_400_B, "mout_aclk_mfc_400_b",
			mout_aclk_mfc_400_b_p, MUX_SEL_TOP4, 4, 1),
	MUX(CLK_MOUT_ACLK_MFC_400_A, "mout_aclk_mfc_400_a",
			mout_aclk_mfc_400_a_p, MUX_SEL_TOP4, 0, 1),

	/* MUX_SEL_TOP_MSCL */
	MUX(CLK_MOUT_SCLK_JPEG_C, "mout_sclk_jpeg_c", mout_sclk_jpeg_c_p,
			MUX_SEL_TOP_MSCL, 8, 1),
	MUX(CLK_MOUT_SCLK_JPEG_B, "mout_sclk_jpeg_b", mout_sclk_jpeg_b_p,
			MUX_SEL_TOP_MSCL, 4, 1),
	MUX(CLK_MOUT_SCLK_JPEG_A, "mout_sclk_jpeg_a", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_MSCL, 0, 1),

	/* MUX_SEL_TOP_CAM1 */
	MUX(CLK_MOUT_SCLK_ISP_SENSOR2, "mout_sclk_isp_sensor2",
			mout_bus_pll_user_t_p, MUX_SEL_TOP_CAM1, 24, 1),
	MUX(CLK_MOUT_SCLK_ISP_SENSOR1, "mout_sclk_isp_sensor1",
			mout_bus_pll_user_t_p, MUX_SEL_TOP_CAM1, 20, 1),
	MUX(CLK_MOUT_SCLK_ISP_SENSOR0, "mout_sclk_isp_sensor0",
			mout_bus_pll_user_t_p, MUX_SEL_TOP_CAM1, 16, 1),
	MUX(CLK_MOUT_SCLK_ISP_UART, "mout_sclk_isp_uart",
			mout_bus_pll_user_t_p, MUX_SEL_TOP_CAM1, 8, 1),
	MUX(CLK_MOUT_SCLK_ISP_SPI1, "mout_sclk_isp_spi1",
			mout_bus_pll_user_t_p, MUX_SEL_TOP_CAM1, 4, 1),
	MUX(CLK_MOUT_SCLK_ISP_SPI0, "mout_sclk_isp_spi0",
			mout_bus_pll_user_t_p, MUX_SEL_TOP_CAM1, 0, 1),

	/* MUX_SEL_TOP_FSYS0 */
	MUX(CLK_MOUT_SCLK_MMC2_B, "mout_sclk_mmc2_b", mout_sclk_mmc2_b_p,
			MUX_SEL_TOP_FSYS0, 28, 1),
	MUX(CLK_MOUT_SCLK_MMC2_A, "mout_sclk_mmc2_a", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_FSYS0, 24, 1),
	MUX(CLK_MOUT_SCLK_MMC1_B, "mout_sclk_mmc1_b", mout_sclk_mmc1_b_p,
			MUX_SEL_TOP_FSYS0, 20, 1),
	MUX(CLK_MOUT_SCLK_MMC1_A, "mout_sclk_mmc1_a", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_FSYS0, 16, 1),
	MUX(CLK_MOUT_SCLK_MMC0_D, "mout_sclk_mmc0_d", mout_sclk_mmc0_d_p,
			MUX_SEL_TOP_FSYS0, 12, 1),
	MUX(CLK_MOUT_SCLK_MMC0_C, "mout_sclk_mmc0_c", mout_sclk_mmc0_c_p,
			MUX_SEL_TOP_FSYS0, 8, 1),
	MUX(CLK_MOUT_SCLK_MMC0_B, "mout_sclk_mmc0_b", mout_sclk_mmc0_b_p,
			MUX_SEL_TOP_FSYS0, 4, 1),
	MUX(CLK_MOUT_SCLK_MMC0_A, "mout_sclk_mmc0_a", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_FSYS0, 0, 1),

	/* MUX_SEL_TOP_FSYS1 */
	MUX(CLK_MOUT_SCLK_PCIE_100, "mout_sclk_pcie_100", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_FSYS1, 12, 1),
	MUX(CLK_MOUT_SCLK_UFSUNIPRO, "mout_sclk_ufsunipro",
			mout_mphy_pll_user_t_p, MUX_SEL_TOP_FSYS1, 8, 1),
	MUX(CLK_MOUT_SCLK_USBHOST30, "mout_sclk_usbhost30",
			mout_bus_pll_user_t_p, MUX_SEL_TOP_FSYS1, 4, 1),
	MUX(CLK_MOUT_SCLK_USBDRD30, "mout_sclk_usbdrd30",
			mout_bus_pll_user_t_p, MUX_SEL_TOP_FSYS1, 0, 1),

	/* MUX_SEL_TOP_PERIC0 */
	MUX(CLK_MOUT_SCLK_SPI4, "mout_sclk_spi4", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_PERIC0, 28, 1),
	MUX(CLK_MOUT_SCLK_SPI3, "mout_sclk_spi3", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_PERIC0, 24, 1),
	MUX(CLK_MOUT_SCLK_UART2, "mout_sclk_uart2", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_PERIC0, 20, 1),
	MUX(CLK_MOUT_SCLK_UART1, "mout_sclk_uart1", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_PERIC0, 16, 1),
	MUX(CLK_MOUT_SCLK_UART0, "mout_sclk_uart0", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_PERIC0, 12, 1),
	MUX(CLK_MOUT_SCLK_SPI2, "mout_sclk_spi2", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_PERIC0, 8, 1),
	MUX(CLK_MOUT_SCLK_SPI1, "mout_sclk_spi1", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_PERIC0, 4, 1),
	MUX(CLK_MOUT_SCLK_SPI0, "mout_sclk_spi0", mout_bus_pll_user_t_p,
			MUX_SEL_TOP_PERIC0, 0, 1),

	/* MUX_SEL_TOP_PERIC1 */
	MUX(CLK_MOUT_SCLK_SLIMBUS, "mout_sclk_slimbus", mout_aud_pll_user_p,
			MUX_SEL_TOP_PERIC1, 16, 1),
	MUX(CLK_MOUT_SCLK_SPDIF, "mout_sclk_spdif", mout_sclk_spdif_p,
			MUX_SEL_TOP_PERIC1, 12, 2),
	MUX(CLK_MOUT_SCLK_AUDIO1, "mout_sclk_audio1", mout_sclk_audio1_p,
			MUX_SEL_TOP_PERIC1, 4, 2),
	MUX(CLK_MOUT_SCLK_AUDIO0, "mout_sclk_audio0", mout_sclk_audio0_p,
			MUX_SEL_TOP_PERIC1, 0, 2),
};

static struct samsung_div_clock top_div_clks[] __initdata = {
	/* DIV_TOP1 */
	DIV(CLK_DIV_ACLK_GSCL_111, "div_aclk_gscl_111", "mout_aclk_gscl_333",
			DIV_TOP1, 28, 3),
	DIV(CLK_DIV_ACLK_GSCL_333, "div_aclk_gscl_333", "mout_aclk_gscl_333",
			DIV_TOP1, 24, 3),
	DIV(CLK_DIV_ACLK_HEVC_400, "div_aclk_hevc_400", "mout_aclk_hevc_400",
			DIV_TOP1, 20, 3),
	DIV(CLK_DIV_ACLK_MFC_400, "div_aclk_mfc_400", "mout_aclk_mfc_400_c",
			DIV_TOP1, 12, 3),
	DIV(CLK_DIV_ACLK_G2D_266, "div_aclk_g2d_266", "mout_bus_pll_user",
			DIV_TOP1, 8, 3),
	DIV(CLK_DIV_ACLK_G2D_400, "div_aclk_g2d_400", "mout_aclk_g2d_400_b",
			DIV_TOP1, 0, 3),

	/* DIV_TOP2 */
	DIV(CLK_DIV_ACLK_FSYS_200, "div_aclk_fsys_200", "mout_bus_pll_user",
			DIV_TOP2, 0, 3),

	/* DIV_TOP3 */
	DIV(CLK_DIV_ACLK_IMEM_SSSX_266, "div_aclk_imem_sssx_266",
			"mout_bus_pll_user", DIV_TOP3, 24, 3),
	DIV(CLK_DIV_ACLK_IMEM_200, "div_aclk_imem_200",
			"mout_bus_pll_user", DIV_TOP3, 20, 3),
	DIV(CLK_DIV_ACLK_IMEM_266, "div_aclk_imem_266",
			"mout_bus_pll_user", DIV_TOP3, 16, 3),
	DIV(CLK_DIV_ACLK_PERIC_66_B, "div_aclk_peric_66_b",
			"div_aclk_peric_66_a", DIV_TOP3, 12, 3),
	DIV(CLK_DIV_ACLK_PERIC_66_A, "div_aclk_peric_66_a",
			"mout_bus_pll_user", DIV_TOP3, 8, 3),
	DIV(CLK_DIV_ACLK_PERIS_66_B, "div_aclk_peris_66_b",
			"div_aclk_peris_66_a", DIV_TOP3, 4, 3),
	DIV(CLK_DIV_ACLK_PERIS_66_A, "div_aclk_peris_66_a",
			"mout_bus_pll_user", DIV_TOP3, 0, 3),

	/* DIV_TOP_FSYS0 */
	DIV(CLK_DIV_SCLK_MMC1_B, "div_sclk_mmc1_b", "div_sclk_mmc1_a",
			DIV_TOP_FSYS0, 16, 8),
	DIV(CLK_DIV_SCLK_MMC1_A, "div_sclk_mmc1_a", "mout_sclk_mmc1_b",
			DIV_TOP_FSYS0, 12, 4),
	DIV_F(CLK_DIV_SCLK_MMC0_B, "div_sclk_mmc0_b", "div_sclk_mmc0_a",
			DIV_TOP_FSYS0, 4, 8, CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DIV_SCLK_MMC0_A, "div_sclk_mmc0_a", "mout_sclk_mmc0_d",
			DIV_TOP_FSYS0, 0, 4, CLK_SET_RATE_PARENT, 0),

	/* DIV_TOP_FSYS1 */
	DIV(CLK_DIV_SCLK_MMC2_B, "div_sclk_mmc2_b", "div_sclk_mmc2_a",
			DIV_TOP_FSYS1, 4, 8),
	DIV(CLK_DIV_SCLK_MMC2_A, "div_sclk_mmc2_a", "mout_sclk_mmc2_b",
			DIV_TOP_FSYS1, 0, 4),

	/* DIV_TOP_PERIC0 */
	DIV(CLK_DIV_SCLK_SPI1_B, "div_sclk_spi1_b", "div_sclk_spi1_a",
			DIV_TOP_PERIC0, 16, 8),
	DIV(CLK_DIV_SCLK_SPI1_A, "div_sclk_spi1_a", "mout_sclk_spi1",
			DIV_TOP_PERIC0, 12, 4),
	DIV(CLK_DIV_SCLK_SPI0_B, "div_sclk_spi0_b", "div_sclk_spi0_a",
			DIV_TOP_PERIC0, 4, 8),
	DIV(CLK_DIV_SCLK_SPI0_A, "div_sclk_spi0_a", "mout_sclk_spi0",
			DIV_TOP_PERIC0, 0, 4),

	/* DIV_TOP_PERIC1 */
	DIV(CLK_DIV_SCLK_SPI2_B, "div_sclk_spi2_b", "div_sclk_spi2_a",
			DIV_TOP_PERIC1, 4, 8),
	DIV(CLK_DIV_SCLK_SPI2_A, "div_sclk_spi2_a", "mout_sclk_spi2",
			DIV_TOP_PERIC1, 0, 4),

	/* DIV_TOP_PERIC2 */
	DIV(CLK_DIV_SCLK_UART2, "div_sclk_uart2", "mout_sclk_uart2",
			DIV_TOP_PERIC2, 8, 4),
	DIV(CLK_DIV_SCLK_UART1, "div_sclk_uart1", "mout_sclk_uart0",
			DIV_TOP_PERIC2, 4, 4),
	DIV(CLK_DIV_SCLK_UART0, "div_sclk_uart0", "mout_sclk_uart1",
			DIV_TOP_PERIC2, 0, 4),

	/* DIV_TOP_PERIC3 */
	DIV(CLK_DIV_SCLK_I2S1, "div_sclk_i2s1", "sclk_audio1",
			DIV_TOP_PERIC3, 16, 6),
	DIV(CLK_DIV_SCLK_PCM1, "div_sclk_pcm1", "sclk_audio1",
			DIV_TOP_PERIC3, 8, 8),
	DIV(CLK_DIV_SCLK_AUDIO1, "div_sclk_audio1", "mout_sclk_audio1",
			DIV_TOP_PERIC3, 4, 4),
	DIV(CLK_DIV_SCLK_AUDIO0, "div_sclk_audio0", "mout_sclk_audio0",
			DIV_TOP_PERIC3, 0, 4),

	/* DIV_TOP_PERIC4 */
	DIV(CLK_DIV_SCLK_SPI4_B, "div_sclk_spi4_b", "div_sclk_spi4_a",
			DIV_TOP_PERIC4, 16, 8),
	DIV(CLK_DIV_SCLK_SPI4_A, "div_sclk_spi4_a", "mout_sclk_spi4",
			DIV_TOP_PERIC4, 12, 4),
	DIV(CLK_DIV_SCLK_SPI3_B, "div_sclk_spi3_b", "div_sclk_spi3_a",
			DIV_TOP_PERIC4, 4, 8),
	DIV(CLK_DIV_SCLK_SPI3_A, "div_sclk_spi3_a", "mout_sclk_spi3",
			DIV_TOP_PERIC4, 0, 4),
};

static struct samsung_gate_clock top_gate_clks[] __initdata = {
	/* ENABLE_ACLK_TOP */
	GATE(CLK_ACLK_PERIC_66, "aclk_peric_66", "div_aclk_peric_66_b",
			ENABLE_ACLK_TOP, 22,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PERIS_66, "aclk_peris_66", "div_aclk_peris_66_b",
			ENABLE_ACLK_TOP, 21,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_FSYS_200, "aclk_fsys_200", "div_aclk_fsys_200",
			ENABLE_ACLK_TOP, 18,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_G2D_266, "aclk_g2d_266", "div_aclk_g2d_266",
			ENABLE_ACLK_TOP, 2,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_G2D_400, "aclk_g2d_400", "div_aclk_g2d_400",
			ENABLE_ACLK_TOP, 0,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0),

	/* ENABLE_SCLK_TOP_FSYS */
	GATE(CLK_SCLK_MMC2_FSYS, "sclk_mmc2_fsys", "div_sclk_mmc2_b",
			ENABLE_SCLK_TOP_FSYS, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC1_FSYS, "sclk_mmc1_fsys", "div_sclk_mmc1_b",
			ENABLE_SCLK_TOP_FSYS, 5, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC0_FSYS, "sclk_mmc0_fsys", "div_sclk_mmc0_b",
			ENABLE_SCLK_TOP_FSYS, 4, CLK_SET_RATE_PARENT, 0),

	/* ENABLE_SCLK_TOP_PERIC */
	GATE(CLK_SCLK_SPI4_PERIC, "sclk_spi4_peric", "div_sclk_spi4_b",
			ENABLE_SCLK_TOP_PERIC, 12, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI3_PERIC, "sclk_spi3_peric", "div_sclk_spi3_b",
			ENABLE_SCLK_TOP_PERIC, 11, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPDIF_PERIC, "sclk_spdif_peric", "mout_sclk_spdif",
			ENABLE_SCLK_TOP_PERIC, 9, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_I2S1_PERIC, "sclk_i2s1_peric", "div_sclk_i2s1",
			ENABLE_SCLK_TOP_PERIC, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PCM1_PERIC, "sclk_pcm1_peric", "div_sclk_pcm1",
			ENABLE_SCLK_TOP_PERIC, 7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART2_PERIC, "sclk_uart2_peric", "div_sclk_uart2",
			ENABLE_SCLK_TOP_PERIC, 5, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART1_PERIC, "sclk_uart1_peric", "div_sclk_uart1",
			ENABLE_SCLK_TOP_PERIC, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART0_PERIC, "sclk_uart0_peric", "div_sclk_uart0",
			ENABLE_SCLK_TOP_PERIC, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI2_PERIC, "sclk_spi2_peric", "div_sclk_spi2_b",
			ENABLE_SCLK_TOP_PERIC, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI1_PERIC, "sclk_spi1_peric", "div_sclk_spi1_b",
			ENABLE_SCLK_TOP_PERIC, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0_PERIC, "sclk_spi0_peric", "div_sclk_spi0_b",
			ENABLE_SCLK_TOP_PERIC, 0, CLK_SET_RATE_PARENT, 0),

	/* MUX_ENABLE_TOP_PERIC1 */
	GATE(CLK_SCLK_SLIMBUS, "sclk_slimbus", "mout_sclk_slimbus",
			MUX_ENABLE_TOP_PERIC1, 16, 0, 0),
	GATE(CLK_SCLK_AUDIO1, "sclk_audio1", "div_sclk_audio1",
			MUX_ENABLE_TOP_PERIC1, 4, 0, 0),
	GATE(CLK_SCLK_AUDIO0, "sclk_audio0", "div_sclk_audio0",
			MUX_ENABLE_TOP_PERIC1, 0, 0, 0),
};

/*
 * ATLAS_PLL & APOLLO_PLL & MEM0_PLL & MEM1_PLL & BUS_PLL & MFC_PLL
 * & MPHY_PLL & G3D_PLL & DISP_PLL & ISP_PLL
 */
static struct samsung_pll_rate_table exynos5443_pll_rates[] = {
	PLL_35XX_RATE(2500000000U, 625, 6,  0),
	PLL_35XX_RATE(2400000000U, 500, 5,  0),
	PLL_35XX_RATE(2300000000U, 575, 6,  0),
	PLL_35XX_RATE(2200000000U, 550, 6,  0),
	PLL_35XX_RATE(2100000000U, 350, 4,  0),
	PLL_35XX_RATE(2000000000U, 500, 6,  0),
	PLL_35XX_RATE(1900000000U, 475, 6,  0),
	PLL_35XX_RATE(1800000000U, 375, 5,  0),
	PLL_35XX_RATE(1700000000U, 425, 6,  0),
	PLL_35XX_RATE(1600000000U, 400, 6,  0),
	PLL_35XX_RATE(1500000000U, 250, 4,  0),
	PLL_35XX_RATE(1400000000U, 350, 6,  0),
	PLL_35XX_RATE(1332000000U, 222, 4,  0),
	PLL_35XX_RATE(1300000000U, 325, 6,  0),
	PLL_35XX_RATE(1200000000U, 500, 5,  1),
	PLL_35XX_RATE(1100000000U, 550, 6,  1),
	PLL_35XX_RATE(1086000000U, 362, 4,  1),
	PLL_35XX_RATE(1066000000U, 533, 6,  1),
	PLL_35XX_RATE(1000000000U, 500, 6,  1),
	PLL_35XX_RATE(933000000U,  311, 4,  1),
	PLL_35XX_RATE(921000000U,  307, 4,  1),
	PLL_35XX_RATE(900000000U,  375, 5,  1),
	PLL_35XX_RATE(825000000U,  275, 4,  1),
	PLL_35XX_RATE(800000000U,  400, 6,  1),
	PLL_35XX_RATE(733000000U,  733, 12, 1),
	PLL_35XX_RATE(700000000U,  360, 6,  1),
	PLL_35XX_RATE(667000000U,  222, 4,  1),
	PLL_35XX_RATE(633000000U,  211, 4,  1),
	PLL_35XX_RATE(600000000U,  500, 5,  2),
	PLL_35XX_RATE(552000000U,  460, 5,  2),
	PLL_35XX_RATE(550000000U,  550, 6,  2),
	PLL_35XX_RATE(543000000U,  362, 4,  2),
	PLL_35XX_RATE(533000000U,  533, 6,  2),
	PLL_35XX_RATE(500000000U,  500, 6,  2),
	PLL_35XX_RATE(444000000U,  370, 5,  2),
	PLL_35XX_RATE(420000000U,  350, 5,  2),
	PLL_35XX_RATE(400000000U,  400, 6,  2),
	PLL_35XX_RATE(350000000U,  360, 6,  2),
	PLL_35XX_RATE(333000000U,  222, 4,  2),
	PLL_35XX_RATE(300000000U,  500, 5,  3),
	PLL_35XX_RATE(266000000U,  532, 6,  3),
	PLL_35XX_RATE(200000000U,  400, 6,  3),
	PLL_35XX_RATE(166000000U,  332, 6,  3),
	PLL_35XX_RATE(160000000U,  320, 6,  3),
	PLL_35XX_RATE(133000000U,  552, 6,  4),
	PLL_35XX_RATE(100000000U,  400, 6,  4),
	{ /* sentinel */ }
};

/* AUD_PLL */
static struct samsung_pll_rate_table exynos5443_aud_pll_rates[] = {
	PLL_36XX_RATE(400000000U, 200, 3, 2,      0),
	PLL_36XX_RATE(393216000U, 197, 3, 2, -25690),
	PLL_36XX_RATE(384000000U, 128, 2, 2,      0),
	PLL_36XX_RATE(368640000U, 246, 4, 2, -15729),
	PLL_36XX_RATE(361507200U, 181, 3, 2, -16148),
	PLL_36XX_RATE(338688000U, 113, 2, 2,  -6816),
	PLL_36XX_RATE(294912000U,  98, 1, 3,  19923),
	PLL_36XX_RATE(288000000U,  96, 1, 3,      0),
	PLL_36XX_RATE(252000000U,  84, 1, 3,      0),
	{ /* sentinel */ }
};

static struct samsung_pll_clock top_pll_clks[] __initdata = {
	PLL(pll_35xx, CLK_FOUT_ISP_PLL, "fout_isp_pll", "oscclk",
		ISP_PLL_LOCK, ISP_PLL_CON0, exynos5443_pll_rates),
	PLL(pll_36xx, CLK_FOUT_AUD_PLL, "fout_aud_pll", "oscclk",
		AUD_PLL_LOCK, AUD_PLL_CON0, exynos5443_aud_pll_rates),
};

static struct samsung_cmu_info top_cmu_info __initdata = {
	.pll_clks		= top_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(top_pll_clks),
	.mux_clks		= top_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(top_mux_clks),
	.div_clks		= top_div_clks,
	.nr_div_clks		= ARRAY_SIZE(top_div_clks),
	.gate_clks		= top_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(top_gate_clks),
	.fixed_clks		= top_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(top_fixed_clks),
	.fixed_factor_clks	= top_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(top_fixed_factor_clks),
	.nr_clk_ids		= TOP_NR_CLK,
	.clk_regs		= top_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(top_clk_regs),
};

static void __init exynos5433_cmu_top_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &top_cmu_info);
}
CLK_OF_DECLARE(exynos5433_cmu_top, "samsung,exynos5433-cmu-top",
		exynos5433_cmu_top_init);

/*
 * Register offset definitions for CMU_CPIF
 */
#define MPHY_PLL_LOCK		0x0000
#define MPHY_PLL_CON0		0x0100
#define MPHY_PLL_CON1		0x0104
#define MPHY_PLL_FREQ_DET	0x010c
#define MUX_SEL_CPIF0		0x0200
#define DIV_CPIF		0x0600
#define ENABLE_SCLK_CPIF	0x0a00

static unsigned long cpif_clk_regs[] __initdata = {
	MPHY_PLL_LOCK,
	MPHY_PLL_CON0,
	MPHY_PLL_CON1,
	MPHY_PLL_FREQ_DET,
	MUX_SEL_CPIF0,
	ENABLE_SCLK_CPIF,
};

/* list of all parent clock list */
PNAME(mout_mphy_pll_p)		= { "oscclk", "fout_mphy_pll", };

static struct samsung_pll_clock cpif_pll_clks[] __initdata = {
	PLL(pll_35xx, CLK_FOUT_MPHY_PLL, "fout_mphy_pll", "oscclk",
		MPHY_PLL_LOCK, MPHY_PLL_CON0, exynos5443_pll_rates),
};

static struct samsung_mux_clock cpif_mux_clks[] __initdata = {
	/* MUX_SEL_CPIF0 */
	MUX(CLK_MOUT_MPHY_PLL, "mout_mphy_pll", mout_mphy_pll_p, MUX_SEL_CPIF0,
			0, 1),
};

static struct samsung_div_clock cpif_div_clks[] __initdata = {
	/* DIV_CPIF */
	DIV(CLK_DIV_SCLK_MPHY, "div_sclk_mphy", "mout_mphy_pll", DIV_CPIF,
			0, 6),
};

static struct samsung_gate_clock cpif_gate_clks[] __initdata = {
	/* ENABLE_SCLK_CPIF */
	GATE(CLK_SCLK_MPHY_PLL, "sclk_mphy_pll", "mout_mphy_pll",
			ENABLE_SCLK_CPIF, 9, 0, 0),
	GATE(CLK_SCLK_UFS_MPHY, "sclk_ufs_mphy", "div_sclk_mphy",
			ENABLE_SCLK_CPIF, 4, 0, 0),
};

static struct samsung_cmu_info cpif_cmu_info __initdata = {
	.pll_clks		= cpif_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cpif_pll_clks),
	.mux_clks		= cpif_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cpif_mux_clks),
	.div_clks		= cpif_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cpif_div_clks),
	.gate_clks		= cpif_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cpif_gate_clks),
	.nr_clk_ids		= CPIF_NR_CLK,
	.clk_regs		= cpif_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cpif_clk_regs),
};

static void __init exynos5433_cmu_cpif_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &cpif_cmu_info);
}
CLK_OF_DECLARE(exynos5433_cmu_cpif, "samsung,exynos5433-cmu-cpif",
		exynos5433_cmu_cpif_init);

/*
 * Register offset definitions for CMU_MIF
 */
#define MEM0_PLL_LOCK			0x0000
#define MEM1_PLL_LOCK			0x0004
#define BUS_PLL_LOCK			0x0008
#define MFC_PLL_LOCK			0x000c
#define MEM0_PLL_CON0			0x0100
#define MEM0_PLL_CON1			0x0104
#define MEM0_PLL_FREQ_DET		0x010c
#define MEM1_PLL_CON0			0x0110
#define MEM1_PLL_CON1			0x0114
#define MEM1_PLL_FREQ_DET		0x011c
#define BUS_PLL_CON0			0x0120
#define BUS_PLL_CON1			0x0124
#define BUS_PLL_FREQ_DET		0x012c
#define MFC_PLL_CON0			0x0130
#define MFC_PLL_CON1			0x0134
#define MFC_PLL_FREQ_DET		0x013c

static unsigned long mif_clk_regs[] __initdata = {
	MEM0_PLL_LOCK,
	MEM1_PLL_LOCK,
	BUS_PLL_LOCK,
	MFC_PLL_LOCK,
	MEM0_PLL_CON0,
	MEM0_PLL_CON1,
	MEM0_PLL_FREQ_DET,
	MEM1_PLL_CON0,
	MEM1_PLL_CON1,
	MEM1_PLL_FREQ_DET,
	BUS_PLL_CON0,
	BUS_PLL_CON1,
	BUS_PLL_FREQ_DET,
	MFC_PLL_CON0,
	MFC_PLL_CON1,
	MFC_PLL_FREQ_DET,
};

static struct samsung_pll_clock mif_pll_clks[] __initdata = {
	PLL(pll_35xx, CLK_FOUT_MEM0_PLL, "fout_mem0_pll", "oscclk",
		MEM0_PLL_LOCK, MEM0_PLL_CON0, exynos5443_pll_rates),
	PLL(pll_35xx, CLK_FOUT_MEM1_PLL, "fout_mem1_pll", "oscclk",
		MEM1_PLL_LOCK, MEM1_PLL_CON0, exynos5443_pll_rates),
	PLL(pll_35xx, CLK_FOUT_BUS_PLL, "fout_bus_pll", "oscclk",
		BUS_PLL_LOCK, BUS_PLL_CON0, exynos5443_pll_rates),
	PLL(pll_35xx, CLK_FOUT_MFC_PLL, "fout_mfc_pll", "oscclk",
		MFC_PLL_LOCK, MFC_PLL_CON0, exynos5443_pll_rates),
};

static struct samsung_cmu_info mif_cmu_info __initdata = {
	.pll_clks		= mif_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(mif_pll_clks),
	.nr_clk_ids		= MIF_NR_CLK,
	.clk_regs		= mif_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mif_clk_regs),
};

static void __init exynos5433_cmu_mif_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &mif_cmu_info);
}
CLK_OF_DECLARE(exynos5433_cmu_mif, "samsung,exynos5433-cmu-mif",
		exynos5433_cmu_mif_init);

/*
 * Register offset definitions for CMU_PERIC
 */
#define DIV_PERIC			0x0600
#define DIV_STAT_PERIC			0x0700
#define ENABLE_ACLK_PERIC		0x0800
#define ENABLE_PCLK_PERIC0		0x0900
#define ENABLE_PCLK_PERIC1		0x0904
#define ENABLE_SCLK_PERIC		0x0A00
#define ENABLE_IP_PERIC0		0x0B00
#define ENABLE_IP_PERIC1		0x0B04
#define ENABLE_IP_PERIC2		0x0B08

static unsigned long peric_clk_regs[] __initdata = {
	DIV_PERIC,
	DIV_STAT_PERIC,
	ENABLE_ACLK_PERIC,
	ENABLE_PCLK_PERIC0,
	ENABLE_PCLK_PERIC1,
	ENABLE_SCLK_PERIC,
	ENABLE_IP_PERIC0,
	ENABLE_IP_PERIC1,
	ENABLE_IP_PERIC2,
};

static struct samsung_div_clock peric_div_clks[] __initdata = {
	/* DIV_PERIC */
	DIV(CLK_DIV_SCLK_SCI, "div_sclk_sci", "oscclk", DIV_PERIC, 4, 4),
	DIV(CLK_DIV_SCLK_SC_IN, "div_sclk_sc_in", "oscclk", DIV_PERIC, 0, 4),
};

static struct samsung_gate_clock peric_gate_clks[] __initdata = {
	/* ENABLE_ACLK_PERIC */
	GATE(CLK_ACLK_AHB2APB_PERIC2P, "aclk_ahb2apb_peric2p", "aclk_peric_66",
			ENABLE_ACLK_PERIC, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AHB2APB_PERIC1P, "aclk_ahb2apb_peric1p", "aclk_peric_66",
			ENABLE_ACLK_PERIC, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AHB2APB_PERIC0P, "aclk_ahb2apb_peric0p", "aclk_peric_66",
			ENABLE_ACLK_PERIC, 1, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PERICNP_66, "aclk_pericnp_66", "aclk_peric_66",
			ENABLE_ACLK_PERIC, 0, CLK_IGNORE_UNUSED, 0),

	/* ENABLE_PCLK_PERIC0 */
	GATE(CLK_PCLK_SCI, "pclk_sci", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			31, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_GPIO_FINGER, "pclk_gpio_finger", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 30, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_GPIO_ESE, "pclk_gpio_ese", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 29, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PWM, "pclk_pwm", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			28, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_SPDIF, "pclk_spdif", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			26, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_PCM1, "pclk_pcm1", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			25, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_I2S1, "pclk_i2s", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			24, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_SPI2, "pclk_spi2", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			23, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_SPI1, "pclk_spi1", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			22, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_SPI0, "pclk_spi0", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_ADCIF, "pclk_adcif", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			20, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_GPIO_TOUCH, "pclk_gpio_touch", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 19, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_GPIO_NFC, "pclk_gpio_nfc", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 18, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_GPIO_PERIC, "pclk_gpio_peric", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 17, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PMU_PERIC, "pclk_pmu_peric", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 16, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_SYSREG_PERIC, "pclk_sysreg_peric", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 15,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_UART2, "pclk_uart2", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			14, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_UART1, "pclk_uart1", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			13, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_UART0, "pclk_uart0", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			12, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C3, "pclk_hsi2c3", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 11, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C2, "pclk_hsi2c2", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 10, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C1, "pclk_hsi2c1", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 9, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C0, "pclk_hsi2c0", "aclk_peric_66",
			ENABLE_PCLK_PERIC0, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_I2C7, "pclk_i2c7", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_I2C6, "pclk_i2c6", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_I2C5, "pclk_i2c5", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			5, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_I2C4, "pclk_i2c4", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_I2C3, "pclk_i2c3", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_I2C2, "pclk_i2c2", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_I2C1, "pclk_i2c1", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_I2C0, "pclk_i2c0", "aclk_peric_66", ENABLE_PCLK_PERIC0,
			0, CLK_SET_RATE_PARENT, 0),

	/* ENABLE_PCLK_PERIC1 */
	GATE(CLK_PCLK_SPI4, "pclk_spi4", "aclk_peric_66", ENABLE_PCLK_PERIC1,
			9, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_SPI3, "pclk_spi3", "aclk_peric_66", ENABLE_PCLK_PERIC1,
			8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C11, "pclk_hsi2c11", "aclk_peric_66",
			ENABLE_PCLK_PERIC1, 7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C10, "pclk_hsi2c10", "aclk_peric_66",
			ENABLE_PCLK_PERIC1, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C9, "pclk_hsi2c9", "aclk_peric_66",
			ENABLE_PCLK_PERIC1, 5, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C8, "pclk_hsi2c8", "aclk_peric_66",
			ENABLE_PCLK_PERIC1, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C7, "pclk_hsi2c7", "aclk_peric_66",
			ENABLE_PCLK_PERIC1, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C6, "pclk_hsi2c6", "aclk_peric_66",
			ENABLE_PCLK_PERIC1, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C5, "pclk_hsi2c5", "aclk_peric_66",
			ENABLE_PCLK_PERIC1, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PCLK_HSI2C4, "pclk_hsi2c4", "aclk_peric_66",
			ENABLE_PCLK_PERIC1, 0, CLK_SET_RATE_PARENT, 0),

	/* ENABLE_SCLK_PERIC */
	GATE(CLK_SCLK_IOCLK_SPI4, "sclk_ioclk_spi4", "ioclk_spi4_clk_in",
			ENABLE_SCLK_PERIC, 21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_IOCLK_SPI3, "sclk_ioclk_spi3", "ioclk_spi3_clk_in",
			ENABLE_SCLK_PERIC, 20, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI4, "sclk_spi4", "sclk_spi4_peric", ENABLE_SCLK_PERIC,
			19, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI3, "sclk_spi3", "sclk_spi3_peric", ENABLE_SCLK_PERIC,
			18, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SCI, "sclk_sci", "div_sclk_sci", ENABLE_SCLK_PERIC,
			17, 0, 0),
	GATE(CLK_SCLK_SC_IN, "sclk_sc_in", "div_sclk_sc_in", ENABLE_SCLK_PERIC,
			16, 0, 0),
	GATE(CLK_SCLK_PWM, "sclk_pwm", "oscclk", ENABLE_SCLK_PERIC, 15, 0, 0),
	GATE(CLK_SCLK_IOCLK_SPI2, "sclk_ioclk_spi2", "ioclk_spi2_clk_in",
			ENABLE_SCLK_PERIC, 13, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_IOCLK_SPI1, "sclk_ioclk_spi1", "ioclk_spi1_clk_in",
			ENABLE_SCLK_PERIC, 12,
			CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_IOCLK_SPI0, "sclk_ioclk_spi0", "ioclk_spi0_clk_in",
			ENABLE_SCLK_PERIC, 11, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_IOCLK_I2S1_BCLK, "sclk_ioclk_i2s1_bclk",
			"ioclk_i2s1_bclk_in", ENABLE_SCLK_PERIC, 10,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPDIF, "sclk_spdif", "sclk_spdif_peric",
			ENABLE_SCLK_PERIC, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PCM1, "sclk_pcm1", "sclk_pcm1_peric",
			ENABLE_SCLK_PERIC, 7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_I2S1, "sclk_i2s1", "sclk_i2s1_peric",
			ENABLE_SCLK_PERIC, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI2, "sclk_spi2", "sclk_spi2_peric", ENABLE_SCLK_PERIC,
			5, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI1, "sclk_spi1", "sclk_spi1_peric", ENABLE_SCLK_PERIC,
			4, CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0, "sclk_spi0", "sclk_spi0_peric", ENABLE_SCLK_PERIC,
			3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART2, "sclk_uart2", "sclk_uart2_peric",
			ENABLE_SCLK_PERIC, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART1, "sclk_uart1", "sclk_uart1_peric",
			ENABLE_SCLK_PERIC, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART0, "sclk_uart0", "sclk_uart0_peric",
			ENABLE_SCLK_PERIC, 0, CLK_SET_RATE_PARENT, 0),
};

static struct samsung_cmu_info peric_cmu_info __initdata = {
	.div_clks		= peric_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peric_div_clks),
	.gate_clks		= peric_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peric_gate_clks),
	.nr_clk_ids		= PERIC_NR_CLK,
	.clk_regs		= peric_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric_clk_regs),
};

static void __init exynos5433_cmu_peric_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &peric_cmu_info);
}

CLK_OF_DECLARE(exynos5433_cmu_peric, "samsung,exynos5433-cmu-peric",
		exynos5433_cmu_peric_init);

/*
 * Register offset definitions for CMU_PERIS
 */
#define ENABLE_ACLK_PERIS				0x0800
#define ENABLE_PCLK_PERIS				0x0900
#define ENABLE_PCLK_PERIS_SECURE_TZPC			0x0904
#define ENABLE_PCLK_PERIS_SECURE_SECKEY_APBIF		0x0908
#define ENABLE_PCLK_PERIS_SECURE_CHIPID_APBIF		0x090c
#define ENABLE_PCLK_PERIS_SECURE_TOPRTC			0x0910
#define ENABLE_PCLK_PERIS_SECURE_CUSTOM_EFUSE_APBIF	0x0914
#define ENABLE_PCLK_PERIS_SECURE_ANTIRBK_CNT_APBIF	0x0918
#define ENABLE_PCLK_PERIS_SECURE_OTP_CON_APBIF		0x091c
#define ENABLE_SCLK_PERIS				0x0a00
#define ENABLE_SCLK_PERIS_SECURE_SECKEY			0x0a04
#define ENABLE_SCLK_PERIS_SECURE_CHIPID			0x0a08
#define ENABLE_SCLK_PERIS_SECURE_TOPRTC			0x0a0c
#define ENABLE_SCLK_PERIS_SECURE_CUSTOM_EFUSE		0x0a10
#define ENABLE_SCLK_PERIS_SECURE_ANTIRBK_CNT		0x0a14
#define ENABLE_SCLK_PERIS_SECURE_OTP_CON		0x0a18
#define ENABLE_IP_PERIS0				0x0b00
#define ENABLE_IP_PERIS1				0x0b04
#define ENABLE_IP_PERIS_SECURE_TZPC			0x0b08
#define ENABLE_IP_PERIS_SECURE_SECKEY			0x0b0c
#define ENABLE_IP_PERIS_SECURE_CHIPID			0x0b10
#define ENABLE_IP_PERIS_SECURE_TOPRTC			0x0b14
#define ENABLE_IP_PERIS_SECURE_CUSTOM_EFUSE		0x0b18
#define ENABLE_IP_PERIS_SECURE_ANTIBRK_CNT		0x0b1c
#define ENABLE_IP_PERIS_SECURE_OTP_CON			0x0b20

static unsigned long peris_clk_regs[] __initdata = {
	ENABLE_ACLK_PERIS,
	ENABLE_PCLK_PERIS,
	ENABLE_PCLK_PERIS_SECURE_TZPC,
	ENABLE_PCLK_PERIS_SECURE_SECKEY_APBIF,
	ENABLE_PCLK_PERIS_SECURE_CHIPID_APBIF,
	ENABLE_PCLK_PERIS_SECURE_TOPRTC,
	ENABLE_PCLK_PERIS_SECURE_CUSTOM_EFUSE_APBIF,
	ENABLE_PCLK_PERIS_SECURE_ANTIRBK_CNT_APBIF,
	ENABLE_PCLK_PERIS_SECURE_OTP_CON_APBIF,
	ENABLE_SCLK_PERIS,
	ENABLE_SCLK_PERIS_SECURE_SECKEY,
	ENABLE_SCLK_PERIS_SECURE_CHIPID,
	ENABLE_SCLK_PERIS_SECURE_TOPRTC,
	ENABLE_SCLK_PERIS_SECURE_CUSTOM_EFUSE,
	ENABLE_SCLK_PERIS_SECURE_ANTIRBK_CNT,
	ENABLE_SCLK_PERIS_SECURE_OTP_CON,
	ENABLE_IP_PERIS0,
	ENABLE_IP_PERIS1,
	ENABLE_IP_PERIS_SECURE_TZPC,
	ENABLE_IP_PERIS_SECURE_SECKEY,
	ENABLE_IP_PERIS_SECURE_CHIPID,
	ENABLE_IP_PERIS_SECURE_TOPRTC,
	ENABLE_IP_PERIS_SECURE_CUSTOM_EFUSE,
	ENABLE_IP_PERIS_SECURE_ANTIBRK_CNT,
	ENABLE_IP_PERIS_SECURE_OTP_CON,
};

static struct samsung_gate_clock peris_gate_clks[] __initdata = {
	/* ENABLE_ACLK_PERIS */
	GATE(CLK_ACLK_AHB2APB_PERIS1P, "aclk_ahb2apb_peris1p", "aclk_peris_66",
			ENABLE_ACLK_PERIS, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AHB2APB_PERIS0P, "aclk_ahb2apb_peris0p", "aclk_peris_66",
			ENABLE_ACLK_PERIS, 1, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PERISNP_66, "aclk_perisnp_66", "aclk_peris_66",
			ENABLE_ACLK_PERIS, 0, CLK_IGNORE_UNUSED, 0),

	/* ENABLE_PCLK_PERIS */
	GATE(CLK_PCLK_HPM_APBIF, "pclk_hpm_apbif", "aclk_peris_66",
			ENABLE_PCLK_PERIS, 30, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_TMU1_APBIF, "pclk_tmu1_apbif", "aclk_peris_66",
			ENABLE_PCLK_PERIS, 24, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_TMU0_APBIF, "pclk_tmu0_apbif", "aclk_peris_66",
			ENABLE_PCLK_PERIS, 23, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PMU_PERIS, "pclk_pmu_peris", "aclk_peris_66",
			ENABLE_PCLK_PERIS, 22, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_SYSREG_PERIS, "pclk_sysreg_peris", "aclk_peris_66",
			ENABLE_PCLK_PERIS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_CMU_TOP_APBIF, "pclk_cmu_top_apbif", "aclk_peris_66",
			ENABLE_PCLK_PERIS, 20, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_WDT_APOLLO, "pclk_wdt_apollo", "aclk_peris_66",
			ENABLE_PCLK_PERIS, 17, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_WDT_ATLAS, "pclk_wdt_atlas", "aclk_peris_66",
			ENABLE_PCLK_PERIS, 16, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_MCT, "pclk_mct", "aclk_peris_66",
			ENABLE_PCLK_PERIS, 15, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_HDMI_CEC, "pclk_hdmi_cec", "aclk_peris_66",
			ENABLE_PCLK_PERIS, 14, CLK_IGNORE_UNUSED, 0),

	/* ENABLE_PCLK_PERIS_SECURE_TZPC */
	GATE(CLK_PCLK_TZPC12, "pclk_tzpc12", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 12, 0, 0),
	GATE(CLK_PCLK_TZPC11, "pclk_tzpc11", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 11, 0, 0),
	GATE(CLK_PCLK_TZPC10, "pclk_tzpc10", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 10, 0, 0),
	GATE(CLK_PCLK_TZPC9, "pclk_tzpc9", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 9, 0, 0),
	GATE(CLK_PCLK_TZPC8, "pclk_tzpc8", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 8, 0, 0),
	GATE(CLK_PCLK_TZPC7, "pclk_tzpc7", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 7, 0, 0),
	GATE(CLK_PCLK_TZPC6, "pclk_tzpc6", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 6, 0, 0),
	GATE(CLK_PCLK_TZPC5, "pclk_tzpc5", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 5, 0, 0),
	GATE(CLK_PCLK_TZPC4, "pclk_tzpc4", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 4, 0, 0),
	GATE(CLK_PCLK_TZPC3, "pclk_tzpc3", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 3, 0, 0),
	GATE(CLK_PCLK_TZPC2, "pclk_tzpc2", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 2, 0, 0),
	GATE(CLK_PCLK_TZPC1, "pclk_tzpc1", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 1, 0, 0),
	GATE(CLK_PCLK_TZPC0, "pclk_tzpc0", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TZPC, 0, 0, 0),

	/* ENABLE_PCLK_PERIS_SECURE_SECKEY_APBIF */
	GATE(CLK_PCLK_SECKEY_APBIF, "pclk_seckey_apbif", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_SECKEY_APBIF, 0, 0, 0),

	/* ENABLE_PCLK_PERIS_SECURE_CHIPID_APBIF */
	GATE(CLK_PCLK_CHIPID_APBIF, "pclk_chipid_apbif", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_CHIPID_APBIF, 0, 0, 0),

	/* ENABLE_PCLK_PERIS_SECURE_TOPRTC */
	GATE(CLK_PCLK_TOPRTC, "pclk_toprtc", "aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_TOPRTC, 0, 0, 0),

	/* ENABLE_PCLK_PERIS_SECURE_CUSTOM_EFUSE_APBIF */
	GATE(CLK_PCLK_CUSTOM_EFUSE_APBIF, "pclk_custom_efuse_apbif",
			"aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_CUSTOM_EFUSE_APBIF, 0, 0, 0),

	/* ENABLE_PCLK_PERIS_SECURE_ANTIRBK_CNT_APBIF */
	GATE(CLK_PCLK_ANTIRBK_CNT_APBIF, "pclk_antirbk_cnt_apbif",
			"aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_ANTIRBK_CNT_APBIF, 0, 0, 0),

	/* ENABLE_PCLK_PERIS_SECURE_OTP_CON_APBIF */
	GATE(CLK_PCLK_OTP_CON_APBIF, "pclk_otp_con_apbif",
			"aclk_peris_66",
			ENABLE_PCLK_PERIS_SECURE_OTP_CON_APBIF, 0, 0, 0),

	/* ENABLE_SCLK_PERIS */
	GATE(CLK_SCLK_ASV_TB, "sclk_asv_tb", "oscclk_efuse_common",
			ENABLE_SCLK_PERIS, 10, 0, 0),
	GATE(CLK_SCLK_TMU1, "sclk_tmu1", "oscclk_efuse_common",
			ENABLE_SCLK_PERIS, 4, 0, 0),
	GATE(CLK_SCLK_TMU0, "sclk_tmu0", "oscclk_efuse_common",
			ENABLE_SCLK_PERIS, 3, 0, 0),

	/* ENABLE_SCLK_PERIS_SECURE_SECKEY */
	GATE(CLK_SCLK_SECKEY, "sclk_seckey", "oscclk_efuse_common",
			ENABLE_SCLK_PERIS_SECURE_SECKEY, 0, 0, 0),

	/* ENABLE_SCLK_PERIS_SECURE_CHIPID */
	GATE(CLK_SCLK_CHIPID, "sclk_chipid", "oscclk_efuse_common",
			ENABLE_SCLK_PERIS_SECURE_CHIPID, 0, 0, 0),

	/* ENABLE_SCLK_PERIS_SECURE_TOPRTC */
	GATE(CLK_SCLK_TOPRTC, "sclk_toprtc", "oscclk_efuse_common",
			ENABLE_SCLK_PERIS_SECURE_TOPRTC, 0, 0, 0),

	/* ENABLE_SCLK_PERIS_SECURE_CUSTOM_EFUSE */
	GATE(CLK_SCLK_CUSTOM_EFUSE, "sclk_custom_efuse", "oscclk_efuse_common",
			ENABLE_SCLK_PERIS_SECURE_CUSTOM_EFUSE, 0, 0, 0),

	/* ENABLE_SCLK_PERIS_SECURE_ANTIRBK_CNT */
	GATE(CLK_SCLK_ANTIRBK_CNT, "sclk_antirbk_cnt", "oscclk_efuse_common",
			ENABLE_SCLK_PERIS_SECURE_ANTIRBK_CNT, 0, 0, 0),

	/* ENABLE_SCLK_PERIS_SECURE_OTP_CON */
	GATE(CLK_SCLK_OTP_CON, "sclk_otp_con", "oscclk_efuse_common",
			ENABLE_SCLK_PERIS_SECURE_OTP_CON, 0, 0, 0),
};

static struct samsung_cmu_info peris_cmu_info __initdata = {
	.gate_clks		= peris_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peris_gate_clks),
	.nr_clk_ids		= PERIS_NR_CLK,
	.clk_regs		= peris_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peris_clk_regs),
};

static void __init exynos5433_cmu_peris_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &peris_cmu_info);
}

CLK_OF_DECLARE(exynos5433_cmu_peris, "samsung,exynos5433-cmu-peris",
		exynos5433_cmu_peris_init);

/*
 * Register offset definitions for CMU_FSYS
 */
#define MUX_SEL_FSYS0			0x0200
#define MUX_SEL_FSYS1			0x0204
#define MUX_SEL_FSYS2			0x0208
#define MUX_SEL_FSYS3			0x020c
#define MUX_SEL_FSYS4			0x0210
#define MUX_ENABLE_FSYS0		0x0300
#define MUX_ENABLE_FSYS1		0x0304
#define MUX_ENABLE_FSYS2		0x0308
#define MUX_ENABLE_FSYS3		0x030c
#define MUX_ENABLE_FSYS4		0x0310
#define MUX_STAT_FSYS0			0x0400
#define MUX_STAT_FSYS1			0x0404
#define MUX_STAT_FSYS2			0x0408
#define MUX_STAT_FSYS3			0x040c
#define MUX_STAT_FSYS4			0x0410
#define MUX_IGNORE_FSYS2		0x0508
#define MUX_IGNORE_FSYS3		0x050c
#define ENABLE_ACLK_FSYS0		0x0800
#define ENABLE_ACLK_FSYS1		0x0804
#define ENABLE_PCLK_FSYS		0x0900
#define ENABLE_SCLK_FSYS		0x0a00
#define ENABLE_IP_FSYS0			0x0b00
#define ENABLE_IP_FSYS1			0x0b04

/* list of all parent clock list */
PNAME(mout_aclk_fsys_200_user_p)	= { "oscclk", "div_aclk_fsys_200", };
PNAME(mout_sclk_mmc2_user_p)		= { "oscclk", "sclk_mmc2_fsys", };
PNAME(mout_sclk_mmc1_user_p)		= { "oscclk", "sclk_mmc1_fsys", };
PNAME(mout_sclk_mmc0_user_p)		= { "oscclk", "sclk_mmc0_fsys", };

static unsigned long fsys_clk_regs[] __initdata = {
	MUX_SEL_FSYS0,
	MUX_SEL_FSYS1,
	MUX_SEL_FSYS2,
	MUX_SEL_FSYS3,
	MUX_SEL_FSYS4,
	MUX_ENABLE_FSYS0,
	MUX_ENABLE_FSYS1,
	MUX_ENABLE_FSYS2,
	MUX_ENABLE_FSYS3,
	MUX_ENABLE_FSYS4,
	MUX_STAT_FSYS0,
	MUX_STAT_FSYS1,
	MUX_STAT_FSYS2,
	MUX_STAT_FSYS3,
	MUX_STAT_FSYS4,
	MUX_IGNORE_FSYS2,
	MUX_IGNORE_FSYS3,
	ENABLE_ACLK_FSYS0,
	ENABLE_ACLK_FSYS1,
	ENABLE_PCLK_FSYS,
	ENABLE_SCLK_FSYS,
	ENABLE_IP_FSYS0,
	ENABLE_IP_FSYS1,
};

static struct samsung_mux_clock fsys_mux_clks[] __initdata = {
	/* MUX_SEL_FSYS0 */
	MUX(CLK_MOUT_ACLK_FSYS_200_USER, "mout_aclk_fsys_200_user",
			mout_aclk_fsys_200_user_p, MUX_SEL_FSYS0, 0, 1),

	/* MUX_SEL_FSYS1 */
	MUX(CLK_MOUT_SCLK_MMC2_USER, "mout_sclk_mmc2_user",
			mout_sclk_mmc2_user_p, MUX_SEL_FSYS1, 20, 1),
	MUX(CLK_MOUT_SCLK_MMC1_USER, "mout_sclk_mmc1_user",
			mout_sclk_mmc1_user_p, MUX_SEL_FSYS1, 16, 1),
	MUX(CLK_MOUT_SCLK_MMC0_USER, "mout_sclk_mmc0_user",
			mout_sclk_mmc0_user_p, MUX_SEL_FSYS1, 12, 1),
};

static struct samsung_gate_clock fsys_gate_clks[] __initdata = {
	/* ENABLE_ACLK_FSYS0 */
	GATE(CLK_ACLK_PCIE, "aclk_pcie", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 13, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PDMA1, "aclk_pdma1", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 11, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_TSI, "aclk_tsi", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 10, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_MMC2, "aclk_mmc2", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 8, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_MMC1, "aclk_mmc1", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 7, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_MMC0, "aclk_mmc0", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 6, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_UFS, "aclk_ufs", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 5, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_USBHOST20, "aclk_usbhost20", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_USBHOST30, "aclk_usbhost30", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_USBDRD30, "aclk_usbdrd30", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 1, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PDMA0, "aclk_pdma0", "mout_aclk_fsys_200_user",
			ENABLE_ACLK_FSYS0, 0, CLK_IGNORE_UNUSED, 0),

	/* ENABLE_SCLK_FSYS */
	GATE(CLK_SCLK_MMC2, "sclk_mmc2", "mout_sclk_mmc2_user",
			ENABLE_SCLK_FSYS, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC1, "sclk_mmc1", "mout_sclk_mmc1_user",
			ENABLE_SCLK_FSYS, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC0, "sclk_mmc0", "mout_sclk_mmc0_user",
			ENABLE_SCLK_FSYS, 2, CLK_SET_RATE_PARENT, 0),

	/* ENABLE_IP_FSYS0 */
	GATE(CLK_PDMA1, "pdma1", "aclk_pdma1", ENABLE_IP_FSYS0, 15, 0, 0),
	GATE(CLK_PDMA0, "pdma0", "aclk_pdma0", ENABLE_IP_FSYS0, 0, 0, 0),
};

static struct samsung_cmu_info fsys_cmu_info __initdata = {
	.mux_clks		= fsys_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys_mux_clks),
	.gate_clks		= fsys_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys_gate_clks),
	.nr_clk_ids		= FSYS_NR_CLK,
	.clk_regs		= fsys_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys_clk_regs),
};

static void __init exynos5433_cmu_fsys_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &fsys_cmu_info);
}

CLK_OF_DECLARE(exynos5433_cmu_fsys, "samsung,exynos5433-cmu-fsys",
		exynos5433_cmu_fsys_init);

/*
 * Register offset definitions for CMU_G2D
 */
#define MUX_SEL_G2D0				0x0200
#define MUX_SEL_ENABLE_G2D0			0x0300
#define MUX_SEL_STAT_G2D0			0x0400
#define DIV_G2D					0x0600
#define DIV_STAT_G2D				0x0700
#define DIV_ENABLE_ACLK_G2D			0x0800
#define DIV_ENABLE_ACLK_G2D_SECURE_SMMU_G2D	0x0804
#define DIV_ENABLE_PCLK_G2D			0x0900
#define DIV_ENABLE_PCLK_G2D_SECURE_SMMU_G2D	0x0904
#define DIV_ENABLE_IP_G2D0			0x0b00
#define DIV_ENABLE_IP_G2D1			0x0b04
#define DIV_ENABLE_IP_G2D_SECURE_SMMU_G2D	0x0b08

static unsigned long g2d_clk_regs[] __initdata = {
	MUX_SEL_G2D0,
	MUX_SEL_ENABLE_G2D0,
	MUX_SEL_STAT_G2D0,
	DIV_G2D,
	DIV_STAT_G2D,
	DIV_ENABLE_ACLK_G2D,
	DIV_ENABLE_ACLK_G2D_SECURE_SMMU_G2D,
	DIV_ENABLE_PCLK_G2D,
	DIV_ENABLE_PCLK_G2D_SECURE_SMMU_G2D,
	DIV_ENABLE_IP_G2D0,
	DIV_ENABLE_IP_G2D1,
	DIV_ENABLE_IP_G2D_SECURE_SMMU_G2D,
};

/* list of all parent clock list */
PNAME(mout_aclk_g2d_266_user_p)		= { "oscclk", "aclk_g2d_266", };
PNAME(mout_aclk_g2d_400_user_p)		= { "oscclk", "aclk_g2d_400", };

static struct samsung_mux_clock g2d_mux_clks[] __initdata = {
	/* MUX_SEL_G2D0 */
	MUX(CLK_MUX_ACLK_G2D_266_USER, "mout_aclk_g2d_266_user",
			mout_aclk_g2d_266_user_p, MUX_SEL_G2D0, 4, 1),
	MUX(CLK_MUX_ACLK_G2D_400_USER, "mout_aclk_g2d_400_user",
			mout_aclk_g2d_400_user_p, MUX_SEL_G2D0, 0, 1),
};

static struct samsung_div_clock g2d_div_clks[] __initdata = {
	/* DIV_G2D */
	DIV(CLK_DIV_PCLK_G2D, "div_pclk_g2d", "mout_aclk_g2d_266_user",
			DIV_G2D, 0, 2),
};

static struct samsung_gate_clock g2d_gate_clks[] __initdata = {
	/* DIV_ENABLE_ACLK_G2D */
	GATE(CLK_ACLK_SMMU_MDMA1, "aclk_smmu_mdma1", "mout_aclk_g2d_266_user",
			DIV_ENABLE_ACLK_G2D, 12, 0, 0),
	GATE(CLK_ACLK_BTS_MDMA1, "aclk_bts_mdam1", "mout_aclk_g2d_266_user",
			DIV_ENABLE_ACLK_G2D, 11, 0, 0),
	GATE(CLK_ACLK_BTS_G2D, "aclk_bts_g2d", "mout_aclk_g2d_400_user",
			DIV_ENABLE_ACLK_G2D, 10, 0, 0),
	GATE(CLK_ACLK_ALB_G2D, "aclk_alb_g2d", "mout_aclk_g2d_400_user",
			DIV_ENABLE_ACLK_G2D, 9, 0, 0),
	GATE(CLK_ACLK_AXIUS_G2DX, "aclk_axius_g2dx", "mout_aclk_g2d_400_user",
			DIV_ENABLE_ACLK_G2D, 8, 0, 0),
	GATE(CLK_ACLK_ASYNCAXI_SYSX, "aclk_asyncaxi_sysx",
			"mout_aclk_g2d_400_user", DIV_ENABLE_ACLK_G2D,
			7, 0, 0),
	GATE(CLK_ACLK_AHB2APB_G2D1P, "aclk_ahb2apb_g2d1p", "div_pclk_g2d",
			DIV_ENABLE_ACLK_G2D, 6, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AHB2APB_G2D0P, "aclk_ahb2apb_g2d0p", "div_pclk_g2d",
			DIV_ENABLE_ACLK_G2D, 5, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_XIU_G2DX, "aclk_xiu_g2dx", "mout_aclk_g2d_400_user",
			DIV_ENABLE_ACLK_G2D, 4, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_G2DNP_133, "aclk_g2dnp_133", "div_pclk_g2d",
			DIV_ENABLE_ACLK_G2D, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_G2DND_400, "aclk_g2dnd_400", "mout_aclk_g2d_400_user",
			DIV_ENABLE_ACLK_G2D, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_MDMA1, "aclk_mdma1", "mout_aclk_g2d_266_user",
			DIV_ENABLE_ACLK_G2D, 1, 0, 0),
	GATE(CLK_ACLK_G2D, "aclk_g2d", "mout_aclk_g2d_400_user",
			DIV_ENABLE_ACLK_G2D, 0, 0, 0),

	/* DIV_ENABLE_ACLK_G2D_SECURE_SMMU_G2D */
	GATE(CLK_ACLK_SMMU_G2D, "aclk_smmu_g2d", "mout_aclk_g2d_400_user",
		DIV_ENABLE_ACLK_G2D_SECURE_SMMU_G2D, 0, 0, 0),

	/* DIV_ENABLE_PCLK_G2D */
	GATE(CLK_PCLK_SMMU_MDMA1, "pclk_smmu_mdma1", "div_pclk_g2d",
			DIV_ENABLE_PCLK_G2D, 7, 0, 0),
	GATE(CLK_PCLK_BTS_MDMA1, "pclk_bts_mdam1", "div_pclk_g2d",
			DIV_ENABLE_PCLK_G2D, 6, 0, 0),
	GATE(CLK_PCLK_BTS_G2D, "pclk_bts_g2d", "div_pclk_g2d",
			DIV_ENABLE_PCLK_G2D, 5, 0, 0),
	GATE(CLK_PCLK_ALB_G2D, "pclk_alb_g2d", "div_pclk_g2d",
			DIV_ENABLE_PCLK_G2D, 4, 0, 0),
	GATE(CLK_PCLK_ASYNCAXI_SYSX, "pclk_asyncaxi_sysx", "div_pclk_g2d",
			DIV_ENABLE_PCLK_G2D, 3, 0, 0),
	GATE(CLK_PCLK_PMU_G2D, "pclk_pmu_g2d", "div_pclk_g2d",
			DIV_ENABLE_PCLK_G2D, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_SYSREG_G2D, "pclk_sysreg_g2d", "div_pclk_g2d",
			DIV_ENABLE_PCLK_G2D, 1, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_G2D, "pclk_g2d", "div_pclk_g2d", DIV_ENABLE_PCLK_G2D,
			0, 0, 0),

	/* DIV_ENABLE_PCLK_G2D_SECURE_SMMU_G2D */
	GATE(CLK_PCLK_SMMU_G2D, "pclk_smmu_g2d", "div_pclk_g2d",
		DIV_ENABLE_PCLK_G2D_SECURE_SMMU_G2D, 0, 0, 0),
};

static struct samsung_cmu_info g2d_cmu_info __initdata = {
	.mux_clks		= g2d_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(g2d_mux_clks),
	.div_clks		= g2d_div_clks,
	.nr_div_clks		= ARRAY_SIZE(g2d_div_clks),
	.gate_clks		= g2d_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(g2d_gate_clks),
	.nr_clk_ids		= G2D_NR_CLK,
	.clk_regs		= g2d_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(g2d_clk_regs),
};

static void __init exynos5433_cmu_g2d_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &g2d_cmu_info);
}

CLK_OF_DECLARE(exynos5433_cmu_g2d, "samsung,exynos5433-cmu-g2d",
		exynos5433_cmu_g2d_init);
