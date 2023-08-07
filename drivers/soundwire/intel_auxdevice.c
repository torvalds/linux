// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-22 Intel Corporation.

/*
 * Soundwire Intel Manager Driver
 */

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/auxiliary_bus.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include "cadence_master.h"
#include "bus.h"
#include "intel.h"
#include "intel_auxdevice.h"

/* IDA min selected to avoid conflicts with HDaudio/iDISP SDI values */
#define INTEL_DEV_NUM_IDA_MIN           4

#define INTEL_MASTER_SUSPEND_DELAY_MS	3000

/*
 * debug/config flags for the Intel SoundWire Master.
 *
 * Since we may have multiple masters active, we can have up to 8
 * flags reused in each byte, with master0 using the ls-byte, etc.
 */

#define SDW_INTEL_MASTER_DISABLE_PM_RUNTIME		BIT(0)
#define SDW_INTEL_MASTER_DISABLE_CLOCK_STOP		BIT(1)
#define SDW_INTEL_MASTER_DISABLE_PM_RUNTIME_IDLE	BIT(2)
#define SDW_INTEL_MASTER_DISABLE_MULTI_LINK		BIT(3)

static int md_flags;
module_param_named(sdw_md_flags, md_flags, int, 0444);
MODULE_PARM_DESC(sdw_md_flags, "SoundWire Intel Master device flags (0x0 all off)");

static int generic_pre_bank_switch(struct sdw_bus *bus)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	struct sdw_intel *sdw = cdns_to_intel(cdns);

	return sdw->link_res->hw_ops->pre_bank_switch(sdw);
}

static int generic_post_bank_switch(struct sdw_bus *bus)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	struct sdw_intel *sdw = cdns_to_intel(cdns);

	return sdw->link_res->hw_ops->post_bank_switch(sdw);
}

static void generic_new_peripheral_assigned(struct sdw_bus *bus, int dev_num)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	struct sdw_intel *sdw = cdns_to_intel(cdns);

	/* paranoia check, this should never happen */
	if (dev_num < INTEL_DEV_NUM_IDA_MIN || dev_num > SDW_MAX_DEVICES)  {
		dev_err(bus->dev, "%s: invalid dev_num %d\n", __func__, dev_num);
		return;
	}

	if (sdw->link_res->hw_ops->program_sdi)
		sdw->link_res->hw_ops->program_sdi(sdw, dev_num);
}

static int sdw_master_read_intel_prop(struct sdw_bus *bus)
{
	struct sdw_master_prop *prop = &bus->prop;
	struct fwnode_handle *link;
	char name[32];
	u32 quirk_mask;

	/* Find master handle */
	snprintf(name, sizeof(name),
		 "mipi-sdw-link-%d-subproperties", bus->link_id);

	link = device_get_named_child_node(bus->dev, name);
	if (!link) {
		dev_err(bus->dev, "Master node %s not found\n", name);
		return -EIO;
	}

	fwnode_property_read_u32(link,
				 "intel-sdw-ip-clock",
				 &prop->mclk_freq);

	/* the values reported by BIOS are the 2x clock, not the bus clock */
	prop->mclk_freq /= 2;

	fwnode_property_read_u32(link,
				 "intel-quirk-mask",
				 &quirk_mask);

	if (quirk_mask & SDW_INTEL_QUIRK_MASK_BUS_DISABLE)
		prop->hw_disabled = true;

	prop->quirks = SDW_MASTER_QUIRKS_CLEAR_INITIAL_CLASH |
		SDW_MASTER_QUIRKS_CLEAR_INITIAL_PARITY;

	return 0;
}

static int intel_prop_read(struct sdw_bus *bus)
{
	/* Initialize with default handler to read all DisCo properties */
	sdw_master_read_prop(bus);

	/* read Intel-specific properties */
	sdw_master_read_intel_prop(bus);

	return 0;
}

static struct sdw_master_ops sdw_intel_ops = {
	.read_prop = intel_prop_read,
	.override_adr = sdw_dmi_override_adr,
	.xfer_msg = cdns_xfer_msg,
	.xfer_msg_defer = cdns_xfer_msg_defer,
	.set_bus_conf = cdns_bus_conf,
	.pre_bank_switch = generic_pre_bank_switch,
	.post_bank_switch = generic_post_bank_switch,
	.read_ping_status = cdns_read_ping_status,
	.new_peripheral_assigned = generic_new_peripheral_assigned,
};

