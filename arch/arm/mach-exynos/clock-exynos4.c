/* linux/arch/arm/mach-exynos/clock-exynos4.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - Clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/syscore_ops.h>

#include <plat/cpu-freq.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/s5p-clock.h>
#include <plat/clock-clksrc.h>
#include <plat/devs.h>
#include <plat/pm.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/regs-audss.h>
#include <mach/dev-sysmmu.h>
#include <mach/exynos-clock.h>
#include <mach/clock-domain.h>

#ifdef CONFIG_PM
static struct sleep_save exynos4_clock_save[] = {
	/* CMU side */
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_CAM),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_TV),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_LCD0),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_MAUDIO),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_FSYS),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_PERIL0),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_PERIL1),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_DMC),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_LEFTBUS),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_RIGHTBUS),
	SAVE_ITEM(EXYNOS4_CLKGATE_SCLKCAM),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_CAM),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_TV),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_MFC),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_G3D),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_LCD0),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_FSYS),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_GPS),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_PERIL),
	SAVE_ITEM(EXYNOS4_CLKGATE_BLOCK),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_DMC),
	SAVE_ITEM(EXYNOS4_CLKGATE_SCLKCPU),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_CPU),
	SAVE_ITEM(EXYNOS4_CLKDIV_LEFTBUS),
	SAVE_ITEM(EXYNOS4_CLKDIV_RIGHTBUS),
	SAVE_ITEM(EXYNOS4_CLKDIV_CAM),
	SAVE_ITEM(EXYNOS4_CLKDIV_TV),
	SAVE_ITEM(EXYNOS4_CLKDIV_MFC),
	SAVE_ITEM(EXYNOS4_CLKDIV_G3D),
	SAVE_ITEM(EXYNOS4_CLKDIV_LCD0),
	SAVE_ITEM(EXYNOS4_CLKDIV_MAUDIO),
	SAVE_ITEM(EXYNOS4_CLKDIV_FSYS0),
	SAVE_ITEM(EXYNOS4_CLKDIV_FSYS1),
	SAVE_ITEM(EXYNOS4_CLKDIV_FSYS2),
	SAVE_ITEM(EXYNOS4_CLKDIV_FSYS3),
	SAVE_ITEM(EXYNOS4_CLKDIV_PERIL0),
	SAVE_ITEM(EXYNOS4_CLKDIV_PERIL1),
	SAVE_ITEM(EXYNOS4_CLKDIV_PERIL2),
	SAVE_ITEM(EXYNOS4_CLKDIV_PERIL3),
	SAVE_ITEM(EXYNOS4_CLKDIV_PERIL4),
	SAVE_ITEM(EXYNOS4_CLKDIV_PERIL5),
	SAVE_ITEM(EXYNOS4_CLKDIV_TOP),
	SAVE_ITEM(EXYNOS4_CLKSRC_TOP0),
	SAVE_ITEM(EXYNOS4_CLKSRC_TOP1),
	SAVE_ITEM(EXYNOS4_CLKSRC_CAM),
	SAVE_ITEM(EXYNOS4_CLKSRC_MFC),
	SAVE_ITEM(EXYNOS4_CLKSRC_LCD0),
	SAVE_ITEM(EXYNOS4_CLKSRC_MAUDIO),
	SAVE_ITEM(EXYNOS4_CLKSRC_FSYS),
	SAVE_ITEM(EXYNOS4_CLKSRC_PERIL0),
	SAVE_ITEM(EXYNOS4_CLKSRC_PERIL1),
	SAVE_ITEM(EXYNOS4_CLKSRC_DMC),
	SAVE_ITEM(EXYNOS4_CLKSRC_CPU),
};
#endif

struct clk exynos4_clk_sclk_hdmi27m = {
	.name		= "sclk_hdmi27m",
	.rate		= 27000000,
};

struct clk exynos4_clk_sclk_hdmiphy = {
	.name		= "sclk_hdmiphy",
};

struct clk exynos4_clk_sclk_usbphy0 = {
	.name		= "sclk_usbphy0",
	.rate		= 27000000,
};

struct clk exynos4_clk_sclk_usbphy1 = {
	.name		= "sclk_usbphy1",
};

static struct clk exynos4_clk_audiocdclk1 = {
	.name           = "audiocdclk",
};

static struct clk exynos4_clk_audiocdclk2 = {
	.name		= "audiocdclk",
};

static struct clk exynos4_clk_spdifcdclk = {
	.name		= "spdifcdclk",
};

static int exynos4_clksrc_mask_top_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKSRC_MASK_TOP, clk, enable);
}

static int exynos4_clksrc_mask_cam_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKSRC_MASK_CAM, clk, enable);
}

static int exynos4_clksrc_mask_lcd0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKSRC_MASK_LCD0, clk, enable);
}

int exynos4_clksrc_mask_lcd1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKSRC_MASK_LCD1, clk, enable);
}

int exynos4_clksrc_mask_fsys_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKSRC_MASK_FSYS, clk, enable);
}

static int exynos4_clksrc_mask_peril0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKSRC_MASK_PERIL0, clk, enable);
}

static int exynos4_clksrc_mask_peril1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKSRC_MASK_PERIL1, clk, enable);
}

static int exynos4_clksrc_mask_tv_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKSRC_MASK_TV, clk, enable);
}

int exynos4_clk_ip_leftbus_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_LEFTBUS, clk, enable);
}

static int exynos4_clk_ip_mfc_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_MFC, clk, enable);
}

int exynos4_clk_ip_cam_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_CAM, clk, enable);
}

int exynos4_clk_ip_tv_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_TV, clk, enable);
}

static int exynos4_clk_ip_g3d_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_G3D, clk, enable);
}

int exynos4_clk_ip_image_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_IMAGE, clk, enable);
}

int exynos4_clk_ip_rightbus_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_RIGHTBUS, clk, enable);
}

static int exynos4_clk_ip_lcd0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_LCD0, clk, enable);
}

int exynos4_clk_ip_fsys_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_FSYS, clk, enable);
}

int exynos4_clk_ip_gps_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_GPS, clk, enable);
}

int exynos4_clk_ip_peril_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_PERIL, clk, enable);
}

int exynos4_clk_ip_perir_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_PERIR, clk, enable);
}

static int exynos4_clksrc_mask_maudio_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKSRC_MASK_MAUDIO, clk, enable);
}

static int exynos4_clk_audss_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_AUDSS, clk, enable);
}

static int __maybe_unused exynos4_clk_epll_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_EPLL_CON0, clk, enable);
}

static int exynos4_clk_vpll_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_VPLL_CON0, clk, enable);
}

int exynos4_clk_ip_dmc_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_DMC, clk, enable);
}

static int exynos4_clk_sclkapll_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_SCLKCPU, clk, enable);
}

static int exynos4_clk_ip_cpu_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_CPU, clk, enable);
}

static int exynos4_clk_hdmiphy_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_HDMI_PHY_CONTROL, clk, enable);
}

static int exynos4_clk_dac_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_DAC_PHY_CONTROL, clk, enable);
}

/* Core list of CMU_CPU side */

static struct clksrc_clk exynos4_clk_mout_apll = {
	.clk	= {
		.name		= "mout_apll",
	},
	.sources = &clk_src_apll,
	.reg_src = { .reg = EXYNOS4_CLKSRC_CPU, .shift = 0, .size = 1 },
};

struct clksrc_clk exynos4_clk_sclk_apll = {
	.clk	= {
		.name		= "sclk_apll",
		.parent		= &exynos4_clk_mout_apll.clk,
		.enable		= exynos4_clk_sclkapll_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_CPU, .shift = 24, .size = 3 },
};

struct clksrc_clk exynos4_clk_audiocdclk0 = {
	.clk	= {
		.name		= "audiocdclk",
		.rate		= 16934400,
	},
};

struct clksrc_clk exynos4_clk_mout_epll = {
	.clk	= {
		.name		= "mout_epll",
		.parent = &clk_fout_epll,
	},
	.sources = &clk_src_epll,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP0, .shift = 4, .size = 1 },
};

struct clksrc_clk exynos4_clk_mout_mpll = {
	.clk = {
		.name		= "mout_mpll",
	},
	.sources = &clk_src_mpll,
	/* reg_src will be added in SoC's clock */
};

