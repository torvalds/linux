/*
 * linux/arch/arm/mach-exynos/clock-exynos4210.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4210 - Clock support
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
#include <mach/dev-sysmmu.h>
#include <mach/exynos-clock.h>
#include <mach/dev-sysmmu.h>

static struct clksrc_clk *sysclks[] = {
	/* nothing here yet */
};

#ifdef CONFIG_PM
static struct sleep_save exynos4210_clock_save[] = {
	/* CMU side */
	SAVE_ITEM(EXYNOS4_CLKSRC_MASK_LCD1),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_IMAGE_4210),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_LCD1),
	SAVE_ITEM(EXYNOS4_CLKGATE_IP_PERIR_4210),
	SAVE_ITEM(EXYNOS4_CLKDIV_IMAGE),
	SAVE_ITEM(EXYNOS4_CLKDIV_LCD1),
	SAVE_ITEM(EXYNOS4_CLKSRC_IMAGE),
	SAVE_ITEM(EXYNOS4_CLKSRC_LCD1),
};

static struct sleep_save exynos4210_epll_save[] = {
       SAVE_ITEM(EXYNOS4_EPLL_LOCK),
       SAVE_ITEM(EXYNOS4_EPLL_CON0),
       SAVE_ITEM(EXYNOS4_EPLL_CON1),
};

static struct sleep_save exynos4210_vpll_save[] = {
       SAVE_ITEM(EXYNOS4_VPLL_LOCK),
       SAVE_ITEM(EXYNOS4_VPLL_CON0),
       SAVE_ITEM(EXYNOS4_VPLL_CON1),
};
#endif

static int exynos4_clk_ip_lcd1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_LCD1, clk, enable);
}

static struct clk init_clocks_off[] = {
	{
		.name		= "qeg2d",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "fimg2d",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "tvenc",
		.parent		= &exynos4_clk_aclk_160.clk,
		.enable		= exynos4_clk_ip_tv_ctrl,
		.ctrlbit	= (1 << 2),
#if !defined(CONFIG_VIDEO_TSI)
	}, {
		.name		= "tsi",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 4),
#endif
	}, {
		.name		= "sataphy",
		.parent		= &exynos4_clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "sata",
		.parent		= &exynos4_clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "ppmulcd1",
		.enable		= exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name           = "sysmmu",
		.devname        = SYSMMU_CLOCK_NAME(fimd1, 7),
		.enable         = exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit        = (1 << 4),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(g2d, 9),
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name           = "dsim1",
		.enable         = exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit        = (1 << 3),
	}, {
		.name           = "mdnie1",
		.enable         = exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit        = (1 << 2),
	}, {
		.name           = "mie1",
		.enable         = exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit        = (1 << 1),
	}, {
		.name           = "lcd",
		.devname        = "s3cfb.1",
		.enable         = exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit        = (1 << 0),
	}, {
		.name		= "pciephy",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "pcie",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 14),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(pcie, 8),
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 18),
	}, {
		.name		= "sysmmu",
		.devname	= SYSMMU_CLOCK_NAME(2d, 9),
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 3),
#ifdef CONFIG_INTERNAL_MODEM_IF
	}, {
		.name		= "modem",
		.enable		= exynos4_clk_ip_peril_ctrl,
		.ctrlbit	= (1 << 28),
#endif
	}
};

static struct clk init_clocks[] = {
	{
		.name		= "cmu_dmcpart",
		.enable		= exynos4_clk_ip_perir_ctrl,
		.ctrlbit	= (1 << 4),
#if defined(CONFIG_VIDEO_TSI)
	}, {
		.name		= "tsi",
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 4),
#endif
	}, {
		.name		= "dmc1",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "dmc0",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 0),
	},
};

static struct clksrc_clk clksrcs[] = {
	{
		.clk	= {
			.name		= "sclk_sata",
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &exynos4_clkset_mout_corebus,
		.reg_src = { .reg = EXYNOS4_CLKSRC_FSYS, .shift = 24, .size = 1 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS0, .shift = 20, .size = 4 },
	},
};


static u32 epll_div_4210[][6] = {
	{ 192000000, 0, 48, 3, 1, 0 },
	{ 180000000, 0, 45, 3, 1, 0 },
	{  73728000, 1, 73, 3, 3, 47710 },
	{  67737600, 1, 90, 4, 3, 20762 },
	{  49152000, 0, 49, 3, 3, 9961 },
	{  45158400, 0, 45, 3, 3, 10381 },
	{ 180633600, 0, 45, 3, 1, 10381 },
};

static unsigned long exynos4210_epll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int exynos4210_epll_set_rate(struct clk *clk, unsigned long rate)
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


