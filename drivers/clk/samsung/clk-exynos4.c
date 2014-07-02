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

#include <dt-bindings/clock/exynos4.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>

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
#define SRC_MFC			0xc228
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
#define E4210_MPLL_LOCK		0x14008
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

static void __iomem *reg_base;
static enum exynos4_soc exynos4_soc;

/*
 * Support for CMU save/restore across system suspends
 */
#ifdef CONFIG_PM_SLEEP
static struct samsung_clk_reg_dump *exynos4_save_common;
static struct samsung_clk_reg_dump *exynos4_save_soc;
static struct samsung_clk_reg_dump *exynos4_save_pll;

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

static unsigned long exynos4_clk_pll_regs[] __initdata = {
	EPLL_LOCK,
	VPLL_LOCK,
	EPLL_CON0,
	EPLL_CON1,
	EPLL_CON2,
	VPLL_CON0,
	VPLL_CON1,
	VPLL_CON2,
};

static unsigned long exynos4_clk_regs[] __initdata = {
	SRC_LEFTBUS,
	DIV_LEFTBUS,
	GATE_IP_LEFTBUS,
	SRC_RIGHTBUS,
	DIV_RIGHTBUS,
	GATE_IP_RIGHTBUS,
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

static const struct samsung_clk_reg_dump src_mask_suspend[] = {
	{ .offset = SRC_MASK_TOP,		.value = 0x00000001, },
	{ .offset = SRC_MASK_CAM,		.value = 0x11111111, },
	{ .offset = SRC_MASK_TV,		.value = 0x00000111, },
	{ .offset = SRC_MASK_LCD0,		.value = 0x00001111, },
	{ .offset = SRC_MASK_MAUDIO,		.value = 0x00000001, },
	{ .offset = SRC_MASK_FSYS,		.value = 0x01011111, },
	{ .offset = SRC_MASK_PERIL0,		.value = 0x01111111, },
	{ .offset = SRC_MASK_PERIL1,		.value = 0x01110111, },
	{ .offset = SRC_MASK_DMC,		.value = 0x00010000, },
};

static const struct samsung_clk_reg_dump src_mask_suspend_e4210[] = {
	{ .offset = E4210_SRC_MASK_LCD1,	.value = 0x00001111, },
};

#define PLL_ENABLED	(1 << 31)
#define PLL_LOCKED	(1 << 29)

static void exynos4_clk_wait_for_pll(u32 reg)
{
	u32 pll_con;

	pll_con = readl(reg_base + reg);
	if (!(pll_con & PLL_ENABLED))
		return;

	while (!(pll_con & PLL_LOCKED)) {
		cpu_relax();
		pll_con = readl(reg_base + reg);
	}
}

static int exynos4_clk_suspend(void)
{
	samsung_clk_save(reg_base, exynos4_save_common,
				ARRAY_SIZE(exynos4_clk_regs));
	samsung_clk_save(reg_base, exynos4_save_pll,
				ARRAY_SIZE(exynos4_clk_pll_regs));

	if (exynos4_soc == EXYNOS4210) {
		samsung_clk_save(reg_base, exynos4_save_soc,
					ARRAY_SIZE(exynos4210_clk_save));
		samsung_clk_restore(reg_base, src_mask_suspend_e4210,
					ARRAY_SIZE(src_mask_suspend_e4210));
	} else {
		samsung_clk_save(reg_base, exynos4_save_soc,
					ARRAY_SIZE(exynos4x12_clk_save));
	}

	samsung_clk_restore(reg_base, src_mask_suspend,
					ARRAY_SIZE(src_mask_suspend));

	return 0;
}

static void exynos4_clk_resume(void)
{
	samsung_clk_restore(reg_base, exynos4_save_pll,
				ARRAY_SIZE(exynos4_clk_pll_regs));

	exynos4_clk_wait_for_pll(EPLL_CON0);
	exynos4_clk_wait_for_pll(VPLL_CON0);

	samsung_clk_restore(reg_base, exynos4_save_common,
				ARRAY_SIZE(exynos4_clk_regs));

	if (exynos4_soc == EXYNOS4210)
		samsung_clk_restore(reg_base, exynos4_save_soc,
					ARRAY_SIZE(exynos4210_clk_save));
	else
		samsung_clk_restore(reg_base, exynos4_save_soc,
					ARRAY_SIZE(exynos4x12_clk_save));
}

static struct syscore_ops exynos4_clk_syscore_ops = {
	.suspend = exynos4_clk_suspend,
	.resume = exynos4_clk_resume,
};

static void __init exynos4_clk_sleep_init(void)
{
	exynos4_save_common = samsung_clk_alloc_reg_dump(exynos4_clk_regs,
					ARRAY_SIZE(exynos4_clk_regs));
	if (!exynos4_save_common)
		goto err_warn;

	if (exynos4_soc == EXYNOS4210)
		exynos4_save_soc = samsung_clk_alloc_reg_dump(
					exynos4210_clk_save,
					ARRAY_SIZE(exynos4210_clk_save));
	else
		exynos4_save_soc = samsung_clk_alloc_reg_dump(
					exynos4x12_clk_save,
					ARRAY_SIZE(exynos4x12_clk_save));
	if (!exynos4_save_soc)
		goto err_common;

	exynos4_save_pll = samsung_clk_alloc_reg_dump(exynos4_clk_pll_regs,
					ARRAY_SIZE(exynos4_clk_pll_regs));
	if (!exynos4_save_pll)
		goto err_soc;

	register_syscore_ops(&exynos4_clk_syscore_ops);
	return;

err_soc:
	kfree(exynos4_save_soc);
err_common:
	kfree(exynos4_save_common);
err_warn:
	pr_warn("%s: failed to allocate sleep save data, no sleep support!\n",
		__func__);
}
#else
static void __init exynos4_clk_sleep_init(void) {}
#endif

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
	FRATE(CLK_XXTI, "xxti", NULL, CLK_IS_ROOT, 0),
	FRATE(CLK_XUSBXTI, "xusbxti", NULL, CLK_IS_ROOT, 0),
};

/* fixed rate clocks generated inside the soc */
static struct samsung_fixed_rate_clock exynos4_fixed_rate_clks[] __initdata = {
	FRATE(0, "sclk_hdmi24m", NULL, CLK_IS_ROOT, 24000000),
	FRATE(CLK_SCLK_HDMIPHY, "sclk_hdmiphy", NULL, CLK_IS_ROOT, 27000000),
	FRATE(0, "sclk_usbphy0", NULL, CLK_IS_ROOT, 48000000),
};

static struct samsung_fixed_rate_clock exynos4210_fixed_rate_clks[] __initdata = {
	FRATE(0, "sclk_usbphy1", NULL, CLK_IS_ROOT, 48000000),
};

/* list of mux clocks supported in all exynos4 soc's */
static struct samsung_mux_clock exynos4_mux_clks[] __initdata = {
	MUX_FA(CLK_MOUT_APLL, "mout_apll", mout_apll_p, SRC_CPU, 0, 1,
			CLK_SET_RATE_PARENT, 0, "mout_apll"),
	MUX(0, "mout_hdmi", mout_hdmi_p, SRC_TV, 0, 1),
	MUX(0, "mout_mfc1", sclk_evpll_p, SRC_MFC, 4, 1),
	MUX(0, "mout_mfc", mout_mfc_p, SRC_MFC, 8, 1),
	MUX_F(CLK_MOUT_G3D1, "mout_g3d1", sclk_evpll_p, SRC_G3D, 4, 1,
			CLK_SET_RATE_PARENT, 0),
	MUX_F(CLK_MOUT_G3D, "mout_g3d", mout_g3d_p, SRC_G3D, 8, 1,
			CLK_SET_RATE_PARENT, 0),
	MUX(0, "mout_spdif", mout_spdif_p, SRC_PERIL1, 8, 2),
	MUX(0, "mout_onenand1", mout_onenand1_p, SRC_TOP0, 0, 1),
	MUX(CLK_SCLK_EPLL, "sclk_epll", mout_epll_p, SRC_TOP0, 4, 1),
	MUX(0, "mout_onenand", mout_onenand_p, SRC_TOP0, 28, 1),
};