static struct clk *exynos4_clkset_moutcore_list[] = {
	[0] = &exynos4_clk_mout_apll.clk,
	[1] = &exynos4_clk_mout_mpll.clk,
};

static struct clksrc_sources exynos4_clkset_moutcore = {
	.sources	= exynos4_clkset_moutcore_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_moutcore_list),
};

static struct clksrc_clk exynos4_clk_moutcore = {
	.clk	= {
		.name		= "moutcore",
	},
	.sources = &exynos4_clkset_moutcore,
	.reg_src = { .reg = EXYNOS4_CLKSRC_CPU, .shift = 16, .size = 1 },
};

static struct clksrc_clk exynos4_clk_coreclk = {
	.clk	= {
		.name		= "core_clk",
		.parent		= &exynos4_clk_moutcore.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_CPU, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos4_clk_armclk = {
	.clk	= {
		.name		= "armclk",
		.parent		= &exynos4_clk_coreclk.clk,
	},
};

static struct clksrc_clk exynos4_clk_aclk_corem0 = {
	.clk	= {
		.name		= "aclk_corem0",
		.parent		= &exynos4_clk_coreclk.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_CPU, .shift = 4, .size = 3 },
};

static struct clksrc_clk exynos4_clk_aclk_cores = {
	.clk	= {
		.name		= "aclk_cores",
		.parent		= &exynos4_clk_coreclk.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_CPU, .shift = 4, .size = 3 },
};

static struct clksrc_clk exynos4_clk_aclk_corem1 = {
	.clk	= {
		.name		= "aclk_corem1",
		.parent		= &exynos4_clk_coreclk.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_CPU, .shift = 8, .size = 3 },
};

static struct clksrc_clk exynos4_clk_periphclk = {
	.clk	= {
		.name		= "periphclk",
		.parent		= &exynos4_clk_coreclk.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_CPU, .shift = 12, .size = 3 },
};

/* Core list of CMU_CORE side */

struct clk *exynos4_clkset_corebus_list[] = {
	[0] = &exynos4_clk_mout_mpll.clk,
	[1] = &exynos4_clk_sclk_apll.clk,
};

struct clksrc_sources exynos4_clkset_mout_corebus = {
	.sources	= exynos4_clkset_corebus_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_corebus_list),
};

static struct clksrc_clk exynos4_clk_mout_corebus = {
	.clk	= {
		.name		= "mout_corebus",
	},
	.sources = &exynos4_clkset_mout_corebus,
	.reg_src = { .reg = EXYNOS4_CLKSRC_DMC, .shift = 4, .size = 1 },
};

static struct clksrc_clk exynos4_clk_sclk_dmc = {
	.clk	= {
		.name		= "sclk_dmc",
		.parent		= &exynos4_clk_mout_corebus.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_DMC0, .shift = 12, .size = 3 },
};

static struct clksrc_clk exynos4_clk_aclk_cored = {
	.clk	= {
		.name		= "aclk_cored",
		.parent		= &exynos4_clk_sclk_dmc.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_DMC0, .shift = 16, .size = 3 },
};

static struct clksrc_clk exynos4_clk_aclk_corep = {
	.clk	= {
		.name		= "aclk_corep",
		.parent		= &exynos4_clk_aclk_cored.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_DMC0, .shift = 20, .size = 3 },
};

static struct clksrc_clk exynos4_clk_aclk_acp = {
	.clk	= {
		.name		= "aclk_acp",
		.parent		= &exynos4_clk_mout_corebus.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_DMC0, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos4_clk_pclk_acp = {
	.clk	= {
		.name		= "pclk_acp",
		.parent		= &exynos4_clk_aclk_acp.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_DMC0, .shift = 4, .size = 3 },
};

static struct clk *exynos4_clkset_c2c_list[] = {
	[0] = &exynos4_clk_mout_mpll.clk,
	[1] = &exynos4_clk_sclk_apll.clk,
};

static struct clksrc_sources exynos4_clkset_mout_c2c = {
	.sources	= exynos4_clkset_c2c_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_c2c_list),
};

static struct clksrc_clk exynos4_clk_sclk_c2c = {
	.clk	= {
		.name		= "sclk_c2c",
		.id		= -1,
	},
	.sources = &exynos4_clkset_mout_c2c,
	.reg_src = { .reg = EXYNOS4_CLKSRC_DMC, .shift = 0, .size = 1 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_DMC1, .shift = 4, .size = 3 },
};

static struct clksrc_clk exynos4_clk_aclk_c2c = {
	.clk	= {
		.name	= "aclk_c2c",
		.id	= -1,
		.parent	= &exynos4_clk_sclk_c2c.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_DMC1, .shift = 12, .size = 3 },
};

/* Core list of CMU_TOP side */

struct clk *exynos4_clkset_aclk_top_list[] = {
	[0] = &exynos4_clk_mout_mpll.clk,
	[1] = &exynos4_clk_sclk_apll.clk,
};

struct clksrc_sources exynos4_clkset_aclk = {
	.sources	= exynos4_clkset_aclk_top_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_aclk_top_list),
};

struct clksrc_clk exynos4_clk_aclk_200 = {
	.clk	= {
		.name		= "aclk_200",
	},
};

static struct clksrc_clk exynos4_clk_aclk_100 = {
	.clk	= {
		.name		= "aclk_100",
	},
	.sources = &exynos4_clkset_aclk,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP0, .shift = 16, .size = 1 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_TOP, .shift = 4, .size = 4 },
};

struct clksrc_clk exynos4_clk_aclk_160 = {
	.clk	= {
		.name		= "aclk_160",
	},
	.sources = &exynos4_clkset_aclk,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP0, .shift = 20, .size = 1 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_TOP, .shift = 8, .size = 3 },
};

struct clksrc_clk exynos4_clk_aclk_133 = {
	.clk	= {
		.name		= "aclk_133",
	},
	.sources = &exynos4_clkset_aclk,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP0, .shift = 24, .size = 1 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_TOP, .shift = 12, .size = 3 },
};

/* CMU_LEFT/RIGHTBUS side */
static struct clk *exynos4_clkset_aclk_lrbus_list[] = {
	[0] = &exynos4_clk_mout_mpll.clk,
	[1] = &exynos4_clk_sclk_apll.clk,
};

static struct clksrc_sources exynos4_clkset_aclk_lrbus = {
	.sources	= exynos4_clkset_aclk_lrbus_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_aclk_lrbus_list),
};

static struct clksrc_clk exynos4_clk_aclk_gdl = {
	.clk	= {
		.name		= "aclk_gdl",
	},
	.sources = &exynos4_clkset_aclk_lrbus,
	.reg_src = { .reg = EXYNOS4_CLKSRC_LEFTBUS, .shift = 0, .size = 1 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_LEFTBUS, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos4_clk_aclk_gdr = {
	.clk	= {
		.name		= "aclk_gdr",
	},
	.sources = &exynos4_clkset_aclk_lrbus,
	.reg_src = { .reg = EXYNOS4_CLKSRC_RIGHTBUS, .shift = 0, .size = 1 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_RIGHTBUS, .shift = 0, .size = 3 },
};

static struct clk *exynos4_clkset_vpllsrc_list[] = {
	[0] = &clk_fin_vpll,
	[1] = &exynos4_clk_sclk_hdmi27m,
};

static struct clksrc_sources exynos4_clkset_vpllsrc = {
	.sources	= exynos4_clkset_vpllsrc_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_vpllsrc_list),
};

static struct clksrc_clk exynos4_clk_vpllsrc = {
	.clk	= {
		.name		= "vpll_src",
		.enable		= exynos4_clksrc_mask_top_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos4_clkset_vpllsrc,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP1, .shift = 0, .size = 1 },
};

static struct clk *exynos4_clkset_sclk_vpll_list[] = {
	[0] = &exynos4_clk_vpllsrc.clk,
	[1] = &clk_fout_vpll,
};

static struct clksrc_sources exynos4_clkset_sclk_vpll = {
	.sources	= exynos4_clkset_sclk_vpll_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_sclk_vpll_list),
};

