// SPDX-License-Identifier: GPL-2.0-only
/*
 * DaVinci Power Management Routines
 *
 * Copyright (C) 2009 Texas Instruments, Inc. https://www.ti.com/
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/spinlock.h>

#include <asm/cacheflush.h>
#include <asm/delay.h>
#include <asm/io.h>

#include "common.h"
#include "da8xx.h"
#include "mux.h"
#include "pm.h"
#include "clock.h"
#include "psc.h"
#include "sram.h"

#define DA850_PLL1_BASE		0x01e1a000
#define DEEPSLEEP_SLEEPCOUNT_MASK	0xFFFF
#define DEEPSLEEP_SLEEPCOUNT		128

static void (*davinci_sram_suspend) (struct davinci_pm_config *);
static struct davinci_pm_config pm_config = {
	.sleepcount = DEEPSLEEP_SLEEPCOUNT,
	.ddrpsc_num = DA8XX_LPSC1_EMIF3C,
};

static void davinci_sram_push(void *dest, void *src, unsigned int size)
{
	memcpy(dest, src, size);
	flush_icache_range((unsigned long)dest, (unsigned long)(dest + size));
}

static void davinci_pm_suspend(void)
{
	unsigned val;

	if (pm_config.cpupll_reg_base != pm_config.ddrpll_reg_base) {

		/* Switch CPU PLL to bypass mode */
		val = __raw_readl(pm_config.cpupll_reg_base + PLLCTL);
		val &= ~(PLLCTL_PLLENSRC | PLLCTL_PLLEN);
		__raw_writel(val, pm_config.cpupll_reg_base + PLLCTL);

		udelay(PLL_BYPASS_TIME);

		/* Powerdown CPU PLL */
		val = __raw_readl(pm_config.cpupll_reg_base + PLLCTL);
		val |= PLLCTL_PLLPWRDN;
		__raw_writel(val, pm_config.cpupll_reg_base + PLLCTL);
	}

	/* Configure sleep count in deep sleep register */
	val = __raw_readl(pm_config.deepsleep_reg);
	val &= ~DEEPSLEEP_SLEEPCOUNT_MASK;
	val |= pm_config.sleepcount;
	__raw_writel(val, pm_config.deepsleep_reg);

	/* System goes to sleep in this call */
	davinci_sram_suspend(&pm_config);

	if (pm_config.cpupll_reg_base != pm_config.ddrpll_reg_base) {

		/* put CPU PLL in reset */
		val = __raw_readl(pm_config.cpupll_reg_base + PLLCTL);
		val &= ~PLLCTL_PLLRST;
		__raw_writel(val, pm_config.cpupll_reg_base + PLLCTL);

		/* put CPU PLL in power down */
		val = __raw_readl(pm_config.cpupll_reg_base + PLLCTL);
		val &= ~PLLCTL_PLLPWRDN;
		__raw_writel(val, pm_config.cpupll_reg_base + PLLCTL);

		/* wait for CPU PLL reset */
		udelay(PLL_RESET_TIME);

		/* bring CPU PLL out of reset */
		val = __raw_readl(pm_config.cpupll_reg_base + PLLCTL);
		val |= PLLCTL_PLLRST;
		__raw_writel(val, pm_config.cpupll_reg_base + PLLCTL);

		/* Wait for CPU PLL to lock */
		udelay(PLL_LOCK_TIME);

		/* Remove CPU PLL from bypass mode */
		val = __raw_readl(pm_config.cpupll_reg_base + PLLCTL);
		val &= ~PLLCTL_PLLENSRC;
		val |= PLLCTL_PLLEN;
		__raw_writel(val, pm_config.cpupll_reg_base + PLLCTL);
	}
}

static int davinci_pm_enter(suspend_state_t state)
{
	int ret = 0;

	switch (state) {
	case PM_SUSPEND_MEM:
		davinci_pm_suspend();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct platform_suspend_ops davinci_pm_ops = {
	.enter		= davinci_pm_enter,
	.valid		= suspend_valid_only_mem,
};

int __init davinci_pm_init(void)
{
	int ret;

	ret = davinci_cfg_reg(DA850_RTC_ALARM);
	if (ret)
		return ret;

	pm_config.ddr2_ctlr_base = da8xx_get_mem_ctlr();
	pm_config.deepsleep_reg = DA8XX_SYSCFG1_VIRT(DA8XX_DEEPSLEEP_REG);

	pm_config.cpupll_reg_base = ioremap(DA8XX_PLL0_BASE, SZ_4K);
	if (!pm_config.cpupll_reg_base)
		return -ENOMEM;

	pm_config.ddrpll_reg_base = ioremap(DA850_PLL1_BASE, SZ_4K);
	if (!pm_config.ddrpll_reg_base) {
		ret = -ENOMEM;
		goto no_ddrpll_mem;
	}

	pm_config.ddrpsc_reg_base = ioremap(DA8XX_PSC1_BASE, SZ_4K);
	if (!pm_config.ddrpsc_reg_base) {
		ret = -ENOMEM;
		goto no_ddrpsc_mem;
	}

	davinci_sram_suspend = sram_alloc(davinci_cpu_suspend_sz, NULL);
	if (!davinci_sram_suspend) {
		pr_err("PM: cannot allocate SRAM memory\n");
		ret = -ENOMEM;
		goto no_sram_mem;
	}

	davinci_sram_push(davinci_sram_suspend, davinci_cpu_suspend,
						davinci_cpu_suspend_sz);

	suspend_set_ops(&davinci_pm_ops);

	return 0;

no_sram_mem:
	iounmap(pm_config.ddrpsc_reg_base);
no_ddrpsc_mem:
	iounmap(pm_config.ddrpll_reg_base);
no_ddrpll_mem:
	iounmap(pm_config.cpupll_reg_base);
	return ret;
}
