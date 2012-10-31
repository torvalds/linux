/*
 * driver.c - driver support
 *
 * (C) 2006-2007 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *               Shaohua Li <shaohua.li@intel.com>
 *               Adam Belay <abelay@novell.com>
 *
 * This code is licenced under the GPL.
 */

#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/cpuidle.h>

#include "cpuidle.h"

static struct cpuidle_driver *cpuidle_curr_driver;
DEFINE_SPINLOCK(cpuidle_driver_lock);

static void set_power_states(struct cpuidle_driver *drv)
{
	int i;

	/*
	 * cpuidle driver should set the drv->power_specified bit
	 * before registering if the driver provides
	 * power_usage numbers.
	 *
	 * If power_specified is not set,
	 * we fill in power_usage with decreasing values as the
	 * cpuidle code has an implicit assumption that state Cn
	 * uses less power than C(n-1).
	 *
	 * With CONFIG_ARCH_HAS_CPU_RELAX, C0 is already assigned
	 * an power value of -1.  So we use -2, -3, etc, for other
	 * c-states.
	 */
	for (i = CPUIDLE_DRIVER_STATE_START; i < drv->state_count; i++)
		drv->states[i].power_usage = -1 - i;
}

static void __cpuidle_driver_init(struct cpuidle_driver *drv)
{
	drv->refcnt = 0;

	if (!drv->power_specified)
		set_power_states(drv);
}

static void cpuidle_set_driver(struct cpuidle_driver *drv)
{
	cpuidle_curr_driver = drv;
}

static int __cpuidle_register_driver(struct cpuidle_driver *drv)
{
	if (!drv || !drv->state_count)
		return -EINVAL;

	if (cpuidle_disabled())
		return -ENODEV;

	if (cpuidle_get_driver())
		return -EBUSY;

	__cpuidle_driver_init(drv);

	cpuidle_set_driver(drv);

	return 0;
}

static void __cpuidle_unregister_driver(struct cpuidle_driver *drv)
{
	if (drv != cpuidle_get_driver())
		return;

	if (!WARN_ON(drv->refcnt > 0))
		cpuidle_set_driver(NULL);
}

/**
 * cpuidle_register_driver - registers a driver
 * @drv: the driver
 */
int cpuidle_register_driver(struct cpuidle_driver *drv)
{
	int ret;

	spin_lock(&cpuidle_driver_lock);
	ret = __cpuidle_register_driver(drv);
	spin_unlock(&cpuidle_driver_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(cpuidle_register_driver);

/**
 * cpuidle_get_driver - return the current driver
 */
struct cpuidle_driver *cpuidle_get_driver(void)
{
	return cpuidle_curr_driver;
}
EXPORT_SYMBOL_GPL(cpuidle_get_driver);

/**
 * cpuidle_unregister_driver - unregisters a driver
 * @drv: the driver
 */
void cpuidle_unregister_driver(struct cpuidle_driver *drv)
{
	spin_lock(&cpuidle_driver_lock);
	__cpuidle_unregister_driver(drv);
	spin_unlock(&cpuidle_driver_lock);
}
EXPORT_SYMBOL_GPL(cpuidle_unregister_driver);

struct cpuidle_driver *cpuidle_driver_ref(void)
{
	struct cpuidle_driver *drv;

	spin_lock(&cpuidle_driver_lock);

	drv = cpuidle_get_driver();
	drv->refcnt++;

	spin_unlock(&cpuidle_driver_lock);
	return drv;
}

void cpuidle_driver_unref(void)
{
	struct cpuidle_driver *drv = cpuidle_get_driver();

	spin_lock(&cpuidle_driver_lock);

	if (drv && !WARN_ON(drv->refcnt <= 0))
		drv->refcnt--;

	spin_unlock(&cpuidle_driver_lock);
}