/* list of mux clocks supported in exynos4210 soc */
static struct samsung_mux_clock exynos4210_mux_early[] __initdata = {
	MUX(0, "mout_vpllsrc", mout_vpllsrc_p, SRC_TOP1, 0, 1),
};

static struct samsung_mux_clock exynos4210_mux_clks[] __initdata = {
	MUX(0, "mout_aclk200", sclk_ampll_p4210, SRC_TOP0, 12, 1),
	MUX(0, "mout_aclk100", sclk_ampll_p4210, SRC_TOP0, 16, 1),
	MUX(0, "mout_aclk160", sclk_ampll_p4210, SRC_TOP0, 20, 1),
	MUX(0, "mout_aclk133", sclk_ampll_p4210, SRC_TOP0, 24, 1),
	MUX(0, "mout_mixer", mout_mixer_p4210, SRC_TV, 4, 1),
	MUX(0, "mout_dac", mout_dac_p4210, SRC_TV, 8, 1),
	MUX(0, "mout_g2d0", sclk_ampll_p4210, E4210_SRC_IMAGE, 0, 1),
	MUX(0, "mout_g2d1", sclk_evpll_p, E4210_SRC_IMAGE, 4, 1),
	MUX(0, "mout_g2d", mout_g2d_p, E4210_SRC_IMAGE, 8, 1),
	MUX(0, "mout_fimd1", group1_p4210, E4210_SRC_LCD1, 0, 4),
	MUX(0, "mout_mipi1", group1_p4210, E4210_SRC_LCD1, 12, 4),
	MUX(CLK_SCLK_MPLL, "sclk_mpll", mout_mpll_p, SRC_CPU, 8, 1),
	MUX(CLK_MOUT_CORE, "mout_core", mout_core_p4210, SRC_CPU, 16, 1),
	MUX(CLK_SCLK_VPLL, "sclk_vpll", sclk_vpll_p4210, SRC_TOP0, 8, 1),
	MUX(CLK_MOUT_FIMC0, "mout_fimc0", group1_p4210, SRC_CAM, 0, 4),
	MUX(CLK_MOUT_FIMC1, "mout_fimc1", group1_p4210, SRC_CAM, 4, 4),
	MUX(CLK_MOUT_FIMC2, "mout_fimc2", group1_p4210, SRC_CAM, 8, 4),
	MUX(CLK_MOUT_FIMC3, "mout_fimc3", group1_p4210, SRC_CAM, 12, 4),
	MUX(CLK_MOUT_CAM0, "mout_cam0", group1_p4210, SRC_CAM, 16, 4),
	MUX(CLK_MOUT_CAM1, "mout_cam1", group1_p4210, SRC_CAM, 20, 4),
	MUX(CLK_MOUT_CSIS0, "mout_csis0", group1_p4210, SRC_CAM, 24, 4),
	MUX(CLK_MOUT_CSIS1, "mout_csis1", group1_p4210, SRC_CAM, 28, 4),
	MUX(0, "mout_mfc0", sclk_ampll_p4210, SRC_MFC, 0, 1),
	MUX_F(CLK_MOUT_G3D0, "mout_g3d0", sclk_ampll_p4210, SRC_G3D, 0, 1,
			CLK_SET_RATE_PARENT, 0),
	MUX(0, "mout_fimd0", group1_p4210, SRC_LCD0, 0, 4),
	MUX(0, "mout_mipi0", group1_p4210, SRC_LCD0, 12, 4),
	MUX(0, "mout_audio0", mout_audio0_p4210, SRC_MAUDIO, 0, 4),
	MUX(0, "mout_mmc0", group1_p4210, SRC_FSYS, 0, 4),
	MUX(0, "mout_mmc1", group1_p4210, SRC_FSYS, 4, 4),
	MUX(0, "mout_mmc2", group1_p4210, SRC_FSYS, 8, 4),
	MUX(0, "mout_mmc3", group1_p4210, SRC_FSYS, 12, 4),
	MUX(0, "mout_mmc4", group1_p4210, SRC_FSYS, 16, 4),
	MUX(0, "mout_sata", sclk_ampll_p4210, SRC_FSYS, 24, 1),
	MUX(0, "mout_uart0", group1_p4210, SRC_PERIL0, 0, 4),
	MUX(0, "mout_uart1", group1_p4210, SRC_PERIL0, 4, 4),
	MUX(0, "mout_uart2", group1_p4210, SRC_PERIL0, 8, 4),
	MUX(0, "mout_uart3", group1_p4210, SRC_PERIL0, 12, 4),
	MUX(0, "mout_uart4", group1_p4210, SRC_PERIL0, 16, 4),
	MUX(0, "mout_audio1", mout_audio1_p4210, SRC_PERIL1, 0, 4),
	MUX(0, "mout_audio2", mout_audio2_p4210, SRC_PERIL1, 4, 4),
	MUX(0, "mout_spi0", group1_p4210, SRC_PERIL1, 16, 4),
	MUX(0, "mout_spi1", group1_p4210, SRC_PERIL1, 20, 4),
	MUX(0, "mout_spi2", group1_p4210, SRC_PERIL1, 24, 4),
};

