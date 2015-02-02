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

PNAME(mout_sclk_hdmi_spdif_p)	= { "sclk_audio1", "ioclk_spdif_extclk", };

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

	/* MUX_SEL_TOP_DISP */
	MUX(CLK_MOUT_SCLK_HDMI_SPDIF, "mout_sclk_hdmi_spdif",
			mout_sclk_hdmi_spdif_p, MUX_SEL_TOP_DISP, 0, 1),
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
#define MUX_SEL_MIF0			0x0200
#define MUX_SEL_MIF1			0x0204
#define MUX_SEL_MIF2			0x0208
#define MUX_SEL_MIF3			0x020c
#define MUX_SEL_MIF4			0x0210
#define MUX_SEL_MIF5			0x0214
#define MUX_SEL_MIF6			0x0218
#define MUX_SEL_MIF7			0x021c
#define MUX_ENABLE_MIF0			0x0300
#define MUX_ENABLE_MIF1			0x0304
#define MUX_ENABLE_MIF2			0x0308
#define MUX_ENABLE_MIF3			0x030c
#define MUX_ENABLE_MIF4			0x0310
#define MUX_ENABLE_MIF5			0x0314
#define MUX_ENABLE_MIF6			0x0318
#define MUX_ENABLE_MIF7			0x031c
#define MUX_STAT_MIF0			0x0400
#define MUX_STAT_MIF1			0x0404
#define MUX_STAT_MIF2			0x0408
#define MUX_STAT_MIF3			0x040c
#define MUX_STAT_MIF4			0x0410
#define MUX_STAT_MIF5			0x0414
#define MUX_STAT_MIF6			0x0418
#define MUX_STAT_MIF7			0x041c
#define DIV_MIF1			0x0604
#define DIV_MIF2			0x0608
#define DIV_MIF3			0x060c
#define DIV_MIF4			0x0610
#define DIV_MIF5			0x0614
#define DIV_MIF_PLL_FREQ_DET		0x0618
#define DIV_STAT_MIF1			0x0704
#define DIV_STAT_MIF2			0x0708
#define DIV_STAT_MIF3			0x070c
#define DIV_STAT_MIF4			0x0710
#define DIV_STAT_MIF5			0x0714
#define DIV_STAT_MIF_PLL_FREQ_DET	0x0718
#define ENABLE_ACLK_MIF0		0x0800
#define ENABLE_ACLK_MIF1		0x0804
#define ENABLE_ACLK_MIF2		0x0808
#define ENABLE_ACLK_MIF3		0x080c
#define ENABLE_PCLK_MIF			0x0900
#define ENABLE_PCLK_MIF_SECURE_DREX0_TZ	0x0904
#define ENABLE_PCLK_MIF_SECURE_DREX1_TZ	0x0908
#define ENABLE_PCLK_MIF_SECURE_MONOTONIC_CNT	0x090c
#define ENABLE_PCLK_MIF_SECURE_RTC	0x0910
#define ENABLE_SCLK_MIF			0x0a00
#define ENABLE_IP_MIF0			0x0b00
#define ENABLE_IP_MIF1			0x0b04
#define ENABLE_IP_MIF2			0x0b08
#define ENABLE_IP_MIF3			0x0b0c
#define ENABLE_IP_MIF_SECURE_DREX0_TZ	0x0b10
#define ENABLE_IP_MIF_SECURE_DREX1_TZ	0x0b14
#define ENABLE_IP_MIF_SECURE_MONOTONIC_CNT	0x0b18
#define ENABLE_IP_MIF_SECURE_RTC	0x0b1c
#define CLKOUT_CMU_MIF			0x0c00
#define CLKOUT_CMU_MIF_DIV_STAT		0x0c04
#define DREX_FREQ_CTRL0			0x1000
#define DREX_FREQ_CTRL1			0x1004
#define PAUSE				0x1008
#define DDRPHY_LOCK_CTRL		0x100c

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
	MUX_SEL_MIF0,
	MUX_SEL_MIF1,
	MUX_SEL_MIF2,
	MUX_SEL_MIF3,
	MUX_SEL_MIF4,
	MUX_SEL_MIF5,
	MUX_SEL_MIF6,
	MUX_SEL_MIF7,
	MUX_ENABLE_MIF0,
	MUX_ENABLE_MIF1,
	MUX_ENABLE_MIF2,
	MUX_ENABLE_MIF3,
	MUX_ENABLE_MIF4,
	MUX_ENABLE_MIF5,
	MUX_ENABLE_MIF6,
	MUX_ENABLE_MIF7,
	MUX_STAT_MIF0,
	MUX_STAT_MIF1,
	MUX_STAT_MIF2,
	MUX_STAT_MIF3,
	MUX_STAT_MIF4,
	MUX_STAT_MIF5,
	MUX_STAT_MIF6,
	MUX_STAT_MIF7,
	DIV_MIF1,
	DIV_MIF2,
	DIV_MIF3,
	DIV_MIF4,
	DIV_MIF5,
	DIV_MIF_PLL_FREQ_DET,
	DIV_STAT_MIF1,
	DIV_STAT_MIF2,
	DIV_STAT_MIF3,
	DIV_STAT_MIF4,
	DIV_STAT_MIF5,
	DIV_STAT_MIF_PLL_FREQ_DET,
	ENABLE_ACLK_MIF0,
	ENABLE_ACLK_MIF1,
	ENABLE_ACLK_MIF2,
	ENABLE_ACLK_MIF3,
	ENABLE_PCLK_MIF,
	ENABLE_PCLK_MIF_SECURE_DREX0_TZ,
	ENABLE_PCLK_MIF_SECURE_DREX1_TZ,
	ENABLE_PCLK_MIF_SECURE_MONOTONIC_CNT,
	ENABLE_PCLK_MIF_SECURE_RTC,
	ENABLE_SCLK_MIF,
	ENABLE_IP_MIF0,
	ENABLE_IP_MIF1,
	ENABLE_IP_MIF2,
	ENABLE_IP_MIF3,
	ENABLE_IP_MIF_SECURE_DREX0_TZ,
	ENABLE_IP_MIF_SECURE_DREX1_TZ,
	ENABLE_IP_MIF_SECURE_MONOTONIC_CNT,
	ENABLE_IP_MIF_SECURE_RTC,
	CLKOUT_CMU_MIF,
	CLKOUT_CMU_MIF_DIV_STAT,
	DREX_FREQ_CTRL0,
	DREX_FREQ_CTRL1,
	PAUSE,
	DDRPHY_LOCK_CTRL,
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

/* list of all parent clock list */
PNAME(mout_mfc_pll_div2_p)	= { "mout_mfc_pll", "dout_mfc_pll", };
PNAME(mout_bus_pll_div2_p)	= { "mout_bus_pll", "dout_bus_pll", };
PNAME(mout_mem1_pll_div2_p)	= { "mout_mem1_pll", "dout_mem1_pll", };
PNAME(mout_mem0_pll_div2_p)	= { "mout_mem0_pll", "dout_mem0_pll", };
PNAME(mout_mfc_pll_p)		= { "oscclk", "fout_mfc_pll", };
PNAME(mout_bus_pll_p)		= { "oscclk", "fout_bus_pll", };
PNAME(mout_mem1_pll_p)		= { "oscclk", "fout_mem1_pll", };
PNAME(mout_mem0_pll_p)		= { "oscclk", "fout_mem0_pll", };

PNAME(mout_clk2x_phy_c_p)	= { "mout_mem0_pll_div2", "mout_clkm_phy_b", };
PNAME(mout_clk2x_phy_b_p)	= { "mout_bus_pll_div2", "mout_clkm_phy_a", };
PNAME(mout_clk2x_phy_a_p)	= { "mout_bus_pll_div2", "mout_mfc_pll_div2", };
PNAME(mout_clkm_phy_b_p)	= { "mout_mem1_pll_div2", "mout_clkm_phy_a", };

PNAME(mout_aclk_mifnm_200_p)	= { "mout_mem0_pll_div2", "div_mif_pre", };
PNAME(mout_aclk_mifnm_400_p)	= { "mout_mem1_pll_div2", "mout_bus_pll_div2",};

PNAME(mout_aclk_disp_333_b_p)	= { "mout_aclk_disp_333_a",
				    "mout_bus_pll_div2", };
PNAME(mout_aclk_disp_333_a_p)	= { "mout_mfc_pll_div2", "sclk_mphy_pll", };

PNAME(mout_sclk_decon_vclk_c_p)	= { "mout_sclk_decon_vclk_b",
				    "sclk_mphy_pll", };
PNAME(mout_sclk_decon_vclk_b_p)	= { "mout_sclk_decon_vclk_a",
				    "mout_mfc_pll_div2", };
