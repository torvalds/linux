/* linux/arch/arm/mach-exynos4/clock.c
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
#include <plat/exynos4.h>
#include <plat/pm.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/sysmmu.h>
#include <mach/exynos4-clock.h>

static struct sleep_save exynos4_clock_save[] = {
	SAVE_ITEM(S5P_CLKDIV_LEFTBUS),
	SAVE_ITEM(S5P_CLKGATE_IP_LEFTBUS),
	SAVE_ITEM(S5P_CLKDIV_RIGHTBUS),
	SAVE_ITEM(S5P_CLKGATE_IP_RIGHTBUS),
	SAVE_ITEM(S5P_CLKSRC_TOP0),
	SAVE_ITEM(S5P_CLKSRC_TOP1),
	SAVE_ITEM(S5P_CLKSRC_CAM),
	SAVE_ITEM(S5P_CLKSRC_TV),
	SAVE_ITEM(S5P_CLKSRC_MFC),
	SAVE_ITEM(S5P_CLKSRC_G3D),
	SAVE_ITEM(S5P_CLKSRC_LCD0),
	SAVE_ITEM(S5P_CLKSRC_MAUDIO),
	SAVE_ITEM(S5P_CLKSRC_FSYS),
	SAVE_ITEM(S5P_CLKSRC_PERIL0),
	SAVE_ITEM(S5P_CLKSRC_PERIL1),
	SAVE_ITEM(S5P_CLKDIV_CAM),
	SAVE_ITEM(S5P_CLKDIV_TV),
	SAVE_ITEM(S5P_CLKDIV_MFC),
	SAVE_ITEM(S5P_CLKDIV_G3D),
	SAVE_ITEM(S5P_CLKDIV_LCD0),
	SAVE_ITEM(S5P_CLKDIV_MAUDIO),
	SAVE_ITEM(S5P_CLKDIV_FSYS0),
	SAVE_ITEM(S5P_CLKDIV_FSYS1),
	SAVE_ITEM(S5P_CLKDIV_FSYS2),
	SAVE_ITEM(S5P_CLKDIV_FSYS3),
	SAVE_ITEM(S5P_CLKDIV_PERIL0),
	SAVE_ITEM(S5P_CLKDIV_PERIL1),
	SAVE_ITEM(S5P_CLKDIV_PERIL2),
	SAVE_ITEM(S5P_CLKDIV_PERIL3),
	SAVE_ITEM(S5P_CLKDIV_PERIL4),
	SAVE_ITEM(S5P_CLKDIV_PERIL5),
	SAVE_ITEM(S5P_CLKDIV_TOP),
	SAVE_ITEM(S5P_CLKSRC_MASK_TOP),
	SAVE_ITEM(S5P_CLKSRC_MASK_CAM),
	SAVE_ITEM(S5P_CLKSRC_MASK_TV),
	SAVE_ITEM(S5P_CLKSRC_MASK_LCD0),
	SAVE_ITEM(S5P_CLKSRC_MASK_MAUDIO),
	SAVE_ITEM(S5P_CLKSRC_MASK_FSYS),
	SAVE_ITEM(S5P_CLKSRC_MASK_PERIL0),
	SAVE_ITEM(S5P_CLKSRC_MASK_PERIL1),
	SAVE_ITEM(S5P_CLKDIV2_RATIO),
	SAVE_ITEM(S5P_CLKGATE_SCLKCAM),
	SAVE_ITEM(S5P_CLKGATE_IP_CAM),
	SAVE_ITEM(S5P_CLKGATE_IP_TV),
	SAVE_ITEM(S5P_CLKGATE_IP_MFC),
	SAVE_ITEM(S5P_CLKGATE_IP_G3D),
	SAVE_ITEM(S5P_CLKGATE_IP_LCD0),
	SAVE_ITEM(S5P_CLKGATE_IP_FSYS),
	SAVE_ITEM(S5P_CLKGATE_IP_GPS),
	SAVE_ITEM(S5P_CLKGATE_IP_PERIL),
	SAVE_ITEM(S5P_CLKGATE_BLOCK),
	SAVE_ITEM(S5P_CLKSRC_MASK_DMC),
	SAVE_ITEM(S5P_CLKSRC_DMC),
	SAVE_ITEM(S5P_CLKDIV_DMC0),
	SAVE_ITEM(S5P_CLKDIV_DMC1),
	SAVE_ITEM(S5P_CLKGATE_IP_DMC),
	SAVE_ITEM(S5P_CLKSRC_CPU),
	SAVE_ITEM(S5P_CLKDIV_CPU),
	SAVE_ITEM(S5P_CLKDIV_CPU + 0x4),
	SAVE_ITEM(S5P_CLKGATE_SCLKCPU),
	SAVE_ITEM(S5P_CLKGATE_IP_CPU),
};

struct clk clk_sclk_hdmi27m = {
	.name		= "sclk_hdmi27m",
	.rate		= 27000000,
};

struct clk clk_sclk_hdmiphy = {
	.name		= "sclk_hdmiphy",
};

struct clk clk_sclk_usbphy0 = {
	.name		= "sclk_usbphy0",
	.rate		= 27000000,
};

struct clk clk_sclk_usbphy1 = {
	.name		= "sclk_usbphy1",
};

static struct clk dummy_apb_pclk = {
	.name		= "apb_pclk",
	.id		= -1,
};

static int exynos4_clksrc_mask_top_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKSRC_MASK_TOP, clk, enable);
}

static int exynos4_clksrc_mask_cam_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKSRC_MASK_CAM, clk, enable);
}

static int exynos4_clksrc_mask_lcd0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKSRC_MASK_LCD0, clk, enable);
}

int exynos4_clksrc_mask_fsys_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKSRC_MASK_FSYS, clk, enable);
}

static int exynos4_clksrc_mask_peril0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKSRC_MASK_PERIL0, clk, enable);
}

static int exynos4_clksrc_mask_peril1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKSRC_MASK_PERIL1, clk, enable);
}

static int exynos4_clk_ip_mfc_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_MFC, clk, enable);
}

static int exynos4_clksrc_mask_tv_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKSRC_MASK_TV, clk, enable);
}

static int exynos4_clk_ip_cam_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_CAM, clk, enable);
}

static int exynos4_clk_ip_tv_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_TV, clk, enable);
}

static int exynos4_clk_ip_image_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_IMAGE, clk, enable);
}

static int exynos4_clk_ip_lcd0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_LCD0, clk, enable);
}

int exynos4_clk_ip_lcd1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_LCD1, clk, enable);
}

int exynos4_clk_ip_fsys_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_FSYS, clk, enable);
}

static int exynos4_clk_ip_peril_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_PERIL, clk, enable);
}

static int exynos4_clk_ip_perir_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_PERIR, clk, enable);
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

static struct clksrc_clk clk_mout_apll = {
	.clk	= {
		.name		= "mout_apll",
	},
	.sources	= &clk_src_apll,
	.reg_src	= { .reg = S5P_CLKSRC_CPU, .shift = 0, .size = 1 },
};

struct clksrc_clk clk_sclk_apll = {
	.clk	= {
		.name		= "sclk_apll",
		.parent		= &clk_mout_apll.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 24, .size = 3 },
};

struct clksrc_clk clk_mout_epll = {
	.clk	= {
		.name		= "mout_epll",
	},
	.sources	= &clk_src_epll,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 4, .size = 1 },
};

struct clksrc_clk clk_mout_mpll = {
	.clk = {
		.name		= "mout_mpll",
	},
	.sources	= &clk_src_mpll,

	/* reg_src will be added in each SoCs' clock */
};