/* list of mux clocks supported in exynos4x12 soc */
static struct samsung_mux_clock exynos4x12_mux_clks[] __initdata = {
	MUX(CLK_MOUT_MPLL_USER_C, "mout_mpll_user_c", mout_mpll_user_p4x12,
			SRC_CPU, 24, 1),
	MUX(0, "mout_aclk266_gps", aclk_p4412, SRC_TOP1, 4, 1),
	MUX(0, "mout_aclk400_mcuisp", aclk_p4412, SRC_TOP1, 8, 1),
	MUX(CLK_MOUT_MPLL_USER_T, "mout_mpll_user_t", mout_mpll_user_p4x12,
			SRC_TOP1, 12, 1),
	MUX(0, "mout_user_aclk266_gps", mout_user_aclk266_gps_p4x12,
			SRC_TOP1, 16, 1),
	MUX(CLK_ACLK200, "aclk200", mout_user_aclk200_p4x12, SRC_TOP1, 20, 1),
	MUX(CLK_ACLK400_MCUISP, "aclk400_mcuisp",
		mout_user_aclk400_mcuisp_p4x12, SRC_TOP1, 24, 1),
	MUX(0, "mout_aclk200", aclk_p4412, SRC_TOP0, 12, 1),
	MUX(0, "mout_aclk100", aclk_p4412, SRC_TOP0, 16, 1),
	MUX(0, "mout_aclk160", aclk_p4412, SRC_TOP0, 20, 1),
	MUX(0, "mout_aclk133", aclk_p4412, SRC_TOP0, 24, 1),
	MUX(0, "mout_mdnie0", group1_p4x12, SRC_LCD0, 4, 4),
	MUX(0, "mout_mdnie_pwm0", group1_p4x12, SRC_LCD0, 8, 4),
	MUX(0, "mout_sata", sclk_ampll_p4x12, SRC_FSYS, 24, 1),
	MUX(0, "mout_jpeg0", sclk_ampll_p4x12, E4X12_SRC_CAM1, 0, 1),
	MUX(0, "mout_jpeg1", sclk_evpll_p, E4X12_SRC_CAM1, 4, 1),
	MUX(0, "mout_jpeg", mout_jpeg_p, E4X12_SRC_CAM1, 8, 1),
	MUX(CLK_SCLK_MPLL, "sclk_mpll", mout_mpll_p, SRC_DMC, 12, 1),
	MUX(CLK_SCLK_VPLL, "sclk_vpll", mout_vpll_p, SRC_TOP0, 8, 1),
	MUX(CLK_MOUT_CORE, "mout_core", mout_core_p4x12, SRC_CPU, 16, 1),
	MUX(CLK_MOUT_FIMC0, "mout_fimc0", group1_p4x12, SRC_CAM, 0, 4),
	MUX(CLK_MOUT_FIMC1, "mout_fimc1", group1_p4x12, SRC_CAM, 4, 4),
	MUX(CLK_MOUT_FIMC2, "mout_fimc2", group1_p4x12, SRC_CAM, 8, 4),
	MUX(CLK_MOUT_FIMC3, "mout_fimc3", group1_p4x12, SRC_CAM, 12, 4),
	MUX(CLK_MOUT_CAM0, "mout_cam0", group1_p4x12, SRC_CAM, 16, 4),
	MUX(CLK_MOUT_CAM1, "mout_cam1", group1_p4x12, SRC_CAM, 20, 4),
	MUX(CLK_MOUT_CSIS0, "mout_csis0", group1_p4x12, SRC_CAM, 24, 4),
	MUX(CLK_MOUT_CSIS1, "mout_csis1", group1_p4x12, SRC_CAM, 28, 4),
	MUX(0, "mout_mfc0", sclk_ampll_p4x12, SRC_MFC, 0, 1),
	MUX_F(CLK_MOUT_G3D0, "mout_g3d0", sclk_ampll_p4x12, SRC_G3D, 0, 1,
			CLK_SET_RATE_PARENT, 0),
	MUX(0, "mout_fimd0", group1_p4x12, SRC_LCD0, 0, 4),
	MUX(0, "mout_mipi0", group1_p4x12, SRC_LCD0, 12, 4),
	MUX(0, "mout_audio0", mout_audio0_p4x12, SRC_MAUDIO, 0, 4),
	MUX(0, "mout_mmc0", group1_p4x12, SRC_FSYS, 0, 4),
	MUX(0, "mout_mmc1", group1_p4x12, SRC_FSYS, 4, 4),
	MUX(0, "mout_mmc2", group1_p4x12, SRC_FSYS, 8, 4),
	MUX(0, "mout_mmc3", group1_p4x12, SRC_FSYS, 12, 4),
	MUX(0, "mout_mmc4", group1_p4x12, SRC_FSYS, 16, 4),
	MUX(0, "mout_mipihsi", aclk_p4412, SRC_FSYS, 24, 1),
	MUX(0, "mout_uart0", group1_p4x12, SRC_PERIL0, 0, 4),
	MUX(0, "mout_uart1", group1_p4x12, SRC_PERIL0, 4, 4),
	MUX(0, "mout_uart2", group1_p4x12, SRC_PERIL0, 8, 4),
	MUX(0, "mout_uart3", group1_p4x12, SRC_PERIL0, 12, 4),
	MUX(0, "mout_uart4", group1_p4x12, SRC_PERIL0, 16, 4),
	MUX(0, "mout_audio1", mout_audio1_p4x12, SRC_PERIL1, 0, 4),
	MUX(0, "mout_audio2", mout_audio2_p4x12, SRC_PERIL1, 4, 4),
	MUX(0, "mout_spi0", group1_p4x12, SRC_PERIL1, 16, 4),
	MUX(0, "mout_spi1", group1_p4x12, SRC_PERIL1, 20, 4),
	MUX(0, "mout_spi2", group1_p4x12, SRC_PERIL1, 24, 4),
	MUX(0, "mout_pwm_isp", group1_p4x12, E4X12_SRC_ISP, 0, 4),
	MUX(0, "mout_spi0_isp", group1_p4x12, E4X12_SRC_ISP, 4, 4),
	MUX(0, "mout_spi1_isp", group1_p4x12, E4X12_SRC_ISP, 8, 4),
	MUX(0, "mout_uart_isp", group1_p4x12, E4X12_SRC_ISP, 12, 4),
	MUX(0, "mout_g2d0", sclk_ampll_p4210, SRC_DMC, 20, 1),
	MUX(0, "mout_g2d1", sclk_evpll_p, SRC_DMC, 24, 1),
	MUX(0, "mout_g2d", mout_g2d_p, SRC_DMC, 28, 1),
};

/* list of divider clocks supported in all exynos4 soc's */
static struct samsung_div_clock exynos4_div_clks[] __initdata = {
	DIV(0, "div_core", "mout_core", DIV_CPU0, 0, 3),
	DIV(0, "div_core2", "div_core", DIV_CPU0, 28, 3),
	DIV(0, "div_fimc0", "mout_fimc0", DIV_CAM, 0, 4),
	DIV(0, "div_fimc1", "mout_fimc1", DIV_CAM, 4, 4),
	DIV(0, "div_fimc2", "mout_fimc2", DIV_CAM, 8, 4),
	DIV(0, "div_fimc3", "mout_fimc3", DIV_CAM, 12, 4),
	DIV(0, "div_cam0", "mout_cam0", DIV_CAM, 16, 4),
	DIV(0, "div_cam1", "mout_cam1", DIV_CAM, 20, 4),
	DIV(0, "div_csis0", "mout_csis0", DIV_CAM, 24, 4),
	DIV(0, "div_csis1", "mout_csis1", DIV_CAM, 28, 4),
	DIV(CLK_SCLK_MFC, "sclk_mfc", "mout_mfc", DIV_MFC, 0, 4),
	DIV_F(0, "div_g3d", "mout_g3d", DIV_G3D, 0, 4,
			CLK_SET_RATE_PARENT, 0),
	DIV(0, "div_fimd0", "mout_fimd0", DIV_LCD0, 0, 4),
	DIV(0, "div_mipi0", "mout_mipi0", DIV_LCD0, 16, 4),
	DIV(0, "div_audio0", "mout_audio0", DIV_MAUDIO, 0, 4),
	DIV(CLK_SCLK_PCM0, "sclk_pcm0", "sclk_audio0", DIV_MAUDIO, 4, 8),
	DIV(0, "div_mmc0", "mout_mmc0", DIV_FSYS1, 0, 4),
	DIV(0, "div_mmc1", "mout_mmc1", DIV_FSYS1, 16, 4),
	DIV(0, "div_mmc2", "mout_mmc2", DIV_FSYS2, 0, 4),
	DIV(0, "div_mmc3", "mout_mmc3", DIV_FSYS2, 16, 4),
	DIV(CLK_SCLK_PIXEL, "sclk_pixel", "sclk_vpll", DIV_TV, 0, 4),
	DIV(CLK_ACLK100, "aclk100", "mout_aclk100", DIV_TOP, 4, 4),
	DIV(CLK_ACLK160, "aclk160", "mout_aclk160", DIV_TOP, 8, 3),
	DIV(CLK_ACLK133, "aclk133", "mout_aclk133", DIV_TOP, 12, 3),
	DIV(0, "div_onenand", "mout_onenand1", DIV_TOP, 16, 3),
	DIV(CLK_SCLK_SLIMBUS, "sclk_slimbus", "sclk_epll", DIV_PERIL3, 4, 4),
	DIV(CLK_SCLK_PCM1, "sclk_pcm1", "sclk_audio1", DIV_PERIL4, 4, 8),
	DIV(CLK_SCLK_PCM2, "sclk_pcm2", "sclk_audio2", DIV_PERIL4, 20, 8),
	DIV(CLK_SCLK_I2S1, "sclk_i2s1", "sclk_audio1", DIV_PERIL5, 0, 6),
	DIV(CLK_SCLK_I2S2, "sclk_i2s2", "sclk_audio2", DIV_PERIL5, 8, 6),
	DIV(0, "div_mmc4", "mout_mmc4", DIV_FSYS3, 0, 4),
	DIV_F(0, "div_mmc_pre4", "div_mmc4", DIV_FSYS3, 8, 8,
			CLK_SET_RATE_PARENT, 0),
	DIV(0, "div_uart0", "mout_uart0", DIV_PERIL0, 0, 4),
	DIV(0, "div_uart1", "mout_uart1", DIV_PERIL0, 4, 4),
	DIV(0, "div_uart2", "mout_uart2", DIV_PERIL0, 8, 4),
	DIV(0, "div_uart3", "mout_uart3", DIV_PERIL0, 12, 4),
	DIV(0, "div_uart4", "mout_uart4", DIV_PERIL0, 16, 4),
	DIV(0, "div_spi0", "mout_spi0", DIV_PERIL1, 0, 4),
	DIV(0, "div_spi_pre0", "div_spi0", DIV_PERIL1, 8, 8),
	DIV(0, "div_spi1", "mout_spi1", DIV_PERIL1, 16, 4),
	DIV(0, "div_spi_pre1", "div_spi1", DIV_PERIL1, 24, 8),
	DIV(0, "div_spi2", "mout_spi2", DIV_PERIL2, 0, 4),
	DIV(0, "div_spi_pre2", "div_spi2", DIV_PERIL2, 8, 8),
	DIV(0, "div_audio1", "mout_audio1", DIV_PERIL4, 0, 4),
	DIV(0, "div_audio2", "mout_audio2", DIV_PERIL4, 16, 4),
	DIV(CLK_ARM_CLK, "arm_clk", "div_core2", DIV_CPU0, 28, 3),
	DIV(CLK_SCLK_APLL, "sclk_apll", "mout_apll", DIV_CPU0, 24, 3),
	DIV_F(0, "div_mipi_pre0", "div_mipi0", DIV_LCD0, 20, 4,
			CLK_SET_RATE_PARENT, 0),
	DIV_F(0, "div_mmc_pre0", "div_mmc0", DIV_FSYS1, 8, 8,
			CLK_SET_RATE_PARENT, 0),
	DIV_F(0, "div_mmc_pre1", "div_mmc1", DIV_FSYS1, 24, 8,
			CLK_SET_RATE_PARENT, 0),
	DIV_F(0, "div_mmc_pre2", "div_mmc2", DIV_FSYS2, 8, 8,
			CLK_SET_RATE_PARENT, 0),
	DIV_F(0, "div_mmc_pre3", "div_mmc3", DIV_FSYS2, 24, 8,
			CLK_SET_RATE_PARENT, 0),
};

