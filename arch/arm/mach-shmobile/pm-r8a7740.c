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
#include <linux/io.h>
#include <linux/suspend.h>

#include "common.h"
#include "pm-rmobile.h"

#define SYSC_BASE	IOMEM(0xe6180000)

#if defined(CONFIG_PM) && !defined(CONFIG_ARCH_MULTIPLATFORM)
static int r8a7740_pd_a3sm_suspend(void)
{
	/*
	 * The A3SM domain contains the CPU core and therefore it should
	 * only be turned off if the CPU is not in use.
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

static int r8a7740_pd_d4_suspend(void)
{
	/*
	 * The D4 domain contains the Coresight-ETM hardware block and
	 * therefore it should only be turned off if the debug module is
	 * not in use.
	 */
	return -EBUSY;
}

static struct rmobile_pm_domain r8a7740_pm_domains[] = {
	{
		.genpd.name	= "A4LC",
		.base		= SYSC_BASE,
		.bit_shift	= 1,
	}, {
		.genpd.name	= "A4MP",
		.base		= SYSC_BASE,
		.bit_shift	= 2,
	}, {
		.genpd.name	= "D4",
		.base		= SYSC_BASE,
		.bit_shift	= 3,
		.gov		= &pm_domain_always_on_gov,
		.suspend	= r8a7740_pd_d4_suspend,
	}, {
		.genpd.name	= "A4R",
		.base		= SYSC_BASE,
		.bit_shift	= 5,
	}, {
		.genpd.name	= "A3RV",
		.base		= SYSC_BASE,
		.bit_shift	= 6,
	}, {
		.genpd.name	= "A4S",
		.base		= SYSC_BASE,
		.bit_shift	= 10,
		.no_debug	= true,
	}, {
		.genpd.name	= "A3SP",
		.base		= SYSC_BASE,
		.bit_shift	= 11,
		.gov		= &pm_domain_always_on_gov,
		.no_debug	= true,
		.suspend	= r8a7740_pd_a3sp_suspend,
	}, {
		.genpd.name	= "A3SM",
		.base		= SYSC_BASE,
		.bit_shift	= 12,
		.gov		= &pm_domain_always_on_gov,
		.suspend	= r8a7740_pd_a3sm_suspend,
	}, {
		.genpd.name	= "A3SG",
		.base		= SYSC_BASE,
		.bit_shift	= 13,
	}, {
		.genpd.name	= "A4SU",
		.base		= SYSC_BASE,
		.bit_shift	= 20,
	},
};

void __init r8a7740_init_pm_domains(void)
{
	rmobile_init_domains(r8a7740_pm_domains, ARRAY_SIZE(r8a7740_pm_domains));
	pm_genpd_add_subdomain_names("A4R", "A3RV");
	pm_genpd_add_subdomain_names("A4S", "A3SP");
	pm_genpd_add_subdomain_names("A4S", "A3SM");
	pm_genpd_add_subdomain_names("A4S", "A3SG");
}
#endif /* CONFIG_PM && !CONFIG_ARCH_MULTIPLATFORM */

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