static struct clk *clkset_moutcore_list[] = {
	[0] = &clk_mout_apll.clk,
	[1] = &clk_mout_mpll.clk,
};

static struct clksrc_sources clkset_moutcore = {
	.sources	= clkset_moutcore_list,
	.nr_sources	= ARRAY_SIZE(clkset_moutcore_list),
};

static struct clksrc_clk clk_moutcore = {
	.clk	= {
		.name		= "moutcore",
	},
	.sources	= &clkset_moutcore,
	.reg_src	= { .reg = S5P_CLKSRC_CPU, .shift = 16, .size = 1 },
};

static struct clksrc_clk clk_coreclk = {
	.clk	= {
		.name		= "core_clk",
		.parent		= &clk_moutcore.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 0, .size = 3 },
};

static struct clksrc_clk clk_armclk = {
	.clk	= {
		.name		= "armclk",
		.parent		= &clk_coreclk.clk,
	},
};

static struct clksrc_clk clk_aclk_corem0 = {
	.clk	= {
		.name		= "aclk_corem0",
		.parent		= &clk_coreclk.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 4, .size = 3 },
};

static struct clksrc_clk clk_aclk_cores = {
	.clk	= {
		.name		= "aclk_cores",
		.parent		= &clk_coreclk.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 4, .size = 3 },
};

static struct clksrc_clk clk_aclk_corem1 = {
	.clk	= {
		.name		= "aclk_corem1",
		.parent		= &clk_coreclk.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 8, .size = 3 },
};

static struct clksrc_clk clk_periphclk = {
	.clk	= {
		.name		= "periphclk",
		.parent		= &clk_coreclk.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 12, .size = 3 },
};

