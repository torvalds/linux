/*
 * Copyright (c) 2011-2012 Samsung Electronics Co., Ltd.
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
#include <plat/pm.h>

#include <mach/hardware.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/exynos-clock.h>
#include <mach/sysmmu.h>

#include "common.h"
#include "clock-exynos4.h"

#ifdef CONFIG_PM_SLEEP
static struct sleep_save exynos4212_clock_save[] = {
	SAVE_ITEM(EXYNOS4_CLKSRC_IMAGE),
	SAVE_ITEM(EXYNOS4_CLKDIV_IMAGE),
	SAVE_ITEM(EXYNOS4212_CLKGATE_IP_IMAGE),
	SAVE_ITEM(EXYNOS4212_CLKGATE_IP_PERIR),
};
#endif

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

static struct clk *clk_src_mpll_user_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos4_clk_mout_mpll.clk,
};

static struct clksrc_sources clk_src_mpll_user = {
	.sources	= clk_src_mpll_user_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mpll_user_list),
};

static struct clksrc_clk clk_mout_mpll_user = {
	.clk = {
		.name		= "mout_mpll_user",
	},
	.sources	= &clk_src_mpll_user,
	.reg_src	= { .reg = EXYNOS4_CLKSRC_CPU, .shift = 24, .size = 1 },
};

static struct clk *exynos4212_clkset_aclk_lrbus_user_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos4_clk_mout_mpll.clk,
};

static struct clksrc_sources exynos4212_clkset_aclk_lrbus_user = {
	.sources        = exynos4212_clkset_aclk_lrbus_user_list,
	.nr_sources     = ARRAY_SIZE(exynos4212_clkset_aclk_lrbus_user_list),
};

static struct clksrc_clk exynos4212_clk_aclk_gdl_user = {
	.clk    = {
		.name           = "aclk_gdl_user",
	},
	.sources = &exynos4212_clkset_aclk_lrbus_user,
	.reg_src = { .reg = EXYNOS4_CLKSRC_LEFTBUS, .shift = 4, .size = 1 },
};

static struct clksrc_clk exynos4212_clk_aclk_gdr_user = {
	.clk    = {
		.name           = "aclk_gdr_user",
	},
	.sources = &exynos4212_clkset_aclk_lrbus_user,
	.reg_src = { .reg = EXYNOS4_CLKSRC_RIGHTBUS, .shift = 4, .size = 1 },
};

static struct clksrc_clk exynos4212_clk_mout_aclk_266 = {
	.clk    = {
		.name           = "mout_aclk_266",
	},
	.sources = &exynos4_clkset_aclk,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP1, .shift = 4, .size = 1 },
};

static struct clksrc_clk exynos4212_clk_dout_aclk_266 = {
	.clk    = {
		.name           = "dout_aclk_266",
		.parent         = &exynos4212_clk_mout_aclk_266.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_TOP, .shift = 20, .size = 3 },
};

static struct clksrc_clk exynos4212_clk_mout_aclk_200 = {
	.clk    = {
		.name           = "mout_aclk_200",
	},
	.sources = &exynos4_clkset_aclk,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP0, .shift = 12, .size = 1 },
};

static struct clksrc_clk exynos4212_clk_dout_aclk_200 = {
	.clk    = {
		.name           = "dout_aclk_200",
		.parent         = &exynos4212_clk_mout_aclk_200.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_TOP, .shift = 0, .size = 3 },
};

static struct clksrc_clk exynos4212_clk_mout_aclk_400_mcuisp = {
	.clk    = {
		.name           = "mout_aclk_400_mcuisp",
	},
	.sources = &exynos4_clkset_aclk,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP1, .shift = 8, .size = 1 },
};

static struct clksrc_clk exynos4212_clk_dout_aclk_400_mcuisp = {
	.clk    = {
		.name           = "dout_aclk_400_mcuisp",
		.parent         = &exynos4212_clk_mout_aclk_400_mcuisp.clk,
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
	.clk = {
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
	.sources        = exynos4212_clk_aclk_266_list,
	.nr_sources     = ARRAY_SIZE(exynos4212_clk_aclk_266_list),
};

struct clksrc_clk exynos4212_clk_aclk_266 = {
	.clk    = {
		.name           = "aclk_266",
	},
	.sources = &exynos4212_clkset_aclk_266,
	.reg_src = { .reg = EXYNOS4_CLKSRC_TOP1, .shift = 16, .size = 1 },
};

static struct clk *exynos4212_clk_aclk_200_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos4212_clk_dout_aclk_200.clk,
};

static struct clksrc_sources exynos4212_clkset_aclk_200 = {
	.sources        = exynos4212_clk_aclk_200_list,
	.nr_sources     = ARRAY_SIZE(exynos4212_clk_aclk_200_list),
};

static struct clksrc_clk *sysclks[] = {
	&clk_mout_mpll_user,
	&exynos4212_clk_aclk_gdl_user,
	&exynos4212_clk_aclk_gdr_user,
	&exynos4212_clk_mout_aclk_266,
	&exynos4212_clk_dout_aclk_266,
	&exynos4212_clk_mout_aclk_200,
	&exynos4212_clk_dout_aclk_200,
	&exynos4212_clk_mout_aclk_400_mcuisp,
	&exynos4212_clk_dout_aclk_400_mcuisp,
	&exynos4212_clk_aclk_266,
	&exynos4212_clk_aclk_400_mcuisp,
};

static struct clksrc_clk clksrcs[] = {
	/* nothing here yet */
};