struct clksrc_clk exynos4_clk_sclk_vpll = {
	.clk	= {
		.name		= "sclk_vpll",
	},
	.sources = &exynos4_clkset_sclk_vpll,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP0, .shift = 8, .size = 1 },
};

static struct clk *exynos4_clkset_sclk_dac_list[] = {
	[0] = &exynos4_clk_sclk_vpll.clk,
	[1] = &exynos4_clk_sclk_hdmiphy,
};

static struct clksrc_sources exynos4_clkset_sclk_dac = {
	.sources	= exynos4_clkset_sclk_dac_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_sclk_dac_list),
};

static struct clksrc_clk exynos4_clk_sclk_dac = {
	.clk	= {
		.name		= "sclk_dac",
		.enable		= exynos4_clksrc_mask_tv_ctrl,
		.ctrlbit	= (1 << 8),
	},
	.sources = &exynos4_clkset_sclk_dac,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TV, .shift = 8, .size = 1 },
};

static struct clksrc_clk exynos4_clk_sclk_pixel = {
	.clk	= {
		.name		= "sclk_pixel",
		.parent		= &exynos4_clk_sclk_vpll.clk,
	},
	.reg_div	= { .reg = EXYNOS4_CLKDIV_TV, .shift = 0, .size = 4 },
};

static struct clk *exynos4_clkset_sclk_hdmi_list[] = {
	[0] = &exynos4_clk_sclk_pixel.clk,
	[1] = &exynos4_clk_sclk_hdmiphy,
};

static struct clksrc_sources exynos4_clkset_sclk_hdmi = {
	.sources	= exynos4_clkset_sclk_hdmi_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_sclk_hdmi_list),
};

static struct clksrc_clk exynos4_clk_sclk_hdmi = {
	.clk	= {
		.name		= "sclk_hdmi",
		.enable		= exynos4_clksrc_mask_tv_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos4_clkset_sclk_hdmi,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TV, .shift = 0, .size = 1 },
};

static struct clk *exynos4_clkset_sclk_mixer_list[] = {
	[0] = &exynos4_clk_sclk_dac.clk,
	[1] = &exynos4_clk_sclk_hdmi.clk,
};

static struct clksrc_sources exynos4_clkset_sclk_mixer = {
	.sources	= exynos4_clkset_sclk_mixer_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_sclk_mixer_list),
};

static struct clksrc_clk exynos4_clk_sclk_mixer = {
	.clk    = {
		.name           = "sclk_mixer",
		.id             = -1,
		.enable         = exynos4_clksrc_mask_tv_ctrl,
		.ctrlbit        = (1 << 4),
	},
	.sources = &exynos4_clkset_sclk_mixer,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TV, .shift = 4, .size = 1 },
};

static struct clksrc_clk *exynos4_sclk_tv[] = {
	&exynos4_clk_sclk_dac,
	&exynos4_clk_sclk_pixel,
	&exynos4_clk_sclk_hdmi,
	&exynos4_clk_sclk_mixer,
};

static struct clk exynos4_init_clocks_off[] = {
	{
		.name		= "ppmuright",
		.enable		= exynos4_clk_ip_rightbus_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "ppmuleft",
		.enable		= exynos4_clk_ip_leftbus_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "timers",
		.parent		= &exynos4_clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1<<24),
	}, {
		.name		= "csis",
		.devname	= "s3c-csis.0",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "csis",
		.devname	= "s3c-csis.1",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "fimc",
		.devname	= "s3c-fimc.0",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "fimc",
		.devname	= "s3c-fimc.1",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "fimc",
		.devname	= "s3c-fimc.2",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "fimc",
		.devname	= "s3c-fimc.3",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "jpeg",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= ((1 << 11) | (1 << 6)),
	}, {
		.name		= "pxl_async0",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "pxl_async1",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "ppmucamif",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "jpeg",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "rotator",
		.devname	= "exynos-rot",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.0",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.1",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.2",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.3",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "dwmci",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "adc",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "keypad",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "rtc",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "watchdog",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "hdmicec",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "sromc",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "usbhost",
		.enable		= exynos4_clk_ip_fsys_ctrl ,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "usbotg",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "spi",
		.devname	= "s3c64xx-spi.0",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "spi",
		.devname	= "s3c64xx-spi.1",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "spi",
		.devname	= "s3c64xx-spi.2",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.1",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 20),
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.2",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 21),
	}, {
		.name		= "pcm",
		.devname	= "samsung-pcm.1",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 22),
	}, {
		.name		= "pcm",
		.devname	= "samsung-pcm.2",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 23),
	}, {
		.name		= "slimbus",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 25),
	}, {
		.name		= "spdif",
		.devname	= "samsung-spdif",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 26),
	}, {
		.name		= "ac97",
		.devname	= "samsung-ac97",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 27),
	}, {
		.name		= "i2c-hdmiphy",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "ppmutv",
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "hdmi",
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "dac",
		.devname        = "s5p-sdo",
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "mixer",
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "vp",
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "ppmuimage",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "qerotator",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "rotator",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name           = "hdmiphy",
		.devname        = "exynos4-hdmi",
		.enable         = exynos4_clk_hdmiphy_ctrl,
		.ctrlbit        = (1 << 0),
	}, {
		.name           = "dacphy",
		.devname        = "s5p-sdo",
		.enable         = exynos4_clk_dac_ctrl,
		.ctrlbit        = (1 << 0),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(sss, 0),
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(fimc0, 1),
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(fimc1, 2),
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(fimc2, 3),
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(fimc3, 4),
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(jpeg, 5),
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(fimd0, 6),
		.enable		= exynos4_clk_ip_lcd0_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(rot, 10),
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "sysmmu",

		.devname	= SYSMMU_CLOCK_NAME(mdma, 11),
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(tv, 12),
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(mfc_l, 13),
		.enable		= exynos4_clk_ip_mfc_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(mfc_r, 14),
		.enable		= exynos4_clk_ip_mfc_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(gps, 16),
		.enable		= exynos4_clk_ip_gps_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "gps",
		.enable		= exynos4_clk_ip_gps_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "ppmumfc",
		.enable		= exynos4_clk_ip_mfc_ctrl,
		.ctrlbit	= ((0x1 << 4) | (0x1 << 3)),
	}, {
		.name		= "mfc",
		.devname	= "s3c-mfc",
		.enable		= exynos4_clk_ip_mfc_ctrl,
		.ctrlbit	= (0x1 << 0),
	}, {
		.name		= "tsi",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "onenand",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "nfcon",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "ppmufsys",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "ppmug3d",
		.enable		= exynos4_clk_ip_g3d_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "ppmucam",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "hpm",
		.enable		= exynos4_clk_ip_cpu_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "iec",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "apc",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "ppmuacp",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "ppmucpu",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "ppmudmc1",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "ppmudmc0",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 8),
#ifdef CONFIG_CPU_EXYNOS4210
	}, {
		.name		= "qesss",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "id_remapper",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "qecpu",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "fbm_dmc1",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "seckey",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.5",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.4",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.3",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.2",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.1",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.0",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 5),
#endif
	}, {
		.name		= "qefimc",
		.devname	= "s5p-qe.3",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "qefimc",
		.devname	= "s5p-qe.2",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "qefimc",
		.devname	= "s5p-qe.1",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "qefimc",
		.devname	= "s5p-qe.0",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "qeg3d",
		.enable		= exynos4_clk_ip_g3d_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "secss",
		.parent		= &exynos4_clk_aclk_acp.clk,
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 4),
	},
};

static struct clk exynos4_i2cs_clocks[] = {
	{
		.name		= "i2c",
		.devname	= "s3c2440-i2c.0",
		.parent		= &exynos4_clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.1",
		.parent		= &exynos4_clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.2",
		.parent		= &exynos4_clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.3",
		.parent		= &exynos4_clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.4",
		.parent		= &exynos4_clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.5",
		.parent		= &exynos4_clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.6",
		.parent		= &exynos4_clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.7",
		.parent		= &exynos4_clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-hdmiphy-i2c",
		.parent		= &exynos4_clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 14),
	}
};