/* Core list of CMU_CORE side */

struct clk *clkset_corebus_list[] = {
	[0] = &clk_mout_mpll.clk,
	[1] = &clk_sclk_apll.clk,
};

struct clksrc_sources clkset_mout_corebus = {
	.sources	= clkset_corebus_list,
	.nr_sources	= ARRAY_SIZE(clkset_corebus_list),
};

static struct clksrc_clk clk_mout_corebus = {
	.clk	= {
		.name		= "mout_corebus",
	},
	.sources	= &clkset_mout_corebus,
	.reg_src	= { .reg = S5P_CLKSRC_DMC, .shift = 4, .size = 1 },
};

static struct clksrc_clk clk_sclk_dmc = {
	.clk	= {
		.name		= "sclk_dmc",
		.parent		= &clk_mout_corebus.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_DMC0, .shift = 12, .size = 3 },
};

static struct clksrc_clk clk_aclk_cored = {
	.clk	= {
		.name		= "aclk_cored",
		.parent		= &clk_sclk_dmc.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_DMC0, .shift = 16, .size = 3 },
};

static struct clksrc_clk clk_aclk_corep = {
	.clk	= {
		.name		= "aclk_corep",
		.parent		= &clk_aclk_cored.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_DMC0, .shift = 20, .size = 3 },
};

static struct clksrc_clk clk_aclk_acp = {
	.clk	= {
		.name		= "aclk_acp",
		.parent		= &clk_mout_corebus.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_DMC0, .shift = 0, .size = 3 },
};

static struct clksrc_clk clk_pclk_acp = {
	.clk	= {
		.name		= "pclk_acp",
		.parent		= &clk_aclk_acp.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_DMC0, .shift = 4, .size = 3 },
};

/* Core list of CMU_TOP side */

struct clk *clkset_aclk_top_list[] = {
	[0] = &clk_mout_mpll.clk,
	[1] = &clk_sclk_apll.clk,
};

struct clksrc_sources clkset_aclk = {
	.sources	= clkset_aclk_top_list,
	.nr_sources	= ARRAY_SIZE(clkset_aclk_top_list),
};

static struct clksrc_clk clk_aclk_200 = {
	.clk	= {
		.name		= "aclk_200",
	},
	.sources	= &clkset_aclk,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 12, .size = 1 },
	.reg_div	= { .reg = S5P_CLKDIV_TOP, .shift = 0, .size = 3 },
};

static struct clksrc_clk clk_aclk_100 = {
	.clk	= {
		.name		= "aclk_100",
	},
	.sources	= &clkset_aclk,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 16, .size = 1 },
	.reg_div	= { .reg = S5P_CLKDIV_TOP, .shift = 4, .size = 4 },
};

static struct clksrc_clk clk_aclk_160 = {
	.clk	= {
		.name		= "aclk_160",
	},
	.sources	= &clkset_aclk,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 20, .size = 1 },
	.reg_div	= { .reg = S5P_CLKDIV_TOP, .shift = 8, .size = 3 },
};

struct clksrc_clk clk_aclk_133 = {
	.clk	= {
		.name		= "aclk_133",
	},
	.sources	= &clkset_aclk,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 24, .size = 1 },
	.reg_div	= { .reg = S5P_CLKDIV_TOP, .shift = 12, .size = 3 },
};

static struct clk *clkset_vpllsrc_list[] = {
	[0] = &clk_fin_vpll,
	[1] = &clk_sclk_hdmi27m,
};

static struct clksrc_sources clkset_vpllsrc = {
	.sources	= clkset_vpllsrc_list,
	.nr_sources	= ARRAY_SIZE(clkset_vpllsrc_list),
};

static struct clksrc_clk clk_vpllsrc = {
	.clk	= {
		.name		= "vpll_src",
		.enable		= exynos4_clksrc_mask_top_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources	= &clkset_vpllsrc,
	.reg_src	= { .reg = S5P_CLKSRC_TOP1, .shift = 0, .size = 1 },
};

static struct clk *clkset_sclk_vpll_list[] = {
	[0] = &clk_vpllsrc.clk,
	[1] = &clk_fout_vpll,
};

static struct clksrc_sources clkset_sclk_vpll = {
	.sources	= clkset_sclk_vpll_list,
	.nr_sources	= ARRAY_SIZE(clkset_sclk_vpll_list),
};

struct clksrc_clk clk_sclk_vpll = {
	.clk	= {
		.name		= "sclk_vpll",
	},
	.sources	= &clkset_sclk_vpll,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 8, .size = 1 },
};

