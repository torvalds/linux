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
#include <linux/pm_clock.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/bitrev.h>
#include <linux/console.h>
#include <asm/io.h>
#include <asm/tlbflush.h>
#include <asm/suspend.h>
#include <mach/common.h>
#include <mach/sh7372.h>
#include <mach/pm-rmobile.h>

/* DBG */
#define DBGREG1 0xe6100020
#define DBGREG9 0xe6100040

/* CPGA */
#define SYSTBCR 0xe6150024
#define MSTPSR0 0xe6150030
#define MSTPSR1 0xe6150038
#define MSTPSR2 0xe6150040
#define MSTPSR3 0xe6150048
#define MSTPSR4 0xe615004c
#define PLLC01STPCR 0xe61500c8

/* SYSC */
#define SPDCR 0xe6180008
#define SWUCR 0xe6180014
#define SBAR 0xe6180020
#define WUPRMSK 0xe6180028
#define WUPSMSK 0xe618002c
#define WUPSMSK2 0xe6180048
#define PSTR 0xe6180080
#define WUPSFAC 0xe6180098
#define IRQCR 0xe618022c
#define IRQCR2 0xe6180238
#define IRQCR3 0xe6180244
#define IRQCR4 0xe6180248
#define PDNSEL 0xe6180254

/* INTC */
#define ICR1A 0xe6900000
#define ICR2A 0xe6900004
#define ICR3A 0xe6900008
#define ICR4A 0xe690000c
#define INTMSK00A 0xe6900040
#define INTMSK10A 0xe6900044
#define INTMSK20A 0xe6900048
#define INTMSK30A 0xe690004c

/* MFIS */
#define SMFRAM 0xe6a70000

/* AP-System Core */
#define APARMBAREA 0xe6f10020

#define PSTR_RETRIES 100
#define PSTR_DELAY_US 10

#ifdef CONFIG_PM

static int pd_power_down(struct generic_pm_domain *genpd)
{
	struct sh7372_pm_domain *sh7372_pd = to_sh7372_pd(genpd);
	unsigned int mask = 1 << sh7372_pd->bit_shift;

	if (sh7372_pd->suspend) {
		int ret = sh7372_pd->suspend();

		if (ret)
			return ret;
	}

	if (__raw_readl(PSTR) & mask) {
		unsigned int retry_count;

		__raw_writel(mask, SPDCR);

		for (retry_count = PSTR_RETRIES; retry_count; retry_count--) {
			if (!(__raw_readl(SPDCR) & mask))
				break;
			cpu_relax();
		}
	}

	if (!sh7372_pd->no_debug)
		pr_debug("%s: Power off, 0x%08x -> PSTR = 0x%08x\n",
			 genpd->name, mask, __raw_readl(PSTR));

	return 0;
}

static int __pd_power_up(struct sh7372_pm_domain *sh7372_pd, bool do_resume)
{
	unsigned int mask = 1 << sh7372_pd->bit_shift;
	unsigned int retry_count;
	int ret = 0;

	if (__raw_readl(PSTR) & mask)
		goto out;

	__raw_writel(mask, SWUCR);

	for (retry_count = 2 * PSTR_RETRIES; retry_count; retry_count--) {
		if (!(__raw_readl(SWUCR) & mask))
			break;
		if (retry_count > PSTR_RETRIES)
			udelay(PSTR_DELAY_US);
		else
			cpu_relax();
	}
	if (!retry_count)
		ret = -EIO;

	if (!sh7372_pd->no_debug)
		pr_debug("%s: Power on, 0x%08x -> PSTR = 0x%08x\n",
			 sh7372_pd->genpd.name, mask, __raw_readl(PSTR));

 out:
	if (ret == 0 && sh7372_pd->resume && do_resume)
		sh7372_pd->resume();

	return ret;
}

static int pd_power_up(struct generic_pm_domain *genpd)
{
	 return __pd_power_up(to_sh7372_pd(genpd), true);
}

static int sh7372_a4r_suspend(void)
{
	sh7372_intcs_suspend();
	__raw_writel(0x300fffff, WUPRMSK); /* avoid wakeup */
	return 0;
}

static bool pd_active_wakeup(struct device *dev)
{
	bool (*active_wakeup)(struct device *dev);

	active_wakeup = dev_gpd_data(dev)->ops.active_wakeup;
	return active_wakeup ? active_wakeup(dev) : true;
}

