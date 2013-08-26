/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for all Exynos4 SoCs.
*/

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk.h"

/* Exynos4 clock controller register offsets */
#define SRC_LEFTBUS		0x4200
#define DIV_LEFTBUS		0x4500
#define GATE_IP_LEFTBUS		0x4800
#define E4X12_GATE_IP_IMAGE	0x4930
#define SRC_RIGHTBUS		0x8200
#define DIV_RIGHTBUS		0x8500
#define GATE_IP_RIGHTBUS	0x8800
#define E4X12_GATE_IP_PERIR	0x8960
#define EPLL_LOCK		0xc010
#define VPLL_LOCK		0xc020
#define EPLL_CON0		0xc110
#define EPLL_CON1		0xc114
#define EPLL_CON2		0xc118
#define VPLL_CON0		0xc120
#define VPLL_CON1		0xc124
#define VPLL_CON2		0xc128
#define SRC_TOP0		0xc210
#define SRC_TOP1		0xc214
#define SRC_CAM			0xc220
#define SRC_TV			0xc224
#define SRC_MFC			0xcc28
#define SRC_G3D			0xc22c
#define E4210_SRC_IMAGE		0xc230
#define SRC_LCD0		0xc234
#define E4210_SRC_LCD1		0xc238
#define E4X12_SRC_ISP		0xc238
#define SRC_MAUDIO		0xc23c
#define SRC_FSYS		0xc240
#define SRC_PERIL0		0xc250
#define SRC_PERIL1		0xc254
#define E4X12_SRC_CAM1		0xc258
#define SRC_MASK_TOP		0xc310
#define SRC_MASK_CAM		0xc320
#define SRC_MASK_TV		0xc324
#define SRC_MASK_LCD0		0xc334
#define E4210_SRC_MASK_LCD1	0xc338
#define E4X12_SRC_MASK_ISP	0xc338
#define SRC_MASK_MAUDIO		0xc33c
#define SRC_MASK_FSYS		0xc340
#define SRC_MASK_PERIL0		0xc350
#define SRC_MASK_PERIL1		0xc354
#define DIV_TOP			0xc510
#define DIV_CAM			0xc520
#define DIV_TV			0xc524
#define DIV_MFC			0xc528
#define DIV_G3D			0xc52c
#define DIV_IMAGE		0xc530
#define DIV_LCD0		0xc534
#define E4210_DIV_LCD1		0xc538
#define E4X12_DIV_ISP		0xc538
#define DIV_MAUDIO		0xc53c
#define DIV_FSYS0		0xc540
#define DIV_FSYS1		0xc544
#define DIV_FSYS2		0xc548
#define DIV_FSYS3		0xc54c
#define DIV_PERIL0		0xc550
#define DIV_PERIL1		0xc554
#define DIV_PERIL2		0xc558
#define DIV_PERIL3		0xc55c
#define DIV_PERIL4		0xc560
#define DIV_PERIL5		0xc564
#define E4X12_DIV_CAM1		0xc568
#define GATE_SCLK_CAM		0xc820
#define GATE_IP_CAM		0xc920
#define GATE_IP_TV		0xc924
#define GATE_IP_MFC		0xc928
#define GATE_IP_G3D		0xc92c
#define E4210_GATE_IP_IMAGE	0xc930
#define GATE_IP_LCD0		0xc934
#define E4210_GATE_IP_LCD1	0xc938
#define E4X12_GATE_IP_ISP	0xc938
#define E4X12_GATE_IP_MAUDIO	0xc93c
#define GATE_IP_FSYS		0xc940
#define GATE_IP_GPS		0xc94c
#define GATE_IP_PERIL		0xc950
#define E4210_GATE_IP_PERIR	0xc960
#define GATE_BLOCK		0xc970
#define E4X12_MPLL_LOCK		0x10008
#define E4X12_MPLL_CON0		0x10108
#define SRC_DMC			0x10200
#define SRC_MASK_DMC		0x10300
#define DIV_DMC0		0x10500
#define DIV_DMC1		0x10504
#define GATE_IP_DMC		0x10900
#define APLL_LOCK		0x14000
#define APLL_CON0		0x14100
#define E4210_MPLL_CON0		0x14108
#define SRC_CPU			0x14200
#define DIV_CPU0		0x14500
#define DIV_CPU1		0x14504
#define GATE_SCLK_CPU		0x14800
#define GATE_IP_CPU		0x14900
#define E4X12_DIV_ISP0		0x18300
#define E4X12_DIV_ISP1		0x18304
#define E4X12_GATE_ISP0		0x18800
#define E4X12_GATE_ISP1		0x18804

/* the exynos4 soc type */
enum exynos4_soc {
	EXYNOS4210,
	EXYNOS4X12,
};

/* list of PLLs to be registered */
enum exynos4_plls {
	apll, mpll, epll, vpll,
	nr_plls			/* number of PLLs */
};

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
enum exynos4_clks {
	none,

	/* core clocks */
	xxti, xusbxti, fin_pll, fout_apll, fout_mpll, fout_epll, fout_vpll,
	sclk_apll, sclk_mpll, sclk_epll, sclk_vpll, arm_clk, aclk200, aclk100,
	aclk160, aclk133, mout_mpll_user_t, mout_mpll_user_c, mout_core,
	mout_apll, /* 20 */

	/* gate for special clocks (sclk) */
	sclk_fimc0 = 128, sclk_fimc1, sclk_fimc2, sclk_fimc3, sclk_cam0,
	sclk_cam1, sclk_csis0, sclk_csis1, sclk_hdmi, sclk_mixer, sclk_dac,
	sclk_pixel, sclk_fimd0, sclk_mdnie0, sclk_mdnie_pwm0, sclk_mipi0,
	sclk_audio0, sclk_mmc0, sclk_mmc1, sclk_mmc2, sclk_mmc3, sclk_mmc4,
	sclk_sata, sclk_uart0, sclk_uart1, sclk_uart2, sclk_uart3, sclk_uart4,
	sclk_audio1, sclk_audio2, sclk_spdif, sclk_spi0, sclk_spi1, sclk_spi2,
	sclk_slimbus, sclk_fimd1, sclk_mipi1, sclk_pcm1, sclk_pcm2, sclk_i2s1,
	sclk_i2s2, sclk_mipihsi, sclk_mfc, sclk_pcm0, sclk_g3d, sclk_pwm_isp,
	sclk_spi0_isp, sclk_spi1_isp, sclk_uart_isp, sclk_fimg2d,

	/* gate clocks */
	fimc0 = 256, fimc1, fimc2, fimc3, csis0, csis1, jpeg, smmu_fimc0,
	smmu_fimc1, smmu_fimc2, smmu_fimc3, smmu_jpeg, vp, mixer, tvenc, hdmi,
	smmu_tv, mfc, smmu_mfcl, smmu_mfcr, g3d, g2d, rotator, mdma, smmu_g2d,
	smmu_rotator, smmu_mdma, fimd0, mie0, mdnie0, dsim0, smmu_fimd0, fimd1,
	mie1, dsim1, smmu_fimd1, pdma0, pdma1, pcie_phy, sata_phy, tsi, sdmmc0,
	sdmmc1, sdmmc2, sdmmc3, sdmmc4, sata, sromc, usb_host, usb_device, pcie,
	onenand, nfcon, smmu_pcie, gps, smmu_gps, uart0, uart1, uart2, uart3,
	uart4, i2c0, i2c1, i2c2, i2c3, i2c4, i2c5, i2c6, i2c7, i2c_hdmi, tsadc,
	spi0, spi1, spi2, i2s1, i2s2, pcm0, i2s0, pcm1, pcm2, pwm, slimbus,
	spdif, ac97, modemif, chipid, sysreg, hdmi_cec, mct, wdt, rtc, keyif,
	audss, mipi_hsi, mdma2, pixelasyncm0, pixelasyncm1, fimc_lite0,
	fimc_lite1, ppmuispx, ppmuispmx, fimc_isp, fimc_drc, fimc_fd, mcuisp,
	gicisp, smmu_isp, smmu_drc, smmu_fd, smmu_lite0, smmu_lite1, mcuctl_isp,
	mpwm_isp, i2c0_isp, i2c1_isp, mtcadc_isp, pwm_isp, wdt_isp, uart_isp,
	asyncaxim, smmu_ispcx, spi0_isp, spi1_isp, pwm_isp_sclk, spi0_isp_sclk,
	spi1_isp_sclk, uart_isp_sclk, tmu_apbif,

	/* mux clocks */
	mout_fimc0 = 384, mout_fimc1, mout_fimc2, mout_fimc3, mout_cam0,
	mout_cam1, mout_csis0, mout_csis1, mout_g3d0, mout_g3d1, mout_g3d,
	aclk400_mcuisp,