static struct clk init_clocks_off[] = {
	{
		.name		= "timers",
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1<<24),
	}, {
		.name		= "csis",
		.devname	= "s5p-mipi-csis.0",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "csis",
		.devname	= "s5p-mipi-csis.1",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "fimc",
		.devname	= "exynos4-fimc.0",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "fimc",
		.devname	= "exynos4-fimc.1",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "fimc",
		.devname	= "exynos4-fimc.2",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "fimc",
		.devname	= "exynos4-fimc.3",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "fimd",
		.devname	= "exynos4-fb.0",
		.enable		= exynos4_clk_ip_lcd0_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.0",
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.1",
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.2",
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "hsmmc",
		.devname	= "s3c-sdhci.3",
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "dwmmc",
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "dac",
		.devname	= "s5p-sdo",
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "mixer",
		.devname	= "s5p-mixer",
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "vp",
		.devname	= "s5p-mixer",
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "hdmi",
		.devname	= "exynos4-hdmi",
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "hdmiphy",
		.devname	= "exynos4-hdmi",
		.enable		= exynos4_clk_hdmiphy_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "dacphy",
		.devname	= "s5p-sdo",
		.enable		= exynos4_clk_dac_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "dma",
		.devname	= "dma-pl330.0",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "dma",
		.devname	= "dma-pl330.1",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 1),
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
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "usbhost",
		.enable		= exynos4_clk_ip_fsys_ctrl ,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "otg",
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
		.devname	= "samsung-i2s.0",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 19),
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
		.name		= "ac97",
		.devname	= "samsung-ac97",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 27),
	}, {
		.name		= "fimg2d",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "mfc",
		.devname	= "s5p-mfc",
		.enable		= exynos4_clk_ip_mfc_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.0",
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.1",
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.2",
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.3",
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.4",
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.5",
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.6",
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.7",
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-hdmiphy-i2c",
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "SYSMMU_MDMA",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "SYSMMU_FIMC0",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "SYSMMU_FIMC1",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "SYSMMU_FIMC2",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "SYSMMU_FIMC3",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "SYSMMU_JPEG",
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "SYSMMU_FIMD0",
		.enable		= exynos4_clk_ip_lcd0_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "SYSMMU_FIMD1",
		.enable		= exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "SYSMMU_PCIe",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "SYSMMU_G2D",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "SYSMMU_ROTATOR",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "SYSMMU_TV",
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "SYSMMU_MFC_L",
		.enable		= exynos4_clk_ip_mfc_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "SYSMMU_MFC_R",
		.enable		= exynos4_clk_ip_mfc_ctrl,
		.ctrlbit	= (1 << 2),
	}
};

static struct clk init_clocks[] = {
	{
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
		.name		= "uart",
		.devname	= "s5pv210-uart.5",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 5),
	}
};

struct clk *clkset_group_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &clk_xusbxti,
	[2] = &clk_sclk_hdmi27m,
	[3] = &clk_sclk_usbphy0,
	[4] = &clk_sclk_usbphy1,
	[5] = &clk_sclk_hdmiphy,
	[6] = &clk_mout_mpll.clk,
	[7] = &clk_mout_epll.clk,
	[8] = &clk_sclk_vpll.clk,
};

struct clksrc_sources clkset_group = {
	.sources	= clkset_group_list,
	.nr_sources	= ARRAY_SIZE(clkset_group_list),
};

static struct clk *clkset_mout_g2d0_list[] = {
	[0] = &clk_mout_mpll.clk,
	[1] = &clk_sclk_apll.clk,
};

static struct clksrc_sources clkset_mout_g2d0 = {
	.sources	= clkset_mout_g2d0_list,
	.nr_sources	= ARRAY_SIZE(clkset_mout_g2d0_list),
};

static struct clksrc_clk clk_mout_g2d0 = {
	.clk	= {
		.name		= "mout_g2d0",
	},
	.sources	= &clkset_mout_g2d0,
	.reg_src	= { .reg = S5P_CLKSRC_IMAGE, .shift = 0, .size = 1 },
};

static struct clk *clkset_mout_g2d1_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_sclk_vpll.clk,
};

static struct clksrc_sources clkset_mout_g2d1 = {
	.sources	= clkset_mout_g2d1_list,
	.nr_sources	= ARRAY_SIZE(clkset_mout_g2d1_list),
};

static struct clksrc_clk clk_mout_g2d1 = {
	.clk	= {
		.name		= "mout_g2d1",
	},
	.sources	= &clkset_mout_g2d1,
	.reg_src	= { .reg = S5P_CLKSRC_IMAGE, .shift = 4, .size = 1 },
};

static struct clk *clkset_mout_g2d_list[] = {
	[0] = &clk_mout_g2d0.clk,
	[1] = &clk_mout_g2d1.clk,
};

