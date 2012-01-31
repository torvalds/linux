/*
 * drivers/base/power/domain_governor.c - Governors for device PM domains.
 *
 * Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
 *
 * This file is released under the GPLv2.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pm_domain.h>
#include <linux/pm_qos.h>
#include <linux/hrtimer.h>

#ifdef CONFIG_PM_RUNTIME

/**
 * default_stop_ok - Default PM domain governor routine for stopping devices.
 * @dev: Device to check.
 */
bool default_stop_ok(struct device *dev)
{
	struct gpd_timing_data *td = &dev_gpd_data(dev)->td;

	dev_dbg(dev, "%s()\n", __func__);

	if (dev->power.max_time_suspended_ns < 0 || td->break_even_ns == 0)
		return true;

	return td->stop_latency_ns + td->start_latency_ns < td->break_even_ns
		&& td->break_even_ns < dev->power.max_time_suspended_ns;
}

/**
 * default_power_down_ok - Default generic PM domain power off governor routine.
 * @pd: PM domain to check.
 *
 * This routine must be executed under the PM domain's lock.
 */
static bool default_power_down_ok(struct dev_pm_domain *pd)
{
	struct generic_pm_domain *genpd = pd_to_genpd(pd);
	struct gpd_link *link;
	struct pm_domain_data *pdd;
	s64 min_dev_off_time_ns;
	s64 off_on_time_ns;
	ktime_t time_now = ktime_get();

	off_on_time_ns = genpd->power_off_latency_ns +
				genpd->power_on_latency_ns;
	/*
	 * It doesn't make sense to remove power from the domain if saving
	 * the state of all devices in it and the power off/power on operations
	 * take too much time.
	 *
	 * All devices in this domain have been stopped already at this point.
	 */
	list_for_each_entry(pdd, &genpd->dev_list, list_node) {
		if (pdd->dev->driver)
			off_on_time_ns +=
				to_gpd_data(pdd)->td.save_state_latency_ns;
	}

	/*
	 * Check if subdomains can be off for enough time.
	 *
	 * All subdomains have been powered off already at this point.
	 */
	list_for_each_entry(link, &genpd->master_links, master_node) {
		struct generic_pm_domain *sd = link->slave;
		s64 sd_max_off_ns = sd->max_off_time_ns;

		if (sd_max_off_ns < 0)
			continue;

		sd_max_off_ns -= ktime_to_ns(ktime_sub(time_now,
						       sd->power_off_time));
		/*
		 * Check if the subdomain is allowed to be off long enough for
		 * the current domain to turn off and on (that's how much time
		 * it will have to wait worst case).
		 */
		if (sd_max_off_ns <= off_on_time_ns)
			return false;
	}

	/*
	 * Check if the devices in the domain can be off enough time.
	 */
	min_dev_off_time_ns = -1;
	list_for_each_entry(pdd, &genpd->dev_list, list_node) {
		struct gpd_timing_data *td;
		struct device *dev = pdd->dev;
		s64 dev_off_time_ns;

		if (!dev->driver || dev->power.max_time_suspended_ns < 0)
			continue;

		td = &to_gpd_data(pdd)->td;
		dev_off_time_ns = dev->power.max_time_suspended_ns -
			(td->start_latency_ns + td->restore_state_latency_ns +
				ktime_to_ns(ktime_sub(time_now,
						dev->power.suspend_time)));
		if (dev_off_time_ns <= off_on_time_ns)
			return false;

		if (min_dev_off_time_ns > dev_off_time_ns
		    || min_dev_off_time_ns < 0)
			min_dev_off_time_ns = dev_off_time_ns;
	}

	if (min_dev_off_time_ns < 0) {
		/*
		 * There are no latency constraints, so the domain can spend
		 * arbitrary time in the "off" state.
		 */
		genpd->max_off_time_ns = -1;
		return true;
	}

	/*
	 * The difference between the computed minimum delta and the time needed
	 * to turn the domain on is the maximum theoretical time this domain can
	 * spend in the "off" state.
	 */
	min_dev_off_time_ns -= genpd->power_on_latency_ns;

	/*
	 * If the difference between the computed minimum delta and the time
	 * needed to turn the domain off and back on on is smaller than the
	 * domain's power break even time, removing power from the domain is not
	 * worth it.
	 */
	if (genpd->break_even_ns >
	    min_dev_off_time_ns - genpd->power_off_latency_ns)
		return false;

	genpd->max_off_time_ns = min_dev_off_time_ns;
	return true;
}

static bool always_on_power_down_ok(struct dev_pm_domain *domain)
{
	return false;
}

#else /* !CONFIG_PM_RUNTIME */

bool default_stop_ok(struct device *dev)
{
	return false;
}

#define default_power_down_ok	NULL
#define always_on_power_down_ok	NULL

#endif /* !CONFIG_PM_RUNTIME */

struct dev_power_governor simple_qos_governor = {
	.stop_ok = default_stop_ok,
	.power_down_ok = default_power_down_ok,
};

/**
 * pm_genpd_gov_always_on - A governor implementing an always-on policy
 */
struct dev_power_governor pm_domain_always_on_gov = {
	.power_down_ok = always_on_power_down_ok,
	.stop_ok = default_stop_ok,
};
