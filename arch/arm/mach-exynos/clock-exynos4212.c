/*
 * linux/arch/arm/mach-exynos/clock-exynos4212.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4212 - Clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
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

#include <mach/hardware.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/exynos-clock.h>
#include <mach/dev-sysmmu.h>

#ifdef CONFIG_PM
static struct sleep_save exynos4212_clock_save[] = {
	/* CMU side */
	SAVE_ITEM(EXYNOS4_DMC_PAUSE_CTRL),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_ISP),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_DMC1),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_IMAGE_4212),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_PERIR_4212),
	SAVE_ITEM(EXYNOS4_CLKGATE_BUS_LEFTBUS),
	SAVE_ITEM(EXYNOS4_CLKGATE_BUS_IMAGE),
	SAVE_ITEM(EXYNOS4_CLKGATE_BUS_RIGHTBUS),
	SAVE_ITEM(EXYNOS4_CLKGATE_BUS_PERIR),
	SAVE_ITEM(EXYNOS4_CLKGATE_BUS_PERIL),
	SAVE_ITEM(EXYNOS4_CLKGATE_BUS_DMC0),
	SAVE_ITEM(EXYNOS4_CLKGATE_BUS_DMC1),
	SAVE_ITEM(EXYNOS4_CLKGATE_SCLK_DMC),
	SAVE_ITEM(EXYNOS4_CLKDIV_CAM1),
	SAVE_ITEM(EXYNOS4_CLKDIV_ISP),
	SAVE_ITEM(EXYNOS4_CLKDIV_IMAGE),
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_ISP),
	SAVE_ITEM(EXYNOS4_CLKSRC_ISP),
	SAVE_ITEM(EXYNOS4_CLKSRC_CAM1),
};

static struct sleep_save exynos4212_epll_save[] = {
	SAVE_ITEM(EXYNOS4_EPLL_CON2),
};

static struct sleep_save exynos4212_vpll_save[] = {
	SAVE_ITEM(EXYNOS4_VPLL_CON2),
};
#endif

static int exynos4212_clk_bus_dmc0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_BUS_DMC0, clk, enable);
}

static int exynos4212_clk_bus_dmc1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_BUS_DMC1, clk, enable);
}

static int exynos4212_clk_sclk_dmc_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_SCLK_DMC, clk, enable);
}

static int __maybe_unused exynos4212_clk_bus_peril_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_BUS_PERIL, clk, enable);
}

static int __maybe_unused exynos4212_clk_bus_perir_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_BUS_PERIR, clk, enable);
}

static int exynos4212_clk_ip_isp_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_ISP, clk, enable);
}

static int exynos4212_clk_ip_isp0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_ISP0, clk, enable);
}

static int exynos4212_clk_ip_isp1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_ISP1, clk, enable);
}

static struct clk *exynos4212_clk_src_mpll_user_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos4_clk_mout_mpll.clk,
};

static struct clksrc_sources exynos4212_clk_src_mpll_user = {
	.sources	= exynos4212_clk_src_mpll_user_list,
	.nr_sources	= ARRAY_SIZE(exynos4212_clk_src_mpll_user_list),
};

static struct clksrc_clk exynos4212_clk_mout_mpll_user = {
	.clk	= {
		.name		= "mout_mpll_user",
	},
	.sources = &exynos4212_clk_src_mpll_user,
	.reg_src = { .reg = EXYNOS4_CLKSRC_CPU, .shift = 24, .size = 1 },
};

static struct clk *exynos4212_clkset_aclk_lrbus_user_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos4_clk_mout_mpll.clk,
};

static struct clksrc_sources exynos4212_clkset_aclk_lrbus_user = {
	.sources	= exynos4212_clkset_aclk_lrbus_user_list,
	.nr_sources	= ARRAY_SIZE(exynos4212_clkset_aclk_lrbus_user_list),
};

static struct clksrc_clk exynos4212_clk_aclk_gdl_user = {
	.clk	= {
		.name		= "aclk_gdl_user",
	},
	.sources = &exynos4212_clkset_aclk_lrbus_user,
	.reg_src = { .reg = EXYNOS4_CLKSRC_LEFTBUS, .shift = 4, .size = 1 },
};

static struct clksrc_clk exynos4212_clk_aclk_gdr_user = {
	.clk	= {
		.name		= "aclk_gdr_user",
	},
	.sources = &exynos4212_clkset_aclk_lrbus_user,
	.reg_src = { .reg = EXYNOS4_CLKSRC_RIGHTBUS, .shift = 4, .size = 1 },
};

