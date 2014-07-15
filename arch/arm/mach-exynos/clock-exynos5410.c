/* linux/arch/arm/mach-exynos/clock-exynos5410.c
 *
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5410 - Clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/syscore_ops.h>
#include <linux/jiffies.h>

#include <plat/cpu-freq.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/s5p-clock.h>
#include <plat/clock-clksrc.h>
#include <plat/pm.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/regs-pmu.h>
#include <mach/sysmmu.h>
#include "board-smdk5410.h"

void wait_clkdiv_stable_time(void __iomem *reg,
			unsigned int mask, unsigned int val)
{
	unsigned long timeout;
	unsigned int temp;
	bool ret;

	timeout = jiffies + HZ;
	do {
		temp = __raw_readl(reg);
		ret = val ? temp == mask : !(temp & mask);
		if (ret)
			goto done;

		cpu_relax();
	} while (time_before(jiffies, timeout));

	/*
	 * If register status is not changed for at most 1 second
	 * (200 jiffies), system occurs kernel panic.
	 */
	pr_err("Register(0x%x) status is not changed, status : 0x%x\n",
			(unsigned int*)(reg), temp);
	BUG();

done:
	return;
}

/* This setup_pll function will set rate and set parent the pll */
static void setup_pll(const char *pll_name,
			struct clk *parent_clk, unsigned long rate)
{
	struct clk *tmp_clk;

	clk_set_rate(parent_clk, rate);
	tmp_clk = clk_get(NULL, pll_name);
	clk_set_parent(tmp_clk, parent_clk);
	clk_put(tmp_clk);
}

static void __init exynos5410_pll_init(void)
{
	/* CPLL MFC and DISP are used. Also CPLL clock rate is 640MHz */
	setup_pll("mout_cpll", &clk_fout_cpll, 640000000);
	/* DPLL gscaler and scaler are used. Also DPLL clock rate is 600MHz */
	setup_pll("mout_dpll", &clk_fout_dpll, 600000000);
	/* EPLL audio is used. Also EPLL clock rate is 400MHz */
	setup_pll("mout_epll", &clk_fout_epll, 400000000);
	/* VPLL G3D is used. Also VPLL clock rate is 350MHz */
	setup_pll("mout_vpll", &clk_fout_vpll, 350000000);
	/* IPLL MIPI CSI and ISP are used. Also IPLL clock rate is 432MHz */
	setup_pll("mout_ipll", &clk_fout_ipll, 432000000);
}

#ifdef CONFIG_PM_SLEEP
static struct sleep_save exynos5410_clock_save[] = {
	SAVE_ITEM(EXYNOS5_CLKSRC_CPU),
	SAVE_ITEM(EXYNOS5_CLKSRC_CPERI0),
	SAVE_ITEM(EXYNOS5_CLKSRC_CPERI1),
	SAVE_ITEM(EXYNOS5_CLKSRC_TOP0),
	SAVE_ITEM(EXYNOS5_CLKSRC_TOP1),
	SAVE_ITEM(EXYNOS5_CLKSRC_TOP2),
	SAVE_ITEM(EXYNOS5_CLKSRC_TOP3),
	SAVE_ITEM(EXYNOS5_CLKSRC_MAUDIO),
	SAVE_ITEM(EXYNOS5_CLKSRC_CDREX),
	SAVE_ITEM(EXYNOS5_CLKSRC_DISP0_0),
	SAVE_ITEM(EXYNOS5_CLKSRC_DISP0_1),
	SAVE_ITEM(EXYNOS5_CLKSRC_DISP1_0),
	SAVE_ITEM(EXYNOS5_CLKSRC_DISP1_1),
	SAVE_ITEM(EXYNOS5_CLKSRC_KFC),
	SAVE_ITEM(EXYNOS5_CLKSRC_PERIC0),
	SAVE_ITEM(EXYNOS5_CLKSRC_PERIC1),
	SAVE_ITEM(EXYNOS5_CLKSRC_GSCL),
	SAVE_ITEM(EXYNOS5_CLKDIV_GEN),
	SAVE_ITEM(EXYNOS5_CLKDIV_CDREX),
	SAVE_ITEM(EXYNOS5_CLKDIV_CDREX2),
	SAVE_ITEM(EXYNOS5_CLKDIV_ACP),
	SAVE_ITEM(EXYNOS5_CLKDIV_TOP0),
	SAVE_ITEM(EXYNOS5_CLKDIV_TOP1),
	SAVE_ITEM(EXYNOS5_CLKDIV_TOP2),
	SAVE_ITEM(EXYNOS5_CLKDIV_TOP3),
	SAVE_ITEM(EXYNOS5_CLKDIV_FSYS0),
	SAVE_ITEM(EXYNOS5_CLKDIV_FSYS1),
	SAVE_ITEM(EXYNOS5_CLKDIV_FSYS2),
	SAVE_ITEM(EXYNOS5_CLKDIV_GSCL),
	SAVE_ITEM(EXYNOS5_CLKDIV_DISP0_0),
	SAVE_ITEM(EXYNOS5_CLKDIV_DISP0_1),
	SAVE_ITEM(EXYNOS5_CLKDIV_DISP1_0),
	SAVE_ITEM(EXYNOS5_CLKDIV_DISP1_1),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC0),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC1),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC2),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC3),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC4),
	SAVE_ITEM(EXYNOS5_CLKDIV_PERIC5),
	SAVE_ITEM(EXYNOS5_CLKDIV_CPERI1),
	SAVE_ITEM(EXYNOS5410_CLKDIV_G2D),
	SAVE_ITEM(EXYNOS5_CLKGATE_BUS_DISP1),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_PERIC),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_PERIS),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_FSYS),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_GSCL0),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_GSCL),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_GEN),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_G2D),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_DISP0),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_DISP1),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_MFC),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_G3D),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_CDREX),
	SAVE_ITEM(EXYNOS5_CLKGATE_IP_CORE),
	SAVE_ITEM(EXYNOS5_CLKGATE_SCLK_CPU),
	SAVE_ITEM(EXYNOS5_CLKSRC_FSYS),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_PERIC0),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_FSYS),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_DISP0_0),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_DISP1_0),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_PERIC1),
	SAVE_ITEM(EXYNOS5_CLKSRC_MASK_MAUDIO),
	SAVE_ITEM(EXYNOS5_CLKGATE_BUS_DISP1),
	SAVE_ITEM(EXYNOS5_CLKGATE_BUS_FSYS0),
};

static struct sleep_save exynos5410_cpll_save[] = {
	SAVE_ITEM(EXYNOS5_CPLL_LOCK),
	SAVE_ITEM(EXYNOS5_CPLL_CON0),
};

static struct sleep_save exynos5410_dpll_save[] = {
	SAVE_ITEM(EXYNOS5_DPLL_LOCK),
	SAVE_ITEM(EXYNOS5_DPLL_CON0),
};

static struct sleep_save exynos5410_ipll_save[] = {
	SAVE_ITEM(EXYNOS5_IPLL_LOCK),
	SAVE_ITEM(EXYNOS5_IPLL_CON0),
};

static struct sleep_save exynos5410_epll_save[] = {
	SAVE_ITEM(EXYNOS5_EPLL_LOCK),
	SAVE_ITEM(EXYNOS5_EPLL_CON0),
	SAVE_ITEM(EXYNOS5_EPLL_CON1),
};

static struct sleep_save exynos5410_vpll_save[] = {
	SAVE_ITEM(EXYNOS5_VPLL_LOCK),
	SAVE_ITEM(EXYNOS5_VPLL_CON0),
	SAVE_ITEM(EXYNOS5_VPLL_CON1),
};
#endif

static int exynos5_clk_ip_core_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_CORE, clk, enable);
}

static int exynos5_clk_ip_peric_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_PERIC, clk, enable);
}

static int exynos5_clksrc_mask_peric0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_PERIC0, clk, enable);
}

static int exynos5_clk_ip_peris_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_PERIS, clk, enable);
}

static int exynos5_clk_ip_fsys_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_FSYS, clk, enable);
}

static int exynos5_clk_bus_disp1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_BUS_DISP1, clk, enable);
}

static int exynos5_clk_bus_fsys0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_BUS_FSYS0, clk, enable);
}

static int exynos5_clk_bus_gen_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_BUS_GEN, clk, enable);
}

static int exynos5_clksrc_mask_fsys_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_FSYS, clk, enable);
}

static int exynos5_clk_ip_gscl0_ctrl(struct clk *clk, int enable)
{
	if (clk->name && strstr(clk->name, "flite"))
		return 0;

	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_GSCL0, clk, enable);
}

static int exynos5_clk_ip_gscl1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_GSCL, clk, enable);
}

static int exynos5_clk_ip_gen_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_GEN, clk, enable);
}

static int exynos5_clk_ip_g2d_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_G2D, clk, enable);
}

static int exynos5_clk_ip_disp0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_DISP0, clk, enable);
}

static int exynos5_clksrc_mask_disp1_0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_DISP1_0, clk, enable);
}

static int exynos5_clk_ip_disp1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_DISP1, clk, enable);
}

static int exynos5_clk_ip_isp0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_ISP0, clk, enable);
}

static int exynos5_clk_ip_isp1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_ISP1, clk, enable);
}

static int exynos5_clk_ip_mfc_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_MFC, clk, enable);
}

static int exynos5_clk_ip_cdrex_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_CDREX, clk, enable);
}

static int exynos5_clk_ip_g3d_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_IP_G3D, clk, enable);
}

static int exynos5_clk_clkout_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS_PMU_DEBUG, clk, !enable);
}

static int exynos5_clk_hdmiphy_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS_HDMI_PHY_CONTROL, clk, enable);
}

static int exynos5_clk_sclk_cpu_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKGATE_SCLK_CPU, clk, enable);
}

static int exynos5_clksrc_init(struct clk *clk)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	void __iomem *reg;
	u32 mask;
	u32 val;

	if (sclk) {
		reg = sclk->reg_div.reg;
		if (reg) {
			mask = bit_mask(sclk->reg_div.shift, sclk->reg_div.size);
			val = __raw_readl(reg);
			val &= mask;
			clk->orig_div = val;
			return 0;
		}
	}
	return -1;
}

static int exynos5_clk_div_ctrl(struct clk *clk, int enable)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	void __iomem *reg = sclk->reg_div.reg;
	u32 mask = bit_mask(sclk->reg_div.shift, sclk->reg_div.size);
	u32 val;

	val = __raw_readl(reg);

	if (enable) {
		val &= ~mask;
		val |= clk->orig_div;
	} else {
		clk->orig_div = val & mask;
		val |= mask;
	}

	__raw_writel(val, reg);

	return 0;
}

static int exynos5_clk_src_ctrl(struct clk *clk, int enable)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	u32 clksrc = __raw_readl(sclk->reg_src.reg);
	u32 mask = bit_mask(sclk->reg_src.shift, sclk->reg_src.size);

	if (enable) {
		clksrc &= ~mask;
		clksrc |= clk->orig_src;
	} else {
		clksrc &= ~mask;
	}

	__raw_writel(clksrc, sclk->reg_src.reg);

	return 0;
}

static int exynos5_clk_src_div_ctrl(struct clk *clk, int enable)
{
	exynos5_clk_div_ctrl(clk, enable);
	exynos5_clk_src_ctrl(clk, enable);

	return 0;
}

/*
 * Clock for PHY
 */
static struct clk exynos5_clk_sclk_hdmi24m = {
	.name		= "sclk_hdmi24m",
	.rate		= 24000000,
};

static struct clk exynos5_clk_sclk_hdmi27m = {
	.name		= "sclk_hdmi27m",
	.rate		= 24000000,
};

static struct clk exynos5_clk_sclk_hdmiphy = {
	.name		= "sclk_hdmiphy",
};

static struct clk exynos5_clk_sclk_dptxphy = {
	.name		= "sclk_dptx",
};

static struct clk exynos5_clk_sclk_usbphy = {
	.name		= "sclk_usbphy",
	.rate		= 48000000,
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

static int exynos5_clksrc_mask_peric1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_PERIC1, clk, enable);
}

static int exynos5_clksrc_mask_maudio_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_CLKSRC_MASK_MAUDIO, clk, enable);
}

static int exynos5_apll_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_APLL_CON1, clk, !enable);
}

static int exynos5_ipll_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_IPLL_CON1, clk, !enable);
}

static int exynos5_epll_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_EPLL_CON2, clk, !enable);
}

static int exynos5_vpll_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS5_VPLL_CON2, clk, !enable);
}

/*
 * PLL output clock
 */
struct clk clk_fout_kpll = {
	.name		= "fout_kpll",
	.id		= -1,
};

struct clk clk_fout_ipll = {
	.name		= "fout_ipll",
	.id		= -1,
};

/*
 * Mux of PLL output clock
 */
/* Mux output for APLL */
static struct clksrc_clk exynos5_clk_mout_apll = {
	.clk	= {
		.name		= "mout_apll",
	},
	.sources = &clk_src_apll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_CPU, .shift = 0, .size = 1 },
};

/* Mux output for MPLL */
static struct clksrc_clk exynos5_clk_mout_mpll = {
	.clk = {
		.name		= "mout_mpll",
	},
	.sources = &clk_src_mpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_CPERI1, .shift = 8, .size = 1 },
};

/* Mux output for EPLL */
static struct clksrc_clk exynos5_clk_mout_epll = {
	.clk	= {
		.name		= "mout_epll",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &clk_src_epll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 12, .size = 1 },
};

/* Mux output for DPLL */
static struct clksrc_clk exynos5_clk_mout_dpll = {
	.clk	= {
		.name		= "mout_dpll",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &clk_src_dpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 10, .size = 1 },
};

