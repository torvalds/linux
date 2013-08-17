/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Clock support for EXYNOS5 SoCs
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
#include <plat/pm.h>

#include <mach/map.h>
#include <mach/sysmmu.h>
#include <mach/regs-clock.h>

#include <media/exynos_fimc_is.h>

#include "common.h"

#ifdef CONFIG_PM_SLEEP
static struct sleep_save exynos5_clock_save[] = {
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_TOP),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_GSCL),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_DISP1_0),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_FSYS),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_MAUDIO),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_PERIC0),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_PERIC1),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_CORE),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_SYSRGT),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_ACP),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_SYSLFT),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_GSCL),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_DISP1),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_MFC),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_G3D),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_GEN),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_FSYS),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_PERIC),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_PERIS),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_CDREX),
	SAVE_ITEM(EXYNOS5_CLKGATE_BLOCK),
	SAVE_ITEM(EXYNOS5_CLKGATE_BUS_SYSLFT),
	SAVE_ITEM(EXYNOS5_CLKDIV_ACP),
	SAVE_ITEM(EXYNOS5_CLKDIV_TOP0),
	SAVE_ITEM(EXYNOS5_CLKDIV_TOP1),
	SAVE_ITEM(EXYNOS5_CLKDIV_GSCL),
	SAVE_ITEM(EXYNOS5_CLKDIV_DISP1_0),
	SAVE_ITEM(EXYNOS5_CLKDIV_GEN),
	SAVE_ITEM(EXYNOS5_CLKDIV_MAUDIO),
	SAVE_ITEM(EXYNOS5_CLKDIV_FSYS0),
	SAVE_ITEM(EXYNOS5_CLKDIV_FSYS1),
	SAVE_ITEM(EXYNOS5_CLKDIV_FSYS2),
	SAVE_ITEM(EXYNOS5_CLKDIV_FSYS3),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC0),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC1),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC2),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC3),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC4),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC5),
	SAVE_ITEM(EXYNOS5_CLKDIV2_RATIO0),
	SAVE_ITEM(EXYNOS5_CLKDIV2_RATIO1),
	SAVE_ITEM(EXYNOS5_CLKDIV4_RATIO),
	SAVE_ITEM(EXYNOS5_CLKSRC_TOP0),
	SAVE_ITEM(EXYNOS5_CLKSRC_TOP1),
	SAVE_ITEM(EXYNOS5_CLKSRC_TOP2),
	SAVE_ITEM(EXYNOS5_CLKSRC_TOP3),
	SAVE_ITEM(EXYNOS5_CLKSRC_GSCL),
	SAVE_ITEM(EXYNOS5_CLKSRC_DISP1_0),
	SAVE_ITEM(EXYNOS5_CLKSRC_MAUDIO),
	SAVE_ITEM(EXYNOS5_CLKSRC_FSYS),
	SAVE_ITEM(EXYNOS5_CLKSRC_PERIC0),
	SAVE_ITEM(EXYNOS5_CLKSRC_PERIC1),
	SAVE_ITEM(EXYNOS5_EPLL_CON0),
	SAVE_ITEM(EXYNOS5_EPLL_CON1),
	SAVE_ITEM(EXYNOS5_EPLL_CON2),
	SAVE_ITEM(EXYNOS5_VPLL_CON0),
	SAVE_ITEM(EXYNOS5_VPLL_CON1),
	SAVE_ITEM(EXYNOS5_VPLL_CON2),
	SAVE_ITEM(EXYNOS5_PWR_CTRL1),
	SAVE_ITEM(EXYNOS5_PWR_CTRL2),
	SAVE_ITEM(EXYNOS5_GPLL_CON0),
	SAVE_ITEM(EXYNOS5_GPLL_CON1),
};
#endif

static struct clk exynos5_clk_sclk_dptxphy = {
	.name		= "sclk_dptx",
};

static struct clk exynos5_clk_sclk_hdmi24m = {
	.name		= "sclk_hdmi24m",
	.rate		= 24000000,
};

static struct clk exynos5_clk_sclk_hdmi27m = {
	.name		= "sclk_hdmi27m",
	.rate		= 27000000,
};

static struct clk exynos5_clk_sclk_hdmiphy = {
	.name		= "sclk_hdmiphy",
};

static struct clk exynos5_clk_sclk_usbphy = {
	.name		= "sclk_usbphy",
	.rate		= 48000000,
};

static struct clk dummy_apb_pclk = {
	.name		= "apb_pclk",
};

struct clksrc_clk exynos5_clk_audiocdclk0 = {
	.clk	= {
		.name		= "audiocdclk",
		.rate		= 16934400,
	},
};

static struct clk exynos5_clk_audiocdclk1 = {
	.name           = "audiocdclk",
};

static struct clk exynos5_clk_audiocdclk2 = {
	.name		= "audiocdclk",
};

static struct clk exynos5_clk_spdifcdclk = {
	.name		= "spdifcdclk",
};

/*
 * MOUT_BPLL_FOUT
 * No need .ctrlbit, this is always on
 */
static struct clk clk_fout_bpll_div2 = {
	.name		= "fout_bpll_div2",
	.id			= -1,
};

/*
 * MOUT_MPLL_FOUT
 * No need .ctrlbit, this is always on
 */
static struct clk clk_fout_mpll_div2 = {
	.name		= "fout_mpll_div2",
	.id			= -1,
};

/* GPLL clock output */
static struct clk clk_fout_gpll = {
	.name		= "fout_gpll",
	.id			= -1,
};

/*
 * This clock is for only mif dvfs virtually.
 */
static struct clk exynos5_mif_clk = {
	.name		= "mif_clk",
	.id		= -1,
};

/*
 * This clock is for only int dvfs virtually.
 */
static struct clk exynos5_int_clk = {
	.name		= "int_clk",
	.id		= -1,
};

static int exynos5_clksrc_mask_top_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_TOP, clk, enable);
}

static int exynos5_clksrc_mask_peric1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_PERIC1, clk, enable);
}

static int exynos5_clksrc_mask_disp1_0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_DISP1_0, clk, enable);
}

static int exynos5_clksrc_mask_maudio_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_MAUDIO, clk, enable);
}

static int exynos5_clksrc_mask_fsys_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_FSYS, clk, enable);
}

static int exynos5_clk_ip_gscl_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_GSCL, clk, enable);
}

static int exynos5_clksrc_mask_gscl_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_GSCL, clk, enable);
}

static int exynos5_clksrc_mask_peric0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_PERIC0, clk, enable);
}

static int exynos5_clksrc_mask_gen_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_GEN, clk, enable);
}

static int exynos5_clk_ip_acp_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_ACP, clk, enable);
}

static int exynos5_clk_ip_core_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_CORE, clk, enable);
}

static int exynos5_clk_ip_disp1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_DISP1, clk, enable);
}

static int exynos5_clk_ip_fsys_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_FSYS, clk, enable);
}

static int exynos5_clk_ip_sysrgt_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_SYSRGT, clk, enable);
}

static int exynos5_clk_block_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_BLOCK, clk, enable);
}

static int exynos5_clk_ip_gen_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_GEN, clk, enable);
}

static int exynos5_clk_ip_mfc_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_MFC, clk, enable);
}

static int exynos5_clk_ip_g3d_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_G3D, clk, enable);
}

static int exynos5_clk_ip_peric_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_PERIC, clk, enable);
}

static int exynos5_clk_ip_peris_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_PERIS, clk, enable);
}

static int exynos5_clk_ip_isp0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_ISP0, clk, enable);
}

static int exynos5_clk_ip_isp1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_ISP1, clk, enable);
}

static int exynos5_clk_bus_syslft_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_BUS_SYSLFT, clk, enable);
}

static int exynos5_clk_clkout_ctrl(struct clk *clk, int enable)
{
	/*
	 * Setting the bit disable the clock
	 * and clearing it enables the clock
	 */
	return s5p_gatectrl(EXYNOS_PMU_DEBUG, clk, !enable);
}

/* Core list of CMU_CPU side */

static struct clksrc_clk exynos5_clk_mout_apll = {
	.clk	= {
		.name		= "mout_apll",
	},
	.sources = &clk_src_apll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_CPU, .shift = 0, .size = 1 },
};

static struct clksrc_clk exynos5_clk_sclk_apll = {
	.clk	= {
		.name		= "sclk_apll",
		.parent		= &exynos5_clk_mout_apll.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_CPU0, .shift = 24, .size = 3 },
};

/* Possible clock source for BPLL_FOUT Mux */
static struct clk *exynos5_clkset_mout_bpll_fout_list[] = {
	[0] = &clk_fout_bpll_div2,
	[1] = &clk_fout_bpll,
};

static struct clksrc_sources exynos5_clkset_mout_bpll_fout = {
	.sources	= exynos5_clkset_mout_bpll_fout_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_mout_bpll_fout_list),
};

static struct clksrc_clk exynos5_clk_mout_bpll_fout = {
	.clk    = {
		.name		= "mout_bpll_fout",
	},
	.sources = &exynos5_clkset_mout_bpll_fout,
	.reg_src = { .reg = EXYNOS5_PLL_DIV2_SEL, .shift = 0, .size = 1 },
};

/* Possible clock source for BPLL Mux */
static struct clk *exynos5_clkset_mout_bpll_list[] = {
	[0] = &clk_fin_bpll,
	[1] = &exynos5_clk_mout_bpll_fout.clk,
};

static struct clksrc_sources exynos5_clkset_mout_bpll = {
	.sources        = exynos5_clkset_mout_bpll_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_mout_bpll_list),
};

static struct clksrc_clk exynos5_clk_mout_bpll = {
	.clk	= {
		.name		= "mout_bpll",
	},
	.sources = &exynos5_clkset_mout_bpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_CDREX, .shift = 0, .size = 1 },
};

static struct clk *exynos5_clk_src_bpll_user_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos5_clk_mout_bpll.clk,
};

static struct clksrc_sources exynos5_clk_src_bpll_user = {
	.sources	= exynos5_clk_src_bpll_user_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clk_src_bpll_user_list),
};

static struct clksrc_clk exynos5_clk_mout_bpll_user = {
	.clk	= {
		.name		= "mout_bpll_user",
	},
	.sources = &exynos5_clk_src_bpll_user,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 24, .size = 1 },
};

static struct clksrc_clk exynos5_clk_mout_cpll = {
	.clk	= {
		.name		= "mout_cpll",
	},
	.sources = &clk_src_cpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 8, .size = 1 },
};

static struct clksrc_clk exynos5_clk_mout_epll = {
	.clk	= {
		.name		= "mout_epll",
	},
	.sources = &clk_src_epll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 12, .size = 1 },
};

/* Possible clock source for MPLL_FOUT Mux */
static struct clk *exynos5_clkset_mout_mpll_fout_list[] = {
	[0] = &clk_fout_mpll_div2,
	[1] = &clk_fout_mpll,
};

static struct clksrc_sources exynos5_clkset_mout_mpll_fout = {
	.sources	= exynos5_clkset_mout_mpll_fout_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_mout_mpll_fout_list),
};

static struct clksrc_clk exynos5_clk_mout_mpll_fout = {
	.clk    = {
		.name           = "mout_mpll_fout",
	},
	.sources = &exynos5_clkset_mout_mpll_fout,
	.reg_src = { .reg = EXYNOS5_PLL_DIV2_SEL, .shift = 4, .size = 1 },
};

/* Possible clock source for MPLL Mux */
static struct clk *exynos5_clkset_mout_mpll_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos5_clk_mout_mpll_fout.clk,
};

static struct clksrc_sources exynos5_clkset_mout_mpll = {
	.sources	= exynos5_clkset_mout_mpll_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_mout_mpll_list),
};

struct clksrc_clk exynos5_clk_mout_mpll = {
	.clk = {
		.name		= "mout_mpll",
	},
	.sources = &exynos5_clkset_mout_mpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_CORE1, .shift = 8, .size = 1 },
};

/* Possible clock sources for GPLL Mux */
static struct clk *clk_src_gpll_list[] = {
	[0] = &clk_fin_gpll,
	[1] = &clk_fout_gpll,
};

static struct clksrc_sources clk_src_gpll = {
	.sources	= clk_src_gpll_list,
	.nr_sources = ARRAY_SIZE(clk_src_gpll_list),
};

