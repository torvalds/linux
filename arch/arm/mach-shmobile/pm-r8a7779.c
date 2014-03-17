/*
 * r8a7779 Power management support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/err.h>
#include <linux/pm_clock.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <asm/io.h>
#include <mach/common.h>
#include <mach/pm-rcar.h>
#include <mach/r8a7779.h>

/* SYSC */
#define SYSCIER 0x0c
#define SYSCIMR 0x10

#if defined(CONFIG_PM) || defined(CONFIG_SMP)

static void __init r8a7779_sysc_init(void)
{
	void __iomem *base = rcar_sysc_init(0xffd85000);

	/* enable all interrupt sources, but do not use interrupt handler */
	iowrite32(0x0131000e, base + SYSCIER);
	iowrite32(0, base + SYSCIMR);
}

#else /* CONFIG_PM || CONFIG_SMP */

static inline void r8a7779_sysc_init(void) {}

#endif /* CONFIG_PM || CONFIG_SMP */

#ifdef CONFIG_PM

static int pd_power_down(struct generic_pm_domain *genpd)
{
	return rcar_sysc_power_down(to_r8a7779_ch(genpd));
}

static int pd_power_up(struct generic_pm_domain *genpd)
{
	return rcar_sysc_power_up(to_r8a7779_ch(genpd));
}

static bool pd_is_off(struct generic_pm_domain *genpd)
{
	return rcar_sysc_power_is_off(to_r8a7779_ch(genpd));
}

static bool pd_active_wakeup(struct device *dev)
{
	return true;
}

static void r8a7779_init_pm_domain(struct r8a7779_pm_domain *r8a7779_pd)
{
	struct generic_pm_domain *genpd = &r8a7779_pd->genpd;

	pm_genpd_init(genpd, NULL, false);
	genpd->dev_ops.stop = pm_clk_suspend;
	genpd->dev_ops.start = pm_clk_resume;
	genpd->dev_ops.active_wakeup = pd_active_wakeup;
	genpd->dev_irq_safe = true;
	genpd->power_off = pd_power_down;
	genpd->power_on = pd_power_up;

	if (pd_is_off(&r8a7779_pd->genpd))
		pd_power_up(&r8a7779_pd->genpd);
}

static struct r8a7779_pm_domain r8a7779_pm_domains[] = {
	{
		.genpd.name = "SH4A",
		.ch = {
			.chan_offs = 0x80, /* PWRSR1 .. PWRER1 */
			.isr_bit = 16, /* SH4A */
		},
	},
	{
		.genpd.name = "SGX",
		.ch = {
			.chan_offs = 0xc0, /* PWRSR2 .. PWRER2 */
			.isr_bit = 20, /* SGX */
		},
	},
	{
		.genpd.name = "VDP1",
		.ch = {
			.chan_offs = 0x100, /* PWRSR3 .. PWRER3 */
			.isr_bit = 21, /* VDP */
		},
	},
	{
		.genpd.name = "IMPX3",
		.ch = {
			.chan_offs = 0x140, /* PWRSR4 .. PWRER4 */
			.isr_bit = 24, /* IMP */
		},
	},
};

void __init r8a7779_init_pm_domains(void)
{
	int j;

	for (j = 0; j < ARRAY_SIZE(r8a7779_pm_domains); j++)
		r8a7779_init_pm_domain(&r8a7779_pm_domains[j]);
}

#endif /* CONFIG_PM */

void __init r8a7779_pm_init(void)
{
	static int once;

	if (!once++)
		r8a7779_sysc_init();
}