/* list of divider clocks supported in exynos4210 soc */
static struct samsung_div_clock exynos4210_div_clks[] __initdata = {
	DIV(CLK_ACLK200, "aclk200", "mout_aclk200", DIV_TOP, 0, 3),
	DIV(CLK_SCLK_FIMG2D, "sclk_fimg2d", "mout_g2d", DIV_IMAGE, 0, 4),
	DIV(0, "div_fimd1", "mout_fimd1", E4210_DIV_LCD1, 0, 4),
	DIV(0, "div_mipi1", "mout_mipi1", E4210_DIV_LCD1, 16, 4),
	DIV(0, "div_sata", "mout_sata", DIV_FSYS0, 20, 4),
	DIV_F(0, "div_mipi_pre1", "div_mipi1", E4210_DIV_LCD1, 20, 4,
			CLK_SET_RATE_PARENT, 0),
};

/* list of divider clocks supported in exynos4x12 soc */
static struct samsung_div_clock exynos4x12_div_clks[] __initdata = {
	DIV(0, "div_mdnie0", "mout_mdnie0", DIV_LCD0, 4, 4),
	DIV(0, "div_mdnie_pwm0", "mout_mdnie_pwm0", DIV_LCD0, 8, 4),
	DIV(0, "div_mdnie_pwm_pre0", "div_mdnie_pwm0", DIV_LCD0, 12, 4),
	DIV(0, "div_mipihsi", "mout_mipihsi", DIV_FSYS0, 20, 4),
	DIV(0, "div_jpeg", "mout_jpeg", E4X12_DIV_CAM1, 0, 4),
	DIV(CLK_DIV_ACLK200, "div_aclk200", "mout_aclk200", DIV_TOP, 0, 3),
	DIV(0, "div_aclk266_gps", "mout_aclk266_gps", DIV_TOP, 20, 3),
	DIV(CLK_DIV_ACLK400_MCUISP, "div_aclk400_mcuisp", "mout_aclk400_mcuisp",
						DIV_TOP, 24, 3),
	DIV(0, "div_pwm_isp", "mout_pwm_isp", E4X12_DIV_ISP, 0, 4),
	DIV(0, "div_spi0_isp", "mout_spi0_isp", E4X12_DIV_ISP, 4, 4),
	DIV(0, "div_spi0_isp_pre", "div_spi0_isp", E4X12_DIV_ISP, 8, 8),
	DIV(0, "div_spi1_isp", "mout_spi1_isp", E4X12_DIV_ISP, 16, 4),
	DIV(0, "div_spi1_isp_pre", "div_spi1_isp", E4X12_DIV_ISP, 20, 8),
	DIV(0, "div_uart_isp", "mout_uart_isp", E4X12_DIV_ISP, 28, 4),
	DIV_F(CLK_DIV_ISP0, "div_isp0", "aclk200", E4X12_DIV_ISP0, 0, 3,
						CLK_GET_RATE_NOCACHE, 0),
	DIV_F(CLK_DIV_ISP1, "div_isp1", "aclk200", E4X12_DIV_ISP0, 4, 3,
						CLK_GET_RATE_NOCACHE, 0),
	DIV(0, "div_mpwm", "div_isp1", E4X12_DIV_ISP1, 0, 3),
	DIV_F(CLK_DIV_MCUISP0, "div_mcuisp0", "aclk400_mcuisp", E4X12_DIV_ISP1,
						4, 3, CLK_GET_RATE_NOCACHE, 0),
	DIV_F(CLK_DIV_MCUISP1, "div_mcuisp1", "div_mcuisp0", E4X12_DIV_ISP1,
						8, 3, CLK_GET_RATE_NOCACHE, 0),
	DIV(CLK_SCLK_FIMG2D, "sclk_fimg2d", "mout_g2d", DIV_DMC1, 0, 4),
};

