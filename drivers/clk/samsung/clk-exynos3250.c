// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 * Common Clock Framework support for Exynos3250 SoC.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/exynos3250.h>

#include "clk.h"
#include "clk-cpu.h"
#include "clk-pll.h"

#define SRC_LEFTBUS		0x4200
#define DIV_LEFTBUS		0x4500
#define GATE_IP_LEFTBUS		0x4800
#define SRC_RIGHTBUS		0x8200
#define DIV_RIGHTBUS		0x8500
#define GATE_IP_RIGHTBUS	0x8800
#define GATE_IP_PERIR		0x8960
#define MPLL_LOCK		0xc010
#define MPLL_CON0		0xc110
#define VPLL_LOCK		0xc020
#define VPLL_CON0		0xc120
#define UPLL_LOCK		0xc030
#define UPLL_CON0		0xc130
#define SRC_TOP0		0xc210
#define SRC_TOP1		0xc214
#define SRC_CAM			0xc220
#define SRC_MFC			0xc228
#define SRC_G3D			0xc22c
#define SRC_LCD			0xc234
#define SRC_ISP			0xc238
#define SRC_FSYS		0xc240
#define SRC_PERIL0		0xc250
#define SRC_PERIL1		0xc254
#define SRC_MASK_TOP		0xc310
#define SRC_MASK_CAM		0xc320
#define SRC_MASK_LCD		0xc334
#define SRC_MASK_ISP		0xc338
#define SRC_MASK_FSYS		0xc340
#define SRC_MASK_PERIL0		0xc350
#define SRC_MASK_PERIL1		0xc354
#define DIV_TOP			0xc510
#define DIV_CAM			0xc520
#define DIV_MFC			0xc528
#define DIV_G3D			0xc52c
#define DIV_LCD			0xc534
#define DIV_ISP			0xc538
#define DIV_FSYS0		0xc540
#define DIV_FSYS1		0xc544
#define DIV_FSYS2		0xc548
#define DIV_PERIL0		0xc550
#define DIV_PERIL1		0xc554
#define DIV_PERIL3		0xc55c
#define DIV_PERIL4		0xc560
#define DIV_PERIL5		0xc564
#define DIV_CAM1		0xc568
#define CLKDIV2_RATIO		0xc580
#define GATE_SCLK_CAM		0xc820
#define GATE_SCLK_MFC		0xc828
#define GATE_SCLK_G3D		0xc82c
#define GATE_SCLK_LCD		0xc834
#define GATE_SCLK_ISP_TOP	0xc838
#define GATE_SCLK_FSYS		0xc840
#define GATE_SCLK_PERIL		0xc850
#define GATE_IP_CAM		0xc920
#define GATE_IP_MFC		0xc928
#define GATE_IP_G3D		0xc92c
#define GATE_IP_LCD		0xc934
#define GATE_IP_ISP		0xc938
#define GATE_IP_FSYS		0xc940
#define GATE_IP_PERIL		0xc950
#define GATE_BLOCK		0xc970
#define APLL_LOCK		0x14000
#define APLL_CON0		0x14100
#define SRC_CPU			0x14200
#define DIV_CPU0		0x14500
#define DIV_CPU1		0x14504
#define PWR_CTRL1		0x15020
#define PWR_CTRL2		0x15024

/* Below definitions are used for PWR_CTRL settings */
#define PWR_CTRL1_CORE2_DOWN_RATIO(x)		(((x) & 0x7) << 28)
#define PWR_CTRL1_CORE1_DOWN_RATIO(x)		(((x) & 0x7) << 16)
#define PWR_CTRL1_DIV2_DOWN_EN			(1 << 9)
#define PWR_CTRL1_DIV1_DOWN_EN			(1 << 8)
#define PWR_CTRL1_USE_CORE3_WFE			(1 << 7)
#define PWR_CTRL1_USE_CORE2_WFE			(1 << 6)
#define PWR_CTRL1_USE_CORE1_WFE			(1 << 5)
#define PWR_CTRL1_USE_CORE0_WFE			(1 << 4)
#define PWR_CTRL1_USE_CORE3_WFI			(1 << 3)
#define PWR_CTRL1_USE_CORE2_WFI			(1 << 2)
#define PWR_CTRL1_USE_CORE1_WFI			(1 << 1)
#define PWR_CTRL1_USE_CORE0_WFI			(1 << 0)

/* NOTE: Must be equal to the last clock ID increased by one */
#define CLKS_NR_MAIN				(CLK_SCLK_MMC2 + 1)
#define CLKS_NR_DMC				(CLK_DIV_DMCD + 1)
#define CLKS_NR_ISP				(CLK_SCLK_MPWM_ISP + 1)

static const unsigned long exynos3250_cmu_clk_regs[] __initconst = {
	SRC_LEFTBUS,
	DIV_LEFTBUS,
	GATE_IP_LEFTBUS,
	SRC_RIGHTBUS,
	DIV_RIGHTBUS,
	GATE_IP_RIGHTBUS,
	GATE_IP_PERIR,
	MPLL_LOCK,
	MPLL_CON0,
	VPLL_LOCK,
	VPLL_CON0,
	UPLL_LOCK,
	UPLL_CON0,
	SRC_TOP0,
	SRC_TOP1,
	SRC_CAM,
	SRC_MFC,
	SRC_G3D,
	SRC_LCD,
	SRC_ISP,
	SRC_FSYS,
	SRC_PERIL0,
	SRC_PERIL1,
	SRC_MASK_TOP,
	SRC_MASK_CAM,
	SRC_MASK_LCD,
	SRC_MASK_ISP,
	SRC_MASK_FSYS,
	SRC_MASK_PERIL0,
	SRC_MASK_PERIL1,
	DIV_TOP,
	DIV_CAM,
	DIV_MFC,
	DIV_G3D,
	DIV_LCD,
	DIV_ISP,
	DIV_FSYS0,
	DIV_FSYS1,
	DIV_FSYS2,
	DIV_PERIL0,
	DIV_PERIL1,
	DIV_PERIL3,
	DIV_PERIL4,
	DIV_PERIL5,
	DIV_CAM1,
	CLKDIV2_RATIO,
	GATE_SCLK_CAM,
	GATE_SCLK_MFC,
	GATE_SCLK_G3D,
	GATE_SCLK_LCD,
	GATE_SCLK_ISP_TOP,
	GATE_SCLK_FSYS,
	GATE_SCLK_PERIL,
	GATE_IP_CAM,
	GATE_IP_MFC,
	GATE_IP_G3D,
	GATE_IP_LCD,
	GATE_IP_ISP,
	GATE_IP_FSYS,
	GATE_IP_PERIL,
	GATE_BLOCK,
	APLL_LOCK,
	SRC_CPU,
	DIV_CPU0,
	DIV_CPU1,
	PWR_CTRL1,
	PWR_CTRL2,
};

