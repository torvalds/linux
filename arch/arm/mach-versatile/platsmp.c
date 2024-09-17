// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This code is specific to the hardware found on ARM Realview and
 * Versatile Express platforms where the CPUs are unable to be individually
 * woken, and where there is no way to hot-unplug CPUs.  Real platforms
 * should not copy this code.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>

#include "platsmp.h"

/*
 * versatile_cpu_release controls the release of CPUs from the holding
 * pen in headsmp.S, which exists because we are not always able to
 * control the release of individual CPUs from the board firmware.
 * Production platforms do not need this.
 */
volatile int versatile_cpu_release = -1;

/*
 * Write versatile_cpu_release in a way that is guaranteed to be visible to
 * all observers, irrespective of whether they're taking part in coherency
 * or not.  This is necessary for the hotplug code to work reliably.
 */
static void versatile_write_cpu_release(int val)
{
	versatile_cpu_release = val;
	smp_wmb();
	sync_cache_w(&versatile_cpu_release);
}

/*
 * versatile_lock exists to avoid running the loops_per_jiffy delay loop
 * calibrations on the secondary CPU while the requesting CPU is using
 * the limited-bandwidth bus - which affects the calibration value.
 * Production platforms do not need this.
 */
static DEFINE_RAW_SPINLOCK(versatile_lock);

void versatile_secondary_init(unsigned int cpu)
{
	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	versatile_write_cpu_release(-1);

	/*
	 * Synchronise with the boot thread.
	 */
	raw_spin_lock(&versatile_lock);
	raw_spin_unlock(&versatile_lock);
}

int versatile_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;

	/*
	 * Set synchronisation state between this boot processor
	 * and the secondary one
	 */
	raw_spin_lock(&versatile_lock);

	/*
	 * This is really belt and braces; we hold unintended secondary
	 * CPUs in the holding pen until we're ready for them.  However,
	 * since we haven't sent them a soft interrupt, they shouldn't
	 * be there.
	 */
	versatile_write_cpu_release(cpu_logical_map(cpu));

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing
	 * the boot monitor to read the system wide flags register,
	 * and branch to the address found there.
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		smp_rmb();
		if (versatile_cpu_release == -1)
			break;

		udelay(10);
	}

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	raw_spin_unlock(&versatile_lock);

	return versatile_cpu_release != -1 ? -ENOSYS : 0;
}