	/* div clocks */
	div_isp0 = 450, div_isp1, div_mcuisp0, div_mcuisp1, div_aclk200,
	div_aclk400_mcuisp,

	nr_clks,
};

/*
 * list of controller registers to be saved and restored during a
 * suspend/resume cycle.
 */
static unsigned long exynos4210_clk_save[] __initdata = {
	E4210_SRC_IMAGE,
	E4210_SRC_LCD1,
	E4210_SRC_MASK_LCD1,
	E4210_DIV_LCD1,
	E4210_GATE_IP_IMAGE,
	E4210_GATE_IP_LCD1,
	E4210_GATE_IP_PERIR,
	E4210_MPLL_CON0,
};

static unsigned long exynos4x12_clk_save[] __initdata = {
	E4X12_GATE_IP_IMAGE,
	E4X12_GATE_IP_PERIR,
	E4X12_SRC_CAM1,
	E4X12_DIV_ISP,
	E4X12_DIV_CAM1,
	E4X12_MPLL_CON0,
};

static unsigned long exynos4_clk_regs[] __initdata = {
	SRC_LEFTBUS,
	DIV_LEFTBUS,
	GATE_IP_LEFTBUS,
	SRC_RIGHTBUS,
	DIV_RIGHTBUS,
	GATE_IP_RIGHTBUS,
	EPLL_CON0,
	EPLL_CON1,
	EPLL_CON2,
	VPLL_CON0,
	VPLL_CON1,
	VPLL_CON2,
	SRC_TOP0,
	SRC_TOP1,
	SRC_CAM,
	SRC_TV,
	SRC_MFC,
	SRC_G3D,
	SRC_LCD0,
	SRC_MAUDIO,
	SRC_FSYS,
	SRC_PERIL0,
	SRC_PERIL1,
	SRC_MASK_TOP,
	SRC_MASK_CAM,
	SRC_MASK_TV,
	SRC_MASK_LCD0,
	SRC_MASK_MAUDIO,
	SRC_MASK_FSYS,
	SRC_MASK_PERIL0,
	SRC_MASK_PERIL1,
	DIV_TOP,
	DIV_CAM,
	DIV_TV,
	DIV_MFC,
	DIV_G3D,
	DIV_IMAGE,
	DIV_LCD0,
	DIV_MAUDIO,
	DIV_FSYS0,
	DIV_FSYS1,
	DIV_FSYS2,
	DIV_FSYS3,
	DIV_PERIL0,
	DIV_PERIL1,
	DIV_PERIL2,
	DIV_PERIL3,
	DIV_PERIL4,
	DIV_PERIL5,
	GATE_SCLK_CAM,
	GATE_IP_CAM,
	GATE_IP_TV,
	GATE_IP_MFC,
	GATE_IP_G3D,
	GATE_IP_LCD0,
	GATE_IP_FSYS,
	GATE_IP_GPS,
	GATE_IP_PERIL,
	GATE_BLOCK,
	SRC_MASK_DMC,
	SRC_DMC,
	DIV_DMC0,
	DIV_DMC1,
	GATE_IP_DMC,
	APLL_CON0,
	SRC_CPU,
	DIV_CPU0,
	DIV_CPU1,
	GATE_SCLK_CPU,
	GATE_IP_CPU,
};

/* list of all parent clock list */
PNAME(mout_apll_p)	= { "fin_pll", "fout_apll", };
PNAME(mout_mpll_p)	= { "fin_pll", "fout_mpll", };
PNAME(mout_epll_p)	= { "fin_pll", "fout_epll", };
PNAME(mout_vpllsrc_p)	= { "fin_pll", "sclk_hdmi24m", };
PNAME(mout_vpll_p)	= { "fin_pll", "fout_vpll", };
PNAME(sclk_evpll_p)	= { "sclk_epll", "sclk_vpll", };
PNAME(mout_mfc_p)	= { "mout_mfc0", "mout_mfc1", };
PNAME(mout_g3d_p)	= { "mout_g3d0", "mout_g3d1", };
PNAME(mout_g2d_p)	= { "mout_g2d0", "mout_g2d1", };
PNAME(mout_hdmi_p)	= { "sclk_pixel", "sclk_hdmiphy", };
PNAME(mout_jpeg_p)	= { "mout_jpeg0", "mout_jpeg1", };
PNAME(mout_spdif_p)	= { "sclk_audio0", "sclk_audio1", "sclk_audio2",
				"spdif_extclk", };
PNAME(mout_onenand_p)  = {"aclk133", "aclk160", };
PNAME(mout_onenand1_p) = {"mout_onenand", "sclk_vpll", };

/* Exynos 4210-specific parent groups */
PNAME(sclk_vpll_p4210)	= { "mout_vpllsrc", "fout_vpll", };
PNAME(mout_core_p4210)	= { "mout_apll", "sclk_mpll", };
PNAME(sclk_ampll_p4210)	= { "sclk_mpll", "sclk_apll", };
PNAME(group1_p4210)	= { "xxti", "xusbxti", "sclk_hdmi24m",
				"sclk_usbphy0", "none",	"sclk_hdmiphy",
				"sclk_mpll", "sclk_epll", "sclk_vpll", };
PNAME(mout_audio0_p4210) = { "cdclk0", "none", "sclk_hdmi24m",
				"sclk_usbphy0", "xxti", "xusbxti", "sclk_mpll",
				"sclk_epll", "sclk_vpll" };
PNAME(mout_audio1_p4210) = { "cdclk1", "none", "sclk_hdmi24m",
				"sclk_usbphy0", "xxti", "xusbxti", "sclk_mpll",
				"sclk_epll", "sclk_vpll", };
PNAME(mout_audio2_p4210) = { "cdclk2", "none", "sclk_hdmi24m",
				"sclk_usbphy0", "xxti", "xusbxti", "sclk_mpll",
				"sclk_epll", "sclk_vpll", };
PNAME(mout_mixer_p4210)	= { "sclk_dac", "sclk_hdmi", };
PNAME(mout_dac_p4210)	= { "sclk_vpll", "sclk_hdmiphy", };

/* Exynos 4x12-specific parent groups */
PNAME(mout_mpll_user_p4x12) = { "fin_pll", "sclk_mpll", };
PNAME(mout_core_p4x12)	= { "mout_apll", "mout_mpll_user_c", };
PNAME(sclk_ampll_p4x12)	= { "mout_mpll_user_t", "sclk_apll", };
PNAME(group1_p4x12)	= { "xxti", "xusbxti", "sclk_hdmi24m", "sclk_usbphy0",
				"none",	"sclk_hdmiphy", "mout_mpll_user_t",
				"sclk_epll", "sclk_vpll", };
PNAME(mout_audio0_p4x12) = { "cdclk0", "none", "sclk_hdmi24m",
				"sclk_usbphy0", "xxti", "xusbxti",
				"mout_mpll_user_t", "sclk_epll", "sclk_vpll" };
PNAME(mout_audio1_p4x12) = { "cdclk1", "none", "sclk_hdmi24m",
				"sclk_usbphy0", "xxti", "xusbxti",
				"mout_mpll_user_t", "sclk_epll", "sclk_vpll", };
PNAME(mout_audio2_p4x12) = { "cdclk2", "none", "sclk_hdmi24m",
				"sclk_usbphy0", "xxti", "xusbxti",
				"mout_mpll_user_t", "sclk_epll", "sclk_vpll", };
PNAME(aclk_p4412)	= { "mout_mpll_user_t", "sclk_apll", };
PNAME(mout_user_aclk400_mcuisp_p4x12) = {"fin_pll", "div_aclk400_mcuisp", };
PNAME(mout_user_aclk200_p4x12) = {"fin_pll", "div_aclk200", };
PNAME(mout_user_aclk266_gps_p4x12) = {"fin_pll", "div_aclk266_gps", };

/* fixed rate clocks generated outside the soc */
static struct samsung_fixed_rate_clock exynos4_fixed_rate_ext_clks[] __initdata = {
	FRATE(xxti, "xxti", NULL, CLK_IS_ROOT, 0),
	FRATE(xusbxti, "xusbxti", NULL, CLK_IS_ROOT, 0),
};

/* fixed rate clocks generated inside the soc */
static struct samsung_fixed_rate_clock exynos4_fixed_rate_clks[] __initdata = {
	FRATE(none, "sclk_hdmi24m", NULL, CLK_IS_ROOT, 24000000),
	FRATE(none, "sclk_hdmiphy", NULL, CLK_IS_ROOT, 27000000),
	FRATE(none, "sclk_usbphy0", NULL, CLK_IS_ROOT, 48000000),
};

static struct samsung_fixed_rate_clock exynos4210_fixed_rate_clks[] __initdata = {
	FRATE(none, "sclk_usbphy1", NULL, CLK_IS_ROOT, 48000000),
};