PNAME(mout_sclk_decon_p)	= { "oscclk", "mout_bus_pll_div2", };
PNAME(mout_sclk_decon_eclk_c_p)	= { "mout_sclk_decon_eclk_b",
				    "sclk_mphy_pll", };
PNAME(mout_sclk_decon_eclk_b_p)	= { "mout_sclk_decon_eclk_a",
				    "mout_mfc_pll_div2", };

PNAME(mout_sclk_decon_tv_eclk_c_p) = { "mout_sclk_decon_tv_eclk_b",
				       "sclk_mphy_pll", };
PNAME(mout_sclk_decon_tv_eclk_b_p) = { "mout_sclk_decon_tv_eclk_a",
				       "mout_mfc_pll_div2", };
PNAME(mout_sclk_dsd_c_p)	= { "mout_sclk_dsd_b", "mout_bus_pll_div2", };
PNAME(mout_sclk_dsd_b_p)	= { "mout_sclk_dsd_a", "sclk_mphy_pll", };
PNAME(mout_sclk_dsd_a_p)	= { "oscclk", "mout_mfc_pll_div2", };

PNAME(mout_sclk_dsim0_c_p)	= { "mout_sclk_dsim0_b", "sclk_mphy_pll", };
PNAME(mout_sclk_dsim0_b_p)	= { "mout_sclk_dsim0_a", "mout_mfc_pll_div2" };

PNAME(mout_sclk_decon_tv_vclk_c_p) = { "mout_sclk_decon_tv_vclk_b",
				       "sclk_mphy_pll", };
PNAME(mout_sclk_decon_tv_vclk_b_p) = { "mout_sclk_decon_tv_vclk_a",
				       "mout_mfc_pll_div2", };
PNAME(mout_sclk_dsim1_c_p)	= { "mout_sclk_dsim1_b", "sclk_mphy_pll", };
PNAME(mout_sclk_dsim1_b_p)	= { "mout_sclk_dsim1_a", "mout_mfc_pll_div2",};

static struct samsung_fixed_factor_clock mif_fixed_factor_clks[] __initdata = {
	/* dout_{mfc|bus|mem1|mem0}_pll is half fixed rate from parent mux */
	FFACTOR(CLK_DOUT_MFC_PLL, "dout_mfc_pll", "mout_mfc_pll", 1, 1, 0),
	FFACTOR(CLK_DOUT_BUS_PLL, "dout_bus_pll", "mout_bus_pll", 1, 1, 0),
	FFACTOR(CLK_DOUT_MEM1_PLL, "dout_mem1_pll", "mout_mem1_pll", 1, 1, 0),
	FFACTOR(CLK_DOUT_MEM0_PLL, "dout_mem0_pll", "mout_mem0_pll", 1, 1, 0),
};

static struct samsung_mux_clock mif_mux_clks[] __initdata = {
	/* MUX_SEL_MIF0 */
	MUX(CLK_MOUT_MFC_PLL_DIV2, "mout_mfc_pll_div2", mout_mfc_pll_div2_p,
			MUX_SEL_MIF0, 28, 1),
	MUX(CLK_MOUT_BUS_PLL_DIV2, "mout_bus_pll_div2", mout_bus_pll_div2_p,
			MUX_SEL_MIF0, 24, 1),
	MUX(CLK_MOUT_MEM1_PLL_DIV2, "mout_mem1_pll_div2", mout_mem1_pll_div2_p,
			MUX_SEL_MIF0, 20, 1),
	MUX(CLK_MOUT_MEM0_PLL_DIV2, "mout_mem0_pll_div2", mout_mem0_pll_div2_p,
			MUX_SEL_MIF0, 16, 1),
	MUX(CLK_MOUT_MFC_PLL, "mout_mfc_pll", mout_mfc_pll_p, MUX_SEL_MIF0,
			12, 1),
	MUX(CLK_MOUT_BUS_PLL, "mout_bus_pll", mout_bus_pll_p, MUX_SEL_MIF0,
			8, 1),
	MUX(CLK_MOUT_MEM1_PLL, "mout_mem1_pll", mout_mem1_pll_p, MUX_SEL_MIF0,
			4, 1),
	MUX(CLK_MOUT_MEM0_PLL, "mout_mem0_pll", mout_mem0_pll_p, MUX_SEL_MIF0,
			0, 1),

	/* MUX_SEL_MIF1 */
	MUX(CLK_MOUT_CLK2X_PHY_C, "mout_clk2x_phy_c", mout_clk2x_phy_c_p,
			MUX_SEL_MIF1, 24, 1),
	MUX(CLK_MOUT_CLK2X_PHY_B, "mout_clk2x_phy_b", mout_clk2x_phy_b_p,
			MUX_SEL_MIF1, 20, 1),
	MUX(CLK_MOUT_CLK2X_PHY_A, "mout_clk2x_phy_a", mout_clk2x_phy_a_p,
			MUX_SEL_MIF1, 16, 1),
	MUX(CLK_MOUT_CLKM_PHY_C, "mout_clkm_phy_c", mout_clk2x_phy_c_p,
			MUX_SEL_MIF1, 12, 1),
	MUX(CLK_MOUT_CLKM_PHY_B, "mout_clkm_phy_b", mout_clkm_phy_b_p,
			MUX_SEL_MIF1, 8, 1),
	MUX(CLK_MOUT_CLKM_PHY_A, "mout_clkm_phy_a", mout_clk2x_phy_a_p,
			MUX_SEL_MIF1, 4, 1),

	/* MUX_SEL_MIF2 */
	MUX(CLK_MOUT_ACLK_MIFNM_200, "mout_aclk_mifnm_200",
			mout_aclk_mifnm_200_p, MUX_SEL_MIF2, 8, 1),
	MUX(CLK_MOUT_ACLK_MIFNM_400, "mout_aclk_mifnm_400",
			mout_aclk_mifnm_400_p, MUX_SEL_MIF2, 0, 1),

	/* MUX_SEL_MIF3 */
	MUX(CLK_MOUT_ACLK_DISP_333_B, "mout_aclk_disp_333_b",
			mout_aclk_disp_333_b_p, MUX_SEL_MIF3, 4, 1),
	MUX(CLK_MOUT_ACLK_DISP_333_A, "mout_aclk_disp_333_a",
			mout_aclk_disp_333_a_p, MUX_SEL_MIF3, 0, 1),

	/* MUX_SEL_MIF4 */
	MUX(CLK_MOUT_SCLK_DECON_VCLK_C, "mout_sclk_decon_vclk_c",
			mout_sclk_decon_vclk_c_p, MUX_SEL_MIF4, 24, 1),
	MUX(CLK_MOUT_SCLK_DECON_VCLK_B, "mout_sclk_decon_vclk_b",
			mout_sclk_decon_vclk_b_p, MUX_SEL_MIF4, 20, 1),
	MUX(CLK_MOUT_SCLK_DECON_VCLK_A, "mout_sclk_decon_vclk_a",
			mout_sclk_decon_p, MUX_SEL_MIF4, 16, 1),
	MUX(CLK_MOUT_SCLK_DECON_ECLK_C, "mout_sclk_decon_eclk_c",
			mout_sclk_decon_eclk_c_p, MUX_SEL_MIF4, 8, 1),
	MUX(CLK_MOUT_SCLK_DECON_ECLK_B, "mout_sclk_decon_eclk_b",
			mout_sclk_decon_eclk_b_p, MUX_SEL_MIF4, 4, 1),
	MUX(CLK_MOUT_SCLK_DECON_ECLK_A, "mout_sclk_decon_eclk_a",
			mout_sclk_decon_p, MUX_SEL_MIF4, 0, 1),

	/* MUX_SEL_MIF5 */
	MUX(CLK_MOUT_SCLK_DECON_TV_ECLK_C, "mout_sclk_decon_tv_eclk_c",
			mout_sclk_decon_tv_eclk_c_p, MUX_SEL_MIF5, 24, 1),
	MUX(CLK_MOUT_SCLK_DECON_TV_ECLK_B, "mout_sclk_decon_tv_eclk_b",
			mout_sclk_decon_tv_eclk_b_p, MUX_SEL_MIF5, 20, 1),
	MUX(CLK_MOUT_SCLK_DECON_TV_ECLK_A, "mout_sclk_decon_tv_eclk_a",
			mout_sclk_decon_p, MUX_SEL_MIF5, 16, 1),
	MUX(CLK_MOUT_SCLK_DSD_C, "mout_sclk_dsd_c", mout_sclk_dsd_c_p,
			MUX_SEL_MIF5, 8, 1),
	MUX(CLK_MOUT_SCLK_DSD_B, "mout_sclk_dsd_b", mout_sclk_dsd_b_p,
			MUX_SEL_MIF5, 4, 1),
	MUX(CLK_MOUT_SCLK_DSD_A, "mout_sclk_dsd_a", mout_sclk_dsd_a_p,
			MUX_SEL_MIF5, 0, 1),

