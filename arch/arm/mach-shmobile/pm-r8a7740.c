/*
 * r8a7740 power management support
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 * Copyright (C) 2012  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/console.h>
#include <linux/suspend.h>
#include <mach/pm-rmobile.h>
#include <mach/common.h>

#ifdef CONFIG_PM
static int r8a7740_pd_a4s_suspend(void)
{
	/*
	 * The A4S domain contains the CPU core and therefore it should
	 * only be turned off if the CPU is in use.
	 */
	return -EBUSY;
}

static int r8a7740_pd_a3sp_suspend(void)
{
	/*
	 * Serial consoles make use of SCIF hardware located in A3SP,
	 * keep such power domain on if "no_console_suspend" is set.
	 */
	return console_suspend_enabled ? 0 : -EBUSY;
}

static struct rmobile_pm_domain r8a7740_pm_domains[] = {
	{
		.genpd.name	= "A4S",
		.bit_shift	= 10,
		.gov		= &pm_domain_always_on_gov,
		.no_debug	= true,
		.suspend	= r8a7740_pd_a4s_suspend,
	},
	{
		.genpd.name	= "A3SP",
		.bit_shift	= 11,
		.gov		= &pm_domain_always_on_gov,
		.no_debug	= true,
		.suspend	= r8a7740_pd_a3sp_suspend,
	},
	{
		.genpd.name	= "A4LC",
		.bit_shift	= 1,
	},
};

void __init r8a7740_init_pm_domains(void)
{
	rmobile_init_domains(r8a7740_pm_domains, ARRAY_SIZE(r8a7740_pm_domains));
	pm_genpd_add_subdomain_names("A4S", "A3SP");
}

#endif /* CONFIG_PM */

#ifdef CONFIG_SUSPEND
static int r8a7740_enter_suspend(suspend_state_t suspend_state)
{
	cpu_do_idle();
	return 0;
}

static void r8a7740_suspend_init(void)
{
	shmobile_suspend_ops.enter = r8a7740_enter_suspend;
}
#else
static void r8a7740_suspend_init(void) {}
#endif

void __init r8a7740_pm_init(void)
{
	r8a7740_suspend_init();
}