/* list of mux clocks supported in all exynos4 soc's */
static struct samsung_mux_clock exynos4_mux_clks[] __initdata = {
	MUX_FA(mout_apll, "mout_apll", mout_apll_p, SRC_CPU, 0, 1,
			CLK_SET_RATE_PARENT, 0, "mout_apll"),
	MUX(none, "mout_hdmi", mout_hdmi_p, SRC_TV, 0, 1),
	MUX(none, "mout_mfc1", sclk_evpll_p, SRC_MFC, 4, 1),
	MUX(none, "mout_mfc", mout_mfc_p, SRC_MFC, 8, 1),
	MUX_F(mout_g3d1, "mout_g3d1", sclk_evpll_p, SRC_G3D, 4, 1,
			CLK_SET_RATE_PARENT, 0),
	MUX_F(mout_g3d, "mout_g3d", mout_g3d_p, SRC_G3D, 8, 1,
			CLK_SET_RATE_PARENT, 0),
	MUX(none, "mout_spdif", mout_spdif_p, SRC_PERIL1, 8, 2),
	MUX(none, "mout_onenand1", mout_onenand1_p, SRC_TOP0, 0, 1),
	MUX(sclk_epll, "sclk_epll", mout_epll_p, SRC_TOP0, 4, 1),
	MUX(none, "mout_onenand", mout_onenand_p, SRC_TOP0, 28, 1),
};

/* list of mux clocks supported in exynos4210 soc */
static struct samsung_mux_clock exynos4210_mux_clks[] __initdata = {
	MUX(none, "mout_aclk200", sclk_ampll_p4210, SRC_TOP0, 12, 1),
	MUX(none, "mout_aclk100", sclk_ampll_p4210, SRC_TOP0, 16, 1),
	MUX(none, "mout_aclk160", sclk_ampll_p4210, SRC_TOP0, 20, 1),
	MUX(none, "mout_aclk133", sclk_ampll_p4210, SRC_TOP0, 24, 1),
	MUX(none, "mout_vpllsrc", mout_vpllsrc_p, SRC_TOP1, 0, 1),
	MUX(none, "mout_mixer", mout_mixer_p4210, SRC_TV, 4, 1),
	MUX(none, "mout_dac", mout_dac_p4210, SRC_TV, 8, 1),
	MUX(none, "mout_g2d0", sclk_ampll_p4210, E4210_SRC_IMAGE, 0, 1),
	MUX(none, "mout_g2d1", sclk_evpll_p, E4210_SRC_IMAGE, 4, 1),
	MUX(none, "mout_g2d", mout_g2d_p, E4210_SRC_IMAGE, 8, 1),
	MUX(none, "mout_fimd1", group1_p4210, E4210_SRC_LCD1, 0, 4),
	MUX(none, "mout_mipi1", group1_p4210, E4210_SRC_LCD1, 12, 4),
	MUX(sclk_mpll, "sclk_mpll", mout_mpll_p, SRC_CPU, 8, 1),
	MUX(mout_core, "mout_core", mout_core_p4210, SRC_CPU, 16, 1),
	MUX(sclk_vpll, "sclk_vpll", sclk_vpll_p4210, SRC_TOP0, 8, 1),
	MUX(mout_fimc0, "mout_fimc0", group1_p4210, SRC_CAM, 0, 4),
	MUX(mout_fimc1, "mout_fimc1", group1_p4210, SRC_CAM, 4, 4),
	MUX(mout_fimc2, "mout_fimc2", group1_p4210, SRC_CAM, 8, 4),
	MUX(mout_fimc3, "mout_fimc3", group1_p4210, SRC_CAM, 12, 4),
	MUX(mout_cam0, "mout_cam0", group1_p4210, SRC_CAM, 16, 4),
	MUX(mout_cam1, "mout_cam1", group1_p4210, SRC_CAM, 20, 4),
	MUX(mout_csis0, "mout_csis0", group1_p4210, SRC_CAM, 24, 4),
	MUX(mout_csis1, "mout_csis1", group1_p4210, SRC_CAM, 28, 4),
	MUX(none, "mout_mfc0", sclk_ampll_p4210, SRC_MFC, 0, 1),
	MUX_F(mout_g3d0, "mout_g3d0", sclk_ampll_p4210, SRC_G3D, 0, 1,
			CLK_SET_RATE_PARENT, 0),
	MUX(none, "mout_fimd0", group1_p4210, SRC_LCD0, 0, 4),
	MUX(none, "mout_mipi0", group1_p4210, SRC_LCD0, 12, 4),
	MUX(none, "mout_audio0", mout_audio0_p4210, SRC_MAUDIO, 0, 4),
	MUX(none, "mout_mmc0", group1_p4210, SRC_FSYS, 0, 4),
	MUX(none, "mout_mmc1", group1_p4210, SRC_FSYS, 4, 4),
	MUX(none, "mout_mmc2", group1_p4210, SRC_FSYS, 8, 4),
	MUX(none, "mout_mmc3", group1_p4210, SRC_FSYS, 12, 4),
	MUX(none, "mout_mmc4", group1_p4210, SRC_FSYS, 16, 4),
	MUX(none, "mout_sata", sclk_ampll_p4210, SRC_FSYS, 24, 1),
	MUX(none, "mout_uart0", group1_p4210, SRC_PERIL0, 0, 4),
	MUX(none, "mout_uart1", group1_p4210, SRC_PERIL0, 4, 4),
	MUX(none, "mout_uart2", group1_p4210, SRC_PERIL0, 8, 4),
	MUX(none, "mout_uart3", group1_p4210, SRC_PERIL0, 12, 4),
	MUX(none, "mout_uart4", group1_p4210, SRC_PERIL0, 16, 4),
	MUX(none, "mout_audio1", mout_audio1_p4210, SRC_PERIL1, 0, 4),
	MUX(none, "mout_audio2", mout_audio2_p4210, SRC_PERIL1, 4, 4),
	MUX(none, "mout_spi0", group1_p4210, SRC_PERIL1, 16, 4),
	MUX(none, "mout_spi1", group1_p4210, SRC_PERIL1, 20, 4),
	MUX(none, "mout_spi2", group1_p4210, SRC_PERIL1, 24, 4),
};