	/* MUX_SEL_MIF6 */
	MUX(CLK_MOUT_SCLK_DSIM0_C, "mout_sclk_dsim0_c", mout_sclk_dsim0_c_p,
			MUX_SEL_MIF6, 8, 1),
	MUX(CLK_MOUT_SCLK_DSIM0_B, "mout_sclk_dsim0_b", mout_sclk_dsim0_b_p,
			MUX_SEL_MIF6, 4, 1),
	MUX(CLK_MOUT_SCLK_DSIM0_A, "mout_sclk_dsim0_a", mout_sclk_decon_p,
			MUX_SEL_MIF6, 0, 1),

	/* MUX_SEL_MIF7 */
	MUX(CLK_MOUT_SCLK_DECON_TV_VCLK_C, "mout_sclk_decon_tv_vclk_c",
			mout_sclk_decon_tv_vclk_c_p, MUX_SEL_MIF7, 24, 1),
	MUX(CLK_MOUT_SCLK_DECON_TV_VCLK_B, "mout_sclk_decon_tv_vclk_b",
			mout_sclk_decon_tv_vclk_b_p, MUX_SEL_MIF7, 20, 1),
	MUX(CLK_MOUT_SCLK_DECON_TV_VCLK_A, "mout_sclk_decon_tv_vclk_a",
			mout_sclk_decon_p, MUX_SEL_MIF7, 16, 1),
	MUX(CLK_MOUT_SCLK_DSIM1_C, "mout_sclk_dsim1_c", mout_sclk_dsim1_c_p,
			MUX_SEL_MIF7, 8, 1),
	MUX(CLK_MOUT_SCLK_DSIM1_B, "mout_sclk_dsim1_b", mout_sclk_dsim1_b_p,
			MUX_SEL_MIF7, 4, 1),
	MUX(CLK_MOUT_SCLK_DSIM1_A, "mout_sclk_dsim1_a", mout_sclk_decon_p,
			MUX_SEL_MIF7, 0, 1),
};

static struct samsung_div_clock mif_div_clks[] __initdata = {
	/* DIV_MIF1 */
	DIV(CLK_DIV_SCLK_HPM_MIF, "div_sclk_hpm_mif", "div_clk2x_phy",
			DIV_MIF1, 16, 2),
	DIV(CLK_DIV_ACLK_DREX1, "div_aclk_drex1", "div_clk2x_phy", DIV_MIF1,
			12, 2),
	DIV(CLK_DIV_ACLK_DREX0, "div_aclk_drex0", "div_clk2x_phy", DIV_MIF1,
			8, 2),
	DIV(CLK_DIV_CLK2XPHY, "div_clk2x_phy", "mout_clk2x_phy_c", DIV_MIF1,
			4, 4),

	/* DIV_MIF2 */
	DIV(CLK_DIV_ACLK_MIF_266, "div_aclk_mif_266", "mout_bus_pll_div2",
			DIV_MIF2, 20, 3),
	DIV(CLK_DIV_ACLK_MIFND_133, "div_aclk_mifnd_133", "div_mif_pre",
			DIV_MIF2, 16, 4),
	DIV(CLK_DIV_ACLK_MIF_133, "div_aclk_mif_133", "div_mif_pre",
			DIV_MIF2, 12, 4),
	DIV(CLK_DIV_ACLK_MIFNM_200, "div_aclk_mifnm_200",
			"mout_aclk_mifnm_200", DIV_MIF2, 8, 3),
	DIV(CLK_DIV_ACLK_MIF_200, "div_aclk_mif_200", "div_aclk_mif_400",
			DIV_MIF2, 4, 2),
	DIV(CLK_DIV_ACLK_MIF_400, "div_aclk_mif_400", "mout_aclk_mifnm_400",
			DIV_MIF2, 0, 3),

	/* DIV_MIF3 */
	DIV(CLK_DIV_ACLK_BUS2_400, "div_aclk_bus2_400", "div_mif_pre",
			DIV_MIF3, 16, 4),
	DIV(CLK_DIV_ACLK_DISP_333, "div_aclk_disp_333", "mout_aclk_disp_333_b",
			DIV_MIF3, 4, 3),
	DIV(CLK_DIV_ACLK_CPIF_200, "div_aclk_cpif_200", "mout_aclk_mifnm_200",
			DIV_MIF3, 0, 3),

	/* DIV_MIF4 */
	DIV(CLK_DIV_SCLK_DSIM1, "div_sclk_dsim1", "mout_sclk_dsim1_c",
			DIV_MIF4, 24, 4),
	DIV(CLK_DIV_SCLK_DECON_TV_VCLK, "div_sclk_decon_tv_vclk",
			"mout_sclk_decon_tv_vclk_c", DIV_MIF4, 20, 4),
	DIV(CLK_DIV_SCLK_DSIM0, "div_sclk_dsim0", "mout_sclk_dsim0_c",
			DIV_MIF4, 16, 4),
	DIV(CLK_DIV_SCLK_DSD, "div_sclk_dsd", "mout_sclk_dsd_c",
			DIV_MIF4, 12, 4),
	DIV(CLK_DIV_SCLK_DECON_TV_ECLK, "div_sclk_decon_tv_eclk",
			"mout_sclk_decon_tv_eclk_c", DIV_MIF4, 8, 4),
	DIV(CLK_DIV_SCLK_DECON_VCLK, "div_sclk_decon_vclk",
			"mout_sclk_decon_vclk_c", DIV_MIF4, 4, 4),
	DIV(CLK_DIV_SCLK_DECON_ECLK, "div_sclk_decon_eclk",
			"mout_sclk_decon_eclk_c", DIV_MIF4, 0, 4),

	/* DIV_MIF5 */
	DIV(CLK_DIV_MIF_PRE, "div_mif_pre", "mout_bus_pll_div2", DIV_MIF5,
			0, 3),
};

static struct samsung_gate_clock mif_gate_clks[] __initdata = {
	/* ENABLE_ACLK_MIF0 */
	GATE(CLK_CLK2X_PHY1, "clk2k_phy1", "div_clk2x_phy", ENABLE_ACLK_MIF0,
			19, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_CLK2X_PHY0, "clk2x_phy0", "div_clk2x_phy", ENABLE_ACLK_MIF0,
			18, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_CLKM_PHY1, "clkm_phy1", "mout_clkm_phy_c", ENABLE_ACLK_MIF0,
			17, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_CLKM_PHY0, "clkm_phy0", "mout_clkm_phy_c", ENABLE_ACLK_MIF0,
			16, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_RCLK_DREX1, "rclk_drex1", "oscclk", ENABLE_ACLK_MIF0,
			15, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_RCLK_DREX0, "rclk_drex0", "oscclk", ENABLE_ACLK_MIF0,
			14, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX1_TZ, "aclk_drex1_tz", "div_aclk_drex1",
			ENABLE_ACLK_MIF0, 13, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX0_TZ, "aclk_drex0_tz", "div_aclk_drex0",
			ENABLE_ACLK_MIF0, 12, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX1_PEREV, "aclk_drex1_perev", "div_aclk_drex1",
			ENABLE_ACLK_MIF0, 11, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX0_PEREV, "aclk_drex0_perev", "div_aclk_drex0",
			ENABLE_ACLK_MIF0, 10, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX1_MEMIF, "aclk_drex1_memif", "div_aclk_drex1",
			ENABLE_ACLK_MIF0, 9, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX0_MEMIF, "aclk_drex0_memif", "div_aclk_drex0",
			ENABLE_ACLK_MIF0, 8, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX1_SCH, "aclk_drex1_sch", "div_aclk_drex1",
			ENABLE_ACLK_MIF0, 7, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX0_SCH, "aclk_drex0_sch", "div_aclk_drex0",
			ENABLE_ACLK_MIF0, 6, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX1_BUSIF, "aclk_drex1_busif", "div_aclk_drex1",
			ENABLE_ACLK_MIF0, 5, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX0_BUSIF, "aclk_drex0_busif", "div_aclk_drex0",
			ENABLE_ACLK_MIF0, 4, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX1_BUSIF_RD, "aclk_drex1_busif_rd", "div_aclk_drex1",
			ENABLE_ACLK_MIF0, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX0_BUSIF_RD, "aclk_drex0_busif_rd", "div_aclk_drex0",
			ENABLE_ACLK_MIF0, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX1, "aclk_drex1", "div_aclk_drex1",
			ENABLE_ACLK_MIF0, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DREX0, "aclk_drex0", "div_aclk_drex0",
			ENABLE_ACLK_MIF0, 1, CLK_IGNORE_UNUSED, 0),