static struct clksrc_clk exynos5_clk_mout_gpll = {
	.clk	= {
		.name		= "mout_gpll"
	},
	.sources = &clk_src_gpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 24, .size = 1},
};

static struct clk *exynos_clkset_vpllsrc_list[] = {
	[0] = &clk_fin_vpll,
	[1] = &exynos5_clk_sclk_hdmi27m,
};

static struct clksrc_sources exynos5_clkset_vpllsrc = {
	.sources	= exynos_clkset_vpllsrc_list,
	.nr_sources	= ARRAY_SIZE(exynos_clkset_vpllsrc_list),
};

static struct clksrc_clk exynos5_clk_vpllsrc = {
	.clk	= {
		.name		= "vpll_src",
		.enable		= exynos5_clksrc_mask_top_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos5_clkset_vpllsrc,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 0, .size = 1 },
};

static struct clk *exynos5_clkset_sclk_vpll_list[] = {
	[0] = &exynos5_clk_vpllsrc.clk,
	[1] = &clk_fout_vpll,
};

static struct clksrc_sources exynos5_clkset_sclk_vpll = {
	.sources	= exynos5_clkset_sclk_vpll_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_sclk_vpll_list),
};

static struct clksrc_clk exynos5_clk_sclk_vpll = {
	.clk	= {
		.name		= "sclk_vpll",
	},
	.sources = &exynos5_clkset_sclk_vpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 16, .size = 1 },
};

static struct clksrc_clk exynos5_clk_sclk_pixel = {
	.clk	= {
		.name		= "sclk_pixel",
		.parent		= &exynos5_clk_sclk_vpll.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 28, .size = 4 },
};

static struct clk *exynos5_clkset_sclk_hdmi_list[] = {
	[0] = &exynos5_clk_sclk_pixel.clk,
	[1] = &exynos5_clk_sclk_hdmiphy,
};

static struct clksrc_sources exynos5_clkset_sclk_hdmi = {
	.sources	= exynos5_clkset_sclk_hdmi_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_sclk_hdmi_list),
};

static struct clksrc_clk exynos5_clk_sclk_hdmi = {
	.clk	= {
		.name		= "sclk_hdmi",
		.enable		= exynos5_clksrc_mask_disp1_0_ctrl,
		.ctrlbit	= (1 << 20),
	},
	.sources = &exynos5_clkset_sclk_hdmi,
	.reg_src = { .reg = EXYNOS5_CLKSRC_DISP1_0, .shift = 20, .size = 1 },
};

static struct clk *exynos5_clkset_sclk_cec_list[] = {
	[0] = &exynos5_clk_sclk_pixel.clk,
	[1] = &exynos5_clk_sclk_hdmiphy,
};

static struct clksrc_sources exynos5_clkset_sclk_cec = {
	.sources	= exynos5_clkset_sclk_cec_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_sclk_cec_list),
};

static struct clksrc_clk exynos5_clk_sclk_cec = {
	.clk	= {
		.name           = "sclk_cec",
		.enable		= exynos5_clksrc_mask_disp1_0_ctrl,
		.ctrlbit	= (1 << 20),
	},
	.sources = &exynos5_clkset_sclk_cec,
	.reg_src = { .reg = EXYNOS5_CLKSRC_DISP1_0, .shift = 20, .size = 1 },
};

static struct clksrc_clk *exynos5_sclk_tv[] = {
	&exynos5_clk_sclk_pixel,
	&exynos5_clk_sclk_hdmi,
	&exynos5_clk_sclk_cec,
};

static struct clk *exynos5_clk_src_mpll_user_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos5_clk_mout_mpll.clk,
};

static struct clksrc_sources exynos5_clk_src_mpll_user = {
	.sources	= exynos5_clk_src_mpll_user_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clk_src_mpll_user_list),
};

static struct clksrc_clk exynos5_clk_mout_mpll_user = {
	.clk	= {
		.name		= "mout_mpll_user",
	},
	.sources = &exynos5_clk_src_mpll_user,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 20, .size = 1 },
};

static struct clk *exynos5_clkset_mout_cpu_list[] = {
	[0] = &exynos5_clk_mout_apll.clk,
	[1] = &exynos5_clk_mout_mpll.clk,
};

static struct clksrc_sources exynos5_clkset_mout_cpu = {
	.sources	= exynos5_clkset_mout_cpu_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_mout_cpu_list),
};

static struct clksrc_clk exynos5_clk_mout_cpu = {
	.clk	= {
		.name		= "mout_cpu",
	},
	.sources = &exynos5_clkset_mout_cpu,
	.reg_src = { .reg = EXYNOS5_CLKSRC_CPU, .shift = 16, .size = 1 },
	.reg_src_stat = {.reg = EXYNOS5_CLKMUX_STATCPU, .shift = 16, .size = 3},
};

static struct clksrc_clk exynos5_clk_dout_armclk = {
	.clk	= {
		.name		= "dout_armclk",
		.parent		= &exynos5_clk_mout_cpu.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_CPU0, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos5_clk_dout_arm2clk = {
	.clk	= {
		.name		= "dout_arm2clk",
		.parent		= &exynos5_clk_dout_armclk.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_CPU0, .shift = 28, .size = 3 },
};

static struct clk exynos5_clk_armclk = {
	.name		= "armclk",
	.parent		= &exynos5_clk_dout_arm2clk.clk,
};

/* Core list of CMU_CDREX side */

static struct clk *exynos5_clkset_cdrex_list[] = {
	[0] = &exynos5_clk_mout_mpll.clk,
	[1] = &exynos5_clk_mout_bpll.clk,
};

static struct clksrc_sources exynos5_clkset_cdrex = {
	.sources	= exynos5_clkset_cdrex_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_cdrex_list),
};

static struct clksrc_clk exynos5_clk_mclk_cdrex = {
	.clk	= {
		.name		= "mclk_cdrex",
	},
	.sources = &exynos5_clkset_cdrex,
	.reg_src = { .reg = EXYNOS5_CLKSRC_CDREX, .shift = 4, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_CDREX, .shift = 28, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_acp = {
	.clk	= {
		.name		= "aclk_acp",
		.parent		= &exynos5_clk_mout_mpll.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ACP, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos5_clk_pclk_acp = {
	.clk	= {
		.name		= "pclk_acp",
		.parent		= &exynos5_clk_aclk_acp.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ACP, .shift = 4, .size = 3 },
};

/* Core list of CMU_TOP side */

struct clk *exynos5_clkset_aclk_top_list[] = {
	[0] = &exynos5_clk_mout_mpll_user.clk,
	[1] = &exynos5_clk_mout_bpll_user.clk,
};

struct clksrc_sources exynos5_clkset_aclk = {
	.sources	= exynos5_clkset_aclk_top_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_aclk_top_list),
};

static struct clksrc_clk exynos5_clk_aclk_400_g3d_mid = {
	.clk	= {
		.name		= "aclk_400_g3d_mid",
	},
	.sources = &exynos5_clkset_aclk,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 20, .size = 1 },
};

static struct clk *exynos5_clkset_aclk_g3d_list[] = {
	[0] = &exynos5_clk_aclk_400_g3d_mid.clk,
	[1] = &exynos5_clk_mout_gpll.clk,
};

static struct clksrc_sources exynos5_clkset_aclk_g3d = {
	.sources	= exynos5_clkset_aclk_g3d_list,
	.nr_sources = ARRAY_SIZE(exynos5_clkset_aclk_g3d_list),
};

static struct clksrc_clk exynos5_clk_aclk_400_g3d = {
	.clk	= {
		.name		= "aclk_400_g3d",
	},
	.sources = &exynos5_clkset_aclk_g3d,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP1, .shift = 28, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 24, .size = 3 },
};

struct clk *exynos5_clkset_aclk_333_166_list[] = {
	[0] = &exynos5_clk_mout_cpll.clk,
	[1] = &exynos5_clk_mout_mpll_user.clk,
};

struct clksrc_sources exynos5_clkset_aclk_333_166 = {
	.sources	= exynos5_clkset_aclk_333_166_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_aclk_333_166_list),
};

static struct clksrc_clk exynos5_clk_mout_aclk_333 = {
	.clk	= {
		.name		= "mout_aclk_333",
	},
	.sources = &exynos5_clkset_aclk_333_166,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 16, .size = 1 },
};

static struct clksrc_clk exynos5_clk_dout_aclk_333 = {
	.clk	= {
		.name		= "dout_aclk_333",
		.parent		= &exynos5_clk_mout_aclk_333.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 20, .size = 3 },
};

struct clk *exynos5_clkset_aclk_333_sub_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_dout_aclk_333.clk,
};

struct clksrc_sources exynos5_clkset_aclk_333_sub = {
	.sources	= exynos5_clkset_aclk_333_sub_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_aclk_333_sub_list),
};

static struct clksrc_clk exynos5_clk_aclk_333 = {
	.clk	= {
		.name		= "aclk_333",
	},
	.sources = &exynos5_clkset_aclk_333_sub,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 24, .size = 1 },
};

static struct clksrc_clk exynos5_clk_aclk_166 = {
	.clk	= {
		.name		= "aclk_166",
	},
	.sources = &exynos5_clkset_aclk_333_166,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 8, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 8, .size = 3 },
};

/* For ACLK_300_disp1_mid */
static struct clksrc_clk exynos5_clk_mout_aclk_300_disp1_mid = {
	.clk	= {
		.name		= "mout_aclk_300_disp1_mid",
	},
	.sources = &exynos5_clkset_aclk,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 14, .size = 1 },
};

static struct clk *clk_src_mid1_list[] = {
	[0] = &exynos5_clk_sclk_vpll.clk,
	[1] = &exynos5_clk_mout_cpll.clk,
};

static struct clksrc_sources exynos5_clkset_mid1 = {
	.sources	= clk_src_mid1_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mid1_list),
};

/* For ACLK_300_disp1_mid1 */
static struct clksrc_clk exynos5_clk_mout_aclk_300_disp1_mid1 = {
	.clk	= {
		.name		= "mout_aclk_300_disp1_mid1",
	},
	.sources = &exynos5_clkset_mid1,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP1, .shift = 8, .size = 1 },
};

/* For ACLK_300_disp1 */
struct clk *exynos5_clkset_mout_aclk_300_disp1_list[] = {
	[0] = &exynos5_clk_mout_aclk_300_disp1_mid.clk,
	[1] = &exynos5_clk_mout_aclk_300_disp1_mid1.clk,
};

struct clksrc_sources exynos5_clkset_mout_aclk_300_disp1 = {
	.sources	= exynos5_clkset_mout_aclk_300_disp1_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_mout_aclk_300_disp1_list),
};

static struct clksrc_clk exynos5_clk_mout_aclk_300_disp1 = {
	.clk	= {
		.name		= "mout_aclk_300_disp1",
	},
	.sources = &exynos5_clkset_mout_aclk_300_disp1,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 15, .size = 1 },
};

static struct clksrc_clk exynos5_clk_dout_aclk_300_disp1 = {
	.clk	= {
		.name		= "dout_aclk_300_disp1",
		.parent		= &exynos5_clk_mout_aclk_300_disp1.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 28, .size = 3 },
};

static struct clk *clk_src_aclk_300_disp1_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_dout_aclk_300_disp1.clk,
};

static struct clksrc_sources exynos5_clkset_aclk_300_disp1 = {
	.sources	= clk_src_aclk_300_disp1_list,
	.nr_sources	= ARRAY_SIZE(clk_src_aclk_300_disp1_list),
};

static struct clksrc_clk exynos5_clk_aclk_300_disp1 = {
	.clk	= {
		.name		= "aclk_300_disp1",
	},
	.sources = &exynos5_clkset_aclk_300_disp1,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 6, .size = 1 },
};


/* For ACLK_300_gscl_mid */
static struct clksrc_clk exynos5_clk_mout_aclk_300_gscl_mid = {
	.clk	= {
		.name		= "mout_aclk_300_gscl_mid",
	},
	.sources = &exynos5_clkset_aclk,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 24, .size = 1 },
};

/* For ACLK_300_gscl_mid1 */
static struct clksrc_clk exynos5_clk_mout_aclk_300_gscl_mid1 = {
	.clk	= {
		.name		= "mout_aclk_300_gscl_mid1",
	},
	.sources = &exynos5_clkset_mid1,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP1, .shift = 12, .size = 1 },
};

