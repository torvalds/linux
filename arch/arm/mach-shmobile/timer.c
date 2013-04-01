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
#include <linux/delay.h>
#include <asm/mach/time.h>
#include <asm/smp_twd.h>

void __init shmobile_setup_delay(unsigned int max_cpu_core_mhz,
				 unsigned int mult, unsigned int div)
{
	/* calculate a worst-case loops-per-jiffy value
	 * based on maximum cpu core mhz setting and the
	 * __delay() implementation in arch/arm/lib/delay.S
	 *
	 * this will result in a longer delay than expected
	 * when the cpu core runs on lower frequencies.
	 */

	unsigned int value = (1000000 * mult) / (HZ * div);

	if (!preset_lpj)
		preset_lpj = max_cpu_core_mhz * value;
}

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

void __init shmobile_timer_init(void)
{
}
