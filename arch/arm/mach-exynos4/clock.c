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

#include <plat/cpu-freq.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/s5p-clock.h>
#include <plat/clock-clksrc.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/sysmmu.h>

static struct clk clk_sclk_hdmi27m = {
	.name		= "sclk_hdmi27m",
	.id		= -1,
	.rate		= 27000000,
};

static struct clk clk_sclk_hdmiphy = {
	.name		= "sclk_hdmiphy",
	.id		= -1,
};

static struct clk clk_sclk_usbphy0 = {
	.name		= "sclk_usbphy0",
	.id		= -1,
	.rate		= 27000000,
};

static struct clk clk_sclk_usbphy1 = {
	.name		= "sclk_usbphy1",
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

static int exynos4_clksrc_mask_lcd1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKSRC_MASK_LCD1, clk, enable);
}

static int exynos4_clksrc_mask_fsys_ctrl(struct clk *clk, int enable)
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

static int exynos4_clk_ip_lcd1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_LCD1, clk, enable);
}

static int exynos4_clk_ip_fsys_ctrl(struct clk *clk, int enable)
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

/* Core list of CMU_CPU side */

static struct clksrc_clk clk_mout_apll = {
	.clk	= {
		.name		= "mout_apll",
		.id		= -1,
	},
	.sources	= &clk_src_apll,
	.reg_src	= { .reg = S5P_CLKSRC_CPU, .shift = 0, .size = 1 },
};

static struct clksrc_clk clk_sclk_apll = {
	.clk	= {
		.name		= "sclk_apll",
		.id		= -1,
		.parent		= &clk_mout_apll.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 24, .size = 3 },
};

static struct clksrc_clk clk_mout_epll = {
	.clk	= {
		.name		= "mout_epll",
		.id		= -1,
	},
	.sources	= &clk_src_epll,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 4, .size = 1 },
};

static struct clksrc_clk clk_mout_mpll = {
	.clk = {
		.name		= "mout_mpll",
		.id		= -1,
	},
	.sources	= &clk_src_mpll,
	.reg_src	= { .reg = S5P_CLKSRC_CPU, .shift = 8, .size = 1 },
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
		.id		= -1,
	},
	.sources	= &clkset_moutcore,
	.reg_src	= { .reg = S5P_CLKSRC_CPU, .shift = 16, .size = 1 },
};

static struct clksrc_clk clk_coreclk = {
	.clk	= {
		.name		= "core_clk",
		.id		= -1,
		.parent		= &clk_moutcore.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 0, .size = 3 },
};

static struct clksrc_clk clk_armclk = {
	.clk	= {
		.name		= "armclk",
		.id		= -1,
		.parent		= &clk_coreclk.clk,
	},
};

static struct clksrc_clk clk_aclk_corem0 = {
	.clk	= {
		.name		= "aclk_corem0",
		.id		= -1,
		.parent		= &clk_coreclk.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 4, .size = 3 },
};

static struct clksrc_clk clk_aclk_cores = {
	.clk	= {
		.name		= "aclk_cores",
		.id		= -1,
		.parent		= &clk_coreclk.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 4, .size = 3 },
};

static struct clksrc_clk clk_aclk_corem1 = {
	.clk	= {
		.name		= "aclk_corem1",
		.id		= -1,
		.parent		= &clk_coreclk.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 8, .size = 3 },
};

static struct clksrc_clk clk_periphclk = {
	.clk	= {
		.name		= "periphclk",
		.id		= -1,
		.parent		= &clk_coreclk.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 12, .size = 3 },
};

/* Core list of CMU_CORE side */

static struct clk *clkset_corebus_list[] = {
	[0] = &clk_mout_mpll.clk,
	[1] = &clk_sclk_apll.clk,
};

static struct clksrc_sources clkset_mout_corebus = {
	.sources	= clkset_corebus_list,
	.nr_sources	= ARRAY_SIZE(clkset_corebus_list),
};

static struct clksrc_clk clk_mout_corebus = {
	.clk	= {
		.name		= "mout_corebus",
		.id		= -1,
	},
	.sources	= &clkset_mout_corebus,
	.reg_src	= { .reg = S5P_CLKSRC_DMC, .shift = 4, .size = 1 },
};