	/* ENABLE_ACLK_MIF1 */
	GATE(CLK_ACLK_ASYNCAXIS_MIF_IMEM, "aclk_asyncaxis_mif_imem",
			"div_aclk_mif_200", ENABLE_ACLK_MIF1, 28,
			CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIS_NOC_P_CCI, "aclk_asyncaxis_noc_p_cci",
			"div_aclk_mif_200", ENABLE_ACLK_MIF1,
			27, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIM_NOC_P_CCI, "aclk_asyncaxim_noc_p_cci",
			"div_aclk_mif_133", ENABLE_ACLK_MIF1,
			26, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIS_CP1, "aclk_asyncaxis_cp1",
			"div_aclk_mifnm_200", ENABLE_ACLK_MIF1,
			25, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIM_CP1, "aclk_asyncaxim_cp1",
			"div_aclk_drex1", ENABLE_ACLK_MIF1,
			24, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIS_CP0, "aclk_asyncaxis_cp0",
			"div_aclk_mifnm_200", ENABLE_ACLK_MIF1,
			23, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIM_CP0, "aclk_asyncaxim_cp0",
			"div_aclk_drex0", ENABLE_ACLK_MIF1,
			22, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIS_DREX1_3, "aclk_asyncaxis_drex1_3",
			"div_aclk_mif_133", ENABLE_ACLK_MIF1,
			21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIM_DREX1_3, "aclk_asyncaxim_drex1_3",
			"div_aclk_drex1", ENABLE_ACLK_MIF1,
			20, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIS_DREX1_1, "aclk_asyncaxis_drex1_1",
			"div_aclk_mif_133", ENABLE_ACLK_MIF1,
			19, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIM_DREX1_1, "aclk_asyncaxim_drex1_1",
			"div_aclk_drex1", ENABLE_ACLK_MIF1,
			18, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIS_DREX1_0, "aclk_asyncaxis_drex1_0",
			"div_aclk_mif_133", ENABLE_ACLK_MIF1,
			17, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIM_DREX1_0, "aclk_asyncaxim_drex1_0",
			"div_aclk_drex1", ENABLE_ACLK_MIF1,
			16, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIS_DREX0_3, "aclk_asyncaxis_drex0_3",
			"div_aclk_mif_133", ENABLE_ACLK_MIF1,
			15, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIM_DREX0_3, "aclk_asyncaxim_drex0_3",
			"div_aclk_drex0", ENABLE_ACLK_MIF1,
			14, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIS_DREX0_1, "aclk_asyncaxis_drex0_1",
			"div_aclk_mif_133", ENABLE_ACLK_MIF1,
			13, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIM_DREX0_1, "aclk_asyncaxim_drex0_1",
			"div_aclk_drex0", ENABLE_ACLK_MIF1,
			12, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIS_DREX0_0, "aclk_asyncaxis_drex0_0",
			"div_aclk_mif_133", ENABLE_ACLK_MIF1,
			11, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAXIM_DREX0_0, "aclk_asyncaxim_drex0_0",
			"div_aclk_drex0", ENABLE_ACLK_MIF1,
			10, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AHB2APB_MIF2P, "aclk_ahb2apb_mif2p", "div_aclk_mif_133",
			ENABLE_ACLK_MIF1, 9, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AHB2APB_MIF1P, "aclk_ahb2apb_mif1p", "div_aclk_mif_133",
			ENABLE_ACLK_MIF1, 8, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AHB2APB_MIF0P, "aclk_ahb2apb_mif0p", "div_aclk_mif_133",
			ENABLE_ACLK_MIF1, 7, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_IXIU_CCI, "aclk_ixiu_cci", "div_aclk_mif_400",
			ENABLE_ACLK_MIF1, 6, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_XIU_MIFSFRX, "aclk_xiu_mifsfrx", "div_aclk_mif_200",
			ENABLE_ACLK_MIF1, 5, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_MIFNP_133, "aclk_mifnp_133", "div_aclk_mif_133",
			ENABLE_ACLK_MIF1, 4, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_MIFNM_200, "aclk_mifnm_200", "div_aclk_mifnm_200",
			ENABLE_ACLK_MIF1, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_MIFND_133, "aclk_mifnd_133", "div_aclk_mifnd_133",
			ENABLE_ACLK_MIF1, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_MIFND_400, "aclk_mifnd_400", "div_aclk_mif_400",
			ENABLE_ACLK_MIF1, 1, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_CCI, "aclk_cci", "div_aclk_mif_400", ENABLE_ACLK_MIF1,
			0, CLK_IGNORE_UNUSED, 0),

	/* ENABLE_ACLK_MIF2 */
	GATE(CLK_ACLK_MIFND_266, "aclk_mifnd_266", "div_aclk_mif_266",
			ENABLE_ACLK_MIF2, 20, 0, 0),
	GATE(CLK_ACLK_PPMU_DREX1S3, "aclk_ppmu_drex1s3", "div_aclk_drex1",
			ENABLE_ACLK_MIF2, 17, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PPMU_DREX1S1, "aclk_ppmu_drex1s1", "div_aclk_drex1",
			ENABLE_ACLK_MIF2, 16, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PPMU_DREX1S0, "aclk_ppmu_drex1s0", "div_aclk_drex1",
			ENABLE_ACLK_MIF2, 15, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PPMU_DREX0S3, "aclk_ppmu_drex0s3", "div_aclk_drex0",
			ENABLE_ACLK_MIF2, 14, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PPMU_DREX0S1, "aclk_ppmu_drex0s1", "div_aclk_drex0",
			ENABLE_ACLK_MIF2, 13, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_PPMU_DREX0S0, "aclk_ppmu_drex0s0", "div_aclk_drex0",
			ENABLE_ACLK_MIF2, 12, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AXIDS_CCI_MIFSFRX, "aclk_axids_cci_mifsfrx",
			"div_aclk_mif_200", ENABLE_ACLK_MIF2, 7,
			CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AXISYNCDNS_CCI, "aclk_axisyncdns_cci",
			"div_aclk_mif_400", ENABLE_ACLK_MIF2,
			5, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AXISYNCDN_CCI, "aclk_axisyncdn_cci", "div_aclk_mif_400",
			ENABLE_ACLK_MIF2, 4, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AXISYNCDN_NOC_D, "aclk_axisyncdn_noc_d",
			"div_aclk_mif_200", ENABLE_ACLK_MIF2,
			3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_ASYNCAPBS_MIF_CSSYS, "aclk_asyncapbs_mif_cssys",
			"div_aclk_mifnd_133", ENABLE_ACLK_MIF2, 0, 0, 0),

	/* ENABLE_ACLK_MIF3 */
	GATE(CLK_ACLK_BUS2_400, "aclk_bus2_400", "div_aclk_bus2_400",
			ENABLE_ACLK_MIF3, 4,
			CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_ACLK_DISP_333, "aclk_disp_333", "div_aclk_disp_333",
			ENABLE_ACLK_MIF3, 1,
			CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_ACLK_CPIF_200, "aclk_cpif_200", "div_aclk_cpif_200",
			ENABLE_ACLK_MIF3, 0,
			CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0),

	/* ENABLE_PCLK_MIF */
	GATE(CLK_PCLK_PPMU_DREX1S3, "pclk_ppmu_drex1s3", "div_aclk_drex1",
			ENABLE_PCLK_MIF, 29, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PPMU_DREX1S1, "pclk_ppmu_drex1s1", "div_aclk_drex1",
			ENABLE_PCLK_MIF, 28, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PPMU_DREX1S0, "pclk_ppmu_drex1s0", "div_aclk_drex1",
			ENABLE_PCLK_MIF, 27, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PPMU_DREX0S3, "pclk_ppmu_drex0s3", "div_aclk_drex0",
			ENABLE_PCLK_MIF, 26, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PPMU_DREX0S1, "pclk_ppmu_drex0s1", "div_aclk_drex0",
			ENABLE_PCLK_MIF, 25, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_PPMU_DREX0S0, "pclk_ppmu_drex0s0", "div_aclk_drex0",
			ENABLE_PCLK_MIF, 24, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_ASYNCAXI_NOC_P_CCI, "pclk_asyncaxi_noc_p_cci",
			"div_aclk_mif_133", ENABLE_PCLK_MIF, 21,
			CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_ASYNCAXI_CP1, "pclk_asyncaxi_cp1", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 19, 0, 0),
	GATE(CLK_PCLK_ASYNCAXI_CP0, "pclk_asyncaxi_cp0", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 18, 0, 0),
	GATE(CLK_PCLK_ASYNCAXI_DREX1_3, "pclk_asyncaxi_drex1_3",
			"div_aclk_mif_133", ENABLE_PCLK_MIF, 17, 0, 0),
	GATE(CLK_PCLK_ASYNCAXI_DREX1_1, "pclk_asyncaxi_drex1_1",
			"div_aclk_mif_133", ENABLE_PCLK_MIF, 16, 0, 0),
	GATE(CLK_PCLK_ASYNCAXI_DREX1_0, "pclk_asyncaxi_drex1_0",
			"div_aclk_mif_133", ENABLE_PCLK_MIF, 15, 0, 0),
	GATE(CLK_PCLK_ASYNCAXI_DREX0_3, "pclk_asyncaxi_drex0_3",
			"div_aclk_mif_133", ENABLE_PCLK_MIF, 14, 0, 0),
	GATE(CLK_PCLK_ASYNCAXI_DREX0_1, "pclk_asyncaxi_drex0_1",
			"div_aclk_mif_133", ENABLE_PCLK_MIF, 13, 0, 0),
	GATE(CLK_PCLK_ASYNCAXI_DREX0_0, "pclk_asyncaxi_drex0_0",
			"div_aclk_mif_133", ENABLE_PCLK_MIF, 12, 0, 0),
	GATE(CLK_PCLK_MIFSRVND_133, "pclk_mifsrvnd_133", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 11, 0, 0),
	GATE(CLK_PCLK_PMU_MIF, "pclk_pmu_mif", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 10, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_SYSREG_MIF, "pclk_sysreg_mif", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 9, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_GPIO_ALIVE, "pclk_gpio_alive", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 8, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_ABB, "pclk_abb", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 7, 0, 0),
	GATE(CLK_PCLK_PMU_APBIF, "pclk_pmu_apbif", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 6, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_DDR_PHY1, "pclk_ddr_phy1", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 5, 0, 0),
	GATE(CLK_PCLK_DREX1, "pclk_drex1", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_DDR_PHY0, "pclk_ddr_phy0", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 2, 0, 0),
	GATE(CLK_PCLK_DREX0, "pclk_drex0", "div_aclk_mif_133",
			ENABLE_PCLK_MIF, 0, CLK_IGNORE_UNUSED, 0),

