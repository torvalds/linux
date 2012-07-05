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
#include <mach/pm-rmobile.h>

#ifdef CONFIG_PM
static int r8a7740_pd_a4s_suspend(void)
{
	/*
	 * The A4S domain contains the CPU core and therefore it should
	 * only be turned off if the CPU is in use.
	 */
	return -EBUSY;
}

struct rmobile_pm_domain r8a7740_pd_a4s = {
	.genpd.name	= "A4S",
	.bit_shift	= 10,
	.gov		= &pm_domain_always_on_gov,
	.no_debug	= true,
	.suspend	= r8a7740_pd_a4s_suspend,
};

static int r8a7740_pd_a3sp_suspend(void)
{
	/*
	 * Serial consoles make use of SCIF hardware located in A3SP,
	 * keep such power domain on if "no_console_suspend" is set.
	 */
	return console_suspend_enabled ? 0 : -EBUSY;
}

struct rmobile_pm_domain r8a7740_pd_a3sp = {
	.genpd.name	= "A3SP",
	.bit_shift	= 11,
	.gov		= &pm_domain_always_on_gov,
	.no_debug	= true,
	.suspend	= r8a7740_pd_a3sp_suspend,
};

#endif /* CONFIG_PM */