	epll_con = __raw_readl(EXYNOS4_EPLL_CON0);
	epll_con &= ~(0x1 << 27 | \
			PLL46XX_MDIV_MASK << PLL46XX_MDIV_SHIFT |   \
			PLL46XX_PDIV_MASK << PLL46XX_PDIV_SHIFT | \
			PLL46XX_SDIV_MASK << PLL46XX_SDIV_SHIFT);

	for (i = 0; i < ARRAY_SIZE(epll_div_4210); i++) {
		if (epll_div_4210[i][0] == rate) {
			epll_con_k = epll_div_4210[i][5] << 0;
			epll_con |= epll_div_4210[i][1] << 27;
			epll_con |= epll_div_4210[i][2] << PLL46XX_MDIV_SHIFT;
			epll_con |= epll_div_4210[i][3] << PLL46XX_PDIV_SHIFT;
			epll_con |= epll_div_4210[i][4] << PLL46XX_SDIV_SHIFT;
			break;
		}
	}

	if (i == ARRAY_SIZE(epll_div_4210)) {
		printk(KERN_ERR "%s: Invalid Clock EPLL Frequency\n",
				__func__);
		return -EINVAL;
	}

	epll_rate /= 1000000;

	/* 3000 max_cycls : specification data */
	locktime = 3000 / epll_rate * epll_div_4210[i][3];
	lockcnt = locktime * 10000 / (10000 / epll_rate);

	__raw_writel(lockcnt, EXYNOS4_EPLL_LOCK);

	__raw_writel(epll_con, EXYNOS4_EPLL_CON0);
	__raw_writel(epll_con_k, EXYNOS4_EPLL_CON1);

	do {
		tmp = __raw_readl(EXYNOS4_EPLL_CON0);
	} while (!(tmp & 0x1 << EXYNOS4_EPLLCON0_LOCKED_SHIFT));

	clk->rate = rate;

	return 0;
}


static struct vpll_div_data vpll_div_4210[] = {
	{54000000, 3, 53, 3, 1024, 0, 17, 0},
	{108000000, 3, 53, 2, 1024, 0, 17, 0},
	{260000000, 3, 63, 1, 1950, 0, 20, 1},
	{330000000, 2, 53, 1, 2048, 1,  1, 1},
#ifdef CONFIG_EXYNOS4_MSHC_VPLL_46MHZ
	{370882812, 3, 44, 0, 2417, 0, 14, 0},
#endif
};