/*
 * probe and init (aux_dev_id argument is required by function prototype but not used)
 */
static int intel_link_probe(struct auxiliary_device *auxdev,
			    const struct auxiliary_device_id *aux_dev_id)

{
	struct device *dev = &auxdev->dev;
	struct sdw_intel_link_dev *ldev = auxiliary_dev_to_sdw_intel_link_dev(auxdev);
	struct sdw_intel *sdw;
	struct sdw_cdns *cdns;
	struct sdw_bus *bus;
	int ret;

	sdw = devm_kzalloc(dev, sizeof(*sdw), GFP_KERNEL);
	if (!sdw)
		return -ENOMEM;

	cdns = &sdw->cdns;
	bus = &cdns->bus;

	sdw->instance = auxdev->id;
	sdw->link_res = &ldev->link_res;
	cdns->dev = dev;
	cdns->registers = sdw->link_res->registers;
	cdns->ip_offset = sdw->link_res->ip_offset;
	cdns->instance = sdw->instance;
	cdns->msg_count = 0;

	bus->link_id = auxdev->id;
	bus->dev_num_ida_min = INTEL_DEV_NUM_IDA_MIN;
	bus->clk_stop_timeout = 1;

	sdw_cdns_probe(cdns);

	/* Set ops */
	bus->ops = &sdw_intel_ops;

	/* set driver data, accessed by snd_soc_dai_get_drvdata() */
	auxiliary_set_drvdata(auxdev, cdns);

	/* use generic bandwidth allocation algorithm */
	sdw->cdns.bus.compute_params = sdw_compute_params;

	/* avoid resuming from pm_runtime suspend if it's not required */
	dev_pm_set_driver_flags(dev, DPM_FLAG_SMART_SUSPEND);

	ret = sdw_bus_master_add(bus, dev, dev->fwnode);
	if (ret) {
		dev_err(dev, "sdw_bus_master_add fail: %d\n", ret);
		return ret;
	}

	if (bus->prop.hw_disabled)
		dev_info(dev,
			 "SoundWire master %d is disabled, will be ignored\n",
			 bus->link_id);
	/*
	 * Ignore BIOS err_threshold, it's a really bad idea when dealing
	 * with multiple hardware synchronized links
	 */
	bus->prop.err_threshold = 0;

	return 0;
}

