/*
 * omap-pm-noop.c - OMAP power management interface - dummy version
 *
 * This code implements the OMAP power management interface to
 * drivers, CPUIdle, CPUFreq, and DSP Bridge.  It is strictly for
 * debug/demonstration use, as it does nothing but printk() whenever a
 * function is called (when DEBUG is defined, below)
 *
 * Copyright (C) 2008-2009 Texas Instruments, Inc.
 * Copyright (C) 2008-2009 Nokia Corporation
 * Paul Walmsley
 *
 * Interface developed by (in alphabetical order):
 * Karthik Dasu, Tony Lindgren, Rajendra Nayak, Sakari Poussa, Veeramanikandan
 * Raju, Anand Sawant, Igor Stoppa, Paul Walmsley, Richard Woodruff
 */

#undef DEBUG

#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include "omap_device.h"
#include "omap-pm.h"

static bool off_mode_enabled;
static int dummy_context_loss_counter;

/*
 * Device-driver-originated constraints (via board-*.c files)
 */

int omap_pm_set_max_mpu_wakeup_lat(struct device *dev, long t)
{
	if (!dev || t < -1) {
		WARN(1, "OMAP PM: %s: invalid parameter(s)", __func__);
		return -EINVAL;
	}

	if (t == -1)
		pr_debug("OMAP PM: remove max MPU wakeup latency constraint: dev %s\n",
			 dev_name(dev));
	else
		pr_debug("OMAP PM: add max MPU wakeup latency constraint: dev %s, t = %ld usec\n",
			 dev_name(dev), t);

	/*
	 * For current Linux, this needs to map the MPU to a
	 * powerdomain, then go through the list of current max lat
	 * constraints on the MPU and find the smallest.  If
	 * the latency constraint has changed, the code should
	 * recompute the state to enter for the next powerdomain
	 * state.
	 *
	 * TI CDP code can call constraint_set here.
	 */

	return 0;
}

int omap_pm_set_min_bus_tput(struct device *dev, u8 agent_id, unsigned long r)
{
	if (!dev || (agent_id != OCP_INITIATOR_AGENT &&
	    agent_id != OCP_TARGET_AGENT)) {
		WARN(1, "OMAP PM: %s: invalid parameter(s)", __func__);
		return -EINVAL;
	}

	if (r == 0)
		pr_debug("OMAP PM: remove min bus tput constraint: dev %s for agent_id %d\n",
			 dev_name(dev), agent_id);
	else
		pr_debug("OMAP PM: add min bus tput constraint: dev %s for agent_id %d: rate %ld KiB\n",
			 dev_name(dev), agent_id, r);

	/*
	 * This code should model the interconnect and compute the
	 * required clock frequency, convert that to a VDD2 OPP ID, then
	 * set the VDD2 OPP appropriately.
	 *
	 * TI CDP code can call constraint_set here on the VDD2 OPP.
	 */

	return 0;
}

/*
 * DSP Bridge-specific constraints
 */


/**
 * omap_pm_enable_off_mode - notify OMAP PM that off-mode is enabled
 *
 * Intended for use only by OMAP PM core code to notify this layer
 * that off mode has been enabled.
 */
void omap_pm_enable_off_mode(void)
{
	off_mode_enabled = true;
}

/**
 * omap_pm_disable_off_mode - notify OMAP PM that off-mode is disabled
 *
 * Intended for use only by OMAP PM core code to notify this layer
 * that off mode has been disabled.
 */
void omap_pm_disable_off_mode(void)
{
	off_mode_enabled = false;
}

/*
 * Device context loss tracking
 */

#ifdef CONFIG_ARCH_OMAP2PLUS

int omap_pm_get_dev_context_loss_count(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int count;

	if (WARN_ON(!dev))
		return -ENODEV;

	if (dev->pm_domain == &omap_device_pm_domain) {
		count = omap_device_get_context_loss_count(pdev);
	} else {
		WARN_ONCE(off_mode_enabled, "omap_pm: using dummy context loss counter; device %s should be converted to omap_device",
			  dev_name(dev));

		count = dummy_context_loss_counter;

		if (off_mode_enabled) {
			count++;
			/*
			 * Context loss count has to be a non-negative value.
			 * Clear the sign bit to get a value range from 0 to
			 * INT_MAX.
			 */
			count &= INT_MAX;
			dummy_context_loss_counter = count;
		}
	}

	pr_debug("OMAP PM: context loss count for dev %s = %d\n",
		 dev_name(dev), count);

	return count;
}

#else

int omap_pm_get_dev_context_loss_count(struct device *dev)
{
	return dummy_context_loss_counter;
}

#endif

/* Should be called before clk framework init */
int __init omap_pm_if_early_init(void)
{
	return 0;
}

/* Must be called after clock framework is initialized */
int __init omap_pm_if_init(void)
{
	return 0;
}