/* Mux output for BPLL */
static struct clk *clk_src_bpll_list[] = {
	[0] = &clk_fin_bpll,
	[1] = &clk_fout_bpll,
};

static struct clksrc_sources exynos5_clk_src_bpll = {
	.sources	= clk_src_bpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_bpll_list),
};

static struct clksrc_clk exynos5_clk_mout_bpll = {
	.clk	= {
		.name		= "mout_bpll",
	},
	.sources = &exynos5_clk_src_bpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_CDREX, .shift = 0, .size = 1 },
};

/* Mux output for CPLL */
static struct clk *clk_src_cpll_list[] = {
	[0] = &clk_fin_cpll,
	[1] = &clk_fout_cpll,
};

static struct clksrc_sources exynos5_clk_src_cpll = {
	.sources	= clk_src_cpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_cpll_list),
};

static struct clksrc_clk exynos5_clk_mout_cpll = {
	.clk	= {
		.name		= "mout_cpll",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &exynos5_clk_src_cpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 8, .size = 1 },
};

/* Mux output for KPLL */
static struct clk *clk_src_kpll_list[] = {
	[0] = &clk_fin_kpll,
	[1] = &clk_fout_kpll,
};

static struct clksrc_sources clk_src_kpll = {
	.sources	= clk_src_kpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_kpll_list),
};

static struct clksrc_clk exynos5_clk_mout_kpll = {
	.clk	= {
		.name		= "mout_kpll",
	},
	.sources = &clk_src_kpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_KFC, .shift = 0, .size = 1 },
};

/* Mux output for IPLL */
static struct clk *clk_src_ipll_list[] = {
	[0] = &clk_fin_ipll,
	[1] = &clk_fout_ipll,
};

static struct clksrc_sources clk_src_ipll = {
	.sources	= clk_src_ipll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_ipll_list),
};

static struct clksrc_clk exynos5_clk_mout_ipll = {
	.clk	= {
		.name		= "mout_ipll",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &clk_src_ipll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 14, .size = 1 },
};

/* DIV output for IPLL */
static struct clksrc_clk exynos5_clk_dout_aclk_333_432_isp = {
	.clk	= {
		.name		= "dout_aclk_333_432_isp",
		.parent		= &exynos5_clk_mout_ipll.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP2, .shift = 20, .size = 3 },
};

/* Possible clock sources for aclk_333_432_isp_sub Mux */
static struct clk *clk_src_333_432_isp_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_dout_aclk_333_432_isp.clk,
};

static struct clksrc_sources clk_src_333_432_isp = {
	.sources        = clk_src_333_432_isp_list,
	.nr_sources     = ARRAY_SIZE(clk_src_333_432_isp_list),
};

static struct clksrc_clk exynos5_clk_aclk_333_432_isp = {
	.clk	= {
		.name		= "aclk_333_432_isp",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &clk_src_333_432_isp,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 25, .size = 1 },
};

/* ISP_DIV0 */
static struct clksrc_clk exynos5_clk_isp_div0 = {
	.clk	= {
		.name		= "isp_div0",
		.parent		= &exynos5_clk_aclk_333_432_isp.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ISP0, .shift = 0, .size = 3 },
};

/* ISP_DIV1 */
static struct clksrc_clk exynos5_clk_isp_div1 = {
	.clk	= {
		.name		= "isp_div1",
		.parent		= &exynos5_clk_aclk_333_432_isp.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ISP0, .shift = 4, .size = 3 },
};

/* MPWM_DIV */
static struct clksrc_clk exynos5_clk_mpwm_div = {
	.clk	= {
		.name		= "mpwm_div",
		.parent		= &exynos5_clk_isp_div1.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ISP2, .shift = 0, .size = 3 },
};

/* DIV output for ACLK_333_432_GSCL */
static struct clksrc_clk exynos5_clk_dout_aclk_333_432_gscl = {
	.clk	= {
		.name		= "dout_aclk_333_432_gscl",
		.parent		= &exynos5_clk_mout_ipll.clk,
		.init		= exynos5_clksrc_init,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP2, .shift = 24, .size = 3 },
};

/* Possible clock sources for aclk_333_432_gscl_sub Mux */
static struct clk *clk_src_aclk_333_432_gscl_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_dout_aclk_333_432_gscl.clk,
};

static struct clksrc_sources clk_src_aclk_333_432_gscl = {
	.sources        = clk_src_aclk_333_432_gscl_list,
	.nr_sources     = ARRAY_SIZE(clk_src_aclk_333_432_gscl_list),
};

static struct clksrc_clk exynos5_clk_aclk_333_432_gscl = {
	.clk	= {
		.name		= "aclk_333_432_gscl",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &clk_src_aclk_333_432_gscl,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 26, .size = 1 },
};

/* GSCL_DIV */
static struct clksrc_clk exynos5_clk_pclk_166_gscl = {
	.clk    = {
		.name           = "pclk_166_gscl",
		.parent         = &exynos5_clk_aclk_333_432_gscl.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV2_RATIO0, .shift = 6, .size = 2 },
};

/* Mux output for VPLL_SRC */
static struct clk *exynos5_clkset_mout_vpllsrc_list[] = {
	[0] = &clk_fin_vpll,
	[1] = &exynos5_clk_sclk_hdmi27m,
};

static struct clksrc_sources exynos5_clkset_mout_vpllsrc = {
	.sources	= exynos5_clkset_mout_vpllsrc_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_mout_vpllsrc_list),
};

static struct clksrc_clk exynos5_clk_mout_vpllsrc = {
	.clk	= {
		.name		= "vpll_src",
	},
	.sources = &exynos5_clkset_mout_vpllsrc,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 0, .size = 1 },
};

/* Mux output for VPLL */
static struct clk *exynos5_clkset_mout_vpll_list[] = {
	[0] = &exynos5_clk_mout_vpllsrc.clk,
	[1] = &clk_fout_vpll,
};

static struct clksrc_sources exynos5_clkset_mout_vpll = {
	.sources	= exynos5_clkset_mout_vpll_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_mout_vpll_list),
};

static struct clksrc_clk exynos5_clk_mout_vpll = {
	.clk	= {
		.name		= "mout_vpll",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &exynos5_clkset_mout_vpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 16, .size = 1 },
};

static struct clksrc_clk exynos5_clk_sclk_pixel = {
	.clk	= {
		.name		= "sclk_pixel",
		.parent		= &exynos5_clk_mout_vpll.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 28, .size = 4 },
};

static struct clk *exynos5_clkset_sclk_hdmi_list[] = {
	[0] = &exynos5_clk_sclk_pixel.clk,
	[1] = &exynos5_clk_sclk_hdmiphy,
};

static struct clksrc_sources exynos5_clkset_sclk_hdmi = {
	.sources        = exynos5_clkset_sclk_hdmi_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_sclk_hdmi_list),
};

static struct clksrc_clk exynos5_clk_sclk_hdmi = {
	.clk    = {
		.name           = "sclk_hdmi",
		.enable         = exynos5_clksrc_mask_disp1_0_ctrl,
		.ctrlbit        = (1 << 20),
	},
	.sources = &exynos5_clkset_sclk_hdmi,
	.reg_src = { .reg = EXYNOS5_CLKSRC_DISP1_0, .shift = 20, .size = 1 },
};

static struct clksrc_clk *exynos5_sclk_tv[] = {
	&exynos5_clk_sclk_pixel,
	&exynos5_clk_sclk_hdmi,
};

/* CPU(EAGLE & KFC) Part clock */
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
};

static struct clksrc_clk exynos5_clk_dout_arm = {
	.clk	= {
		.name		= "dout_arm",
		.parent		= &exynos5_clk_mout_cpu.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_CPU0, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos5_clk_dout_arm2 = {
	.clk	= {
		.name		= "dout_arm2",
		.parent		= &exynos5_clk_dout_arm.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_CPU0, .shift = 28, .size = 3 },
};

static struct clksrc_clk exynos5_clk_dout_acp = {
	.clk	= {
		.name		= "dout_acp",
		.parent		= &exynos5_clk_dout_arm2.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_CPU0, .shift = 8, .size = 3 },
};

static struct clk exynos5_clk_armclk = {
	.name		= "armclk",
	.parent		= &exynos5_clk_dout_acp.clk,
};

/* KFC support */
static struct clk *exynos5_clkset_mout_cpu_kfc_list[] = {
	[0] = &exynos5_clk_mout_kpll.clk,
	[1] = &exynos5_clk_mout_mpll.clk,
};

static struct clksrc_sources exynos5_clkset_mout_cpu_kfc = {
	.sources        = exynos5_clkset_mout_cpu_kfc_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_mout_cpu_kfc_list),
};

static struct clksrc_clk exynos5_clk_mout_cpu_kfc = {
	.clk    = {
		.name           = "mout_cpu_kfc",
	},
	.sources = &exynos5_clkset_mout_cpu_kfc,
	.reg_src = { .reg = EXYNOS5_CLKSRC_KFC, .shift = 16, .size = 1 },
};

static struct clksrc_clk exynos5_clk_dout_kfc = {
	.clk	= {
		.name		= "dout_kfc",
		.parent		= &exynos5_clk_mout_cpu_kfc.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_KFC0, .shift = 0, .size = 3 },
};

static struct clk exynos5_clk_kfcclk = {
	.name		= "kfcclk",
	.parent		= &exynos5_clk_dout_kfc.clk,
};

/* TOP2 Part clock */
static struct clksrc_clk exynos5_clk_dout_aclk_300_gscl = {
	.clk	= {
		.name		= "dout_aclk_300_gscl",
		.parent		= &exynos5_clk_mout_dpll.clk,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP2, .shift = 8, .size = 3 },
};

/* Possible clock sources for aclk_300_gscl_sub Mux */
static struct clk *clk_src_gscl_300_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_dout_aclk_300_gscl.clk,
};

static struct clksrc_sources clk_src_gscl_300 = {
	.sources        = clk_src_gscl_300_list,
	.nr_sources     = ARRAY_SIZE(clk_src_gscl_300_list),
};

static struct clksrc_clk exynos5_clk_aclk_300_gscl = {
	.clk	= {
		.name		= "aclk_300_gscl",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &clk_src_gscl_300,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 17, .size = 1 },
};

static struct clksrc_clk exynos5_clk_dout_aclk_300_jpeg = {
	.clk	= {
		.name		= "dout_aclk_300_jpeg",
		.parent		= &exynos5_clk_mout_dpll.clk,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP2, .shift = 28, .size = 3 },
};

/* Possible clock sources for aclk_300_jpeg_sub Mux */
static struct clk *clk_src_jpeg_300_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_dout_aclk_300_jpeg.clk,
};

static struct clksrc_sources clk_src_jpeg_300 = {
	.sources        = clk_src_jpeg_300_list,
	.nr_sources     = ARRAY_SIZE(clk_src_jpeg_300_list),
};

static struct clksrc_clk exynos5_clk_aclk_300_jpeg = {
	.clk	= {
		.name		= "aclk_300_jpeg",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &clk_src_jpeg_300,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 27, .size = 1 },
};

static struct clksrc_clk exynos5_clk_dout_aclk_300_disp1 = {
	.clk	= {
		.name		= "dout_aclk_300_disp1",
		.parent		= &exynos5_clk_mout_dpll.clk,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP2, .shift = 16, .size = 3 },
};

/* Possible clock sources for aclk_300_disp1_sub Mux */
static struct clk *clk_src_disp1_300_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_dout_aclk_300_disp1.clk,
};

static struct clksrc_sources clk_src_disp1_300 = {
	.sources	= clk_src_disp1_300_list,
	.nr_sources	= ARRAY_SIZE(clk_src_disp1_300_list),
};

static struct clksrc_clk exynos5_clk_aclk_300_disp1 = {
	.clk	= {
		.name		= "aclk_300_disp1",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &clk_src_disp1_300,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 19, .size = 1 },
};

static struct clksrc_clk exynos5_clk_sclk_jpeg = {
	.clk	= {
		.name		= "sclk_jpeg",
		.parent		= &exynos5_clk_mout_cpll.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_GEN, .shift = 4, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_aclk_300_disp0 = {
	.clk	= {
		.name		= "dout_aclk_300_disp0",
		.parent		= &exynos5_clk_mout_dpll.clk,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP2, .shift = 12, .size = 3 },
};

/* CDREX Part clock */
static struct clksrc_clk exynos5_clk_dout_sclk_cdrex = {
	.clk	= {
		.name		= "dout_sclk_cdrex",
		.parent		= &exynos5_clk_mout_bpll.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_CDREX, .shift = 24, .size = 3 },
};

static struct clksrc_clk exynos5_clk_dout_clk2x_phy = {
	.clk	= {
		.name		= "dout_clk2x_phy",
		.parent		= &exynos5_clk_dout_sclk_cdrex.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_CDREX, .shift = 3, .size = 5 },
};

static struct clksrc_clk exynos5_clk_dout_cclk_cdrex = {
	.clk	= {
		.name		= "dout_cclk_cdrex",
		.parent		= &exynos5_clk_dout_clk2x_phy.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_CDREX, .shift = 8, .size = 3 },
};

static struct clksrc_clk exynos5_clk_dout_pclk_cdrex = {
	.clk	= {
		.name		= "dout_pclk_cdrex",
		.parent		= &exynos5_clk_dout_cclk_cdrex.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
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

/* Top Part clock */

/* Mux output for MPLL_USER */
static struct clk *exynos5_clkset_mout_mpll_user_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos5_clk_mout_mpll.clk,
};

static struct clksrc_sources exynos5_clkset_mout_mpll_user = {
	.sources	= exynos5_clkset_mout_mpll_user_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_mout_mpll_user_list),
};

static struct clksrc_clk exynos5_clk_mout_mpll_user = {
	.clk	= {
		.name		= "mout_mpll_user",
	},
	.sources = &exynos5_clkset_mout_mpll_user,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 20, .size = 1 },
};

/* Mux output for BPLL_USER */
static struct clk *exynos5_clkset_mout_bpll_user_list[] = {
	[0] = &clk_fin_bpll,
	[1] = &exynos5_clk_mout_bpll.clk,
};

static struct clksrc_sources exynos5_clkset_mout_bpll_user = {
	.sources	= exynos5_clkset_mout_bpll_user_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_mout_bpll_user_list),
};