/* For ACLK_300_gscl */
struct clk *exynos5_clkset_aclk_300_gscl_list[] = {
	[0] = &exynos5_clk_mout_aclk_300_gscl_mid.clk,
	[1] = &exynos5_clk_mout_aclk_300_gscl_mid1.clk,
};

struct clksrc_sources exynos5_clkset_aclk_300_gscl = {
	.sources	= exynos5_clkset_aclk_300_gscl_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_aclk_300_gscl_list),
};

static struct clksrc_clk exynos5_clk_mout_aclk_300_gscl = {
	.clk	= {
		.name		= "mout_aclk_300_gscl",
	},
	.sources = &exynos5_clkset_aclk_300_gscl,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 25, .size = 1 },
};

static struct clksrc_clk exynos5_clk_dout_aclk_300_gscl = {
	.clk	= {
		.name		= "dout_aclk_300_gscl",
		.parent		= &exynos5_clk_mout_aclk_300_gscl.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP1, .shift = 12, .size = 3 },
};

/* Possible clock sources for aclk_300_gscl_sub Mux */
static struct clk *clk_src_gscl_300_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_dout_aclk_300_gscl.clk,
};

static struct clksrc_sources clk_src_gscl_300 = {
	.sources	= clk_src_gscl_300_list,
	.nr_sources	= ARRAY_SIZE(clk_src_gscl_300_list),
};

static struct clksrc_clk exynos5_clk_aclk_300_gscl = {
	.clk	= {
		.name		= "aclk_300_gscl",
	},
	.sources = &clk_src_gscl_300,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 10, .size = 1 },
};

static struct clksrc_clk exynos5_clk_aclk_266 = {
	.clk	= {
		.name		= "aclk_266",
		.parent		= &exynos5_clk_mout_mpll_user.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 16, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_200 = {
	.clk	= {
		.name		= "aclk_200",
		.parent		= &exynos5_clk_mout_mpll_user.clk,
	},
	.sources = &exynos5_clkset_aclk,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 12, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 12, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_66_pre = {
	.clk	= {
		.name		= "aclk_66_pre",
		.parent		= &exynos5_clk_mout_mpll_user.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP1, .shift = 24, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_66 = {
	.clk	= {
		.name		= "aclk_66",
		.parent		= &exynos5_clk_aclk_66_pre.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 0, .size = 3 },
};

/* Possible clock sources for aclk_200_disp1_sub Mux */
static struct clk *clk_src_disp1_200_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_aclk_200.clk,
};

static struct clksrc_sources clk_src_disp1_200 = {
	.sources	= clk_src_disp1_200_list,
	.nr_sources	= ARRAY_SIZE(clk_src_disp1_200_list),
};

/* For CLKOUT */
struct clk *exynos5_clkset_clk_clkout_list[] = {
	/* Others are for debugging */
	[16] = &clk_xxti,
	[17] = &clk_xusbxti,
};

struct clksrc_sources exynos5_clkset_clk_clkout = {
	.sources	= exynos5_clkset_clk_clkout_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_clk_clkout_list),
};

static struct clksrc_clk exynos5_clk_clkout = {
	.clk	= {
		.name		= "clkout",
		.enable		= exynos5_clk_clkout_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos5_clkset_clk_clkout,
	.reg_src = { .reg = EXYNOS_PMU_DEBUG, .shift = 8, .size = 5 },
};

static int exynos5_gate_clk_set_parent(struct clk *clk, struct clk *parent)
{
	clk->parent = parent;
	return 0;
}

static struct clk_ops exynos5_gate_clk_ops = {
	.set_parent = exynos5_gate_clk_set_parent
};

static struct clk exynos5_init_clocks_off[] = {
	{
		.name		= "timers",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 24),
	}, {
		.name		= "hdmicec",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peris_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "watchdog",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peris_ctrl,
		.ctrlbit	= (1 << 19),
	}, {
		.name		= "rtc",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peris_ctrl,
		.ctrlbit	= (1 << 20),
	}, {
		.name		= "pkey0",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peris_ctrl,
		.ctrlbit	= (1 << 22),
	}, {
		.name		= "pkey1",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peris_ctrl,
		.ctrlbit	= (1 << 23),
	}, {
		.name		= "monocnt",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peris_ctrl,
		.ctrlbit	= (1 << 24),
	}, {
		.name		= "mipihsi",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "rtic",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= ((1 << 11) | (1 << 9)),
	}, {
		.name		= "hsmmc",
		.devname	= "exynos4-sdhci.0",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "hsmmc",
		.devname	= "exynos4-sdhci.1",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "hsmmc",
		.devname	= "exynos4-sdhci.2",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "hsmmc",
		.devname	= "exynos4-sdhci.3",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "dwmci",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "sromc",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "sata",
		.devname	= "ahci",
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "sata_phy",
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 24),
	}, {
		.name		= "sata_phy_i2c",
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 25),
	}, {
		.name		= "fimd",
		.devname        = "exynos5-fb.1",
		.enable		= exynos5_clk_ip_disp1_ctrl,
		.ctrlbit	= ((0x7 << 10) | (1 << 0)),
	}, {
		.name		= "dp",
		.devname        = "s5p-dp",
		.enable		= exynos5_clk_ip_disp1_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "mfc",
		.devname	= "s5p-mfc",
		.enable		= exynos5_clk_ip_mfc_ctrl,
		.ctrlbit	= ((1 << 4) | (1 << 3) | (1 << 0)),
	}, {
		.name		= "g3d",
		.devname	= "mali.0",
		.enable		= exynos5_clk_ip_g3d_ctrl,
		.ctrlbit	= ((1 << 1) | (1 << 0)),
	}, {
		.name		= "isp0",
		.devname	= FIMC_IS_MODULE_NAME,
		.enable		= exynos5_clk_ip_isp0_ctrl,
		.ctrlbit	= (0xDFFFC0FF << 0),
	}, {
		.name		= "isp1",
		.devname	= FIMC_IS_MODULE_NAME,
		.enable		= exynos5_clk_ip_isp1_ctrl,
		.ctrlbit	= (0x3F07 << 0),
	},{
		.name		= "hdmi",
		.devname	= "exynos5-hdmi",
		.enable		= exynos5_clk_ip_disp1_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "mixer",
		.devname	= "s5p-mixer",
		.enable		= exynos5_clk_ip_disp1_ctrl,
		.ctrlbit	= ((0xf << 13) | (1 << 5)),
	}, {
		.name		= "fimc-lite.0",
		.enable		= exynos5_clk_ip_gscl_ctrl,
		.ctrlbit	= ((1 << 13) | (1 << 0)),
	}, {
		.name		= "fimc-lite.1",
		.enable		= exynos5_clk_ip_gscl_ctrl,
		.ctrlbit	= ((1 << 14) | (1 << 0)),
	}, {
		.name		= "gscl",
		.devname	= "exynos-gsc.0",
		.enable		= exynos5_clk_ip_gscl_ctrl,
		.ctrlbit	= ((1 << 15) | (1 << 0)),
	}, {
		.name		= "gscl",
		.devname	= "exynos-gsc.1",
		.enable		= exynos5_clk_ip_gscl_ctrl,
		.ctrlbit	= ((1 << 16) | (1 << 1)),
	}, {
		.name		= "gscl",
		.devname	= "exynos-gsc.2",
		.enable		= exynos5_clk_ip_gscl_ctrl,
		.ctrlbit	= ((1 << 17) | (1 << 2)),
	}, {
		.name		= "gscl",
		.devname	= "exynos-gsc.3",
		.enable		= exynos5_clk_ip_gscl_ctrl,
		.ctrlbit	= ((1 << 18) | (1 << 3)),
	}, {
		.name		= "camif_top",
		.enable		= exynos5_clk_ip_gscl_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "gscl_wrap0",
		.devname	= "s5p-mipi-csis.0",
		.enable		= exynos5_clk_ip_gscl_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "gscl_wrap1",
		.devname	= "s5p-mipi-csis.1",
		.enable		= exynos5_clk_ip_gscl_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "rotator",
		.devname	= "exynos-rot",
		.enable		= exynos5_clk_ip_gen_ctrl,
		.ctrlbit	= ((1 << 11) | (1 << 1)),
	}, {
		.name		= "jpeg",
		.enable		= exynos5_clk_ip_gen_ctrl,
		.ctrlbit	= ((1 << 12) | (1 << 2)),
	}, {
		.name		= "smmu_mdma1",
		.enable		= exynos5_clk_ip_gen_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "dsim0",
		.enable		= exynos5_clk_ip_disp1_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.1",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 20),
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.2",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 21),
	}, {
		.name		= "pcm",
		.devname	= "samsung-pcm.1",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 22),
	}, {
		.name		= "pcm",
		.devname	= "samsung-pcm.2",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 23),
	}, {
		.name		= "spdif",
		.devname	= "samsung-spdif",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 26),
	}, {
		.name		= "ac97",
		.devname	= "samsung-ac97",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 27),
	}, {
		.name		= "uis",
		.devname	= "exynos-uis.0",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 28),
	}, {
		.name		= "uis",
		.devname	= "exynos-uis.1",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 29),
	}, {
		.name		= "uis",
		.devname	= "exynos-uis.2",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 30),
	}, {
		.name		= "uis",
		.devname	= "exynos-uis.3",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 31),
	}, {
		.name		= "usbhost",
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "usbotg",
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "usbdrd30",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 19),
	}, {
		.name		= "nfcon",
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 22),
	}, {
		.name		= "iop",
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= ((1 << 30) | (1 << 26) | (1 << 23)),
	}, {
		.name		= "core_iop",
		.enable		= exynos5_clk_ip_core_ctrl,
		.ctrlbit	= ((1 << 21) | (1 << 3)),
	}, {
		.name		= "mcu_iop",
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.0",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.1",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.2",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.3",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.4",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.5",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.6",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.7",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "i2c",
		.devname	= "s3c2440-i2c.8",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "adc",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(mfc_lr, 0),
		.enable		= &exynos5_clk_ip_mfc_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (3 << 1),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(tv, 2),
		.enable		= &exynos5_clk_ip_disp1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 9)
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(jpeg, 3),
		.enable		= &exynos5_clk_ip_gen_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(rot, 4),
		.enable		= &exynos5_clk_ip_gen_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 6)
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(gsc0, 5),
		.enable		= &exynos5_clk_ip_gscl_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(gsc1, 6),
		.enable		= &exynos5_clk_ip_gscl_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(gsc2, 7),
		.enable		= &exynos5_clk_ip_gscl_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(gsc3, 8),
		.enable		= &exynos5_clk_ip_gscl_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(isp0, 9),
		.enable		= &exynos5_clk_ip_isp0_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (0x3F << 8),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(fimd1, 11),
		.enable		= &exynos5_clk_ip_disp1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(isp1, 16),
		.enable		= &exynos5_clk_ip_isp1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (0xF << 4),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(camif0, 12),
		.enable		= &exynos5_clk_ip_gscl_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(camif1, 13),
		.enable		= &exynos5_clk_ip_gscl_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(camif2, 14),
		.enable		= &exynos5_clk_ip_gscl_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 20),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(2d, 15),
		.enable		= &exynos5_clk_ip_acp_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 7)
	}, {
		.name		= "fimg2d",
		.devname	= "s5p-fimg2d",
		.enable		= exynos5_clk_ip_acp_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "spi",
		.devname	= "s3c64xx-spi.0",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "spi",
		.devname	= "s3c64xx-spi.1",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "spi",
		.devname	= "s3c64xx-spi.2",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "efclk",
		.enable		= exynos5_clk_bus_syslft_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "mdma",
		.enable		= exynos5_clk_ip_acp_ctrl,
		.ctrlbit	= ((1 << 1) | (1 << 8)),
	}
};

static struct clk exynos5_init_clocks_on[] = {
	{
		.name		= "uart",
		.devname	= "exynos4210-uart.0",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "uart",
		.devname	= "exynos4210-uart.1",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "uart",
		.devname	= "exynos4210-uart.2",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "uart",
		.devname	= "exynos4210-uart.3",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "uart",
		.devname	= "exynos4210-uart.4",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "uart",
		.devname	= "exynos4210-uart.5",
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "secss",
		.parent		= &exynos5_clk_aclk_acp.clk,
		.enable		= exynos5_clk_ip_acp_ctrl,
		.ctrlbit	= (1 << 2),
	}
};

