/* linux/arch/arm/mach-s5pv310/clock.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV310 - Clock support
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

static struct clk clk_sclk_hdmi27m = {
	.name		= "sclk_hdmi27m",
	.id		= -1,
	.rate		= 27000000,
};

static int s5pv310_clksrc_mask_peril0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKSRC_MASK_PERIL0, clk, enable);
}

static int s5pv310_clk_ip_peril_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP_PERIL, clk, enable);
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
	[0] = &clk_sclk_apll.clk,
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

static struct clksrc_clk clk_atclk = {
	.clk	= {
		.name		= "atclk",
		.id		= -1,
		.parent		= &clk_moutcore.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 16, .size = 3 },
};

static struct clksrc_clk clk_pclk_dbg = {
	.clk	= {
		.name		= "pclk_dbg",
		.id		= -1,
		.parent		= &clk_atclk.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CPU, .shift = 20, .size = 3 },
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
	.reg_src	= { .reg = S5P_CLKSRC_CORE, .shift = 4, .size = 1 },
};

static struct clksrc_clk clk_sclk_dmc = {
	.clk	= {
		.name		= "sclk_dmc",
		.id		= -1,
		.parent		= &clk_mout_corebus.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CORE0, .shift = 12, .size = 3 },
};

static struct clksrc_clk clk_aclk_cored = {
	.clk	= {
		.name		= "aclk_cored",
		.id		= -1,
		.parent		= &clk_sclk_dmc.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CORE0, .shift = 16, .size = 3 },
};

static struct clksrc_clk clk_aclk_corep = {
	.clk	= {
		.name		= "aclk_corep",
		.id		= -1,
		.parent		= &clk_aclk_cored.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CORE0, .shift = 20, .size = 3 },
};

static struct clksrc_clk clk_aclk_acp = {
	.clk	= {
		.name		= "aclk_acp",
		.id		= -1,
		.parent		= &clk_mout_corebus.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CORE0, .shift = 0, .size = 3 },
};

static struct clksrc_clk clk_pclk_acp = {
	.clk	= {
		.name		= "pclk_acp",
		.id		= -1,
		.parent		= &clk_aclk_acp.clk,
	},
	.reg_div	= { .reg = S5P_CLKDIV_CORE0, .shift = 4, .size = 3 },
};

/* Core list of CMU_TOP side */

static struct clk *clkset_aclk_top_list[] = {
	[0] = &clk_mout_mpll.clk,
	[1] = &clk_sclk_apll.clk,
};

static struct clksrc_sources clkset_aclk_200 = {
	.sources	= clkset_aclk_top_list,
	.nr_sources	= ARRAY_SIZE(clkset_aclk_top_list),
};

static struct clksrc_clk clk_aclk_200 = {
	.clk	= {
		.name		= "aclk_200",
		.id		= -1,
	},
	.sources	= &clkset_aclk_200,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 12, .size = 1 },
	.reg_div	= { .reg = S5P_CLKDIV_TOP, .shift = 0, .size = 3 },
};

static struct clksrc_sources clkset_aclk_100 = {
	.sources	= clkset_aclk_top_list,
	.nr_sources	= ARRAY_SIZE(clkset_aclk_top_list),
};

static struct clksrc_clk clk_aclk_100 = {
	.clk	= {
		.name		= "aclk_100",
		.id		= -1,
	},
	.sources	= &clkset_aclk_100,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 16, .size = 1 },
	.reg_div	= { .reg = S5P_CLKDIV_TOP, .shift = 4, .size = 4 },
};

static struct clksrc_sources clkset_aclk_160 = {
	.sources	= clkset_aclk_top_list,
	.nr_sources	= ARRAY_SIZE(clkset_aclk_top_list),
};

static struct clksrc_clk clk_aclk_160 = {
	.clk	= {
		.name		= "aclk_160",
		.id		= -1,
	},
	.sources	= &clkset_aclk_160,
	.reg_src	= { .reg = S5P_CLKSRC_TOP0, .shift = 20, .size = 1 },
	.reg_div	= { .reg = S5P_CLKDIV_TOP, .shift = 8, .size = 3 },
};

static struct clksrc_sources clkset_aclk_133 = {
	.sources	= clkset_aclk_top_list,
	.nr_sources	= ARRAY_SIZE(clkset_aclk_top_list),
};

static struct clksrc_clk clk_aclk_133 = {
	.clk	= {
		.name		= "aclk_133",
		.id		= -1,
	},
	.sources	= &clkset_aclk_133,
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

static struct clk init_clocks_disable[] = {
	{
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_aclk_100.clk,
		.enable		= s5pv310_clk_ip_peril_ctrl,
		.ctrlbit	= (1<<24),
	}
};

static struct clk init_clocks[] = {
	{
		.name		= "uart",
		.id		= 0,
		.enable		= s5pv310_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "uart",
		.id		= 1,
		.enable		= s5pv310_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "uart",
		.id		= 2,
		.enable		= s5pv310_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "uart",
		.id		= 3,
		.enable		= s5pv310_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "uart",
		.id		= 4,
		.enable		= s5pv310_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "uart",
		.id		= 5,
		.enable		= s5pv310_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 5),
	}
};

static struct clk *clkset_group_list[] = {
	[0] = &clk_ext_xtal_mux,
	[1] = &clk_xusbxti,
	[2] = &clk_sclk_hdmi27m,
	[6] = &clk_mout_mpll.clk,
	[7] = &clk_mout_epll.clk,
	[8] = &clk_sclk_vpll.clk,
};