/* list of all parent clock list */
PNAME(mout_vpllsrc_p)		= { "fin_pll", };

PNAME(mout_apll_p)		= { "fin_pll", "fout_apll", };
PNAME(mout_mpll_p)		= { "fin_pll", "fout_mpll", };
PNAME(mout_vpll_p)		= { "fin_pll", "fout_vpll", };
PNAME(mout_upll_p)		= { "fin_pll", "fout_upll", };

PNAME(mout_mpll_user_p)		= { "fin_pll", "div_mpll_pre", };
PNAME(mout_epll_user_p)		= { "fin_pll", "mout_epll", };
PNAME(mout_core_p)		= { "mout_apll", "mout_mpll_user_c", };
PNAME(mout_hpm_p)		= { "mout_apll", "mout_mpll_user_c", };

PNAME(mout_ebi_p)		= { "div_aclk_200", "div_aclk_160", };
PNAME(mout_ebi_1_p)		= { "mout_ebi", "mout_vpll", };

PNAME(mout_gdl_p)		= { "mout_mpll_user_l", };
PNAME(mout_gdr_p)		= { "mout_mpll_user_r", };

PNAME(mout_aclk_400_mcuisp_sub_p)
				= { "fin_pll", "div_aclk_400_mcuisp", };
PNAME(mout_aclk_266_0_p)	= { "div_mpll_pre", "mout_vpll", };
PNAME(mout_aclk_266_1_p)	= { "mout_epll_user", };
PNAME(mout_aclk_266_p)		= { "mout_aclk_266_0", "mout_aclk_266_1", };
PNAME(mout_aclk_266_sub_p)	= { "fin_pll", "div_aclk_266", };

PNAME(group_div_mpll_pre_p)	= { "div_mpll_pre", };
PNAME(group_epll_vpll_p)	= { "mout_epll_user", "mout_vpll" };
PNAME(group_sclk_p)		= { "xxti", "xusbxti",
				    "none", "none",
				    "none", "none", "div_mpll_pre",
				    "mout_epll_user", "mout_vpll", };
PNAME(group_sclk_audio_p)	= { "audiocdclk", "none",
				    "none", "none",
				    "xxti", "xusbxti",
				    "div_mpll_pre", "mout_epll_user",
				    "mout_vpll", };
PNAME(group_sclk_cam_blk_p)	= { "xxti", "xusbxti",
				    "none", "none", "none",
				    "none", "div_mpll_pre",
				    "mout_epll_user", "mout_vpll",
				    "none", "none", "none",
				    "div_cam_blk_320", };
PNAME(group_sclk_fimd0_p)	= { "xxti", "xusbxti",
				    "m_bitclkhsdiv4_2l", "none",
				    "none", "none", "div_mpll_pre",
				    "mout_epll_user", "mout_vpll",
				    "none", "none", "none",
				    "div_lcd_blk_145", };

PNAME(mout_mfc_p)		= { "mout_mfc_0", "mout_mfc_1" };
PNAME(mout_g3d_p)		= { "mout_g3d_0", "mout_g3d_1" };

static const struct samsung_fixed_factor_clock fixed_factor_clks[] __initconst = {
	FFACTOR(0, "sclk_mpll_1600", "mout_mpll", 1, 1, 0),
	FFACTOR(0, "sclk_mpll_mif", "mout_mpll", 1, 2, 0),
	FFACTOR(0, "sclk_bpll", "fout_bpll", 1, 2, 0),
	FFACTOR(0, "div_cam_blk_320", "sclk_mpll_1600", 1, 5, 0),
	FFACTOR(0, "div_lcd_blk_145", "sclk_mpll_1600", 1, 11, 0),

	/* HACK: fin_pll hardcoded to xusbxti until detection is implemented. */
	FFACTOR(CLK_FIN_PLL, "fin_pll", "xusbxti", 1, 1, 0),
};