/* list of gate clocks supported in all exynos4 soc's */
static struct samsung_gate_clock exynos4_gate_clks[] __initdata = {
	/*
	 * After all Exynos4 based platforms are migrated to use device tree,
	 * the device name and clock alias names specified below for some
	 * of the clocks can be removed.
	 */
	GATE(CLK_SCLK_HDMI, "sclk_hdmi", "mout_hdmi", SRC_MASK_TV, 0, 0, 0),
	GATE(CLK_SCLK_SPDIF, "sclk_spdif", "mout_spdif", SRC_MASK_PERIL1, 8, 0,
		0),
	GATE(CLK_JPEG, "jpeg", "aclk160", GATE_IP_CAM, 6, 0, 0),
	GATE(CLK_MIE0, "mie0", "aclk160", GATE_IP_LCD0, 1, 0, 0),
	GATE(CLK_DSIM0, "dsim0", "aclk160", GATE_IP_LCD0, 3, 0, 0),
	GATE(CLK_FIMD1, "fimd1", "aclk160", E4210_GATE_IP_LCD1, 0, 0, 0),
	GATE(CLK_MIE1, "mie1", "aclk160", E4210_GATE_IP_LCD1, 1, 0, 0),
	GATE(CLK_DSIM1, "dsim1", "aclk160", E4210_GATE_IP_LCD1, 3, 0, 0),
	GATE(CLK_SMMU_FIMD1, "smmu_fimd1", "aclk160", E4210_GATE_IP_LCD1, 4, 0,
		0),
	GATE(CLK_TSI, "tsi", "aclk133", GATE_IP_FSYS, 4, 0, 0),
	GATE(CLK_SROMC, "sromc", "aclk133", GATE_IP_FSYS, 11, 0, 0),
	GATE(CLK_SCLK_G3D, "sclk_g3d", "div_g3d", GATE_IP_G3D, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_USB_DEVICE, "usb_device", "aclk133", GATE_IP_FSYS, 13, 0, 0),
	GATE(CLK_ONENAND, "onenand", "aclk133", GATE_IP_FSYS, 15, 0, 0),
	GATE(CLK_NFCON, "nfcon", "aclk133", GATE_IP_FSYS, 16, 0, 0),
	GATE(CLK_GPS, "gps", "aclk133", GATE_IP_GPS, 0, 0, 0),
	GATE(CLK_SMMU_GPS, "smmu_gps", "aclk133", GATE_IP_GPS, 1, 0, 0),
	GATE(CLK_SLIMBUS, "slimbus", "aclk100", GATE_IP_PERIL, 25, 0, 0),
	GATE(CLK_SCLK_CAM0, "sclk_cam0", "div_cam0", GATE_SCLK_CAM, 4,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_CAM1, "sclk_cam1", "div_cam1", GATE_SCLK_CAM, 5,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MIPI0, "sclk_mipi0", "div_mipi_pre0",
			SRC_MASK_LCD0, 12, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_AUDIO0, "sclk_audio0", "div_audio0", SRC_MASK_MAUDIO, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_AUDIO1, "sclk_audio1", "div_audio1", SRC_MASK_PERIL1, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_VP, "vp", "aclk160", GATE_IP_TV, 0, 0, 0),
	GATE(CLK_MIXER, "mixer", "aclk160", GATE_IP_TV, 1, 0, 0),
	GATE(CLK_HDMI, "hdmi", "aclk160", GATE_IP_TV, 3, 0, 0),
	GATE(CLK_PWM, "pwm", "aclk100", GATE_IP_PERIL, 24, 0, 0),
	GATE(CLK_SDMMC4, "sdmmc4", "aclk133", GATE_IP_FSYS, 9, 0, 0),
	GATE(CLK_USB_HOST, "usb_host", "aclk133", GATE_IP_FSYS, 12, 0, 0),
	GATE(CLK_SCLK_FIMC0, "sclk_fimc0", "div_fimc0", SRC_MASK_CAM, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_FIMC1, "sclk_fimc1", "div_fimc1", SRC_MASK_CAM, 4,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_FIMC2, "sclk_fimc2", "div_fimc2", SRC_MASK_CAM, 8,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_FIMC3, "sclk_fimc3", "div_fimc3", SRC_MASK_CAM, 12,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_CSIS0, "sclk_csis0", "div_csis0", SRC_MASK_CAM, 24,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_CSIS1, "sclk_csis1", "div_csis1", SRC_MASK_CAM, 28,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_FIMD0, "sclk_fimd0", "div_fimd0", SRC_MASK_LCD0, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC0, "sclk_mmc0", "div_mmc_pre0", SRC_MASK_FSYS, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC1, "sclk_mmc1", "div_mmc_pre1", SRC_MASK_FSYS, 4,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC2, "sclk_mmc2", "div_mmc_pre2", SRC_MASK_FSYS, 8,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC3, "sclk_mmc3", "div_mmc_pre3", SRC_MASK_FSYS, 12,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC4, "sclk_mmc4", "div_mmc_pre4", SRC_MASK_FSYS, 16,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART0, "uclk0", "div_uart0", SRC_MASK_PERIL0, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART1, "uclk1", "div_uart1", SRC_MASK_PERIL0, 4,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART2, "uclk2", "div_uart2", SRC_MASK_PERIL0, 8,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART3, "uclk3", "div_uart3", SRC_MASK_PERIL0, 12,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART4, "uclk4", "div_uart4", SRC_MASK_PERIL0, 16,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_AUDIO2, "sclk_audio2", "div_audio2", SRC_MASK_PERIL1, 4,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0, "sclk_spi0", "div_spi_pre0", SRC_MASK_PERIL1, 16,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI1, "sclk_spi1", "div_spi_pre1", SRC_MASK_PERIL1, 20,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI2, "sclk_spi2", "div_spi_pre2", SRC_MASK_PERIL1, 24,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_FIMC0, "fimc0", "aclk160", GATE_IP_CAM, 0,
			0, 0),
	GATE(CLK_FIMC1, "fimc1", "aclk160", GATE_IP_CAM, 1,
			0, 0),
	GATE(CLK_FIMC2, "fimc2", "aclk160", GATE_IP_CAM, 2,
			0, 0),
	GATE(CLK_FIMC3, "fimc3", "aclk160", GATE_IP_CAM, 3,
			0, 0),
	GATE(CLK_CSIS0, "csis0", "aclk160", GATE_IP_CAM, 4,
			0, 0),
	GATE(CLK_CSIS1, "csis1", "aclk160", GATE_IP_CAM, 5,
			0, 0),
	GATE(CLK_SMMU_FIMC0, "smmu_fimc0", "aclk160", GATE_IP_CAM, 7,
			0, 0),
	GATE(CLK_SMMU_FIMC1, "smmu_fimc1", "aclk160", GATE_IP_CAM, 8,
			0, 0),
	GATE(CLK_SMMU_FIMC2, "smmu_fimc2", "aclk160", GATE_IP_CAM, 9,
			0, 0),
	GATE(CLK_SMMU_FIMC3, "smmu_fimc3", "aclk160", GATE_IP_CAM, 10,
			0, 0),
	GATE(CLK_SMMU_JPEG, "smmu_jpeg", "aclk160", GATE_IP_CAM, 11,
			0, 0),
	GATE(CLK_PIXELASYNCM0, "pxl_async0", "aclk160", GATE_IP_CAM, 17, 0, 0),
	GATE(CLK_PIXELASYNCM1, "pxl_async1", "aclk160", GATE_IP_CAM, 18, 0, 0),
	GATE(CLK_SMMU_TV, "smmu_tv", "aclk160", GATE_IP_TV, 4,
			0, 0),
	GATE(CLK_MFC, "mfc", "aclk100", GATE_IP_MFC, 0, 0, 0),
	GATE(CLK_SMMU_MFCL, "smmu_mfcl", "aclk100", GATE_IP_MFC, 1,
			0, 0),
	GATE(CLK_SMMU_MFCR, "smmu_mfcr", "aclk100", GATE_IP_MFC, 2,
			0, 0),
	GATE(CLK_FIMD0, "fimd0", "aclk160", GATE_IP_LCD0, 0,
			0, 0),
	GATE(CLK_SMMU_FIMD0, "smmu_fimd0", "aclk160", GATE_IP_LCD0, 4,
			0, 0),
	GATE(CLK_PDMA0, "pdma0", "aclk133", GATE_IP_FSYS, 0,
			0, 0),
	GATE(CLK_PDMA1, "pdma1", "aclk133", GATE_IP_FSYS, 1,
			0, 0),
	GATE(CLK_SDMMC0, "sdmmc0", "aclk133", GATE_IP_FSYS, 5,
			0, 0),
	GATE(CLK_SDMMC1, "sdmmc1", "aclk133", GATE_IP_FSYS, 6,
			0, 0),
	GATE(CLK_SDMMC2, "sdmmc2", "aclk133", GATE_IP_FSYS, 7,
			0, 0),
	GATE(CLK_SDMMC3, "sdmmc3", "aclk133", GATE_IP_FSYS, 8,
			0, 0),
	GATE(CLK_UART0, "uart0", "aclk100", GATE_IP_PERIL, 0,
			0, 0),
	GATE(CLK_UART1, "uart1", "aclk100", GATE_IP_PERIL, 1,
			0, 0),
	GATE(CLK_UART2, "uart2", "aclk100", GATE_IP_PERIL, 2,
			0, 0),
	GATE(CLK_UART3, "uart3", "aclk100", GATE_IP_PERIL, 3,
			0, 0),
	GATE(CLK_UART4, "uart4", "aclk100", GATE_IP_PERIL, 4,
			0, 0),
	GATE(CLK_I2C0, "i2c0", "aclk100", GATE_IP_PERIL, 6,
			0, 0),
	GATE(CLK_I2C1, "i2c1", "aclk100", GATE_IP_PERIL, 7,
			0, 0),
	GATE(CLK_I2C2, "i2c2", "aclk100", GATE_IP_PERIL, 8,
			0, 0),
	GATE(CLK_I2C3, "i2c3", "aclk100", GATE_IP_PERIL, 9,
			0, 0),
	GATE(CLK_I2C4, "i2c4", "aclk100", GATE_IP_PERIL, 10,
			0, 0),
	GATE(CLK_I2C5, "i2c5", "aclk100", GATE_IP_PERIL, 11,
			0, 0),
	GATE(CLK_I2C6, "i2c6", "aclk100", GATE_IP_PERIL, 12,
			0, 0),
	GATE(CLK_I2C7, "i2c7", "aclk100", GATE_IP_PERIL, 13,
			0, 0),
	GATE(CLK_I2C_HDMI, "i2c-hdmi", "aclk100", GATE_IP_PERIL, 14,
			0, 0),
	GATE(CLK_SPI0, "spi0", "aclk100", GATE_IP_PERIL, 16,
			0, 0),
	GATE(CLK_SPI1, "spi1", "aclk100", GATE_IP_PERIL, 17,
			0, 0),
	GATE(CLK_SPI2, "spi2", "aclk100", GATE_IP_PERIL, 18,
			0, 0),
	GATE(CLK_I2S1, "i2s1", "aclk100", GATE_IP_PERIL, 20,
			0, 0),
	GATE(CLK_I2S2, "i2s2", "aclk100", GATE_IP_PERIL, 21,
			0, 0),
	GATE(CLK_PCM1, "pcm1", "aclk100", GATE_IP_PERIL, 22,
			0, 0),
	GATE(CLK_PCM2, "pcm2", "aclk100", GATE_IP_PERIL, 23,
			0, 0),
	GATE(CLK_SPDIF, "spdif", "aclk100", GATE_IP_PERIL, 26,
			0, 0),
	GATE(CLK_AC97, "ac97", "aclk100", GATE_IP_PERIL, 27,
			0, 0),
};