static struct clksrc_sources clkset_group = {
	.sources	= clkset_group_list,
	.nr_sources	= ARRAY_SIZE(clkset_group_list),
};

static struct clksrc_clk clksrcs[] = {
	{
		.clk	= {
			.name		= "uclk1",
			.id		= 0,
			.enable		= s5pv310_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 0, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 0, .size = 4 },
	}, {
		.clk		= {
			.name		= "uclk1",
			.id		= 1,
			.enable		= s5pv310_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 4),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 4, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 4, .size = 4 },
	}, {
		.clk		= {
			.name		= "uclk1",
			.id		= 2,
			.enable		= s5pv310_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 8),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 8, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 8, .size = 4 },
	}, {
		.clk		= {
			.name		= "uclk1",
			.id		= 3,
			.enable		= s5pv310_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 12),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 12, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL0, .shift = 12, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_pwm",
			.id		= -1,
			.enable		= s5pv310_clksrc_mask_peril0_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &clkset_group,
		.reg_src = { .reg = S5P_CLKSRC_PERIL0, .shift = 24, .size = 4 },
		.reg_div = { .reg = S5P_CLKDIV_PERIL3, .shift = 0, .size = 4 },
	},
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
	&clk_atclk,
	&clk_pclk_dbg,
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
};

void __init_or_cpufreq s5pv310_setup_clocks(void)
{
	struct clk *xtal_clk;
	unsigned long apll;
	unsigned long mpll;
	unsigned long epll;
	unsigned long vpll;
	unsigned long vpllsrc;
	unsigned long xtal;
	unsigned long armclk;
	unsigned long aclk_corem0;
	unsigned long aclk_cores;
	unsigned long aclk_corem1;
	unsigned long periphclk;
	unsigned long sclk_dmc;
	unsigned long aclk_cored;
	unsigned long aclk_corep;
	unsigned long aclk_acp;
	unsigned long pclk_acp;
	unsigned int ptr;

	printk(KERN_DEBUG "%s: registering clocks\n", __func__);

	xtal_clk = clk_get(NULL, "xtal");
	BUG_ON(IS_ERR(xtal_clk));

	xtal = clk_get_rate(xtal_clk);
	clk_put(xtal_clk);

	printk(KERN_DEBUG "%s: xtal is %ld\n", __func__, xtal);

	apll = s5p_get_pll45xx(xtal, __raw_readl(S5P_APLL_CON0), pll_4508);
	mpll = s5p_get_pll45xx(xtal, __raw_readl(S5P_MPLL_CON0), pll_4508);
	epll = s5p_get_pll46xx(xtal, __raw_readl(S5P_EPLL_CON0),
				__raw_readl(S5P_EPLL_CON1), pll_4600);

	vpllsrc = clk_get_rate(&clk_vpllsrc.clk);
	vpll = s5p_get_pll46xx(vpllsrc, __raw_readl(S5P_VPLL_CON0),
				__raw_readl(S5P_VPLL_CON1), pll_4650);

	clk_fout_apll.rate = apll;
	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;
	clk_fout_vpll.rate = vpll;

	printk(KERN_INFO "S5PV310: PLL settings, A=%ld, M=%ld, E=%ld V=%ld",
			apll, mpll, epll, vpll);

	armclk = clk_get_rate(&clk_armclk.clk);
	aclk_corem0 = clk_get_rate(&clk_aclk_corem0.clk);
	aclk_cores = clk_get_rate(&clk_aclk_cores.clk);
	aclk_corem1 = clk_get_rate(&clk_aclk_corem1.clk);
	periphclk = clk_get_rate(&clk_periphclk.clk);
	sclk_dmc = clk_get_rate(&clk_sclk_dmc.clk);
	aclk_cored = clk_get_rate(&clk_aclk_cored.clk);
	aclk_corep = clk_get_rate(&clk_aclk_corep.clk);
	aclk_acp = clk_get_rate(&clk_aclk_acp.clk);
	pclk_acp = clk_get_rate(&clk_pclk_acp.clk);

	printk(KERN_INFO "S5PV310: ARMCLK=%ld, COREM0=%ld, CORES=%ld\n"
			 "COREM1=%ld, PERI=%ld, DMC=%ld, CORED=%ld\n"
			 "COREP=%ld, ACLK_ACP=%ld, PCLK_ACP=%ld",
			armclk, aclk_corem0, aclk_cores, aclk_corem1,
			periphclk, sclk_dmc, aclk_cored, aclk_corep,
			aclk_acp, pclk_acp);

	clk_f.rate = armclk;
	clk_h.rate = sclk_dmc;
	clk_p.rate = periphclk;

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_set_clksrc(&clksrcs[ptr], true);
}

static struct clk *clks[] __initdata = {
	/* Nothing here yet */
};

void __init s5pv310_register_clocks(void)
{
	struct clk *clkp;
	int ret;
	int ptr;

	ret = s3c24xx_register_clocks(clks, ARRAY_SIZE(clks));
	if (ret > 0)
		printk(KERN_ERR "Failed to register %u clocks\n", ret);

	for (ptr = 0; ptr < ARRAY_SIZE(sysclks); ptr++)
		s3c_register_clksrc(sysclks[ptr], 1);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));
	s3c_register_clocks(init_clocks, ARRAY_SIZE(init_clocks));

	clkp = init_clocks_disable;
	for (ptr = 0; ptr < ARRAY_SIZE(init_clocks_disable); ptr++, clkp++) {
		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
		(clkp->enable)(clkp, 0);
	}

	s3c_pwmclk_init();
}