	/* ENABLE_PCLK_MIF_SECURE_DREX0_TZ */
	GATE(CLK_PCLK_DREX0_TZ, "pclk_drex0_tz", "div_aclk_mif_133",
			ENABLE_PCLK_MIF_SECURE_DREX0_TZ, 0, 0, 0),

	/* ENABLE_PCLK_MIF_SECURE_DREX1_TZ */
	GATE(CLK_PCLK_DREX1_TZ, "pclk_drex1_tz", "div_aclk_mif_133",
			ENABLE_PCLK_MIF_SECURE_DREX1_TZ, 0, 0, 0),

	/* ENABLE_PCLK_MIF_SECURE_MONOTONIC_CNT */
	GATE(CLK_PCLK_MONOTONIC_CNT, "pclk_monotonic_cnt", "div_aclk_mif_133",
			ENABLE_PCLK_MIF_SECURE_RTC, 0, 0, 0),

	/* ENABLE_PCLK_MIF_SECURE_RTC */
	GATE(CLK_PCLK_RTC, "pclk_rtc", "div_aclk_mif_133",
			ENABLE_PCLK_MIF_SECURE_RTC, 0, 0, 0),

	/* ENABLE_SCLK_MIF */
	GATE(CLK_SCLK_DSIM1_DISP, "sclk_dsim1_disp", "div_sclk_dsim1",
			ENABLE_SCLK_MIF, 15, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCLK_DECON_TV_VCLK_DISP, "sclk_decon_tv_vclk_disp",
			"div_sclk_decon_tv_vclk", ENABLE_SCLK_MIF,
			14, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCLK_DSIM0_DISP, "sclk_dsim0_disp", "div_sclk_dsim0",
			ENABLE_SCLK_MIF, 9, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCLK_DSD_DISP, "sclk_dsd_disp", "div_sclk_dsd",
			ENABLE_SCLK_MIF, 8, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCLK_DECON_TV_ECLK_DISP, "sclk_decon_tv_eclk_disp",
			"div_sclk_decon_tv_eclk", ENABLE_SCLK_MIF,
			7, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCLK_DECON_VCLK_DISP, "sclk_decon_vclk_disp",
			"div_sclk_decon_vclk", ENABLE_SCLK_MIF,
			6, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCLK_DECON_ECLK_DISP, "sclk_decon_eclk_disp",
			"div_sclk_decon_eclk", ENABLE_SCLK_MIF,
			5, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCLK_HPM_MIF, "sclk_hpm_mif", "div_sclk_hpm_mif",
			ENABLE_SCLK_MIF, 4,
			CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MFC_PLL, "sclk_mfc_pll", "mout_mfc_pll_div2",
			ENABLE_SCLK_MIF, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCLK_BUS_PLL, "sclk_bus_pll", "mout_bus_pll_div2",
			ENABLE_SCLK_MIF, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCLK_BUS_PLL_APOLLO, "sclk_bus_pll_apollo", "sclk_bus_pll",
			ENABLE_SCLK_MIF, 1, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCLK_BUS_PLL_ATLAS, "sclk_bus_pll_atlas", "sclk_bus_pll",
			ENABLE_SCLK_MIF, 0, CLK_IGNORE_UNUSED, 0),

	/* ENABLE_SCLK_TOP_DISP */
	GATE(CLK_SCLK_HDMI_SPDIF_DISP, "sclk_hdmi_spdif_disp",
			"mout_sclk_hdmi_spdif", ENABLE_SCLK_TOP_DISP, 0,
			CLK_IGNORE_UNUSED, 0),
};

static struct samsung_cmu_info mif_cmu_info __initdata = {
	.pll_clks		= mif_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(mif_pll_clks),
	.mux_clks		= mif_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mif_mux_clks),
	.div_clks		= mif_div_clks,
	.nr_div_clks		= ARRAY_SIZE(mif_div_clks),
	.gate_clks		= mif_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(mif_gate_clks),
	.fixed_factor_clks	= mif_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(mif_fixed_factor_clks),
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

/*
 * Register offset definitions for CMU_DISP
 */
#define DISP_PLL_LOCK			0x0000
#define DISP_PLL_CON0			0x0100
#define DISP_PLL_CON1			0x0104
#define DISP_PLL_FREQ_DET		0x0108
#define MUX_SEL_DISP0			0x0200
#define MUX_SEL_DISP1			0x0204
#define MUX_SEL_DISP2			0x0208
#define MUX_SEL_DISP3			0x020c
#define MUX_SEL_DISP4			0x0210
#define MUX_ENABLE_DISP0		0x0300
#define MUX_ENABLE_DISP1		0x0304
#define MUX_ENABLE_DISP2		0x0308
#define MUX_ENABLE_DISP3		0x030c
#define MUX_ENABLE_DISP4		0x0310
#define MUX_STAT_DISP0			0x0400
#define MUX_STAT_DISP1			0x0404
#define MUX_STAT_DISP2			0x0408
#define MUX_STAT_DISP3			0x040c
#define MUX_STAT_DISP4			0x0410
#define MUX_IGNORE_DISP2		0x0508
#define DIV_DISP			0x0600
#define DIV_DISP_PLL_FREQ_DET		0x0604
#define DIV_STAT_DISP			0x0700
#define DIV_STAT_DISP_PLL_FREQ_DET	0x0704
#define ENABLE_ACLK_DISP0		0x0800
#define ENABLE_ACLK_DISP1		0x0804
#define ENABLE_PCLK_DISP		0x0900
#define ENABLE_SCLK_DISP		0x0a00
#define ENABLE_IP_DISP0			0x0b00
#define ENABLE_IP_DISP1			0x0b04
#define CLKOUT_CMU_DISP			0x0c00
#define CLKOUT_CMU_DISP_DIV_STAT	0x0c04

static unsigned long disp_clk_regs[] __initdata = {
	DISP_PLL_LOCK,
	DISP_PLL_CON0,
	DISP_PLL_CON1,
	DISP_PLL_FREQ_DET,
	MUX_SEL_DISP0,
	MUX_SEL_DISP1,
	MUX_SEL_DISP2,
	MUX_SEL_DISP3,
	MUX_SEL_DISP4,
	MUX_ENABLE_DISP0,
	MUX_ENABLE_DISP1,
	MUX_ENABLE_DISP2,
	MUX_ENABLE_DISP3,
	MUX_ENABLE_DISP4,
	MUX_STAT_DISP0,
	MUX_STAT_DISP1,
	MUX_STAT_DISP2,
	MUX_STAT_DISP3,
	MUX_STAT_DISP4,
	MUX_IGNORE_DISP2,
	DIV_DISP,
	DIV_DISP_PLL_FREQ_DET,
	DIV_STAT_DISP,
	DIV_STAT_DISP_PLL_FREQ_DET,
	ENABLE_ACLK_DISP0,
	ENABLE_ACLK_DISP1,
	ENABLE_PCLK_DISP,
	ENABLE_SCLK_DISP,
	ENABLE_IP_DISP0,
	ENABLE_IP_DISP1,
	CLKOUT_CMU_DISP,
	CLKOUT_CMU_DISP_DIV_STAT,
};

