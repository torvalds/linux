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

struct dev_power_governor simple_qos_governor = {
	.stop_ok = default_stop_ok,
};