static struct clksrc_clk exynos5_clk_mout_bpll_user = {
	.clk	= {
		.name		= "mout_bpll_user",
	},
	.sources = &exynos5_clkset_mout_bpll_user,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP2, .shift = 24, .size = 1 },
};

/* Mux output for MPLL_BPLL */
static struct clk *exynos5_clkset_mout_mpll_bpll_list[] = {
	[0] = &exynos5_clk_mout_mpll_user.clk,
	[1] = &exynos5_clk_mout_bpll_user.clk,
};

static struct clksrc_sources exynos5_clkset_mout_mpll_bpll = {
	.sources	= exynos5_clkset_mout_mpll_bpll_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_mout_mpll_bpll_list),
};

static struct clksrc_clk exynos5_clk_mout_mpll_bpll = {
	.clk	= {
		.name		= "mout_mpll_bpll",
	},
	.sources = &exynos5_clkset_mout_mpll_bpll,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP1, .shift = 20, .size = 1 },
};

/* Clock for ACLK_XXX */
static struct clk *exynos5_clkset_aclk_xxx_list[] = {
	[0] = &exynos5_clk_mout_mpll_user.clk,
	[1] = &exynos5_clk_mout_bpll_user.clk,
};

static struct clksrc_sources exynos5_clkset_aclk_xxx = {
	.sources	= exynos5_clkset_aclk_xxx_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_aclk_xxx_list),
};

static struct clk *exynos5_clkset_aclk_xxx_pre_list[] = {
	[0] = &exynos5_clk_mout_cpll.clk,
	[1] = &exynos5_clk_mout_mpll_user.clk,
};

static struct clksrc_sources exynos5_clkset_aclk_xxx_pre = {
	.sources	= exynos5_clkset_aclk_xxx_pre_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_aclk_xxx_pre_list),
};

/* ACKL_400 */
static struct clksrc_clk exynos5_clk_aclk_400 = {
	.clk	= {
		.name		= "aclk_400",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.sources = &exynos5_clkset_aclk_xxx,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 20, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 24, .size = 3 },
};

/* ACKL_400_ISP_PRE */
static struct clksrc_clk exynos5_clk_aclk_400_isp_pre = {
	.clk	= {
		.name		= "aclk_400_isp_pre",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_src_div_ctrl,
	},
	.sources = &exynos5_clkset_aclk_xxx,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP1, .shift = 24, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP1, .shift = 20, .size = 3 },
};

/* ACLK_400_ISP */
static struct clk *exynos5_clkset_aclk_400_isp_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_aclk_400_isp_pre.clk,
};

static struct clksrc_sources exynos5_clkset_aclk_400_isp = {
	.sources        = exynos5_clkset_aclk_400_isp_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_aclk_400_isp_list),
};

static struct clksrc_clk exynos5_clk_aclk_400_isp = {
	.clk	= {
		.name		= "aclk_400_isp",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &exynos5_clkset_aclk_400_isp,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 20, .size = 1 },
};

/* MCUISP_DIV0 */
static struct clksrc_clk exynos5_clk_mcuisp_div0 = {
	.clk	= {
		.name		= "mcuisp_div0",
		.parent		= &exynos5_clk_aclk_400_isp.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ISP1, .shift = 0, .size = 3 },
};

/* MCUISP_DIV1 */
static struct clksrc_clk exynos5_clk_mcuisp_div1 = {
	.clk	= {
		.name		= "mcuisp_div1",
		.parent		= &exynos5_clk_aclk_400_isp.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_ISP1, .shift = 4, .size = 3 },
};

/* ACKL_266_GSCL_PRE */
static struct clksrc_clk exynos5_clk_aclk_266_gscl_pre = {
	.clk	= {
		.name		= "aclk_266_gscl_pre",
		.enable		= exynos5_clk_src_div_ctrl,
		.init		= exynos5_clksrc_init,
	},
	.sources = &exynos5_clkset_aclk_xxx,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP1, .shift = 28, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP2, .shift =  4, .size = 3 },
};

/* ACLK_266_GSCL */
static struct clk *exynos5_clkset_aclk_266_gscl_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_aclk_266_gscl_pre.clk,
};

static struct clksrc_sources exynos5_clkset_aclk_266_gscl = {
	.sources	= exynos5_clkset_aclk_266_gscl_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_aclk_266_gscl_list),
};

static struct clksrc_clk exynos5_clk_aclk_266_gscl = {
	.clk	= {
		.name		= "aclk_266_gscl",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &exynos5_clkset_aclk_266_gscl,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 8, .size = 1 },
};

/* ACLK_333_PRE */
static struct clksrc_clk exynos5_clk_aclk_333_pre = {
	.clk	= {
		.name		= "aclk_333_pre",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_src_div_ctrl,
	},
	.sources = &exynos5_clkset_aclk_xxx_pre,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 16, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 20, .size = 3 },
};

/* ACLK_333 */
static struct clk *exynos5_clkset_aclk_333_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_aclk_333_pre.clk,
};

static struct clksrc_sources exynos5_clkset_aclk_333 = {
	.sources	= exynos5_clkset_aclk_333_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_aclk_333_list),
};

static struct clksrc_clk exynos5_clk_aclk_333 = {
	.clk	= {
		.name		= "aclk_333",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &exynos5_clkset_aclk_333,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 24, .size = 1 },
};

/* ACKL_266 */
static struct clksrc_clk exynos5_clk_aclk_266 = {
	.clk	= {
		.name		= "aclk_266",
		.parent		= &exynos5_clk_mout_mpll_user.clk,
		.enable		= exynos5_clk_div_ctrl,
		.init		= exynos5_clksrc_init,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 16, .size = 3 },
};

/* ACLK_266_ISP */
static struct clk *exynos5_clkset_aclk_266_isp_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_aclk_266.clk,
};

static struct clksrc_sources exynos5_clkset_aclk_266_isp = {
	.sources        = exynos5_clkset_aclk_266_isp_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_aclk_266_isp_list),
};

static struct clksrc_clk exynos5_clk_aclk_266_isp = {
	.clk	= {
		.name		= "aclk_266_isp",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &exynos5_clkset_aclk_266_isp,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 16, .size = 1 },
};

/* ACLK_200 */
static struct clksrc_clk exynos5_clk_aclk_200 = {
	.clk	= {
		.name		= "aclk_200",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_src_div_ctrl,
	},
	.sources = &exynos5_clkset_aclk_xxx,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 12, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 12, .size = 3 },
};

/* Possible clock sources for aclk_200_disp1_sub Mux */
static struct clk *clk_src_disp1_200_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_aclk_200.clk,
};

/* ACLK_200_DSP0 */
static struct clksrc_sources exynos5_clkset_aclk_200_disp0 = {
	.sources	= clk_src_disp1_200_list,
	.nr_sources	= ARRAY_SIZE(clk_src_disp1_200_list),
};

static struct clksrc_clk exynos5_clk_aclk_200_disp0 = {
	.clk	= {
		.name		= "aclk_200_disp0",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &exynos5_clkset_aclk_200_disp0,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 0, .size = 1 },
};

/* ACLK_200_DSP1 */
static struct clksrc_clk exynos5_clk_aclk_200_disp1 = {
	.clk	= {
		.name		= "aclk_200_disp1",
		.enable		= exynos5_clk_src_ctrl,
	},
	.sources = &exynos5_clkset_aclk_200_disp0,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 4, .size = 1 },
};

/* ACLK_166 */
static struct clksrc_clk exynos5_clk_aclk_166 = {
	.clk	= {
		.name		= "aclk_166",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_src_div_ctrl,
	},
	.sources = &exynos5_clkset_aclk_xxx_pre,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 8, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 8, .size = 3 },
};

/* For ACLK_66 */
static struct clksrc_clk exynos5_clk_dout_aclk_66_pre = {
	.clk	= {
		.name		= "aclk_66_pre",
		.parent		= &exynos5_clk_mout_mpll_user.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP1, .shift = 24, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_66 = {
	.clk	= {
		.name		= "aclk_66",
		.parent		= &exynos5_clk_dout_aclk_66_pre.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP0, .shift = 0, .size = 3 },
};

/* For CLKOUT */
struct clk *exynos5_clkset_clk_clkout_list[] = {
	/* Others are for debugging */
	[16] = &clk_xxti,
	[17] = &clk_xusbxti,
};

struct clksrc_sources exynos5_clkset_clk_clkout = {
	.sources        = exynos5_clkset_clk_clkout_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_clk_clkout_list),
};

static struct clksrc_clk exynos5_clk_clkout = {
	.clk	= {
		.name		= "clkout",
		.enable         = exynos5_clk_clkout_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos5_clkset_clk_clkout,
	.reg_src = { .reg = EXYNOS_PMU_DEBUG, .shift = 8, .size = 5 },
};

/* For mipihsi */
static struct clk *exynos5_clkset_mout_mipihsi_list[] = {
	[0] = &exynos5_clk_mout_mpll_user.clk,
	[1] = &exynos5_clk_mout_bpll_user.clk,
};

static struct clksrc_sources exynos5_clkset_mout_mipihsi = {
	.sources        = exynos5_clkset_mout_mipihsi_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_mout_mipihsi_list),
};


static struct clksrc_clk exynos5_clk_mipihsi = {
	.clk	= {
		.name		= "mipihsi_txbase",
		.init           = exynos5_clksrc_init,
		.enable         = exynos5_clk_div_ctrl,
		.parent		= &exynos5_clk_mout_bpll_user.clk,
	},
	.sources = &exynos5_clkset_mout_mipihsi,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP1, .shift = 16, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP1, .shift = 28, .size = 3 },
};
static struct clk *clkset_sclk_mau_audio0_list[] = {
	[0] = &exynos5_clk_audiocdclk0.clk,
	[1] = &clk_ext_xtal_mux,
	[2] = &exynos5_clk_sclk_hdmi27m,
	[3] = &exynos5_clk_sclk_dptxphy,
	[4] = &exynos5_clk_sclk_usbphy,
	[5] = &exynos5_clk_sclk_hdmiphy,
	[6] = &exynos5_clk_mout_mpll.clk,
	[7] = &exynos5_clk_mout_epll.clk,
	[8] = &exynos5_clk_mout_vpll.clk,
	[9] = &exynos5_clk_mout_cpll.clk,
};

static struct clksrc_sources exynos5_clkset_sclk_mau_audio0 = {
	.sources	= clkset_sclk_mau_audio0_list,
	.nr_sources	= ARRAY_SIZE(clkset_sclk_mau_audio0_list),
};

static struct clksrc_clk exynos5_clk_sclk_mau_audio0 = {
	.clk	= {
		.name		= "sclk_mau_audio0",
		.enable		= exynos5_clksrc_mask_maudio_ctrl,
		.ctrlbit	= (1 << 0),
	},
	.sources = &exynos5_clkset_sclk_mau_audio0,
	.reg_src = { .reg = EXYNOS5_CLKSRC_MAUDIO, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_MAUDIO, .shift = 0, .size = 4 },
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
	[8] = &exynos5_clk_mout_vpll.clk,
	[9] = &exynos5_clk_mout_cpll.clk,
};

static struct clksrc_sources exynos5_clkset_sclk_audio0 = {
	.sources	= clkset_sclk_audio0_list,
	.nr_sources	= ARRAY_SIZE(clkset_sclk_audio0_list),
};

static struct clksrc_clk exynos5_clk_sclk_audio0 = {
	.clk	= {
		.name		= "sclk_audio",
		.enable		= exynos5_clksrc_mask_peric1_ctrl,
		.ctrlbit	= (1 << 12),
	},
	.sources = &exynos5_clkset_sclk_audio0,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC1, .shift = 12, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC5, .shift = 24, .size = 4 },
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
	[8] = &exynos5_clk_mout_vpll.clk,
	[9] = &exynos5_clk_mout_cpll.clk,
};

static struct clksrc_sources exynos5_clkset_sclk_audio1 = {
	.sources        = exynos5_clkset_sclk_audio1_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_sclk_audio1_list),
};