/* list of mux clocks supported in exynos4x12 soc */
static struct samsung_mux_clock exynos4x12_mux_clks[] __initdata = {
	MUX(mout_mpll_user_c, "mout_mpll_user_c", mout_mpll_user_p4x12,
			SRC_CPU, 24, 1),
	MUX(none, "mout_aclk266_gps", aclk_p4412, SRC_TOP1, 4, 1),
	MUX(none, "mout_aclk400_mcuisp", aclk_p4412, SRC_TOP1, 8, 1),
	MUX(mout_mpll_user_t, "mout_mpll_user_t", mout_mpll_user_p4x12,
			SRC_TOP1, 12, 1),
	MUX(none, "mout_user_aclk266_gps", mout_user_aclk266_gps_p4x12,
			SRC_TOP1, 16, 1),
	MUX(aclk200, "aclk200", mout_user_aclk200_p4x12, SRC_TOP1, 20, 1),
	MUX(aclk400_mcuisp, "aclk400_mcuisp", mout_user_aclk400_mcuisp_p4x12,
			SRC_TOP1, 24, 1),
	MUX(none, "mout_aclk200", aclk_p4412, SRC_TOP0, 12, 1),
	MUX(none, "mout_aclk100", aclk_p4412, SRC_TOP0, 16, 1),
	MUX(none, "mout_aclk160", aclk_p4412, SRC_TOP0, 20, 1),
	MUX(none, "mout_aclk133", aclk_p4412, SRC_TOP0, 24, 1),
	MUX(none, "mout_mdnie0", group1_p4x12, SRC_LCD0, 4, 4),
	MUX(none, "mout_mdnie_pwm0", group1_p4x12, SRC_LCD0, 8, 4),
	MUX(none, "mout_sata", sclk_ampll_p4x12, SRC_FSYS, 24, 1),
	MUX(none, "mout_jpeg0", sclk_ampll_p4x12, E4X12_SRC_CAM1, 0, 1),
	MUX(none, "mout_jpeg1", sclk_evpll_p, E4X12_SRC_CAM1, 4, 1),
	MUX(none, "mout_jpeg", mout_jpeg_p, E4X12_SRC_CAM1, 8, 1),
	MUX(sclk_mpll, "sclk_mpll", mout_mpll_p, SRC_DMC, 12, 1),
	MUX(sclk_vpll, "sclk_vpll", mout_vpll_p, SRC_TOP0, 8, 1),
	MUX(mout_core, "mout_core", mout_core_p4x12, SRC_CPU, 16, 1),
	MUX(mout_fimc0, "mout_fimc0", group1_p4x12, SRC_CAM, 0, 4),
	MUX(mout_fimc1, "mout_fimc1", group1_p4x12, SRC_CAM, 4, 4),
	MUX(mout_fimc2, "mout_fimc2", group1_p4x12, SRC_CAM, 8, 4),
	MUX(mout_fimc3, "mout_fimc3", group1_p4x12, SRC_CAM, 12, 4),
	MUX(mout_cam0, "mout_cam0", group1_p4x12, SRC_CAM, 16, 4),
	MUX(mout_cam1, "mout_cam1", group1_p4x12, SRC_CAM, 20, 4),
	MUX(mout_csis0, "mout_csis0", group1_p4x12, SRC_CAM, 24, 4),
	MUX(mout_csis1, "mout_csis1", group1_p4x12, SRC_CAM, 28, 4),
	MUX(none, "mout_mfc0", sclk_ampll_p4x12, SRC_MFC, 0, 1),
	MUX_F(mout_g3d0, "mout_g3d0", sclk_ampll_p4x12, SRC_G3D, 0, 1,
			CLK_SET_RATE_PARENT, 0),
	MUX(none, "mout_fimd0", group1_p4x12, SRC_LCD0, 0, 4),
	MUX(none, "mout_mipi0", group1_p4x12, SRC_LCD0, 12, 4),
	MUX(none, "mout_audio0", mout_audio0_p4x12, SRC_MAUDIO, 0, 4),
	MUX(none, "mout_mmc0", group1_p4x12, SRC_FSYS, 0, 4),
	MUX(none, "mout_mmc1", group1_p4x12, SRC_FSYS, 4, 4),
	MUX(none, "mout_mmc2", group1_p4x12, SRC_FSYS, 8, 4),
	MUX(none, "mout_mmc3", group1_p4x12, SRC_FSYS, 12, 4),
	MUX(none, "mout_mmc4", group1_p4x12, SRC_FSYS, 16, 4),
	MUX(none, "mout_mipihsi", aclk_p4412, SRC_FSYS, 24, 1),
	MUX(none, "mout_uart0", group1_p4x12, SRC_PERIL0, 0, 4),
	MUX(none, "mout_uart1", group1_p4x12, SRC_PERIL0, 4, 4),
	MUX(none, "mout_uart2", group1_p4x12, SRC_PERIL0, 8, 4),
	MUX(none, "mout_uart3", group1_p4x12, SRC_PERIL0, 12, 4),
	MUX(none, "mout_uart4", group1_p4x12, SRC_PERIL0, 16, 4),
	MUX(none, "mout_audio1", mout_audio1_p4x12, SRC_PERIL1, 0, 4),
	MUX(none, "mout_audio2", mout_audio2_p4x12, SRC_PERIL1, 4, 4),
	MUX(none, "mout_spi0", group1_p4x12, SRC_PERIL1, 16, 4),
	MUX(none, "mout_spi1", group1_p4x12, SRC_PERIL1, 20, 4),
	MUX(none, "mout_spi2", group1_p4x12, SRC_PERIL1, 24, 4),
	MUX(none, "mout_pwm_isp", group1_p4x12, E4X12_SRC_ISP, 0, 4),
	MUX(none, "mout_spi0_isp", group1_p4x12, E4X12_SRC_ISP, 4, 4),
	MUX(none, "mout_spi1_isp", group1_p4x12, E4X12_SRC_ISP, 8, 4),
	MUX(none, "mout_uart_isp", group1_p4x12, E4X12_SRC_ISP, 12, 4),
	MUX(none, "mout_g2d0", sclk_ampll_p4210, SRC_DMC, 20, 1),
	MUX(none, "mout_g2d1", sclk_evpll_p, SRC_DMC, 24, 1),
	MUX(none, "mout_g2d", mout_g2d_p, SRC_DMC, 28, 1),
};

/* list of divider clocks supported in all exynos4 soc's */
static struct samsung_div_clock exynos4_div_clks[] __initdata = {
	DIV(none, "div_core", "mout_core", DIV_CPU0, 0, 3),
	DIV(none, "div_core2", "div_core", DIV_CPU0, 28, 3),
	DIV(none, "div_fimc0", "mout_fimc0", DIV_CAM, 0, 4),
	DIV(none, "div_fimc1", "mout_fimc1", DIV_CAM, 4, 4),
	DIV(none, "div_fimc2", "mout_fimc2", DIV_CAM, 8, 4),
	DIV(none, "div_fimc3", "mout_fimc3", DIV_CAM, 12, 4),
	DIV(none, "div_cam0", "mout_cam0", DIV_CAM, 16, 4),
	DIV(none, "div_cam1", "mout_cam1", DIV_CAM, 20, 4),
	DIV(none, "div_csis0", "mout_csis0", DIV_CAM, 24, 4),
	DIV(none, "div_csis1", "mout_csis1", DIV_CAM, 28, 4),
	DIV(sclk_mfc, "sclk_mfc", "mout_mfc", DIV_MFC, 0, 4),
	DIV_F(none, "div_g3d", "mout_g3d", DIV_G3D, 0, 4,
			CLK_SET_RATE_PARENT, 0),
	DIV(none, "div_fimd0", "mout_fimd0", DIV_LCD0, 0, 4),
	DIV(none, "div_mipi0", "mout_mipi0", DIV_LCD0, 16, 4),
	DIV(none, "div_audio0", "mout_audio0", DIV_MAUDIO, 0, 4),
	DIV(sclk_pcm0, "sclk_pcm0", "sclk_audio0", DIV_MAUDIO, 4, 8),
	DIV(none, "div_mmc0", "mout_mmc0", DIV_FSYS1, 0, 4),
	DIV(none, "div_mmc1", "mout_mmc1", DIV_FSYS1, 16, 4),
	DIV(none, "div_mmc2", "mout_mmc2", DIV_FSYS2, 0, 4),
	DIV(none, "div_mmc3", "mout_mmc3", DIV_FSYS2, 16, 4),
	DIV(sclk_pixel, "sclk_pixel", "sclk_vpll", DIV_TV, 0, 4),
	DIV(aclk100, "aclk100", "mout_aclk100", DIV_TOP, 4, 4),
	DIV(aclk160, "aclk160", "mout_aclk160", DIV_TOP, 8, 3),
	DIV(aclk133, "aclk133", "mout_aclk133", DIV_TOP, 12, 3),
	DIV(none, "div_onenand", "mout_onenand1", DIV_TOP, 16, 3),
	DIV(sclk_slimbus, "sclk_slimbus", "sclk_epll", DIV_PERIL3, 4, 4),
	DIV(sclk_pcm1, "sclk_pcm1", "sclk_audio1", DIV_PERIL4, 4, 8),
	DIV(sclk_pcm2, "sclk_pcm2", "sclk_audio2", DIV_PERIL4, 20, 8),
	DIV(sclk_i2s1, "sclk_i2s1", "sclk_audio1", DIV_PERIL5, 0, 6),
	DIV(sclk_i2s2, "sclk_i2s2", "sclk_audio2", DIV_PERIL5, 8, 6),
	DIV(none, "div_mmc4", "mout_mmc4", DIV_FSYS3, 0, 4),
	DIV(none, "div_mmc_pre4", "div_mmc4", DIV_FSYS3, 8, 8),
	DIV(none, "div_uart0", "mout_uart0", DIV_PERIL0, 0, 4),
	DIV(none, "div_uart1", "mout_uart1", DIV_PERIL0, 4, 4),
	DIV(none, "div_uart2", "mout_uart2", DIV_PERIL0, 8, 4),
	DIV(none, "div_uart3", "mout_uart3", DIV_PERIL0, 12, 4),
	DIV(none, "div_uart4", "mout_uart4", DIV_PERIL0, 16, 4),
	DIV(none, "div_spi0", "mout_spi0", DIV_PERIL1, 0, 4),
	DIV(none, "div_spi_pre0", "div_spi0", DIV_PERIL1, 8, 8),
	DIV(none, "div_spi1", "mout_spi1", DIV_PERIL1, 16, 4),
	DIV(none, "div_spi_pre1", "div_spi1", DIV_PERIL1, 24, 8),
	DIV(none, "div_spi2", "mout_spi2", DIV_PERIL2, 0, 4),
	DIV(none, "div_spi_pre2", "div_spi2", DIV_PERIL2, 8, 8),
	DIV(none, "div_audio1", "mout_audio1", DIV_PERIL4, 0, 4),
	DIV(none, "div_audio2", "mout_audio2", DIV_PERIL4, 16, 4),
	DIV(arm_clk, "arm_clk", "div_core2", DIV_CPU0, 28, 3),
	DIV(sclk_apll, "sclk_apll", "mout_apll", DIV_CPU0, 24, 3),
	DIV_F(none, "div_mipi_pre0", "div_mipi0", DIV_LCD0, 20, 4,
			CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_mmc_pre0", "div_mmc0", DIV_FSYS1, 8, 8,
			CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_mmc_pre1", "div_mmc1", DIV_FSYS1, 24, 8,
			CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_mmc_pre2", "div_mmc2", DIV_FSYS2, 8, 8,
			CLK_SET_RATE_PARENT, 0),
	DIV_F(none, "div_mmc_pre3", "div_mmc3", DIV_FSYS2, 24, 8,
			CLK_SET_RATE_PARENT, 0),
};

