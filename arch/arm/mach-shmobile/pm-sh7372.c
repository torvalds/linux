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

#include <asm/cpuidle.h>
#include <asm/io.h>
#include <asm/tlbflush.h>
#include <asm/suspend.h>

#include "common.h"
#include "pm-rmobile.h"
#include "sh7372.h"

/* DBG */
#define DBGREG1 IOMEM(0xe6100020)
#define DBGREG9 IOMEM(0xe6100040)

/* CPGA */
#define SYSTBCR IOMEM(0xe6150024)
#define MSTPSR0 IOMEM(0xe6150030)
#define MSTPSR1 IOMEM(0xe6150038)
#define MSTPSR2 IOMEM(0xe6150040)
#define MSTPSR3 IOMEM(0xe6150048)
#define MSTPSR4 IOMEM(0xe615004c)
#define PLLC01STPCR IOMEM(0xe61500c8)

/* SYSC */
#define SBAR IOMEM(0xe6180020)
#define WUPRMSK IOMEM(0xe6180028)
#define WUPSMSK IOMEM(0xe618002c)
#define WUPSMSK2 IOMEM(0xe6180048)
#define WUPSFAC IOMEM(0xe6180098)
#define IRQCR IOMEM(0xe618022c)
#define IRQCR2 IOMEM(0xe6180238)
#define IRQCR3 IOMEM(0xe6180244)
#define IRQCR4 IOMEM(0xe6180248)
#define PDNSEL IOMEM(0xe6180254)

/* INTC */
#define ICR1A IOMEM(0xe6900000)
#define ICR2A IOMEM(0xe6900004)
#define ICR3A IOMEM(0xe6900008)
#define ICR4A IOMEM(0xe690000c)
#define INTMSK00A IOMEM(0xe6900040)
#define INTMSK10A IOMEM(0xe6900044)
#define INTMSK20A IOMEM(0xe6900048)
#define INTMSK30A IOMEM(0xe690004c)

/* MFIS */
/* FIXME: pointing where? */
#define SMFRAM 0xe6a70000

/* AP-System Core */
#define APARMBAREA IOMEM(0xe6f10020)

#ifdef CONFIG_PM

#define PM_DOMAIN_ON_OFF_LATENCY_NS	250000

static int sh7372_a4r_pd_suspend(void)
{
	sh7372_intcs_suspend();
	__raw_writel(0x300fffff, WUPRMSK); /* avoid wakeup */
	return 0;
}

static bool a4s_suspend_ready;

static int sh7372_a4s_pd_suspend(void)
{
	/*
	 * The A4S domain contains the CPU core and therefore it should
	 * only be turned off if the CPU is not in use.  This may happen
	 * during system suspend, when SYSC is going to be used for generating
	 * resume signals and a4s_suspend_ready is set to let
	 * sh7372_enter_suspend() know that it can turn A4S off.
	 */
	a4s_suspend_ready = true;
	return -EBUSY;
}

static void sh7372_a4s_pd_resume(void)
{
	a4s_suspend_ready = false;
}

static int sh7372_a3sp_pd_suspend(void)
{
	/*
	 * Serial consoles make use of SCIF hardware located in A3SP,
	 * keep such power domain on if "no_console_suspend" is set.
	 */
	return console_suspend_enabled ? 0 : -EBUSY;
}

static struct rmobile_pm_domain sh7372_pm_domains[] = {
	{
		.genpd.name = "A4LC",
		.genpd.power_on_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.genpd.power_off_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.bit_shift = 1,
	},
	{
		.genpd.name = "A4MP",
		.genpd.power_on_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.genpd.power_off_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.bit_shift = 2,
	},
	{
		.genpd.name = "D4",
		.genpd.power_on_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.genpd.power_off_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.bit_shift = 3,
	},
	{
		.genpd.name = "A4R",
		.genpd.power_on_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.genpd.power_off_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.bit_shift = 5,
		.suspend = sh7372_a4r_pd_suspend,
		.resume = sh7372_intcs_resume,
	},
	{
		.genpd.name = "A3RV",
		.genpd.power_on_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.genpd.power_off_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.bit_shift = 6,
	},
	{
		.genpd.name = "A3RI",
		.genpd.power_on_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.genpd.power_off_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.bit_shift = 8,
	},
	{
		.genpd.name = "A4S",
		.genpd.power_on_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.genpd.power_off_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.bit_shift = 10,
		.gov = &pm_domain_always_on_gov,
		.no_debug = true,
		.suspend = sh7372_a4s_pd_suspend,
		.resume = sh7372_a4s_pd_resume,
	},
	{
		.genpd.name = "A3SP",
		.genpd.power_on_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.genpd.power_off_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.bit_shift = 11,
		.gov = &pm_domain_always_on_gov,
		.no_debug = true,
		.suspend = sh7372_a3sp_pd_suspend,
	},
	{
		.genpd.name = "A3SG",
		.genpd.power_on_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.genpd.power_off_latency_ns = PM_DOMAIN_ON_OFF_LATENCY_NS,
		.bit_shift = 13,
	},
};