static struct clk *clkset_sclk_audio0_list[] = {
	[0] = &exynos4_clk_audiocdclk0.clk,
	[1] = NULL,
	[2] = &exynos4_clk_sclk_hdmi27m,
	[3] = &exynos4_clk_sclk_usbphy0,
	[4] = &clk_ext_xtal_mux,
	[5] = &clk_xusbxti,
	[6] = &exynos4_clk_mout_mpll.clk,
	[7] = &exynos4_clk_mout_epll.clk,
	[8] = &exynos4_clk_sclk_vpll.clk,
};

static struct clksrc_sources exynos4_clkset_sclk_audio0 = {
	.sources	= clkset_sclk_audio0_list,
	.nr_sources	= ARRAY_SIZE(clkset_sclk_audio0_list),
};

static struct clksrc_clk exynos4_clk_sclk_audio0 = {
	.clk	= {
		.name		= "audio-bus",
		.enable		= exynos4_clksrc_mask_maudio_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos4_clkset_sclk_audio0,
	.reg_src = { .reg = EXYNOS4_CLKSRC_MAUDIO, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_MAUDIO, .shift = 0, .size = 4 },
};

static struct clk *exynos4_clkset_mout_audss_list[] = {
	&clk_ext_xtal_mux,
	&clk_fout_epll,
};

static struct clksrc_sources clkset_mout_audss = {
	.sources	= exynos4_clkset_mout_audss_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_audss_list),
};

static struct clksrc_clk exynos4_clk_mout_audss = {
	.clk	= {
		.name		= "mout_audss",
	},
	.sources = &clkset_mout_audss,
	.reg_src = { .reg = S5P_CLKSRC_AUDSS, .shift = 0, .size = 1 },
};

static struct clk *exynos4_clkset_sclk_audss_list[] = {
	&exynos4_clk_mout_audss.clk,
	&exynos4_clk_audiocdclk0.clk,
	&exynos4_clk_sclk_audio0.clk,
};

static struct clksrc_sources exynos4_clkset_sclk_audss = {
	.sources	= exynos4_clkset_sclk_audss_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_sclk_audss_list),
};

static struct clksrc_clk exynos4_clk_sclk_audss_i2s = {
	.clk		= {
		.name		= "i2sclk",
		.parent		= &exynos4_clk_mout_audss.clk,
		.enable		= exynos4_clk_audss_ctrl,
		.ctrlbit	= S5P_AUDSS_CLKGATE_I2SSPECIAL,
	},
	.sources = &exynos4_clkset_sclk_audss,
	.reg_src = { .reg = S5P_CLKSRC_AUDSS, .shift = 2, .size = 2 },
	.reg_div = { .reg = S5P_CLKDIV_AUDSS, .shift = 8, .size = 4 },
};

static struct clksrc_clk exynos4_clk_dout_audss_srp = {
	.clk	= {
		.name		= "dout_srp",
		.parent		= &exynos4_clk_mout_audss.clk,
	},
	.reg_div = { .reg = S5P_CLKDIV_AUDSS, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos4_clk_sclk_audss_bus = {
	.clk	= {
		.name		= "busclk",
		.parent		= &exynos4_clk_dout_audss_srp.clk,
		.enable		= exynos4_clk_audss_ctrl,
		.ctrlbit	= S5P_AUDSS_CLKGATE_I2SBUS,
	},
	.reg_div = { .reg = S5P_CLKDIV_AUDSS, .shift = 4, .size = 4 },
};

static struct clk exynos4_init_audss_clocks[] = {
	{
		.name		= "srpclk",
		.enable		= exynos4_clk_audss_ctrl,
		.parent		= &exynos4_clk_dout_audss_srp.clk,
		.ctrlbit	= S5P_AUDSS_CLKGATE_RP | S5P_AUDSS_CLKGATE_UART
				| S5P_AUDSS_CLKGATE_TIMER,
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.0",
		.enable		= exynos4_clk_audss_ctrl,
		.ctrlbit	= S5P_AUDSS_CLKGATE_I2SSPECIAL | S5P_AUDSS_CLKGATE_I2SBUS,
	}, {
		.name		= "pcm",
		.devname	= "samsung-pcm.0",
		.enable		= exynos4_clk_audss_ctrl,
		.ctrlbit	= S5P_AUDSS_CLKGATE_PCMSPECIAL | S5P_AUDSS_CLKGATE_PCMBUS,
	},
};

static struct clk *exynos4_clkset_sclk_audio1_list[] = {
	[0] = &exynos4_clk_audiocdclk1,
	[1] = NULL,
	[2] = &exynos4_clk_sclk_hdmi27m,
	[3] = &exynos4_clk_sclk_usbphy0,
	[4] = &clk_ext_xtal_mux,
	[5] = &clk_xusbxti,
	[6] = &exynos4_clk_mout_mpll.clk,
	[7] = &exynos4_clk_mout_epll.clk,
	[8] = &exynos4_clk_sclk_vpll.clk,
};

static struct clksrc_sources exynos4_clkset_sclk_audio1 = {
	.sources	= exynos4_clkset_sclk_audio1_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_sclk_audio1_list),
};

static struct clksrc_clk exynos4_clk_sclk_audio1 = {
	.clk	= {
		.name		= "audio-bus1",
		.enable		= exynos4_clksrc_mask_peril1_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos4_clkset_sclk_audio1,
	.reg_src = { .reg = EXYNOS4_CLKSRC_PERIL1, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL4, .shift = 0, .size = 8 },
};

static struct clk *exynos4_clkset_sclk_audio2_list[] = {
	[0] = &exynos4_clk_audiocdclk2,
	[1] = NULL,
	[2] = &exynos4_clk_sclk_hdmi27m,
	[3] = &exynos4_clk_sclk_usbphy0,
	[4] = &clk_ext_xtal_mux,
	[5] = &clk_xusbxti,
	[6] = &exynos4_clk_mout_mpll.clk,
	[7] = &exynos4_clk_mout_epll.clk,
	[8] = &exynos4_clk_sclk_vpll.clk,
};

static struct clksrc_sources exynos4_clkset_sclk_audio2 = {
	.sources	= exynos4_clkset_sclk_audio2_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_sclk_audio2_list),
};

static struct clksrc_clk exynos4_clk_sclk_audio2 = {
	.clk	= {
		.name		= "audio-bus2",
		.enable		= exynos4_clksrc_mask_peril1_ctrl,
		.ctrlbit	= (1 << 4),
	},
	.sources = &exynos4_clkset_sclk_audio2,
	.reg_src = { .reg = EXYNOS4_CLKSRC_PERIL1, .shift = 4, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL4, .shift = 16, .size = 4 },
};

static struct clk *exynos4_clkset_sclk_spdif_list[] = {
	[0] = &exynos4_clk_sclk_audio0.clk,
	[1] = &exynos4_clk_sclk_audio1.clk,
	[2] = &exynos4_clk_sclk_audio2.clk,
	[3] = &exynos4_clk_spdifcdclk,
};

static struct clksrc_sources exynos4_clkset_sclk_spdif = {
	.sources	= exynos4_clkset_sclk_spdif_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_sclk_spdif_list),
};

static struct clksrc_clk exynos4_clk_sclk_spdif = {
	.clk	= {
		.name		= "sclk_spdif",
		.enable		= exynos4_clksrc_mask_peril1_ctrl,
		.ctrlbit	= (1 << 8),
		.ops		= &s5p_sclk_spdif_ops,
	},
	.sources = &exynos4_clkset_sclk_spdif,
	.reg_src = { .reg = EXYNOS4_CLKSRC_PERIL1, .shift = 8, .size = 2 },
};

struct clk exynos4_init_dmaclocks[] = {
	{
		.name		= "pdma",
		.devname	= "s3c-pl330.0",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= ((1 << 8) | (1 << 5) | (1 << 2)),
	}, {
		.name		= "pdma",
		.devname	= "s3c-pl330.1",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "pdma",
		.devname	= "s3c-pl330.2",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 1),
	},
};

static struct clk exynos4_init_clocks[] = {
	{
#ifndef CONFIG_CPU_EXYNOS4210
		.name		= "seckey",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.5",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.4",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.3",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.2",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.1",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "tzpc",
		.devname	= "exnos4-tzpc.0",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
#endif
		.name		= "cssys",
		.enable		= exynos4_clk_ip_cpu_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "gic",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 20),
#ifndef CONFIG_CPU_EXYNOS4210
	}, {
		.name		= "qesss",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "id_remapper",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "qecpu",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "fbm_dmc1",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 6),