static struct clksrc_clk exynos4212_clk_mout_aclk_266 = {
	.clk	= {
		.name		= "mout_aclk_266",
	},
	.sources = &exynos4_clkset_aclk,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP1, .shift = 4, .size = 1 },
};

static struct clksrc_clk exynos4212_clk_dout_aclk_266 = {
	.clk	= {
		.name		= "dout_aclk_266",
		.parent		= &exynos4212_clk_mout_aclk_266.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_TOP, .shift = 20, .size = 3 },
};

static struct clksrc_clk exynos4212_clk_mout_aclk_200 = {
	.clk	= {
		.name		= "mout_aclk_200",
	},
	.sources = &exynos4_clkset_aclk,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP0, .shift = 12, .size = 1 },
};

static struct clksrc_clk exynos4212_clk_dout_aclk_200 = {
	.clk	= {
		.name		= "dout_aclk_200",
		.parent		= &exynos4212_clk_mout_aclk_200.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_TOP, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos4212_clk_mout_aclk_400_mcuisp = {
	.clk	= {
		.name		= "mout_aclk_400_mcuisp",
	},
	.sources = &exynos4_clkset_aclk,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP1, .shift = 8, .size = 1 },
};

static struct clksrc_clk exynos4212_clk_dout_aclk_400_mcuisp = {
	.clk	= {
		.name		= "dout_aclk_400_mcuisp",
		.parent		= &exynos4212_clk_mout_aclk_400_mcuisp.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_TOP, .shift = 24, .size = 3 },
};

static struct clk *exynos4212_clk_aclk_400_mcuisp_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos4212_clk_dout_aclk_400_mcuisp.clk,
};

static struct clksrc_sources exynos4212_clkset_aclk_400_mcuisp = {
	.sources	= exynos4212_clk_aclk_400_mcuisp_list,
	.nr_sources	= ARRAY_SIZE(exynos4212_clk_aclk_400_mcuisp_list),
};

struct clksrc_clk exynos4212_clk_aclk_400_mcuisp = {
	.clk	= {
		.name		= "aclk_400_mcuisp",
	},
	.sources = &exynos4212_clkset_aclk_400_mcuisp,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP1, .shift = 24, .size = 1 },
};

static struct clk *exynos4212_clk_aclk_266_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos4212_clk_dout_aclk_266.clk,
};

static struct clksrc_sources exynos4212_clkset_aclk_266 = {
	.sources	= exynos4212_clk_aclk_266_list,
	.nr_sources	= ARRAY_SIZE(exynos4212_clk_aclk_266_list),
};

struct clksrc_clk exynos4212_clk_aclk_266 = {
	.clk	= {
		.name		= "aclk_266",
	},
	.sources = &exynos4212_clkset_aclk_266,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP1, .shift = 16, .size = 1 },
};

static struct clk *exynos4212_clk_aclk_200_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos4212_clk_dout_aclk_200.clk,
};

static struct clksrc_sources exynos4212_clkset_aclk_200 = {
	.sources	= exynos4212_clk_aclk_200_list,
	.nr_sources	= ARRAY_SIZE(exynos4212_clk_aclk_200_list),
};

static struct clk *exynos4212_clkset_mout_jpeg0_list[] = {
	[0] = &exynos4212_clk_mout_mpll_user.clk,
	[1] = &exynos4_clk_sclk_apll.clk,
};

static struct clksrc_sources exynos4212_clkset_mout_jpeg0 = {
	.sources	= exynos4212_clkset_mout_jpeg0_list,
	.nr_sources	= ARRAY_SIZE(exynos4212_clkset_mout_jpeg0_list),
};

struct clksrc_clk exynos4212_clk_mout_jpeg0 = {
	.clk	= {
		.name		= "mout_jpeg0",
	},
	.sources = &exynos4212_clkset_mout_jpeg0,
	.reg_src = { .reg = EXYNOS4_CLKSRC_CAM1, .shift = 0, .size = 1 },
};

static struct clk *exynos4212_clkset_mout_jpeg1_list[] = {
	[0] = &exynos4_clk_mout_epll.clk,
	[1] = &exynos4_clk_sclk_vpll.clk,
};

struct clksrc_sources exynos4212_clkset_mout_jpeg1 = {
	.sources	= exynos4212_clkset_mout_jpeg1_list,
	.nr_sources	= ARRAY_SIZE(exynos4212_clkset_mout_jpeg1_list),
};