/* list of gate clocks supported in exynos4210 soc */
static struct samsung_gate_clock exynos4210_gate_clks[] __initdata = {
	GATE(CLK_TVENC, "tvenc", "aclk160", GATE_IP_TV, 2, 0, 0),
	GATE(CLK_G2D, "g2d", "aclk200", E4210_GATE_IP_IMAGE, 0, 0, 0),
	GATE(CLK_ROTATOR, "rotator", "aclk200", E4210_GATE_IP_IMAGE, 1, 0, 0),
	GATE(CLK_MDMA, "mdma", "aclk200", E4210_GATE_IP_IMAGE, 2, 0, 0),
	GATE(CLK_SMMU_G2D, "smmu_g2d", "aclk200", E4210_GATE_IP_IMAGE, 3, 0, 0),
	GATE(CLK_SMMU_MDMA, "smmu_mdma", "aclk200", E4210_GATE_IP_IMAGE, 5, 0,
		0),
	GATE(CLK_PCIE_PHY, "pcie_phy", "aclk133", GATE_IP_FSYS, 2, 0, 0),
	GATE(CLK_SATA_PHY, "sata_phy", "aclk133", GATE_IP_FSYS, 3, 0, 0),
	GATE(CLK_SATA, "sata", "aclk133", GATE_IP_FSYS, 10, 0, 0),
	GATE(CLK_PCIE, "pcie", "aclk133", GATE_IP_FSYS, 14, 0, 0),
	GATE(CLK_SMMU_PCIE, "smmu_pcie", "aclk133", GATE_IP_FSYS, 18, 0, 0),
	GATE(CLK_MODEMIF, "modemif", "aclk100", GATE_IP_PERIL, 28, 0, 0),
	GATE(CLK_CHIPID, "chipid", "aclk100", E4210_GATE_IP_PERIR, 0, 0, 0),
	GATE(CLK_SYSREG, "sysreg", "aclk100", E4210_GATE_IP_PERIR, 0,
			CLK_IGNORE_UNUSED, 0),
	GATE(CLK_HDMI_CEC, "hdmi_cec", "aclk100", E4210_GATE_IP_PERIR, 11, 0,
		0),
	GATE(CLK_SMMU_ROTATOR, "smmu_rotator", "aclk200",
			E4210_GATE_IP_IMAGE, 4, 0, 0),
	GATE(CLK_SCLK_MIPI1, "sclk_mipi1", "div_mipi_pre1",
			E4210_SRC_MASK_LCD1, 12, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SATA, "sclk_sata", "div_sata",
			SRC_MASK_FSYS, 24, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MIXER, "sclk_mixer", "mout_mixer", SRC_MASK_TV, 4, 0, 0),
	GATE(CLK_SCLK_DAC, "sclk_dac", "mout_dac", SRC_MASK_TV, 8, 0, 0),
	GATE(CLK_TSADC, "tsadc", "aclk100", GATE_IP_PERIL, 15,
			0, 0),
	GATE(CLK_MCT, "mct", "aclk100", E4210_GATE_IP_PERIR, 13,
			0, 0),
	GATE(CLK_WDT, "watchdog", "aclk100", E4210_GATE_IP_PERIR, 14,
			0, 0),
	GATE(CLK_RTC, "rtc", "aclk100", E4210_GATE_IP_PERIR, 15,
			0, 0),
	GATE(CLK_KEYIF, "keyif", "aclk100", E4210_GATE_IP_PERIR, 16,
			0, 0),
	GATE(CLK_SCLK_FIMD1, "sclk_fimd1", "div_fimd1", E4210_SRC_MASK_LCD1, 0,
			CLK_SET_RATE_PARENT, 0),
	GATE(CLK_TMU_APBIF, "tmu_apbif", "aclk100", E4210_GATE_IP_PERIR, 17, 0,
		0),
};

/* list of gate clocks supported in exynos4x12 soc */
static struct samsung_gate_clock exynos4x12_gate_clks[] __initdata = {
	GATE(CLK_AUDSS, "audss", "sclk_epll", E4X12_GATE_IP_MAUDIO, 0, 0, 0),
	GATE(CLK_MDNIE0, "mdnie0", "aclk160", GATE_IP_LCD0, 2, 0, 0),
	GATE(CLK_ROTATOR, "rotator", "aclk200", E4X12_GATE_IP_IMAGE, 1, 0, 0),
	GATE(CLK_MDMA, "mdma", "aclk200", E4X12_GATE_IP_IMAGE, 2, 0, 0),
	GATE(CLK_SMMU_MDMA, "smmu_mdma", "aclk200", E4X12_GATE_IP_IMAGE, 5, 0,
		0),
	GATE(CLK_MIPI_HSI, "mipi_hsi", "aclk133", GATE_IP_FSYS, 10, 0, 0),
	GATE(CLK_CHIPID, "chipid", "aclk100", E4X12_GATE_IP_PERIR, 0, 0, 0),
	GATE(CLK_SYSREG, "sysreg", "aclk100", E4X12_GATE_IP_PERIR, 1,
			CLK_IGNORE_UNUSED, 0),
	GATE(CLK_HDMI_CEC, "hdmi_cec", "aclk100", E4X12_GATE_IP_PERIR, 11, 0,
		0),
	GATE(CLK_SCLK_MDNIE0, "sclk_mdnie0", "div_mdnie0",
			SRC_MASK_LCD0, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MDNIE_PWM0, "sclk_mdnie_pwm0", "div_mdnie_pwm_pre0",
			SRC_MASK_LCD0, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MIPIHSI, "sclk_mipihsi", "div_mipihsi",
			SRC_MASK_FSYS, 24, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SMMU_ROTATOR, "smmu_rotator", "aclk200",
			E4X12_GATE_IP_IMAGE, 4, 0, 0),
	GATE(CLK_MCT, "mct", "aclk100", E4X12_GATE_IP_PERIR, 13,
			0, 0),
	GATE(CLK_RTC, "rtc", "aclk100", E4X12_GATE_IP_PERIR, 15,
			0, 0),
	GATE(CLK_KEYIF, "keyif", "aclk100", E4X12_GATE_IP_PERIR, 16, 0, 0),
	GATE(CLK_SCLK_PWM_ISP, "sclk_pwm_isp", "div_pwm_isp",
			E4X12_SRC_MASK_ISP, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0_ISP, "sclk_spi0_isp", "div_spi0_isp_pre",
			E4X12_SRC_MASK_ISP, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI1_ISP, "sclk_spi1_isp", "div_spi1_isp_pre",
			E4X12_SRC_MASK_ISP, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART_ISP, "sclk_uart_isp", "div_uart_isp",
			E4X12_SRC_MASK_ISP, 12, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_PWM_ISP_SCLK, "pwm_isp_sclk", "sclk_pwm_isp",
			E4X12_GATE_IP_ISP, 0, 0, 0),
	GATE(CLK_SPI0_ISP_SCLK, "spi0_isp_sclk", "sclk_spi0_isp",
			E4X12_GATE_IP_ISP, 1, 0, 0),
	GATE(CLK_SPI1_ISP_SCLK, "spi1_isp_sclk", "sclk_spi1_isp",
			E4X12_GATE_IP_ISP, 2, 0, 0),
	GATE(CLK_UART_ISP_SCLK, "uart_isp_sclk", "sclk_uart_isp",
			E4X12_GATE_IP_ISP, 3, 0, 0),
	GATE(CLK_WDT, "watchdog", "aclk100", E4X12_GATE_IP_PERIR, 14, 0, 0),
	GATE(CLK_PCM0, "pcm0", "aclk100", E4X12_GATE_IP_MAUDIO, 2,
			0, 0),
	GATE(CLK_I2S0, "i2s0", "aclk100", E4X12_GATE_IP_MAUDIO, 3,
			0, 0),
	GATE(CLK_FIMC_ISP, "isp", "aclk200", E4X12_GATE_ISP0, 0,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_FIMC_DRC, "drc", "aclk200", E4X12_GATE_ISP0, 1,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_FIMC_FD, "fd", "aclk200", E4X12_GATE_ISP0, 2,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_FIMC_LITE0, "lite0", "aclk200", E4X12_GATE_ISP0, 3,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_FIMC_LITE1, "lite1", "aclk200", E4X12_GATE_ISP0, 4,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_MCUISP, "mcuisp", "aclk200", E4X12_GATE_ISP0, 5,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_GICISP, "gicisp", "aclk200", E4X12_GATE_ISP0, 7,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_SMMU_ISP, "smmu_isp", "aclk200", E4X12_GATE_ISP0, 8,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_SMMU_DRC, "smmu_drc", "aclk200", E4X12_GATE_ISP0, 9,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_SMMU_FD, "smmu_fd", "aclk200", E4X12_GATE_ISP0, 10,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_SMMU_LITE0, "smmu_lite0", "aclk200", E4X12_GATE_ISP0, 11,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_SMMU_LITE1, "smmu_lite1", "aclk200", E4X12_GATE_ISP0, 12,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_PPMUISPMX, "ppmuispmx", "aclk200", E4X12_GATE_ISP0, 20,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_PPMUISPX, "ppmuispx", "aclk200", E4X12_GATE_ISP0, 21,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_MCUCTL_ISP, "mcuctl_isp", "aclk200", E4X12_GATE_ISP0, 23,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_MPWM_ISP, "mpwm_isp", "aclk200", E4X12_GATE_ISP0, 24,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_I2C0_ISP, "i2c0_isp", "aclk200", E4X12_GATE_ISP0, 25,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_I2C1_ISP, "i2c1_isp", "aclk200", E4X12_GATE_ISP0, 26,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_MTCADC_ISP, "mtcadc_isp", "aclk200", E4X12_GATE_ISP0, 27,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_PWM_ISP, "pwm_isp", "aclk200", E4X12_GATE_ISP0, 28,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_WDT_ISP, "wdt_isp", "aclk200", E4X12_GATE_ISP0, 30,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_UART_ISP, "uart_isp", "aclk200", E4X12_GATE_ISP0, 31,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_ASYNCAXIM, "asyncaxim", "aclk200", E4X12_GATE_ISP1, 0,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_SMMU_ISPCX, "smmu_ispcx", "aclk200", E4X12_GATE_ISP1, 4,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_SPI0_ISP, "spi0_isp", "aclk200", E4X12_GATE_ISP1, 12,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_SPI1_ISP, "spi1_isp", "aclk200", E4X12_GATE_ISP1, 13,
			CLK_IGNORE_UNUSED | CLK_GET_RATE_NOCACHE, 0),
	GATE(CLK_G2D, "g2d", "aclk200", GATE_IP_DMC, 23, 0, 0),
	GATE(CLK_TMU_APBIF, "tmu_apbif", "aclk100", E4X12_GATE_IP_PERIR, 17, 0,
		0),
};

