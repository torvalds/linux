/*
 * SH-Mobile Timer
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2002 - 2009  Paul Mundt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <linux/platform_device.h>
#include <asm/mach/time.h>
#include <asm/smp_twd.h>

static void __init shmobile_late_time_init(void)
{
	/*
	 * Make sure all compiled-in early timers register themselves.
	 *
	 * Run probe() for two "earlytimer" devices, these will be the
	 * clockevents and clocksource devices respectively. In the event
	 * that only a clockevents device is available, we -ENODEV on the
	 * clocksource and the jiffies clocksource is used transparently
	 * instead. No error handling is necessary here.
	 */
	early_platform_driver_register_all("earlytimer");
	early_platform_driver_probe("earlytimer", 2, 0);
}

void __init shmobile_earlytimer_init(void)
{
	late_time_init = shmobile_late_time_init;
}

static void __init shmobile_timer_init(void)
{
}

void __init shmobile_twd_init(struct twd_local_timer *twd_local_timer)
{
#ifdef CONFIG_HAVE_ARM_TWD
	int err = twd_local_timer_register(twd_local_timer);
	if (err)
		pr_err("twd_local_timer_register failed %d\n", err);
#endif
}

struct sys_timer shmobile_timer = {
	.init		= shmobile_timer_init,
};