int intel_link_startup(struct auxiliary_device *auxdev)
{
	struct device *dev = &auxdev->dev;
	struct sdw_cdns *cdns = auxiliary_get_drvdata(auxdev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	int link_flags;
	bool multi_link;
	u32 clock_stop_quirks;
	int ret;

	if (bus->prop.hw_disabled) {
		dev_info(dev,
			 "SoundWire master %d is disabled, ignoring\n",
			 sdw->instance);
		return 0;
	}

	link_flags = md_flags >> (bus->link_id * 8);
	multi_link = !(link_flags & SDW_INTEL_MASTER_DISABLE_MULTI_LINK);
	if (!multi_link) {
		dev_dbg(dev, "Multi-link is disabled\n");
	} else {
		/*
		 * hardware-based synchronization is required regardless
		 * of the number of segments used by a stream: SSP-based
		 * synchronization is gated by gsync when the multi-master
		 * mode is set.
		 */
		bus->hw_sync_min_links = 1;
	}
	bus->multi_link = multi_link;

	/* Initialize shim, controller */
	ret = sdw_intel_link_power_up(sdw);
	if (ret)
		goto err_init;

	/* Register DAIs */
	ret = sdw_intel_register_dai(sdw);
	if (ret) {
		dev_err(dev, "DAI registration failed: %d\n", ret);
		goto err_power_up;
	}

	sdw_intel_debugfs_init(sdw);

	/* start bus */
	ret = sdw_intel_start_bus(sdw);
	if (ret) {
		dev_err(dev, "bus start failed: %d\n", ret);
		goto err_power_up;
	}

	/* Enable runtime PM */
	if (!(link_flags & SDW_INTEL_MASTER_DISABLE_PM_RUNTIME)) {
		pm_runtime_set_autosuspend_delay(dev,
						 INTEL_MASTER_SUSPEND_DELAY_MS);
		pm_runtime_use_autosuspend(dev);
		pm_runtime_mark_last_busy(dev);

		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	clock_stop_quirks = sdw->link_res->clock_stop_quirks;
	if (clock_stop_quirks & SDW_INTEL_CLK_STOP_NOT_ALLOWED) {
		/*
		 * To keep the clock running we need to prevent
		 * pm_runtime suspend from happening by increasing the
		 * reference count.
		 * This quirk is specified by the parent PCI device in
		 * case of specific latency requirements. It will have
		 * no effect if pm_runtime is disabled by the user via
		 * a module parameter for testing purposes.
		 */
		pm_runtime_get_noresume(dev);
	}

	/*
	 * The runtime PM status of Slave devices is "Unsupported"
	 * until they report as ATTACHED. If they don't, e.g. because
	 * there are no Slave devices populated or if the power-on is
	 * delayed or dependent on a power switch, the Master will
	 * remain active and prevent its parent from suspending.
	 *
	 * Conditionally force the pm_runtime core to re-evaluate the
	 * Master status in the absence of any Slave activity. A quirk
	 * is provided to e.g. deal with Slaves that may be powered on
	 * with a delay. A more complete solution would require the
	 * definition of Master properties.
	 */
	if (!(link_flags & SDW_INTEL_MASTER_DISABLE_PM_RUNTIME_IDLE))
		pm_runtime_idle(dev);

	sdw->startup_done = true;
	return 0;

err_power_up:
	sdw_intel_link_power_down(sdw);
err_init:
	return ret;
}

static void intel_link_remove(struct auxiliary_device *auxdev)
{
	struct sdw_cdns *cdns = auxiliary_get_drvdata(auxdev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;

	/*
	 * Since pm_runtime is already disabled, we don't decrease
	 * the refcount when the clock_stop_quirk is
	 * SDW_INTEL_CLK_STOP_NOT_ALLOWED
	 */
	if (!bus->prop.hw_disabled) {
		sdw_intel_debugfs_exit(sdw);
		sdw_cdns_enable_interrupt(cdns, false);
	}
	sdw_bus_master_delete(bus);
}

int intel_link_process_wakeen_event(struct auxiliary_device *auxdev)
{
	struct device *dev = &auxdev->dev;
	struct sdw_intel *sdw;
	struct sdw_bus *bus;

	sdw = auxiliary_get_drvdata(auxdev);
	bus = &sdw->cdns.bus;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	if (!sdw_intel_shim_check_wake(sdw))
		return 0;

	/* disable WAKEEN interrupt ASAP to prevent interrupt flood */
	sdw_intel_shim_wake(sdw, false);

	/*
	 * resume the Master, which will generate a bus reset and result in
	 * Slaves re-attaching and be re-enumerated. The SoundWire physical
	 * device which generated the wake will trigger an interrupt, which
	 * will in turn cause the corresponding Linux Slave device to be
	 * resumed and the Slave codec driver to check the status.
	 */
	pm_request_resume(dev);

	return 0;
}

/*
 * PM calls
 */

static int intel_resume_child_device(struct device *dev, void *data)
{
	int ret;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);

	if (!slave->probed) {
		dev_dbg(dev, "skipping device, no probed driver\n");
		return 0;
	}
	if (!slave->dev_num_sticky) {
		dev_dbg(dev, "skipping device, never detected on bus\n");
		return 0;
	}

	ret = pm_request_resume(dev);
	if (ret < 0) {
		dev_err(dev, "%s: pm_request_resume failed: %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int __maybe_unused intel_pm_prepare(struct device *dev)
{
	struct sdw_cdns *cdns = dev_get_drvdata(dev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	u32 clock_stop_quirks;
	int ret;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	clock_stop_quirks = sdw->link_res->clock_stop_quirks;

	if (pm_runtime_suspended(dev) &&
	    pm_runtime_suspended(dev->parent) &&
	    ((clock_stop_quirks & SDW_INTEL_CLK_STOP_BUS_RESET) ||
	     !clock_stop_quirks)) {
		/*
		 * if we've enabled clock stop, and the parent is suspended, the SHIM registers
		 * are not accessible and the shim wake cannot be disabled.
		 * The only solution is to resume the entire bus to full power
		 */

		/*
		 * If any operation in this block fails, we keep going since we don't want
		 * to prevent system suspend from happening and errors should be recoverable
		 * on resume.
		 */

		/*
		 * first resume the device for this link. This will also by construction
		 * resume the PCI parent device.
		 */
		ret = pm_request_resume(dev);
		if (ret < 0) {
			dev_err(dev, "%s: pm_request_resume failed: %d\n", __func__, ret);
			return 0;
		}

		/*
		 * Continue resuming the entire bus (parent + child devices) to exit
		 * the clock stop mode. If there are no devices connected on this link
		 * this is a no-op.
		 * The resume to full power could have been implemented with a .prepare
		 * step in SoundWire codec drivers. This would however require a lot
		 * of code to handle an Intel-specific corner case. It is simpler in
		 * practice to add a loop at the link level.
		 */
		ret = device_for_each_child(bus->dev, NULL, intel_resume_child_device);

		if (ret < 0)
			dev_err(dev, "%s: intel_resume_child_device failed: %d\n", __func__, ret);
	}

	return 0;
}

static int __maybe_unused intel_suspend(struct device *dev)
{
	struct sdw_cdns *cdns = dev_get_drvdata(dev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	u32 clock_stop_quirks;
	int ret;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	if (pm_runtime_suspended(dev)) {
		dev_dbg(dev, "pm_runtime status: suspended\n");

		clock_stop_quirks = sdw->link_res->clock_stop_quirks;

		if ((clock_stop_quirks & SDW_INTEL_CLK_STOP_BUS_RESET) ||
		    !clock_stop_quirks) {

			if (pm_runtime_suspended(dev->parent)) {
				/*
				 * paranoia check: this should not happen with the .prepare
				 * resume to full power
				 */
				dev_err(dev, "%s: invalid config: parent is suspended\n", __func__);
			} else {
				sdw_intel_shim_wake(sdw, false);
			}
		}

		return 0;
	}

	ret = sdw_intel_stop_bus(sdw, false);
	if (ret < 0) {
		dev_err(dev, "%s: cannot stop bus: %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int __maybe_unused intel_suspend_runtime(struct device *dev)
{
	struct sdw_cdns *cdns = dev_get_drvdata(dev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	u32 clock_stop_quirks;
	int ret;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	clock_stop_quirks = sdw->link_res->clock_stop_quirks;

	if (clock_stop_quirks & SDW_INTEL_CLK_STOP_TEARDOWN) {
		ret = sdw_intel_stop_bus(sdw, false);
		if (ret < 0) {
			dev_err(dev, "%s: cannot stop bus during teardown: %d\n",
				__func__, ret);
			return ret;
		}
	} else if (clock_stop_quirks & SDW_INTEL_CLK_STOP_BUS_RESET || !clock_stop_quirks) {
		ret = sdw_intel_stop_bus(sdw, true);
		if (ret < 0) {
			dev_err(dev, "%s: cannot stop bus during clock_stop: %d\n",
				__func__, ret);
			return ret;
		}
	} else {
		dev_err(dev, "%s clock_stop_quirks %x unsupported\n",
			__func__, clock_stop_quirks);
		ret = -EINVAL;
	}

	return ret;
}

static int __maybe_unused intel_resume(struct device *dev)
{
	struct sdw_cdns *cdns = dev_get_drvdata(dev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	int link_flags;
	int ret;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	link_flags = md_flags >> (bus->link_id * 8);

	if (pm_runtime_suspended(dev)) {
		dev_dbg(dev, "pm_runtime status was suspended, forcing active\n");

		/* follow required sequence from runtime_pm.rst */
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_mark_last_busy(dev);
		pm_runtime_enable(dev);

		link_flags = md_flags >> (bus->link_id * 8);

		if (!(link_flags & SDW_INTEL_MASTER_DISABLE_PM_RUNTIME_IDLE))
			pm_runtime_idle(dev);
	}

	ret = sdw_intel_link_power_up(sdw);
	if (ret) {
		dev_err(dev, "%s failed: %d\n", __func__, ret);
		return ret;
	}

	/*
	 * make sure all Slaves are tagged as UNATTACHED and provide
	 * reason for reinitialization
	 */
	sdw_clear_slave_status(bus, SDW_UNATTACH_REQUEST_MASTER_RESET);

	ret = sdw_intel_start_bus(sdw);
	if (ret < 0) {
		dev_err(dev, "cannot start bus during resume\n");
		sdw_intel_link_power_down(sdw);
		return ret;
	}

	/*
	 * after system resume, the pm_runtime suspend() may kick in
	 * during the enumeration, before any children device force the
	 * master device to remain active.  Using pm_runtime_get()
	 * routines is not really possible, since it'd prevent the
	 * master from suspending.
	 * A reasonable compromise is to update the pm_runtime
	 * counters and delay the pm_runtime suspend by several
	 * seconds, by when all enumeration should be complete.
	 */
	pm_runtime_mark_last_busy(dev);

	return 0;
}

static int __maybe_unused intel_resume_runtime(struct device *dev)
{
	struct sdw_cdns *cdns = dev_get_drvdata(dev);
	struct sdw_intel *sdw = cdns_to_intel(cdns);
	struct sdw_bus *bus = &cdns->bus;
	u32 clock_stop_quirks;
	int ret;

	if (bus->prop.hw_disabled || !sdw->startup_done) {
		dev_dbg(dev, "SoundWire master %d is disabled or not-started, ignoring\n",
			bus->link_id);
		return 0;
	}

	/* unconditionally disable WAKEEN interrupt */
	sdw_intel_shim_wake(sdw, false);

	clock_stop_quirks = sdw->link_res->clock_stop_quirks;

	if (clock_stop_quirks & SDW_INTEL_CLK_STOP_TEARDOWN) {
		ret = sdw_intel_link_power_up(sdw);
		if (ret) {
			dev_err(dev, "%s: power_up failed after teardown: %d\n", __func__, ret);
			return ret;
		}

		/*
		 * make sure all Slaves are tagged as UNATTACHED and provide
		 * reason for reinitialization
		 */
		sdw_clear_slave_status(bus, SDW_UNATTACH_REQUEST_MASTER_RESET);

		ret = sdw_intel_start_bus(sdw);
		if (ret < 0) {
			dev_err(dev, "%s: cannot start bus after teardown: %d\n", __func__, ret);
			sdw_intel_link_power_down(sdw);
			return ret;
		}

	} else if (clock_stop_quirks & SDW_INTEL_CLK_STOP_BUS_RESET) {
		ret = sdw_intel_link_power_up(sdw);
		if (ret) {
			dev_err(dev, "%s: power_up failed after bus reset: %d\n", __func__, ret);
			return ret;
		}

		ret = sdw_intel_start_bus_after_reset(sdw);
		if (ret < 0) {
			dev_err(dev, "%s: cannot start bus after reset: %d\n", __func__, ret);
			sdw_intel_link_power_down(sdw);
			return ret;
		}
	} else if (!clock_stop_quirks) {

		sdw_intel_check_clock_stop(sdw);

		ret = sdw_intel_link_power_up(sdw);
		if (ret) {
			dev_err(dev, "%s: power_up failed: %d\n", __func__, ret);
			return ret;
		}

		ret = sdw_intel_start_bus_after_clock_stop(sdw);
		if (ret < 0) {
			dev_err(dev, "%s: cannot start bus after clock stop: %d\n", __func__, ret);
			sdw_intel_link_power_down(sdw);
			return ret;
		}
	} else {
		dev_err(dev, "%s: clock_stop_quirks %x unsupported\n",
			__func__, clock_stop_quirks);
		ret = -EINVAL;
	}

	return ret;
}

static const struct dev_pm_ops intel_pm = {
	.prepare = intel_pm_prepare,
	SET_SYSTEM_SLEEP_PM_OPS(intel_suspend, intel_resume)
	SET_RUNTIME_PM_OPS(intel_suspend_runtime, intel_resume_runtime, NULL)
};

static const struct auxiliary_device_id intel_link_id_table[] = {
	{ .name = "soundwire_intel.link" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, intel_link_id_table);

static struct auxiliary_driver sdw_intel_drv = {
	.probe = intel_link_probe,
	.remove = intel_link_remove,
	.driver = {
		/* auxiliary_driver_register() sets .name to be the modname */
		.pm = &intel_pm,
	},
	.id_table = intel_link_id_table
};
module_auxiliary_driver(sdw_intel_drv);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Intel Soundwire Link Driver");