struct clksrc_clk exynos4212_clk_mout_jpeg1 = {
	.clk	= {
		.name		= "mout_jpeg1",
	},
	.sources = &exynos4212_clkset_mout_jpeg1,
	.reg_src = { .reg = EXYNOS4_CLKSRC_CAM1, .shift = 4, .size = 1 },
};

static struct clk *exynos4212_clkset_mout_jpeg_list[] = {
	[0] = &exynos4212_clk_mout_jpeg0.clk,
	[1] = &exynos4212_clk_mout_jpeg1.clk,
};

static struct clksrc_sources exynos4212_clkset_mout_jpeg = {
	.sources	= exynos4212_clkset_mout_jpeg_list,
	.nr_sources	= ARRAY_SIZE(exynos4212_clkset_mout_jpeg_list),
};

struct clksrc_clk exynos4212_clk_aclk_jpeg = {
	.clk    = {
		.name           = "aclk_clk_jpeg",
	},
	.sources = &exynos4212_clkset_mout_jpeg,
	.reg_src = { .reg = EXYNOS4_CLKSRC_CAM1, .shift = 8, .size = 1 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_CAM1, .shift = 0, .size = 4 },
};

static struct clk *exynos4212_clkset_c2c_list[] = {
	[0] = &exynos4_clk_mout_mpll.clk,
	[1] = &exynos4_clk_sclk_apll.clk,
};

static struct clksrc_sources exynos4212_clkset_sclk_c2c = {
	.sources	= exynos4212_clkset_c2c_list,
	.nr_sources	= ARRAY_SIZE(exynos4212_clkset_c2c_list),
};

static struct clksrc_clk exynos4212_clk_sclk_c2c = {
	.clk	= {
		.name		= "sclk_c2c",
		.id		= -1,
	},
	.sources = &exynos4212_clkset_sclk_c2c,
	.reg_src = { .reg = EXYNOS4_CLKSRC_DMC, .shift = 0, .size = 1 },
	.reg_div = { .reg = EXYNOS4_CLKDIV_DMC1, .shift = 4, .size = 3 },
};

static struct clksrc_clk exynos4212_clk_aclk_c2c = {
	.clk	= {
		.name		= "aclk_c2c",
		.id		= -1,
		.parent		= &exynos4212_clk_sclk_c2c.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_DMC1, .shift = 12, .size = 3 },
};

static struct clksrc_clk *exynos4212_sysclks[] = {
	&exynos4212_clk_mout_mpll_user,
	&exynos4212_clk_aclk_gdl_user,
	&exynos4212_clk_aclk_gdr_user,
	&exynos4212_clk_mout_aclk_400_mcuisp,
	&exynos4212_clk_mout_aclk_266,
	&exynos4212_clk_mout_aclk_200,
	&exynos4212_clk_dout_aclk_200,
	&exynos4212_clk_aclk_400_mcuisp,
	&exynos4212_clk_aclk_266,
	&exynos4212_clk_mout_jpeg0,
	&exynos4212_clk_mout_jpeg1,
	&exynos4212_clk_aclk_jpeg,
	&exynos4212_clk_sclk_c2c,
	&exynos4212_clk_aclk_c2c,
};

static struct clk exynos4212_init_clocks_off[] = {
	{
		.name		= "mipihsi",
		.parent		= &exynos4_clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "ppmuisp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 20 | 1 << 21),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(g2d_acp, 15),
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 24),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(is_isp, 16),
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(is_drc, 17),
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(is_fd, 18),
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(is_cpu, 19),
		.enable		= exynos4212_clk_ip_isp1_ctrl,
		.ctrlbit	= (1 << 4),
	},
#ifndef CONFIG_SAMSUNG_C2C
	{
		.name		= "c2c",
		.devname	= "samsung-c2c",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 26 | 1 << 27 | 1 << 30),
	}, {
		.name		= "sclk_c2c_off",
		.enable		= exynos4212_clk_sclk_dmc_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "pclk_c2c_off",
		.enable		= exynos4212_clk_bus_dmc1_ctrl,
		.ctrlbit	= (1 << 27 | 1 << 30),
	}, {
		.name		= "aclk_c2c_off",
		.enable		= exynos4212_clk_bus_dmc0_ctrl,
		.ctrlbit	= (1 << 21 | 1 << 22 | 1 << 24),
	},