static struct samsung_clock_alias exynos4_aliases[] __initdata = {
	ALIAS(CLK_MOUT_CORE, NULL, "moutcore"),
	ALIAS(CLK_ARM_CLK, NULL, "armclk"),
	ALIAS(CLK_SCLK_APLL, NULL, "mout_apll"),
};

static struct samsung_clock_alias exynos4210_aliases[] __initdata = {
	ALIAS(CLK_SCLK_MPLL, NULL, "mout_mpll"),
};

static struct samsung_clock_alias exynos4x12_aliases[] __initdata = {
	ALIAS(CLK_MOUT_MPLL_USER_C, NULL, "mout_mpll"),
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

static void __init exynos4_clk_register_finpll(struct samsung_clk_provider *ctx)
{
	struct samsung_fixed_rate_clock fclk;
	struct clk *clk;
	unsigned long finpll_f = 24000000;
	char *parent_name;
	unsigned int xom = exynos4_get_xom();

	parent_name = xom & 1 ? "xusbxti" : "xxti";
	clk = clk_get(NULL, parent_name);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to lookup parent clock %s, assuming "
			"fin_pll clock frequency is 24MHz\n", __func__,
			parent_name);
	} else {
		finpll_f = clk_get_rate(clk);
	}

	fclk.id = CLK_FIN_PLL;
	fclk.name = "fin_pll";
	fclk.parent_name = NULL;
	fclk.flags = CLK_IS_ROOT;
	fclk.fixed_rate = finpll_f;
	samsung_clk_register_fixed_rate(ctx, &fclk, 1);

}

static struct of_device_id ext_clk_match[] __initdata = {
	{ .compatible = "samsung,clock-xxti", .data = (void *)0, },
	{ .compatible = "samsung,clock-xusbxti", .data = (void *)1, },
	{},
};

/* PLLs PMS values */
static struct samsung_pll_rate_table exynos4210_apll_rates[] __initdata = {
	PLL_45XX_RATE(1200000000, 150,  3, 1, 28),
	PLL_45XX_RATE(1000000000, 250,  6, 1, 28),
	PLL_45XX_RATE( 800000000, 200,  6, 1, 28),
	PLL_45XX_RATE( 666857142, 389, 14, 1, 13),
	PLL_45XX_RATE( 600000000, 100,  4, 1, 13),
	PLL_45XX_RATE( 533000000, 533, 24, 1,  5),
	PLL_45XX_RATE( 500000000, 250,  6, 2, 28),
	PLL_45XX_RATE( 400000000, 200,  6, 2, 28),
	PLL_45XX_RATE( 200000000, 200,  6, 3, 28),
	{ /* sentinel */ }
};

static struct samsung_pll_rate_table exynos4210_epll_rates[] __initdata = {
	PLL_4600_RATE(192000000, 48, 3, 1,     0, 0),
	PLL_4600_RATE(180633605, 45, 3, 1, 10381, 0),
	PLL_4600_RATE(180000000, 45, 3, 1,     0, 0),
	PLL_4600_RATE( 73727996, 73, 3, 3, 47710, 1),
	PLL_4600_RATE( 67737602, 90, 4, 3, 20762, 1),
	PLL_4600_RATE( 49151992, 49, 3, 3,  9961, 0),
	PLL_4600_RATE( 45158401, 45, 3, 3, 10381, 0),
	{ /* sentinel */ }
};

static struct samsung_pll_rate_table exynos4210_vpll_rates[] __initdata = {
	PLL_4650_RATE(360000000, 44, 3, 0, 1024, 0, 14, 0),
	PLL_4650_RATE(324000000, 53, 2, 1, 1024, 1,  1, 1),
	PLL_4650_RATE(259617187, 63, 3, 1, 1950, 0, 20, 1),
	PLL_4650_RATE(110000000, 53, 3, 2, 2048, 0, 17, 0),
	PLL_4650_RATE( 55360351, 53, 3, 3, 2417, 0, 17, 0),
	{ /* sentinel */ }
};

static struct samsung_pll_rate_table exynos4x12_apll_rates[] __initdata = {
	PLL_35XX_RATE(1500000000, 250, 4, 0),
	PLL_35XX_RATE(1400000000, 175, 3, 0),
	PLL_35XX_RATE(1300000000, 325, 6, 0),
	PLL_35XX_RATE(1200000000, 200, 4, 0),
	PLL_35XX_RATE(1100000000, 275, 6, 0),
	PLL_35XX_RATE(1000000000, 125, 3, 0),
	PLL_35XX_RATE( 900000000, 150, 4, 0),
	PLL_35XX_RATE( 800000000, 100, 3, 0),
	PLL_35XX_RATE( 700000000, 175, 3, 1),
	PLL_35XX_RATE( 600000000, 200, 4, 1),
	PLL_35XX_RATE( 500000000, 125, 3, 1),
	PLL_35XX_RATE( 400000000, 100, 3, 1),
	PLL_35XX_RATE( 300000000, 200, 4, 2),
	PLL_35XX_RATE( 200000000, 100, 3, 2),
	{ /* sentinel */ }
};