static struct clksrc_sources clkset_mout_g2d = {
	.sources	= clkset_mout_g2d_list,
	.nr_sources	= ARRAY_SIZE(clkset_mout_g2d_list),
};

static struct clk *clkset_mout_mfc0_list[] = {
	[0] = &clk_mout_mpll.clk,
	[1] = &clk_sclk_apll.clk,
};

static struct clksrc_sources clkset_mout_mfc0 = {
	.sources	= clkset_mout_mfc0_list,
	.nr_sources	= ARRAY_SIZE(clkset_mout_mfc0_list),
};

static struct clksrc_clk clk_mout_mfc0 = {
	.clk	= {
		.name		= "mout_mfc0",
	},
	.sources	= &clkset_mout_mfc0,
	.reg_src	= { .reg = S5P_CLKSRC_MFC, .shift = 0, .size = 1 },
};

static struct clk *clkset_mout_mfc1_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_sclk_vpll.clk,
};

static struct clksrc_sources clkset_mout_mfc1 = {
	.sources	= clkset_mout_mfc1_list,
	.nr_sources	= ARRAY_SIZE(clkset_mout_mfc1_list),
};

static struct clksrc_clk clk_mout_mfc1 = {
	.clk	= {
		.name		= "mout_mfc1",
	},
	.sources	= &clkset_mout_mfc1,
	.reg_src	= { .reg = S5P_CLKSRC_MFC, .shift = 4, .size = 1 },
};

static struct clk *clkset_mout_mfc_list[] = {
	[0] = &clk_mout_mfc0.clk,
	[1] = &clk_mout_mfc1.clk,
};

static struct clksrc_sources clkset_mout_mfc = {
	.sources	= clkset_mout_mfc_list,
	.nr_sources	= ARRAY_SIZE(clkset_mout_mfc_list),
};

static struct clk *clkset_sclk_dac_list[] = {
	[0] = &clk_sclk_vpll.clk,
	[1] = &clk_sclk_hdmiphy,
};

static struct clksrc_sources clkset_sclk_dac = {
	.sources	= clkset_sclk_dac_list,
	.nr_sources	= ARRAY_SIZE(clkset_sclk_dac_list),
};

static struct clksrc_clk clk_sclk_dac = {
	.clk		= {
		.name		= "sclk_dac",
		.enable		= exynos4_clksrc_mask_tv_ctrl,
		.ctrlbit	= (1 << 8),
	},
	.sources = &clkset_sclk_dac,
	.reg_src = { .reg = S5P_CLKSRC_TV, .shift = 8, .size = 1 },
};

static struct clksrc_clk clk_sclk_pixel = {
	.clk		= {
		.name		= "sclk_pixel",
		.parent = &clk_sclk_vpll.clk,
	},
	.reg_div = { .reg = S5P_CLKDIV_TV, .shift = 0, .size = 4 },
};

static struct clk *clkset_sclk_hdmi_list[] = {
	[0] = &clk_sclk_pixel.clk,
	[1] = &clk_sclk_hdmiphy,
};

static struct clksrc_sources clkset_sclk_hdmi = {
	.sources	= clkset_sclk_hdmi_list,
	.nr_sources	= ARRAY_SIZE(clkset_sclk_hdmi_list),
};

static struct clksrc_clk clk_sclk_hdmi = {
	.clk		= {
		.name		= "sclk_hdmi",
		.enable		= exynos4_clksrc_mask_tv_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &clkset_sclk_hdmi,
	.reg_src = { .reg = S5P_CLKSRC_TV, .shift = 0, .size = 1 },
};

static struct clk *clkset_sclk_mixer_list[] = {
	[0] = &clk_sclk_dac.clk,
	[1] = &clk_sclk_hdmi.clk,
};

static struct clksrc_sources clkset_sclk_mixer = {
	.sources	= clkset_sclk_mixer_list,
	.nr_sources	= ARRAY_SIZE(clkset_sclk_mixer_list),
};

static struct clksrc_clk clk_sclk_mixer = {
	.clk		= {
		.name		= "sclk_mixer",
		.enable		= exynos4_clksrc_mask_tv_ctrl,
		.ctrlbit	= (1 << 4),
	},
	.sources = &clkset_sclk_mixer,
	.reg_src = { .reg = S5P_CLKSRC_TV, .shift = 4, .size = 1 },
};

static struct clksrc_clk *sclk_tv[] = {
	&clk_sclk_dac,
	&clk_sclk_pixel,
	&clk_sclk_hdmi,
	&clk_sclk_mixer,
};

static struct clksrc_clk clk_dout_mmc0 = {
	.clk		= {
		.name		= "dout_mmc0",
	},
	.sources = &clkset_group,
	.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 0, .size = 4 },
	.reg_div = { .reg = S5P_CLKDIV_FSYS1, .shift = 0, .size = 4 },
};