/* list of divider clocks supported in exynos4210 soc */
static struct samsung_div_clock exynos4210_div_clks[] __initdata = {
	DIV(aclk200, "aclk200", "mout_aclk200", DIV_TOP, 0, 3),
	DIV(sclk_fimg2d, "sclk_fimg2d", "mout_g2d", DIV_IMAGE, 0, 4),
	DIV(none, "div_fimd1", "mout_fimd1", E4210_DIV_LCD1, 0, 4),
	DIV(none, "div_mipi1", "mout_mipi1", E4210_DIV_LCD1, 16, 4),
	DIV(none, "div_sata", "mout_sata", DIV_FSYS0, 20, 4),
	DIV_F(none, "div_mipi_pre1", "div_mipi1", E4210_DIV_LCD1, 20, 4,
			CLK_SET_RATE_PARENT, 0),
};

/* list of divider clocks supported in exynos4x12 soc */
static struct samsung_div_clock exynos4x12_div_clks[] __initdata = {
	DIV(none, "div_mdnie0", "mout_mdnie0", DIV_LCD0, 4, 4),
	DIV(none, "div_mdnie_pwm0", "mout_mdnie_pwm0", DIV_LCD0, 8, 4),
	DIV(none, "div_mdnie_pwm_pre0", "div_mdnie_pwm0", DIV_LCD0, 12, 4),
	DIV(none, "div_mipihsi", "mout_mipihsi", DIV_FSYS0, 20, 4),
	DIV(none, "div_jpeg", "mout_jpeg", E4X12_DIV_CAM1, 0, 4),
	DIV(div_aclk200, "div_aclk200", "mout_aclk200", DIV_TOP, 0, 3),
	DIV(none, "div_aclk266_gps", "mout_aclk266_gps", DIV_TOP, 20, 3),
	DIV(div_aclk400_mcuisp, "div_aclk400_mcuisp", "mout_aclk400_mcuisp",
						DIV_TOP, 24, 3),
	DIV(none, "div_pwm_isp", "mout_pwm_isp", E4X12_DIV_ISP, 0, 4),
	DIV(none, "div_spi0_isp", "mout_spi0_isp", E4X12_DIV_ISP, 4, 4),
	DIV(none, "div_spi0_isp_pre", "div_spi0_isp", E4X12_DIV_ISP, 8, 8),
	DIV(none, "div_spi1_isp", "mout_spi1_isp", E4X12_DIV_ISP, 16, 4),
	DIV(none, "div_spi1_isp_pre", "div_spi1_isp", E4X12_DIV_ISP, 20, 8),
	DIV(none, "div_uart_isp", "mout_uart_isp", E4X12_DIV_ISP, 28, 4),
	DIV(div_isp0, "div_isp0", "aclk200", E4X12_DIV_ISP0, 0, 3),
	DIV(div_isp1, "div_isp1", "aclk200", E4X12_DIV_ISP0, 4, 3),
	DIV(none, "div_mpwm", "div_isp1", E4X12_DIV_ISP1, 0, 3),
	DIV(div_mcuisp0, "div_mcuisp0", "aclk400_mcuisp", E4X12_DIV_ISP1, 4, 3),
	DIV(div_mcuisp1, "div_mcuisp1", "div_mcuisp0", E4X12_DIV_ISP1, 8, 3),
	DIV(sclk_fimg2d, "sclk_fimg2d", "mout_g2d", DIV_DMC1, 0, 4),
};