#endif
	{
		.name		= "uart_isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 31),
	}, {
		.name		= "wdt_isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 30),
	}, {
		.name		= "pwm_isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 28),
	}, {
		.name		= "mtcadc",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 27),
	}, {
		.name		= "i2c1_isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 26),
	}, {
		.name		= "i2c0_isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 25),
	}, {
		.name		= "mpwm_isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 24),
	}, {
		.name		= "mcuctl_isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 23),
	}, {
		.name		= "qelite1",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "qelite0",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "qefd",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "qedrc",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "qeisp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "sysmmu_lite1",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "sysmmu_lite0",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "gic_isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "mcu_isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "lite1",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "lite0",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "fd",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "drc",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "spi1_isp",
		.enable		= exynos4212_clk_ip_isp1_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "spi0_isp",
		.enable		= exynos4212_clk_ip_isp1_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "aync_caxim",
		.enable		= exynos4212_clk_ip_isp1_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "sysmmu_fd",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "sysmmu_drc",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "sysmmu_isp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "sysmmu_ispcx",
		.enable		= exynos4212_clk_ip_isp1_ctrl,
		.ctrlbit	= (1 << 4),
	},
};

static struct clksrc_clk exynos4212_clksrcs[] = {
	{
		.clk	= {
			.name		= "sclk_mipihsi",
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &exynos4_clkset_mout_corebus,
		.reg_src = { .reg = EXYNOS4_CLKSRC_FSYS, .shift = 24, .size = 1 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS0, .shift = 20, .size = 4 },
	},
};

static struct clk exynos4212_clk_isp[] = {
	{
		.name	= "aclk_400_mcuisp_muxed",
		.parent = &exynos4212_clk_aclk_400_mcuisp.clk,
	}, {
		.name	= "aclk_200_muxed",
		.parent	= &exynos4_clk_aclk_200.clk,
	},
};

static struct clksrc_clk exynos4212_clk_isp_srcs_div0 = {
	.clk		= {
		.name		= "sclk_mcuisp_div0",
		.parent		= &exynos4212_clk_aclk_400_mcuisp.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_ISP1, .shift = 4, .size = 3 },
};

static struct clksrc_clk exynos4212_clk_isp_srcs[] = {
	{
		.clk		= {
			.name		= "sclk_mcuisp_div1",
			.parent = &exynos4212_clk_isp_srcs_div0.clk,
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP1, .shift = 8, .size = 3 },
	}, {
		.clk		= {
			.name		= "sclk_aclk_div0",
			.parent = &exynos4_clk_aclk_200.clk,
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP0, .shift = 0, .size = 3 },
	}, {
		.clk		= {
			.name		= "sclk_aclk_div1",
			.parent = &exynos4_clk_aclk_200.clk,
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP0, .shift = 4, .size = 3 },
	}, {
		.clk		= {
			.name		= "sclk_uart_isp",
			.enable		= exynos4212_clk_ip_isp_ctrl,
			.ctrlbit	= (1 << 3),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_ISP, .shift = 12, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP, .shift = 28, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_spi1_isp",
			.enable		= exynos4212_clk_ip_isp_ctrl,
			.ctrlbit	= (1 << 2),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_ISP, .shift = 8, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP, .shift = 16, .size = 12 },
	}, {
		.clk		= {
			.name		= "sclk_spi0_isp",
			.enable		= exynos4212_clk_ip_isp_ctrl,
			.ctrlbit	= (1 << 1),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_ISP, .shift = 4, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP, .shift = 4, .size = 12 },
	}, {
		.clk		= {
			.name		= "sclk_pwm_isp",
			.enable		= exynos4212_clk_ip_isp_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_ISP, .shift = 0, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP, .shift = 0, .size = 4 },
	},
};
#if defined(CONFIG_BOARD_ODROID_X2) || defined(CONFIG_BOARD_ODROID_Q2) || defined(CONFIG_BOARD_ODROID_U2)
static struct vpll_div_data vpll_div_4212[] = {
        {54000000, 2, 72, 4, 0, 0, 0, 0},
        {108000000, 2, 72, 3, 0, 0, 0, 0},
        {160000000, 3, 160, 3, 0, 0, 0, 0},
        {200000000, 3, 200, 3, 0, 0, 0, 0},  
        {266000000, 3, 133, 2, 0, 0, 0, 0},
        {275000000, 2, 92, 2, 43692, 0, 0, 0},
        {300000000, 2, 100, 2, 0, 0, 0, 0},
        {333000000, 2, 111, 2, 0, 0, 0, 0},
        {350000000, 3, 175, 2, 0, 0, 0, 0},
        {400000000, 3, 100, 1, 0, 0, 0, 0},
        {440000000, 3, 110, 1, 0, 0, 0, 0},
        {500000000, 2, 166, 2, 0, 0, 0, 0},
        {533000000, 3, 133, 1, 16384, 0, 0, 0},
        {600000000, 2, 100, 1, 0, 0, 0, 0},
        {640000000, 3, 160, 1, 0, 0, 0, 0},
        {666000000, 2, 111, 1, 0, 0, 0, 0},
        {700000000, 3, 175, 1, 0, 0, 0, 0},
        {733000000, 2, 122, 1, 0, 0, 0, 0},
        {750000000, 2, 125, 1, 0, 0, 0, 0},
        {800000000, 2, 133, 1, 0, 0, 0, 0},
};
#elif defined(CONFIG_BOARD_ODROID_X) || defined(CONFIG_BOARD_ODROID_Q) || defined(CONFIG_BOARD_ODROID_U)
static struct vpll_div_data vpll_div_4212[] = {
        {54000000, 2, 72, 4, 0, 0, 0, 0},
        {108000000, 2, 72, 3, 0, 0, 0, 0},
        {160000000, 3, 160, 3, 0, 0, 0, 0},
        {200000000, 3, 200, 3, 0, 0, 0, 0},
        {266000000, 3, 133, 2, 0, 0, 0, 0},
        {275000000, 2, 92, 2, 43692, 0, 0, 0},
        {300000000, 2, 100, 2, 0, 0, 0, 0},
        {333000000, 2, 111, 2, 0, 0, 0, 0},
        {350000000, 3, 175, 2, 0, 0, 0, 0},
        {400000000, 3, 100, 1, 0, 0, 0, 0},
        {440000000, 3, 110, 1, 0, 0, 0, 0},
        {500000000, 2, 166, 2, 0, 0, 0, 0},
        {533000000, 3, 133, 1, 16384, 0, 0, 0},
};
#endif

static unsigned long exynos4212_vpll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int exynos4212_vpll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int vpll_con0, vpll_con1;
	unsigned int i;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	vpll_con0 = __raw_readl(EXYNOS4_VPLL_CON0);
	vpll_con0 &= ~(PLL36XX_MDIV_MASK << PLL36XX_MDIV_SHIFT |	\
		       PLL36XX_PDIV_MASK << PLL36XX_PDIV_SHIFT |	\
		       PLL36XX_SDIV_MASK << PLL36XX_SDIV_SHIFT);

