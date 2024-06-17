// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
// Copyright(c) 2015-2023 Intel Corporation

#include <linux/acpi.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include "cadence_master.h"
#include "bus.h"
#include "intel.h"

int intel_start_bus(struct sdw_intel *sdw)
{
	struct device *dev = sdw->cdns.dev;
	struct sdw_cdns *cdns = &sdw->cdns;
	struct sdw_bus *bus = &cdns->bus;
	int ret;

	/*
	 * follow recommended programming flows to avoid timeouts when
	 * gsync is enabled
	 */
	if (bus->multi_link)
		sdw_intel_sync_arm(sdw);

	ret = sdw_cdns_init(cdns);
	if (ret < 0) {
		dev_err(dev, "%s: unable to initialize Cadence IP: %d\n", __func__, ret);
		return ret;
	}

	sdw_cdns_config_update(cdns);

	if (bus->multi_link) {
		ret = sdw_intel_sync_go(sdw);
		if (ret < 0) {
			dev_err(dev, "%s: sync go failed: %d\n", __func__, ret);
			return ret;
		}
	}

	ret = sdw_cdns_config_update_set_wait(cdns);
	if (ret < 0) {
		dev_err(dev, "%s: CONFIG_UPDATE BIT still set\n", __func__);
		return ret;
	}

	ret = sdw_cdns_exit_reset(cdns);
	if (ret < 0) {
		dev_err(dev, "%s: unable to exit bus reset sequence: %d\n", __func__, ret);
		return ret;
	}

	ret = sdw_cdns_enable_interrupt(cdns, true);
	if (ret < 0) {
		dev_err(dev, "%s: cannot enable interrupts: %d\n", __func__, ret);
		return ret;
	}

	sdw_cdns_check_self_clearing_bits(cdns, __func__,
					  true, INTEL_MASTER_RESET_ITERATIONS);

	return 0;
}

int intel_start_bus_after_reset(struct sdw_intel *sdw)
{
	struct device *dev = sdw->cdns.dev;
	struct sdw_cdns *cdns = &sdw->cdns;
	struct sdw_bus *bus = &cdns->bus;
	bool clock_stop0;
	int status;
	int ret;

	/*
	 * An exception condition occurs for the CLK_STOP_BUS_RESET
	 * case if one or more masters remain active. In this condition,
	 * all the masters are powered on for they are in the same power
	 * domain. Master can preserve its context for clock stop0, so
	 * there is no need to clear slave status and reset bus.
	 */
	clock_stop0 = sdw_cdns_is_clock_stop(&sdw->cdns);

	if (!clock_stop0) {

		/*
		 * make sure all Slaves are tagged as UNATTACHED and
		 * provide reason for reinitialization
		 */

		status = SDW_UNATTACH_REQUEST_MASTER_RESET;
		sdw_clear_slave_status(bus, status);

		/*
		 * follow recommended programming flows to avoid
		 * timeouts when gsync is enabled
		 */
		if (bus->multi_link)
			sdw_intel_sync_arm(sdw);

		/*
		 * Re-initialize the IP since it was powered-off
		 */
		sdw_cdns_init(&sdw->cdns);

	} else {
		ret = sdw_cdns_enable_interrupt(cdns, true);
		if (ret < 0) {
			dev_err(dev, "cannot enable interrupts during resume\n");
			return ret;
		}
	}

	ret = sdw_cdns_clock_restart(cdns, !clock_stop0);
	if (ret < 0) {
		dev_err(dev, "unable to restart clock during resume\n");
		if (!clock_stop0)
			sdw_cdns_enable_interrupt(cdns, false);
		return ret;
	}

	if (!clock_stop0) {
		sdw_cdns_config_update(cdns);

		if (bus->multi_link) {
			ret = sdw_intel_sync_go(sdw);
			if (ret < 0) {
				dev_err(sdw->cdns.dev, "sync go failed during resume\n");
				return ret;
			}
		}

		ret = sdw_cdns_config_update_set_wait(cdns);
		if (ret < 0) {
			dev_err(dev, "%s: CONFIG_UPDATE BIT still set\n", __func__);
			return ret;
		}

		ret = sdw_cdns_exit_reset(cdns);
		if (ret < 0) {
			dev_err(dev, "unable to exit bus reset sequence during resume\n");
			return ret;
		}

		ret = sdw_cdns_enable_interrupt(cdns, true);
		if (ret < 0) {
			dev_err(dev, "cannot enable interrupts during resume\n");
			return ret;
		}

	}
	sdw_cdns_check_self_clearing_bits(cdns, __func__, true, INTEL_MASTER_RESET_ITERATIONS);

	return 0;
}