/* list of gate clocks supported in all exynos4 soc's */
static struct samsung_gate_clock exynos4_gate_clks[] __initdata = {
	/*
	 * After all Exynos4 based platforms are migrated to use device tree,
	 * the device name and clock alias names specified below for some
	 * of the clocks can be removed.
	 */
	GATE(sclk_hdmi, "sclk_hdmi", "mout_hdmi", SRC_MASK_TV, 0, 0, 0),
	GATE(sclk_spdif, "sclk_spdif", "mout_spdif", SRC_MASK_PERIL1, 8, 0, 0),
	GATE(jpeg, "jpeg", "aclk160", GATE_IP_CAM, 6, 0, 0),
	GATE(mie0, "mie0", "aclk160", GATE_IP_LCD0, 1, 0, 0),
	GATE(dsim0, "dsim0", "aclk160", GATE_IP_LCD0, 3, 0, 0),
	GATE(fimd1, "fimd1", "aclk160", E4210_GATE_IP_LCD1, 0, 0, 0),
	GATE(mie1, "mie1", "aclk160", E4210_GATE_IP_LCD1, 1, 0, 0),
	GATE(dsim1, "dsim1", "aclk160", E4210_GATE_IP_LCD1, 3, 0, 0),
	GATE(smmu_fimd1, "smmu_fimd1", "aclk160", E4210_GATE_IP_LCD1, 4, 0, 0),
	GATE(tsi, "tsi", "aclk133", GATE_IP_FSYS, 4, 0, 0),
	GATE(sromc, "sromc", "aclk133", GATE_IP_FSYS, 11, 0, 0),
	GATE(sclk_g3d, "sclk_g3d", "div_g3d", GATE_IP_G3D, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(usb_device, "usb_device", "aclk133", GATE_IP_FSYS, 13, 0, 0),
	GATE(onenand, "onenand", "aclk133", GATE_IP_FSYS, 15, 0, 0),
	GATE(nfcon, "nfcon", "aclk133", GATE_IP_FSYS, 16, 0, 0),
	GATE(gps, "gps", "aclk133", GATE_IP_GPS, 0, 0, 0),
	GATE(smmu_gps, "smmu_gps", "aclk133", GATE_IP_GPS, 1, 0, 0),
	GATE(slimbus, "slimbus", "aclk100", GATE_IP_PERIL, 25, 0, 0),
	GATE(sclk_cam0, "sclk_cam0", "div_cam0", GATE_SCLK_CAM, 4,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_cam1, "sclk_cam1", "div_cam1", GATE_SCLK_CAM, 5,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mipi0, "sclk_mipi0", "div_mipi_pre0",
			SRC_MASK_LCD0, 12, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_audio0, "sclk_audio0", "div_audio0", SRC_MASK_MAUDIO, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_audio1, "sclk_audio1", "div_audio1", SRC_MASK_PERIL1, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(vp, "vp", "aclk160", GATE_IP_TV, 0, 0, 0),
	GATE(mixer, "mixer", "aclk160", GATE_IP_TV, 1, 0, 0),
	GATE(hdmi, "hdmi", "aclk160", GATE_IP_TV, 3, 0, 0),
	GATE(pwm, "pwm", "aclk100", GATE_IP_PERIL, 24, 0, 0),
	GATE(sdmmc4, "sdmmc4", "aclk133", GATE_IP_FSYS, 9, 0, 0),
	GATE(usb_host, "usb_host", "aclk133", GATE_IP_FSYS, 12, 0, 0),
	GATE(sclk_fimc0, "sclk_fimc0", "div_fimc0", SRC_MASK_CAM, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_fimc1, "sclk_fimc1", "div_fimc1", SRC_MASK_CAM, 4,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_fimc2, "sclk_fimc2", "div_fimc2", SRC_MASK_CAM, 8,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_fimc3, "sclk_fimc3", "div_fimc3", SRC_MASK_CAM, 12,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_csis0, "sclk_csis0", "div_csis0", SRC_MASK_CAM, 24,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_csis1, "sclk_csis1", "div_csis1", SRC_MASK_CAM, 28,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_fimd0, "sclk_fimd0", "div_fimd0", SRC_MASK_LCD0, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc0, "sclk_mmc0", "div_mmc_pre0", SRC_MASK_FSYS, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc1, "sclk_mmc1", "div_mmc_pre1", SRC_MASK_FSYS, 4,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc2, "sclk_mmc2", "div_mmc_pre2", SRC_MASK_FSYS, 8,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc3, "sclk_mmc3", "div_mmc_pre3", SRC_MASK_FSYS, 12,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mmc4, "sclk_mmc4", "div_mmc_pre4", SRC_MASK_FSYS, 16,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart0, "uclk0", "div_uart0", SRC_MASK_PERIL0, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart1, "uclk1", "div_uart1", SRC_MASK_PERIL0, 4,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart2, "uclk2", "div_uart2", SRC_MASK_PERIL0, 8,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart3, "uclk3", "div_uart3", SRC_MASK_PERIL0, 12,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart4, "uclk4", "div_uart4", SRC_MASK_PERIL0, 16,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_audio2, "sclk_audio2", "div_audio2", SRC_MASK_PERIL1, 4,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spi0, "sclk_spi0", "div_spi_pre0", SRC_MASK_PERIL1, 16,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spi1, "sclk_spi1", "div_spi_pre1", SRC_MASK_PERIL1, 20,
			CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spi2, "sclk_spi2", "div_spi_pre2", SRC_MASK_PERIL1, 24,
			CLK_SET_RATE_PARENT, 0),
	GATE(fimc0, "fimc0", "aclk160", GATE_IP_CAM, 0,
			0, 0),
	GATE(fimc1, "fimc1", "aclk160", GATE_IP_CAM, 1,
			0, 0),
	GATE(fimc2, "fimc2", "aclk160", GATE_IP_CAM, 2,
			0, 0),
	GATE(fimc3, "fimc3", "aclk160", GATE_IP_CAM, 3,
			0, 0),
	GATE(csis0, "csis0", "aclk160", GATE_IP_CAM, 4,
			0, 0),
	GATE(csis1, "csis1", "aclk160", GATE_IP_CAM, 5,
			0, 0),
	GATE(smmu_fimc0, "smmu_fimc0", "aclk160", GATE_IP_CAM, 7,
			0, 0),
	GATE(smmu_fimc1, "smmu_fimc1", "aclk160", GATE_IP_CAM, 8,
			0, 0),
	GATE(smmu_fimc2, "smmu_fimc2", "aclk160", GATE_IP_CAM, 9,
			0, 0),
	GATE(smmu_fimc3, "smmu_fimc3", "aclk160", GATE_IP_CAM, 10,
			0, 0),
	GATE(smmu_jpeg, "smmu_jpeg", "aclk160", GATE_IP_CAM, 11,
			0, 0),
	GATE(pixelasyncm0, "pxl_async0", "aclk160", GATE_IP_CAM, 17, 0, 0),
	GATE(pixelasyncm1, "pxl_async1", "aclk160", GATE_IP_CAM, 18, 0, 0),
	GATE(smmu_tv, "smmu_tv", "aclk160", GATE_IP_TV, 4,
			0, 0),
	GATE(mfc, "mfc", "aclk100", GATE_IP_MFC, 0, 0, 0),
	GATE(smmu_mfcl, "smmu_mfcl", "aclk100", GATE_IP_MFC, 1,
			0, 0),
	GATE(smmu_mfcr, "smmu_mfcr", "aclk100", GATE_IP_MFC, 2,
			0, 0),
	GATE(fimd0, "fimd0", "aclk160", GATE_IP_LCD0, 0,
			0, 0),
	GATE(smmu_fimd0, "smmu_fimd0", "aclk160", GATE_IP_LCD0, 4,
			0, 0),
	GATE(pdma0, "pdma0", "aclk133", GATE_IP_FSYS, 0,
			0, 0),
	GATE(pdma1, "pdma1", "aclk133", GATE_IP_FSYS, 1,
			0, 0),
	GATE(sdmmc0, "sdmmc0", "aclk133", GATE_IP_FSYS, 5,
			0, 0),
	GATE(sdmmc1, "sdmmc1", "aclk133", GATE_IP_FSYS, 6,
			0, 0),
	GATE(sdmmc2, "sdmmc2", "aclk133", GATE_IP_FSYS, 7,
			0, 0),
	GATE(sdmmc3, "sdmmc3", "aclk133", GATE_IP_FSYS, 8,
			0, 0),
	GATE(uart0, "uart0", "aclk100", GATE_IP_PERIL, 0,
			0, 0),
	GATE(uart1, "uart1", "aclk100", GATE_IP_PERIL, 1,
			0, 0),
	GATE(uart2, "uart2", "aclk100", GATE_IP_PERIL, 2,
			0, 0),
	GATE(uart3, "uart3", "aclk100", GATE_IP_PERIL, 3,
			0, 0),
	GATE(uart4, "uart4", "aclk100", GATE_IP_PERIL, 4,
			0, 0),
	GATE(i2c0, "i2c0", "aclk100", GATE_IP_PERIL, 6,
			0, 0),
	GATE(i2c1, "i2c1", "aclk100", GATE_IP_PERIL, 7,
			0, 0),
	GATE(i2c2, "i2c2", "aclk100", GATE_IP_PERIL, 8,
			0, 0),
	GATE(i2c3, "i2c3", "aclk100", GATE_IP_PERIL, 9,
			0, 0),
	GATE(i2c4, "i2c4", "aclk100", GATE_IP_PERIL, 10,
			0, 0),
	GATE(i2c5, "i2c5", "aclk100", GATE_IP_PERIL, 11,
			0, 0),
	GATE(i2c6, "i2c6", "aclk100", GATE_IP_PERIL, 12,
			0, 0),
	GATE(i2c7, "i2c7", "aclk100", GATE_IP_PERIL, 13,
			0, 0),
	GATE(i2c_hdmi, "i2c-hdmi", "aclk100", GATE_IP_PERIL, 14,
			0, 0),
	GATE(spi0, "spi0", "aclk100", GATE_IP_PERIL, 16,
			0, 0),
	GATE(spi1, "spi1", "aclk100", GATE_IP_PERIL, 17,
			0, 0),
	GATE(spi2, "spi2", "aclk100", GATE_IP_PERIL, 18,
			0, 0),
	GATE(i2s1, "i2s1", "aclk100", GATE_IP_PERIL, 20,
			0, 0),
	GATE(i2s2, "i2s2", "aclk100", GATE_IP_PERIL, 21,
			0, 0),
	GATE(pcm1, "pcm1", "aclk100", GATE_IP_PERIL, 22,
			0, 0),
	GATE(pcm2, "pcm2", "aclk100", GATE_IP_PERIL, 23,
			0, 0),
	GATE(spdif, "spdif", "aclk100", GATE_IP_PERIL, 26,
			0, 0),
	GATE(ac97, "ac97", "aclk100", GATE_IP_PERIL, 27,
			0, 0),
};

/* list of gate clocks supported in exynos4210 soc */
static struct samsung_gate_clock exynos4210_gate_clks[] __initdata = {
	GATE(tvenc, "tvenc", "aclk160", GATE_IP_TV, 2, 0, 0),
	GATE(g2d, "g2d", "aclk200", E4210_GATE_IP_IMAGE, 0, 0, 0),
	GATE(rotator, "rotator", "aclk200", E4210_GATE_IP_IMAGE, 1, 0, 0),
	GATE(mdma, "mdma", "aclk200", E4210_GATE_IP_IMAGE, 2, 0, 0),
	GATE(smmu_g2d, "smmu_g2d", "aclk200", E4210_GATE_IP_IMAGE, 3, 0, 0),
	GATE(smmu_mdma, "smmu_mdma", "aclk200", E4210_GATE_IP_IMAGE, 5, 0, 0),
	GATE(pcie_phy, "pcie_phy", "aclk133", GATE_IP_FSYS, 2, 0, 0),
	GATE(sata_phy, "sata_phy", "aclk133", GATE_IP_FSYS, 3, 0, 0),
	GATE(sata, "sata", "aclk133", GATE_IP_FSYS, 10, 0, 0),
	GATE(pcie, "pcie", "aclk133", GATE_IP_FSYS, 14, 0, 0),
	GATE(smmu_pcie, "smmu_pcie", "aclk133", GATE_IP_FSYS, 18, 0, 0),
	GATE(modemif, "modemif", "aclk100", GATE_IP_PERIL, 28, 0, 0),
	GATE(chipid, "chipid", "aclk100", E4210_GATE_IP_PERIR, 0, 0, 0),
	GATE(sysreg, "sysreg", "aclk100", E4210_GATE_IP_PERIR, 0,
			CLK_IGNORE_UNUSED, 0),
	GATE(hdmi_cec, "hdmi_cec", "aclk100", E4210_GATE_IP_PERIR, 11, 0, 0),
	GATE(smmu_rotator, "smmu_rotator", "aclk200",
			E4210_GATE_IP_IMAGE, 4, 0, 0),
	GATE(sclk_mipi1, "sclk_mipi1", "div_mipi_pre1",
			E4210_SRC_MASK_LCD1, 12, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_sata, "sclk_sata", "div_sata",
			SRC_MASK_FSYS, 24, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mixer, "sclk_mixer", "mout_mixer", SRC_MASK_TV, 4, 0, 0),
	GATE(sclk_dac, "sclk_dac", "mout_dac", SRC_MASK_TV, 8, 0, 0),
	GATE(tsadc, "tsadc", "aclk100", GATE_IP_PERIL, 15,
			0, 0),
	GATE(mct, "mct", "aclk100", E4210_GATE_IP_PERIR, 13,
			0, 0),
	GATE(wdt, "watchdog", "aclk100", E4210_GATE_IP_PERIR, 14,
			0, 0),
	GATE(rtc, "rtc", "aclk100", E4210_GATE_IP_PERIR, 15,
			0, 0),
	GATE(keyif, "keyif", "aclk100", E4210_GATE_IP_PERIR, 16,
			0, 0),
	GATE(sclk_fimd1, "sclk_fimd1", "div_fimd1", E4210_SRC_MASK_LCD1, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(tmu_apbif, "tmu_apbif", "aclk100", E4210_GATE_IP_PERIR, 17, 0, 0),
};

/* list of gate clocks supported in exynos4x12 soc */
static struct samsung_gate_clock exynos4x12_gate_clks[] __initdata = {
	GATE(audss, "audss", "sclk_epll", E4X12_GATE_IP_MAUDIO, 0, 0, 0),
	GATE(mdnie0, "mdnie0", "aclk160", GATE_IP_LCD0, 2, 0, 0),
	GATE(rotator, "rotator", "aclk200", E4X12_GATE_IP_IMAGE, 1, 0, 0),
	GATE(mdma2, "mdma2", "aclk200", E4X12_GATE_IP_IMAGE, 2, 0, 0),
	GATE(smmu_mdma, "smmu_mdma", "aclk200", E4X12_GATE_IP_IMAGE, 5, 0, 0),
	GATE(mipi_hsi, "mipi_hsi", "aclk133", GATE_IP_FSYS, 10, 0, 0),
	GATE(chipid, "chipid", "aclk100", E4X12_GATE_IP_PERIR, 0, 0, 0),
	GATE(sysreg, "sysreg", "aclk100", E4X12_GATE_IP_PERIR, 1,
			CLK_IGNORE_UNUSED, 0),
	GATE(hdmi_cec, "hdmi_cec", "aclk100", E4X12_GATE_IP_PERIR, 11, 0, 0),
	GATE(sclk_mdnie0, "sclk_mdnie0", "div_mdnie0",
			SRC_MASK_LCD0, 4, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mdnie_pwm0, "sclk_mdnie_pwm0", "div_mdnie_pwm_pre0",
			SRC_MASK_LCD0, 8, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_mipihsi, "sclk_mipihsi", "div_mipihsi",
			SRC_MASK_FSYS, 24, CLK_SET_RATE_PARENT, 0),
	GATE(smmu_rotator, "smmu_rotator", "aclk200",
			E4X12_GATE_IP_IMAGE, 4, 0, 0),
	GATE(mct, "mct", "aclk100", E4X12_GATE_IP_PERIR, 13,
			0, 0),
	GATE(rtc, "rtc", "aclk100", E4X12_GATE_IP_PERIR, 15,
			0, 0),
	GATE(keyif, "keyif", "aclk100", E4X12_GATE_IP_PERIR, 16, 0, 0),
	GATE(sclk_pwm_isp, "sclk_pwm_isp", "div_pwm_isp",
			E4X12_SRC_MASK_ISP, 0, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spi0_isp, "sclk_spi0_isp", "div_spi0_isp_pre",
			E4X12_SRC_MASK_ISP, 4, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_spi1_isp, "sclk_spi1_isp", "div_spi1_isp_pre",
			E4X12_SRC_MASK_ISP, 8, CLK_SET_RATE_PARENT, 0),
	GATE(sclk_uart_isp, "sclk_uart_isp", "div_uart_isp",
			E4X12_SRC_MASK_ISP, 12, CLK_SET_RATE_PARENT, 0),
	GATE(pwm_isp_sclk, "pwm_isp_sclk", "sclk_pwm_isp",
			E4X12_GATE_IP_ISP, 0, 0, 0),
	GATE(spi0_isp_sclk, "spi0_isp_sclk", "sclk_spi0_isp",
			E4X12_GATE_IP_ISP, 1, 0, 0),
	GATE(spi1_isp_sclk, "spi1_isp_sclk", "sclk_spi1_isp",
			E4X12_GATE_IP_ISP, 2, 0, 0),
	GATE(uart_isp_sclk, "uart_isp_sclk", "sclk_uart_isp",
			E4X12_GATE_IP_ISP, 3, 0, 0),
	GATE(wdt, "watchdog", "aclk100", E4X12_GATE_IP_PERIR, 14, 0, 0),
	GATE(pcm0, "pcm0", "aclk100", E4X12_GATE_IP_MAUDIO, 2,
			0, 0),
	GATE(i2s0, "i2s0", "aclk100", E4X12_GATE_IP_MAUDIO, 3,
			0, 0),
	GATE(fimc_isp, "isp", "aclk200", E4X12_GATE_ISP0, 0,
			CLK_IGNORE_UNUSED, 0),
	GATE(fimc_drc, "drc", "aclk200", E4X12_GATE_ISP0, 1,
			CLK_IGNORE_UNUSED, 0),
	GATE(fimc_fd, "fd", "aclk200", E4X12_GATE_ISP0, 2,
			CLK_IGNORE_UNUSED, 0),
	GATE(fimc_lite0, "lite0", "aclk200", E4X12_GATE_ISP0, 3,
			CLK_IGNORE_UNUSED, 0),
	GATE(fimc_lite1, "lite1", "aclk200", E4X12_GATE_ISP0, 4,
			CLK_IGNORE_UNUSED, 0),
	GATE(mcuisp, "mcuisp", "aclk200", E4X12_GATE_ISP0, 5,
			CLK_IGNORE_UNUSED, 0),
	GATE(gicisp, "gicisp", "aclk200", E4X12_GATE_ISP0, 7,
			CLK_IGNORE_UNUSED, 0),
	GATE(smmu_isp, "smmu_isp", "aclk200", E4X12_GATE_ISP0, 8,
			CLK_IGNORE_UNUSED, 0),
	GATE(smmu_drc, "smmu_drc", "aclk200", E4X12_GATE_ISP0, 9,
			CLK_IGNORE_UNUSED, 0),
	GATE(smmu_fd, "smmu_fd", "aclk200", E4X12_GATE_ISP0, 10,
			CLK_IGNORE_UNUSED, 0),
	GATE(smmu_lite0, "smmu_lite0", "aclk200", E4X12_GATE_ISP0, 11,
			CLK_IGNORE_UNUSED, 0),
	GATE(smmu_lite1, "smmu_lite1", "aclk200", E4X12_GATE_ISP0, 12,
			CLK_IGNORE_UNUSED, 0),
	GATE(ppmuispmx, "ppmuispmx", "aclk200", E4X12_GATE_ISP0, 20,
			CLK_IGNORE_UNUSED, 0),
	GATE(ppmuispx, "ppmuispx", "aclk200", E4X12_GATE_ISP0, 21,
			CLK_IGNORE_UNUSED, 0),
	GATE(mcuctl_isp, "mcuctl_isp", "aclk200", E4X12_GATE_ISP0, 23,
			CLK_IGNORE_UNUSED, 0),
	GATE(mpwm_isp, "mpwm_isp", "aclk200", E4X12_GATE_ISP0, 24,
			CLK_IGNORE_UNUSED, 0),
	GATE(i2c0_isp, "i2c0_isp", "aclk200", E4X12_GATE_ISP0, 25,
			CLK_IGNORE_UNUSED, 0),
	GATE(i2c1_isp, "i2c1_isp", "aclk200", E4X12_GATE_ISP0, 26,
			CLK_IGNORE_UNUSED, 0),
	GATE(mtcadc_isp, "mtcadc_isp", "aclk200", E4X12_GATE_ISP0, 27,
			CLK_IGNORE_UNUSED, 0),
	GATE(pwm_isp, "pwm_isp", "aclk200", E4X12_GATE_ISP0, 28,
			CLK_IGNORE_UNUSED, 0),
	GATE(wdt_isp, "wdt_isp", "aclk200", E4X12_GATE_ISP0, 30,
			CLK_IGNORE_UNUSED, 0),
	GATE(uart_isp, "uart_isp", "aclk200", E4X12_GATE_ISP0, 31,
			CLK_IGNORE_UNUSED, 0),
	GATE(asyncaxim, "asyncaxim", "aclk200", E4X12_GATE_ISP1, 0,
			CLK_IGNORE_UNUSED, 0),
	GATE(smmu_ispcx, "smmu_ispcx", "aclk200", E4X12_GATE_ISP1, 4,
			CLK_IGNORE_UNUSED, 0),
	GATE(spi0_isp, "spi0_isp", "aclk200", E4X12_GATE_ISP1, 12,
			CLK_IGNORE_UNUSED, 0),
	GATE(spi1_isp, "spi1_isp", "aclk200", E4X12_GATE_ISP1, 13,
			CLK_IGNORE_UNUSED, 0),
	GATE(g2d, "g2d", "aclk200", GATE_IP_DMC, 23, 0, 0),
	GATE(tmu_apbif, "tmu_apbif", "aclk100", E4X12_GATE_IP_PERIR, 17, 0, 0),
};

static struct samsung_clock_alias exynos4_aliases[] __initdata = {
	ALIAS(mout_core, NULL, "moutcore"),
	ALIAS(arm_clk, NULL, "armclk"),
	ALIAS(sclk_apll, NULL, "mout_apll"),
};

static struct samsung_clock_alias exynos4210_aliases[] __initdata = {
	ALIAS(sclk_mpll, NULL, "mout_mpll"),
};

static struct samsung_clock_alias exynos4x12_aliases[] __initdata = {
	ALIAS(mout_mpll_user_c, NULL, "mout_mpll"),
};

/*
 * The parent of the fin_pll clock is selected by the XOM[0] bit. This bit
 * resides in chipid register space, outside of the clock controller memory
 * mapped space. So to determine the parent of fin_pll clock, the chipid
 * controller is first remapped and the value of XOM[0] bit is read to
 * determine the parent clock.
 */
static unsigned long exynos4_get_xom(void)
{
	unsigned long xom = 0;
	void __iomem *chipid_base;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "samsung,exynos4210-chipid");
	if (np) {
		chipid_base = of_iomap(np, 0);

		if (chipid_base)
			xom = readl(chipid_base + 8);

		iounmap(chipid_base);
	}

	return xom;
}

static void __init exynos4_clk_register_finpll(unsigned long xom)
{
	struct samsung_fixed_rate_clock fclk;
	struct clk *clk;
	unsigned long finpll_f = 24000000;
	char *parent_name;

	parent_name = xom & 1 ? "xusbxti" : "xxti";
	clk = clk_get(NULL, parent_name);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to lookup parent clock %s, assuming "
			"fin_pll clock frequency is 24MHz\n", __func__,
			parent_name);
	} else {
		finpll_f = clk_get_rate(clk);
	}

