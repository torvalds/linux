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
#endif /* CONFIG_PM */