static int sh7372_stop_dev(struct device *dev)
{
	int (*stop)(struct device *dev);

	stop = dev_gpd_data(dev)->ops.stop;
	if (stop) {
		int ret = stop(dev);
		if (ret)
			return ret;
	}
	return pm_clk_suspend(dev);
}

static int sh7372_start_dev(struct device *dev)
{
	int (*start)(struct device *dev);
	int ret;

	ret = pm_clk_resume(dev);
	if (ret)
		return ret;

	start = dev_gpd_data(dev)->ops.start;
	if (start)
		ret = start(dev);

	return ret;
}

void sh7372_init_pm_domain(struct sh7372_pm_domain *sh7372_pd)
{
	struct generic_pm_domain *genpd = &sh7372_pd->genpd;
	struct dev_power_governor *gov = sh7372_pd->gov;

	pm_genpd_init(genpd, gov ? : &simple_qos_governor, false);
	genpd->dev_ops.stop = sh7372_stop_dev;
	genpd->dev_ops.start = sh7372_start_dev;
	genpd->dev_ops.active_wakeup = pd_active_wakeup;
	genpd->dev_irq_safe = true;
	genpd->power_off = pd_power_down;
	genpd->power_on = pd_power_up;
	__pd_power_up(sh7372_pd, false);
}

void sh7372_add_device_to_domain(struct sh7372_pm_domain *sh7372_pd,
				 struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pm_genpd_add_device(&sh7372_pd->genpd, dev);
	if (pm_clk_no_clocks(dev))
		pm_clk_add(dev, NULL);
}

void sh7372_pm_add_subdomain(struct sh7372_pm_domain *sh7372_pd,
			     struct sh7372_pm_domain *sh7372_sd)
{
	pm_genpd_add_subdomain(&sh7372_pd->genpd, &sh7372_sd->genpd);
}

struct sh7372_pm_domain sh7372_a4lc = {
	.genpd.name = "A4LC",
	.bit_shift = 1,
};

struct sh7372_pm_domain sh7372_a4mp = {
	.genpd.name = "A4MP",
	.bit_shift = 2,
};

struct sh7372_pm_domain sh7372_d4 = {
	.genpd.name = "D4",
	.bit_shift = 3,
};

struct sh7372_pm_domain sh7372_a4r = {
	.genpd.name = "A4R",
	.bit_shift = 5,
	.suspend = sh7372_a4r_suspend,
	.resume = sh7372_intcs_resume,
};

struct sh7372_pm_domain sh7372_a3rv = {
	.genpd.name = "A3RV",
	.bit_shift = 6,
};

struct sh7372_pm_domain sh7372_a3ri = {
	.genpd.name = "A3RI",
	.bit_shift = 8,
};

static int sh7372_a4s_suspend(void)
{
	/*
	 * The A4S domain contains the CPU core and therefore it should
	 * only be turned off if the CPU is in use.
	 */
	return -EBUSY;
}

struct sh7372_pm_domain sh7372_a4s = {
	.genpd.name = "A4S",
	.bit_shift = 10,
	.gov = &pm_domain_always_on_gov,
	.no_debug = true,
	.suspend = sh7372_a4s_suspend,
};

static int sh7372_a3sp_suspend(void)
{
	/*
	 * Serial consoles make use of SCIF hardware located in A3SP,
	 * keep such power domain on if "no_console_suspend" is set.
	 */
	return console_suspend_enabled ? 0 : -EBUSY;
}

struct sh7372_pm_domain sh7372_a3sp = {
	.genpd.name = "A3SP",
	.bit_shift = 11,
	.gov = &pm_domain_always_on_gov,
	.no_debug = true,
	.suspend = sh7372_a3sp_suspend,
};

struct sh7372_pm_domain sh7372_a3sg = {
	.genpd.name = "A3SG",
	.bit_shift = 13,
};

struct rmobile_pm_domain sh7372_pd_a4lc = {
	.genpd.name = "A4LC",
	.bit_shift = 1,
};

struct rmobile_pm_domain sh7372_pd_a4mp = {
	.genpd.name = "A4MP",
	.bit_shift = 2,
};

struct rmobile_pm_domain sh7372_pd_d4 = {
	.genpd.name = "D4",
	.bit_shift = 3,
};

static int sh7372_a4r_pd_suspend(void)
{
	sh7372_intcs_suspend();
	__raw_writel(0x300fffff, WUPRMSK); /* avoid wakeup */
	return 0;
}

struct rmobile_pm_domain sh7372_pd_a4r = {
	.genpd.name = "A4R",
	.bit_shift = 5,
	.suspend = sh7372_a4r_pd_suspend,
	.resume = sh7372_intcs_resume,
};