void __init sh7372_init_pm_domains(void)
{
	rmobile_init_domains(sh7372_pm_domains, ARRAY_SIZE(sh7372_pm_domains));
	pm_genpd_add_subdomain_names("A4LC", "A3RV");
	pm_genpd_add_subdomain_names("A4R", "A4LC");
	pm_genpd_add_subdomain_names("A4S", "A3SG");
	pm_genpd_add_subdomain_names("A4S", "A3SP");
}

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

static void sh7372_enter_a4s_common(int pllc0_on)
{
	sh7372_intca_suspend();
	sh7372_set_reset_vector(SMFRAM);
	sh7372_enter_sysc(pllc0_on, 1 << 10);
	sh7372_intca_resume();
}

static void sh7372_pm_setup_smfram(void)
{
	/* pass physical address of cpu_resume() to assembly resume code */
	sh7372_cpu_resume = virt_to_phys(cpu_resume);

	memcpy((void *)SMFRAM, sh7372_resume_core_standby_sysc, 0x100);
}
#else
static inline void sh7372_pm_setup_smfram(void) {}
#endif /* CONFIG_SUSPEND || CONFIG_CPU_IDLE */

#ifdef CONFIG_CPU_IDLE
static int sh7372_do_idle_core_standby(unsigned long unused)
{
	cpu_do_idle(); /* WFI when SYSTBCR == 0x10 -> Core Standby */
	return 0;
}

static int sh7372_enter_core_standby(struct cpuidle_device *dev,
				     struct cpuidle_driver *drv, int index)
{
	sh7372_set_reset_vector(__pa(sh7372_resume_core_standby_sysc));

	/* enter sleep mode with SYSTBCR to 0x10 */
	__raw_writel(0x10, SYSTBCR);
	cpu_suspend(0, sh7372_do_idle_core_standby);
	__raw_writel(0, SYSTBCR);

	 /* disable reset vector translation */
	__raw_writel(0, SBAR);

	return 1;
}

static int sh7372_enter_a3sm_pll_on(struct cpuidle_device *dev,
				    struct cpuidle_driver *drv, int index)
{
	sh7372_enter_a3sm_common(1);
	return 2;
}

static int sh7372_enter_a3sm_pll_off(struct cpuidle_device *dev,
				     struct cpuidle_driver *drv, int index)
{
	sh7372_enter_a3sm_common(0);
	return 3;
}

static int sh7372_enter_a4s(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	unsigned long msk, msk2;

	if (!sh7372_sysc_valid(&msk, &msk2))
		return sh7372_enter_a3sm_pll_off(dev, drv, index);

	sh7372_setup_sysc(msk, msk2);
	sh7372_enter_a4s_common(0);
	return 4;
}

static struct cpuidle_driver sh7372_cpuidle_driver = {
	.name			= "sh7372_cpuidle",
	.owner			= THIS_MODULE,
	.state_count		= 5,
	.safe_state_index	= 0, /* C1 */
	.states[0] = ARM_CPUIDLE_WFI_STATE,
	.states[1] = {
		.name = "C2",
		.desc = "Core Standby Mode",
		.exit_latency = 10,
		.target_residency = 20 + 10,
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.enter = sh7372_enter_core_standby,
	},
	.states[2] = {
		.name = "C3",
		.desc = "A3SM PLL ON",
		.exit_latency = 20,
		.target_residency = 30 + 20,
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.enter = sh7372_enter_a3sm_pll_on,
	},
	.states[3] = {
		.name = "C4",
		.desc = "A3SM PLL OFF",
		.exit_latency = 120,
		.target_residency = 30 + 120,
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.enter = sh7372_enter_a3sm_pll_off,
	},
	.states[4] = {
		.name = "C5",
		.desc = "A4S PLL OFF",
		.exit_latency = 240,
		.target_residency = 30 + 240,
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.enter = sh7372_enter_a4s,
		.disabled = true,
	},
};

static void __init sh7372_cpuidle_init(void)
{
	shmobile_cpuidle_set_driver(&sh7372_cpuidle_driver);
}
#else
static void __init sh7372_cpuidle_init(void) {}
#endif

#ifdef CONFIG_SUSPEND
static int sh7372_enter_suspend(suspend_state_t suspend_state)
{
	unsigned long msk, msk2;

	/* check active clocks to determine potential wakeup sources */
	if (sh7372_sysc_valid(&msk, &msk2) && a4s_suspend_ready) {
		/* convert INTC mask/sense to SYSC mask/sense */
		sh7372_setup_sysc(msk, msk2);

		/* enter A4S sleep with PLLC0 off */
		pr_debug("entering A4S\n");
		sh7372_enter_a4s_common(0);
		return 0;
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
		pm_genpd_name_poweron("A4R");
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

void __init sh7372_pm_init(void)
{
	/* enable DBG hardware block to kick SYSC */
	__raw_writel(0x0000a500, DBGREG9);
	__raw_writel(0x0000a501, DBGREG9);
	__raw_writel(0x00000000, DBGREG1);

	/* do not convert A3SM, A3SP, A3SG, A4R power down into A4S */
	__raw_writel(0, PDNSEL);

	sh7372_pm_setup_smfram();

	sh7372_suspend_init();
	sh7372_cpuidle_init();
}

void __init sh7372_pm_init_late(void)
{
	shmobile_init_late();
	pm_genpd_name_attach_cpuidle("A4S", 4);
}