void intel_check_clock_stop(struct sdw_intel *sdw)
{
	struct device *dev = sdw->cdns.dev;
	bool clock_stop0;

	clock_stop0 = sdw_cdns_is_clock_stop(&sdw->cdns);
	if (!clock_stop0)
		dev_err(dev, "%s: invalid configuration, clock was not stopped\n", __func__);
}

int intel_start_bus_after_clock_stop(struct sdw_intel *sdw)
{
	struct device *dev = sdw->cdns.dev;
	struct sdw_cdns *cdns = &sdw->cdns;
	int ret;

	ret = sdw_cdns_clock_restart(cdns, false);
	if (ret < 0) {
		dev_err(dev, "%s: unable to restart clock: %d\n", __func__, ret);
		return ret;
	}

	ret = sdw_cdns_enable_interrupt(cdns, true);
	if (ret < 0) {
		dev_err(dev, "%s: cannot enable interrupts: %d\n", __func__, ret);
		return ret;
	}

	sdw_cdns_check_self_clearing_bits(cdns, __func__, true, INTEL_MASTER_RESET_ITERATIONS);

	return 0;
}

int intel_stop_bus(struct sdw_intel *sdw, bool clock_stop)
{
	struct device *dev = sdw->cdns.dev;
	struct sdw_cdns *cdns = &sdw->cdns;
	bool wake_enable = false;
	int ret;

	if (clock_stop) {
		ret = sdw_cdns_clock_stop(cdns, true);
		if (ret < 0)
			dev_err(dev, "%s: cannot stop clock: %d\n", __func__, ret);
		else
			wake_enable = true;
	}

	ret = sdw_cdns_enable_interrupt(cdns, false);
	if (ret < 0) {
		dev_err(dev, "%s: cannot disable interrupts: %d\n", __func__, ret);
		return ret;
	}

	ret = sdw_intel_link_power_down(sdw);
	if (ret) {
		dev_err(dev, "%s: Link power down failed: %d\n", __func__, ret);
		return ret;
	}

	sdw_intel_shim_wake(sdw, wake_enable);

	return 0;
}

/*
 * bank switch routines
 */

int intel_pre_bank_switch(struct sdw_intel *sdw)
{
	struct sdw_cdns *cdns = &sdw->cdns;
	struct sdw_bus *bus = &cdns->bus;

	/* Write to register only for multi-link */
	if (!bus->multi_link)
		return 0;

	sdw_intel_sync_arm(sdw);

	return 0;
}

int intel_post_bank_switch(struct sdw_intel *sdw)
{
	struct sdw_cdns *cdns = &sdw->cdns;
	struct sdw_bus *bus = &cdns->bus;
	int ret = 0;

	/* Write to register only for multi-link */
	if (!bus->multi_link)
		return 0;

	mutex_lock(sdw->link_res->shim_lock);

	/*
	 * post_bank_switch() ops is called from the bus in loop for
	 * all the Masters in the steam with the expectation that
	 * we trigger the bankswitch for the only first Master in the list
	 * and do nothing for the other Masters
	 *
	 * So, set the SYNCGO bit only if CMDSYNC bit is set for any Master.
	 */
	if (sdw_intel_sync_check_cmdsync_unlocked(sdw))
		ret = sdw_intel_sync_go_unlocked(sdw);

	mutex_unlock(sdw->link_res->shim_lock);

	if (ret < 0)
		dev_err(sdw->cdns.dev, "Post bank switch failed: %d\n", ret);

	return ret;
}