/* list of all parent clock list */
PNAME(mout_disp_pll_p)			= { "oscclk", "fout_disp_pll", };
PNAME(mout_sclk_dsim1_user_p)		= { "oscclk", "sclk_dsim1_disp", };
PNAME(mout_sclk_dsim0_user_p)		= { "oscclk", "sclk_dsim0_disp", };
PNAME(mout_sclk_dsd_user_p)		= { "oscclk", "sclk_dsd_disp", };
PNAME(mout_sclk_decon_tv_eclk_user_p)	= { "oscclk",
					    "sclk_decon_tv_eclk_disp", };
PNAME(mout_sclk_decon_vclk_user_p)	= { "oscclk",
					    "sclk_decon_vclk_disp", };
PNAME(mout_sclk_decon_eclk_user_p)	= { "oscclk",
					    "sclk_decon_eclk_disp", };
PNAME(mout_sclk_decon_tv_vlkc_user_p)	= { "oscclk",
					    "sclk_decon_tv_vclk_disp", };
PNAME(mout_aclk_disp_333_user_p)	= { "oscclk", "aclk_disp_333", };

PNAME(mout_phyclk_mipidphy1_bitclkdiv8_user_p)	= { "oscclk",
					"phyclk_mipidphy1_bitclkdiv8_phy", };
PNAME(mout_phyclk_mipidphy1_rxclkesc0_user_p)	= { "oscclk",
					"phyclk_mipidphy1_rxclkesc0_phy", };
PNAME(mout_phyclk_mipidphy0_bitclkdiv8_user_p)	= { "oscclk",
					"phyclk_mipidphy0_bitclkdiv8_phy", };
PNAME(mout_phyclk_mipidphy0_rxclkesc0_user_p)	= { "oscclk",
					"phyclk_mipidphy0_rxclkesc0_phy", };
PNAME(mout_phyclk_hdmiphy_tmds_clko_user_p)	= { "oscclk",
					"phyclk_hdmiphy_tmds_clko_phy", };
PNAME(mout_phyclk_hdmiphy_pixel_clko_user_p)	= { "oscclk",
					"phyclk_hdmiphy_pixel_clko_phy", };

PNAME(mout_sclk_dsim0_p)		= { "mout_disp_pll",
					    "mout_sclk_dsim0_user", };
PNAME(mout_sclk_decon_tv_eclk_p)	= { "mout_disp_pll",
					    "mout_sclk_decon_tv_eclk_user", };
PNAME(mout_sclk_decon_vclk_p)		= { "mout_disp_pll",
					    "mout_sclk_decon_vclk_user", };
PNAME(mout_sclk_decon_eclk_p)		= { "mout_disp_pll",
					    "mout_sclk_decon_eclk_user", };

PNAME(mout_sclk_dsim1_b_disp_p)		= { "mout_sclk_dsim1_a_disp",
					    "mout_sclk_dsim1_user", };
PNAME(mout_sclk_decon_tv_vclk_c_disp_p)	= {
				"mout_phyclk_hdmiphy_pixel_clko_user",
				"mout_sclk_decon_tv_vclk_b_disp", };
PNAME(mout_sclk_decon_tv_vclk_b_disp_p)	= { "mout_sclk_decon_tv_vclk_a_disp",
					    "mout_sclk_decon_tv_vclk_user", };

static struct samsung_pll_clock disp_pll_clks[] __initdata = {
	PLL(pll_35xx, CLK_FOUT_DISP_PLL, "fout_disp_pll", "oscclk",
		DISP_PLL_LOCK, DISP_PLL_CON0, exynos5443_pll_rates),
};

static struct samsung_fixed_factor_clock disp_fixed_factor_clks[] __initdata = {
	/*
	 * sclk_rgb_{vclk|tv_vclk} is half clock of sclk_decon_{vclk|tv_vclk}.
	 * The divider has fixed value (2) between sclk_rgb_{vclk|tv_vclk}
	 * and sclk_decon_{vclk|tv_vclk}.
	 */
	FFACTOR(CLK_SCLK_RGB_VCLK, "sclk_rgb_vclk", "sclk_decon_vclk",
			1, 2, 0),
	FFACTOR(CLK_SCLK_RGB_TV_VCLK, "sclk_rgb_tv_vclk", "sclk_decon_tv_vclk",
			1, 2, 0),
};

static struct samsung_fixed_rate_clock disp_fixed_clks[] __initdata = {
	/* PHY clocks from MIPI_DPHY1 */
	FRATE(0, "phyclk_mipidphy1_bitclkdiv8_phy", NULL, CLK_IS_ROOT,
			188000000),
	FRATE(0, "phyclk_mipidphy1_rxclkesc0_phy", NULL, CLK_IS_ROOT,
			100000000),
	/* PHY clocks from MIPI_DPHY0 */
	FRATE(0, "phyclk_mipidphy0_bitclkdiv8_phy", NULL, CLK_IS_ROOT,
			188000000),
	FRATE(0, "phyclk_mipidphy0_rxclkesc0_phy", NULL, CLK_IS_ROOT,
			100000000),
	/* PHY clocks from HDMI_PHY */
	FRATE(0, "phyclk_hdmiphy_tmds_clko_phy", NULL, CLK_IS_ROOT, 300000000),
	FRATE(0, "phyclk_hdmiphy_pixel_clko_phy", NULL, CLK_IS_ROOT, 166000000),
};

static struct samsung_mux_clock disp_mux_clks[] __initdata = {
	/* MUX_SEL_DISP0 */
	MUX(CLK_MOUT_DISP_PLL, "mout_disp_pll", mout_disp_pll_p, MUX_SEL_DISP0,
			0, 1),

	/* MUX_SEL_DISP1 */
	MUX(CLK_MOUT_SCLK_DSIM1_USER, "mout_sclk_dsim1_user",
			mout_sclk_dsim1_user_p, MUX_SEL_DISP1, 28, 1),
	MUX(CLK_MOUT_SCLK_DSIM0_USER, "mout_sclk_dsim0_user",
			mout_sclk_dsim0_user_p, MUX_SEL_DISP1, 24, 1),
	MUX(CLK_MOUT_SCLK_DSD_USER, "mout_sclk_dsd_user", mout_sclk_dsd_user_p,
			MUX_SEL_DISP1, 20, 1),
	MUX(CLK_MOUT_SCLK_DECON_TV_ECLK_USER, "mout_sclk_decon_tv_eclk_user",
			mout_sclk_decon_tv_eclk_user_p, MUX_SEL_DISP1, 16, 1),
	MUX(CLK_MOUT_SCLK_DECON_VCLK_USER, "mout_sclk_decon_vclk_user",
			mout_sclk_decon_vclk_user_p, MUX_SEL_DISP1, 12, 1),
	MUX(CLK_MOUT_SCLK_DECON_ECLK_USER, "mout_sclk_decon_eclk_user",
			mout_sclk_decon_eclk_user_p, MUX_SEL_DISP1, 8, 1),
	MUX(CLK_MOUT_SCLK_DECON_TV_VCLK_USER, "mout_sclk_decon_tv_vclk_user",
			mout_sclk_decon_tv_vlkc_user_p, MUX_SEL_DISP1, 4, 1),
	MUX(CLK_MOUT_ACLK_DISP_333_USER, "mout_aclk_disp_333_user",
			mout_aclk_disp_333_user_p, MUX_SEL_DISP1, 0, 1),

	/* MUX_SEL_DISP2 */
	MUX(CLK_MOUT_PHYCLK_MIPIDPHY1_BITCLKDIV8_USER,
			"mout_phyclk_mipidphy1_bitclkdiv8_user",
			mout_phyclk_mipidphy1_bitclkdiv8_user_p, MUX_SEL_DISP2,
			20, 1),
	MUX(CLK_MOUT_PHYCLK_MIPIDPHY1_RXCLKESC0_USER,
			"mout_phyclk_mipidphy1_rxclkesc0_user",
			mout_phyclk_mipidphy1_rxclkesc0_user_p, MUX_SEL_DISP2,
			16, 1),
	MUX(CLK_MOUT_PHYCLK_MIPIDPHY0_BITCLKDIV8_USER,
			"mout_phyclk_mipidphy0_bitclkdiv8_user",
			mout_phyclk_mipidphy0_bitclkdiv8_user_p, MUX_SEL_DISP2,
			12, 1),
	MUX(CLK_MOUT_PHYCLK_MIPIDPHY0_RXCLKESC0_USER,
			"mout_phyclk_mipidphy0_rxclkesc0_user",
			mout_phyclk_mipidphy0_rxclkesc0_user_p, MUX_SEL_DISP2,
			8, 1),
	MUX(CLK_MOUT_PHYCLK_HDMIPHY_TMDS_CLKO_USER,
			"mout_phyclk_hdmiphy_tmds_clko_user",
			mout_phyclk_hdmiphy_tmds_clko_user_p, MUX_SEL_DISP2,
			4, 1),
	MUX(CLK_MOUT_PHYCLK_HDMIPHY_PIXEL_CLKO_USER,
			"mout_phyclk_hdmiphy_pixel_clko_user",
			mout_phyclk_hdmiphy_pixel_clko_user_p, MUX_SEL_DISP2,
			0, 1),