	fclk.id = fin_pll;
	fclk.name = "fin_pll";
	fclk.parent_name = NULL;
	fclk.flags = CLK_IS_ROOT;
	fclk.fixed_rate = finpll_f;
	samsung_clk_register_fixed_rate(&fclk, 1);

}

static struct of_device_id ext_clk_match[] __initdata = {
	{ .compatible = "samsung,clock-xxti", .data = (void *)0, },
	{ .compatible = "samsung,clock-xusbxti", .data = (void *)1, },
	{},
};

static struct samsung_pll_clock exynos4x12_plls[nr_plls] __initdata = {
	[apll] = PLL(pll_35xx, fout_apll, "fout_apll", "fin_pll",
			APLL_LOCK, APLL_CON0, NULL),
	[mpll] = PLL(pll_35xx, fout_mpll, "fout_mpll", "fin_pll",
			E4X12_MPLL_LOCK, E4X12_MPLL_CON0, NULL),
	[epll] = PLL(pll_36xx, fout_epll, "fout_epll", "fin_pll",
			EPLL_LOCK, EPLL_CON0, NULL),
	[vpll] = PLL(pll_36xx, fout_vpll, "fout_vpll", "fin_pll",
			VPLL_LOCK, VPLL_CON0, NULL),
};

/* register exynos4 clocks */
static void __init exynos4_clk_init(struct device_node *np,
				    enum exynos4_soc exynos4_soc,
				    void __iomem *reg_base, unsigned long xom)
{
	struct clk *apll, *mpll, *epll, *vpll;

	reg_base = of_iomap(np, 0);
	if (!reg_base)
		panic("%s: failed to map registers\n", __func__);

	if (exynos4_soc == EXYNOS4210)
		samsung_clk_init(np, reg_base, nr_clks,
			exynos4_clk_regs, ARRAY_SIZE(exynos4_clk_regs),
			exynos4210_clk_save, ARRAY_SIZE(exynos4210_clk_save));
	else
		samsung_clk_init(np, reg_base, nr_clks,
			exynos4_clk_regs, ARRAY_SIZE(exynos4_clk_regs),
			exynos4x12_clk_save, ARRAY_SIZE(exynos4x12_clk_save));

	samsung_clk_of_register_fixed_ext(exynos4_fixed_rate_ext_clks,
			ARRAY_SIZE(exynos4_fixed_rate_ext_clks),
			ext_clk_match);

	exynos4_clk_register_finpll(xom);

	if (exynos4_soc == EXYNOS4210) {
		apll = samsung_clk_register_pll45xx("fout_apll", "fin_pll",
					reg_base + APLL_CON0, pll_4508);
		mpll = samsung_clk_register_pll45xx("fout_mpll", "fin_pll",
					reg_base + E4210_MPLL_CON0, pll_4508);
		epll = samsung_clk_register_pll46xx("fout_epll", "fin_pll",
					reg_base + EPLL_CON0, pll_4600);
		vpll = samsung_clk_register_pll46xx("fout_vpll", "mout_vpllsrc",
					reg_base + VPLL_CON0, pll_4650c);

		samsung_clk_add_lookup(apll, fout_apll);
		samsung_clk_add_lookup(mpll, fout_mpll);
		samsung_clk_add_lookup(epll, fout_epll);
		samsung_clk_add_lookup(vpll, fout_vpll);
	} else {
		samsung_clk_register_pll(exynos4x12_plls,
					ARRAY_SIZE(exynos4x12_plls), reg_base);
	}

	samsung_clk_register_fixed_rate(exynos4_fixed_rate_clks,
			ARRAY_SIZE(exynos4_fixed_rate_clks));
	samsung_clk_register_mux(exynos4_mux_clks,
			ARRAY_SIZE(exynos4_mux_clks));
	samsung_clk_register_div(exynos4_div_clks,
			ARRAY_SIZE(exynos4_div_clks));
	samsung_clk_register_gate(exynos4_gate_clks,
			ARRAY_SIZE(exynos4_gate_clks));

	if (exynos4_soc == EXYNOS4210) {
		samsung_clk_register_fixed_rate(exynos4210_fixed_rate_clks,
			ARRAY_SIZE(exynos4210_fixed_rate_clks));
		samsung_clk_register_mux(exynos4210_mux_clks,
			ARRAY_SIZE(exynos4210_mux_clks));
		samsung_clk_register_div(exynos4210_div_clks,
			ARRAY_SIZE(exynos4210_div_clks));
		samsung_clk_register_gate(exynos4210_gate_clks,
			ARRAY_SIZE(exynos4210_gate_clks));
		samsung_clk_register_alias(exynos4210_aliases,
			ARRAY_SIZE(exynos4210_aliases));
	} else {
		samsung_clk_register_mux(exynos4x12_mux_clks,
			ARRAY_SIZE(exynos4x12_mux_clks));
		samsung_clk_register_div(exynos4x12_div_clks,
			ARRAY_SIZE(exynos4x12_div_clks));
		samsung_clk_register_gate(exynos4x12_gate_clks,
			ARRAY_SIZE(exynos4x12_gate_clks));
		samsung_clk_register_alias(exynos4x12_aliases,
			ARRAY_SIZE(exynos4x12_aliases));
	}

	samsung_clk_register_alias(exynos4_aliases,
			ARRAY_SIZE(exynos4_aliases));

	pr_info("%s clocks: sclk_apll = %ld, sclk_mpll = %ld\n"
		"\tsclk_epll = %ld, sclk_vpll = %ld, arm_clk = %ld\n",
		exynos4_soc == EXYNOS4210 ? "Exynos4210" : "Exynos4x12",
		_get_rate("sclk_apll"),	_get_rate("sclk_mpll"),
		_get_rate("sclk_epll"), _get_rate("sclk_vpll"),
		_get_rate("arm_clk"));
}


static void __init exynos4210_clk_init(struct device_node *np)
{
	exynos4_clk_init(np, EXYNOS4210, NULL, exynos4_get_xom());
}
CLK_OF_DECLARE(exynos4210_clk, "samsung,exynos4210-clock", exynos4210_clk_init);

static void __init exynos4412_clk_init(struct device_node *np)
{
	exynos4_clk_init(np, EXYNOS4X12, NULL, exynos4_get_xom());
}
CLK_OF_DECLARE(exynos4412_clk, "samsung,exynos4412-clock", exynos4412_clk_init);
