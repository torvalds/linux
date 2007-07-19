/*
 * File:         arch/blackfin/mach-common/pm.c
 * Based on:     arm/mach-omap/pm.c
 * Author:       Cliff Brake <cbrake@accelent.com> Copyright (c) 2001
 *
 * Created:      2001
 * Description:  Power management for the bfin
 *
 * Modified:     Nicolas Pitre - PXA250 support
 *                Copyright (c) 2002 Monta Vista Software, Inc.
 *               David Singleton - OMAP1510
 *                Copyright (c) 2002 Monta Vista Software, Inc.
 *               Dirk Behme <dirk.behme@de.bosch.com> - OMAP1510/1610
 *                Copyright 2004
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/pm.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/io.h>
#include <linux/irq.h>

#include <asm/dpmc.h>
#include <asm/gpio.h>

#ifdef CONFIG_PM_WAKEUP_GPIO_POLAR_H
#define WAKEUP_TYPE	PM_WAKE_HIGH
#endif

#ifdef CONFIG_PM_WAKEUP_GPIO_POLAR_L
#define WAKEUP_TYPE	PM_WAKE_LOW
#endif

#ifdef CONFIG_PM_WAKEUP_GPIO_POLAR_EDGE_F
#define WAKEUP_TYPE	PM_WAKE_FALLING
#endif

#ifdef CONFIG_PM_WAKEUP_GPIO_POLAR_EDGE_R
#define WAKEUP_TYPE	PM_WAKE_RISING
#endif

#ifdef CONFIG_PM_WAKEUP_GPIO_POLAR_EDGE_B
#define WAKEUP_TYPE	PM_WAKE_BOTH_EDGES
#endif

void bfin_pm_suspend_standby_enter(void)
{
#ifdef CONFIG_PM_WAKEUP_BY_GPIO
	gpio_pm_wakeup_request(CONFIG_PM_WAKEUP_GPIO_NUMBER, WAKEUP_TYPE);
#endif

#if defined(CONFIG_PM_WAKEUP_BY_GPIO) || defined(CONFIG_PM_WAKEUP_GPIO_API)
	{
		u32 flags;

		local_irq_save(flags);

		sleep_deeper(gpio_pm_setup()); /*Goto Sleep*/

		gpio_pm_restore();

		bfin_write_SIC_IWR(IWR_ENABLE_ALL);

		local_irq_restore(flags);
	}
#endif

#if defined(CONFIG_PM_WAKEUP_GPIO_BY_SIC_IWR)
	sleep_deeper(CONFIG_PM_WAKEUP_SIC_IWR);
	bfin_write_SIC_IWR(IWR_ENABLE_ALL);
#endif				/* CONFIG_PM_WAKEUP_GPIO_BY_SIC_IWR */
}


/*
 *	bfin_pm_prepare - Do preliminary suspend work.
 *	@state:		suspend state we're entering.
 *
 */
static int bfin_pm_prepare(suspend_state_t state)
{
	int error = 0;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		break;

	case PM_SUSPEND_MEM:
		return -ENOTSUPP;

	default:
		return -EINVAL;
	}

	return error;
}

/*
 *	bfin_pm_enter - Actually enter a sleep state.
 *	@state:		State we're entering.
 *
 */
static int bfin_pm_enter(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
		bfin_pm_suspend_standby_enter();
		break;

	case PM_SUSPEND_MEM:
		return -ENOTSUPP;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 *	bfin_pm_finish - Finish up suspend sequence.
 *	@state:		State we're coming out of.
 *
 *	This is called after we wake back up (or if entering the sleep state
 *	failed).
 */
static int bfin_pm_finish(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
		break;

	case PM_SUSPEND_MEM:
		return -ENOTSUPP;

	default:
		return -EINVAL;
	}

	return 0;
}

struct pm_ops bfin_pm_ops = {
	.prepare = bfin_pm_prepare,
	.enter = bfin_pm_enter,
	.finish = bfin_pm_finish,
};

static int __init bfin_pm_init(void)
{
	pm_set_ops(&bfin_pm_ops);
	return 0;
}

__initcall(bfin_pm_init);