static struct clksrc_clk clk_sclk_dmc = {
	.clk	= {
		.name		= "sclk_dmc",
		.id		= -1,
		.parent		= &clk_mout_corebus.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_DMC0, .shift = 12, .size = 3 },
};

static struct clksrc_clk clk_aclk_cored = {
	.clk	= {
		.name		= "aclk_cored",
		.id		= -1,
		.parent		= &clk_sclk_dmc.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_DMC0, .shift = 16, .size = 3 },
};

static struct clksrc_clk clk_aclk_corep = {
	.clk	= {
		.name		= "aclk_corep",
		.id		= -1,
		.parent		= &clk_aclk_cored.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_DMC0, .shift = 20, .size = 3 },
};

static struct clksrc_clk clk_aclk_acp = {
	.clk	= {
		.name		= "aclk_acp",
		.id		= -1,
		.parent		= &clk_mout_corebus.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_DMC0, .shift = 0, .size = 3 },
};

static struct clksrc_clk clk_pclk_acp = {
	.clk	= {
		.name		= "pclk_acp",
		.id		= -1,
		.parent		= &clk_aclk_acp.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_DMC0, .shift = 4, .size = 3 },
};

/* Core list of CMU_TOP side */

static struct clk *clkset_aclk_top_list[] = {
	[0] = &clk_mout_mpll.clk,
	[1] = &clk_sclk_apll.clk,
};

static struct clksrc_sources clkset_aclk = {
	.sources	= clkset_aclk_top_list,
	.nr_sources	= ARRAY_SIZE(clkset_aclk_top_list),
};

static struct clksrc_clk clk_aclk_200 = {
	.clk	= {
		.name		= "aclk_200",
		.id		= -1,
	},
	.sources	= &clkset_aclk,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 12, .size = 1 },
	.reg_div	= { .reg = S5P_CLKDIV_TOP, .shift = 0, .size = 3 },
};

static struct clksrc_clk clk_aclk_100 = {
	.clk	= {
		.name		= "aclk_100",
		.id		= -1,
	},
	.sources	= &clkset_aclk,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 16, .size = 1 },
	.reg_div	= { .reg = S5P_CLKDIV_TOP, .shift = 4, .size = 4 },
};

static struct clksrc_clk clk_aclk_160 = {
	.clk	= {
		.name		= "aclk_160",
		.id		= -1,
	},
	.sources	= &clkset_aclk,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 20, .size = 1 },
	.reg_div	= { .reg = S5P_CLKDIV_TOP, .shift = 8, .size = 3 },
};

static struct clksrc_clk clk_aclk_133 = {
	.clk	= {
		.name		= "aclk_133",
		.id		= -1,
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
		.id		= -1,
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

static struct clksrc_clk clk_sclk_vpll = {
	.clk	= {
		.name		= "sclk_vpll",
		.id		= -1,
	},
	.sources	= &clkset_sclk_vpll,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 8, .size = 1 },
};

static struct clk init_clocks_off[] = {
	{
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1<<24),
	}, {
		.name		= "csis",
		.id		= 0,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "csis",
		.id		= 1,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "fimc",
		.id		= 0,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "fimc",
		.id		= 1,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "fimc",
		.id		= 2,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "fimc",
		.id		= 3,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "fimd",
		.id		= 0,
		.enable		= exynos4_clk_ip_lcd0_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "fimd",
		.id		= 1,
		.enable		= exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "sataphy",
		.id		= -1,
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "hsmmc",
		.id		= 0,
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "hsmmc",
		.id		= 1,
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "hsmmc",
		.id		= 2,
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "hsmmc",
		.id		= 3,
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "hsmmc",
		.id		= 4,
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "sata",
		.id		= -1,
		.parent		= &clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "pdma",
		.id		= 0,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "pdma",
		.id		= 1,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "adc",
		.id		= -1,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "keypad",
		.id		= -1,
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "rtc",
		.id		= -1,
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "watchdog",
		.id		= -1,
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "usbhost",
		.id		= -1,
		.enable		= exynos4_clk_ip_fsys_ctrl ,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "otg",
		.id		= -1,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "spi",
		.id		= 0,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "spi",
		.id		= 1,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "spi",
		.id		= 2,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "iis",
		.id		= 0,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 19),
	}, {
		.name		= "iis",
		.id		= 1,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 20),
	}, {
		.name		= "iis",
		.id		= 2,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 21),
	}, {
		.name		= "ac97",
		.id		= -1,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 27),
	}, {
		.name		= "fimg2d",
		.id		= -1,
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "i2c",
		.id		= 0,
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "i2c",
		.id		= 1,
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "i2c",
		.id		= 2,
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "i2c",
		.id		= 3,
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "i2c",
		.id		= 4,
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "i2c",
		.id		= 5,
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "i2c",
		.id		= 6,
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "i2c",
		.id		= 7,
		.parent		= &clk_aclk_100.clk,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "SYSMMU_MDMA",
		.id		= -1,
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "SYSMMU_FIMC0",
		.id		= -1,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "SYSMMU_FIMC1",
		.id		= -1,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "SYSMMU_FIMC2",
		.id		= -1,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "SYSMMU_FIMC3",
		.id		= -1,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "SYSMMU_JPEG",
		.id		= -1,
		.enable		= exynos4_clk_ip_cam_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "SYSMMU_FIMD0",
		.id		= -1,
		.enable		= exynos4_clk_ip_lcd0_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "SYSMMU_FIMD1",
		.id		= -1,
		.enable		= exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "SYSMMU_PCIe",
		.id		= -1,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "SYSMMU_G2D",
		.id		= -1,
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "SYSMMU_ROTATOR",
		.id		= -1,
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "SYSMMU_TV",
		.id		= -1,
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "SYSMMU_MFC_L",
		.id		= -1,
		.enable		= exynos4_clk_ip_mfc_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "SYSMMU_MFC_R",
		.id		= -1,
		.enable		= exynos4_clk_ip_mfc_ctrl,
		.ctrlbit	= (1 << 2),
	}
};