struct rmobile_pm_domain sh7372_pd_a3rv = {
	.genpd.name = "A3RV",
	.bit_shift = 6,
};

struct rmobile_pm_domain sh7372_pd_a3ri = {
	.genpd.name = "A3RI",
	.bit_shift = 8,
};

static int sh7372_pd_a4s_suspend(void)
{
	/*
	 * The A4S domain contains the CPU core and therefore it should
	 * only be turned off if the CPU is in use.
	 */
	return -EBUSY;
}

struct rmobile_pm_domain sh7372_pd_a4s = {
	.genpd.name = "A4S",
	.bit_shift = 10,
	.gov = &pm_domain_always_on_gov,
	.no_debug = true,
	.suspend = sh7372_pd_a4s_suspend,
};

static int sh7372_a3sp_pd_suspend(void)
{
	/*
	 * Serial consoles make use of SCIF hardware located in A3SP,
	 * keep such power domain on if "no_console_suspend" is set.
	 */
	return console_suspend_enabled ? 0 : -EBUSY;
}

struct rmobile_pm_domain sh7372_pd_a3sp = {
	.genpd.name = "A3SP",
	.bit_shift = 11,
	.gov = &pm_domain_always_on_gov,
	.no_debug = true,
	.suspend = sh7372_a3sp_pd_suspend,
};

struct rmobile_pm_domain sh7372_pd_a3sg = {
	.genpd.name = "A3SG",
	.bit_shift = 13,
};

#endif /* CONFIG_PM */

#if defined(CONFIG_SUSPEND) || defined(CONFIG_CPU_IDLE)
static void sh7372_set_reset_vector(unsigned long address)
{
	/* set reset vector, translate 4k */
	__raw_writel(address, SBAR);
	__raw_writel(0, APARMBAREA);
}

static void sh7372_enter_sysc(int pllc0_on, unsigned long sleep_mode)
{
	if (pllc0_on)
		__raw_writel(0, PLLC01STPCR);
	else
		__raw_writel(1 << 28, PLLC01STPCR);

	__raw_readl(WUPSFAC); /* read wakeup int. factor before sleep */
	cpu_suspend(sleep_mode, sh7372_do_idle_sysc);
	__raw_readl(WUPSFAC); /* read wakeup int. factor after wakeup */

	 /* disable reset vector translation */
	__raw_writel(0, SBAR);
}

static int sh7372_sysc_valid(unsigned long *mskp, unsigned long *msk2p)
{
	unsigned long mstpsr0, mstpsr1, mstpsr2, mstpsr3, mstpsr4;
	unsigned long msk, msk2;

	/* check active clocks to determine potential wakeup sources */

	mstpsr0 = __raw_readl(MSTPSR0);
	if ((mstpsr0 & 0x00000003) != 0x00000003) {
		pr_debug("sh7372 mstpsr0 0x%08lx\n", mstpsr0);
		return 0;
	}

	mstpsr1 = __raw_readl(MSTPSR1);
	if ((mstpsr1 & 0xff079b7f) != 0xff079b7f) {
		pr_debug("sh7372 mstpsr1 0x%08lx\n", mstpsr1);
		return 0;
	}

	mstpsr2 = __raw_readl(MSTPSR2);
	if ((mstpsr2 & 0x000741ff) != 0x000741ff) {
		pr_debug("sh7372 mstpsr2 0x%08lx\n", mstpsr2);
		return 0;
	}

	mstpsr3 = __raw_readl(MSTPSR3);
	if ((mstpsr3 & 0x1a60f010) != 0x1a60f010) {
		pr_debug("sh7372 mstpsr3 0x%08lx\n", mstpsr3);
		return 0;
	}

	mstpsr4 = __raw_readl(MSTPSR4);
	if ((mstpsr4 & 0x00008cf0) != 0x00008cf0) {
		pr_debug("sh7372 mstpsr4 0x%08lx\n", mstpsr4);
		return 0;
	}

	msk = 0;
	msk2 = 0;

	/* make bitmaps of limited number of wakeup sources */

	if ((mstpsr2 & (1 << 23)) == 0) /* SPU2 */
		msk |= 1 << 31;

	if ((mstpsr2 & (1 << 12)) == 0) /* MFI_MFIM */
		msk |= 1 << 21;

	if ((mstpsr4 & (1 << 3)) == 0) /* KEYSC */
		msk |= 1 << 2;

	if ((mstpsr1 & (1 << 24)) == 0) /* CMT0 */
		msk |= 1 << 1;

	if ((mstpsr3 & (1 << 29)) == 0) /* CMT1 */
		msk |= 1 << 1;

	if ((mstpsr4 & (1 << 0)) == 0) /* CMT2 */
		msk |= 1 << 1;

	if ((mstpsr2 & (1 << 13)) == 0) /* MFI_MFIS */
		msk2 |= 1 << 17;

	*mskp = msk;
	*msk2p = msk2;

	return 1;
}