static struct clk init_clocks_off[] = {
	{
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(2d, 15),
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 24),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(isp0, 9),
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (7 << 8),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(isp1, 16),
		.enable		= exynos4212_clk_ip_isp1_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(camif0, 12),
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(camif1, 13),
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "ppmuisp",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 20 | 1 << 21),
	}, {
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

static struct clk exynos4212_clk_isp[] = {
	{
		.name	= "aclk_400_mcuisp_muxed",
		.parent	= &exynos4212_clk_aclk_400_mcuisp.clk,
	}, {
		.name	= "aclk_200_muxed",
		.parent	= &exynos4_clk_aclk_200.clk,
	},
};

static struct clksrc_clk exynos4212_clk_isp_srcs_div0 = {
	.clk = {
		.name		= "sclk_mcuisp_div0",
		.parent		= &exynos4212_clk_aclk_400_mcuisp.clk,
	},
	.reg_div = { .reg = EXYNOS4_CLKDIV_ISP1, .shift = 4, .size = 3 },
};

static struct clksrc_clk exynos4212_clk_isp_srcs[] = {
	{
		.clk = {
			.name		= "sclk_mcuisp_div1",
			.parent		= &exynos4212_clk_isp_srcs_div0.clk,
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP1, .shift = 8, .size = 3 },
	}, {
		.clk = {
			.name		= "sclk_aclk_div0",
			.parent		= &exynos4_clk_aclk_200.clk,
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP0, .shift = 0, .size = 3 },
	}, {
		.clk = {
			.name		= "sclk_aclk_div1",
			.parent		= &exynos4_clk_aclk_200.clk,
		},
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP0, .shift = 4, .size = 3 },
	}, {
		.clk = {
			.name		= "sclk_uart_isp",
			.enable		= exynos4212_clk_ip_isp_ctrl,
			.ctrlbit	= (1 << 3),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_ISP, .shift = 12, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP, .shift = 28, .size = 4 },
	}, {
		.clk = {
			.name		= "sclk_spi1_isp",
			.enable		= exynos4212_clk_ip_isp_ctrl,
			.ctrlbit	= (1 << 2),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_ISP, .shift = 8, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP, .shift = 16, .size = 12 },
	}, {
		.clk = {
			.name		= "sclk_spi0_isp",
			.enable		= exynos4212_clk_ip_isp_ctrl,
			.ctrlbit	= (1 << 1),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_ISP, .shift = 4, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP, .shift = 4, .size = 12 },
	}, {
		.clk = {
			.name		= "sclk_pwm_isp",
			.enable		= exynos4212_clk_ip_isp_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4_CLKSRC_ISP, .shift = 0, .size = 4 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_ISP, .shift = 0, .size = 4 },
	},
};

#ifdef CONFIG_PM_SLEEP
static int exynos4212_clock_suspend(void)
{
	s3c_pm_do_save(exynos4212_clock_save, ARRAY_SIZE(exynos4212_clock_save));

	return 0;
}

static void exynos4212_clock_resume(void)
{
	s3c_pm_do_restore_core(exynos4212_clock_save, ARRAY_SIZE(exynos4212_clock_save));
}

#else
#define exynos4212_clock_suspend NULL
#define exynos4212_clock_resume NULL
#endif

static struct syscore_ops exynos4212_clock_syscore_ops = {
	.suspend	= exynos4212_clock_suspend,
	.resume		= exynos4212_clock_resume,
};

void __init exynos4212_register_clocks(void)
{
	int ptr;

	/* usbphy1 is removed */
	exynos4_clkset_group_list[4] = NULL;

	/* mout_mpll_user is used */
	exynos4_clkset_group_list[6] = &clk_mout_mpll_user.clk;
	exynos4_clkset_aclk_top_list[0] = &clk_mout_mpll_user.clk;

	exynos4_clk_mout_mpll.reg_src.reg = EXYNOS4_CLKSRC_DMC;
	exynos4_clk_mout_mpll.reg_src.shift = 12;
	exynos4_clk_mout_mpll.reg_src.size = 1;

	exynos4_clk_aclk_200.sources = &exynos4212_clkset_aclk_200;
	exynos4_clk_aclk_200.reg_src.reg = EXYNOS4_CLKSRC_TOP1;
	exynos4_clk_aclk_200.reg_src.shift = 20;
	exynos4_clk_aclk_200.reg_src.size = 1;

	for (ptr = 0; ptr < ARRAY_SIZE(sysclks); ptr++)
		s3c_register_clksrc(sysclks[ptr], 1);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));

	s3c_register_clksrc(&exynos4212_clk_isp_srcs_div0, 1);
	s3c_register_clksrc(exynos4212_clk_isp_srcs, ARRAY_SIZE(exynos4212_clk_isp_srcs));
	s3c_register_clocks(exynos4212_clk_isp, ARRAY_SIZE(exynos4212_clk_isp));
	s3c_disable_clocks(&exynos4212_clk_isp_srcs[3].clk, 1);
	s3c_disable_clocks(&exynos4212_clk_isp_srcs[4].clk, 1);
	s3c_disable_clocks(&exynos4212_clk_isp_srcs[5].clk, 1);
	s3c_disable_clocks(&exynos4212_clk_isp_srcs[6].clk, 1);

	register_syscore_ops(&exynos4212_clock_syscore_ops);
}
