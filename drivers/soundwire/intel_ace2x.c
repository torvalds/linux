// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
// Copyright(c) 2023 Intel Corporation. All rights reserved.

/*
 * Soundwire Intel ops for LunarLake
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include <sound/hda-mlink.h>
#include "cadence_master.h"
#include "bus.h"
#include "intel.h"

static int intel_link_power_up(struct sdw_intel *sdw)
{
	int ret;

	mutex_lock(sdw->link_res->shim_lock);

	ret = hdac_bus_eml_sdw_power_up_unlocked(sdw->link_res->hbus, sdw->instance);
	if (ret < 0) {
		dev_err(sdw->cdns.dev, "%s: hdac_bus_eml_sdw_power_up failed: %d\n",
			__func__, ret);
		goto out;
	}

	sdw->cdns.link_up = true;
out:
	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
}

static int intel_link_power_down(struct sdw_intel *sdw)
{
	int ret;

	mutex_lock(sdw->link_res->shim_lock);

	sdw->cdns.link_up = false;

	ret = hdac_bus_eml_sdw_power_down_unlocked(sdw->link_res->hbus, sdw->instance);
	if (ret < 0) {
		dev_err(sdw->cdns.dev, "%s: hdac_bus_eml_sdw_power_down failed: %d\n",
			__func__, ret);

		/*
		 * we leave the sdw->cdns.link_up flag as false since we've disabled
		 * the link at this point and cannot handle interrupts any longer.
		 */
	}

	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
}

const struct sdw_intel_hw_ops sdw_intel_lnl_hw_ops = {
	.debugfs_init = intel_ace2x_debugfs_init,
	.debugfs_exit = intel_ace2x_debugfs_exit,

	.link_power_up = intel_link_power_up,
	.link_power_down = intel_link_power_down,
};
EXPORT_SYMBOL_NS(sdw_intel_lnl_hw_ops, SOUNDWIRE_INTEL);

MODULE_IMPORT_NS(SND_SOC_SOF_HDA_MLINK);