static unsigned long exynos4210_vpll_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int exynos4210_vpll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int vpll_con0, vpll_con1;
	unsigned int i;

	/* Return if nothing changed */
	if (clk->rate == rate)
		return 0;

	vpll_con0 = __raw_readl(EXYNOS4_VPLL_CON0);
	vpll_con0 &= ~(0x1 << 27 |					\
			PLL90XX_MDIV_MASK << PLL90XX_MDIV_SHIFT |	\
			PLL90XX_PDIV_MASK << PLL90XX_PDIV_SHIFT |	\
			PLL90XX_SDIV_MASK << PLL90XX_SDIV_SHIFT);

	vpll_con1 = __raw_readl(EXYNOS4_VPLL_CON1);
	vpll_con1 &= ~(0x1f << 24 |	\
			0x3f << 16 |	\
			0xfff << 0);

	for (i = 0; i < ARRAY_SIZE(vpll_div_4210); i++) {
		if (vpll_div_4210[i].rate == rate) {
			vpll_con0 |= vpll_div_4210[i].vsel << 27;
			vpll_con0 |= vpll_div_4210[i].pdiv << PLL90XX_PDIV_SHIFT;
			vpll_con0 |= vpll_div_4210[i].mdiv << PLL90XX_MDIV_SHIFT;
			vpll_con0 |= vpll_div_4210[i].sdiv << PLL90XX_SDIV_SHIFT;
			vpll_con1 |= vpll_div_4210[i].mrr << 24;
			vpll_con1 |= vpll_div_4210[i].mfr << 16;
			vpll_con1 |= vpll_div_4210[i].k << 0;
			break;
		}
	}

	if (i == ARRAY_SIZE(vpll_div_4210)) {
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
static int exynos4210_clock_suspend(void)
{
	s3c_pm_do_save(exynos4210_clock_save, ARRAY_SIZE(exynos4210_clock_save));
	s3c_pm_do_save(exynos4210_epll_save, ARRAY_SIZE(exynos4210_epll_save));
	s3c_pm_do_save(exynos4210_vpll_save, ARRAY_SIZE(exynos4210_vpll_save));

	return 0;
}

static void exynos4210_clock_resume(void)
{
	unsigned int tmp;

	s3c_pm_do_restore_core(exynos4210_epll_save, ARRAY_SIZE(exynos4210_epll_save));
	s3c_pm_do_restore_core(exynos4210_vpll_save, ARRAY_SIZE(exynos4210_vpll_save));

	/* waiting epll & vpll locking time */
	do {
		tmp = __raw_readl(EXYNOS4_EPLL_CON0);
	} while (!(tmp & 0x1 << EXYNOS4_EPLLCON0_LOCKED_SHIFT));

	do {
		tmp = __raw_readl(EXYNOS4_VPLL_CON0);
	} while (!(tmp & 0x1 << EXYNOS4_VPLLCON0_LOCKED_SHIFT));

	s3c_pm_do_restore_core(exynos4210_clock_save, ARRAY_SIZE(exynos4210_clock_save));
}
#else
#define exynos4210_clock_suspend NULL
#define exynos4210_clock_resume NULL
#endif

struct syscore_ops exynos4210_clock_syscore_ops = {
	.suspend        = exynos4210_clock_suspend,
	.resume         = exynos4210_clock_resume,
};

void __init exynos4210_register_clocks(void)
{
	int ptr;

	exynos4_clk_mout_mpll.reg_src.reg = EXYNOS4_CLKSRC_CPU;
	exynos4_clk_mout_mpll.reg_src.shift = 8;
	exynos4_clk_mout_mpll.reg_src.size = 1;

	exynos4_clk_aclk_200.sources = &exynos4_clkset_aclk;
	exynos4_clk_aclk_200.reg_src.reg = EXYNOS4_CLKSRC_TOP0;
	exynos4_clk_aclk_200.reg_src.shift = 12;
	exynos4_clk_aclk_200.reg_src.size = 1;
	exynos4_clk_aclk_200.reg_div.reg = EXYNOS4_CLKDIV_TOP;
	exynos4_clk_aclk_200.reg_div.shift = 0;
	exynos4_clk_aclk_200.reg_div.size = 3;

	exynos4_clk_fimg2d.enable = exynos4_clk_ip_image_ctrl;
	exynos4_clk_fimg2d.ctrlbit = (1 << 3) | (1 << 0);

	exynos4_clk_mout_g2d0.reg_src.reg = EXYNOS4_CLKSRC_IMAGE;
	exynos4_clk_mout_g2d0.reg_src.shift = 0;
	exynos4_clk_mout_g2d0.reg_src.size = 1;

	exynos4_clk_mout_g2d1.reg_src.reg = EXYNOS4_CLKSRC_IMAGE;
	exynos4_clk_mout_g2d1.reg_src.shift = 4;
	exynos4_clk_mout_g2d1.reg_src.size = 1;

	exynos4_clk_sclk_fimg2d.reg_src.reg = EXYNOS4_CLKSRC_IMAGE;
	exynos4_clk_sclk_fimg2d.reg_src.shift = 8;
	exynos4_clk_sclk_fimg2d.reg_src.size = 1;
	exynos4_clk_sclk_fimg2d.reg_div.reg = EXYNOS4_CLKDIV_IMAGE;
	exynos4_clk_sclk_fimg2d.reg_div.shift = 0;
	exynos4_clk_sclk_fimg2d.reg_div.size = 4;

	exynos4_init_dmaclocks[2].parent = &exynos4_init_dmaclocks[1];

	exynos4_epll_ops.get_rate = exynos4210_epll_get_rate;
	exynos4_epll_ops.set_rate = exynos4210_epll_set_rate;
	exynos4_vpll_ops.get_rate = exynos4210_vpll_get_rate;
	exynos4_vpll_ops.set_rate = exynos4210_vpll_set_rate;

	for (ptr = 0; ptr < ARRAY_SIZE(sysclks); ptr++)
		s3c_register_clksrc(sysclks[ptr], 1);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));
	s3c_register_clocks(init_clocks, ARRAY_SIZE(init_clocks));

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	register_syscore_ops(&exynos4210_clock_syscore_ops);
}