static const struct samsung_mux_clock mux_clks[] __initconst = {
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
	MUX(CLK_MOUT_ACLK_200, "mout_aclk_200", group_div_mpll_pre_p,SRC_TOP0, 24, 1),
	MUX(CLK_MOUT_ACLK_160, "mout_aclk_160", group_div_mpll_pre_p, SRC_TOP0, 20, 1),
	MUX(CLK_MOUT_ACLK_100, "mout_aclk_100", group_div_mpll_pre_p, SRC_TOP0, 16, 1),
	MUX(CLK_MOUT_ACLK_266_1, "mout_aclk_266_1", mout_aclk_266_1_p, SRC_TOP0, 14, 1),
	MUX(CLK_MOUT_ACLK_266_0, "mout_aclk_266_0", mout_aclk_266_0_p, SRC_TOP0, 13, 1),
	MUX(CLK_MOUT_ACLK_266, "mout_aclk_266", mout_aclk_266_p, SRC_TOP0, 12, 1),
	MUX(CLK_MOUT_VPLL, "mout_vpll", mout_vpll_p, SRC_TOP0, 8, 1),
	MUX(CLK_MOUT_EPLL_USER, "mout_epll_user", mout_epll_user_p, SRC_TOP0, 4, 1),
	MUX(CLK_MOUT_EBI_1, "mout_ebi_1", mout_ebi_1_p, SRC_TOP0, 0, 1),

	/* SRC_TOP1 */
	MUX(CLK_MOUT_UPLL, "mout_upll", mout_upll_p, SRC_TOP1, 28, 1),
	MUX(CLK_MOUT_ACLK_400_MCUISP_SUB, "mout_aclk_400_mcuisp_sub", mout_aclk_400_mcuisp_sub_p,
		SRC_TOP1, 24, 1),
	MUX(CLK_MOUT_ACLK_266_SUB, "mout_aclk_266_sub", mout_aclk_266_sub_p, SRC_TOP1, 20, 1),
	MUX(CLK_MOUT_MPLL, "mout_mpll", mout_mpll_p, SRC_TOP1, 12, 1),
	MUX(CLK_MOUT_ACLK_400_MCUISP, "mout_aclk_400_mcuisp", group_div_mpll_pre_p, SRC_TOP1, 8, 1),
	MUX(CLK_MOUT_VPLLSRC, "mout_vpllsrc", mout_vpllsrc_p, SRC_TOP1, 0, 1),

	/* SRC_CAM */
	MUX(CLK_MOUT_CAM1, "mout_cam1", group_sclk_p, SRC_CAM, 20, 4),
	MUX(CLK_MOUT_CAM_BLK, "mout_cam_blk", group_sclk_cam_blk_p, SRC_CAM, 0, 4),

	/* SRC_MFC */
	MUX(CLK_MOUT_MFC, "mout_mfc", mout_mfc_p, SRC_MFC, 8, 1),
	MUX(CLK_MOUT_MFC_1, "mout_mfc_1", group_epll_vpll_p, SRC_MFC, 4, 1),
	MUX(CLK_MOUT_MFC_0, "mout_mfc_0", group_div_mpll_pre_p, SRC_MFC, 0, 1),

	/* SRC_G3D */
	MUX(CLK_MOUT_G3D, "mout_g3d", mout_g3d_p, SRC_G3D, 8, 1),
	MUX(CLK_MOUT_G3D_1, "mout_g3d_1", group_epll_vpll_p, SRC_G3D, 4, 1),
	MUX(CLK_MOUT_G3D_0, "mout_g3d_0", group_div_mpll_pre_p, SRC_G3D, 0, 1),

	/* SRC_LCD */
	MUX(CLK_MOUT_MIPI0, "mout_mipi0", group_sclk_p, SRC_LCD, 12, 4),
	MUX(CLK_MOUT_FIMD0, "mout_fimd0", group_sclk_fimd0_p, SRC_LCD, 0, 4),

	/* SRC_ISP */
	MUX(CLK_MOUT_UART_ISP, "mout_uart_isp", group_sclk_p, SRC_ISP, 12, 4),
	MUX(CLK_MOUT_SPI1_ISP, "mout_spi1_isp", group_sclk_p, SRC_ISP, 8, 4),
	MUX(CLK_MOUT_SPI0_ISP, "mout_spi0_isp", group_sclk_p, SRC_ISP, 4, 4),

	/* SRC_FSYS */
	MUX(CLK_MOUT_TSADC, "mout_tsadc", group_sclk_p, SRC_FSYS, 28, 4),
	MUX(CLK_MOUT_MMC2, "mout_mmc2", group_sclk_p, SRC_FSYS, 8, 4),
	MUX(CLK_MOUT_MMC1, "mout_mmc1", group_sclk_p, SRC_FSYS, 4, 4),
	MUX(CLK_MOUT_MMC0, "mout_mmc0", group_sclk_p, SRC_FSYS, 0, 4),

	/* SRC_PERIL0 */
	MUX(CLK_MOUT_UART2, "mout_uart2", group_sclk_p, SRC_PERIL0, 8, 4),
	MUX(CLK_MOUT_UART1, "mout_uart1", group_sclk_p, SRC_PERIL0, 4, 4),
	MUX(CLK_MOUT_UART0, "mout_uart0", group_sclk_p, SRC_PERIL0, 0, 4),

	/* SRC_PERIL1 */
	MUX(CLK_MOUT_SPI1, "mout_spi1", group_sclk_p, SRC_PERIL1, 20, 4),
	MUX(CLK_MOUT_SPI0, "mout_spi0", group_sclk_p, SRC_PERIL1, 16, 4),
	MUX(CLK_MOUT_AUDIO, "mout_audio", group_sclk_audio_p, SRC_PERIL1, 4, 4),

	/* SRC_CPU */
	MUX(CLK_MOUT_MPLL_USER_C, "mout_mpll_user_c", mout_mpll_user_p,
	    SRC_CPU, 24, 1),
	MUX(CLK_MOUT_HPM, "mout_hpm", mout_hpm_p, SRC_CPU, 20, 1),
	MUX_F(CLK_MOUT_CORE, "mout_core", mout_core_p, SRC_CPU, 16, 1,
			CLK_SET_RATE_PARENT, 0),
	MUX_F(CLK_MOUT_APLL, "mout_apll", mout_apll_p, SRC_CPU, 0, 1,
			CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_div_clock div_clks[] __initconst = {
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
	DIV(CLK_DIV_MPLL_PRE, "div_mpll_pre", "sclk_mpll_mif", DIV_TOP, 28, 2),
	DIV(CLK_DIV_ACLK_400_MCUISP, "div_aclk_400_mcuisp",
	    "mout_aclk_400_mcuisp", DIV_TOP, 24, 3),
	DIV(CLK_DIV_EBI, "div_ebi", "mout_ebi_1", DIV_TOP, 16, 3),
	DIV(CLK_DIV_ACLK_200, "div_aclk_200", "mout_aclk_200", DIV_TOP, 12, 3),
	DIV(CLK_DIV_ACLK_160, "div_aclk_160", "mout_aclk_160", DIV_TOP, 8, 3),
	DIV(CLK_DIV_ACLK_100, "div_aclk_100", "mout_aclk_100", DIV_TOP, 4, 4),
	DIV(CLK_DIV_ACLK_266, "div_aclk_266", "mout_aclk_266", DIV_TOP, 0, 3),

	/* DIV_CAM */
	DIV(CLK_DIV_CAM1, "div_cam1", "mout_cam1", DIV_CAM, 20, 4),
	DIV(CLK_DIV_CAM_BLK, "div_cam_blk", "mout_cam_blk", DIV_CAM, 0, 4),

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
	DIV(CLK_DIV_MMC2, "div_mmc2", "mout_mmc2", DIV_FSYS2, 0, 4),

	/* DIV_PERIL0 */
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

	/* DIV_PERIL4 */
	DIV(CLK_DIV_PCM, "div_pcm", "div_audio", DIV_PERIL4, 20, 8),
	DIV(CLK_DIV_AUDIO, "div_audio", "mout_audio", DIV_PERIL4, 16, 4),

	/* DIV_PERIL5 */
	DIV(CLK_DIV_I2S, "div_i2s", "div_audio", DIV_PERIL5, 8, 6),

	/* DIV_CPU0 */
	DIV(CLK_DIV_CORE2, "div_core2", "div_core", DIV_CPU0, 28, 3),
	DIV(CLK_DIV_APLL, "div_apll", "mout_apll", DIV_CPU0, 24, 3),
	DIV(CLK_DIV_PCLK_DBG, "div_pclk_dbg", "div_core2", DIV_CPU0, 20, 3),
	DIV(CLK_DIV_ATB, "div_atb", "div_core2", DIV_CPU0, 16, 3),
	DIV(CLK_DIV_COREM, "div_corem", "div_core2", DIV_CPU0, 4, 3),
	DIV(CLK_DIV_CORE, "div_core", "mout_core", DIV_CPU0, 0, 3),

	/* DIV_CPU1 */
	DIV(CLK_DIV_HPM, "div_hpm", "div_copy", DIV_CPU1, 4, 3),
	DIV(CLK_DIV_COPY, "div_copy", "mout_hpm", DIV_CPU1, 0, 3),
};

static const struct samsung_gate_clock gate_clks[] __initconst = {
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
	GATE(CLK_PPMULEFT, "ppmuleft", "div_aclk_100", GATE_IP_LEFTBUS, 1,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GPIO_LEFT, "gpio_left", "div_aclk_100", GATE_IP_LEFTBUS, 0,
		CLK_IGNORE_UNUSED, 0),

	/* GATE_IP_RIGHTBUS */
	GATE(CLK_ASYNC_ISPMX, "async_ispmx", "div_aclk_100",
		GATE_IP_RIGHTBUS, 9, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNC_FSYSD, "async_fsysd", "div_aclk_100",
		GATE_IP_RIGHTBUS, 5, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNC_LCD0X, "async_lcd0x", "div_aclk_100",
		GATE_IP_RIGHTBUS, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNC_CAMX, "async_camx", "div_aclk_100", GATE_IP_RIGHTBUS, 2,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMURIGHT, "ppmuright", "div_aclk_100", GATE_IP_RIGHTBUS, 1,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GPIO_RIGHT, "gpio_right", "div_aclk_100", GATE_IP_RIGHTBUS, 0,
		CLK_IGNORE_UNUSED, 0),

	/* GATE_IP_PERIR */
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

	/* GATE_SCLK_CAM */
	GATE(CLK_SCLK_JPEG, "sclk_jpeg", "div_cam_blk",
		GATE_SCLK_CAM, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_M2MSCALER, "sclk_m2mscaler", "div_cam_blk",
		GATE_SCLK_CAM, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_GSCALER1, "sclk_gscaler1", "div_cam_blk",
		GATE_SCLK_CAM, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_GSCALER0, "sclk_gscaler0", "div_cam_blk",
		GATE_SCLK_CAM, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_MFC */
	GATE(CLK_SCLK_MFC, "sclk_mfc", "div_mfc",
		GATE_SCLK_MFC, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_G3D */
	GATE(CLK_SCLK_G3D, "sclk_g3d", "div_g3d",
		GATE_SCLK_G3D, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_LCD */
	GATE(CLK_SCLK_MIPIDPHY2L, "sclk_mipidphy2l", "div_mipi0",
		GATE_SCLK_LCD, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MIPI0, "sclk_mipi0", "div_mipi0_pre",
		GATE_SCLK_LCD, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_FIMD0, "sclk_fimd0", "div_fimd0",
		GATE_SCLK_LCD, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_ISP_TOP */
	GATE(CLK_SCLK_CAM1, "sclk_cam1", "div_cam1",
		GATE_SCLK_ISP_TOP, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART_ISP, "sclk_uart_isp", "div_uart_isp",
		GATE_SCLK_ISP_TOP, 3, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI1_ISP, "sclk_spi1_isp", "div_spi1_isp",
		GATE_SCLK_ISP_TOP, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0_ISP, "sclk_spi0_isp", "div_spi0_isp",
		GATE_SCLK_ISP_TOP, 1, CLK_SET_RATE_PARENT, 0),

	/* GATE_SCLK_FSYS */
	GATE(CLK_SCLK_UPLL, "sclk_upll", "mout_upll", GATE_SCLK_FSYS, 10, 0, 0),
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
	GATE(CLK_SCLK_I2S, "sclk_i2s", "div_i2s",
		GATE_SCLK_PERIL, 18, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PCM, "sclk_pcm", "div_pcm",
		GATE_SCLK_PERIL, 16, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI1, "sclk_spi1", "div_spi1_pre",
		GATE_SCLK_PERIL, 7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0, "sclk_spi0", "div_spi0_pre",
		GATE_SCLK_PERIL, 6, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_SCLK_UART2, "sclk_uart2", "div_uart2",
		GATE_SCLK_PERIL, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART1, "sclk_uart1", "div_uart1",
		GATE_SCLK_PERIL, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART0, "sclk_uart0", "div_uart0",
		GATE_SCLK_PERIL, 0, CLK_SET_RATE_PARENT, 0),

	/* GATE_IP_CAM */
	GATE(CLK_QEJPEG, "qejpeg", "div_cam_blk_320", GATE_IP_CAM, 19,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PIXELASYNCM1, "pixelasyncm1", "div_cam_blk_320",
		GATE_IP_CAM, 18, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PIXELASYNCM0, "pixelasyncm0", "div_cam_blk_320",
		GATE_IP_CAM, 17, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMUCAMIF, "ppmucamif", "div_cam_blk_320",
		GATE_IP_CAM, 16, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QEM2MSCALER, "qem2mscaler", "div_cam_blk_320",
		GATE_IP_CAM, 14, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QEGSCALER1, "qegscaler1", "div_cam_blk_320",
		GATE_IP_CAM, 13, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QEGSCALER0, "qegscaler0", "div_cam_blk_320",
		GATE_IP_CAM, 12, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMUJPEG, "smmujpeg", "div_cam_blk_320",
		GATE_IP_CAM, 11, 0, 0),
	GATE(CLK_SMMUM2M2SCALER, "smmum2m2scaler", "div_cam_blk_320",
		GATE_IP_CAM, 9, 0, 0),
	GATE(CLK_SMMUGSCALER1, "smmugscaler1", "div_cam_blk_320",
		GATE_IP_CAM, 8, 0, 0),
	GATE(CLK_SMMUGSCALER0, "smmugscaler0", "div_cam_blk_320",
		GATE_IP_CAM, 7, 0, 0),
	GATE(CLK_JPEG, "jpeg", "div_cam_blk_320", GATE_IP_CAM, 6, 0, 0),
	GATE(CLK_M2MSCALER, "m2mscaler", "div_cam_blk_320",
		GATE_IP_CAM, 2, 0, 0),
	GATE(CLK_GSCALER1, "gscaler1", "div_cam_blk_320", GATE_IP_CAM, 1, 0, 0),
	GATE(CLK_GSCALER0, "gscaler0", "div_cam_blk_320", GATE_IP_CAM, 0, 0, 0),

	/* GATE_IP_MFC */
	GATE(CLK_QEMFC, "qemfc", "div_aclk_200", GATE_IP_MFC, 5,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMUMFC_L, "ppmumfc_l", "div_aclk_200", GATE_IP_MFC, 3,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMUMFC_L, "smmumfc_l", "div_aclk_200", GATE_IP_MFC, 1, 0, 0),
	GATE(CLK_MFC, "mfc", "div_aclk_200", GATE_IP_MFC, 0, 0, 0),

	/* GATE_IP_G3D */
	GATE(CLK_SMMUG3D, "smmug3d", "div_aclk_200", GATE_IP_G3D, 3, 0, 0),
	GATE(CLK_QEG3D, "qeg3d", "div_aclk_200", GATE_IP_G3D, 2,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMUG3D, "ppmug3d", "div_aclk_200", GATE_IP_G3D, 1,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_G3D, "g3d", "div_aclk_200", GATE_IP_G3D, 0, 0, 0),

	/* GATE_IP_LCD */
	GATE(CLK_QE_CH1_LCD, "qe_ch1_lcd", "div_aclk_160", GATE_IP_LCD, 7,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QE_CH0_LCD, "qe_ch0_lcd", "div_aclk_160", GATE_IP_LCD, 6,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMULCD0, "ppmulcd0", "div_aclk_160", GATE_IP_LCD, 5,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMUFIMD0, "smmufimd0", "div_aclk_160", GATE_IP_LCD, 4, 0, 0),
	GATE(CLK_DSIM0, "dsim0", "div_aclk_160", GATE_IP_LCD, 3, 0, 0),
	GATE(CLK_SMIES, "smies", "div_aclk_160", GATE_IP_LCD, 2, 0, 0),
	GATE(CLK_FIMD0, "fimd0", "div_aclk_160", GATE_IP_LCD, 0, 0, 0),

	/* GATE_IP_ISP */
	GATE(CLK_CAM1, "cam1", "mout_aclk_266_sub", GATE_IP_ISP, 5, 0, 0),
	GATE(CLK_UART_ISP_TOP, "uart_isp_top", "mout_aclk_266_sub",
		GATE_IP_ISP, 3, 0, 0),
	GATE(CLK_SPI1_ISP_TOP, "spi1_isp_top", "mout_aclk_266_sub",
		GATE_IP_ISP, 2, 0, 0),
	GATE(CLK_SPI0_ISP_TOP, "spi0_isp_top", "mout_aclk_266_sub",
		GATE_IP_ISP, 1, 0, 0),

	/* GATE_IP_FSYS */
	GATE(CLK_TSADC, "tsadc", "div_aclk_200", GATE_IP_FSYS, 20, 0, 0),
	GATE(CLK_PPMUFILE, "ppmufile", "div_aclk_200", GATE_IP_FSYS, 17,
		CLK_IGNORE_UNUSED, 0),
	GATE(CLK_USBOTG, "usbotg", "div_aclk_200", GATE_IP_FSYS, 13, 0, 0),
	GATE(CLK_USBHOST, "usbhost", "div_aclk_200", GATE_IP_FSYS, 12, 0, 0),
	GATE(CLK_SROMC, "sromc", "div_aclk_200", GATE_IP_FSYS, 11, 0, 0),
	GATE(CLK_SDMMC2, "sdmmc2", "div_aclk_200", GATE_IP_FSYS, 7, 0, 0),
	GATE(CLK_SDMMC1, "sdmmc1", "div_aclk_200", GATE_IP_FSYS, 6, 0, 0),
	GATE(CLK_SDMMC0, "sdmmc0", "div_aclk_200", GATE_IP_FSYS, 5, 0, 0),
	GATE(CLK_PDMA1, "pdma1", "div_aclk_200", GATE_IP_FSYS, 1, 0, 0),
	GATE(CLK_PDMA0, "pdma0", "div_aclk_200", GATE_IP_FSYS, 0, 0, 0),

	/* GATE_IP_PERIL */
	GATE(CLK_PWM, "pwm", "div_aclk_100", GATE_IP_PERIL, 24, 0, 0),
	GATE(CLK_PCM, "pcm", "div_aclk_100", GATE_IP_PERIL, 23, 0, 0),
	GATE(CLK_I2S, "i2s", "div_aclk_100", GATE_IP_PERIL, 21, 0, 0),
	GATE(CLK_SPI1, "spi1", "div_aclk_100", GATE_IP_PERIL, 17, 0, 0),
	GATE(CLK_SPI0, "spi0", "div_aclk_100", GATE_IP_PERIL, 16, 0, 0),
	GATE(CLK_I2C7, "i2c7", "div_aclk_100", GATE_IP_PERIL, 13, 0, 0),
	GATE(CLK_I2C6, "i2c6", "div_aclk_100", GATE_IP_PERIL, 12, 0, 0),
	GATE(CLK_I2C5, "i2c5", "div_aclk_100", GATE_IP_PERIL, 11, 0, 0),
	GATE(CLK_I2C4, "i2c4", "div_aclk_100", GATE_IP_PERIL, 10, 0, 0),
	GATE(CLK_I2C3, "i2c3", "div_aclk_100", GATE_IP_PERIL, 9, 0, 0),
	GATE(CLK_I2C2, "i2c2", "div_aclk_100", GATE_IP_PERIL, 8, 0, 0),
	GATE(CLK_I2C1, "i2c1", "div_aclk_100", GATE_IP_PERIL, 7, 0, 0),
	GATE(CLK_I2C0, "i2c0", "div_aclk_100", GATE_IP_PERIL, 6, 0, 0),
	GATE(CLK_UART2, "uart2", "div_aclk_100", GATE_IP_PERIL, 2, 0, 0),
	GATE(CLK_UART1, "uart1", "div_aclk_100", GATE_IP_PERIL, 1, 0, 0),
	GATE(CLK_UART0, "uart0", "div_aclk_100", GATE_IP_PERIL, 0, 0, 0),
};

/* APLL & MPLL & BPLL & UPLL */
static const struct samsung_pll_rate_table exynos3250_pll_rates[] __initconst = {
	PLL_35XX_RATE(24 * MHZ, 1200000000, 400, 4, 1),
	PLL_35XX_RATE(24 * MHZ, 1100000000, 275, 3, 1),
	PLL_35XX_RATE(24 * MHZ, 1066000000, 533, 6, 1),
	PLL_35XX_RATE(24 * MHZ, 1000000000, 250, 3, 1),
	PLL_35XX_RATE(24 * MHZ,  960000000, 320, 4, 1),
	PLL_35XX_RATE(24 * MHZ,  900000000, 300, 4, 1),
	PLL_35XX_RATE(24 * MHZ,  850000000, 425, 6, 1),
	PLL_35XX_RATE(24 * MHZ,  800000000, 200, 3, 1),
	PLL_35XX_RATE(24 * MHZ,  700000000, 175, 3, 1),
	PLL_35XX_RATE(24 * MHZ,  667000000, 667, 12, 1),
	PLL_35XX_RATE(24 * MHZ,  600000000, 400, 4, 2),
	PLL_35XX_RATE(24 * MHZ,  533000000, 533, 6, 2),
	PLL_35XX_RATE(24 * MHZ,  520000000, 260, 3, 2),
	PLL_35XX_RATE(24 * MHZ,  500000000, 250, 3, 2),
	PLL_35XX_RATE(24 * MHZ,  400000000, 200, 3, 2),
	PLL_35XX_RATE(24 * MHZ,  200000000, 200, 3, 3),
	PLL_35XX_RATE(24 * MHZ,  100000000, 200, 3, 4),
	{ /* sentinel */ }
};

/* EPLL */
static const struct samsung_pll_rate_table exynos3250_epll_rates[] __initconst = {
	PLL_36XX_RATE(24 * MHZ, 800000000, 200, 3, 1,     0),
	PLL_36XX_RATE(24 * MHZ, 288000000,  96, 2, 2,     0),
	PLL_36XX_RATE(24 * MHZ, 192000000, 128, 2, 3,     0),
	PLL_36XX_RATE(24 * MHZ, 144000000,  96, 2, 3,     0),
	PLL_36XX_RATE(24 * MHZ,  96000000, 128, 2, 4,     0),
	PLL_36XX_RATE(24 * MHZ,  84000000, 112, 2, 4,     0),
	PLL_36XX_RATE(24 * MHZ,  80000003, 106, 2, 4, 43691),
	PLL_36XX_RATE(24 * MHZ,  73728000,  98, 2, 4, 19923),
	PLL_36XX_RATE(24 * MHZ,  67737598, 270, 3, 5, 62285),
	PLL_36XX_RATE(24 * MHZ,  65535999, 174, 2, 5, 49982),
	PLL_36XX_RATE(24 * MHZ,  50000000, 200, 3, 5,     0),
	PLL_36XX_RATE(24 * MHZ,  49152002, 131, 2, 5,  4719),
	PLL_36XX_RATE(24 * MHZ,  48000000, 128, 2, 5,     0),
	PLL_36XX_RATE(24 * MHZ,  45158401, 180, 3, 5, 41524),
	{ /* sentinel */ }
};

/* VPLL */
static const struct samsung_pll_rate_table exynos3250_vpll_rates[] __initconst = {
	PLL_36XX_RATE(24 * MHZ, 600000000, 100, 2, 1,     0),
	PLL_36XX_RATE(24 * MHZ, 533000000, 266, 3, 2, 32768),
	PLL_36XX_RATE(24 * MHZ, 519230987, 173, 2, 2,  5046),
	PLL_36XX_RATE(24 * MHZ, 500000000, 250, 3, 2,     0),
	PLL_36XX_RATE(24 * MHZ, 445500000, 148, 2, 2, 32768),
	PLL_36XX_RATE(24 * MHZ, 445055007, 148, 2, 2, 23047),
	PLL_36XX_RATE(24 * MHZ, 400000000, 200, 3, 2,     0),
	PLL_36XX_RATE(24 * MHZ, 371250000, 123, 2, 2, 49152),
	PLL_36XX_RATE(24 * MHZ, 370878997, 185, 3, 2, 28803),
	PLL_36XX_RATE(24 * MHZ, 340000000, 170, 3, 2,     0),
	PLL_36XX_RATE(24 * MHZ, 335000015, 111, 2, 2, 43691),
	PLL_36XX_RATE(24 * MHZ, 333000000, 111, 2, 2,     0),
	PLL_36XX_RATE(24 * MHZ, 330000000, 110, 2, 2,     0),
	PLL_36XX_RATE(24 * MHZ, 320000015, 106, 2, 2, 43691),
	PLL_36XX_RATE(24 * MHZ, 300000000, 100, 2, 2,     0),
	PLL_36XX_RATE(24 * MHZ, 275000000, 275, 3, 3,     0),
	PLL_36XX_RATE(24 * MHZ, 222750000, 148, 2, 3, 32768),
	PLL_36XX_RATE(24 * MHZ, 222528007, 148, 2, 3, 23069),
	PLL_36XX_RATE(24 * MHZ, 160000000, 160, 3, 3,     0),
	PLL_36XX_RATE(24 * MHZ, 148500000,  99, 2, 3,     0),
	PLL_36XX_RATE(24 * MHZ, 148352005,  98, 2, 3, 59070),
	PLL_36XX_RATE(24 * MHZ, 108000000, 144, 2, 4,     0),
	PLL_36XX_RATE(24 * MHZ,  74250000,  99, 2, 4,     0),
	PLL_36XX_RATE(24 * MHZ,  74176002,  98, 2, 4, 59070),
	PLL_36XX_RATE(24 * MHZ,  54054000, 216, 3, 5, 14156),
	PLL_36XX_RATE(24 * MHZ,  54000000, 144, 2, 5,     0),
	{ /* sentinel */ }
};

static const struct samsung_pll_clock exynos3250_plls[] __initconst = {
	PLL(pll_35xx, CLK_FOUT_APLL, "fout_apll", "fin_pll",
		APLL_LOCK, APLL_CON0, exynos3250_pll_rates),
	PLL(pll_35xx, CLK_FOUT_MPLL, "fout_mpll", "fin_pll",
			MPLL_LOCK, MPLL_CON0, exynos3250_pll_rates),
	PLL(pll_36xx, CLK_FOUT_VPLL, "fout_vpll", "fin_pll",
			VPLL_LOCK, VPLL_CON0, exynos3250_vpll_rates),
	PLL(pll_35xx, CLK_FOUT_UPLL, "fout_upll", "fin_pll",
			UPLL_LOCK, UPLL_CON0, exynos3250_pll_rates),
};

#define E3250_CPU_DIV0(apll, pclk_dbg, atb, corem)			\
		(((apll) << 24) | ((pclk_dbg) << 20) | ((atb) << 16) |	\
		((corem) << 4))
#define E3250_CPU_DIV1(hpm, copy)					\
		(((hpm) << 4) | ((copy) << 0))

static const struct exynos_cpuclk_cfg_data e3250_armclk_d[] __initconst = {
	{ 1000000, E3250_CPU_DIV0(1, 7, 4, 1), E3250_CPU_DIV1(7, 7), },
	{  900000, E3250_CPU_DIV0(1, 7, 3, 1), E3250_CPU_DIV1(7, 7), },
	{  800000, E3250_CPU_DIV0(1, 7, 3, 1), E3250_CPU_DIV1(7, 7), },
	{  700000, E3250_CPU_DIV0(1, 7, 3, 1), E3250_CPU_DIV1(7, 7), },
	{  600000, E3250_CPU_DIV0(1, 7, 3, 1), E3250_CPU_DIV1(7, 7), },
	{  500000, E3250_CPU_DIV0(1, 7, 3, 1), E3250_CPU_DIV1(7, 7), },
	{  400000, E3250_CPU_DIV0(1, 7, 3, 1), E3250_CPU_DIV1(7, 7), },
	{  300000, E3250_CPU_DIV0(1, 5, 3, 1), E3250_CPU_DIV1(7, 7), },
	{  200000, E3250_CPU_DIV0(1, 3, 3, 1), E3250_CPU_DIV1(7, 7), },
	{  100000, E3250_CPU_DIV0(1, 1, 1, 1), E3250_CPU_DIV1(7, 7), },
	{  0 },
};

static const struct samsung_cpu_clock exynos3250_cpu_clks[] __initconst = {
	CPU_CLK(CLK_ARM_CLK, "armclk", CLK_MOUT_APLL, CLK_MOUT_MPLL_USER_C,
			CLK_CPU_HAS_DIV1, 0x14200, e3250_armclk_d),
};

static void __init exynos3_core_down_clock(void __iomem *reg_base)
{
	unsigned int tmp;

	/*
	 * Enable arm clock down (in idle) and set arm divider
	 * ratios in WFI/WFE state.
	 */
	tmp = (PWR_CTRL1_CORE2_DOWN_RATIO(7) | PWR_CTRL1_CORE1_DOWN_RATIO(7) |
		PWR_CTRL1_DIV2_DOWN_EN | PWR_CTRL1_DIV1_DOWN_EN |
		PWR_CTRL1_USE_CORE1_WFE | PWR_CTRL1_USE_CORE0_WFE |
		PWR_CTRL1_USE_CORE1_WFI | PWR_CTRL1_USE_CORE0_WFI);
	__raw_writel(tmp, reg_base + PWR_CTRL1);

	/*
	 * Disable the clock up feature on Exynos4x12, in case it was
	 * enabled by bootloader.
	 */
	__raw_writel(0x0, reg_base + PWR_CTRL2);
}

static const struct samsung_cmu_info cmu_info __initconst = {
	.pll_clks		= exynos3250_plls,
	.nr_pll_clks		= ARRAY_SIZE(exynos3250_plls),
	.mux_clks		= mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mux_clks),
	.div_clks		= div_clks,
	.nr_div_clks		= ARRAY_SIZE(div_clks),
	.gate_clks		= gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(gate_clks),
	.fixed_factor_clks	= fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(fixed_factor_clks),
	.cpu_clks		= exynos3250_cpu_clks,
	.nr_cpu_clks		= ARRAY_SIZE(exynos3250_cpu_clks),
	.nr_clk_ids		= CLKS_NR_MAIN,
	.clk_regs		= exynos3250_cmu_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(exynos3250_cmu_clk_regs),
};

static void __init exynos3250_cmu_init(struct device_node *np)
{
	struct samsung_clk_provider *ctx;

	ctx = samsung_cmu_register_one(np, &cmu_info);
	if (!ctx)
		return;

	exynos3_core_down_clock(ctx->reg_base);
}
CLK_OF_DECLARE(exynos3250_cmu, "samsung,exynos3250-cmu", exynos3250_cmu_init);

/*
 * CMU DMC
 */

#define BPLL_LOCK		0x0118
#define BPLL_CON0		0x0218
#define BPLL_CON1		0x021c
#define BPLL_CON2		0x0220
#define SRC_DMC			0x0300
#define DIV_DMC1		0x0504
#define GATE_BUS_DMC0		0x0700
#define GATE_BUS_DMC1		0x0704
#define GATE_BUS_DMC2		0x0708
#define GATE_BUS_DMC3		0x070c
#define GATE_SCLK_DMC		0x0800
#define GATE_IP_DMC0		0x0900
#define GATE_IP_DMC1		0x0904
#define EPLL_LOCK		0x1110
#define EPLL_CON0		0x1114
#define EPLL_CON1		0x1118
#define EPLL_CON2		0x111c
#define SRC_EPLL		0x1120

static const unsigned long exynos3250_cmu_dmc_clk_regs[] __initconst = {
	BPLL_LOCK,
	BPLL_CON0,
	BPLL_CON1,
	BPLL_CON2,
	SRC_DMC,
	DIV_DMC1,
	GATE_BUS_DMC0,
	GATE_BUS_DMC1,
	GATE_BUS_DMC2,
	GATE_BUS_DMC3,
	GATE_SCLK_DMC,
	GATE_IP_DMC0,
	GATE_IP_DMC1,
	EPLL_LOCK,
	EPLL_CON0,
	EPLL_CON1,
	EPLL_CON2,
	SRC_EPLL,
};

PNAME(mout_epll_p)	= { "fin_pll", "fout_epll", };
PNAME(mout_bpll_p)	= { "fin_pll", "fout_bpll", };
PNAME(mout_mpll_mif_p)	= { "fin_pll", "sclk_mpll_mif", };
PNAME(mout_dphy_p)	= { "mout_mpll_mif", "mout_bpll", };

static const struct samsung_mux_clock dmc_mux_clks[] __initconst = {
	/*
	 * NOTE: Following table is sorted by register address in ascending
	 * order and then bitfield shift in descending order, as it is done
	 * in the User's Manual. When adding new entries, please make sure
	 * that the order is preserved, to avoid merge conflicts and make
	 * further work with defined data easier.
	 */

	/* SRC_DMC */
	MUX(CLK_MOUT_MPLL_MIF, "mout_mpll_mif", mout_mpll_mif_p, SRC_DMC, 12, 1),
	MUX(CLK_MOUT_BPLL, "mout_bpll", mout_bpll_p, SRC_DMC, 10, 1),
	MUX(CLK_MOUT_DPHY, "mout_dphy", mout_dphy_p, SRC_DMC, 8, 1),
	MUX(CLK_MOUT_DMC_BUS, "mout_dmc_bus", mout_dphy_p, SRC_DMC,  4, 1),

	/* SRC_EPLL */
	MUX(CLK_MOUT_EPLL, "mout_epll", mout_epll_p, SRC_EPLL, 4, 1),
};

static const struct samsung_div_clock dmc_div_clks[] __initconst = {
	/*
	 * NOTE: Following table is sorted by register address in ascending
	 * order and then bitfield shift in descending order, as it is done
	 * in the User's Manual. When adding new entries, please make sure
	 * that the order is preserved, to avoid merge conflicts and make
	 * further work with defined data easier.
	 */

	/* DIV_DMC1 */
	DIV(CLK_DIV_DMC, "div_dmc", "div_dmc_pre", DIV_DMC1, 27, 3),
	DIV(CLK_DIV_DPHY, "div_dphy", "mout_dphy", DIV_DMC1, 23, 3),
	DIV(CLK_DIV_DMC_PRE, "div_dmc_pre", "mout_dmc_bus", DIV_DMC1, 19, 2),
	DIV(CLK_DIV_DMCP, "div_dmcp", "div_dmcd", DIV_DMC1, 15, 3),
	DIV(CLK_DIV_DMCD, "div_dmcd", "div_dmc", DIV_DMC1, 11, 3),
};

static const struct samsung_pll_clock exynos3250_dmc_plls[] __initconst = {
	PLL(pll_35xx, CLK_FOUT_BPLL, "fout_bpll", "fin_pll",
		BPLL_LOCK, BPLL_CON0, exynos3250_pll_rates),
	PLL(pll_36xx, CLK_FOUT_EPLL, "fout_epll", "fin_pll",
		EPLL_LOCK, EPLL_CON0, exynos3250_epll_rates),
};

static const struct samsung_cmu_info dmc_cmu_info __initconst = {
	.pll_clks		= exynos3250_dmc_plls,
	.nr_pll_clks		= ARRAY_SIZE(exynos3250_dmc_plls),
	.mux_clks		= dmc_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dmc_mux_clks),
	.div_clks		= dmc_div_clks,
	.nr_div_clks		= ARRAY_SIZE(dmc_div_clks),
	.nr_clk_ids		= CLKS_NR_DMC,
	.clk_regs		= exynos3250_cmu_dmc_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(exynos3250_cmu_dmc_clk_regs),
};

static void __init exynos3250_cmu_dmc_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &dmc_cmu_info);
}
CLK_OF_DECLARE(exynos3250_cmu_dmc, "samsung,exynos3250-cmu-dmc",
		exynos3250_cmu_dmc_init);


/*
 * CMU ISP
 */

#define DIV_ISP0		0x300
#define DIV_ISP1		0x304
#define GATE_IP_ISP0		0x800
#define GATE_IP_ISP1		0x804
#define GATE_SCLK_ISP		0x900

static const struct samsung_div_clock isp_div_clks[] __initconst = {
	/*
	 * NOTE: Following table is sorted by register address in ascending
	 * order and then bitfield shift in descending order, as it is done
	 * in the User's Manual. When adding new entries, please make sure
	 * that the order is preserved, to avoid merge conflicts and make
	 * further work with defined data easier.
	 */
	/* DIV_ISP0 */
	DIV(CLK_DIV_ISP1, "div_isp1", "mout_aclk_266_sub", DIV_ISP0, 4, 3),
	DIV(CLK_DIV_ISP0, "div_isp0", "mout_aclk_266_sub", DIV_ISP0, 0, 3),

	/* DIV_ISP1 */
	DIV(CLK_DIV_MCUISP1, "div_mcuisp1", "mout_aclk_400_mcuisp_sub",
		DIV_ISP1, 8, 3),
	DIV(CLK_DIV_MCUISP0, "div_mcuisp0", "mout_aclk_400_mcuisp_sub",
		DIV_ISP1, 4, 3),
	DIV(CLK_DIV_MPWM, "div_mpwm", "div_isp1", DIV_ISP1, 0, 3),
};

static const struct samsung_gate_clock isp_gate_clks[] __initconst = {
	/*
	 * NOTE: Following table is sorted by register address in ascending
	 * order and then bitfield shift in descending order, as it is done
	 * in the User's Manual. When adding new entries, please make sure
	 * that the order is preserved, to avoid merge conflicts and make
	 * further work with defined data easier.
	 */

	/* GATE_IP_ISP0 */
	GATE(CLK_UART_ISP, "uart_isp", "uart_isp_top",
		GATE_IP_ISP0, 31, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_WDT_ISP, "wdt_isp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 30, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PWM_ISP, "pwm_isp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 28, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_I2C1_ISP, "i2c1_isp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 26, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_I2C0_ISP, "i2c0_isp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 25, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_MPWM_ISP, "mpwm_isp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 24, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_MCUCTL_ISP, "mcuctl_isp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 23, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMUISPX, "ppmuispx", "mout_aclk_266_sub",
		GATE_IP_ISP0, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_PPMUISPMX, "ppmuispmx", "mout_aclk_266_sub",
		GATE_IP_ISP0, 20, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QE_LITE1, "qe_lite1", "mout_aclk_266_sub",
		GATE_IP_ISP0, 18, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QE_LITE0, "qe_lite0", "mout_aclk_266_sub",
		GATE_IP_ISP0, 17, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QE_FD, "qe_fd", "mout_aclk_266_sub",
		GATE_IP_ISP0, 16, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QE_DRC, "qe_drc", "mout_aclk_266_sub",
		GATE_IP_ISP0, 15, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QE_ISP, "qe_isp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 14, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_CSIS1, "csis1", "mout_aclk_266_sub",
		GATE_IP_ISP0, 13, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMU_LITE1, "smmu_lite1", "mout_aclk_266_sub",
		GATE_IP_ISP0, 12, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMU_LITE0, "smmu_lite0", "mout_aclk_266_sub",
		GATE_IP_ISP0, 11, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMU_FD, "smmu_fd", "mout_aclk_266_sub",
		GATE_IP_ISP0, 10, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMU_DRC, "smmu_drc", "mout_aclk_266_sub",
		GATE_IP_ISP0, 9, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMU_ISP, "smmu_isp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 8, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GICISP, "gicisp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 7, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_CSIS0, "csis0", "mout_aclk_266_sub",
		GATE_IP_ISP0, 6, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_MCUISP, "mcuisp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 5, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_LITE1, "lite1", "mout_aclk_266_sub",
		GATE_IP_ISP0, 4, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_LITE0, "lite0", "mout_aclk_266_sub",
		GATE_IP_ISP0, 3, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_FD, "fd", "mout_aclk_266_sub",
		GATE_IP_ISP0, 2, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_DRC, "drc", "mout_aclk_266_sub",
		GATE_IP_ISP0, 1, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ISP, "isp", "mout_aclk_266_sub",
		GATE_IP_ISP0, 0, CLK_IGNORE_UNUSED, 0),

	/* GATE_IP_ISP1 */
	GATE(CLK_QE_ISPCX, "qe_ispcx", "uart_isp_top",
		GATE_IP_ISP0, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QE_SCALERP, "qe_scalerp", "uart_isp_top",
		GATE_IP_ISP0, 20, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_QE_SCALERC, "qe_scalerc", "uart_isp_top",
		GATE_IP_ISP0, 19, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMU_SCALERP, "smmu_scalerp", "uart_isp_top",
		GATE_IP_ISP0, 18, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMU_SCALERC, "smmu_scalerc", "uart_isp_top",
		GATE_IP_ISP0, 17, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCALERP, "scalerp", "uart_isp_top",
		GATE_IP_ISP0, 16, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SCALERC, "scalerc", "uart_isp_top",
		GATE_IP_ISP0, 15, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SPI1_ISP, "spi1_isp", "uart_isp_top",
		GATE_IP_ISP0, 13, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SPI0_ISP, "spi0_isp", "uart_isp_top",
		GATE_IP_ISP0, 12, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_SMMU_ISPCX, "smmu_ispcx", "uart_isp_top",
		GATE_IP_ISP0, 4, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_ASYNCAXIM, "asyncaxim", "uart_isp_top",
		GATE_IP_ISP0, 0, CLK_IGNORE_UNUSED, 0),

	/* GATE_SCLK_ISP */
	GATE(CLK_SCLK_MPWM_ISP, "sclk_mpwm_isp", "div_mpwm",
		GATE_SCLK_ISP, 0, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info isp_cmu_info __initconst = {
	.div_clks	= isp_div_clks,
	.nr_div_clks	= ARRAY_SIZE(isp_div_clks),
	.gate_clks	= isp_gate_clks,
	.nr_gate_clks	= ARRAY_SIZE(isp_gate_clks),
	.nr_clk_ids	= CLKS_NR_ISP,
};

static int __init exynos3250_cmu_isp_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	samsung_cmu_register_one(np, &isp_cmu_info);
	return 0;
}

static const struct of_device_id exynos3250_cmu_isp_of_match[] __initconst = {
	{ .compatible = "samsung,exynos3250-cmu-isp", },
	{ /* sentinel */ }
};

static struct platform_driver exynos3250_cmu_isp_driver __initdata = {
	.driver = {
		.name = "exynos3250-cmu-isp",
		.suppress_bind_attrs = true,
		.of_match_table = exynos3250_cmu_isp_of_match,
	},
};

static int __init exynos3250_cmu_platform_init(void)
{
	return platform_driver_probe(&exynos3250_cmu_isp_driver,
					exynos3250_cmu_isp_probe);
}
subsys_initcall(exynos3250_cmu_platform_init);