static struct clk init_clocks[] = {
	{
		.name		= "uart",
		.id		= 0,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "uart",
		.id		= 1,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "uart",
		.id		= 2,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "uart",
		.id		= 3,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "uart",
		.id		= 4,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "uart",
		.id		= 5,
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 5),
	}
};

static struct clk *clkset_group_list[] = {
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

static struct clksrc_sources clkset_group = {
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
		.id		= -1,
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
		.id		= -1,
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

static struct clksrc_clk clk_dout_mmc0 = {
	.clk		= {
		.name		= "dout_mmc0",
		.id		= -1,
	},
	.sources = &clkset_group,
	.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 0, .size = 4 },
	.reg_div = { .reg = S5P_CLKDIV_FSYS1, .shift = 0, .size = 4 },
};

static struct clksrc_clk clk_dout_mmc1 = {
	.clk		= {
		.name		= "dout_mmc1",
		.id		= -1,
	},
	.sources = &clkset_group,
	.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 4, .size = 4 },
	.reg_div = { .reg = S5P_CLKDIV_FSYS1, .shift = 16, .size = 4 },
};

static struct clksrc_clk clk_dout_mmc2 = {
	.clk		= {
		.name		= "dout_mmc2",
		.id		= -1,
	},
	.sources = &clkset_group,
	.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 8, .size = 4 },
	.reg_div = { .reg = S5P_CLKDIV_FSYS2, .shift = 0, .size = 4 },
};

static struct clksrc_clk clk_dout_mmc3 = {
	.clk		= {
		.name		= "dout_mmc3",
		.id		= -1,
	},
	.sources = &clkset_group,
	.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 12, .size = 4 },
	.reg_div = { .reg = S5P_CLKDIV_FSYS2, .shift = 16, .size = 4 },
};

static struct clksrc_clk clk_dout_mmc4 = {
	.clk		= {
		.name		= "dout_mmc4",
		.id		= -1,
	},
	.sources = &clkset_group,
	.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 16, .size = 4 },
	.reg_div = { .reg = S5P_CLKDIV_FSYS3, .shift = 0, .size = 4 },
};

