/*
 * drivers/base/power/domain_governor.c - Governors for device PM domains.
 *
 * Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
 *
 * This file is released under the GPLv2.
 */

#include <linux/kernel.h>
#include <linux/pm_domain.h>
#include <linux/pm_qos.h>
#include <linux/hrtimer.h>

static int dev_update_qos_constraint(struct device *dev, void *data)
{
	s64 *constraint_ns_p = data;
	s32 constraint_ns = -1;

	if (dev->power.subsys_data && dev->power.subsys_data->domain_data)
		constraint_ns = dev_gpd_data(dev)->td.effective_constraint_ns;

	if (constraint_ns < 0) {
		constraint_ns = dev_pm_qos_read_value(dev);
		constraint_ns *= NSEC_PER_USEC;
	}
	if (constraint_ns == 0)
		return 0;

	/*
	 * constraint_ns cannot be negative here, because the device has been
	 * suspended.
	 */
	if (constraint_ns < *constraint_ns_p || *constraint_ns_p == 0)
		*constraint_ns_p = constraint_ns;

	return 0;
}

/**
 * default_stop_ok - Default PM domain governor routine for stopping devices.
 * @dev: Device to check.
 */
static bool default_stop_ok(struct device *dev)
{
	struct gpd_timing_data *td = &dev_gpd_data(dev)->td;
	unsigned long flags;
	s64 constraint_ns;

	dev_dbg(dev, "%s()\n", __func__);

	spin_lock_irqsave(&dev->power.lock, flags);

	if (!td->constraint_changed) {
		bool ret = td->cached_stop_ok;

		spin_unlock_irqrestore(&dev->power.lock, flags);
		return ret;
	}
	td->constraint_changed = false;
	td->cached_stop_ok = false;
	td->effective_constraint_ns = -1;
	constraint_ns = __dev_pm_qos_read_value(dev);

	spin_unlock_irqrestore(&dev->power.lock, flags);

	if (constraint_ns < 0)
		return false;

	constraint_ns *= NSEC_PER_USEC;
	/*
	 * We can walk the children without any additional locking, because
	 * they all have been suspended at this point and their
	 * effective_constraint_ns fields won't be modified in parallel with us.
	 */
	if (!dev->power.ignore_children)
		device_for_each_child(dev, &constraint_ns,
				      dev_update_qos_constraint);

	if (constraint_ns > 0) {
		constraint_ns -= td->save_state_latency_ns +
				td->stop_latency_ns +
				td->start_latency_ns +
				td->restore_state_latency_ns;
		if (constraint_ns == 0)
			return false;
	}
	td->effective_constraint_ns = constraint_ns;
	td->cached_stop_ok = constraint_ns >= 0;

	/*
	 * The children have been suspended already, so we don't need to take
	 * their stop latencies into account here.
	 */
	return td->cached_stop_ok;
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
	s64 min_off_time_ns;
	s64 off_on_time_ns;

	if (genpd->max_off_time_changed) {
		struct gpd_link *link;

		/*
		 * We have to invalidate the cached results for the masters, so
		 * use the observation that default_power_down_ok() is not
		 * going to be called for any master until this instance
		 * returns.
		 */
		list_for_each_entry(link, &genpd->slave_links, slave_node)
			link->master->max_off_time_changed = true;

		genpd->max_off_time_changed = false;
		genpd->cached_power_down_ok = false;
		genpd->max_off_time_ns = -1;
	} else {
		return genpd->cached_power_down_ok;
	}

	off_on_time_ns = genpd->power_off_latency_ns +
				genpd->power_on_latency_ns;

	min_off_time_ns = -1;
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

		/*
		 * Check if the subdomain is allowed to be off long enough for
		 * the current domain to turn off and on (that's how much time
		 * it will have to wait worst case).
		 */
		if (sd_max_off_ns <= off_on_time_ns)
			return false;

		if (min_off_time_ns > sd_max_off_ns || min_off_time_ns < 0)
			min_off_time_ns = sd_max_off_ns;
	}

	/*
	 * Check if the devices in the domain can be off enough time.
	 */
	list_for_each_entry(pdd, &genpd->dev_list, list_node) {
		struct gpd_timing_data *td;
		s64 constraint_ns;

		if (!pdd->dev->driver)
			continue;

		/*
		 * Check if the device is allowed to be off long enough for the
		 * domain to turn off and on (that's how much time it will
		 * have to wait worst case).
		 */
		td = &to_gpd_data(pdd)->td;
		constraint_ns = td->effective_constraint_ns;
		/* default_stop_ok() need not be called before us. */
		if (constraint_ns < 0) {
			constraint_ns = dev_pm_qos_read_value(pdd->dev);
			constraint_ns *= NSEC_PER_USEC;
		}
		if (constraint_ns == 0)
			continue;

		/*
		 * constraint_ns cannot be negative here, because the device has
		 * been suspended.
		 */
		if (constraint_ns <= off_on_time_ns)
			return false;

		if (min_off_time_ns > constraint_ns || min_off_time_ns < 0)
			min_off_time_ns = constraint_ns;
	}

	genpd->cached_power_down_ok = true;

	/*
	 * If the computed minimum device off time is negative, there are no
	 * latency constraints, so the domain can spend arbitrary time in the
	 * "off" state.
	 */
	if (min_off_time_ns < 0)
		return true;

	/*
	 * The difference between the computed minimum subdomain or device off
	 * time and the time needed to turn the domain on is the maximum
	 * theoretical time this domain can spend in the "off" state.
	 */
	genpd->max_off_time_ns = min_off_time_ns - genpd->power_on_latency_ns;
	return true;
}

static bool always_on_power_down_ok(struct dev_pm_domain *domain)
{
	return false;
}

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