	/* MUX_SEL_DISP3 */
	MUX(CLK_MOUT_SCLK_DSIM0, "mout_sclk_dsim0", mout_sclk_dsim0_p,
			MUX_SEL_DISP3, 12, 1),
	MUX(CLK_MOUT_SCLK_DECON_TV_ECLK, "mout_sclk_decon_tv_eclk",
			mout_sclk_decon_tv_eclk_p, MUX_SEL_DISP3, 8, 1),
	MUX(CLK_MOUT_SCLK_DECON_VCLK, "mout_sclk_decon_vclk",
			mout_sclk_decon_vclk_p, MUX_SEL_DISP3, 4, 1),
	MUX(CLK_MOUT_SCLK_DECON_ECLK, "mout_sclk_decon_eclk",
			mout_sclk_decon_eclk_p, MUX_SEL_DISP3, 0, 1),

	/* MUX_SEL_DISP4 */
	MUX(CLK_MOUT_SCLK_DSIM1_B_DISP, "mout_sclk_dsim1_b_disp",
			mout_sclk_dsim1_b_disp_p, MUX_SEL_DISP4, 16, 1),
	MUX(CLK_MOUT_SCLK_DSIM1_A_DISP, "mout_sclk_dsim1_a_disp",
			mout_sclk_dsim0_p, MUX_SEL_DISP4, 12, 1),
	MUX(CLK_MOUT_SCLK_DECON_TV_VCLK_C_DISP,
			"mout_sclk_decon_tv_vclk_c_disp",
			mout_sclk_decon_tv_vclk_c_disp_p, MUX_SEL_DISP4, 8, 1),
	MUX(CLK_MOUT_SCLK_DECON_TV_VCLK_B_DISP,
			"mout_sclk_decon_tv_vclk_b_disp",
			mout_sclk_decon_tv_vclk_b_disp_p, MUX_SEL_DISP4, 4, 1),
	MUX(CLK_MOUT_SCLK_DECON_TV_VCLK_A_DISP,
			"mout_sclk_decon_tv_vclk_a_disp",
			mout_sclk_decon_vclk_p, MUX_SEL_DISP4, 0, 1),
};

static struct samsung_div_clock disp_div_clks[] __initdata = {
	/* DIV_DISP */
	DIV(CLK_DIV_SCLK_DSIM1_DISP, "div_sclk_dsim1_disp",
			"mout_sclk_dsim1_b_disp", DIV_DISP, 24, 3),
	DIV(CLK_DIV_SCLK_DECON_TV_VCLK_DISP, "div_sclk_decon_tv_vclk_disp",
			"mout_sclk_decon_tv_vclk_c_disp", DIV_DISP, 20, 3),
	DIV(CLK_DIV_SCLK_DSIM0_DISP, "div_sclk_dsim0_disp", "mout_sclk_dsim0",
			DIV_DISP, 16, 3),
	DIV(CLK_DIV_SCLK_DECON_TV_ECLK_DISP, "div_sclk_decon_tv_eclk_disp",
			"mout_sclk_decon_tv_eclk", DIV_DISP, 12, 3),
	DIV(CLK_DIV_SCLK_DECON_VCLK_DISP, "div_sclk_decon_vclk_disp",
			"mout_sclk_decon_vclk", DIV_DISP, 8, 3),
	DIV(CLK_DIV_SCLK_DECON_ECLK_DISP, "div_sclk_decon_eclk_disp",
			"mout_sclk_decon_eclk", DIV_DISP, 4, 3),
	DIV(CLK_DIV_PCLK_DISP, "div_pclk_disp", "mout_aclk_disp_333_user",
			DIV_DISP, 0, 2),
};

static struct samsung_gate_clock disp_gate_clks[] __initdata = {
	/* ENABLE_ACLK_DISP0 */
	GATE(CLK_ACLK_DECON_TV, "aclk_decon_tv", "mout_aclk_disp_333_user",
			ENABLE_ACLK_DISP0, 2, 0, 0),
	GATE(CLK_ACLK_DECON, "aclk_decon", "mout_aclk_disp_333_user",
			ENABLE_ACLK_DISP0, 0, 0, 0),

	/* ENABLE_ACLK_DISP1 */
	GATE(CLK_ACLK_SMMU_TV1X, "aclk_smmu_tv1x", "mout_aclk_disp_333_user",
			ENABLE_ACLK_DISP1, 25, 0, 0),
	GATE(CLK_ACLK_SMMU_TV0X, "aclk_smmu_tv0x", "mout_aclk_disp_333_user",
			ENABLE_ACLK_DISP1, 24, 0, 0),
	GATE(CLK_ACLK_SMMU_DECON1X, "aclk_smmu_decon1x",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 23, 0, 0),
	GATE(CLK_ACLK_SMMU_DECON0X, "aclk_smmu_decon0x",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 22, 0, 0),
	GATE(CLK_ACLK_BTS_DECON_TV_M3, "aclk_bts_decon_tv_m3",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 21, 0, 0),
	GATE(CLK_ACLK_BTS_DECON_TV_M2, "aclk_bts_decon_tv_m2",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 20, 0, 0),
	GATE(CLK_ACLK_BTS_DECON_TV_M1, "aclk_bts_decon_tv_m1",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 19, 0, 0),
	GATE(CLK_ACLK_BTS_DECON_TV_M0, "aclk-bts_decon_tv_m0",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 18, 0, 0),
	GATE(CLK_ACLK_BTS_DECON_NM4, "aclk_bts_decon_nm4",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 17, 0, 0),
	GATE(CLK_ACLK_BTS_DECON_NM3, "aclk_bts_decon_nm3",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 16, 0, 0),
	GATE(CLK_ACLK_BTS_DECON_NM2, "aclk_bts_decon_nm2",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 15, 0, 0),
	GATE(CLK_ACLK_BTS_DECON_NM1, "aclk_bts_decon_nm1",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 14, 0, 0),
	GATE(CLK_ACLK_BTS_DECON_NM0, "aclk_bts_decon_nm0",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 13, 0, 0),
	GATE(CLK_ACLK_AHB2APB_DISPSFR2P, "aclk_ahb2apb_dispsfr2p",
			"div_pclk_disp", ENABLE_ACLK_DISP1,
			12, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AHB2APB_DISPSFR1P, "aclk_ahb2apb_dispsfr1p",
			"div_pclk_disp", ENABLE_ACLK_DISP1,
			11, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AHB2APB_DISPSFR0P, "aclk_ahb2apb_dispsfr0p",
			"div_pclk_disp", ENABLE_ACLK_DISP1,
			10, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_AHB_DISPH, "aclk_ahb_disph", "div_pclk_disp",
			ENABLE_ACLK_DISP1, 8, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_XIU_TV1X, "aclk_xiu_tv1x", "mout_aclk_disp_333_user",
			ENABLE_ACLK_DISP1, 7, 0, 0),
	GATE(CLK_ACLK_XIU_TV0X, "aclk_xiu_tv0x", "mout_aclk_disp_333_user",
			ENABLE_ACLK_DISP1, 6, 0, 0),
	GATE(CLK_ACLK_XIU_DECON1X, "aclk_xiu_decon1x",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 5, 0, 0),
	GATE(CLK_ACLK_XIU_DECON0X, "aclk_xiu_decon0x",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 4, 0, 0),
	GATE(CLK_ACLK_XIU_DISP1X, "aclk_xiu_disp1x", "mout_aclk_disp_333_user",
			ENABLE_ACLK_DISP1, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_XIU_DISPNP_100, "aclk_xiu_dispnp_100", "div_pclk_disp",
			ENABLE_ACLK_DISP1, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DISP1ND_333, "aclk_disp1nd_333",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1, 1,
			CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ACLK_DISP0ND_333, "aclk_disp0nd_333",
			"mout_aclk_disp_333_user", ENABLE_ACLK_DISP1,
			0, CLK_IGNORE_UNUSED, 0),