static struct clksrc_clk exynos5_clk_sclk_audio1 = {
	.clk    = {
		.name           = "sclk_audio1",
		.enable         = exynos5_clksrc_mask_peric1_ctrl,
		.ctrlbit        = (1 << 0),
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
	[8] = &exynos5_clk_mout_vpll.clk,
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

static struct clk *exynos5_clkset_usbdrd30_list[] = {
	[0] = &exynos5_clk_mout_mpll_bpll.clk,
	[1] = &clk_xxti,
};

static struct clksrc_sources exynos5_clkset_usbdrd30 = {
	.sources        = exynos5_clkset_usbdrd30_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_usbdrd30_list),
};

static struct clksrc_clk exynos5_clk_sclk_usbdrd300 = {
	.clk	= {
		.name		= "sclk_usbdrd30",
		.devname	= "exynos-dwc3.0",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.sources = &exynos5_clkset_usbdrd30,
	.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 28, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS0, .shift = 24, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_usbdrd301 = {
	.clk	= {
		.name		= "sclk_usbdrd30",
		.devname	= "exynos-dwc3.1",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.sources = &exynos5_clkset_usbdrd30,
	.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 29, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS0, .shift = 28, .size = 4 },
};

static struct clksrc_clk exynos5_dout_usbphy300 = {
	.clk	= {
		.name		= "dout_usbphy30",
		.devname        = "exynos-dwc3.0",
		.init		= exynos5_clksrc_init,
		.parent		= &exynos5_clk_sclk_usbdrd300.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS0, .shift = 16, .size = 4 },
};

static struct clksrc_clk exynos5_dout_usbphy301 = {
	.clk	= {
		.name		= "dout_usbphy30",
		.devname        = "exynos-dwc3.1",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
		.parent		= &exynos5_clk_sclk_usbdrd301.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS0, .shift = 20, .size = 4 },
};

/* Mux output for G3D */
static struct clk *exynos5_clkset_mout_g3d_list[] = {
	[0] = &exynos5_clk_mout_cpll.clk,
	[1] = &exynos5_clk_mout_vpll.clk,
};

static struct clksrc_sources exynos5_clkset_mout_g3d = {
	.sources	= exynos5_clkset_mout_g3d_list,
	.nr_sources = ARRAY_SIZE(exynos5_clkset_mout_g3d_list),
};

static struct clksrc_clk exynos5_clk_mout_g3d = {
	.clk    = {
		.name           = "mout_g3d",
	},
	.sources = &exynos5_clkset_mout_g3d,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP0, .shift = 24, .size = 1 },
};

/* SCLK_G3D_CORE */
static struct clksrc_clk exynos5_clk_sclk_g3d_core = {
	.clk    = {
		.name           = "sclk_g3d_core",
		.parent         = &exynos5_clk_mout_g3d.clk,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP3, .shift = 0,  .size = 3 },
};

/* SCLK_G3D_HYDRA */
static struct clksrc_clk exynos5_clk_sclk_g3d_hydra = {
	.clk    = {
		.name           = "sclk_g3d_hydra",
		.parent     = &exynos5_clk_mout_g3d.clk,
		.init	    = exynos5_clksrc_init,
		.enable	    = exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_TOP3, .shift = 3,  .size = 3 },
};

/* MUX for G3D_CORE_SUB */
static struct clk *exynos5_clkset_g3d_core_sub_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_sclk_g3d_core.clk,
};

static struct clksrc_sources exynos5_clkset_sclk_g3d_core_sub = {
	.sources        = exynos5_clkset_g3d_core_sub_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_g3d_core_sub_list),
};

static struct clksrc_clk exynos5_clk_sclk_g3d_core_sub = {
	.clk	= {
		.name		= "sclk_g3d_core_sub",
		.init	    = exynos5_clksrc_init,
		.enable	    = exynos5_clk_src_ctrl,
	},
	.sources = &exynos5_clkset_sclk_g3d_core_sub,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 28, .size = 1 },
};

/* MUX for G3D_HYDRA_SUB */
static struct clk *exynos5_clkset_g3d_hydra_sub_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &exynos5_clk_sclk_g3d_hydra.clk,
};

static struct clksrc_sources exynos5_clkset_sclk_g3d_hydra_sub = {
	.sources        = exynos5_clkset_g3d_hydra_sub_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_g3d_hydra_sub_list),
};

static struct clksrc_clk exynos5_clk_sclk_g3d_hydra_sub = {
	.clk	= {
		.name		= "sclk_g3d_hydra_sub",
		.init	    = exynos5_clksrc_init,
		.enable	    = exynos5_clk_src_ctrl,
	},
	.sources = &exynos5_clkset_sclk_g3d_hydra_sub,
	.reg_src = { .reg = EXYNOS5_CLKSRC_TOP3, .shift = 29, .size = 1 },
};

/* Common Clock Src group */
static struct clk *exynos5_clkset_group_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = NULL,
	[2] = &exynos5_clk_sclk_hdmi24m,
	[3] = &exynos5_clk_sclk_dptxphy,
	[4] = &exynos5_clk_sclk_usbphy,
	[5] = &exynos5_clk_sclk_hdmiphy,
	[6] = &exynos5_clk_mout_mpll_bpll.clk,
	[7] = &exynos5_clk_mout_epll.clk,
	[8] = &exynos5_clk_mout_vpll.clk,
	[9] = &exynos5_clk_mout_cpll.clk,
	[10] = &exynos5_clk_mout_dpll.clk,
};

static struct clksrc_sources exynos5_clkset_group = {

	.sources	= exynos5_clkset_group_list,
	.nr_sources	= ARRAY_SIZE(exynos5_clkset_group_list),
};

static struct clk exynos5_init_clocks[] = {
	{
		.name		= "sgx_core",
		.parent		= &exynos5_clk_sclk_g3d_core_sub.clk,
		.enable		= exynos5_clk_ip_g3d_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "sgx_hyd",
		.parent		= &exynos5_clk_sclk_g3d_hydra_sub.clk,
		.enable		= exynos5_clk_ip_g3d_ctrl,
		.ctrlbit	= ((1 << 12) | (1 << 11) | (1 << 10) | (1 << 9) | (1 << 6) | (1 << 5) | (1 << 4) | (1 << 1)),
	}, {
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
		.name           = "sysreg",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 1),
	}, {
		.name           = "pmu_apbif",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 2),
	}, {
		.name           = "cmu_toppart",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 3),
	}, {
		.name           = "tzpc0",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 6),
	}, {
		.name           = "tzpc1",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 7),
	}, {
		.name           = "tzpc2",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 8),
	}, {
		.name           = "tzpc3",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 9),
	}, {
		.name           = "tzpc4",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 10),
	}, {
		.name           = "tzpc5",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 11),
	}, {
		.name           = "tzpc6",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 12),
	}, {
		.name           = "tzpc7",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 13),
	}, {
		.name           = "tzpc8",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 14),
	}, {
		.name           = "tzpc9",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 15),
	}, {
		.name           = "seckey_apbif",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 17),
	}, {
		.name           = "st",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 18),
	}, {
		.name           = "tmu_apbif",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 21),
	},
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
		.name           = "iem",
		.enable         = &exynos5_clk_ip_core_ctrl,
		.ctrlbit        = (0x3 << 17),
	}, {
		.name		= "iop",
		.enable		= &exynos5_clk_ip_core_ctrl,
		.ctrlbit	= (1 << 21) | (1 << 3),
	}, {
		.name		= "aclk_g2d_asb",
		.enable		= &exynos5_clk_ip_cdrex_ctrl,
		.ctrlbit	= (1 << 27),
	}, {
		.name		= "sclk_hpm",
		.enable		= &exynos5_clk_sclk_cpu_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "timers",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 24),
	}, {
		.name		= "chipid_apbif",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peris_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name           = "hdmicec",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peris_ctrl,
		.ctrlbit        = (1 << 16),
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
		.name           = "rtic",
		.enable         = exynos5_clk_ip_fsys_ctrl,
		.ctrlbit        = ((1 << 11) | (1 << 9)),
	}, {
		.name           = "sromc",
		.enable         = exynos5_clk_ip_fsys_ctrl,
		.ctrlbit        = (1 << 17),
	}, {
		.name           = "usbdrd30",
		.devname	= "exynos-dwc3.1",
		.enable         = exynos5_clk_ip_fsys_ctrl,
		.parent		= &exynos5_dout_usbphy301.clk,
		.ctrlbit        = (1 << 20),
	}, {
		.name           = "usbdrd30",
		.devname	= "exynos-dwc3.0",
		.enable         = exynos5_clk_ip_fsys_ctrl,
		.parent		= &exynos5_dout_usbphy300.clk,
		.ctrlbit        = (1 << 19),
	}, {
		.name           = "ppmusfmc",
		.enable         = exynos5_clk_ip_fsys_ctrl,
		.ctrlbit        = ((1 << 27) | (1 << 24)),
	}, {
		.name           = "smmusfmc",
		.enable         = exynos5_clk_ip_fsys_ctrl,
		.ctrlbit        = (1 << 30),
	}, {
		.name           = "fsysqe",
		.enable         = exynos5_clk_ip_fsys_ctrl,
		.ctrlbit        = ((1 << 26) | (1 << 25) | (1 << 16) | (1 << 15) | (1 << 5) | (1 << 4)),
	}, {
		.name		= "dwmci",
		.devname	= "dw_mmc.0",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name           = "adc",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peric_ctrl,
		.ctrlbit        = (1 << 15),
	}, {
		.name           = "pcm0",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peric_ctrl,
		.ctrlbit        = (1 << 25),
	}, {
		.name           = "i2s0",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peric_ctrl,
		.ctrlbit        = (1 << 19),
	}, {
		.name		= "dwmci",
		.devname	= "dw_mmc.1",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "dwmci",
		.devname	= "dw_mmc.2",
		.parent		= &exynos5_clk_aclk_200.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "spi",
		.devname	= "s3c64xx-spi.0",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "spi",
		.devname	= "s3c64xx-spi.1",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 17),
	}, {
		.name		= "spi",
		.devname	= "s3c64xx-spi.2",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "spi",
		.devname	= "s3c64xx-spi.3",
		.enable		= exynos5_clk_ip_isp1_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.1",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 20),
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.2",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 21),
	}, {
		.name		= "pcm",
		.devname	= "samsung-pcm.1",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 22),
	}, {
		.name		= "pcm",
		.devname	= "samsung-pcm.2",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 23),
	}, {
		.name		= "spdif",
		.devname	= "samsung-spdif",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 26),
	}, {
		.name		= "ac97",
		.devname	= "samsung-ac97",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 27),
	}, {
		.name		= "keyif",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 28),
	}, {
		.name		= "usbhost",
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "dp",
		.devname	= "s5p-dp",
		.enable		= exynos5_clk_ip_disp1_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "dsim0",
		.enable		= exynos5_clk_ip_disp0_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "dsim1",
		.enable		= exynos5_clk_ip_disp1_ctrl,
		.parent		= &exynos5_clk_aclk_200_disp1.clk,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "mipihsi",
		.parent		= &exynos5_clk_mipihsi.clk,
		.enable		= exynos5_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "gscl",
		.devname	= "exynos-gsc.0",
		.parent		= &exynos5_clk_aclk_300_gscl.clk,
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.ctrlbit	= ((1 << 28) | (1 << 0)),
	}, {
		.name		= "gscl",
		.devname	= "exynos-gsc.1",
		.parent		= &exynos5_clk_aclk_300_gscl.clk,
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.ctrlbit	= ((1 << 29) | (1 << 1)),
	}, {
		.name		= "gscl",
		.devname	= "exynos-gsc.2",
		.parent		= &exynos5_clk_aclk_300_gscl.clk,
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.ctrlbit	= ((1 << 30) | (1 << 2)),
	}, {
		.name		= "gscl",
		.devname	= "exynos-gsc.3",
		.parent		= &exynos5_clk_aclk_300_gscl.clk,
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.ctrlbit	= ((1 << 31) | (1 << 3)),
	}, {
		.name		= "gscl.ppmu",
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.ctrlbit	= (0x7 << 21),
	}, {
		.name		= "3aa",
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.ctrlbit	= ((1 << 24) | (1 << 19) | (1 << 9) | (1 << 4)),
	}, {
		.name		= "sc-aclk",
		.devname	= "exynos5-scaler.0",
		.parent		= &exynos5_clk_aclk_300_gscl.clk,
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "sc-pclk",
		.devname        = "exynos5-scaler.0",
		.parent         = &exynos5_clk_aclk_300_gscl.clk,
		.enable		= exynos5_clk_ip_gscl1_ctrl,
		.ctrlbit	= ((1 << 15) | (1 << 0)),
	}, {
		.name           = "3aa.bts",
		.enable         = exynos5_clk_ip_gscl1_ctrl,
		.ctrlbit        = (1 << 1),
	}, {
		.name		= "rotator",
		.devname	= "exynos-rot",
		.enable		= exynos5_clk_ip_gen_ctrl,
		.parent		= &exynos5_clk_aclk_266.clk,
		.ctrlbit	= ((1 << 11) | (1 << 1)),
	}, {
		.name           = "mfc",
		.devname        = "s3c-mfc",
		.parent		= &exynos5_clk_aclk_333.clk,
		.enable         = exynos5_clk_ip_mfc_ctrl,
		.ctrlbit        = ((3 << 3) | (1 << 0)),
	}, {
		.name           = "mfc.bts",
		.enable         = exynos5_clk_ip_mfc_ctrl,
		.ctrlbit        = (3 << 3),
	}, {
		.name           = "mfc.ppmu",
		.enable         = exynos5_clk_ip_mfc_ctrl,
		.ctrlbit        = (3 << 5),
	}, {
		.name		= "isp0_333_432",
		.devname	= "exynos5-fimc-is",
		.parent         = &exynos5_clk_aclk_333_432_isp.clk,
		.enable         = exynos5_clk_ip_isp0_ctrl,
		.ctrlbit        = ((0x3FF << 22) | (0x3 << 6) | (0xF << 0)),
	}, {
		.name           = "isp0_400",
		.devname        = "exynos5-fimc-is",
		.parent         = &exynos5_clk_aclk_400_isp.clk,
		.enable         = exynos5_clk_ip_isp0_ctrl,
		.ctrlbit        = (0x1 << 5),
	}, {
		.name           = "isp0_266",
		.devname        = "exynos5-fimc-is",
		.parent         = &exynos5_clk_aclk_266_isp.clk,
		.enable		= exynos5_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "isp1_333_432",
		.devname	= "exynos5-fimc-is",
		.parent		= &exynos5_clk_isp_div1.clk,
		.enable		= exynos5_clk_ip_isp1_ctrl,
		.ctrlbit	= (0x3 << 12),
	}, {
		.name		= "isp1_266",
		.devname	= "exynos5-fimc-is",
		.parent         = &exynos5_clk_aclk_266_isp.clk,
		.enable		= exynos5_clk_ip_isp1_ctrl,
		.ctrlbit	= (7 << 0),
	}, {
		.name		= "gscl_flite0",
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.parent		= &exynos5_clk_aclk_333_432_gscl.clk,
		.ctrlbit	= ((1 << 5) | (1 << 10) | (1 << 20) | (1 << 25)),
	}, {
		.name		= "gscl_flite1",
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.parent		= &exynos5_clk_aclk_333_432_gscl.clk,
		.ctrlbit	= ((1 << 6) | (1 << 11) | (1 << 26)),
	}, {
		.name		= "gscl_flite2",
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.parent		= &exynos5_clk_aclk_333_432_gscl.clk,
		.ctrlbit	= ((1 << 7) | (1 << 12) | (1 << 27)),
	}, {
		.name		= "gscl_wrap0",
		.devname	= "s5p-mipi-csis.0",
		.enable		= exynos5_clk_ip_gscl1_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "gscl_wrap1",
		.devname	= "s5p-mipi-csis.1",
		.enable		= exynos5_clk_ip_gscl1_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "gscl_wrap2",
		.devname	= "s5p-mipi-csis.2",
		.enable		= exynos5_clk_ip_gscl1_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "s3d",
		.devname	= "exynos5-s3d",
		.enable		= exynos5_clk_ip_gscl0_ctrl,
		.parent		= &exynos5_clk_aclk_266_gscl.clk,
		.ctrlbit	= ((1 << 8) | (1 << 13)),
	}, {
		.name		= "jpeg",
		.parent		= &exynos5_clk_aclk_166.clk,
		.enable		= exynos5_clk_ip_gen_ctrl,
		.ctrlbit	= ((1 << 2) | (1 << 12)),
	}, {
		.name		= "jpeg-hx",
		.parent		= &exynos5_clk_aclk_300_jpeg.clk,
		.enable		= exynos5_clk_ip_gen_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "hdmi",
		.devname	= "exynos5-hdmi",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_disp1_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "mixer",
		.devname	= "s5p-mixer",
		.enable		= exynos5_clk_ip_disp1_ctrl,
		.parent		= &exynos5_clk_aclk_200_disp1.clk,
		.ctrlbit	= ((1 << 5) | (1 << 13) | (1 << 14)),
	}, {
		.name           = "hdmiphy",
		.devname        = "exynos5-hdmi",
		.enable         = exynos5_clk_hdmiphy_ctrl,
		.ctrlbit        = (1 << 0),
	}, {
		.name           = "mie0",
		.enable         = exynos5_clk_ip_disp0_ctrl,
		.ctrlbit        = (1 << 1),
	}, {
		.name           = "mie1",
		.enable         = exynos5_clk_ip_disp1_ctrl,
		.ctrlbit        = (1 << 1),
	}, {
		.name           = "mdnie1",
		.enable         = exynos5_clk_ip_disp1_ctrl,
		.ctrlbit        = (1 << 2),
	}, {
		.name           = "fimd0",
		.devname        = "exynos5-fb.0",
		.enable         = exynos5_clk_ip_disp0_ctrl,
		.ctrlbit        = ((0x7 << 11) | (1 << 0)),
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(mfc_lr, 0),
		.enable         = &exynos5_clk_ip_mfc_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (3 << 1),
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(tv, 2),
		.enable         = &exynos5_clk_ip_disp1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (1 << 9)
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(jpeg, 3),
		.enable         = &exynos5_clk_ip_gen_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (1 << 7),
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(rot, 4),
		.enable         = &exynos5_clk_ip_gen_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (1 << 6)
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(gsc0, 5),
		.enable         = &exynos5_clk_ip_gscl1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (1 << 6),
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(gsc1, 6),
		.enable         = &exynos5_clk_ip_gscl1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (1 << 7),
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(gsc2, 7),
		.enable         = &exynos5_clk_ip_gscl1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (1 << 8),
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(gsc3, 8),
		.enable         = &exynos5_clk_ip_gscl1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (1 << 9),
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(scaler, 18),
		.enable         = &exynos5_clk_ip_gscl1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (1 << 10),
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(isp0, 9),
		.enable         = &exynos5_clk_ip_isp0_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (0x2F << 8),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(fimd0, 10),
		.enable		= &exynos5_clk_ip_disp0_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 10)
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
		.devname	= SYSMMU_CLOCK_DEVNAME(isp2, 17),
		.enable		= &exynos5_clk_ip_isp1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(isp3, 21),
		.enable		= &exynos5_clk_ip_gscl1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(mjpeg, 20),
		.enable		= &exynos5_clk_ip_gen_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(s3d, 19),
		.enable		= &exynos5_clk_ip_gscl1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(camif0, 12),
		.enable		= &exynos5_clk_ip_gscl1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(camif1, 13),
		.enable		= &exynos5_clk_ip_gscl1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(camif2, 14),
		.enable		= &exynos5_clk_ip_gscl1_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit	= (1 << 5),
	}, {
		.name           = SYSMMU_CLOCK_NAME,
		.devname        = SYSMMU_CLOCK_DEVNAME(2d, 15),
		.enable         = &exynos5_clk_ip_g2d_ctrl,
		.ops		= &exynos5_gate_clk_ops,
		.ctrlbit        = (1 << 7)
	}, {
		.name		= "fimg2d",
		.devname	= "s5p-fimg2d",
		.enable		= exynos5_clk_ip_g2d_ctrl,
		.ctrlbit	= ((1 << 3) | (1 << 10)),
	}, {
		.name           = "c2c",
		.enable         = exynos5_clk_ip_cdrex_ctrl,
		.ctrlbit        = (1 << 0),
	}, {
		.name           = "secss",
		.parent         = &exynos5_clk_aclk_acp.clk,
		.enable         = exynos5_clk_ip_g2d_ctrl,
		.ctrlbit        = (1 << 2),
	}, {
		.name           = "g2dsmmu",
		.enable         = exynos5_clk_ip_g2d_ctrl,
		.ctrlbit        = (7 << 5),
	}, {
		.name		= "ppmuacpx",
		.enable		= exynos5_clk_ip_g2d_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name           = "genppmuqe",
		.enable         = exynos5_clk_ip_gen_ctrl,
		.ctrlbit        = ((1 << 15) | (1 << 14) | (1 << 9)),
	}, {
		.name           = "axi_disp1",
		.enable         = exynos5_clk_bus_disp1_ctrl,
		.parent		= &exynos5_clk_aclk_300_disp1.clk,
		.ctrlbit        = (1 << 4),
	}, {
		.name           = "busfsysout",
		.enable         = exynos5_clk_bus_fsys0_ctrl,
		.ctrlbit        = ((1 << 31) | (1 << 30) | (1 << 29) | (1 << 10)),
	}, {
		.name           = "busgenout",
		.enable		= exynos5_clk_bus_gen_ctrl,
		.ctrlbit        = (1 << 14),
	}, {
		.name           = "fimd0ppmu",
		.enable		= exynos5_clk_ip_disp0_ctrl,
		.ctrlbit        = (1 << 9),
	}, {
		.name		= "sclk_mipi1",
		.enable		= exynos5_clksrc_mask_disp1_0_ctrl,
		.ctrlbit	= (1 << 12),
	}
};