	vpll_con1 = __raw_readl(EXYNOS4_VPLL_CON1);
	vpll_con1 &= ~(0xffff << 0);

	for (i = 0; i < ARRAY_SIZE(vpll_div_4212); i++) {
		if (vpll_div_4212[i].rate == rate) {
			vpll_con0 |= vpll_div_4212[i].pdiv << PLL36XX_PDIV_SHIFT;
			vpll_con0 |= vpll_div_4212[i].mdiv << PLL36XX_MDIV_SHIFT;
			vpll_con0 |= vpll_div_4212[i].sdiv << PLL36XX_SDIV_SHIFT;
			vpll_con1 |= vpll_div_4212[i].k << 0;
			break;
		}
	}

	if (i == ARRAY_SIZE(vpll_div_4212)) {
		printk(KERN_ERR "%s: Invalid Clock VPLL Frequency\n",
				__func__);
		return -EINVAL;
	}

	__raw_writel(vpll_con0, EXYNOS4_VPLL_CON0);
	__raw_writel(vpll_con1, EXYNOS4_VPLL_CON1);

	clk->rate = rate;

	return 0;
}
#ifdef CONFIG_PM
static int exynos4212_clock_suspend(void)
{
	s3c_pm_do_save(exynos4212_clock_save, ARRAY_SIZE(exynos4212_clock_save));
	s3c_pm_do_save(exynos4212_vpll_save, ARRAY_SIZE(exynos4212_vpll_save));
	s3c_pm_do_save(exynos4212_epll_save, ARRAY_SIZE(exynos4212_epll_save));

	return 0;
}