static struct clk exynos5_clk_pdma0 = {
	.name		= "dma",
	.devname	= "dma-pl330.0",
	.enable		= exynos5_clk_ip_fsys_ctrl,
	.ctrlbit	= (1 << 1),
};

static struct clk exynos5_clk_pdma1 = {
	.name		= "dma",
	.devname	= "dma-pl330.1",
	.enable		= exynos5_clk_ip_fsys_ctrl,
	.ctrlbit	= (1 << 2),
};

static struct clk exynos5_clk_mdma1 = {
	.name		= "dma",
	.devname	= "dma-pl330.2",
	.enable		= exynos5_clk_ip_gen_ctrl,
	.ctrlbit	= ((1 << 4) | (1 << 14)),
};

static struct clk exynos5_c2c_clock = {
	.name		= "c2c",
	.devname	= "samsung-c2c",
	.enable		= exynos5_clk_ip_sysrgt_ctrl,
	.ctrlbit	= ((1 << 2) | (1 << 1)),
};

static struct clk *clkset_sclk_audio0_list[] = {
	[0] = &exynos5_clk_audiocdclk0.clk,
	[1] = &clk_ext_xtal_mux,
	[2] = &exynos5_clk_sclk_hdmi27m,
	[3] = &exynos5_clk_sclk_dptxphy,
	[4] = &exynos5_clk_sclk_usbphy,
	[5] = &exynos5_clk_sclk_hdmiphy,
	[6] = &exynos5_clk_mout_mpll.clk,
	[7] = &exynos5_clk_mout_epll.clk,
	[8] = &exynos5_clk_sclk_vpll.clk,
	[9] = &exynos5_clk_mout_cpll.clk,
};

static struct clksrc_sources exynos5_clkset_sclk_audio0 = {
	.sources	= clkset_sclk_audio0_list,
	.nr_sources	= ARRAY_SIZE(clkset_sclk_audio0_list),
};

static struct clksrc_clk exynos5_clk_sclk_audio0 = {
	.clk	= {
		.name		= "sclk_audio",
		.enable		= exynos5_clksrc_mask_maudio_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos5_clkset_sclk_audio0,
	.reg_src = { .reg = EXYNOS5_CLKSRC_MAUDIO, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_MAUDIO, .shift = 0, .size = 4 },
};

static struct clk *exynos5_clkset_sclk_audio1_list[] = {
	[0] = &exynos5_clk_audiocdclk1,
	[1] = &clk_ext_xtal_mux,
	[2] = &exynos5_clk_sclk_hdmi27m,
	[3] = &exynos5_clk_sclk_dptxphy,
	[4] = &exynos5_clk_sclk_usbphy,
	[5] = &exynos5_clk_sclk_hdmiphy,
	[6] = &exynos5_clk_mout_mpll.clk,
	[7] = &exynos5_clk_mout_epll.clk,
	[8] = &exynos5_clk_sclk_vpll.clk,
	[9] = &exynos5_clk_mout_cpll.clk,
};

static struct clksrc_sources exynos5_clkset_sclk_audio1 = {
	.sources	= exynos5_clkset_sclk_audio1_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_sclk_audio1_list),
};

static struct clksrc_clk exynos5_clk_sclk_audio1 = {
	.clk	= {
		.name		= "sclk_audio1",
		.enable		= exynos5_clksrc_mask_peric1_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos5_clkset_sclk_audio1,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC1, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC4, .shift = 0, .size = 4 },
};

static struct clk *exynos5_clkset_sclk_audio2_list[] = {
	[0] = &exynos5_clk_audiocdclk2,
	[1] = &clk_ext_xtal_mux,
	[2] = &exynos5_clk_sclk_hdmi27m,
	[3] = &exynos5_clk_sclk_dptxphy,
	[4] = &exynos5_clk_sclk_usbphy,
	[5] = &exynos5_clk_sclk_hdmiphy,
	[6] = &exynos5_clk_mout_mpll.clk,
	[7] = &exynos5_clk_mout_epll.clk,
	[8] = &exynos5_clk_sclk_vpll.clk,
	[9] = &exynos5_clk_mout_cpll.clk,
};

static struct clksrc_sources exynos5_clkset_sclk_audio2 = {
	.sources	= exynos5_clkset_sclk_audio2_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_sclk_audio2_list),
};

static struct clksrc_clk exynos5_clk_sclk_audio2 = {
	.clk	= {
		.name		= "sclk_audio2",
		.enable		= exynos5_clksrc_mask_peric1_ctrl,
		.ctrlbit	= (1 << 4),
	},
	.sources = &exynos5_clkset_sclk_audio2,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC1, .shift = 4, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC4, .shift = 16, .size = 4 },
};

static struct clk *exynos5_clkset_sclk_spdif_list[] = {
	[0] = &exynos5_clk_sclk_audio0.clk,
	[1] = &exynos5_clk_sclk_audio1.clk,
	[2] = &exynos5_clk_sclk_audio2.clk,
	[3] = &exynos5_clk_spdifcdclk,
};

static struct clksrc_sources exynos5_clkset_sclk_spdif = {
	.sources	= exynos5_clkset_sclk_spdif_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_sclk_spdif_list),
};

static struct clksrc_clk exynos5_clk_sclk_spdif = {
	.clk	= {
		.name		= "sclk_spdif",
		.enable		= exynos5_clksrc_mask_peric1_ctrl,
		.ctrlbit	= (1 << 8),
		.ops		= &s5p_sclk_spdif_ops,
	},
	.sources = &exynos5_clkset_sclk_spdif,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC1, .shift = 8, .size = 2 },
};

struct clk *exynos5_clkset_usbdrd30_list[] = {
	[0] = &exynos5_clk_mout_mpll.clk,
	[1] = &exynos5_clk_mout_cpll.clk,
};

struct clksrc_sources exynos5_clkset_usbdrd30 = {
	.sources	= exynos5_clkset_usbdrd30_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_usbdrd30_list),
};

struct clk *exynos5_clkset_group_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = NULL,
	[2] = &exynos5_clk_sclk_hdmi24m,
	[3] = &exynos5_clk_sclk_dptxphy,
	[4] = &exynos5_clk_sclk_usbphy,
	[5] = &exynos5_clk_sclk_hdmiphy,
	[6] = &exynos5_clk_mout_mpll_user.clk,
	[7] = &exynos5_clk_mout_epll.clk,
	[8] = &exynos5_clk_sclk_vpll.clk,
	[9] = &exynos5_clk_mout_cpll.clk,
};

struct clksrc_sources exynos5_clkset_group = {
	.sources	= exynos5_clkset_group_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_group_list),
};

/* Possible clock sources for aclk_266_gscl_sub Mux */
static struct clk *clk_src_gscl_266_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_aclk_266.clk,
};

static struct clksrc_sources clk_src_gscl_266 = {
	.sources	= clk_src_gscl_266_list,
	.nr_sources	= ARRAY_SIZE(clk_src_gscl_266_list),
};

/* For ACLK_400_ISP */
static struct clksrc_clk exynos5_clk_mout_aclk_400_isp = {
       .clk    = {
		.name		= "mout_aclk_400_isp",
       },
       .sources = &exynos5_clkset_aclk,
       .reg_src = { .reg = EXYNOS5_CLKSRC_TOP1, .shift = 24, .size = 1 },
};

static struct clksrc_clk exynos5_clk_dout_aclk_400_isp = {
	.clk	= {
		.name		= "dout_aclk_400_isp",
		.parent		= &exynos5_clk_mout_aclk_400_isp.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP1, .shift = 20, .size = 3 },
};

static struct clk *exynos5_clkset_aclk_400_isp_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_dout_aclk_400_isp.clk,
};

static struct clksrc_sources exynos5_clkset_aclk_400_isp = {
	.sources	= exynos5_clkset_aclk_400_isp_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_aclk_400_isp_list),
};

static struct clksrc_clk exynos5_clk_aclk_400_isp = {
	.clk	= {
		.name		= "aclk_400_isp",
	},
	.sources = &exynos5_clkset_aclk_400_isp,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 20, .size = 1 },
};

static struct clksrc_clk exynos5_clk_sclk_uart_isp = {
	.clk	= {
		.name		= "sclk_uart_src_isp",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_SCLK_SRC_ISP, .shift = 8, .size = 4 },
};

static struct clksrc_clk exynos5_clk_aclk_266_isp = {
	.clk	= {
		.name		= "aclk_266_isp",

	},
	.sources = &clk_src_gscl_266,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 16, .size = 1 },
};

