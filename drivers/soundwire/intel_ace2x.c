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

/*
 * shim vendor-specific (vs) ops
 */

static void intel_shim_vs_init(struct sdw_intel *sdw)
{
	void __iomem *shim_vs = sdw->link_res->shim_vs;
	u16 act = 0;

	u16p_replace_bits(&act, 0x1, SDW_SHIM2_INTEL_VS_ACTMCTL_DOAIS);
	act |= SDW_SHIM2_INTEL_VS_ACTMCTL_DACTQE;
	act |=  SDW_SHIM2_INTEL_VS_ACTMCTL_DODS;
	intel_writew(shim_vs, SDW_SHIM2_INTEL_VS_ACTMCTL, act);
	usleep_range(10, 15);
}

static int intel_link_power_up(struct sdw_intel *sdw)
{
	struct sdw_bus *bus = &sdw->cdns.bus;
	struct sdw_master_prop *prop = &bus->prop;
	u32 *shim_mask = sdw->link_res->shim_mask;
	unsigned int link_id = sdw->instance;
	u32 syncprd;
	int ret;

	mutex_lock(sdw->link_res->shim_lock);

	if (!*shim_mask) {
		/* we first need to program the SyncPRD/CPU registers */
		dev_dbg(sdw->cdns.dev, "first link up, programming SYNCPRD\n");

		if (prop->mclk_freq % 6000000)
			syncprd = SDW_SHIM_SYNC_SYNCPRD_VAL_38_4;
		else
			syncprd = SDW_SHIM_SYNC_SYNCPRD_VAL_24;

		ret =  hdac_bus_eml_sdw_set_syncprd_unlocked(sdw->link_res->hbus, syncprd);
		if (ret < 0) {
			dev_err(sdw->cdns.dev, "%s: hdac_bus_eml_sdw_set_syncprd failed: %d\n",
				__func__, ret);
			goto out;
		}
	}

	ret = hdac_bus_eml_sdw_power_up_unlocked(sdw->link_res->hbus, link_id);
	if (ret < 0) {
		dev_err(sdw->cdns.dev, "%s: hdac_bus_eml_sdw_power_up failed: %d\n",
			__func__, ret);
		goto out;
	}

	if (!*shim_mask) {
		/* SYNCPU will change once link is active */
		ret =  hdac_bus_eml_sdw_wait_syncpu_unlocked(sdw->link_res->hbus);
		if (ret < 0) {
			dev_err(sdw->cdns.dev, "%s: hdac_bus_eml_sdw_wait_syncpu failed: %d\n",
				__func__, ret);
			goto out;
		}
	}

	*shim_mask |= BIT(link_id);

	sdw->cdns.link_up = true;

	intel_shim_vs_init(sdw);

out:
	mutex_unlock(sdw->link_res->shim_lock);

	return ret;
}

static int intel_link_power_down(struct sdw_intel *sdw)
{
	u32 *shim_mask = sdw->link_res->shim_mask;
	unsigned int link_id = sdw->instance;
	int ret;

	mutex_lock(sdw->link_res->shim_lock);

	sdw->cdns.link_up = false;

	*shim_mask &= ~BIT(link_id);

	ret = hdac_bus_eml_sdw_power_down_unlocked(sdw->link_res->hbus, link_id);
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