static void sh7372_icr_to_irqcr(unsigned long icr, u16 *irqcr1p, u16 *irqcr2p)
{
	u16 tmp, irqcr1, irqcr2;
	int k;

	irqcr1 = 0;
	irqcr2 = 0;

	/* convert INTCA ICR register layout to SYSC IRQCR+IRQCR2 */
	for (k = 0; k <= 7; k++) {
		tmp = (icr >> ((7 - k) * 4)) & 0xf;
		irqcr1 |= (tmp & 0x03) << (k * 2);
		irqcr2 |= (tmp >> 2) << (k * 2);
	}

	*irqcr1p = irqcr1;
	*irqcr2p = irqcr2;
}

static void sh7372_setup_sysc(unsigned long msk, unsigned long msk2)
{
	u16 irqcrx_low, irqcrx_high, irqcry_low, irqcry_high;
	unsigned long tmp;

	/* read IRQ0A -> IRQ15A mask */
	tmp = bitrev8(__raw_readb(INTMSK00A));
	tmp |= bitrev8(__raw_readb(INTMSK10A)) << 8;

	/* setup WUPSMSK from clocks and external IRQ mask */
	msk = (~msk & 0xc030000f) | (tmp << 4);
	__raw_writel(msk, WUPSMSK);

	/* propage level/edge trigger for external IRQ 0->15 */
	sh7372_icr_to_irqcr(__raw_readl(ICR1A), &irqcrx_low, &irqcry_low);
	sh7372_icr_to_irqcr(__raw_readl(ICR2A), &irqcrx_high, &irqcry_high);
	__raw_writel((irqcrx_high << 16) | irqcrx_low, IRQCR);
	__raw_writel((irqcry_high << 16) | irqcry_low, IRQCR2);

	/* read IRQ16A -> IRQ31A mask */
	tmp = bitrev8(__raw_readb(INTMSK20A));
	tmp |= bitrev8(__raw_readb(INTMSK30A)) << 8;

	/* setup WUPSMSK2 from clocks and external IRQ mask */
	msk2 = (~msk2 & 0x00030000) | tmp;
	__raw_writel(msk2, WUPSMSK2);

	/* propage level/edge trigger for external IRQ 16->31 */
	sh7372_icr_to_irqcr(__raw_readl(ICR3A), &irqcrx_low, &irqcry_low);
	sh7372_icr_to_irqcr(__raw_readl(ICR4A), &irqcrx_high, &irqcry_high);
	__raw_writel((irqcrx_high << 16) | irqcrx_low, IRQCR3);
	__raw_writel((irqcry_high << 16) | irqcry_low, IRQCR4);
}

static void sh7372_enter_a3sm_common(int pllc0_on)
{
	/* use INTCA together with SYSC for wakeup */
	sh7372_setup_sysc(1 << 0, 0);
	sh7372_set_reset_vector(__pa(sh7372_resume_core_standby_sysc));
	sh7372_enter_sysc(pllc0_on, 1 << 12);
}

#ifdef CONFIG_CPU_IDLE
static int sh7372_do_idle_core_standby(unsigned long unused)
{
	cpu_do_idle(); /* WFI when SYSTBCR == 0x10 -> Core Standby */
	return 0;
}

static void sh7372_enter_core_standby(void)
{
	sh7372_set_reset_vector(__pa(sh7372_resume_core_standby_sysc));

	/* enter sleep mode with SYSTBCR to 0x10 */
	__raw_writel(0x10, SYSTBCR);
	cpu_suspend(0, sh7372_do_idle_core_standby);
	__raw_writel(0, SYSTBCR);

	 /* disable reset vector translation */
	__raw_writel(0, SBAR);
}

static void sh7372_enter_a3sm_pll_on(void)
{
	sh7372_enter_a3sm_common(1);
}

static void sh7372_enter_a3sm_pll_off(void)
{
	sh7372_enter_a3sm_common(0);
}