#endif
	}, {
		.name		= "fbm_dmc0",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "int_comb",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "g3d",
		.enable		= exynos4_clk_ip_g3d_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "tmu",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "mct",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "cmu_top",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "pmu_apb",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "sysreg",
		.enable		= exynos4_clk_ip_perir_ctrl ,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "chipid",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "uart",
		.devname	= "s5pv210-uart.0",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "uart",
		.devname	= "s5pv210-uart.1",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "uart",
		.devname	= "s5pv210-uart.2",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "uart",
		.devname	= "s5pv210-uart.3",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "uart",
		.devname	= "s5pv210-uart.4",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
#if 1
		.name		= "lcd",
		.devname	= "s3cfb.0",
		.enable 	= exynos4_clk_ip_lcd0_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "mie0",
		.enable 	= exynos4_clk_ip_lcd0_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "mdnie0",
		.enable 	= exynos4_clk_ip_lcd0_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "dsim0",
		.enable 	= exynos4_clk_ip_lcd0_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "ppmulcd",
		.enable 	= exynos4_clk_ip_lcd0_ctrl,
		.ctrlbit	= (1 << 5),
	},
#endif
#ifdef CONFIG_INTERNAL_MODEM_IF
	{
		.name		= "modem",
		.id		= -1,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 28),
	},
#endif
};

struct clk *exynos4_clkset_group_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &clk_xusbxti,
	[2] = &exynos4_clk_sclk_hdmi27m,
	[3] = &exynos4_clk_sclk_usbphy0,
	[4] = &exynos4_clk_sclk_usbphy1,
	[5] = &exynos4_clk_sclk_hdmiphy,
	[6] = &exynos4_clk_mout_mpll.clk,
	[7] = &exynos4_clk_mout_epll.clk,
	[8] = &exynos4_clk_sclk_vpll.clk,
};

struct clksrc_sources exynos4_clkset_group = {
	.sources	= exynos4_clkset_group_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_group_list),
};

static struct clksrc_clk clk_sclk_mipidphy4l = {
	.clk    = {
		.name           = "sclk_mipidphy4l",
		.id             = -1,
		.enable         = exynos4_clksrc_mask_lcd0_ctrl,
		.ctrlbit        = (1 << 12),
	},
	.sources        = &exynos4_clkset_group,
	.reg_src        = { .reg = EXYNOS4_CLKSRC_LCD0, .shift = 12, .size = 4 },
	.reg_div	= { .reg = EXYNOS4_CLKDIV_LCD0, .shift = 16, .size = 4 },
};

static struct clksrc_clk clk_sclk_mipidphy2l = {
	.clk    = {
		.name           = "sclk_mipidphy2l",
		.id             = -1,
		.enable         = exynos4_clksrc_mask_lcd0_ctrl,
		.ctrlbit        = (1 << 12),
	},
	.sources        = &exynos4_clkset_group,
	.reg_src        = { .reg = EXYNOS4_CLKSRC_LCD1, .shift = 12, .size = 4 },
	.reg_div	= { .reg = EXYNOS4_CLKDIV_LCD1, .shift = 16, .size = 4 },
};

static struct clk *exynos4_clkset_mout_g2d0_list[] = {
	[0] = &exynos4_clk_mout_mpll.clk,
	[1] = &exynos4_clk_sclk_apll.clk,
};

static struct clksrc_sources exynos4_clkset_mout_g2d0 = {
	.sources	= exynos4_clkset_mout_g2d0_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_g2d0_list),
};

struct clksrc_clk exynos4_clk_mout_g2d0 = {
	.clk	= {
		.name		= "mout_g2d0",
	},
	.sources = &exynos4_clkset_mout_g2d0,
	.reg_src = { .reg = EXYNOS4_CLKSRC_IMAGE, .shift = 0, .size = 1 },
};

static struct clk *exynos4_clkset_mout_g2d1_list[] = {
	[0] = &exynos4_clk_mout_epll.clk,
	[1] = &exynos4_clk_sclk_vpll.clk,
};

struct clksrc_sources exynos4_clkset_mout_g2d1 = {
	.sources	= exynos4_clkset_mout_g2d1_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_g2d1_list),
};

struct clksrc_clk exynos4_clk_mout_g2d1 = {
	.clk	= {
		.name		= "mout_g2d1",
	},
	.sources = &exynos4_clkset_mout_g2d1,
	.reg_src = { .reg = EXYNOS4_CLKSRC_IMAGE, .shift = 4, .size = 1 },
};

static struct clk *exynos4_clkset_mout_g2d_list[] = {
	[0] = &exynos4_clk_mout_g2d0.clk,
	[1] = &exynos4_clk_mout_g2d1.clk,
};

static struct clksrc_sources exynos4_clkset_mout_g2d = {
	.sources	= exynos4_clkset_mout_g2d_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_g2d_list),
};

struct clksrc_clk exynos4_clk_sclk_fimg2d = {
	.clk    = {
		.name           = "sclk_fimg2d",
		.devname	= "s5p-fimg2d",
	},
	.sources = &exynos4_clkset_mout_g2d,
};

struct clk exynos4_clk_fimg2d = {
	.name           = "fimg2d",
	.devname	= "s5p-fimg2d",
};

struct clk *exynos4_clkset_mout_mfc0_list[] = {
	[0] = &exynos4_clk_mout_mpll.clk,
	[1] = &exynos4_clk_sclk_apll.clk,
};

static struct clksrc_sources exynos4_clkset_mout_mfc0 = {
	.sources	= exynos4_clkset_mout_mfc0_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_mfc0_list),
};

static struct clksrc_clk exynos4_clk_mout_mfc0 = {
	.clk	= {
		.name		= "mout_mfc0",
	},
	.sources = &exynos4_clkset_mout_mfc0,
	.reg_src = { .reg = EXYNOS4_CLKSRC_MFC, .shift = 0, .size = 1 },
};

static struct clk *exynos4_clkset_mout_mfc1_list[] = {
	[0] = &exynos4_clk_mout_epll.clk,
	[1] = &exynos4_clk_sclk_vpll.clk,
};

static struct clksrc_sources exynos4_clkset_mout_mfc1 = {
	.sources	= exynos4_clkset_mout_mfc1_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_mfc1_list),
};

static struct clksrc_clk exynos4_clk_mout_mfc1 = {
	.clk	= {
		.name		= "mout_mfc1",
	},
	.sources = &exynos4_clkset_mout_mfc1,
	.reg_src = { .reg = EXYNOS4_CLKSRC_MFC, .shift = 4, .size = 1 },
};

static struct clk *exynos4_clkset_mout_mfc_list[] = {
	[0] = &exynos4_clk_mout_mfc0.clk,
	[1] = &exynos4_clk_mout_mfc1.clk,
};

static struct clksrc_sources exynos4_clkset_mout_mfc = {
	.sources	= exynos4_clkset_mout_mfc_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_mfc_list),
};

static struct clk *exynos4_clkset_mout_g3d0_list[] = {
	[0] = &exynos4_clk_mout_mpll.clk,
	[1] = &exynos4_clk_sclk_apll.clk,
};

static struct clksrc_sources exynos4_clkset_mout_g3d0 = {
	.sources	= exynos4_clkset_mout_g3d0_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_g3d0_list),
};

static struct clksrc_clk exynos4_clk_mout_g3d0 = {
	.clk	= {
		.name		= "mout_g3d0",
	},
	.sources = &exynos4_clkset_mout_g3d0,
	.reg_src = { .reg = EXYNOS4_CLKSRC_G3D, .shift = 0, .size = 1 },
};

static struct clk *exynos4_clkset_mout_g3d1_list[] = {
	[0] = &exynos4_clk_mout_epll.clk,
	[1] = &exynos4_clk_sclk_vpll.clk,
};

static struct clksrc_sources exynos4_clkset_mout_g3d1 = {
	.sources	= exynos4_clkset_mout_g3d1_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_g3d1_list),
};