static struct clksrc_clk exynos5_clk_dout_mmc0 = {
	.clk		= {
		.name		= "dout_mmc0",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS1, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_mmc1 = {
	.clk		= {
		.name		= "dout_mmc1",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 4, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS1, .shift = 16, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_mmc2 = {
	.clk		= {
		.name		= "dout_mmc2",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 8, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS2, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_mmc3 = {
	.clk		= {
		.name		= "dout_mmc3",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 12, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS2, .shift = 16, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_mmc4 = {
	.clk		= {
		.name		= "dout_mmc4",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 16, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS3, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_uart0 = {
	.clk	= {
		.name		= "uclk1",
		.devname	= "exynos4210-uart.0",
		.enable		= exynos5_clksrc_mask_peric0_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC0, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC0, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_uart1 = {
	.clk	= {
		.name		= "uclk1",
		.devname	= "exynos4210-uart.1",
		.enable		= exynos5_clksrc_mask_peric0_ctrl,
		.ctrlbit	= (1 << 4),
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC0, .shift = 4, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC0, .shift = 4, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_uart2 = {
	.clk	= {
		.name		= "uclk1",
		.devname	= "exynos4210-uart.2",
		.enable		= exynos5_clksrc_mask_peric0_ctrl,
		.ctrlbit	= (1 << 8),
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC0, .shift = 8, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC0, .shift = 8, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_uart3 = {
	.clk	= {
		.name		= "uclk1",
		.devname	= "exynos4210-uart.3",
		.enable		= exynos5_clksrc_mask_peric0_ctrl,
		.ctrlbit	= (1 << 12),
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC0, .shift = 12, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC0, .shift = 12, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_mmc0 = {
	.clk	= {
		.name		= "sclk_mmc",
		.devname	= "exynos4-sdhci.0",
		.parent		= &exynos5_clk_dout_mmc0.clk,
		.enable		= exynos5_clksrc_mask_fsys_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS1, .shift = 8, .size = 8 },
};

static struct clksrc_clk exynos5_clk_sclk_mmc1 = {
	.clk	= {
		.name		= "sclk_mmc",
		.devname	= "exynos4-sdhci.1",
		.parent		= &exynos5_clk_dout_mmc1.clk,
		.enable		= exynos5_clksrc_mask_fsys_ctrl,
		.ctrlbit	= (1 << 4),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS1, .shift = 24, .size = 8 },
};

static struct clksrc_clk exynos5_clk_sclk_mmc2 = {
	.clk	= {
		.name		= "sclk_mmc",
		.devname	= "exynos4-sdhci.2",
		.parent		= &exynos5_clk_dout_mmc2.clk,
		.enable		= exynos5_clksrc_mask_fsys_ctrl,
		.ctrlbit	= (1 << 8),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS2, .shift = 8, .size = 8 },
};

static struct clksrc_clk exynos5_clk_sclk_mmc3 = {
	.clk	= {
		.name		= "sclk_mmc",
		.devname	= "exynos4-sdhci.3",
		.parent		= &exynos5_clk_dout_mmc3.clk,
		.enable		= exynos5_clksrc_mask_fsys_ctrl,
		.ctrlbit	= (1 << 12),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS2, .shift = 24, .size = 8 },
};

static struct clksrc_clk exynos5_clk_dout_spi0 = {
	.clk		= {
		.name		= "dout_spi0",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC1, .shift = 16, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC1, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_spi1 = {
	.clk		= {
		.name		= "dout_spi1",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC1, .shift = 20, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC1, .shift = 16, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_spi2 = {
	.clk		= {
		.name		= "dout_spi2",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC1, .shift = 24, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC2, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_spi0 = {
	.clk	= {
		.name		= "sclk_spi",
		.devname	= "s3c64xx-spi.0",
		.parent		= &exynos5_clk_dout_spi0.clk,
		.enable		= exynos5_clksrc_mask_peric1_ctrl,
		.ctrlbit	= (1 << 16),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC1, .shift = 8, .size = 8 },
};

static struct clksrc_clk exynos5_clk_sclk_spi1 = {
	.clk	= {
		.name		= "sclk_spi",
		.devname	= "s3c64xx-spi.1",
		.parent		= &exynos5_clk_dout_spi1.clk,
		.enable		= exynos5_clksrc_mask_peric1_ctrl,
		.ctrlbit	= (1 << 20),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC1, .shift = 24, .size = 8 },
};

static struct clksrc_clk exynos5_clk_sclk_spi2 = {
	.clk	= {
		.name		= "sclk_spi",
		.devname	= "s3c64xx-spi.2",
		.parent		= &exynos5_clk_dout_spi2.clk,
		.enable		= exynos5_clksrc_mask_peric1_ctrl,
		.ctrlbit	= (1 << 24),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC2, .shift = 8, .size = 8 },
};

static struct clksrc_clk exynos5_clk_aclk_266_isp_div0 = {
	.clk	= {
		.name		= "aclk_266_isp_div0",
		.parent		= &exynos5_clk_aclk_266_isp.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ISP0, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_266_isp_div1 = {
	.clk	= {
		.name		= "aclk_266_isp_div1",
		.parent		= &exynos5_clk_aclk_266_isp.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ISP0, .shift = 4, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_266_isp_divmpwm = {
	.clk	= {
		.name		= "aclk_266_isp_divmpwm",
		.parent		= &exynos5_clk_aclk_266_isp_div1.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ISP2, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_400_isp_div0 = {
	.clk		= {
		.name		= "aclk_400_isp_div0",
		.parent		= &exynos5_clk_aclk_400_isp.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ISP1, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_400_isp_div1 = {
	.clk		= {
		.name		= "aclk_400_isp_div1",
		.parent		= &exynos5_clk_aclk_400_isp.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ISP1, .shift = 4, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_200_disp1 = {
	.clk	= {
		.name		= "aclk_200_disp1",
		.parent		= &exynos5_clk_aclk_200.clk,
	},
	.sources = &clk_src_disp1_200,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 4, .size = 1 },
};

static struct clksrc_clk exynos5_clk_pclk_100_disp1 = {
	.clk	= {
		.name		= "pclk_100_disp1",
		.parent		= &exynos5_clk_aclk_200_disp1.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV2_RATIO0, .shift = 16, .size = 2 },
};

static struct clksrc_clk exynos5_clk_aclk_266_gscl = {
	.clk	= {
		.name		= "aclk_266_gscl",
		.parent		= &exynos5_clk_aclk_266.clk,
	},
	.sources = &clk_src_gscl_266,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 8, .size = 1 },
};

static struct clksrc_clk exynos5_clk_pclk_133_gscl = {
	.clk	= {
		.name		= "pclk_133_gscl",
		.parent		= &exynos5_clk_aclk_266_gscl.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV2_RATIO0, .shift = 4, .size = 2 },
};

static struct clksrc_clk exynos5_clk_pclk_83_mfc = {
	.clk	= {
		.name		= "pclk_83_mfc",
		.parent		= &exynos5_clk_aclk_333.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV4_RATIO, .shift = 0, .size = 2 },
};

static struct clksrc_clk exynos5_clksrcs[] = {
	{
		.clk	= {
			.name		= "sclk_dwmci",
			.parent		= &exynos5_clk_dout_mmc4.clk,
			.enable		= exynos5_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS3, .shift = 8, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_fimd",
			.devname	= "exynos5-fb.1",
			.enable		= exynos5_clksrc_mask_disp1_0_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_DISP1_0, .shift = 0, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 0, .size = 4 },
	}, {
		.clk    = {
			.name		= "sclk_g3d",
			.devname	= "mali.0",
			.enable		= exynos5_clk_block_ctrl,
			.ctrlbit	= (1 << 1),
		},
		.sources = &exynos5_clkset_aclk,
		.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 20, .size = 1 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 24, .size = 3 },
	}, {
		.clk	= {
			.name		= "sclk_gscl_wrap0",
			.devname	= "s5p-mipi-csis.0",
			.enable		= exynos5_clksrc_mask_gscl_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_GSCL, .shift = 24, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_GSCL, .shift = 24, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_gscl_wrap1",
			.devname	= "s5p-mipi-csis.1",
			.enable		= exynos5_clksrc_mask_gscl_ctrl,
			.ctrlbit	= (1 << 28),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_GSCL, .shift = 28, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_GSCL, .shift = 28, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_bayer",
			.enable		= exynos5_clksrc_mask_gscl_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_GSCL, .shift = 12, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_GSCL, .shift = 12, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_cam0",
			.enable		= exynos5_clksrc_mask_gscl_ctrl,
			.ctrlbit	= (1 << 16),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_GSCL, .shift = 16, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_GSCL, .shift = 16, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_cam1",
			.enable		= exynos5_clksrc_mask_gscl_ctrl,
			.ctrlbit	= (1 << 20),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_GSCL, .shift = 20, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_GSCL, .shift = 20, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_jpeg",
			.enable		= exynos5_clksrc_mask_gen_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_GEN, .shift = 0, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_GEN, .shift = 4, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_usbdrd30",
			.enable		= exynos5_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 28),
		},
		.sources = &exynos5_clkset_usbdrd30,
		.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 28, .size = 1 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS0, .shift = 24, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_uart_isp",
			.parent     = &exynos5_clk_sclk_uart_isp.clk,
		},
		.reg_div = { .reg = EXYNOS5_SCLK_DIV_ISP, .shift = 24, .size = 4 },
	},
};

/* Clock initialization code */
static struct clksrc_clk *exynos5_sysclks[] = {
	&exynos5_clk_mout_apll,
	&exynos5_clk_sclk_apll,
	&exynos5_clk_mout_bpll_fout,
	&exynos5_clk_mout_bpll,
	&exynos5_clk_mout_bpll_user,
	&exynos5_clk_mout_cpll,
	&exynos5_clk_mout_epll,
	&exynos5_clk_mout_gpll,
	&exynos5_clk_mout_mpll_fout,
	&exynos5_clk_mout_mpll,
	&exynos5_clk_mout_mpll_user,
	&exynos5_clk_vpllsrc,
	&exynos5_clk_sclk_vpll,
	&exynos5_clk_mout_cpu,
	&exynos5_clk_dout_armclk,
	&exynos5_clk_dout_arm2clk,
	&exynos5_clk_mclk_cdrex,
	&exynos5_clk_aclk_400_g3d_mid,
	&exynos5_clk_aclk_400_g3d,
	&exynos5_clk_mout_aclk_333,
	&exynos5_clk_dout_aclk_333,
	&exynos5_clk_aclk_333,
	&exynos5_clk_aclk_266_gscl,
	&exynos5_clk_aclk_266,
	&exynos5_clk_aclk_200_disp1,
	&exynos5_clk_aclk_200,
	&exynos5_clk_aclk_166,
	&exynos5_clk_aclk_66_pre,
	&exynos5_clk_aclk_66,
	&exynos5_clk_dout_mmc0,
	&exynos5_clk_dout_mmc1,
	&exynos5_clk_dout_mmc2,
	&exynos5_clk_dout_mmc3,
	&exynos5_clk_dout_mmc4,
	&exynos5_clk_aclk_acp,
	&exynos5_clk_pclk_acp,
	&exynos5_clk_mout_aclk_300_disp1_mid,
	&exynos5_clk_mout_aclk_300_disp1_mid1,
	&exynos5_clk_mout_aclk_300_disp1,
	&exynos5_clk_dout_aclk_300_disp1,
	&exynos5_clk_aclk_300_disp1,
	&exynos5_clk_mout_aclk_300_gscl_mid,
	&exynos5_clk_mout_aclk_300_gscl_mid1,
	&exynos5_clk_mout_aclk_300_gscl,
	&exynos5_clk_dout_aclk_300_gscl,
	&exynos5_clk_aclk_300_gscl,
	&exynos5_clk_mout_aclk_400_isp,
	&exynos5_clk_dout_aclk_400_isp,
	&exynos5_clk_aclk_400_isp,
	&exynos5_clk_aclk_266_isp,
	&exynos5_clk_sclk_uart_isp,
	&exynos5_clk_clkout,
	&exynos5_clk_dout_spi0,
	&exynos5_clk_dout_spi1,
	&exynos5_clk_dout_spi2,
};

static struct clk *exynos5_clk_cdev[] = {
	&exynos5_clk_pdma0,
	&exynos5_clk_pdma1,
	&exynos5_clk_mdma1,
};

static struct clksrc_clk *exynos5_clksrc_cdev[] = {
	&exynos5_clk_sclk_uart0,
	&exynos5_clk_sclk_uart1,
	&exynos5_clk_sclk_uart2,
	&exynos5_clk_sclk_uart3,
	&exynos5_clk_sclk_mmc0,
	&exynos5_clk_sclk_mmc1,
	&exynos5_clk_sclk_mmc2,
	&exynos5_clk_sclk_mmc3,
	&exynos5_clk_sclk_audio0,
	&exynos5_clk_sclk_audio1,
	&exynos5_clk_sclk_audio2,
	&exynos5_clk_sclk_spdif,
	&exynos5_clk_sclk_spi0,
	&exynos5_clk_sclk_spi1,
	&exynos5_clk_sclk_spi2,
	&exynos5_clk_pclk_100_disp1,
	&exynos5_clk_pclk_133_gscl,
	&exynos5_clk_pclk_83_mfc,
};

static struct clksrc_clk *exynos5_clksrc_aclk_isp[] = {
	&exynos5_clk_aclk_266_isp_div0,
	&exynos5_clk_aclk_266_isp_div1,
	&exynos5_clk_aclk_266_isp_divmpwm,
	&exynos5_clk_aclk_400_isp_div0,
	&exynos5_clk_aclk_400_isp_div1,
};

static struct clk_lookup exynos5_clk_lookup[] = {
	CLKDEV_INIT("exynos4210-uart.0", "clk_uart_baud0", &exynos5_clk_sclk_uart0.clk),
	CLKDEV_INIT("exynos4210-uart.1", "clk_uart_baud0", &exynos5_clk_sclk_uart1.clk),
	CLKDEV_INIT("exynos4210-uart.2", "clk_uart_baud0", &exynos5_clk_sclk_uart2.clk),
	CLKDEV_INIT("exynos4210-uart.3", "clk_uart_baud0", &exynos5_clk_sclk_uart3.clk),
	CLKDEV_INIT("exynos4-sdhci.0", "mmc_busclk.2", &exynos5_clk_sclk_mmc0.clk),
	CLKDEV_INIT("exynos4-sdhci.1", "mmc_busclk.2", &exynos5_clk_sclk_mmc1.clk),
	CLKDEV_INIT("exynos4-sdhci.2", "mmc_busclk.2", &exynos5_clk_sclk_mmc2.clk),
	CLKDEV_INIT("exynos4-sdhci.3", "mmc_busclk.2", &exynos5_clk_sclk_mmc3.clk),
	CLKDEV_INIT("dma-pl330.0", "apb_pclk", &exynos5_clk_pdma0),
	CLKDEV_INIT("dma-pl330.1", "apb_pclk", &exynos5_clk_pdma1),
	CLKDEV_INIT("dma-pl330.2", "apb_pclk", &exynos5_clk_mdma1),
	CLKDEV_INIT("s3c64xx-spi.0", "spi_busclk0", &exynos5_clk_sclk_spi0.clk),
	CLKDEV_INIT("s3c64xx-spi.1", "spi_busclk0", &exynos5_clk_sclk_spi1.clk),
	CLKDEV_INIT("s3c64xx-spi.2", "spi_busclk0", &exynos5_clk_sclk_spi2.clk),
};

static unsigned long exynos5_epll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static struct clk *exynos5_clks[] __initdata = {
	&exynos5_clk_sclk_hdmi27m,
	&exynos5_clk_sclk_hdmiphy,
	&clk_fout_bpll_div2,
	&clk_fout_bpll,
	&clk_fout_cpll,
	&clk_fout_gpll,
	&exynos5_clk_armclk,
	&clk_fout_mpll_div2,
	&exynos5_mif_clk,
	&exynos5_int_clk,
};

static u32 epll_div[][6] = {
	{ 192000000, 0, 48, 3, 1, 0 },
	{ 180000000, 0, 45, 3, 1, 0 },
	{  73728000, 1, 73, 3, 3, 47710 },
	{  67737600, 1, 90, 4, 3, 20762 },
	{  49152000, 0, 49, 3, 3, 9961 },
	{  45158400, 0, 45, 3, 3, 10381 },
	{ 180633600, 0, 45, 3, 1, 10381 },
};

static int exynos5_epll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int epll_con, epll_con_k;
	unsigned int i;
	unsigned int tmp;
	unsigned int epll_rate;
	unsigned int locktime;
	unsigned int lockcnt;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	if (clk->parent)
		epll_rate = clk_get_rate(clk->parent);
	else
		epll_rate = clk_ext_xtal_mux.rate;

	if (epll_rate != 24000000) {
		pr_err("Invalid Clock : recommended clock is 24MHz.\n");
		return -EINVAL;
	}

	epll_con = __raw_readl(EXYNOS5_EPLL_CON0);
	epll_con &= ~(0x1 << 27 | \
			PLL46XX_MDIV_MASK << PLL46XX_MDIV_SHIFT |   \
			PLL46XX_PDIV_MASK << PLL46XX_PDIV_SHIFT | \
			PLL46XX_SDIV_MASK << PLL46XX_SDIV_SHIFT);

	for (i = 0; i < ARRAY_SIZE(epll_div); i++) {
		if (epll_div[i][0] == rate) {
			epll_con_k = epll_div[i][5] << 0;
			epll_con |= epll_div[i][1] << 27;
			epll_con |= epll_div[i][2] << PLL46XX_MDIV_SHIFT;
			epll_con |= epll_div[i][3] << PLL46XX_PDIV_SHIFT;
			epll_con |= epll_div[i][4] << PLL46XX_SDIV_SHIFT;
			break;
		}
	}

	if (i == ARRAY_SIZE(epll_div)) {
		printk(KERN_ERR "%s: Invalid Clock EPLL Frequency\n",
				__func__);
		return -EINVAL;
	}

	epll_rate /= 1000000;

	/* 3000 max_cycls : specification data */
	locktime = 3000 / epll_rate * epll_div[i][3];
	lockcnt = locktime * 10000 / (10000 / epll_rate);

	__raw_writel(lockcnt, EXYNOS5_EPLL_LOCK);

	__raw_writel(epll_con, EXYNOS5_EPLL_CON0);
	__raw_writel(epll_con_k, EXYNOS5_EPLL_CON1);

	do {
		tmp = __raw_readl(EXYNOS5_EPLL_CON0);
	} while (!(tmp & 0x1 << EXYNOS5_EPLLCON0_LOCKED_SHIFT));

	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5_epll_ops = {
	.get_rate = exynos5_epll_get_rate,
	.set_rate = exynos5_epll_set_rate,
};


#define APLL_FREQ(f, a0, a1, a2, a3, a4, a5, a6, a7, b0, b1, m, p, s) \
	{ \
		.freq = (f) * 1000000, \
		.clk_div_cpu0 = ((a0) | (a1) << 4 | (a2) << 8 |	(a3) << 12 | \
			(a4) << 16 | (a5) << 20 | (a6) << 24 | (a7) << 28), \
		.clk_div_cpu1 = (b0 << 0 | b1 << 4), \
		.mps = ((m) << 16 | (p) << 8 | (s)), \
	}

static struct {
	unsigned long freq;
	u32 clk_div_cpu0;
	u32 clk_div_cpu1;
	u32 mps;
} apll_freq[] = {
	/*
	 * values:
	 * freq
	 * clock divider for ARM, CPUD, ACP, PERIPH, ATB, PCLK_DBG, APLL, ARM2
	 * clock divider for COPY, HPM
	 * PLL M, P, S
	 */
	APLL_FREQ(1700, 0, 3, 7, 7, 7, 2, 5, 0, 7, 7, 425, 6, 0),
	APLL_FREQ(1600, 0, 3, 7, 7, 7, 1, 4, 0, 7, 7, 200, 3, 0),
	APLL_FREQ(1500, 0, 2, 7, 7, 7, 1, 4, 0, 7, 7, 250, 4, 0),
	APLL_FREQ(1400, 0, 2, 7, 7, 6, 1, 4, 0, 7, 7, 175, 3, 0),
	APLL_FREQ(1300, 0, 2, 7, 7, 6, 1, 3, 0, 7, 7, 325, 6, 0),
	APLL_FREQ(1200, 0, 2, 7, 7, 5, 1, 3, 0, 7, 7, 200, 4, 0),
	APLL_FREQ(1100, 0, 3, 7, 7, 5, 1, 3, 0, 7, 7, 275, 6, 0),
	APLL_FREQ(1000, 0, 1, 7, 7, 4, 1, 2, 0, 7, 7, 125, 3, 0),
	APLL_FREQ(900,  0, 1, 7, 7, 4, 1, 2, 0, 7, 7, 150, 4, 0),
	APLL_FREQ(800,  0, 1, 7, 7, 4, 1, 2, 0, 7, 7, 100, 3, 0),
	APLL_FREQ(700,  0, 1, 7, 7, 3, 1, 1, 0, 7, 7, 175, 3, 1),
	APLL_FREQ(600,  0, 1, 7, 7, 3, 1, 1, 0, 7, 7, 200, 4, 1),
	APLL_FREQ(500,  0, 1, 7, 7, 2, 1, 1, 0, 7, 7, 125, 3, 1),
	APLL_FREQ(400,  0, 1, 7, 7, 2, 1, 1, 0, 7, 7, 100, 3, 1),
	APLL_FREQ(300,  0, 1, 7, 7, 1, 1, 1, 0, 7, 7, 200, 4, 2),
	APLL_FREQ(200,  0, 1, 7, 7, 1, 1, 1, 0, 7, 7, 100, 3, 2),
};

static u32 exynos5_gpll_div[][6] = {
	/*rate, P, M, S, AFC_DNB, AFC*/
	{1400000000, 3, 175, 0, 0, 0}, /* for 466MHz */
	{800000000, 3, 100, 0, 0, 0},  /* for 400MHz, 200MHz */
	{667000000, 7, 389, 1, 0, 0},  /* for 333MHz, 222MHz, 166MHz */
	{600000000, 4, 200, 1, 0, 0},  /* for 300MHz, 200MHz, 150MHz */
	{533000000, 12, 533, 1, 0, 0}, /* for 533MHz, 266MHz, 133MHz */
	{450000000, 12, 450, 1, 0, 0}, /* for 450 Hz */
	{400000000, 3, 100, 1, 0, 0},
	{333000000, 4, 222, 2, 0, 0},
	{200000000, 3, 100, 2, 0, 0},
};

static unsigned long exynos5_gpll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int exynos5_gpll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int gpll_con0;
	unsigned int locktime;
	unsigned int tmp;
	unsigned int i;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	gpll_con0 = __raw_readl(EXYNOS5_GPLL_CON0);
	gpll_con0 &= ~(PLL35XX_MDIV_MASK << PLL35XX_MDIV_SHIFT |       \
			PLL35XX_PDIV_MASK << PLL35XX_PDIV_SHIFT |       \
			PLL35XX_SDIV_MASK << PLL35XX_SDIV_SHIFT);

	for (i = 0; i < ARRAY_SIZE(exynos5_gpll_div); i++) {
		if (exynos5_gpll_div[i][0] == rate) {
			gpll_con0 |= exynos5_gpll_div[i][1] << PLL35XX_PDIV_SHIFT;
			gpll_con0 |= exynos5_gpll_div[i][2] << PLL35XX_MDIV_SHIFT;
			gpll_con0 |= exynos5_gpll_div[i][3] << PLL35XX_SDIV_SHIFT;
			break;
		}
	}

	if (i == ARRAY_SIZE(exynos5_gpll_div)) {
		printk(KERN_ERR "%s: Invalid Clock GPLL Frequency\n",
				__func__);
		return -EINVAL;
	}

	/* 250 max_cycls : specification data */
	/* 270@p=1, 1cycle=1/24=41.6ns */
	/* calc >> p=5, 270 * 5 = 1350cycle * 41.6ns = 56.16us */

	locktime = 270 * exynos5_gpll_div[i][1] + 1;

	__raw_writel(locktime, EXYNOS5_GPLL_LOCK);

	__raw_writel(gpll_con0, EXYNOS5_GPLL_CON0);

	do {
		tmp = __raw_readl(EXYNOS5_GPLL_CON0);
	} while (!(tmp & EXYNOS5_GPLLCON0_LOCKED));

	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5_gpll_ops = {
	.get_rate = exynos5_gpll_get_rate,
	.set_rate = exynos5_gpll_set_rate,
};

static int xtal_rate;

static unsigned long exynos5_fout_apll_get_rate(struct clk *clk)
{
	return s5p_get_pll35xx(xtal_rate, __raw_readl(EXYNOS5_APLL_CON0));
}

static void exynos5_apll_set_clkdiv(unsigned int div_index)
{
	unsigned int tmp;

	/* Change Divider - CPU0 */

	tmp = apll_freq[div_index].clk_div_cpu0;

	__raw_writel(tmp, EXYNOS5_CLKDIV_CPU0);

	while (__raw_readl(EXYNOS5_CLKDIV_STATCPU0) & 0x11111111)
		cpu_relax();

	/* Change Divider - CPU1 */
	tmp = apll_freq[div_index].clk_div_cpu1;

	__raw_writel(tmp, EXYNOS5_CLKDIV_CPU1);

	while (__raw_readl(EXYNOS5_CLKDIV_STATCPU1) & 0x11)
		cpu_relax();
}

static void exynos5_apll_set_apll(unsigned int index)
{
	unsigned int tmp, pdiv;

	/* Set APLL Lock time */
	pdiv = ((apll_freq[index].mps >> 8) & 0x3f);

	__raw_writel((pdiv * 250), EXYNOS5_APLL_LOCK);

	/* Change PLL PMS values */
	tmp = __raw_readl(EXYNOS5_APLL_CON0);
	tmp &= ~((0x3ff << 16) | (0x3f << 8) | (0x7 << 0));
	tmp |= apll_freq[index].mps;
	__raw_writel(tmp, EXYNOS5_APLL_CON0);

	/* wait_lock_time */
	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_APLL_CON0);
	} while (!(tmp & (0x1 << 29)));

}

static int exynos5_fout_apll_set_rate(struct clk *clk, unsigned long rate)
{
	int index;

	for (index = 0; index < ARRAY_SIZE(apll_freq); index++)
		if (apll_freq[index].freq == rate)
			break;

	if (index == ARRAY_SIZE(apll_freq))
		return -EINVAL;

	if (rate > clk->rate) {
		/* Clock Configuration Procedure */
		/* 1. Change the system clock divider values */
		exynos5_apll_set_clkdiv(index);
		/* 2. Change the apll m,p,s value */
		exynos5_apll_set_apll(index);
	} else if (rate < clk->rate) {
		/* Clock Configuration Procedure */
		/* 1. Change the apll m,p,s value */
		exynos5_apll_set_apll(index);
		/* 2. Change the system clock divider values */
		exynos5_apll_set_clkdiv(index);
	}

	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5_fout_apll_ops = {
	.get_rate = exynos5_fout_apll_get_rate,
	.set_rate = exynos5_fout_apll_set_rate
};

#define MIF_FREQ(f, a0, b0, b1, b2, b3, b4, b5, c0, c1) \
	{ \
		.freq = (f) * 1000000, \
		.clk_div_sysrgt = (a0),	\
		.clk_div_cdrex = ((b0) << 0 | (b1) << 4 | (b2) << 16 | \
				(b3) << 20 | (b4) << 24 | (b5) << 28), \
		.clk_div_syslft = ((c0) << 0 | (c1) << 4), \
	}

static struct {
	unsigned long freq;
	u32 clk_div_sysrgt;
	u32 clk_div_cdrex;
	u32 clk_div_syslft;
} mif_freq[] = {
	/*
	 * values:
	 * freq
	 * clock divider for ACLK_R1BX
	 * clock divider for ACLK_CDREX, PCLK_CDREX, MCLK_CDREX,
	 *		MCLK_DPHY, ACLK_SFRTZASCP, MCLK_CDREX2
	 * clock divider for ACLK_SYSLFT, PCLK_SYSLFT
	 */
	MIF_FREQ(800, 1, 1, 1, 2, 0, 1, 0, 1, 1),
	MIF_FREQ(667, 1, 1, 1, 2, 0, 1, 0, 1, 1),
	MIF_FREQ(400, 3, 1, 3, 2, 0, 1, 1, 3, 1),
	MIF_FREQ(160, 7, 1, 5, 2, 0, 1, 4, 7, 1),
	MIF_FREQ(100, 7, 1, 6, 2, 0, 1, 7, 7, 1),
};

static unsigned long exynos5_mif_get_rate(struct clk *clk)
{
	unsigned int tmp;

	tmp = __raw_readl(EXYNOS5_CLKDIV_CDREX);
	tmp &= EXYNOS5_CLKDIV_CDREX_MCLK_CDREX2_MASK;
	tmp >>= EXYNOS5_CLKDIV_CDREX_MCLK_CDREX2_SHIFT;

	return clk_get_rate(clk->parent) / (tmp + 1);
}

static void exynos5_mif_set_clkdiv(unsigned int div_index)
{
	unsigned int tmp;

	/* Change Divier - SYSRGT */
	tmp = __raw_readl(EXYNOS5_CLKDIV_SYSRGT);
	tmp &= ~EXYNOS5_CLKDIV_SYSRGT_ACLK_R1BX_MASK;

	tmp |= mif_freq[div_index].clk_div_sysrgt;

	__raw_writel(tmp, EXYNOS5_CLKDIV_SYSRGT);

	while (__raw_readl(EXYNOS5_CLKDIV_STAT_SYSRGT) & 0x1)
		cpu_relax();

	/* Change Divider - CDREX */
	tmp = __raw_readl(EXYNOS5_CLKDIV_CDREX);
	tmp &= ~(EXYNOS5_CLKDIV_CDREX_ACLK_CDREX_MASK |
			EXYNOS5_CLKDIV_CDREX_PCLK_CDREX_MASK |
			EXYNOS5_CLKDIV_CDREX_MCLK_CDREX_MASK |
			EXYNOS5_CLKDIV_CDREX_MCLK_DPHY_MASK |
			EXYNOS5_CLKDIV_CDREX_ACLK_EFCON_MASK |
			EXYNOS5_CLKDIV_CDREX_MCLK_CDREX2_MASK);

	tmp |= mif_freq[div_index].clk_div_cdrex;

	__raw_writel(tmp, EXYNOS5_CLKDIV_CDREX);

	while (__raw_readl(EXYNOS5_CLKDIV_STAT_CDREX) & 0x11110011)
		cpu_relax();

	/* Change Divier - SYSLFT */
	tmp = __raw_readl(EXYNOS5_CLKDIV_SYSLFT);

	tmp &= ~(EXYNOS5_CLKDIV_SYSLFT_PCLK_SYSLFT_MASK |
		 EXYNOS5_CLKDIV_SYSLFT_ACLK_SYSLFT_MASK);

	tmp |= mif_freq[div_index].clk_div_syslft;

	__raw_writel(tmp, EXYNOS5_CLKDIV_SYSLFT);

	while (__raw_readl(EXYNOS5_CLKDIV_STAT_SYSLFT) & 0x11)
		cpu_relax();
}

static int exynos5_mif_set_rate(struct clk *clk, unsigned long rate)
{
	int index;

	for (index = 0; index < ARRAY_SIZE(mif_freq); index++)
		if (mif_freq[index].freq == rate)
			break;

	if (index == ARRAY_SIZE(mif_freq))
		return -EINVAL;

	/* Change the system clock divider values */
	exynos5_mif_set_clkdiv(index);

	return 0;
}

static struct clk_ops exynos5_mif_ops = {
	.get_rate = exynos5_mif_get_rate,
	.set_rate = exynos5_mif_set_rate
};

#define INT_FREQ(f, a0, a1, a2, a3, a4, a5, b0, b1, b2, b3, \
			c0, c1, d0, e0) \
	{ \
		.freq = (f) * 1000000, \
		.clk_div_top0 = ((a0) << 0 | (a1) << 8 | (a2) << 12 | \
				(a3) << 16 | (a4) << 20 | (a5) << 28), \
		.clk_div_top1 = ((b0) << 12 | (b1) << 16 | (b2) << 20 | \
				(b3) << 24), \
		.clk_div_lex = ((c0) << 4 | (c1) << 8), \
		.clk_div_r0x = ((d0) << 4), \
		.clk_div_r1x = ((e0) << 4), \
	}

static struct {
	unsigned long freq;
	u32 clk_div_top0;
	u32 clk_div_top1;
	u32 clk_div_lex;
	u32 clk_div_r0x;
	u32 clk_div_r1x;
} int_freq[] = {
	/*
	 * values:
	 * freq
	 * clock divider for ACLK66, ACLK166, ACLK200, ACLK266,
			ACLK333, ACLK300_DISP1
	 * clock divider for ACLK300_GSCL, ACLK400_IOP, ACLK400_ISP, ACLK66_PRE
	 * clock divider for PCLK_LEX, ATCLK_LEX
	 * clock divider for ACLK_PR0X
	 * clock divider for ACLK_PR1X
	 */
	INT_FREQ(266, 1, 1, 3, 2, 0, 0, 0, 1, 1, 5, 1, 0, 1, 1),
	INT_FREQ(200, 1, 2, 4, 3, 1, 0, 0, 3, 2, 5, 1, 0, 1, 1),
	INT_FREQ(160, 1, 3, 4, 4, 2, 0, 0, 3, 3, 5, 1, 0, 1, 1),
	INT_FREQ(133, 1, 3, 5, 5, 2, 1, 1, 4, 4, 5, 1, 0, 1, 1),
	INT_FREQ(100, 1, 7, 7, 7, 7, 3, 7, 7, 7, 5, 1, 0, 1, 1),
};

static unsigned long exynos5_clk_int_get_rate(struct clk *clk)
{
	return clk->rate;
}

static void exynos5_int_set_clkdiv(unsigned int div_index)
{
	unsigned int tmp;

	/* Change Divider - TOP0 */
	tmp = __raw_readl(EXYNOS5_CLKDIV_TOP0);

	tmp &= ~(EXYNOS5_CLKDIV_TOP0_ACLK266_MASK |
		EXYNOS5_CLKDIV_TOP0_ACLK200_MASK |
		EXYNOS5_CLKDIV_TOP0_ACLK66_MASK |
		EXYNOS5_CLKDIV_TOP0_ACLK333_MASK |
		EXYNOS5_CLKDIV_TOP0_ACLK166_MASK |
		EXYNOS5_CLKDIV_TOP0_ACLK300_DISP1_MASK);

	tmp |= int_freq[div_index].clk_div_top0;

	__raw_writel(tmp, EXYNOS5_CLKDIV_TOP0);

	while (__raw_readl(EXYNOS5_CLKDIV_STAT_TOP0) & 0x151101)
		cpu_relax();

	/* Change Divider - TOP1 */
	tmp = __raw_readl(EXYNOS5_CLKDIV_TOP1);

	tmp &= ~(EXYNOS5_CLKDIV_TOP1_ACLK400_ISP_MASK |
		EXYNOS5_CLKDIV_TOP1_ACLK400_IOP_MASK |
		EXYNOS5_CLKDIV_TOP1_ACLK66_PRE_MASK |
		EXYNOS5_CLKDIV_TOP1_ACLK300_GSCL_MASK);

	tmp |= int_freq[div_index].clk_div_top1;

	__raw_writel(tmp, EXYNOS5_CLKDIV_TOP1);

	while ((__raw_readl(EXYNOS5_CLKDIV_STAT_TOP1) & 0x1110000) &&
		(__raw_readl(EXYNOS5_CLKDIV_STAT_TOP0) & 0x80000))
		cpu_relax();

	/* Change Divider - LEX */
	tmp = __raw_readl(EXYNOS5_CLKDIV_LEX);

	tmp &= ~(EXYNOS5_CLKDIV_LEX_ATCLK_LEX_MASK |
		EXYNOS5_CLKDIV_LEX_PCLK_LEX_MASK);

	tmp |= int_freq[div_index].clk_div_lex;

	__raw_writel(tmp, EXYNOS5_CLKDIV_LEX);

	while (__raw_readl(EXYNOS5_CLKDIV_STAT_LEX) & 0x110)
		cpu_relax();

	/* Change Divider - R0X */
	tmp = __raw_readl(EXYNOS5_CLKDIV_R0X);

	tmp &= ~EXYNOS5_CLKDIV_R0X_PCLK_R0X_MASK;

	tmp |= int_freq[div_index].clk_div_r0x;

	__raw_writel(tmp, EXYNOS5_CLKDIV_R0X);

	while (__raw_readl(EXYNOS5_CLKDIV_STAT_R0X) & 0x10)
		cpu_relax();

	/* Change Divider - R1X */
	tmp = __raw_readl(EXYNOS5_CLKDIV_R1X);

	tmp &= ~EXYNOS5_CLKDIV_R1X_PCLK_R1X_MASK;

	tmp |= int_freq[div_index].clk_div_r1x;

	__raw_writel(tmp, EXYNOS5_CLKDIV_R1X);

	while (__raw_readl(EXYNOS5_CLKDIV_STAT_R1X) & 0x10)
		cpu_relax();
}

static int exynos5_clk_int_set_rate(struct clk *clk, unsigned long rate)
{
	int index;

	for (index = 0; index < ARRAY_SIZE(int_freq); index++)
		if (int_freq[index].freq == rate)
			break;

	if (index == ARRAY_SIZE(int_freq))
		return -EINVAL;

	/* Change the system clock divider values */
	exynos5_int_set_clkdiv(index);

	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5_clk_int_ops = {
	.get_rate = exynos5_clk_int_get_rate,
	.set_rate = exynos5_clk_int_set_rate
};

static u32 exynos5_vpll_div[][8] = {
	{268000000, 6, 268, 2, 41104, 0, 0, 0},
};

static unsigned long exynos5_vpll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int exynos5_vpll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int vpll_con0, vpll_con1;
	unsigned int locktime;
	unsigned int tmp;
	unsigned int i;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	vpll_con0 = __raw_readl(EXYNOS5_VPLL_CON0);
	vpll_con0 &= ~(PLL36XX_MDIV_MASK << PLL36XX_MDIV_SHIFT |
			PLL36XX_PDIV_MASK << PLL36XX_PDIV_SHIFT |
			PLL36XX_SDIV_MASK << PLL36XX_SDIV_SHIFT);

	vpll_con1 = __raw_readl(EXYNOS5_VPLL_CON1);
	vpll_con1 &= ~(0xffff << 0);

	for (i = 0; i < ARRAY_SIZE(exynos5_vpll_div); i++) {
		if (exynos5_vpll_div[i][0] == rate) {
			vpll_con0 |=
				exynos5_vpll_div[i][1] << PLL36XX_PDIV_SHIFT;
			vpll_con0 |=
				exynos5_vpll_div[i][2] << PLL36XX_MDIV_SHIFT;
			vpll_con0 |=
				exynos5_vpll_div[i][3] << PLL36XX_SDIV_SHIFT;
			vpll_con1 |= exynos5_vpll_div[i][4] << 0;
			break;
		}
	}

	if (i == ARRAY_SIZE(exynos5_vpll_div)) {
		printk(KERN_ERR "%s: Invalid Clock VPLL Frequency\n",
				__func__);
		return -EINVAL;
	}

	/* 3000 max_cycls : specification data */
	locktime = 3000 * exynos5_vpll_div[i][1] + 1;

	__raw_writel(locktime, EXYNOS5_VPLL_LOCK);

	__raw_writel(vpll_con0, EXYNOS5_VPLL_CON0);
	__raw_writel(vpll_con1, EXYNOS5_VPLL_CON1);

	do {
		tmp = __raw_readl(EXYNOS5_VPLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS5_VPLLCON0_LOCKED_SHIFT)));

	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5_vpll_ops = {
	.get_rate = exynos5_vpll_get_rate,
	.set_rate = exynos5_vpll_set_rate,
};

#ifdef CONFIG_PM
static int exynos5_clock_suspend(void)
{
	s3c_pm_do_save(exynos5_clock_save, ARRAY_SIZE(exynos5_clock_save));

	return 0;
}

static void exynos5_clock_resume(void)
{
	s3c_pm_do_restore_core(exynos5_clock_save, ARRAY_SIZE(exynos5_clock_save));
}
#else
#define exynos5_clock_suspend NULL
#define exynos5_clock_resume NULL
#endif

struct syscore_ops exynos5_clock_syscore_ops = {
	.suspend	= exynos5_clock_suspend,
	.resume		= exynos5_clock_resume,
};

void __init_or_cpufreq exynos5250_setup_clocks(void)
{
	struct clk *xtal_clk;
	unsigned long apll;
	unsigned long bpll;
	unsigned long cpll;
	unsigned long mpll;
	unsigned long epll;
	unsigned long gpll;
	unsigned long vpll;
	unsigned long vpllsrc;
	unsigned long xtal;
	unsigned long armclk;
	unsigned long mout_cdrex;
	unsigned long aclk_400;
	unsigned long aclk_333;
	unsigned long aclk_266;
	unsigned long aclk_200;
	unsigned long aclk_166;
	unsigned long aclk_66;
	unsigned int ptr;

	printk(KERN_DEBUG "%s: registering clocks\n", __func__);

	xtal_clk = clk_get(NULL, "xtal");
	BUG_ON(IS_ERR(xtal_clk));

	xtal = clk_get_rate(xtal_clk);

	xtal_rate = xtal;

	clk_put(xtal_clk);

	printk(KERN_DEBUG "%s: xtal is %ld\n", __func__, xtal);

	apll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_APLL_CON0));
	bpll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_BPLL_CON0));
	cpll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_CPLL_CON0));
	mpll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_MPLL_CON0));
	epll = s5p_get_pll36xx(xtal, __raw_readl(EXYNOS5_EPLL_CON0),
			__raw_readl(EXYNOS5_EPLL_CON1));
	gpll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_GPLL_CON0));

	vpllsrc = clk_get_rate(&exynos5_clk_vpllsrc.clk);
	vpll = s5p_get_pll36xx(vpllsrc, __raw_readl(EXYNOS5_VPLL_CON0),
			__raw_readl(EXYNOS5_VPLL_CON1));

	clk_fout_apll.ops = &exynos5_fout_apll_ops;
	clk_fout_bpll.rate = bpll;
	clk_fout_bpll_div2.rate = clk_fout_bpll.rate / 2;
	clk_fout_cpll.rate = cpll;
	clk_fout_gpll.rate = gpll;
	clk_fout_mpll.rate = mpll;
	clk_fout_mpll_div2.rate = clk_fout_mpll.rate / 2;
	clk_fout_epll.rate = epll;
	clk_fout_vpll.rate = vpll;
	clk_fout_apll.rate = apll;

	if (clk_set_parent(&exynos5_clk_mout_mpll.clk,
			   &exynos5_clk_mout_mpll_fout.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos5_clk_mout_mpll_fout.clk.name,
				exynos5_clk_mout_mpll.clk.name);

	printk(KERN_INFO "EXYNOS5: PLL settings, A=%ld, B=%ld, C=%ld\n"
			"M=%ld, E=%ld, V=%ld, G=%ld\n",
			apll, bpll, cpll, mpll, epll, vpll, gpll);

	armclk = clk_get_rate(&exynos5_clk_armclk);
	mout_cdrex = clk_get_rate(&exynos5_clk_mclk_cdrex.clk);

	aclk_400 = clk_get_rate(&exynos5_clk_aclk_400_g3d.clk);
	aclk_333 = clk_get_rate(&exynos5_clk_aclk_333.clk);
	aclk_266 = clk_get_rate(&exynos5_clk_aclk_266.clk);
	aclk_200 = clk_get_rate(&exynos5_clk_aclk_200.clk);
	aclk_166 = clk_get_rate(&exynos5_clk_aclk_166.clk);
	aclk_66 = clk_get_rate(&exynos5_clk_aclk_66.clk);

	printk(KERN_INFO "EXYNOS5: ARMCLK=%ld, CDREX=%ld, ACLK400=%ld\n"
			"ACLK333=%ld, ACLK266=%ld, ACLK200=%ld\n"
			"ACLK166=%ld, ACLK66=%ld\n",
			armclk, mout_cdrex, aclk_400,
			aclk_333, aclk_266, aclk_200,
			aclk_166, aclk_66);

	clk_fout_epll.ops = &exynos5_epll_ops;
	clk_fout_vpll.ops = &exynos5_vpll_ops;
	clk_fout_gpll.ops = &exynos5_gpll_ops;

	exynos5_mif_clk.ops = &exynos5_mif_ops;
	exynos5_mif_clk.parent = &exynos5_clk_mclk_cdrex.clk;
	exynos5_int_clk.ops = &exynos5_clk_int_ops;
	exynos5_int_clk.rate = aclk_266;

	if (clk_set_parent(&exynos5_clk_mout_epll.clk, &clk_fout_epll))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				clk_fout_epll.name, exynos5_clk_mout_epll.clk.name);

	if (clk_set_parent(&exynos5_clk_mout_aclk_400_isp.clk, &exynos5_clk_mout_mpll_user.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
			exynos5_clk_mout_mpll_user.clk.name, exynos5_clk_mout_aclk_400_isp.clk.name);
	if (clk_set_parent(&exynos5_clk_aclk_266_isp.clk, &exynos5_clk_aclk_266.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
			exynos5_clk_aclk_266.clk.name, exynos5_clk_aclk_266_isp.clk.name);
	if (clk_set_parent(&exynos5_clk_aclk_400_isp.clk, &exynos5_clk_dout_aclk_400_isp.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
			exynos5_clk_mout_aclk_400_isp.clk.name, exynos5_clk_aclk_400_isp.clk.name);
	if (clk_set_parent(&exynos5_clk_sclk_uart_isp.clk, &exynos5_clk_mout_mpll_user.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
			exynos5_clk_mout_mpll_user.clk.name, exynos5_clk_sclk_uart_isp.clk.name);

	if (clk_set_parent(&exynos5_clk_sclk_mmc0.clk,
				&exynos5_clk_mout_mpll_user.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos5_clk_mout_mpll_user.clk.name,
				exynos5_clk_sclk_mmc0.clk.name);
	if (clk_set_parent(&exynos5_clk_sclk_mmc1.clk,
				&exynos5_clk_mout_mpll_user.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos5_clk_mout_mpll_user.clk.name,
				exynos5_clk_sclk_mmc1.clk.name);
	if (clk_set_parent(&exynos5_clk_sclk_mmc2.clk,
				&exynos5_clk_mout_mpll_user.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos5_clk_mout_mpll_user.clk.name,
				exynos5_clk_sclk_mmc2.clk.name);

	clk_set_rate(&exynos5_clk_sclk_mmc0.clk, 800*MHZ);
	clk_set_rate(&exynos5_clk_sclk_mmc1.clk, 800*MHZ);
	clk_set_rate(&exynos5_clk_sclk_mmc2.clk, 800*MHZ);

	if (clk_set_parent(&exynos5_clk_mout_aclk_300_disp1_mid1.clk, &exynos5_clk_mout_cpll.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos5_clk_mout_cpll.clk.name,
				exynos5_clk_mout_aclk_300_disp1_mid1.clk.name);
	if (clk_set_parent(&exynos5_clk_mout_aclk_300_disp1.clk, &exynos5_clk_mout_aclk_300_disp1_mid1.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos5_clk_mout_aclk_300_disp1_mid1.clk.name,
				exynos5_clk_mout_aclk_300_disp1.clk.name);
	if (clk_set_parent(&exynos5_clk_aclk_300_disp1.clk, &exynos5_clk_dout_aclk_300_disp1.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos5_clk_dout_aclk_300_disp1.clk.name,
				exynos5_clk_aclk_300_disp1.clk.name);

	if (clk_set_parent(&exynos5_clk_mout_aclk_300_gscl_mid1.clk, &exynos5_clk_mout_cpll.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos5_clk_mout_aclk_300_gscl_mid1.clk.name,
				exynos5_clk_mout_cpll.clk.name);
	if (clk_set_parent(&exynos5_clk_mout_aclk_300_gscl.clk, &exynos5_clk_mout_aclk_300_gscl_mid1.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos5_clk_mout_aclk_300_gscl.clk.name,
				exynos5_clk_mout_aclk_300_gscl_mid1.clk.name);
	if (clk_set_parent(&exynos5_clk_aclk_300_gscl.clk, &exynos5_clk_dout_aclk_300_gscl.clk))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				exynos5_clk_aclk_300_gscl.clk.name,
				exynos5_clk_dout_aclk_300_gscl.clk.name);

	clk_set_rate(&exynos5_clk_dout_aclk_300_disp1.clk, 334000000);
	clk_set_rate(&exynos5_clk_dout_aclk_300_gscl.clk, 334000000);

	clk_set_rate(&exynos5_clk_sclk_apll.clk, 100000000);
	clk_set_rate(&exynos5_clk_aclk_266.clk, 300000000);

	clk_set_rate(&exynos5_clk_aclk_acp.clk, 267000000);
	clk_set_rate(&exynos5_clk_pclk_acp.clk, 134000000);

	clk_set_rate(&clk_fout_vpll, 268000000);

	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_clksrcs); ptr++) {
		struct clksrc_clk *clksrc;
		s3c_set_clksrc(&exynos5_clksrcs[ptr], true);
		if (exynos5_clksrcs[ptr].clk.devname &&
				!strcmp(exynos5_clksrcs[ptr].clk.devname,
					"dw_mmc.0")) {
			clksrc = &exynos5_clksrcs[ptr];
			clk_set_rate(&clksrc->clk, 800 * MHZ);
		}

		if (exynos5_clksrcs[ptr].clk.devname &&
				!strcmp(exynos5_clksrcs[ptr].clk.devname,
					"dw_mmc.1")) {
			clksrc = &exynos5_clksrcs[ptr];
			clk_set_rate(&clksrc->clk, 200 * MHZ);
		}

		if (exynos5_clksrcs[ptr].clk.devname &&
				!strcmp(exynos5_clksrcs[ptr].clk.devname,
					"dw_mmc.2")) {
			clksrc = &exynos5_clksrcs[ptr];
			clk_set_rate(&clksrc->clk, 200 * MHZ);
		}
	}
}

void __init exynos5250_register_clocks(void)
{
	int ptr;

	s3c24xx_register_clocks(exynos5_clks, ARRAY_SIZE(exynos5_clks));

	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_sysclks); ptr++)
		s3c_register_clksrc(exynos5_sysclks[ptr], 1);

	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_sclk_tv); ptr++)
		s3c_register_clksrc(exynos5_sclk_tv[ptr], 1);

	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_clksrc_cdev); ptr++)
		s3c_register_clksrc(exynos5_clksrc_cdev[ptr], 1);

	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_clksrc_aclk_isp); ptr++)
		s3c_register_clksrc(exynos5_clksrc_aclk_isp[ptr], 1);

	s3c_register_clksrc(exynos5_clksrcs, ARRAY_SIZE(exynos5_clksrcs));
	s3c_register_clocks(exynos5_init_clocks_on, ARRAY_SIZE(exynos5_init_clocks_on));

	s3c24xx_register_clocks(exynos5_clk_cdev, ARRAY_SIZE(exynos5_clk_cdev));
	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_clk_cdev); ptr++)
		s3c_disable_clocks(exynos5_clk_cdev[ptr], 1);

	s3c_register_clocks(exynos5_init_clocks_off, ARRAY_SIZE(exynos5_init_clocks_off));
	s3c_disable_clocks(exynos5_init_clocks_off, ARRAY_SIZE(exynos5_init_clocks_off));
	clkdev_add_table(exynos5_clk_lookup, ARRAY_SIZE(exynos5_clk_lookup));

	s3c24xx_register_clock(&exynos5_c2c_clock);
	s3c_disable_clocks(&exynos5_c2c_clock, 1);

	s3c_disable_clocks(&exynos5_clk_clkout.clk, 1);
	register_syscore_ops(&exynos5_clock_syscore_ops);
	s3c24xx_register_clock(&dummy_apb_pclk);

	s3c_pwmclk_init();
}
