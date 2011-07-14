/*
 * sh7372 Power management support
 *
 *  Copyright (C) 2011 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/cpuidle.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/tlbflush.h>
#include <mach/common.h>
#include <mach/sh7372.h>

#define SMFRAM 0xe6a70000
#define SYSTBCR 0xe6150024
#define SBAR 0xe6180020
#define APARMBAREA 0xe6f10020

#define SPDCR 0xe6180008
#define SWUCR 0xe6180014
#define PSTR 0xe6180080

#define PSTR_RETRIES 100
#define PSTR_DELAY_US 10

#ifdef CONFIG_PM

static int pd_power_down(struct generic_pm_domain *genpd)
{
	struct sh7372_pm_domain *sh7372_pd = to_sh7372_pd(genpd);
	unsigned int mask = 1 << sh7372_pd->bit_shift;

	if (__raw_readl(PSTR) & mask) {
		unsigned int retry_count;

		__raw_writel(mask, SPDCR);

		for (retry_count = PSTR_RETRIES; retry_count; retry_count--) {
			if (!(__raw_readl(SPDCR) & mask))
				break;
			cpu_relax();
		}
	}

	pr_debug("sh7372 power domain down 0x%08x -> PSTR = 0x%08x\n",
		 mask, __raw_readl(PSTR));

	return 0;
}

static int pd_power_up(struct generic_pm_domain *genpd)
{
	struct sh7372_pm_domain *sh7372_pd = to_sh7372_pd(genpd);
	unsigned int mask = 1 << sh7372_pd->bit_shift;
	unsigned int retry_count;
	int ret = 0;

	if (__raw_readl(PSTR) & mask)
		goto out;

	__raw_writel(mask, SWUCR);

	for (retry_count = 2 * PSTR_RETRIES; retry_count; retry_count--) {
		if (!(__raw_readl(SWUCR) & mask))
			goto out;
		if (retry_count > PSTR_RETRIES)
			udelay(PSTR_DELAY_US);
		else
			cpu_relax();
	}
	if (__raw_readl(SWUCR) & mask)
		ret = -EIO;

 out:
	pr_debug("sh7372 power domain up 0x%08x -> PSTR = 0x%08x\n",
		 mask, __raw_readl(PSTR));

	return ret;
}

static int pd_power_up_a3rv(struct generic_pm_domain *genpd)
{
	int ret = pd_power_up(genpd);

	/* force A4LC on after A3RV has been requested on */
	pm_genpd_poweron(&sh7372_a4lc.genpd);

	return ret;
}

static int pd_power_down_a3rv(struct generic_pm_domain *genpd)
{
	int ret = pd_power_down(genpd);

	/* try to power down A4LC after A3RV is requested off */
	genpd_queue_power_off_work(&sh7372_a4lc.genpd);

	return ret;
}

static int pd_power_down_a4lc(struct generic_pm_domain *genpd)
{
	/* only power down A4LC if A3RV is off */
	if (!(__raw_readl(PSTR) & (1 << sh7372_a3rv.bit_shift)))
		return pd_power_down(genpd);

	return -EBUSY;
}

static bool pd_active_wakeup(struct device *dev)
{
	return true;
}

void sh7372_init_pm_domain(struct sh7372_pm_domain *sh7372_pd)
{
	struct generic_pm_domain *genpd = &sh7372_pd->genpd;

	pm_genpd_init(genpd, NULL, false);
	genpd->stop_device = pm_clk_suspend;
	genpd->start_device = pm_clk_resume;
	genpd->active_wakeup = pd_active_wakeup;

	if (sh7372_pd == &sh7372_a4lc) {
		genpd->power_off = pd_power_down_a4lc;
		genpd->power_on = pd_power_up;
	} else if (sh7372_pd == &sh7372_a3rv) {
		genpd->power_off = pd_power_down_a3rv;
		genpd->power_on = pd_power_up_a3rv;
	} else {
		genpd->power_off = pd_power_down;
		genpd->power_on = pd_power_up;
	}
	genpd->power_on(&sh7372_pd->genpd);
}

void sh7372_add_device_to_domain(struct sh7372_pm_domain *sh7372_pd,
				 struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!dev->power.subsys_data) {
		pm_clk_init(dev);
		pm_clk_add(dev, NULL);
	}
	pm_genpd_add_device(&sh7372_pd->genpd, dev);
}

struct sh7372_pm_domain sh7372_a4lc = {
	.bit_shift = 1,
};

struct sh7372_pm_domain sh7372_a4mp = {
	.bit_shift = 2,
};

struct sh7372_pm_domain sh7372_d4 = {
	.bit_shift = 3,
};

struct sh7372_pm_domain sh7372_a3rv = {
	.bit_shift = 6,
};

struct sh7372_pm_domain sh7372_a3ri = {
	.bit_shift = 8,
};

struct sh7372_pm_domain sh7372_a3sg = {
	.bit_shift = 13,
};

#endif /* CONFIG_PM */

static void sh7372_enter_core_standby(void)
{
	void __iomem *smfram = (void __iomem *)SMFRAM;

	__raw_writel(0, APARMBAREA); /* translate 4k */
	__raw_writel(__pa(sh7372_cpu_resume), SBAR); /* set reset vector */
	__raw_writel(0x10, SYSTBCR); /* enable core standby */

	__raw_writel(0, smfram + 0x3c); /* clear page table address */

	sh7372_cpu_suspend();
	cpu_init();

	/* if page table address is non-NULL then we have been powered down */
	if (__raw_readl(smfram + 0x3c)) {
		__raw_writel(__raw_readl(smfram + 0x40),
			     __va(__raw_readl(smfram + 0x3c)));

		flush_tlb_all();
		set_cr(__raw_readl(smfram + 0x38));
	}

	__raw_writel(0, SYSTBCR); /* disable core standby */
	__raw_writel(0, SBAR); /* disable reset vector translation */
}

#ifdef CONFIG_CPU_IDLE
static void sh7372_cpuidle_setup(struct cpuidle_device *dev)
{
	struct cpuidle_state *state;
	int i = dev->state_count;

	state = &dev->states[i];
	snprintf(state->name, CPUIDLE_NAME_LEN, "C2");
	strncpy(state->desc, "Core Standby Mode", CPUIDLE_DESC_LEN);
	state->exit_latency = 10;
	state->target_residency = 20 + 10;
	state->power_usage = 1; /* perhaps not */
	state->flags = 0;
	state->flags |= CPUIDLE_FLAG_TIME_VALID;
	shmobile_cpuidle_modes[i] = sh7372_enter_core_standby;

	dev->state_count = i + 1;
}

static void sh7372_cpuidle_init(void)
{
	shmobile_cpuidle_setup = sh7372_cpuidle_setup;
}
#else
static void sh7372_cpuidle_init(void) {}
#endif

#ifdef CONFIG_SUSPEND
static int sh7372_enter_suspend(suspend_state_t suspend_state)
{
	sh7372_enter_core_standby();
	return 0;
}

static void sh7372_suspend_init(void)
{
	shmobile_suspend_ops.enter = sh7372_enter_suspend;
}
#else
static void sh7372_suspend_init(void) {}
#endif

#define DBGREG1 0xe6100020
#define DBGREG9 0xe6100040

void __init sh7372_pm_init(void)
{
	/* enable DBG hardware block to kick SYSC */
	__raw_writel(0x0000a500, DBGREG9);
	__raw_writel(0x0000a501, DBGREG9);
	__raw_writel(0x00000000, DBGREG1);

	sh7372_suspend_init();
	sh7372_cpuidle_init();
}