static void exynos4212_clock_resume(void)
{
	s3c_pm_do_restore_core(exynos4212_clock_save, ARRAY_SIZE(exynos4212_clock_save));
	s3c_pm_do_restore_core(exynos4212_vpll_save, ARRAY_SIZE(exynos4212_vpll_save));
	s3c_pm_do_restore_core(exynos4212_epll_save, ARRAY_SIZE(exynos4212_epll_save));
}
#else
#define exynos4212_clock_suspend NULL
#define exynos4212_clock_resume NULL
#endif

struct syscore_ops exynos4212_clock_syscore_ops = {
	.suspend        = exynos4212_clock_suspend,
	.resume         = exynos4212_clock_resume,
};

void __init exynos4212_register_clocks(void)
{
	int ptr;

	/* usbphy1 is removed in exynos 4212 */
	exynos4_clkset_group_list[4] = NULL;

	/* mout_mpll_user is used instead of mout_mpll in exynos 4212 */
	exynos4_clkset_group_list[6] = &exynos4212_clk_mout_mpll_user.clk;
	exynos4_clkset_aclk_top_list[0] = &exynos4212_clk_mout_mpll_user.clk;
	exynos4_clkset_mout_mfc0_list[0] = &exynos4212_clk_mout_mpll_user.clk;

	exynos4_clk_mout_mpll.reg_src.reg = EXYNOS4_CLKSRC_DMC;
	exynos4_clk_mout_mpll.reg_src.shift = 12;
	exynos4_clk_mout_mpll.reg_src.size = 1;

	exynos4_clk_aclk_200.sources = &exynos4212_clkset_aclk_200;
	exynos4_clk_aclk_200.reg_src.reg = EXYNOS4_CLKSRC_TOP1;
	exynos4_clk_aclk_200.reg_src.shift = 20;
	exynos4_clk_aclk_200.reg_src.size = 1;

	exynos4_clk_fimg2d.enable = exynos4_clk_ip_dmc_ctrl;
	exynos4_clk_fimg2d.ctrlbit = (1 << 23);

	exynos4_clk_mout_g2d0.reg_src.reg = EXYNOS4_CLKSRC_DMC;
	exynos4_clk_mout_g2d0.reg_src.shift = 20;
	exynos4_clk_mout_g2d0.reg_src.size = 1;

	exynos4_clk_mout_g2d1.reg_src.reg = EXYNOS4_CLKSRC_DMC;
	exynos4_clk_mout_g2d1.reg_src.shift = 24;
	exynos4_clk_mout_g2d1.reg_src.size = 1;

	exynos4_clk_sclk_fimg2d.reg_src.reg = EXYNOS4_CLKSRC_DMC;
	exynos4_clk_sclk_fimg2d.reg_src.shift = 28;
	exynos4_clk_sclk_fimg2d.reg_src.size = 1;
	exynos4_clk_sclk_fimg2d.reg_div.reg = EXYNOS4_CLKDIV_DMC1;
	exynos4_clk_sclk_fimg2d.reg_div.shift = 0;
	exynos4_clk_sclk_fimg2d.reg_div.size = 4;

	exynos4_vpll_ops.get_rate = exynos4212_vpll_get_rate;
	exynos4_vpll_ops.set_rate = exynos4212_vpll_set_rate;

	for (ptr = 0; ptr < ARRAY_SIZE(exynos4212_sysclks); ptr++)
		s3c_register_clksrc(exynos4212_sysclks[ptr], 1);

	s3c_register_clksrc(exynos4212_clksrcs, ARRAY_SIZE(exynos4212_clksrcs));

	s3c_register_clocks(exynos4212_init_clocks_off, ARRAY_SIZE(exynos4212_init_clocks_off));
	s3c_disable_clocks(exynos4212_init_clocks_off, ARRAY_SIZE(exynos4212_init_clocks_off));

	s3c_register_clksrc(&exynos4212_clk_isp_srcs_div0, 1);
	s3c_register_clksrc(exynos4212_clk_isp_srcs, ARRAY_SIZE(exynos4212_clk_isp_srcs));
	s3c_register_clocks(exynos4212_clk_isp, ARRAY_SIZE(exynos4212_clk_isp));
	s3c_disable_clocks(&exynos4212_clk_isp_srcs[3].clk, 1);
	s3c_disable_clocks(&exynos4212_clk_isp_srcs[4].clk, 1);
	s3c_disable_clocks(&exynos4212_clk_isp_srcs[5].clk, 1);
	s3c_disable_clocks(&exynos4212_clk_isp_srcs[6].clk, 1);

	register_syscore_ops(&exynos4212_clock_syscore_ops);
}