static void sh7372_cpuidle_setup(struct cpuidle_driver *drv)
{
	struct cpuidle_state *state = &drv->states[drv->state_count];

	snprintf(state->name, CPUIDLE_NAME_LEN, "C2");
	strncpy(state->desc, "Core Standby Mode", CPUIDLE_DESC_LEN);
	state->exit_latency = 10;
	state->target_residency = 20 + 10;
	state->flags = CPUIDLE_FLAG_TIME_VALID;
	shmobile_cpuidle_modes[drv->state_count] = sh7372_enter_core_standby;
	drv->state_count++;

	state = &drv->states[drv->state_count];
	snprintf(state->name, CPUIDLE_NAME_LEN, "C3");
	strncpy(state->desc, "A3SM PLL ON", CPUIDLE_DESC_LEN);
	state->exit_latency = 20;
	state->target_residency = 30 + 20;
	state->flags = CPUIDLE_FLAG_TIME_VALID;
	shmobile_cpuidle_modes[drv->state_count] = sh7372_enter_a3sm_pll_on;
	drv->state_count++;

	state = &drv->states[drv->state_count];
	snprintf(state->name, CPUIDLE_NAME_LEN, "C4");
	strncpy(state->desc, "A3SM PLL OFF", CPUIDLE_DESC_LEN);
	state->exit_latency = 120;
	state->target_residency = 30 + 120;
	state->flags = CPUIDLE_FLAG_TIME_VALID;
	shmobile_cpuidle_modes[drv->state_count] = sh7372_enter_a3sm_pll_off;
	drv->state_count++;
}

static void sh7372_cpuidle_init(void)
{
	shmobile_cpuidle_setup = sh7372_cpuidle_setup;
}
#else
static void sh7372_cpuidle_init(void) {}
#endif

#ifdef CONFIG_SUSPEND
static void sh7372_enter_a4s_common(int pllc0_on)
{
	sh7372_intca_suspend();
	memcpy((void *)SMFRAM, sh7372_resume_core_standby_sysc, 0x100);
	sh7372_set_reset_vector(SMFRAM);
	sh7372_enter_sysc(pllc0_on, 1 << 10);
	sh7372_intca_resume();
}

static int sh7372_enter_suspend(suspend_state_t suspend_state)
{
	unsigned long msk, msk2;

	/* check active clocks to determine potential wakeup sources */
	if (sh7372_sysc_valid(&msk, &msk2)) {
		if (!console_suspend_enabled &&
		    sh7372_pd_a4s.genpd.status == GPD_STATE_POWER_OFF) {
			/* convert INTC mask/sense to SYSC mask/sense */
			sh7372_setup_sysc(msk, msk2);

			/* enter A4S sleep with PLLC0 off */
			pr_debug("entering A4S\n");
			sh7372_enter_a4s_common(0);
			return 0;
		}
	}

	/* default to enter A3SM sleep with PLLC0 off */
	pr_debug("entering A3SM\n");
	sh7372_enter_a3sm_common(0);
	return 0;
}

/**
 * sh7372_pm_notifier_fn - SH7372 PM notifier routine.
 * @notifier: Unused.
 * @pm_event: Event being handled.
 * @unused: Unused.
 */
static int sh7372_pm_notifier_fn(struct notifier_block *notifier,
				 unsigned long pm_event, void *unused)
{
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		/*
		 * This is necessary, because the A4R domain has to be "on"
		 * when suspend_device_irqs() and resume_device_irqs() are
		 * executed during system suspend and resume, respectively, so
		 * that those functions don't crash while accessing the INTCS.
		 */
		pm_genpd_poweron(&sh7372_pd_a4r.genpd);
		break;
	case PM_POST_SUSPEND:
		pm_genpd_poweroff_unused();
		break;
	}

	return NOTIFY_DONE;
}

static void sh7372_suspend_init(void)
{
	shmobile_suspend_ops.enter = sh7372_enter_suspend;
	pm_notifier(sh7372_pm_notifier_fn, 0);
}
#else
static void sh7372_suspend_init(void) {}
#endif
#endif /* CONFIG_SUSPEND || CONFIG_CPU_IDLE */

void __init sh7372_pm_init(void)
{
	/* enable DBG hardware block to kick SYSC */
	__raw_writel(0x0000a500, DBGREG9);
	__raw_writel(0x0000a501, DBGREG9);
	__raw_writel(0x00000000, DBGREG1);

	/* do not convert A3SM, A3SP, A3SG, A4R power down into A4S */
	__raw_writel(0, PDNSEL);

	sh7372_suspend_init();
	sh7372_cpuidle_init();
}