static struct clksrc_clk clk_dout_mmc1 = {
	.clk		= {
		.name		= "dout_mmc1",
	},
	.sources = &clkset_group,
	.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 4, .size = 4 },
	.reg_div = { .reg = S5P_CLKDIV_FSYS1, .shift = 16, .size = 4 },
};

static struct clksrc_clk clk_dout_mmc2 = {
	.clk		= {
		.name		= "dout_mmc2",
	},
	.sources = &clkset_group,
	.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 8, .size = 4 },
	.reg_div = { .reg = S5P_CLKDIV_FSYS2, .shift = 0, .size = 4 },
};

static struct clksrc_clk clk_dout_mmc3 = {
	.clk		= {
		.name		= "dout_mmc3",
	},
	.sources = &clkset_group,
	.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 12, .size = 4 },
	.reg_div = { .reg = S5P_CLKDIV_FSYS2, .shift = 16, .size = 4 },
};

static struct clksrc_clk clk_dout_mmc4 = {
	.clk		= {
		.name		= "dout_mmc4",
	},
	.sources = &clkset_group,
	.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 16, .size = 4 },
	.reg_div = { .reg = S5P_CLKDIV_FSYS3, .shift = 0, .size = 4 },
};

static struct clksrc_clk clksrcs[] = {
	{
		.clk	= {
			.name		= "uclk1",
			.devname	= "s5pv210-uart.0",
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 0, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "uclk1",
			.devname	= "s5pv210-uart.1",
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 4, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 4, .size = 4 },
	}, {
		.clk		= {
			.name		= "uclk1",
			.devname	= "s5pv210-uart.2",
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 8, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 8, .size = 4 },
	}, {
		.clk		= {
			.name		= "uclk1",
			.devname	= "s5pv210-uart.3",
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 12, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 12, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_pwm",
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 24, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL3, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_csis",
			.devname	= "s5p-mipi-csis.0",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 24, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 24, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_csis",
			.devname	= "s5p-mipi-csis.1",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 28),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 28, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 28, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_cam0",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 16, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 16, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_cam1",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 20),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 20, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 20, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimc",
			.devname	= "exynos4-fimc.0",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 0, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimc",
			.devname	= "exynos4-fimc.1",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 4, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 4, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimc",
			.devname	= "exynos4-fimc.2",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 8, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 8, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimc",
			.devname	= "exynos4-fimc.3",
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 12, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 12, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimd",
			.devname	= "exynos4-fb.0",
			.enable		= exynos4_clksrc_mask_lcd0_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_LCD0, .shift = 0, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_LCD0, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_spi",
			.devname	= "s3c64xx-spi.0",
			.enable		= exynos4_clksrc_mask_peril1_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL1, .shift = 16, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL1, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_spi",
			.devname	= "s3c64xx-spi.1",
			.enable		= exynos4_clksrc_mask_peril1_ctrl,
			.ctrlbit	= (1 << 20),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL1, .shift = 20, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL1, .shift = 16, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_spi",
			.devname	= "s3c64xx-spi.2",
			.enable		= exynos4_clksrc_mask_peril1_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL1, .shift = 24, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL2, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimg2d",
		},
		.sources = &clkset_mout_g2d,
		.reg_src = { .reg = S5P_CLKSRC_IMAGE, .shift = 8, .size = 1 },
		.reg_div = { .reg = S5P_CLKDIV_IMAGE, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_mfc",
			.devname	= "s5p-mfc",
		},
		.sources = &clkset_mout_mfc,
		.reg_src = { .reg = S5P_CLKSRC_MFC, .shift = 8, .size = 1 },
		.reg_div = { .reg = S5P_CLKDIV_MFC, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.devname	= "s3c-sdhci.0",
			.parent		= &clk_dout_mmc0.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.reg_div = { .reg = S5P_CLKDIV_FSYS1, .shift = 8, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.devname	= "s3c-sdhci.1",
			.parent         = &clk_dout_mmc1.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.reg_div = { .reg = S5P_CLKDIV_FSYS1, .shift = 24, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.devname	= "s3c-sdhci.2",
			.parent         = &clk_dout_mmc2.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.reg_div = { .reg = S5P_CLKDIV_FSYS2, .shift = 8, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.devname	= "s3c-sdhci.3",
			.parent         = &clk_dout_mmc3.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.reg_div = { .reg = S5P_CLKDIV_FSYS2, .shift = 24, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_dwmmc",
			.parent         = &clk_dout_mmc4.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.reg_div = { .reg = S5P_CLKDIV_FSYS3, .shift = 8, .size = 8 },
	}
};

/* Clock initialization code */
static struct clksrc_clk *sysclks[] = {
	&clk_mout_apll,
	&clk_sclk_apll,
	&clk_mout_epll,
	&clk_mout_mpll,
	&clk_moutcore,
	&clk_coreclk,
	&clk_armclk,
	&clk_aclk_corem0,
	&clk_aclk_cores,
	&clk_aclk_corem1,
	&clk_periphclk,
	&clk_mout_corebus,
	&clk_sclk_dmc,
	&clk_aclk_cored,
	&clk_aclk_corep,
	&clk_aclk_acp,
	&clk_pclk_acp,
	&clk_vpllsrc,
	&clk_sclk_vpll,
	&clk_aclk_200,
	&clk_aclk_100,
	&clk_aclk_160,
	&clk_aclk_133,
	&clk_dout_mmc0,
	&clk_dout_mmc1,
	&clk_dout_mmc2,
	&clk_dout_mmc3,
	&clk_dout_mmc4,
	&clk_mout_mfc0,
	&clk_mout_mfc1,
};

static int xtal_rate;

static unsigned long exynos4_fout_apll_get_rate(struct clk *clk)
{
	if (soc_is_exynos4210())
		return s5p_get_pll45xx(xtal_rate, __raw_readl(S5P_APLL_CON0),
					pll_4508);
	else if (soc_is_exynos4212() || soc_is_exynos4412())
		return s5p_get_pll35xx(xtal_rate, __raw_readl(S5P_APLL_CON0));
	else
		return 0;
}

static struct clk_ops exynos4_fout_apll_ops = {
	.get_rate = exynos4_fout_apll_get_rate,
};

static u32 vpll_div[][8] = {
	{  54000000, 3, 53, 3, 1024, 0, 17, 0 },
	{ 108000000, 3, 53, 2, 1024, 0, 17, 0 },
};

static unsigned long exynos4_vpll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int exynos4_vpll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int vpll_con0, vpll_con1 = 0;
	unsigned int i;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	vpll_con0 = __raw_readl(S5P_VPLL_CON0);
	vpll_con0 &= ~(0x1 << 27 |					\
			PLL90XX_MDIV_MASK << PLL46XX_MDIV_SHIFT |	\
			PLL90XX_PDIV_MASK << PLL46XX_PDIV_SHIFT |	\
			PLL90XX_SDIV_MASK << PLL46XX_SDIV_SHIFT);

	vpll_con1 = __raw_readl(S5P_VPLL_CON1);
	vpll_con1 &= ~(PLL46XX_MRR_MASK << PLL46XX_MRR_SHIFT |	\
			PLL46XX_MFR_MASK << PLL46XX_MFR_SHIFT |	\
			PLL4650C_KDIV_MASK << PLL46XX_KDIV_SHIFT);

	for (i = 0; i < ARRAY_SIZE(vpll_div); i++) {
		if (vpll_div[i][0] == rate) {
			vpll_con0 |= vpll_div[i][1] << PLL46XX_PDIV_SHIFT;
			vpll_con0 |= vpll_div[i][2] << PLL46XX_MDIV_SHIFT;
			vpll_con0 |= vpll_div[i][3] << PLL46XX_SDIV_SHIFT;
			vpll_con1 |= vpll_div[i][4] << PLL46XX_KDIV_SHIFT;
			vpll_con1 |= vpll_div[i][5] << PLL46XX_MFR_SHIFT;
			vpll_con1 |= vpll_div[i][6] << PLL46XX_MRR_SHIFT;
			vpll_con0 |= vpll_div[i][7] << 27;
			break;
		}
	}

	if (i == ARRAY_SIZE(vpll_div)) {
		printk(KERN_ERR "%s: Invalid Clock VPLL Frequency\n",
				__func__);
		return -EINVAL;
	}

	__raw_writel(vpll_con0, S5P_VPLL_CON0);
	__raw_writel(vpll_con1, S5P_VPLL_CON1);

	/* Wait for VPLL lock */
	while (!(__raw_readl(S5P_VPLL_CON0) & (1 << PLL46XX_LOCKED_SHIFT)))
		continue;

	clk->rate = rate;
	return 0;
}

static struct clk_ops exynos4_vpll_ops = {
	.get_rate = exynos4_vpll_get_rate,
	.set_rate = exynos4_vpll_set_rate,
};

void __init_or_cpufreq exynos4_setup_clocks(void)
{
	struct clk *xtal_clk;
	unsigned long apll = 0;
	unsigned long mpll = 0;
	unsigned long epll = 0;
	unsigned long vpll = 0;
	unsigned long vpllsrc;
	unsigned long xtal;
	unsigned long armclk;
	unsigned long sclk_dmc;
	unsigned long aclk_200;
	unsigned long aclk_100;
	unsigned long aclk_160;
	unsigned long aclk_133;
	unsigned int ptr;

	printk(KERN_DEBUG "%s: registering clocks\n", __func__);

	xtal_clk = clk_get(NULL, "xtal");
	BUG_ON(IS_ERR(xtal_clk));

	xtal = clk_get_rate(xtal_clk);

	xtal_rate = xtal;

	clk_put(xtal_clk);

	printk(KERN_DEBUG "%s: xtal is %ld\n", __func__, xtal);

	if (soc_is_exynos4210()) {
		apll = s5p_get_pll45xx(xtal, __raw_readl(S5P_APLL_CON0),
					pll_4508);
		mpll = s5p_get_pll45xx(xtal, __raw_readl(S5P_MPLL_CON0),
					pll_4508);
		epll = s5p_get_pll46xx(xtal, __raw_readl(S5P_EPLL_CON0),
					__raw_readl(S5P_EPLL_CON1), pll_4600);

		vpllsrc = clk_get_rate(&clk_vpllsrc.clk);
		vpll = s5p_get_pll46xx(vpllsrc, __raw_readl(S5P_VPLL_CON0),
					__raw_readl(S5P_VPLL_CON1), pll_4650c);
	} else if (soc_is_exynos4212() || soc_is_exynos4412()) {
		apll = s5p_get_pll35xx(xtal, __raw_readl(S5P_APLL_CON0));
		mpll = s5p_get_pll35xx(xtal, __raw_readl(S5P_MPLL_CON0));
		epll = s5p_get_pll36xx(xtal, __raw_readl(S5P_EPLL_CON0),
					__raw_readl(S5P_EPLL_CON1));

		vpllsrc = clk_get_rate(&clk_vpllsrc.clk);
		vpll = s5p_get_pll36xx(vpllsrc, __raw_readl(S5P_VPLL_CON0),
					__raw_readl(S5P_VPLL_CON1));
	} else {
		/* nothing */
	}

	clk_fout_apll.ops = &exynos4_fout_apll_ops;
	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;
	clk_fout_vpll.ops = &exynos4_vpll_ops;
	clk_fout_vpll.rate = vpll;

	printk(KERN_INFO "EXYNOS4: PLL settings, A=%ld, M=%ld, E=%ld V=%ld",
			apll, mpll, epll, vpll);

	armclk = clk_get_rate(&clk_armclk.clk);
	sclk_dmc = clk_get_rate(&clk_sclk_dmc.clk);

	aclk_200 = clk_get_rate(&clk_aclk_200.clk);
	aclk_100 = clk_get_rate(&clk_aclk_100.clk);
	aclk_160 = clk_get_rate(&clk_aclk_160.clk);
	aclk_133 = clk_get_rate(&clk_aclk_133.clk);

	printk(KERN_INFO "EXYNOS4: ARMCLK=%ld, DMC=%ld, ACLK200=%ld\n"
			 "ACLK100=%ld, ACLK160=%ld, ACLK133=%ld\n",
			armclk, sclk_dmc, aclk_200,
			aclk_100, aclk_160, aclk_133);

	clk_f.rate = armclk;
	clk_h.rate = sclk_dmc;
	clk_p.rate = aclk_100;

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_set_clksrc(&clksrcs[ptr], true);
}

static struct clk *clks[] __initdata = {
	&clk_sclk_hdmi27m,
	&clk_sclk_hdmiphy,
	&clk_sclk_usbphy0,
	&clk_sclk_usbphy1,
};

#ifdef CONFIG_PM_SLEEP
static int exynos4_clock_suspend(void)
{
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
	.suspend	= exynos4_clock_suspend,
	.resume		= exynos4_clock_resume,
};

void __init exynos4_register_clocks(void)
{
	int ptr;

	s3c24xx_register_clocks(clks, ARRAY_SIZE(clks));

	for (ptr = 0; ptr < ARRAY_SIZE(sysclks); ptr++)
		s3c_register_clksrc(sysclks[ptr], 1);

	for (ptr = 0; ptr < ARRAY_SIZE(sclk_tv); ptr++)
		s3c_register_clksrc(sclk_tv[ptr], 1);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));
	s3c_register_clocks(init_clocks, ARRAY_SIZE(init_clocks));

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));

	register_syscore_ops(&exynos4_clock_syscore_ops);
	s3c24xx_register_clock(&dummy_apb_pclk);

	s3c_pwmclk_init();
}