static struct clk exynos5_i2cs_clocks[] = {
	{
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
		.devname	= "exynos5-hs-i2c.0",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 10)
	}, {
		.name		= "i2c",
		.devname	= "exynos5-hs-i2c.1",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 11)
	}, {
		.name		= "i2c",
		.devname	= "exynos5-hs-i2c.2",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 12)
	}, {
		.name		= "i2c",
		.devname	= "exynos5-hs-i2c.3",
		.parent		= &exynos5_clk_aclk_66.clk,
		.enable		= exynos5_clk_ip_peric_ctrl,
		.ctrlbit	= (1 << 13)
	}, {
		.name           = "i2c",
		.devname        = "s3c2440-hdmiphy-i2c",
		.parent         = &exynos5_clk_aclk_66.clk,
		.enable         = exynos5_clk_ip_peric_ctrl,
		.ctrlbit        = (1 << 14)
	}
};

static struct clksrc_clk exynos5_clk_sclk_mmc0 = {
	.clk		= {
		.name		= "sclk_mmc0",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS1, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_mmc1 = {
	.clk		= {
		.name		= "sclk_mmc1",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 4, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS1, .shift = 16, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_mmc2 = {
	.clk		= {
		.name		= "sclk_mmc2",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_FSYS, .shift = 8, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS2, .shift = 0, .size = 4 },
};

static struct clk exynos5_clk_pdma0 = {
	.name		= "dma",
	.devname	= "dma-pl330.0",
	.enable		= exynos5_clk_bus_fsys0_ctrl,
	.ctrlbit	= (1 << 1),
};

static struct clk exynos5_clk_pdma1 = {
	.name		= "dma",
	.devname	= "dma-pl330.1",
	.enable		= exynos5_clk_bus_fsys0_ctrl,
	.ctrlbit	= (1 << 2),
};

static struct clk exynos5_clk_mdma = {
	.name		= "dma",
	.devname	= "dma-pl330.2",
	.enable		= exynos5_clk_ip_g2d_ctrl,
	.ctrlbit	= (1 << 1),
};

static struct clk exynos5_clk_mdma1 = {
	.name		= "dma",
	.devname	= "dma-pl330.3",
	.parent		= &exynos5_clk_aclk_266.clk,
	.enable		= exynos5_clk_ip_gen_ctrl,
	.ctrlbit	= (1 << 4),
};

#if defined(CONFIG_S5P_DEV_FIMD0)
static struct clk exynos5_clk_fimd0 = {
	.name           = "fimd0",
	.devname        = "exynos5-fb.0",
	.enable         = exynos5_clk_ip_disp0_ctrl,
	.ctrlbit        = ((0x7 << 11) | (1 << 0)),
};
#endif

static struct clk exynos5_clk_fimd1 = {
	.name		= "fimd",
	.devname	= "exynos5-fb.1",
	.parent		= &exynos5_clk_aclk_300_disp1.clk,
	.enable		= exynos5_clk_ip_disp1_ctrl,
	.ctrlbit	= ((0x7 << 10) | (1 << 0)),
};

static struct clksrc_clk exynos5_clk_dout_spi0 = {
	.clk            = {
		.name           = "dout_spi0",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC1, .shift = 16, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC1, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_spi1 = {
	.clk            = {
		.name           = "dout_spi1",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC1, .shift = 20, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC1, .shift = 16, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_spi2 = {
	.clk            = {
		.name           = "dout_spi2",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC1, .shift = 24, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC2, .shift = 0, .size = 4 },
};

static struct clk *clkset_sclk_disp10_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &clk_ext_xtal_mux,
	[2] = &exynos5_clk_sclk_hdmi27m,
	[3] = &exynos5_clk_sclk_dptxphy,
	[4] = &exynos5_clk_sclk_usbphy,
	[5] = &exynos5_clk_sclk_hdmiphy,
	[6] = &exynos5_clk_mout_mpll.clk,
	[7] = &exynos5_clk_mout_epll.clk,
	[8] = &exynos5_clk_mout_vpll.clk,
	[9] = &exynos5_clk_mout_cpll.clk,
};

static struct clksrc_sources exynos5_clkset_sclk_disp10 = {
	.sources	= clkset_sclk_disp10_list,
	.nr_sources	= ARRAY_SIZE(clkset_sclk_disp10_list),
};

static struct clksrc_clk exynos5_clk_dout_mdnie = {
	.clk		= {
		.name		= "dout_mdnie",
	},
	.sources = &exynos5_clkset_sclk_disp10,
	.reg_src = { .reg = EXYNOS5_CLKSRC_DISP1_0, .shift = 4, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 4, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_mdnie = {
	.clk	= {
		.name		= "sclk_mdnie",
		.parent		= &exynos5_clk_dout_mdnie.clk,
		.enable		= exynos5_clksrc_mask_disp1_0_ctrl,
		.ctrlbit	= (1 << 4),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 4, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_mdnie_pwm = {
	.clk		= {
		.name		= "dout_mdnie_pwm",
	},
	.sources = &exynos5_clkset_sclk_disp10,
	.reg_src = { .reg = EXYNOS5_CLKSRC_DISP1_0, .shift = 8, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 8, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_mdnie_pwm = {
	.clk	= {
		.name		= "sclk_mdnie_pwm",
		.parent		= &exynos5_clk_dout_mdnie_pwm.clk,
		.enable		= exynos5_clksrc_mask_disp1_0_ctrl,
		.ctrlbit	= (1 << 8),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 12, .size = 4 },
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
	.clk    = {
		.name           = "sclk_spi",
		.devname        = "s3c64xx-spi.1",
		.parent         = &exynos5_clk_dout_spi1.clk,
		.enable         = exynos5_clksrc_mask_peric1_ctrl,
		.ctrlbit        = (1 << 20),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC1, .shift = 24, .size = 8 },
};

static struct clksrc_clk exynos5_clk_sclk_spi2 = {
	.clk    = {
		.name           = "sclk_spi",
		.devname        = "s3c64xx-spi.2",
		.parent         = &exynos5_clk_dout_spi2.clk,
		.enable         = exynos5_clksrc_mask_peric1_ctrl,
		.ctrlbit        = (1 << 24),
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC2, .shift = 8, .size = 8 },
};

/*PWI CLOCK*/
static struct clksrc_clk exynos5_clk_sclk_pwi = {
	.clk		= {
		.name		= "sclk_pwi",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_src_div_ctrl,
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_CPERI0, .shift = 16, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_CPERI1, .shift = 8, .size = 4 },
};

/* ISP_BLK */
/* PWM_ISP */
static struct clksrc_clk exynos5_clk_sclk_pwm_isp = {
	.clk            = {
		.name           = "sclk_pwm_isp",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_SCLK_SRC_ISP, .shift = 12, .size = 4 },
	.reg_div = { .reg = EXYNOS5_SCLK_DIV_ISP, .shift = 28, .size = 4 },
};

/* UART_ISP */
static struct clksrc_clk exynos5_clk_sclk_uart_isp = {
	.clk            = {
		.name           = "sclk_uart_isp",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_SCLK_SRC_ISP, .shift = 8, .size = 4 },
	.reg_div = { .reg = EXYNOS5_SCLK_DIV_ISP, .shift = 24, .size = 4 },
};

/* SPI1_ISP */
static struct clksrc_clk exynos5_clk_sclk_spi1_isp = {
	.clk            = {
		.name           = "sclk_spi1_isp",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_SCLK_SRC_ISP, .shift = 4, .size = 4 },
	.reg_div = { .reg = EXYNOS5_SCLK_DIV_ISP, .shift = 12, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_spi1_isp_pre = {
	.clk            = {
		.name           = "sclk_spi1_isp_pre",
		.parent         = &exynos5_clk_sclk_spi1_isp.clk,
	},
	.reg_div = { .reg = EXYNOS5_SCLK_DIV_ISP, .shift = 16, .size = 8 },
};

/* SPI0_ISP */
static struct clksrc_clk exynos5_clk_sclk_spi0_isp = {
	.clk            = {
		.name           = "sclk_spi0_isp",
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_SCLK_SRC_ISP, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS5_SCLK_DIV_ISP, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_spi0_isp_pre = {
	.clk            = {
		.name           = "sclk_spi0_isp_pre",
		.parent         = &exynos5_clk_sclk_spi0_isp.clk,
	},
	.reg_div = { .reg = EXYNOS5_SCLK_DIV_ISP, .shift = 4, .size = 8 },
};

/* Mux output for ISP_SENSOR */
static struct clk *exynos5_clkset_mout_isp_sensor_list[] = {
	[0] = &exynos5_clk_mout_ipll.clk,
	[1] = &exynos5_clk_mout_vpll.clk,
};

static struct clksrc_sources exynos5_clkset_mout_isp_sensor = {
	.sources        = exynos5_clkset_mout_isp_sensor_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_mout_isp_sensor_list),
};

static struct clksrc_clk exynos5_clk_mout_isp_sensor = {
	.clk    = {
		.name           = "sclk_mout_isp_sensor",
	},
	.sources = &exynos5_clkset_mout_isp_sensor,
	.reg_src = { .reg = EXYNOS5_SCLK_SRC_ISP, .shift = 20, .size = 1 },
};

/* SCLK_ISP_SENSOR0 */
static struct clksrc_clk exynos5_clk_sclk_isp_sensor0 = {
	.clk            = {
		.name           = "sclk_isp_sensor0",
		.parent         = &exynos5_clk_mout_isp_sensor.clk,
	},
	.reg_div = { .reg = EXYNOS5_SCLK_DIV_ISP1, .shift = 0, .size = 8 },
};

/* SCLK_ISP_SENSOR1 */
static struct clksrc_clk exynos5_clk_sclk_isp_sensor1 = {
	.clk            = {
		.name           = "sclk_isp_sensor1",
		.parent         = &exynos5_clk_mout_isp_sensor.clk,
	},
	.reg_div = { .reg = EXYNOS5_SCLK_DIV_ISP1, .shift = 8, .size = 8 },
};

/* SCLK_ISP_SENSOR2 */
static struct clksrc_clk exynos5_clk_sclk_isp_sensor2 = {
	.clk            = {
		.name           = "sclk_isp_sensor2",
		.parent         = &exynos5_clk_mout_isp_sensor.clk,
	},
	.reg_div = { .reg = EXYNOS5_SCLK_DIV_ISP1, .shift = 16, .size = 8 },
};

static struct clksrc_clk exynos5_clk_sclk_fimd = {
	.clk	= {
		.name		= "sclk_fimd",
		.devname	= "exynos5-fb.0",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_src_div_ctrl,
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_DISP0_0, .shift = 0, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP0_0, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_mdnie1 = {
	.clk	= {
		.name		= "sclk_mdnie1",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_src_div_ctrl,
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_DISP1_0, .shift = 4, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 4, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_mdnie_pwm1 = {
	.clk	= {
		.name		= "sclk_mdnie_pwm1",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_src_div_ctrl,
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_DISP1_0, .shift = 8, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 8, .size = 4 },
};

static struct clksrc_clk exynos5_clk_dout_mdnie_pwm1 = {
	.clk	= {
		.name		= "dout_mdnie_pwm1",
		.parent		= &exynos5_clk_sclk_mdnie_pwm1.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 12, .size = 4 },
};

static struct clksrc_clk exynos5_clk_sclk_ext_mst_vid = {
	.clk	= {
		.name		= "sclk_ext_mst_vid",
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_src_div_ctrl,
	},
	.sources = &exynos5_clkset_group,
	.reg_src = { .reg = EXYNOS5_CLKSRC_DISP1_0, .shift = 16, .size = 4 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_DISP1_0, .shift = 24, .size = 4 },
};

static struct clksrc_clk exynos5_clksrcs[] = {
	{
		.clk	= {
			.name		= "uclk1",
			.devname	= "s5pv210-uart.0",
			.enable		= exynos5_clksrc_mask_peric0_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC0, .shift = 0, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC0, .shift = 0, .size = 4 },
	}, {
		.clk	= {
			.name		= "uclk1",
			.devname	= "s5pv210-uart.1",
			.enable		= exynos5_clksrc_mask_peric0_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC0, .shift = 4, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC0, .shift = 4, .size = 4 },
	}, {
		.clk    = {
			.name           = "uclk1",
			.devname        = "s5pv210-uart.2",
			.enable         = exynos5_clksrc_mask_peric0_ctrl,
			.ctrlbit        = (1 << 8),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC0, .shift = 8, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC0, .shift = 8, .size = 4 },
	}, {
		.clk    = {
			.name           = "uclk1",
			.devname        = "s5pv210-uart.3",
			.enable         = exynos5_clksrc_mask_peric0_ctrl,
			.ctrlbit        = (1 << 12),
		},
		.sources = &exynos5_clkset_group,
		.reg_src = { .reg = EXYNOS5_CLKSRC_PERIC0, .shift = 12, .size = 4 },
		.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC0, .shift = 12, .size = 4 },
	}, {
		.clk    = {
			.name           = "sclk_dwmci",
			.devname        = "dw_mmc.0",
			.parent         = &exynos5_clk_sclk_mmc0.clk,
			.enable         = exynos5_clksrc_mask_fsys_ctrl,
			.ctrlbit        = (1 << 0),
		},
		.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS1, .shift = 8, .size = 8 },
	}, {
		.clk    = {
			.name           = "sclk_dwmci",
			.devname        = "dw_mmc.1",
			.parent         = &exynos5_clk_sclk_mmc1.clk,
			.enable         = exynos5_clksrc_mask_fsys_ctrl,
			.ctrlbit        = (1 << 4),
		},
		.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS1, .shift = 24, .size = 8 },
	}, {
		.clk    = {
			.name           = "sclk_dwmci",
			.devname        = "dw_mmc.2",
			.parent         = &exynos5_clk_sclk_mmc2.clk,
			.enable         = exynos5_clksrc_mask_fsys_ctrl,
			.ctrlbit        = (1 << 8),
		},
		.reg_div = { .reg = EXYNOS5_CLKDIV_FSYS2, .shift = 8, .size = 8 },
	}, {
		.clk    = {
			.name           = "sclk_pcm",
			.parent         = &exynos5_clk_sclk_audio0.clk,
		},
			.reg_div = { .reg = EXYNOS5_CLKDIV_MAUDIO, .shift = 4, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_pcm0",
			.parent		= &exynos5_clk_sclk_audio0.clk,
		},
			.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC4, .shift = 20, .size = 8 },
	}, {
		.clk	= {
			.name		= "sclk_pcm1",
			.parent		= &exynos5_clk_sclk_audio1.clk,
		},
			.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC4, .shift = 4, .size = 8 },
	}, {
		.clk    = {
			.name           = "sclk_pcm2",
			.parent         = &exynos5_clk_sclk_audio2.clk,
		},
			.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC4, .shift = 12, .size = 8 },
	}, {
		.clk    = {
			.name           = "sclk_i2s",
			.parent         = &exynos5_clk_sclk_audio1.clk,
		},
			.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC5, .shift = 0, .size = 6 },
	}, {
		.clk    = {
			.name           = "sclk_i2s",
			.parent         = &exynos5_clk_sclk_audio2.clk,
		},
			.reg_div = { .reg = EXYNOS5_CLKDIV_PERIC5, .shift = 8, .size = 6 },
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
	},
};

static struct pll_div_data exynos5_cpll_div[] = {
	{666000000, 4, 222, 1, 0,  0, 0},
	{640000000, 3, 160, 1, 0,  0, 0},
	{320000000, 3, 160, 2, 0,  0, 0},
};

static unsigned long exynos5_cpll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int exynos5_cpll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int cpll_con0;
	unsigned int locktime;
	unsigned int tmp;
	unsigned int i;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	cpll_con0 = __raw_readl(EXYNOS5_CPLL_CON0);
	cpll_con0 &= ~(PLL35XX_MDIV_MASK << PLL35XX_MDIV_SHIFT |\
			PLL35XX_PDIV_MASK << PLL35XX_PDIV_SHIFT |\
			PLL35XX_SDIV_MASK << PLL35XX_SDIV_SHIFT);

	for (i = 0; i < ARRAY_SIZE(exynos5_cpll_div); i++) {
		if (exynos5_cpll_div[i].rate == rate) {
			cpll_con0 |= exynos5_cpll_div[i].pdiv << PLL35XX_PDIV_SHIFT;
			cpll_con0 |= exynos5_cpll_div[i].mdiv << PLL35XX_MDIV_SHIFT;
			cpll_con0 |= exynos5_cpll_div[i].sdiv << PLL35XX_SDIV_SHIFT;
			cpll_con0 |= 1 << EXYNOS5_PLL_ENABLE_SHIFT;
			break;
		}
	}

	if (i == ARRAY_SIZE(exynos5_cpll_div)) {
		printk(KERN_ERR "%s: Invalid Clock CPLL Frequency\n", __func__);
		return -EINVAL;
	}

	/* 1500 max_cycls : specification data */
	locktime = 200 * exynos5_cpll_div[i].pdiv;

	__raw_writel(locktime, EXYNOS5_CPLL_LOCK);
	__raw_writel(cpll_con0, EXYNOS5_CPLL_CON0);

	do {
		tmp = __raw_readl(EXYNOS5_CPLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS5_PLLCON0_LOCKED_SHIFT)));

	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5_cpll_ops = {
	.get_rate = exynos5_cpll_get_rate,
	.set_rate = exynos5_cpll_set_rate,
};

static struct pll_div_data exynos5_dpll_div[] = {
	{600000000, 4, 200, 1, 0,  0, 0},
};

static unsigned long exynos5_dpll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int exynos5_dpll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int dpll_con0;
	unsigned int locktime;
	unsigned int tmp;
	unsigned int i;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	dpll_con0 = __raw_readl(EXYNOS5_DPLL_CON0);
	dpll_con0 &= ~(PLL35XX_MDIV_MASK << PLL35XX_MDIV_SHIFT |\
			PLL35XX_PDIV_MASK << PLL35XX_PDIV_SHIFT |\
			PLL35XX_SDIV_MASK << PLL35XX_SDIV_SHIFT);

	for (i = 0; i < ARRAY_SIZE(exynos5_dpll_div); i++) {
		if (exynos5_dpll_div[i].rate == rate) {
			dpll_con0 |= exynos5_dpll_div[i].pdiv << PLL35XX_PDIV_SHIFT;
			dpll_con0 |= exynos5_dpll_div[i].mdiv << PLL35XX_MDIV_SHIFT;
			dpll_con0 |= exynos5_dpll_div[i].sdiv << PLL35XX_SDIV_SHIFT;
			dpll_con0 |= 1 << EXYNOS5_PLL_ENABLE_SHIFT;
			break;
		}
	}

	if (i == ARRAY_SIZE(exynos5_dpll_div)) {
		printk(KERN_ERR "%s: Invalid Clock DPLL Frequency\n", __func__);
		return -EINVAL;
	}
	/* 1500 max_cycls : specification data */
	locktime = 200 * exynos5_dpll_div[i].pdiv;

	__raw_writel(locktime, EXYNOS5_DPLL_LOCK);
	__raw_writel(dpll_con0, EXYNOS5_DPLL_CON0);

	do {
		tmp = __raw_readl(EXYNOS5_DPLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS5_PLLCON0_LOCKED_SHIFT)));

	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5_dpll_ops = {
	.get_rate = exynos5_dpll_get_rate,
	.set_rate = exynos5_dpll_set_rate,
};

static struct pll_div_data exynos5_ipll_div[] = {
	{432000000, 4, 288, 2, 0,  0, 0},
	{666000000, 4, 222, 1, 0,  0, 0},
	{864000000, 4, 288, 1, 0,  0, 0},
};

static unsigned long exynos5_ipll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int exynos5_ipll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int ipll_con0;
	unsigned int locktime;
	unsigned int tmp;
	unsigned int i;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	ipll_con0 = __raw_readl(EXYNOS5_IPLL_CON0);
	ipll_con0 &= ~(PLL35XX_MDIV_MASK << PLL35XX_MDIV_SHIFT |\
			PLL35XX_PDIV_MASK << PLL35XX_PDIV_SHIFT |\
			PLL35XX_SDIV_MASK << PLL35XX_SDIV_SHIFT);

	for (i = 0; i < ARRAY_SIZE(exynos5_ipll_div); i++) {
		if (exynos5_ipll_div[i].rate == rate) {
			ipll_con0 |= exynos5_ipll_div[i].pdiv << PLL35XX_PDIV_SHIFT;
			ipll_con0 |= exynos5_ipll_div[i].mdiv << PLL35XX_MDIV_SHIFT;
			ipll_con0 |= exynos5_ipll_div[i].sdiv << PLL35XX_SDIV_SHIFT;
			ipll_con0 |= 1 << EXYNOS5_PLL_ENABLE_SHIFT;
			break;
		}
	}

	if (i == ARRAY_SIZE(exynos5_ipll_div)) {
		printk(KERN_ERR "%s: Invalid Clock IPLL Frequency\n", __func__);
		return -EINVAL;
	}

	/* 1500 max_cycls : specification data */
	locktime = 200 * exynos5_ipll_div[i].pdiv;

	__raw_writel(locktime, EXYNOS5_IPLL_LOCK);
	__raw_writel(ipll_con0, EXYNOS5_IPLL_CON0);

	do {
		tmp = __raw_readl(EXYNOS5_IPLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS5_PLLCON0_LOCKED_SHIFT)));

	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5_ipll_ops = {
	.get_rate = exynos5_ipll_get_rate,
	.set_rate = exynos5_ipll_set_rate,
};

static struct vpll_div_data exynos5_epll_div[] = {
	{ 45158400, 3, 181, 5, 24012, 0, 0, 0},
	{ 49152000, 3, 197, 5, 25690, 0, 0, 0},
	{ 67737600, 5, 452, 5, 27263, 0, 0, 0},
	{180633600, 5, 301, 3,  3670, 0, 0, 0},
	{200000000, 3, 200, 3,     0, 0, 0, 0},
	{400000000, 3, 200, 2,     0, 0, 0, 0},
	{600000000, 2, 100, 1,     0, 0, 0, 0},
};

static unsigned long exynos5_epll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int exynos5_epll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int epll_con0, epll_con1;
	unsigned int locktime;
	unsigned int tmp;
	unsigned int i;
	unsigned int k;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	epll_con0 = __raw_readl(EXYNOS5_EPLL_CON0);
	epll_con0 &= ~(PLL2650_MDIV_MASK << PLL2650_MDIV_SHIFT |\
			PLL2650_PDIV_MASK << PLL2650_PDIV_SHIFT |\
			PLL2650_SDIV_MASK << PLL2650_SDIV_SHIFT);

	epll_con1 = __raw_readl(EXYNOS5_EPLL_CON1);
	epll_con1 &= ~(0xffff << 0);

	for (i = 0; i < ARRAY_SIZE(exynos5_epll_div); i++) {
		if (exynos5_epll_div[i].rate == rate) {
			k = (~(exynos5_epll_div[i].k) + 1) & EXYNOS5_EPLLCON1_K_MASK;
			epll_con1 |= k << 0;
			epll_con0 |= exynos5_epll_div[i].pdiv << PLL2650_PDIV_SHIFT;
			epll_con0 |= exynos5_epll_div[i].mdiv << PLL2650_MDIV_SHIFT;
			epll_con0 |= exynos5_epll_div[i].sdiv << PLL2650_SDIV_SHIFT;
			epll_con0 |= 1 << EXYNOS5_PLL_ENABLE_SHIFT;
			break;
		}
	}

	if (i == ARRAY_SIZE(exynos5_epll_div)) {
		printk(KERN_ERR "%s: Invalid Clock VPLL Frequency\n", __func__);
		return -EINVAL;
	}

	/* 1500 max_cycls : specification data */
	locktime = 3000 * exynos5_epll_div[i].pdiv;

	__raw_writel(locktime, EXYNOS5_EPLL_LOCK);
	__raw_writel(epll_con0, EXYNOS5_EPLL_CON0);
	__raw_writel(epll_con1, EXYNOS5_EPLL_CON1);

	do {
		tmp = __raw_readl(EXYNOS5_EPLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS5_PLLCON0_LOCKED_SHIFT)));

	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5_epll_ops = {
	.get_rate = exynos5_epll_get_rate,
	.set_rate = exynos5_epll_set_rate,
};

static struct vpll_div_data exynos5_vpll_div[] = {
	{123500000, 4, 330, 4, 0, 0,  0, 0},
	{ 89000000, 3, 178, 4, 0, 0,  0, 0},
	{177000000, 2, 118, 3, 0, 0,  0, 0},
	{266000000, 3, 133, 2, 0, 0,  0, 0},
	{333000000, 2, 111, 2, 0, 0,  0, 0},
	{350000000, 3, 175, 2, 0, 0,  0, 0},
	{440000000, 3, 220, 2, 0, 0,  0, 0},
	{480000000, 3, 240, 2, 0, 0,  0, 0},
	{532000000, 3, 133, 1, 0, 0,  0, 0},
	{640000000, 3, 160, 1, 0, 0,  0, 0},
	{880000000, 3, 220, 1, 0, 0,  0, 0},
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
	unsigned int k;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	/* Change MUX_VPLL_SEL 0: FINPLL */
	tmp = __raw_readl(EXYNOS5_CLKSRC_TOP2);
	tmp &= ~(1 << 16);
	__raw_writel(tmp, EXYNOS5_CLKSRC_TOP2);

	vpll_con0 = __raw_readl(EXYNOS5_VPLL_CON0);
	vpll_con0 &= ~(PLL2650_MDIV_MASK << PLL2650_MDIV_SHIFT |\
			PLL2650_PDIV_MASK << PLL2650_PDIV_SHIFT |\
			PLL2650_SDIV_MASK << PLL2650_SDIV_SHIFT);

	vpll_con1 = __raw_readl(EXYNOS5_VPLL_CON1);
	vpll_con1 &= ~(0xffff << 0);

	for (i = 0; i < ARRAY_SIZE(exynos5_vpll_div); i++) {
		if (exynos5_vpll_div[i].rate == rate) {
			k = (~(exynos5_vpll_div[i].k) + 1) & EXYNOS5_VPLLCON1_K_MASK;
			vpll_con1 |= k << 0;
			vpll_con0 |= exynos5_vpll_div[i].pdiv << PLL2650_PDIV_SHIFT;
			vpll_con0 |= exynos5_vpll_div[i].mdiv << PLL2650_MDIV_SHIFT;
			vpll_con0 |= exynos5_vpll_div[i].sdiv << PLL2650_SDIV_SHIFT;
			vpll_con0 |= 1 << EXYNOS5_PLL_ENABLE_SHIFT;
			break;
		}
	}

	if (i == ARRAY_SIZE(exynos5_vpll_div)) {
		printk(KERN_ERR "%s: Invalid Clock VPLL Frequency\n", __func__);
		return -EINVAL;
	}

	/* 3000 max_cycls : specification data */
	locktime = 3000 * exynos5_vpll_div[i].pdiv;

	__raw_writel(locktime, EXYNOS5_VPLL_LOCK);
	__raw_writel(vpll_con0, EXYNOS5_VPLL_CON0);
	__raw_writel(vpll_con1, EXYNOS5_VPLL_CON1);

	do {
		tmp = __raw_readl(EXYNOS5_VPLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS5_VPLLCON0_LOCKED_SHIFT)));

	clk->rate = rate;

	/* Change MUX_VPLL_SEL 1: FOUTVPLL */
	tmp = __raw_readl(EXYNOS5_CLKSRC_TOP2);
	tmp |= (1 << 16);
	__raw_writel(tmp, EXYNOS5_CLKSRC_TOP2);

	return 0;
}

static struct clk_ops exynos5_vpll_ops = {
	.get_rate = exynos5_vpll_get_rate,
	.set_rate = exynos5_vpll_set_rate,
};

static unsigned long xtal_rate;

static int exynos5410_fout_apll_set_rate(struct clk *clk, unsigned long rate)
{
	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5410_fout_apll_ops = {
	.set_rate = exynos5410_fout_apll_set_rate
};

static int exynos5410_fout_kpll_set_rate(struct clk *clk, unsigned long rate)
{
	clk->rate = rate;

	return 0;
}

static struct clk_ops exynos5410_fout_kpll_ops = {
	.set_rate = exynos5410_fout_kpll_set_rate
};

static struct clk *exynos5_clkset_c2c_list[] = {
	[0] = &exynos5_clk_mout_mpll.clk,
	[1] = &exynos5_clk_mout_bpll.clk,
};

static struct clksrc_sources exynos5_clkset_sclk_c2c = {
	.sources        = exynos5_clkset_c2c_list,
	.nr_sources     = ARRAY_SIZE(exynos5_clkset_c2c_list),
};

static struct clksrc_clk exynos5_clk_sclk_c2c = {
	.clk	= {
		.name		= "sclk_c2c",
		.id		= -1,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.sources = &exynos5_clkset_sclk_c2c,
	.reg_src = { .reg = EXYNOS5_CLKSRC_CDREX, .shift = 12, .size = 1 },
	.reg_div = { .reg = EXYNOS5_CLKDIV_CDREX2, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos5_clk_aclk_c2c = {
	.clk	= {
		.name		= "aclk_c2c",
		.id		= -1,
		.parent		= &exynos5_clk_sclk_c2c.clk,
		.init		= exynos5_clksrc_init,
		.enable		= exynos5_clk_div_ctrl,
	},
	.reg_div = { .reg = EXYNOS5_CLKDIV_CDREX2, .shift = 4, .size = 3 },
};

/* Clock initialization code */
static struct clksrc_clk *exynos5_sysclks[] = {
	&exynos5_clk_mout_apll,
	&exynos5_clk_mout_mpll,
	&exynos5_clk_mout_epll,
	&exynos5_clk_mout_bpll,
	&exynos5_clk_mout_cpll,
	&exynos5_clk_mout_kpll,
	&exynos5_clk_mout_ipll,
	&exynos5_clk_dout_aclk_333_432_isp,
	&exynos5_clk_aclk_333_432_isp,
	&exynos5_clk_isp_div0,
	&exynos5_clk_isp_div1,
	&exynos5_clk_mpwm_div,
	&exynos5_clk_dout_aclk_333_432_gscl,
	&exynos5_clk_aclk_333_432_gscl,
	&exynos5_clk_pclk_166_gscl,
	&exynos5_clk_mout_vpllsrc,
	&exynos5_clk_mout_vpll,
	&exynos5_clk_mout_mpll_user,
	&exynos5_clk_mout_bpll_user,
	&exynos5_clk_mout_mpll_bpll,
	&exynos5_clk_mout_dpll,
	&exynos5_clk_aclk_400,
	&exynos5_clk_aclk_400_isp_pre,
	&exynos5_clk_aclk_400_isp,
	&exynos5_clk_mcuisp_div0,
	&exynos5_clk_mcuisp_div1,
	&exynos5_clk_aclk_266,
	&exynos5_clk_aclk_266_isp,
	&exynos5_clk_aclk_200,
	&exynos5_clk_aclk_200_disp1,
	&exynos5_clk_aclk_166,
	&exynos5_clk_dout_aclk_66_pre,
	&exynos5_clk_aclk_66,
	&exynos5_clk_aclk_acp,
	&exynos5_clk_pclk_acp,
	&exynos5_clk_mout_cpu,
	&exynos5_clk_mout_cpu_kfc,
	&exynos5_clk_dout_arm,
	&exynos5_clk_dout_arm2,
	&exynos5_clk_dout_acp,
	&exynos5_clk_dout_kfc,
	&exynos5_clk_dout_sclk_cdrex,
	&exynos5_clk_dout_clk2x_phy,
	&exynos5_clk_dout_cclk_cdrex,
	&exynos5_clk_dout_pclk_cdrex,
	&exynos5_clk_sclk_mmc0,
	&exynos5_clk_sclk_mmc1,
	&exynos5_clk_sclk_mmc2,
	&exynos5_clk_sclk_pwm_isp,
	&exynos5_clk_sclk_uart_isp,
	&exynos5_clk_sclk_spi1_isp,
	&exynos5_clk_sclk_spi1_isp_pre,
	&exynos5_clk_sclk_spi0_isp,
	&exynos5_clk_sclk_spi0_isp_pre,
	&exynos5_clk_mout_isp_sensor,
	&exynos5_clk_sclk_isp_sensor0,
	&exynos5_clk_sclk_isp_sensor1,
	&exynos5_clk_sclk_isp_sensor2,
	&exynos5_clk_sclk_mau_audio0,
	&exynos5_clk_mout_g3d,
	&exynos5_clk_sclk_g3d_core,
	&exynos5_clk_sclk_g3d_hydra,
	&exynos5_clk_sclk_g3d_core_sub,
	&exynos5_clk_sclk_g3d_hydra_sub,
	&exynos5_clk_clkout,
	&exynos5_clk_dout_aclk_300_disp1,
	&exynos5_clk_aclk_300_disp1,
};

static struct clksrc_clk *exynos5_sysclks_off[] = {
	&exynos5_clk_aclk_333_pre,
	&exynos5_clk_aclk_333,
	&exynos5_clk_aclk_200_disp0,
	&exynos5_clk_dout_aclk_300_gscl,
	&exynos5_clk_aclk_300_gscl,
	&exynos5_clk_dout_aclk_300_disp0,
	&exynos5_clk_dout_aclk_300_jpeg,
	&exynos5_clk_aclk_300_jpeg,
	&exynos5_clk_aclk_c2c,
	&exynos5_clk_sclk_c2c,
	&exynos5_clk_sclk_pwi,
	&exynos5_clk_sclk_jpeg,
	&exynos5_clk_sclk_fimd,
	&exynos5_clk_sclk_mdnie,
	&exynos5_clk_sclk_mdnie1,
	&exynos5_clk_sclk_mdnie_pwm,
	&exynos5_clk_dout_mdnie_pwm1,
	&exynos5_clk_sclk_ext_mst_vid,
	&exynos5_clk_mipihsi,
	&exynos5_clk_sclk_usbdrd300,
	&exynos5_clk_sclk_usbdrd301,
	&exynos5_dout_usbphy300,
	&exynos5_dout_usbphy301,
	&exynos5_clk_dout_spi0,
	&exynos5_clk_dout_spi1,
	&exynos5_clk_dout_spi2,
	&exynos5_clk_sclk_spi0,
	&exynos5_clk_sclk_spi1,
	&exynos5_clk_sclk_spi2,
	&exynos5_clk_sclk_audio0,
	&exynos5_clk_sclk_audio1,
	&exynos5_clk_sclk_audio2,
	&exynos5_clk_sclk_spdif,
};

static struct clk *exynos5_clks[] __initdata = {
	&exynos5_clk_sclk_hdmi27m,
	&exynos5_clk_sclk_hdmiphy,
	&exynos5_clk_armclk,
	&clk_fout_bpll,
	&clk_fout_cpll,
	&clk_fout_kpll,
	&exynos5_clk_kfcclk,
};

static struct clk *exynos5_clk_cdev[] = {
	&exynos5_clk_pdma0,
	&exynos5_clk_pdma1,
	&exynos5_clk_mdma,
	&exynos5_clk_mdma1,
#if defined(CONFIG_S5P_DEV_FIMD0)
	&exynos5_clk_fimd0,
#else
	&exynos5_clk_fimd1
#endif
};

static struct clk_lookup exynos5_clk_lookup[] = {
#if defined(CONFIG_S5P_DEV_FIMD0)
	CLKDEV_INIT("exynos5-fb.0", "lcd", &exynos5_clk_fimd0),
#else
	CLKDEV_INIT("exynos5-fb.1", "lcd", &exynos5_clk_fimd1),
#endif
	CLKDEV_INIT("exynos4210-uart.0", "clk_uart_baud0", &exynos5_clksrcs[0].clk),
	CLKDEV_INIT("exynos4210-uart.1", "clk_uart_baud0", &exynos5_clksrcs[1].clk),
	CLKDEV_INIT("exynos4210-uart.2", "clk_uart_baud0", &exynos5_clksrcs[2].clk),
	CLKDEV_INIT("exynos4210-uart.3", "clk_uart_baud0", &exynos5_clksrcs[3].clk),
	CLKDEV_INIT("dma-pl330.0", "apb_pclk", &exynos5_clk_pdma0),
	CLKDEV_INIT("dma-pl330.1", "apb_pclk", &exynos5_clk_pdma1),
	CLKDEV_INIT("dma-pl330.2", "apb_pclk", &exynos5_clk_mdma),
	CLKDEV_INIT("dma-pl330.3", "apb_pclk", &exynos5_clk_mdma1),
	CLKDEV_INIT("s3c64xx-spi.0", "spi_busclk0", &exynos5_clk_sclk_spi0.clk),
	CLKDEV_INIT("s3c64xx-spi.1", "spi_busclk0", &exynos5_clk_sclk_spi1.clk),
	CLKDEV_INIT("s3c64xx-spi.2", "spi_busclk0", &exynos5_clk_sclk_spi2.clk),
	CLKDEV_INIT("s3c64xx-spi.3", "spi_busclk0",
		&exynos5_clk_sclk_spi0_isp_pre.clk),
};

#ifdef CONFIG_PM
static int exynos5410_clock_suspend(void)
{
	s3c_pm_do_save(exynos5410_clock_save, ARRAY_SIZE(exynos5410_clock_save));
	s3c_pm_do_save(exynos5410_cpll_save, ARRAY_SIZE(exynos5410_cpll_save));
	s3c_pm_do_save(exynos5410_dpll_save, ARRAY_SIZE(exynos5410_dpll_save));
	s3c_pm_do_save(exynos5410_ipll_save, ARRAY_SIZE(exynos5410_ipll_save));
	s3c_pm_do_save(exynos5410_epll_save, ARRAY_SIZE(exynos5410_epll_save));
	s3c_pm_do_save(exynos5410_vpll_save, ARRAY_SIZE(exynos5410_vpll_save));

	return 0;
}

static void exynos5_pll_wait_locktime(void __iomem *con_reg, int shift_value)
{
	unsigned int tmp;

	do {
		tmp = __raw_readl(con_reg);
	} while (tmp >> EXYNOS5_PLL_ENABLE_SHIFT && !(tmp & (0x1 << EXYNOS5_PLLCON0_LOCKED_SHIFT)));
}

static void exynos5410_clock_resume(void)
{
	s3c_pm_do_restore_core(exynos5410_clock_save, ARRAY_SIZE(exynos5410_clock_save));

	s3c_pm_do_restore_core(exynos5410_cpll_save, ARRAY_SIZE(exynos5410_cpll_save));
	s3c_pm_do_restore_core(exynos5410_dpll_save, ARRAY_SIZE(exynos5410_dpll_save));
	s3c_pm_do_restore_core(exynos5410_ipll_save, ARRAY_SIZE(exynos5410_ipll_save));
	s3c_pm_do_restore_core(exynos5410_epll_save, ARRAY_SIZE(exynos5410_epll_save));
	s3c_pm_do_restore_core(exynos5410_vpll_save, ARRAY_SIZE(exynos5410_vpll_save));

	exynos5_pll_wait_locktime(EXYNOS5_CPLL_CON0, EXYNOS5_PLLCON0_LOCKED_SHIFT);
	exynos5_pll_wait_locktime(EXYNOS5_DPLL_CON0, EXYNOS5_PLLCON0_LOCKED_SHIFT);
	exynos5_pll_wait_locktime(EXYNOS5_IPLL_CON0, EXYNOS5_PLLCON0_LOCKED_SHIFT);
	exynos5_pll_wait_locktime(EXYNOS5_EPLL_CON0, EXYNOS5_PLLCON0_LOCKED_SHIFT);
	exynos5_pll_wait_locktime(EXYNOS5_VPLL_CON0, EXYNOS5_PLLCON0_LOCKED_SHIFT);

	clk_fout_apll.rate = s5p_get_pll35xx(xtal_rate, __raw_readl(EXYNOS5_APLL_CON0));
	clk_fout_kpll.rate = s5p_get_pll35xx(xtal_rate, __raw_readl(EXYNOS5_KPLL_CON0));
}
#else
#define exynos5410_clock_suspend NULL
#define exynos5410_clock_resume NULL
#endif

struct syscore_ops exynos5410_clock_syscore_ops = {
	.suspend	= exynos5410_clock_suspend,
	.resume		= exynos5410_clock_resume,
};

void __init_or_cpufreq exynos5410_setup_clocks(void)
{
	struct clk *xtal_clk;

	unsigned long xtal;
	unsigned long apll;
	unsigned long kpll;
	unsigned long mpll;
	unsigned long bpll;
	unsigned long cpll;
	unsigned long vpll;
	unsigned long vpllsrc;
	unsigned long ipll;
	unsigned long dpll;
	unsigned long epll;
	unsigned long eagle_clk;
	unsigned long kfc_clk;
	unsigned long mclk_cdrex;

	unsigned int i;

	printk(KERN_DEBUG "%s: registering clocks\n", __func__);

	xtal_clk = clk_get(NULL, "xtal");
	BUG_ON(IS_ERR(xtal_clk));

	xtal = clk_get_rate(xtal_clk);

	xtal_rate = xtal;

	clk_put(xtal_clk);

	printk(KERN_DEBUG "%s: xtal is %ld\n", __func__, xtal);

	clk_fout_apll.ops = &exynos5410_fout_apll_ops;
	clk_fout_cpll.ops = &exynos5_cpll_ops;
	clk_fout_dpll.ops = &exynos5_dpll_ops;
	clk_fout_epll.ops = &exynos5_epll_ops;
	clk_fout_kpll.ops = &exynos5410_fout_kpll_ops;
	clk_fout_vpll.ops = &exynos5_vpll_ops;
	clk_fout_ipll.ops = &exynos5_ipll_ops;

	exynos5410_pll_init();

	/* Set and check PLLs */
	apll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_APLL_CON0));
	kpll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_KPLL_CON0));
	mpll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_MPLL_CON0));
	bpll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_BPLL_CON0));
	cpll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_CPLL_CON0));
	vpllsrc = clk_get_rate(&exynos5_clk_mout_vpllsrc.clk);
	vpll = s5p_get_pll36xx(vpllsrc, __raw_readl(EXYNOS5_VPLL_CON0),
			__raw_readl(EXYNOS5_VPLL_CON1));
	ipll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_IPLL_CON0));
	dpll = s5p_get_pll35xx(xtal, __raw_readl(EXYNOS5_DPLL_CON0));
	epll = s5p_get_pll36xx(xtal, __raw_readl(EXYNOS5_EPLL_CON0),
			__raw_readl(EXYNOS5_EPLL_CON1));