static struct samsung_pll_rate_table exynos4x12_epll_rates[] __initdata = {
	PLL_36XX_RATE(192000000, 48, 3, 1,     0),
	PLL_36XX_RATE(180633605, 45, 3, 1, 10381),
	PLL_36XX_RATE(180000000, 45, 3, 1,     0),
	PLL_36XX_RATE( 73727996, 73, 3, 3, 47710),
	PLL_36XX_RATE( 67737602, 90, 4, 3, 20762),
	PLL_36XX_RATE( 49151992, 49, 3, 3,  9961),
	PLL_36XX_RATE( 45158401, 45, 3, 3, 10381),
	{ /* sentinel */ }
};

static struct samsung_pll_rate_table exynos4x12_vpll_rates[] __initdata = {
	PLL_36XX_RATE(533000000, 133, 3, 1, 16384),
	PLL_36XX_RATE(440000000, 110, 3, 1,     0),
	PLL_36XX_RATE(350000000, 175, 3, 2,     0),
	PLL_36XX_RATE(266000000, 133, 3, 2,     0),
	PLL_36XX_RATE(160000000, 160, 3, 3,     0),
	PLL_36XX_RATE(106031250,  53, 3, 2,  1024),
	PLL_36XX_RATE( 53015625,  53, 3, 3,  1024),
	{ /* sentinel */ }
};

static struct samsung_pll_clock exynos4210_plls[nr_plls] __initdata = {
	[apll] = PLL_A(pll_4508, CLK_FOUT_APLL, "fout_apll", "fin_pll",
		APLL_LOCK, APLL_CON0, "fout_apll", NULL),
	[mpll] = PLL_A(pll_4508, CLK_FOUT_MPLL, "fout_mpll", "fin_pll",
		E4210_MPLL_LOCK, E4210_MPLL_CON0, "fout_mpll", NULL),
	[epll] = PLL_A(pll_4600, CLK_FOUT_EPLL, "fout_epll", "fin_pll",
		EPLL_LOCK, EPLL_CON0, "fout_epll", NULL),
	[vpll] = PLL_A(pll_4650c, CLK_FOUT_VPLL, "fout_vpll", "mout_vpllsrc",
		VPLL_LOCK, VPLL_CON0, "fout_vpll", NULL),
};

static struct samsung_pll_clock exynos4x12_plls[nr_plls] __initdata = {
	[apll] = PLL(pll_35xx, CLK_FOUT_APLL, "fout_apll", "fin_pll",
			APLL_LOCK, APLL_CON0, NULL),
	[mpll] = PLL(pll_35xx, CLK_FOUT_MPLL, "fout_mpll", "fin_pll",
			E4X12_MPLL_LOCK, E4X12_MPLL_CON0, NULL),
	[epll] = PLL(pll_36xx, CLK_FOUT_EPLL, "fout_epll", "fin_pll",
			EPLL_LOCK, EPLL_CON0, NULL),
	[vpll] = PLL(pll_36xx, CLK_FOUT_VPLL, "fout_vpll", "fin_pll",
			VPLL_LOCK, VPLL_CON0, NULL),
};

/* register exynos4 clocks */
static void __init exynos4_clk_init(struct device_node *np,
				    enum exynos4_soc soc)
{
	struct samsung_clk_provider *ctx;
	exynos4_soc = soc;

	reg_base = of_iomap(np, 0);
	if (!reg_base)
		panic("%s: failed to map registers\n", __func__);

	ctx = samsung_clk_init(np, reg_base, CLK_NR_CLKS);
	if (!ctx)
		panic("%s: unable to allocate context.\n", __func__);

	samsung_clk_of_register_fixed_ext(ctx, exynos4_fixed_rate_ext_clks,
			ARRAY_SIZE(exynos4_fixed_rate_ext_clks),
			ext_clk_match);

	exynos4_clk_register_finpll(ctx);

	if (exynos4_soc == EXYNOS4210) {
		samsung_clk_register_mux(ctx, exynos4210_mux_early,
					ARRAY_SIZE(exynos4210_mux_early));

		if (_get_rate("fin_pll") == 24000000) {
			exynos4210_plls[apll].rate_table =
							exynos4210_apll_rates;
			exynos4210_plls[epll].rate_table =
							exynos4210_epll_rates;
		}

		if (_get_rate("mout_vpllsrc") == 24000000)
			exynos4210_plls[vpll].rate_table =
							exynos4210_vpll_rates;

		samsung_clk_register_pll(ctx, exynos4210_plls,
					ARRAY_SIZE(exynos4210_plls), reg_base);
	} else {
		if (_get_rate("fin_pll") == 24000000) {
			exynos4x12_plls[apll].rate_table =
							exynos4x12_apll_rates;
			exynos4x12_plls[epll].rate_table =
							exynos4x12_epll_rates;
			exynos4x12_plls[vpll].rate_table =
							exynos4x12_vpll_rates;
		}

		samsung_clk_register_pll(ctx, exynos4x12_plls,
					ARRAY_SIZE(exynos4x12_plls), reg_base);
	}

	samsung_clk_register_fixed_rate(ctx, exynos4_fixed_rate_clks,
			ARRAY_SIZE(exynos4_fixed_rate_clks));
	samsung_clk_register_mux(ctx, exynos4_mux_clks,
			ARRAY_SIZE(exynos4_mux_clks));
	samsung_clk_register_div(ctx, exynos4_div_clks,
			ARRAY_SIZE(exynos4_div_clks));
	samsung_clk_register_gate(ctx, exynos4_gate_clks,
			ARRAY_SIZE(exynos4_gate_clks));

	if (exynos4_soc == EXYNOS4210) {
		samsung_clk_register_fixed_rate(ctx, exynos4210_fixed_rate_clks,
			ARRAY_SIZE(exynos4210_fixed_rate_clks));
		samsung_clk_register_mux(ctx, exynos4210_mux_clks,
			ARRAY_SIZE(exynos4210_mux_clks));
		samsung_clk_register_div(ctx, exynos4210_div_clks,
			ARRAY_SIZE(exynos4210_div_clks));
		samsung_clk_register_gate(ctx, exynos4210_gate_clks,
			ARRAY_SIZE(exynos4210_gate_clks));
		samsung_clk_register_alias(ctx, exynos4210_aliases,
			ARRAY_SIZE(exynos4210_aliases));
	} else {
		samsung_clk_register_mux(ctx, exynos4x12_mux_clks,
			ARRAY_SIZE(exynos4x12_mux_clks));
		samsung_clk_register_div(ctx, exynos4x12_div_clks,
			ARRAY_SIZE(exynos4x12_div_clks));
		samsung_clk_register_gate(ctx, exynos4x12_gate_clks,
			ARRAY_SIZE(exynos4x12_gate_clks));
		samsung_clk_register_alias(ctx, exynos4x12_aliases,
			ARRAY_SIZE(exynos4x12_aliases));
	}

	samsung_clk_register_alias(ctx, exynos4_aliases,
			ARRAY_SIZE(exynos4_aliases));

	exynos4_clk_sleep_init();

	pr_info("%s clocks: sclk_apll = %ld, sclk_mpll = %ld\n"
		"\tsclk_epll = %ld, sclk_vpll = %ld, arm_clk = %ld\n",
		exynos4_soc == EXYNOS4210 ? "Exynos4210" : "Exynos4x12",
		_get_rate("sclk_apll"),	_get_rate("sclk_mpll"),
		_get_rate("sclk_epll"), _get_rate("sclk_vpll"),
		_get_rate("arm_clk"));
}


static void __init exynos4210_clk_init(struct device_node *np)
{
	exynos4_clk_init(np, EXYNOS4210);
}
CLK_OF_DECLARE(exynos4210_clk, "samsung,exynos4210-clock", exynos4210_clk_init);

static void __init exynos4412_clk_init(struct device_node *np)
{
	exynos4_clk_init(np, EXYNOS4X12);
}
CLK_OF_DECLARE(exynos4412_clk, "samsung,exynos4412-clock", exynos4412_clk_init);