	/* ENABLE_PCLK_DISP */
	GATE(CLK_PCLK_SMMU_TV1X, "pclk_smmu_tv1x", "div_pclk_disp",
			ENABLE_PCLK_DISP, 23, 0, 0),
	GATE(CLK_PCLK_SMMU_TV0X, "pclk_smmu_tv0x", "div_pclk_disp",
			ENABLE_PCLK_DISP, 22, 0, 0),
	GATE(CLK_PCLK_SMMU_DECON1X, "pclk_smmu_decon1x", "div_pclk_disp",
			ENABLE_PCLK_DISP, 21, 0, 0),
	GATE(CLK_PCLK_SMMU_DECON0X, "pclk_smmu_decon0x", "div_pclk_disp",
			ENABLE_PCLK_DISP, 20, 0, 0),
	GATE(CLK_PCLK_BTS_DECON_TV_M3, "pclk_bts_decon_tv_m3", "div_pclk_disp",
			ENABLE_PCLK_DISP, 19, 0, 0),
	GATE(CLK_PCLK_BTS_DECON_TV_M2, "pclk_bts_decon_tv_m2", "div_pclk_disp",
			ENABLE_PCLK_DISP, 18, 0, 0),
	GATE(CLK_PCLK_BTS_DECON_TV_M1, "pclk_bts_decon_tv_m1", "div_pclk_disp",
			ENABLE_PCLK_DISP, 17, 0, 0),
	GATE(CLK_PCLK_BTS_DECON_TV_M0, "pclk_bts_decon_tv_m0", "div_pclk_disp",
			ENABLE_PCLK_DISP, 16, 0, 0),
	GATE(CLK_PCLK_BTS_DECONM4, "pclk_bts_deconm4", "div_pclk_disp",
			ENABLE_PCLK_DISP, 15, 0, 0),
	GATE(CLK_PCLK_BTS_DECONM3, "pclk_bts_deconm3", "div_pclk_disp",
			ENABLE_PCLK_DISP, 14, 0, 0),
	GATE(CLK_PCLK_BTS_DECONM2, "pclk_bts_deconm2", "div_pclk_disp",
			ENABLE_PCLK_DISP, 13, 0, 0),
	GATE(CLK_PCLK_BTS_DECONM1, "pclk_bts_deconm1", "div_pclk_disp",
			ENABLE_PCLK_DISP, 12, 0, 0),
	GATE(CLK_PCLK_BTS_DECONM0, "pclk_bts_deconm0", "div_pclk_disp",
			ENABLE_PCLK_DISP, 11, 0, 0),
	GATE(CLK_PCLK_MIC1, "pclk_mic1", "div_pclk_disp",
			ENABLE_PCLK_DISP, 10, 0, 0),
	GATE(CLK_PCLK_PMU_DISP, "pclk_pmu_disp", "div_pclk_disp",
			ENABLE_PCLK_DISP, 9, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_SYSREG_DISP, "pclk_sysreg_disp", "div_pclk_disp",
			ENABLE_PCLK_DISP, 8, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PCLK_HDMIPHY, "pclk_hdmiphy", "div_pclk_disp",
			ENABLE_PCLK_DISP, 7, 0, 0),
	GATE(CLK_PCLK_HDMI, "pclk_hdmi", "div_pclk_disp",
			ENABLE_PCLK_DISP, 6, 0, 0),
	GATE(CLK_PCLK_MIC0, "pclk_mic0", "div_pclk_disp",
			ENABLE_PCLK_DISP, 5, 0, 0),
	GATE(CLK_PCLK_DSIM1, "pclk_dsim1", "div_pclk_disp",
			ENABLE_PCLK_DISP, 3, 0, 0),
	GATE(CLK_PCLK_DSIM0, "pclk_dsim0", "div_pclk_disp",
			ENABLE_PCLK_DISP, 2, 0, 0),
	GATE(CLK_PCLK_DECON_TV, "pclk_decon_tv", "div_pclk_disp",
			ENABLE_PCLK_DISP, 1, 0, 0),

	/* ENABLE_SCLK_DISP */
	GATE(CLK_PHYCLK_MIPIDPHY1_BITCLKDIV8, "phyclk_mipidphy1_bitclkdiv8",
			"mout_phyclk_mipidphy1_bitclkdiv8_user",
			ENABLE_SCLK_DISP, 26, 0, 0),
	GATE(CLK_PHYCLK_MIPIDPHY1_RXCLKESC0, "phyclk_mipidphy1_rxclkesc0",
			"mout_phyclk_mipidphy1_rxclkesc0_user",
			ENABLE_SCLK_DISP, 25, 0, 0),
	GATE(CLK_SCLK_RGB_TV_VCLK_TO_DSIM1, "sclk_rgb_tv_vclk_to_dsim1",
			"sclk_rgb_tv_vclk", ENABLE_SCLK_DISP, 24, 0, 0),
	GATE(CLK_SCLK_RGB_TV_VCLK_TO_MIC1, "sclk_rgb_tv_vclk_to_mic1",
			"sclk_rgb_tv_vclk", ENABLE_SCLK_DISP, 23, 0, 0),
	GATE(CLK_SCLK_DSIM1, "sclk_dsim1", "div_sclk_dsim1_disp",
			ENABLE_SCLK_DISP, 22, 0, 0),
	GATE(CLK_SCLK_DECON_TV_VCLK, "sclk_decon_tv_vclk",
			"div_sclk_decon_tv_vclk_disp",
			ENABLE_SCLK_DISP, 21, 0, 0),
	GATE(CLK_PHYCLK_MIPIDPHY0_BITCLKDIV8, "phyclk_mipidphy0_bitclkdiv8",
			"mout_phyclk_mipidphy0_bitclkdiv8_user",
			ENABLE_SCLK_DISP, 15, 0, 0),
	GATE(CLK_PHYCLK_MIPIDPHY0_RXCLKESC0, "phyclk_mipidphy0_rxclkesc0",
			"mout_phyclk_mipidphy0_rxclkesc0_user",
			ENABLE_SCLK_DISP, 14, 0, 0),
	GATE(CLK_PHYCLK_HDMIPHY_TMDS_CLKO, "phyclk_hdmiphy_tmds_clko",
			"mout_phyclk_hdmiphy_tmds_clko_user",
			ENABLE_SCLK_DISP, 13, 0, 0),
	GATE(CLK_PHYCLK_HDMI_PIXEL, "phyclk_hdmi_pixel",
			"sclk_rgb_tv_vclk", ENABLE_SCLK_DISP, 12, 0, 0),
	GATE(CLK_SCLK_RGB_VCLK_TO_SMIES, "sclk_rgb_vclk_to_smies",
			"sclk_rgb_vclk", ENABLE_SCLK_DISP, 11, 0, 0),
	GATE(CLK_SCLK_RGB_VCLK_TO_DSIM0, "sclk_rgb_vclk_to_dsim0",
			"sclk_rgb_vclk", ENABLE_SCLK_DISP, 9, 0, 0),
	GATE(CLK_SCLK_RGB_VCLK_TO_MIC0, "sclk_rgb_vclk_to_mic0",
			"sclk_rgb_vclk", ENABLE_SCLK_DISP, 8, 0, 0),
	GATE(CLK_SCLK_DSD, "sclk_dsd", "mout_sclk_dsd_user",
			ENABLE_SCLK_DISP, 7, 0, 0),
	GATE(CLK_SCLK_HDMI_SPDIF, "sclk_hdmi_spdif", "sclk_hdmi_spdif_disp",
			ENABLE_SCLK_DISP, 6, 0, 0),
	GATE(CLK_SCLK_DSIM0, "sclk_dsim0", "div_sclk_dsim0_disp",
			ENABLE_SCLK_DISP, 5, 0, 0),
	GATE(CLK_SCLK_DECON_TV_ECLK, "sclk_decon_tv_eclk",
			"div_sclk_decon_tv_eclk_disp",
			ENABLE_SCLK_DISP, 4, 0, 0),
	GATE(CLK_SCLK_DECON_VCLK, "sclk_decon_vclk",
			"div_sclk_decon_vclk_disp", ENABLE_SCLK_DISP, 3, 0, 0),
	GATE(CLK_SCLK_DECON_ECLK, "sclk_decon_eclk",
			"div_sclk_decon_eclk_disp", ENABLE_SCLK_DISP, 2, 0, 0),
};

static struct samsung_cmu_info disp_cmu_info __initdata = {
	.pll_clks		= disp_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(disp_pll_clks),
	.mux_clks		= disp_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(disp_mux_clks),
	.div_clks		= disp_div_clks,
	.nr_div_clks		= ARRAY_SIZE(disp_div_clks),
	.gate_clks		= disp_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(disp_gate_clks),
	.fixed_clks		= disp_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(disp_fixed_clks),
	.fixed_factor_clks	= disp_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(disp_fixed_factor_clks),
	.nr_clk_ids		= DISP_NR_CLK,
	.clk_regs		= disp_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(disp_clk_regs),
};

static void __init exynos5433_cmu_disp_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &disp_cmu_info);
}

CLK_OF_DECLARE(exynos5433_cmu_disp, "samsung,exynos5433-cmu-disp",
		exynos5433_cmu_disp_init);