static struct clksrc_clk clksrcs[] = {
	{
		.clk	= {
			.name		= "uclk1",
			.id		= 0,
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 0, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "uclk1",
			.id		= 1,
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 4, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 4, .size = 4 },
	}, {
		.clk		= {
			.name		= "uclk1",
			.id		= 2,
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 8, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 8, .size = 4 },
	}, {
		.clk		= {
			.name		= "uclk1",
			.id		= 3,
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 12, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 12, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_pwm",
			.id		= -1,
			.enable		= exynos4_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 24, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL3, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_csis",
			.id		= 0,
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 24, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 24, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_csis",
			.id		= 1,
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 28),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 28, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 28, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_cam",
			.id		= 0,
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 16, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 16, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_cam",
			.id		= 1,
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 20),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 20, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 20, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimc",
			.id		= 0,
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 0, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimc",
			.id		= 1,
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 4, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 4, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimc",
			.id		= 2,
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 8, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 8, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimc",
			.id		= 3,
			.enable		= exynos4_clksrc_mask_cam_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_CAM, .shift = 12, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_CAM, .shift = 12, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimd",
			.id		= 0,
			.enable		= exynos4_clksrc_mask_lcd0_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_LCD0, .shift = 0, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_LCD0, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimd",
			.id		= 1,
			.enable		= exynos4_clksrc_mask_lcd1_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_LCD1, .shift = 0, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_LCD1, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_sata",
			.id		= -1,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &clkset_mout_corebus,
		.reg_src = { .reg = S5P_CLKSRC_FSYS, .shift = 24, .size = 1 },
		.reg_div = { .reg = S5P_CLKDIV_FSYS0, .shift = 20, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_spi",
			.id		= 0,
			.enable		= exynos4_clksrc_mask_peril1_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL1, .shift = 16, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL1, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_spi",
			.id		= 1,
			.enable		= exynos4_clksrc_mask_peril1_ctrl,
			.ctrlbit	= (1 << 20),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL1, .shift = 20, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL1, .shift = 16, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_spi",
			.id		= 2,
			.enable		= exynos4_clksrc_mask_peril1_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL1, .shift = 24, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL2, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimg2d",
			.id		= -1,
		},
		.sources = &clkset_mout_g2d,
		.reg_src = { .reg = S5P_CLKSRC_IMAGE, .shift = 8, .size = 1 },
		.reg_div = { .reg = S5P_CLKDIV_IMAGE, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.id		= 0,
			.parent		= &clk_dout_mmc0.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.reg_div = { .reg = S5P_CLKDIV_FSYS1, .shift = 8, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.id		= 1,
			.parent         = &clk_dout_mmc1.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.reg_div = { .reg = S5P_CLKDIV_FSYS1, .shift = 24, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.id		= 2,
			.parent         = &clk_dout_mmc2.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.reg_div = { .reg = S5P_CLKDIV_FSYS2, .shift = 8, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.id		= 3,
			.parent         = &clk_dout_mmc3.clk,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.reg_div = { .reg = S5P_CLKDIV_FSYS2, .shift = 24, .size = 8 },
	}, {
		.clk		= {
			.name		= "sclk_mmc",
			.id		= 4,
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
};

static int xtal_rate;

static unsigned long exynos4_fout_apll_get_rate(struct clk *clk)
{
	return s5p_get_pll45xx(xtal_rate, __raw_readl(S5P_APLL_CON0), pll_4508);
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

	apll = s5p_get_pll45xx(xtal, __raw_readl(S5P_APLL_CON0), pll_4508);
	mpll = s5p_get_pll45xx(xtal, __raw_readl(S5P_MPLL_CON0), pll_4508);
	epll = s5p_get_pll46xx(xtal, __raw_readl(S5P_EPLL_CON0),
				__raw_readl(S5P_EPLL_CON1), pll_4600);

	vpllsrc = clk_get_rate(&clk_vpllsrc.clk);
	vpll = s5p_get_pll46xx(vpllsrc, __raw_readl(S5P_VPLL_CON0),
				__raw_readl(S5P_VPLL_CON1), pll_4650);

	clk_fout_apll.ops = &exynos4_fout_apll_ops;
	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;
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
	/* Nothing here yet */
};

void __init exynos4_register_clocks(void)
{
	int ptr;

	s3c24xx_register_clocks(clks, ARRAY_SIZE(clks));

	for (ptr = 0; ptr < ARRAY_SIZE(sysclks); ptr++)
		s3c_register_clksrc(sysclks[ptr], 1);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));
	s3c_register_clocks(init_clocks, ARRAY_SIZE(init_clocks));

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));

	s3c_pwmclk_init();
}