	clk_fout_apll.rate = apll;
	clk_fout_bpll.rate = bpll;
	clk_fout_cpll.rate = cpll;
	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;
	clk_fout_vpll.rate = vpll;
	clk_fout_kpll.rate = kpll;
	clk_fout_ipll.rate = ipll;
	clk_fout_dpll.rate = dpll;

	eagle_clk = clk_get_rate(&exynos5_clk_armclk);
	kfc_clk = clk_get_rate(&exynos5_clk_kfcclk);
	mclk_cdrex = clk_get_rate(&exynos5_clk_dout_clk2x_phy.clk);

	printk(KERN_INFO "EXYNOS5: EAGLECLK=%ld, KFCCLK=%ld, CDREX=%ld\n",
			eagle_clk, kfc_clk, mclk_cdrex);

	for (i = 0; i < ARRAY_SIZE(exynos5_sysclks); i++)
		s3c_set_clksrc(exynos5_sysclks[i], true);

	for (i = 0; i < ARRAY_SIZE(exynos5_clksrcs); i++)
		s3c_set_clksrc(&exynos5_clksrcs[i], true);

}

static struct clk *exynos5_clks_off[] __initdata = {
	&clk_fout_epll,
	&clk_fout_ipll,
};

void __init exynos5410_register_clocks(void)
{
	int ptr;
	struct clksrc_clk *clksrc;

	clk_fout_apll.enable = exynos5_apll_ctrl;
	clk_fout_apll.ctrlbit = (1 << 22);

	clk_fout_ipll.enable = exynos5_ipll_ctrl;
	clk_fout_ipll.ctrlbit = (1 << 22);
	clk_fout_ipll.rate = 24000000;

	clk_fout_epll.enable = exynos5_epll_ctrl;
	clk_fout_epll.ctrlbit = (1 << 4);
	clk_fout_epll.rate = 24000000;

	s3c24xx_register_clocks(exynos5_clks_off, ARRAY_SIZE(exynos5_clks_off));
	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_clks_off); ptr++)
		s3c_disable_clocks(exynos5_clks_off[ptr], 1);

	s3c24xx_register_clocks(exynos5_clks, ARRAY_SIZE(exynos5_clks));

	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_sysclks); ptr++)
		s3c_register_clksrc(exynos5_sysclks[ptr], 1);

	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_sysclks_off); ptr++) {
		clksrc = exynos5_sysclks_off[ptr];
		s3c_register_clksrc(clksrc, 1);
		s3c_disable_clocks(&clksrc->clk, 1);
	}

	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_sclk_tv); ptr++) {
		clksrc = exynos5_sclk_tv[ptr];
		s3c_register_clksrc(exynos5_sclk_tv[ptr], 1);
		s3c_disable_clocks(&clksrc->clk, 1);
	}

	s3c_register_clksrc(exynos5_clksrcs, ARRAY_SIZE(exynos5_clksrcs));
	s3c_register_clocks(exynos5_init_clocks, ARRAY_SIZE(exynos5_init_clocks));

	s3c_register_clocks(exynos5_init_clocks_off, ARRAY_SIZE(exynos5_init_clocks_off));
	s3c_disable_clocks(exynos5_init_clocks_off, ARRAY_SIZE(exynos5_init_clocks_off));

	s3c_register_clocks(exynos5_i2cs_clocks, ARRAY_SIZE(exynos5_i2cs_clocks));
	s3c_disable_clocks(exynos5_i2cs_clocks, ARRAY_SIZE(exynos5_i2cs_clocks));

	s3c24xx_register_clocks(exynos5_clk_cdev, ARRAY_SIZE(exynos5_clk_cdev));
	for (ptr = 0; ptr < ARRAY_SIZE(exynos5_clk_cdev); ptr++)
		s3c_disable_clocks(exynos5_clk_cdev[ptr], 1);

	clkdev_add_table(exynos5_clk_lookup, ARRAY_SIZE(exynos5_clk_lookup));

	register_syscore_ops(&exynos5410_clock_syscore_ops);

	s3c_pwmclk_init();
}