static struct clksrc_clk exynos4_clk_mout_g3d1 = {
	.clk	= {
		.name		= "mout_g3d1",
	},
	.sources = &exynos4_clkset_mout_g3d1,
	.reg_src = { .reg = EXYNOS4_CLKSRC_G3D, .shift = 4, .size = 1 },
};

static struct clk *exynos4_clkset_mout_g3d_list[] = {
	[0] = &exynos4_clk_mout_g3d0.clk,
	[1] = &exynos4_clk_mout_g3d1.clk,
};

static struct clksrc_sources exynos4_clkset_mout_g3d = {
	.sources	= exynos4_clkset_mout_g3d_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_g3d_list),
};

static struct clksrc_clk exynos4_clk_dout_mmc0 = {
	.clk		= {
		.name		= "dout_mmc0",
	},
	.sources = &exynos4_clkset_group,
	.reg_src = { .reg = EXYNOS4_CLKSRC_FSYS, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS1, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos4_clk_dout_mmc1 = {
	.clk		= {
		.name		= "dout_mmc1",
	},
	.sources = &exynos4_clkset_group,
	.reg_src = { .reg = EXYNOS4_CLKSRC_FSYS, .shift = 4, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS1, .shift = 16, .size = 4 },
};

static struct clksrc_clk exynos4_clk_dout_mmc2 = {
	.clk		= {
		.name		= "dout_mmc2",
	},
	.sources = &exynos4_clkset_group,
	.reg_src = { .reg = EXYNOS4_CLKSRC_FSYS, .shift = 8, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS2, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos4_clk_dout_mmc3 = {
	.clk		= {
		.name		= "dout_mmc3",
	},
	.sources = &exynos4_clkset_group,
	.reg_src = { .reg = EXYNOS4_CLKSRC_FSYS, .shift = 12, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS2, .shift = 16, .size = 4 },
};

static struct clksrc_clk exynos4_clk_dout_mmc4 = {
	.clk		= {
		.name		= "dout_mmc4",
	},
	.sources = &exynos4_clkset_group,
	.reg_src = { .reg = EXYNOS4_CLKSRC_FSYS, .shift = 16, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS3, .shift = 0, .size = 4 },
};

static struct clk *exynos4_clkset_mout_hpm_list[] = {
	[0] = &exynos4_clk_mout_apll.clk,
	[1] = &exynos4_clk_mout_mpll.clk,
};

static struct clksrc_sources exynos4_clkset_sclk_hpm = {
	.sources	= exynos4_clkset_mout_hpm_list,
	.nr_sources	= ARRAY_SIZE(exynos4_clkset_mout_hpm_list),
};

static struct clksrc_clk exynos4_clk_dout_copy = {
	.clk	= {
		.name		= "dout_copy",
	},
	.sources = &exynos4_clkset_sclk_hpm,
	.reg_src = { .reg = EXYNOS4_CLKSRC_CPU, .shift = 20, .size = 1 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_CPU1, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos4_clk_dout_spi0 = {
	.clk		= {
		.name		= "dout_spi0",
	},
	.sources = &exynos4_clkset_group,
	.reg_src = { .reg = EXYNOS4_CLKSRC_PERIL1, .shift = 16, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL1, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos4_clk_dout_spi1 = {
	.clk		= {
		.name		= "dout_spi1",
	},
	.sources = &exynos4_clkset_group,
	.reg_src = { .reg = EXYNOS4_CLKSRC_PERIL1, .shift = 20, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL1, .shift = 16, .size = 4 },
};

static struct clksrc_clk exynos4_clk_dout_spi2 = {
	.clk		= {
		.name		= "dout_spi2",
	},
	.sources = &exynos4_clkset_group,
	.reg_src = { .reg = EXYNOS4_CLKSRC_PERIL1, .shift = 24, .size = 4 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL2, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos4_clksrcs[] = {
	{
		.clk	= {
			.name		= "uclk1",
			.devname	= "s5pv210-uart.0",
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_PERIL0, .shift = 0, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL0, .shift = 0, .size = 4 },
	}, {
		.clk	= {
			.name		= "uclk1",
			.devname	= "s5pv210-uart.1",
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_PERIL0, .shift = 4, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL0, .shift = 4, .size = 4 },
	}, {
		.clk	= {
			.name		= "uclk1",
			.devname	= "s5pv210-uart.2",
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_PERIL0, .shift = 8, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL0, .shift = 8, .size = 4 },
	}, {
		.clk	= {
			.name		= "uclk1",
			.devname	= "s5pv210-uart.3",
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_PERIL0, .shift = 12, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL0, .shift = 12, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_csis",
			.devname	= "s3c-csis.0",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_CAM, .shift = 24, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_CAM, .shift = 24, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_csis",
			.devname	= "s3c-csis.1",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 28),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_CAM, .shift = 28, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_CAM, .shift = 28, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_cam0",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_CAM, .shift = 16, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_CAM, .shift = 16, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_cam1",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 20),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_CAM, .shift = 20, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_CAM, .shift = 20, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_fimc",
			.devname	= "s3c-fimc.0",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_CAM, .shift = 0, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_CAM, .shift = 0, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_fimc",
			.devname	= "s3c-fimc.1",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_CAM, .shift = 4, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_CAM, .shift = 4, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_fimc",
			.devname	= "s3c-fimc.2",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_CAM, .shift = 8, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_CAM, .shift = 8, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_fimc",
			.devname	= "s3c-fimc.3",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_CAM, .shift = 12, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_CAM, .shift = 12, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_fimd",
			.devname	= "s3cfb.0",
			.enable		= exynos4_clksrc_mask_lcd0_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_LCD0, .shift = 0, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_LCD0, .shift = 0, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_fimd",
			.devname	= "s3cfb.1",
			.enable		= exynos4_clksrc_mask_lcd1_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_LCD1, .shift = 0, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_LCD1, .shift = 0, .size = 4 },
#if defined(CONFIG_FB_S5P_MDNIE) || defined(CONFIG_MDNIE_SUPPORT)
	}, {
		.clk		= {
			.name		= "sclk_mdnie",
			.id		= -1,
			.enable		= exynos4_clksrc_mask_lcd0_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_LCD0, .shift = 4, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_LCD0, .shift = 4, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_mdnie",
			.id		= 1,
			.enable		= exynos4_clksrc_mask_lcd1_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_LCD1, .shift = 4, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_LCD1, .shift = 4, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_mdnie_pwm",
			.id		= -1,
			.enable		= exynos4_clksrc_mask_lcd0_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_LCD0, .shift = 8, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_LCD0, .shift = 8, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_mdnie_pwm",
			.id		= 1,
			.enable		= exynos4_clksrc_mask_lcd1_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_LCD1, .shift = 8, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_LCD1, .shift = 8, .size = 4 },
#endif
#ifdef CONFIG_FB_MDNIE_PWM
	}, {
		.clk		= {
			.name		= "sclk_mdnie_pwm_pre",
			.id		= -1,
			.enable		= exynos4_clksrc_mask_lcd0_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_LCD0, .shift = 8, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_LCD0, .shift = 12, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_mdnie_pwm_pre",
			.id		= 1,
			.enable		= exynos4_clksrc_mask_lcd1_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_LCD1, .shift = 8, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_LCD1, .shift = 12, .size = 4 },
#endif
	}, {
		.clk		= {
			.name		= "sclk_mipi",
#ifdef CONFIG_FB_S5P_MIPI_DSIM
			.id		= -1,
#else
			.id		= 0,
#endif
			.parent		= &clk_sclk_mipidphy4l.clk,
			.enable		= exynos4_clksrc_mask_lcd0_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_LCD0, .shift = 20, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_mipi",
			.id		= 1,
			.parent		= &clk_sclk_mipidphy2l.clk,
			.enable		= exynos4_clksrc_mask_lcd1_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_LCD1, .shift = 20, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.id		= 0,
			.parent		= &exynos4_clk_dout_mmc0.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS1, .shift = 8, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.id		= 1,
			.parent         = &exynos4_clk_dout_mmc1.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS1, .shift = 24, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.id		= 2,
			.parent         = &exynos4_clk_dout_mmc2.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS2, .shift = 8, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.id		= 3,
			.parent         = &exynos4_clk_dout_mmc3.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS2, .shift = 24, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_dwmci",
			.id		= -1,
			.parent         = &exynos4_clk_dout_mmc4.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS3, .shift = 8, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_spi",
			.devname	= "s3c64xx-spi.0",
			.parent		= &exynos4_clk_dout_spi0.clk,
			.enable		= exynos4_clksrc_mask_peril1_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL1, .shift = 8, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_spi",
			.devname	= "s3c64xx-spi.1",
			.parent		= &exynos4_clk_dout_spi1.clk,
			.enable		= exynos4_clksrc_mask_peril1_ctrl,
			.ctrlbit	= (1 << 20),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL1, .shift = 24, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_spi",
			.devname	= "s3c64xx-spi.2",
			.parent		= &exynos4_clk_dout_spi2.clk,
			.enable		= exynos4_clksrc_mask_peril1_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL2, .shift = 8, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_mfc",
		},
		.sources = &exynos4_clkset_mout_mfc,
		.reg_src = { .reg = EXYNOS4_CLKSRC_MFC, .shift = 8, .size = 1 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_MFC, .shift = 0, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_g3d",
			.enable		= exynos4_clk_ip_g3d_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos4_clkset_mout_g3d,
		.reg_src = { .reg = EXYNOS4_CLKSRC_G3D, .shift = 8, .size = 1 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_G3D, .shift = 0, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_mmc",
			.devname	= "s3c-sdhci.0",
			.parent		= &exynos4_clk_dout_mmc0.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS1, .shift = 8, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_mmc",
			.devname	= "s3c-sdhci.1",
			.parent         = &exynos4_clk_dout_mmc1.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS1, .shift = 24, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_mmc",
			.devname	= "s3c-sdhci.2",
			.parent         = &exynos4_clk_dout_mmc2.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS2, .shift = 8, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_mmc",
			.devname	= "s3c-sdhci.3",
			.parent         = &exynos4_clk_dout_mmc3.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS2, .shift = 24, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_dwmci",
			.parent         = &exynos4_clk_dout_mmc4.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS3, .shift = 8, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_pcm",
			.devname	= "samsung-pcm.0",
			.parent		= &exynos4_clk_sclk_audio0.clk,
		},
			.reg_div = { .reg = EXYNOS4_CLKDIV_MAUDIO, .shift = 4, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_pcm",
			.devname	= "samsung-pcm.1",
			.parent		= &exynos4_clk_sclk_audio1.clk,
		},
			.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL4, .shift = 4, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_pcm",
			.devname        = "samsung-pcm.2",
			.parent		= &exynos4_clk_sclk_audio2.clk,
		},
			.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL4, .shift = 20, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_i2s",
			.parent		= &exynos4_clk_sclk_audio1.clk,
		},
			.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL5, .shift = 0, .size = 6 },
	}, {
		.clk	= {
			.name		= "sclk_i2s",
			.parent		= &exynos4_clk_sclk_audio2.clk,
		},
			.reg_div = { .reg = EXYNOS4_CLKDIV_PERIL5, .shift = 8, .size = 6 },
	}, {
		.clk	= {
			.name		= "sclk_hpm",
			.parent		= &exynos4_clk_dout_copy.clk,
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_CPU1, .shift = 4, .size = 3 },
	}, {
		.clk	= {
			.name		= "sclk_pwi",
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_DMC, .shift = 16, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_DMC1, .shift = 8, .size = 4 },
	},
};

/* Clock initialization code */
static struct clksrc_clk *exynos4_sysclks[] = {
	&exynos4_clk_audiocdclk0,
	&exynos4_clk_mout_apll,
	&exynos4_clk_sclk_apll,
	&exynos4_clk_mout_epll,
	&exynos4_clk_mout_mpll,
	&exynos4_clk_moutcore,
	&exynos4_clk_coreclk,
	&exynos4_clk_armclk,
	&exynos4_clk_aclk_corem0,
	&exynos4_clk_aclk_cores,
	&exynos4_clk_aclk_corem1,
	&exynos4_clk_periphclk,
	&exynos4_clk_mout_corebus,
	&exynos4_clk_sclk_dmc,
	&exynos4_clk_aclk_cored,
	&exynos4_clk_aclk_corep,
	&exynos4_clk_aclk_acp,
	&exynos4_clk_pclk_acp,
	&exynos4_clk_vpllsrc,
	&exynos4_clk_sclk_vpll,
	&exynos4_clk_aclk_200,
	&exynos4_clk_aclk_100,
	&exynos4_clk_aclk_160,
	&exynos4_clk_aclk_133,
	&exynos4_clk_aclk_gdl,
	&exynos4_clk_aclk_gdr,
	&exynos4_clk_mout_mfc0,
	&exynos4_clk_mout_mfc1,
	&exynos4_clk_dout_mmc0,
	&exynos4_clk_dout_mmc1,
	&exynos4_clk_dout_mmc2,
	&exynos4_clk_dout_mmc3,
	&exynos4_clk_dout_mmc4,
	&exynos4_clk_mout_audss,
	&exynos4_clk_sclk_audss_bus,
	&exynos4_clk_sclk_audss_i2s,
	&exynos4_clk_dout_audss_srp,
	&exynos4_clk_sclk_audio0,
	&exynos4_clk_sclk_audio1,
	&exynos4_clk_sclk_audio2,
	&exynos4_clk_sclk_spdif,
	&exynos4_clk_mout_g2d0,
	&exynos4_clk_mout_g2d1,
	&exynos4_clk_dout_copy,
	&exynos4_clk_mout_g3d0,
	&exynos4_clk_mout_g3d1,
	&exynos4_clk_dout_spi0,
	&exynos4_clk_dout_spi1,
	&exynos4_clk_dout_spi2,
#ifdef CONFIG_CPU_EXYNOS4212
	&exynos4_clk_sclk_c2c,
	&exynos4_clk_aclk_c2c,
#endif
	&exynos4_clk_sclk_fimg2d,
};

struct clk_ops exynos4_epll_ops;
struct clk_ops exynos4_vpll_ops;

static int xtal_rate;

static unsigned long exynos4_fout_apll_get_rate(struct clk *clk)
{
	if (soc_is_exynos4210())
		return s5p_get_pll45xx(xtal_rate, __raw_readl(EXYNOS4_APLL_CON0), pll_4508);
	else
		return s5p_get_pll35xx(xtal_rate, __raw_readl(EXYNOS4_APLL_CON0));
}

static struct clk_ops exynos4_fout_apll_ops = {
	.get_rate = exynos4_fout_apll_get_rate,
};

void __init_or_cpufreq exynos4_setup_clocks(void)
{
	struct clk *xtal_clk;
	unsigned long apll;
	unsigned long mpll;
	unsigned long epll;
	unsigned long vpll;
	unsigned long vpllsrc;
	unsigned long xtal;
	unsigned long armclk;
	unsigned long sclk_dmc;
	unsigned long aclk_200;
	unsigned long aclk_160;
	unsigned long aclk_133;
	unsigned long aclk_100;
	unsigned int ptr;

	printk(KERN_DEBUG "%s: registering clocks\n", __func__);

	xtal_clk = clk_get(NULL, "xtal");
	BUG_ON(IS_ERR(xtal_clk));

	xtal = clk_get_rate(xtal_clk);

	xtal_rate = xtal;

	clk_put(xtal_clk);

	printk(KERN_DEBUG "%s: xtal is %ld\n", __func__, xtal);

	if (soc_is_exynos4210()) {
		apll = s5p_get_pll45xx(xtal, __raw_readl(EXYNOS4_APLL_CON0), pll_4508);
		mpll = s5p_get_pll45xx(xtal, __raw_readl(EXYNOS4_MPLL_CON0), pll_4508);
		epll = s5p_get_pll46xx(xtal, __raw_readl(EXYNOS4_EPLL_CON0),
				__raw_readl(EXYNOS4_EPLL_CON1), pll_4600);

		vpllsrc = clk_get_rate(&exynos4_clk_vpllsrc.clk);
		vpll = s5p_get_pll46xx(vpllsrc, __raw_readl(EXYNOS4_VPLL_CON0),
				__raw_readl(EXYNOS4_VPLL_CON1), pll_4650c);
	} else {
		apll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS4_APLL_CON0));
		mpll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS4_MPLL_CON0));
		epll = s5p_get_pll36xx(xtal, __raw_readl(EXYNOS4_EPLL_CON0),
				__raw_readl(EXYNOS4_EPLL_CON1));

		vpllsrc = clk_get_rate(&exynos4_clk_vpllsrc.clk);
		vpll = s5p_get_pll36xx(vpllsrc, __raw_readl(EXYNOS4_VPLL_CON0),
				__raw_readl(EXYNOS4_VPLL_CON1));
	}

	clk_fout_apll.ops = &exynos4_fout_apll_ops;
	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;
	clk_fout_vpll.rate = vpll;

	printk(KERN_INFO "EXYNOS4: PLL settings, A=%ld, M=%ld, E=%ld V=%ld",
			apll, mpll, epll, vpll);

	armclk = clk_get_rate(&exynos4_clk_armclk.clk);
	sclk_dmc = clk_get_rate(&exynos4_clk_sclk_dmc.clk);

	aclk_200 = clk_get_rate(&exynos4_clk_aclk_200.clk);
	aclk_100 = clk_get_rate(&exynos4_clk_aclk_100.clk);
	aclk_160 = clk_get_rate(&exynos4_clk_aclk_160.clk);
	aclk_133 = clk_get_rate(&exynos4_clk_aclk_133.clk);

	printk(KERN_INFO "EXYNOS4: ARMCLK=%ld, DMC=%ld, ACLK200=%ld\n"
			"ACLK160=%ld, ACLK133=%ld, ACLK100=%ld\n",
			armclk, sclk_dmc, aclk_200,
			aclk_160, aclk_133, aclk_100);
#ifdef CONFIG_CPU_EXYNOS4212
	printk(KERN_INFO "EXYNOS4: ACLK400=%ld ACLK266=%ld\n",
			clk_get_rate(&exynos4212_clk_aclk_400_mcuisp.clk), clk_get_rate(&exynos4212_clk_aclk_266.clk));
#endif

	clk_f.rate = armclk;
	clk_h.rate = sclk_dmc;
	clk_p.rate = aclk_100;

	clk_fout_epll.ops = &exynos4_epll_ops;

#ifdef CONFIG_EXYNOS4_MSHC_SUPPORT_PQPRIME_EPLL
	/* This is code for support PegasusQ Prime dynamically */
	if (soc_is_exynos4412() && (samsung_rev() >= EXYNOS4412_REV_2_0)) {
		/* PegasusQ Prime use EPLL rather than MPLL */
		if (clk_set_parent(&exynos4_clk_dout_mmc4.clk, &exynos4_clk_mout_epll.clk))
			printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos4_clk_mout_epll.clk.name, exynos4_clk_dout_mmc4.clk.name);
	}
#endif
#ifdef CONFIG_EXYNOS4_MSHC_EPLL_45MHZ
	if (clk_set_parent(&exynos4_clk_dout_mmc4.clk, &exynos4_clk_mout_epll.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				 exynos4_clk_mout_epll.clk.name, exynos4_clk_dout_mmc4.clk.name);
#endif
#ifdef CONFIG_EXYNOS4_MSHC_VPLL_46MHZ
	if (clk_set_parent(&exynos4_clk_dout_mmc4.clk, &exynos4_clk_sclk_vpll.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos4_clk_sclk_vpll.clk.name, exynos4_clk_dout_mmc4.clk.name);
	if (clk_set_parent(&exynos4_clk_sclk_vpll.clk, &exynos4_clk_fout_vpll))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos4_clk_fout_vpll.clk.name, exynos4_clk_sclk_vpll.clk.name);
#endif
	if (clk_set_parent(&exynos4_clk_mout_epll.clk, &clk_fout_epll))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				clk_fout_epll.name, exynos4_clk_mout_epll.clk.name);

	clk_fout_vpll.enable = exynos4_clk_vpll_ctrl;
	clk_fout_vpll.ops = &exynos4_vpll_ops;

	clk_set_rate(&exynos4_clk_sclk_apll.clk, 100000000);

	for (ptr = 0; ptr < ARRAY_SIZE(exynos4_clksrcs); ptr++)
		s3c_set_clksrc(&exynos4_clksrcs[ptr], true);
}

static struct clk *exynos4_clks[] __initdata = {
	&exynos4_clk_sclk_hdmi27m,
	&exynos4_clk_sclk_hdmiphy,
};

#ifdef CONFIG_PM
static int exynos4_clock_suspend(void)
{
	unsigned int tmp;

	if (!soc_is_exynos4210()) {
		tmp = __raw_readl(EXYNOS4_CLKSRC_TOP1);
		tmp &= ~(0x1 << EXYNOS4_CLKDIV_TOP1_ACLK200_SUB_SHIFT |
			0x1 << EXYNOS4_CLKDIV_TOP1_ACLK400_MCUISP_SUB_SHIFT);
		__raw_writel(tmp, EXYNOS4_CLKSRC_TOP1);
	}

	s3c_pm_do_save(exynos4_clock_save, ARRAY_SIZE(exynos4_clock_save));

	return 0;
}

static void exynos4_clock_resume(void)
{
	s3c_pm_do_restore_core(exynos4_clock_save, ARRAY_SIZE(exynos4_clock_save));
}
#else
#define exynos4_clock_suspend NULL
#define exynos4_clock_resume NULL
#endif

struct syscore_ops exynos4_clock_syscore_ops = {
	.suspend        = exynos4_clock_suspend,
	.resume         = exynos4_clock_resume,
};

void __init exynos4_register_clocks(void)
{
	int ptr;

	s3c24xx_register_clocks(exynos4_clks, ARRAY_SIZE(exynos4_clks));

	for (ptr = 0; ptr < ARRAY_SIZE(exynos4_sysclks); ptr++)
		s3c_register_clksrc(exynos4_sysclks[ptr], 1);

	for (ptr = 0; ptr < ARRAY_SIZE(exynos4_sclk_tv); ptr++)
		s3c_register_clksrc(exynos4_sclk_tv[ptr], 1);

	s3c_register_clksrc(exynos4_clksrcs, ARRAY_SIZE(exynos4_clksrcs));
	s3c_register_clocks(exynos4_init_clocks, ARRAY_SIZE(exynos4_init_clocks));

	s3c_register_clocks(exynos4_init_clocks_off, ARRAY_SIZE(exynos4_init_clocks_off));
	s3c_disable_clocks(exynos4_init_clocks_off, ARRAY_SIZE(exynos4_init_clocks_off));

	s3c_register_clocks(exynos4_init_audss_clocks, ARRAY_SIZE(exynos4_init_audss_clocks));
	s3c_disable_clocks(exynos4_init_audss_clocks, ARRAY_SIZE(exynos4_init_audss_clocks));

	/* Register DMA Clock */
	s3c_register_clocks(exynos4_init_dmaclocks, ARRAY_SIZE(exynos4_init_dmaclocks));
	s3c_disable_clocks(exynos4_init_dmaclocks, ARRAY_SIZE(exynos4_init_dmaclocks));
	s3c_register_clocks(exynos4_i2cs_clocks, ARRAY_SIZE(exynos4_i2cs_clocks));
	s3c_disable_clocks(exynos4_i2cs_clocks, ARRAY_SIZE(exynos4_i2cs_clocks));

	s3c_register_clocks(&exynos4_clk_fimg2d, 1);
	s3c_disable_clocks(&exynos4_clk_fimg2d, 1);

	register_syscore_ops(&exynos4_clock_syscore_ops);
	s3c_pwmclk_init();
}

static int __init clock_domain_init(void)
{
	int index;

	clock_add_domain(LPA_DOMAIN, &exynos4_init_dmaclocks[0]);
	clock_add_domain(LPA_DOMAIN, &exynos4_init_dmaclocks[1]);
	clock_add_domain(LPA_DOMAIN, &exynos4_init_dmaclocks[2]);
	for (index = 0; index < ARRAY_SIZE(exynos4_i2cs_clocks); index++)
		clock_add_domain(LPA_DOMAIN, &exynos4_i2cs_clocks[index]);

	return 0;
}
late_initcall(clock_domain_init);
